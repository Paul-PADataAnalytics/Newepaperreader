#include "AppComm.h"
#include "EBookmarkManager.h"
#include "PowerManager.h"
#include "SystemSettings.h"
#include "DisplayHAL.h"
#include "reader/AppStorage.h"
#include "reader/FileBrowser.h"
#include <ArduinoJson.h>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <SD.h>
#include <sys/time.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#define COMM_LOG(...) Serial.printf(__VA_ARGS__)
#else
#include <cstdio>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#define COMM_LOG(...) printf(__VA_ARGS__)
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

// ---------------------------------------------------------------------------
// Protocol constants (keep in sync with android_app/lib/services/ble_service.dart
// and manual/09_power_and_companion.md)
// ---------------------------------------------------------------------------
static const int PROTOCOL_VERSION = 2;
static const char* FIRMWARE_VERSION = "3.0.0";
static const uint8_t FRAME_FILE_DATA = 0x01;
static const size_t INBOX_CAPACITY = 32;
static const size_t RESPONSE_BUDGET = 460; // stays under the 512-byte ATT value cap
static const uint32_t UPLOAD_STALL_TIMEOUT_MS = 30000;

// ---------------------------------------------------------------------------
// Inbox shared between the radio task and the main loop
// ---------------------------------------------------------------------------
static std::mutex g_inboxMutex;
static std::deque<std::string> g_inbox;
static bool is_comm_init = false;
static volatile bool g_peerDropped = false;

static uint32_t s_bookmarksRev = 0;
static uint32_t s_libraryRev = 0;
static std::string s_lastEvent;

// Pushes one received message. Blocks the radio task (up to ~5s) while the
// inbox is full: the ATT write response is delayed, which throttles the
// phone instead of silently dropping file chunks.
static bool inboxPush(const std::string& msg) {
    for (int attempt = 0; attempt < 1000; attempt++) {
        if (!is_comm_init) return false;
        {
            std::lock_guard<std::mutex> lock(g_inboxMutex);
            if (g_inbox.size() < INBOX_CAPACITY) {
                g_inbox.push_back(msg);
                return true;
            }
        }
#ifndef NATIVE_TESTING
        vTaskDelay(pdMS_TO_TICKS(5));
#else
        usleep(5000);
#endif
    }
    return false;
}

static void inboxClear() {
    std::lock_guard<std::mutex> lock(g_inboxMutex);
    g_inbox.clear();
}

static bool inboxPop(std::string& out) {
    std::lock_guard<std::mutex> lock(g_inboxMutex);
    if (g_inbox.empty()) return false;
    out.swap(g_inbox.front());
    g_inbox.pop_front();
    return true;
}

// ---------------------------------------------------------------------------
// Platform transport
// ---------------------------------------------------------------------------
static void transportRespond(const std::string& json);
static void transportNotifyDelta(const std::string& json);
static bool transportConnected();
#ifdef NATIVE_TESTING
static void nativePump();
#endif

// ---------------------------------------------------------------------------
// Small file helpers (SD-root-relative paths like "/books/foo.epub")
// ---------------------------------------------------------------------------
namespace {

#ifndef NATIVE_TESTING
File g_uploadFile;
#else
FILE* g_uploadFile = nullptr;
#endif
// Upload data is collected into 16KB blocks: many tiny SD writes are far
// slower than a few large ones.
std::vector<uint8_t> g_uploadBuf;
const size_t UPLOAD_BLOCK = 16384;

bool fsExists(const std::string& path) {
#ifndef NATIVE_TESTING
    return SD.exists(path.c_str());
#else
    return access(AppStorage::toRuntimePath(path).c_str(), F_OK) == 0;
#endif
}

bool fsRemove(const std::string& path) {
#ifndef NATIVE_TESTING
    return SD.remove(path.c_str());
#else
    return remove(AppStorage::toRuntimePath(path).c_str()) == 0;
#endif
}

bool fsRename(const std::string& from, const std::string& to) {
#ifndef NATIVE_TESTING
    return SD.rename(from.c_str(), to.c_str());
#else
    return rename(AppStorage::toRuntimePath(from).c_str(), AppStorage::toRuntimePath(to).c_str()) == 0;
#endif
}

void fsEnsureDir(const std::string& dir) {
#ifndef NATIVE_TESTING
    if (!SD.exists(dir.c_str())) SD.mkdir(dir.c_str());
#else
    (void)dir;
#endif
}

bool uploadOpen(const std::string& path) {
#ifndef NATIVE_TESTING
    g_uploadFile = SD.open(path.c_str(), FILE_WRITE);
    return (bool)g_uploadFile;
#else
    g_uploadFile = fopen(AppStorage::toRuntimePath(path).c_str(), "wb");
    return g_uploadFile != nullptr;
#endif
}

bool uploadFlush() {
    if (g_uploadBuf.empty()) return true;
#ifndef NATIVE_TESTING
    size_t n = g_uploadFile ? g_uploadFile.write(g_uploadBuf.data(), g_uploadBuf.size()) : 0;
#else
    size_t n = g_uploadFile ? fwrite(g_uploadBuf.data(), 1, g_uploadBuf.size(), g_uploadFile) : 0;
#endif
    bool ok = n == g_uploadBuf.size();
    g_uploadBuf.clear();
    return ok;
}

size_t uploadWrite(const uint8_t* data, size_t len) {
    if (!g_uploadFile) return 0;
    g_uploadBuf.insert(g_uploadBuf.end(), data, data + len);
    if (g_uploadBuf.size() >= UPLOAD_BLOCK && !uploadFlush()) return 0;
    return len;
}

// Returns false if buffered data could not be written.
bool uploadClose() {
    bool ok = uploadFlush();
#ifndef NATIVE_TESTING
    if (g_uploadFile) g_uploadFile.close();
#else
    if (g_uploadFile) fclose(g_uploadFile);
    g_uploadFile = nullptr;
#endif
    return ok;
}

bool hasBookExtension(const std::string& name) {
    std::string lower = name;
    for (char& c : lower) c = (char)tolower((unsigned char)c);
    auto ends = [&](const char* ext) {
        size_t n = strlen(ext);
        return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
    };
    return ends(".epub") || ends(".txt") || ends(".md") || ends(".rtf");
}

bool isSafeFileName(const std::string& name) {
    if (name.empty() || name.size() > 120) return false;
    if (name[0] == '.') return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    if (name.find("..") != std::string::npos) return false;
    for (char c : name) {
        if ((unsigned char)c < 0x20) return false;
    }
    return true;
}

std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

uint32_t nowEpoch() {
#ifndef NATIVE_TESTING
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
#else
    return (uint32_t)time(nullptr);
#endif
}

// Packs as many pre-serialized items as fit in one response, starting at `off`.
std::string buildPage(const char* type, int rid, const std::vector<std::string>& items, int off) {
    if (off < 0) off = 0;
    std::string head = std::string("{\"t\":\"") + type + "\",\"rid\":" + std::to_string(rid) +
                       ",\"total\":" + std::to_string(items.size()) + ",\"off\":" + std::to_string(off) +
                       ",\"items\":[";
    std::string body;
    size_t i = (size_t)off;
    while (i < items.size()) {
        size_t add = items[i].size() + (body.empty() ? 0 : 1);
        if (!body.empty() && head.size() + body.size() + add + 16 > RESPONSE_BUDGET) break;
        if (!body.empty()) body += ',';
        body += items[i];
        i++;
    }
    int next = i < items.size() ? (int)i : -1;
    return head + body + "],\"next\":" + std::to_string(next) + "}";
}

// --- Library listing --------------------------------------------------------

std::vector<AppStorage::LibraryIndexEntry> g_library;
bool g_libraryValid = false;

void invalidateLibrary() {
    g_libraryValid = false;
    s_libraryRev++;
    AppStorage::markLibraryDirty();
}

const std::vector<AppStorage::LibraryIndexEntry>& getLibrary() {
    if (g_libraryValid) return g_library;

    AppStorage::LibraryIndex cached = AppStorage::loadLibraryIndex("/books");
    if (cached.valid) {
        g_library = cached.entries;
    } else {
        // The e-reader's cached index is only trusted after a sleep-resume,
        // so on a fresh boot do a real scan rather than reporting nothing.
        g_library.clear();
        FileBrowser browser;
        std::vector<FileInfo> files = browser.scanRecursive("/books");
        for (const auto& f : files) {
            if (f.isDirectory || !hasBookExtension(f.name)) continue;
            AppStorage::LibraryIndexEntry e;
            e.path = f.path;
            e.size = (uint32_t)f.size;
            e.mtime = f.mtime;
            g_library.push_back(e);
        }
        AppStorage::saveLibraryIndex("/books", g_library);
    }
    g_libraryValid = true;
    return g_library;
}

// --- Upload state machine ---------------------------------------------------

struct UploadState {
    bool active = false;
    std::string name;
    std::string partPath;
    std::string finalPath;
    uint32_t expected = 0;
    uint32_t written = 0;
    bool failed = false;
    std::string error;
    uint32_t lastActivityMs = 0;
};
UploadState g_upload;

void uploadReset() {
    g_uploadBuf.clear();
    uploadClose();
    if (g_upload.active && !g_upload.partPath.empty() && fsExists(g_upload.partPath)) {
        fsRemove(g_upload.partPath);
    }
    g_upload = UploadState();
    PowerManager::setBusy(false);
}

void respond(JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    transportRespond(out);
}

void respondError(const char* type, int rid, const std::string& error) {
    JsonDocument doc;
    doc["t"] = type;
    doc["rid"] = rid;
    doc["ok"] = false;
    doc["error"] = error;
    respond(doc);
}

void handleFileData(const std::string& frame) {
    if (!g_upload.active) return;
    if (frame.size() < 5) return;
    const uint8_t* p = (const uint8_t*)frame.data();
    uint32_t offset = (uint32_t)p[1] | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
    size_t len = frame.size() - 5;
    g_upload.lastActivityMs = millis();
    PowerManager::noteUserActivity();

    if (g_upload.failed) return;
    if (offset < g_upload.written) return; // retransmitted chunk, already stored
    if (offset > g_upload.written) {
        g_upload.failed = true;
        g_upload.error = "Missing data at offset " + std::to_string(g_upload.written);
        return;
    }
    if (g_upload.written + len > g_upload.expected) {
        g_upload.failed = true;
        g_upload.error = "More data than announced";
        return;
    }
    if (uploadWrite(p + 5, len) != len) {
        g_upload.failed = true;
        g_upload.error = "SD card write failed";
        return;
    }
    g_upload.written += (uint32_t)len;
}

void handleJson(const std::string& msg) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) return;
    std::string type = doc["t"] | "";
    int rid = doc["rid"] | 0;

    // Background bookmark polling must not keep the device awake; anything
    // else is the user actively doing something in the companion app.
    if (type != "GET_BOOKMARKS" && type != "TIME") {
        PowerManager::noteUserActivity();
    }

    if (type == "HELLO") {
        JsonDocument r;
        r["t"] = "HELLO";
        r["rid"] = rid;
        r["proto"] = PROTOCOL_VERSION;
        r["fw"] = FIRMWARE_VERSION;
        r["batt"] = DisplayHAL::getBatteryPercent();
        r["usb"] = PowerManager::isUsbConnected();
        r["usbHost"] = PowerManager::isUsbHostActive();
        r["lastSleep"] = PowerManager::lastSleepSummary();
#ifndef NATIVE_TESTING
        r["free"] = (double)(SD.totalBytes() - SD.usedBytes());
#endif
        respond(r);
        s_lastEvent = "Companion app connected";
    } else if (type == "TIME") {
        uint32_t val = doc["val"] | 0;
        if (doc["tz"].is<int>()) {
            int tz = doc["tz"] | 0;
            if (tz != SystemSettings::get().tzOffsetMin) {
                SystemSettings::get().tzOffsetMin = tz;
                SystemSettings::applyTimezone();
                SystemSettings::save();
            }
        }
        if (val > 0) {
#ifndef NATIVE_TESTING
            struct timeval tv;
            tv.tv_sec = val;
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
#endif
            COMM_LOG("AppComm: clock set to %u\n", (unsigned)val);
        }
        if (rid) {
            JsonDocument r;
            r["t"] = "TIME";
            r["rid"] = rid;
            r["ok"] = true;
            respond(r);
        }
    } else if (type == "BOOK") {
        std::string isbn = doc["isbn"] | "";
        if (isbn.empty()) return;
        std::string title = doc["title"] | "";
        std::string author = doc["author"] | "";
        std::string genre = doc["genre"] | "";
        int page = doc["page"] | 0;
        int total = doc["total"] | 0;
        uint32_t ts = doc["ts"] | 0;

        EBookmarkManager& mgr = EBookmarkManager::getInstance();
        mgr.initialize();
        // Last-change-wins per book.
        uint32_t localTs = 0;
        for (const auto& b : mgr.getBookmarks()) {
            if (b.isbn == isbn && !b.history.empty()) {
                localTs = b.history.back().timestamp;
                break;
            }
        }
        bool applied = false;
        if (ts == 0 || ts >= localTs) {
            mgr.addOrUpdateBook(isbn, title, author, genre, page, total, ts ? ts : nowEpoch());
            s_bookmarksRev++;
            applied = true;
            s_lastEvent = "Synced \"" + title + "\"";
        }
        if (rid) {
            JsonDocument r;
            r["t"] = "BOOK";
            r["rid"] = rid;
            r["ok"] = true;
            r["applied"] = applied;
            respond(r);
        }
    } else if (type == "GET_BOOKMARKS") {
        EBookmarkManager& mgr = EBookmarkManager::getInstance();
        mgr.initialize();
        std::vector<std::string> items;
        for (const auto& b : mgr.getBookmarks()) {
            JsonDocument item;
            item["isbn"] = b.isbn;
            item["title"] = b.title;
            item["author"] = b.author;
            item["genre"] = b.genre;
            item["page"] = b.currentPage;
            item["total"] = b.totalPages;
            uint32_t latest = 0;
            for (const auto& h : b.history) {
                if (h.timestamp > latest) latest = h.timestamp;
            }
            item["ts"] = latest;
            std::string s;
            serializeJson(item, s);
            items.push_back(s);
        }
        transportRespond(buildPage("BOOKMARKS", rid, items, doc["off"] | 0));
    } else if (type == "GET_LIBRARY") {
        int off = doc["off"] | 0;
        if (off == 0 && (doc["refresh"] | false)) g_libraryValid = false;
        const auto& lib = getLibrary();
        std::vector<std::string> items;
        items.reserve(lib.size());
        for (const auto& e : lib) {
            JsonDocument item;
            item["n"] = baseName(e.path);
            item["p"] = e.path;
            item["s"] = e.size;
            std::string s;
            serializeJson(item, s);
            items.push_back(s);
        }
        transportRespond(buildPage("LIBRARY", rid, items, off));
        if (off == 0) s_lastEvent = "Library listed (" + std::to_string(lib.size()) + " books)";
    } else if (type == "DELETE_FILE") {
        // Accept either a full path under /books or a bare file name.
        std::string path = doc["path"] | "";
        std::string name = doc["name"] | "";
        if (path.empty() && !name.empty()) path = "/books/" + name;
        bool ok = false;
        std::string error;
        if (path.rfind("/books/", 0) != 0 || path.find("..") != std::string::npos) {
            error = "Invalid path";
        } else {
            ok = AppStorage::deleteFile(AppStorage::toRuntimePath(path), error);
            if (ok) {
                // Bookmarks live alongside books.
                std::string bmk = AppStorage::toBookmarkPath(path);
                if (fsExists(bmk)) fsRemove(bmk);
                invalidateLibrary();
                s_lastEvent = "Deleted " + baseName(path);
            }
        }
        JsonDocument r;
        r["t"] = "DELETE_RESULT";
        r["rid"] = rid;
        r["path"] = path;
        r["ok"] = ok;
        if (!ok) r["error"] = error;
        respond(r);
    } else if (type == "FILE_BEGIN") {
        if (g_upload.active) uploadReset();
        std::string name = doc["name"] | "";
        uint32_t size = doc["size"] | 0;
        std::string dir = doc["dir"] | "books";
        if (dir != "books" && dir != "images") {
            respondError("FILE_READY", rid, "Unsupported folder");
            return;
        }
        if (!isSafeFileName(name)) {
            respondError("FILE_READY", rid, "Invalid file name");
            return;
        }
#ifndef NATIVE_TESTING
        uint64_t freeBytes = SD.totalBytes() - SD.usedBytes();
        if ((uint64_t)size + 65536 > freeBytes) {
            respondError("FILE_READY", rid, "Not enough space on SD card");
            return;
        }
#endif
        fsEnsureDir("/" + dir);
        g_upload.partPath = "/" + dir + "/.upload.part";
        g_upload.finalPath = "/" + dir + "/" + name;
        if (fsExists(g_upload.partPath)) fsRemove(g_upload.partPath);
        if (!uploadOpen(g_upload.partPath)) {
            g_upload = UploadState();
            respondError("FILE_READY", rid, "Could not create file on SD card");
            return;
        }
        g_upload.active = true;
        g_upload.name = name;
        g_upload.expected = size;
        g_upload.lastActivityMs = millis();
        PowerManager::setBusy(true);
        s_lastEvent = "Receiving " + name;
        JsonDocument r;
        r["t"] = "FILE_READY";
        r["rid"] = rid;
        r["ok"] = true;
        respond(r);
        COMM_LOG("AppComm: upload started %s (%u bytes)\n", name.c_str(), (unsigned)size);
    } else if (type == "FILE_END") {
        JsonDocument r;
        r["t"] = "FILE_RESULT";
        r["rid"] = rid;
        if (!g_upload.active) {
            r["ok"] = false;
            r["error"] = "No transfer in progress";
            respond(r);
            return;
        }
        if (!uploadClose()) {
            g_upload.failed = true;
            g_upload.error = "SD card write failed";
        }
        bool ok = !g_upload.failed && g_upload.written == g_upload.expected;
        std::string error = g_upload.failed ? g_upload.error
                          : ("Received " + std::to_string(g_upload.written) + " of " + std::to_string(g_upload.expected) + " bytes");
        if (ok) {
            if (fsExists(g_upload.finalPath)) fsRemove(g_upload.finalPath);
            ok = fsRename(g_upload.partPath, g_upload.finalPath);
            if (!ok) error = "Could not rename file";
        }
        r["ok"] = ok;
        r["size"] = g_upload.written;
        r["path"] = g_upload.finalPath;
        if (!ok) r["error"] = error;
        COMM_LOG("AppComm: upload %s -> %s\n", g_upload.name.c_str(), ok ? "ok" : error.c_str());
        s_lastEvent = ok ? ("Received " + g_upload.name) : ("Upload failed: " + error);
        if (ok) {
            g_upload.partPath.clear(); // nothing to clean up
            if (g_upload.finalPath.rfind("/books/", 0) == 0) invalidateLibrary();
        }
        uploadReset();
        respond(r);
    } else if (type == "FILE_ABORT") {
        uploadReset();
        s_lastEvent = "Upload cancelled";
        if (rid) {
            JsonDocument r;
            r["t"] = "FILE_ABORT";
            r["rid"] = rid;
            r["ok"] = true;
            respond(r);
        }
    }
}

void handleMessage(const std::string& msg) {
    if (msg.empty()) return;
    if ((uint8_t)msg[0] == FRAME_FILE_DATA) {
        handleFileData(msg);
    } else {
        handleJson(msg);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Common API
// ---------------------------------------------------------------------------

void AppComm::poll() {
    if (!is_comm_init) return;
#ifdef NATIVE_TESTING
    nativePump();
#endif
    std::string msg;
    // Bound the work per loop iteration so a fast upload can't starve the UI.
    for (int i = 0; i < 24 && inboxPop(msg); i++) {
        handleMessage(msg);
    }
    if (g_upload.active) {
        if (g_peerDropped || millis() - g_upload.lastActivityMs > UPLOAD_STALL_TIMEOUT_MS) {
            COMM_LOG("AppComm: upload abandoned\n");
            s_lastEvent = "Upload interrupted";
            uploadReset();
        }
    }
    g_peerDropped = false;
}

bool AppComm::isInitialized() { return is_comm_init; }

bool AppComm::hasPending() {
    if (!is_comm_init) return false;
    std::lock_guard<std::mutex> lock(g_inboxMutex);
    return !g_inbox.empty();
}

bool AppComm::isConnected() { return is_comm_init && transportConnected(); }

uint32_t AppComm::bookmarksRevision() { return s_bookmarksRev; }
uint32_t AppComm::libraryRevision() { return s_libraryRev; }
bool AppComm::isTransferActive() { return g_upload.active; }
int AppComm::transferPercent() {
    if (!g_upload.active || g_upload.expected == 0) return 0;
    return (int)((uint64_t)g_upload.written * 100 / g_upload.expected);
}
std::string AppComm::transferName() { return g_upload.name; }
std::string AppComm::lastEvent() { return s_lastEvent; }

void AppComm::sendBookDelta(const std::string& isbn, const std::string& title, const std::string& author, int page, int total) {
    if (!isConnected()) return;
    JsonDocument doc;
    doc["t"] = "BOOK";
    doc["isbn"] = isbn;
    doc["title"] = title;
    doc["author"] = author;
    doc["page"] = page;
    doc["total"] = total;
    doc["ts"] = nowEpoch();
    std::string payload;
    serializeJson(doc, payload);
    transportNotifyDelta(payload);
}

#ifndef NATIVE_TESTING
// ---------------------------------------------------------------------------
// BLE transport (ESP32)
// ---------------------------------------------------------------------------
static const char* SERVICE_UUID = "12345678-1234-1234-1234-123456789abc";
static const char* WRITE_UUID   = "12345678-1234-1234-1234-123456789001";
static const char* NOTIFY_UUID  = "12345678-1234-1234-1234-123456789002";
static const char* RESP_UUID    = "12345678-1234-1234-1234-123456789003";

static BLEServer* pServer = nullptr;
static BLECharacteristic* pWriteChar = nullptr;
static BLECharacteristic* pNotifyChar = nullptr;
static BLECharacteristic* pRespChar = nullptr;

class CommWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        std::string value = c->getValue();
        if (!value.empty()) inboxPush(value);
    }
};

class CommServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        Serial.println("AppComm: companion connected");
        // Ask for a short connection interval (7.5-15ms): file transfers
        // are bounded by one write round trip per interval.
        server->updateConnParams(param->connect.remote_bda, 6, 12, 0, 400);
        // Keep advertising so a second phone/computer can still connect
        // even if a stale connection is hanging around.
        BLEDevice::startAdvertising();
    }
    void onDisconnect(BLEServer* server) override {
        (void)server;
        g_peerDropped = true;
        Serial.println("AppComm: companion disconnected, advertising again");
        BLEDevice::startAdvertising();
    }
};

static void transportRespond(const std::string& json) {
    if (!pRespChar) return;
    pRespChar->setValue(json);
    if (transportConnected()) pRespChar->notify();
}

static void transportNotifyDelta(const std::string& json) {
    if (!pNotifyChar) return;
    pNotifyChar->setValue(json);
    pNotifyChar->notify();
}

static bool transportConnected() {
    return pServer && pServer->getConnectedCount() > 0;
}

void AppComm::init() {
    if (is_comm_init) return;
    Serial.println("AppComm: starting BLE service");
    BLEDevice::init("EPD-Reader");
    BLEDevice::setMTU(517);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new CommServerCallbacks());

    BLEService* service = pServer->createService(SERVICE_UUID);

    pWriteChar = service->createCharacteristic(WRITE_UUID, BLECharacteristic::PROPERTY_WRITE);
    pWriteChar->setCallbacks(new CommWriteCallbacks());

    pNotifyChar = service->createCharacteristic(NOTIFY_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pNotifyChar->addDescriptor(new BLE2902());
    pNotifyChar->setValue("");

    pRespChar = service->createCharacteristic(RESP_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pRespChar->addDescriptor(new BLE2902());
    pRespChar->setValue("");

    service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    // 250-500ms advertising interval (units of 0.625ms). The library default
    // of 20-40ms keeps the radio busy ~10x more for no practical benefit -
    // phones still find the device within a second or two.
    adv->setMinInterval(0x190);
    adv->setMaxInterval(0x320);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    is_comm_init = true;
    Serial.println("AppComm: advertising as 'EPD-Reader'");
}

void AppComm::deinit() {
    if (!is_comm_init) return;
    uploadReset();
    is_comm_init = false;
    BLEDevice::getAdvertising()->stop();
    BLEDevice::deinit(false);
    inboxClear();
    pServer = nullptr;
    pWriteChar = nullptr;
    pNotifyChar = nullptr;
    pRespChar = nullptr;
    Serial.println("AppComm: BLE stopped");
}

#else
// ---------------------------------------------------------------------------
// TCP transport (native simulator). Frames in both directions are a 2-byte
// little-endian length followed by the payload, mirroring one BLE write /
// notification each.
// ---------------------------------------------------------------------------
static int server_fd = -1;
static int client_socket = -1;
static std::string tcp_rx;

static void tcpSendFrame(const std::string& payload) {
    if (client_socket < 0) return;
    uint8_t hdr[2] = {(uint8_t)(payload.size() & 0xFF), (uint8_t)(payload.size() >> 8)};
    send(client_socket, hdr, 2, MSG_NOSIGNAL);
    send(client_socket, payload.data(), payload.size(), MSG_NOSIGNAL);
}

static void transportRespond(const std::string& json) { tcpSendFrame(json); }
static void transportNotifyDelta(const std::string& json) { tcpSendFrame(json); }
static bool transportConnected() { return client_socket >= 0; }

// Simulator-only test hooks: {"cmd":"touch","x":..,"y":..} taps at logical
// (as-displayed) coordinates; {"cmd":"dump","path":".."} writes a PGM of the
// screen; {"cmd":"idle"} reports whether the main loop has nothing queued.
static bool handleSimCommand(const std::string& msg) {
    if (msg.empty() || msg[0] != '{' || msg.find("\"cmd\"") == std::string::npos) return false;
    JsonDocument doc;
    if (deserializeJson(doc, msg)) return false;
    std::string cmd = doc["cmd"] | "";
    if (cmd == "touch") {
        int x = doc["x"] | -1;
        int y = doc["y"] | -1;
        if (DisplayHAL::isPortrait()) {
            // getTouch() rotates raw controller coordinates for portrait.
            DisplayHAL::injectTouch(y, (EPD_HEIGHT - 1) - x);
        } else {
            DisplayHAL::injectTouch(x, y);
        }
    } else if (cmd == "dump") {
        std::string path = doc["path"] | "screen.pgm";
        DisplayHAL::dumpVisible(path.c_str());
    }
    tcpSendFrame("{\"t\":\"ACK\",\"cmd\":\"" + cmd + "\"}");
    return true;
}

void AppComm::init() {
    if (is_comm_init) return;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    int port = 9876;
    if (const char* env = getenv("EPD_SIM_PORT")) port = atoi(env);
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0 || listen(server_fd, 1) < 0) {
        std::cerr << "AppComm: TCP bind/listen failed" << std::endl;
        close(server_fd);
        server_fd = -1;
        return;
    }
    fcntl(server_fd, F_SETFL, O_NONBLOCK);
    is_comm_init = true;
    std::cout << "AppComm: native TCP mock listening on port " << port << std::endl;
}

void AppComm::deinit() {
    if (!is_comm_init) return;
    uploadReset();
    is_comm_init = false;
    if (client_socket >= 0) close(client_socket);
    if (server_fd >= 0) close(server_fd);
    client_socket = server_fd = -1;
    inboxClear();
}

// Runs before the common poll(): moves bytes from the socket into the inbox.
static void nativePump() {
    if (server_fd < 0) return;
    if (client_socket < 0) {
        client_socket = accept(server_fd, nullptr, nullptr);
        if (client_socket >= 0) {
            fcntl(client_socket, F_SETFL, O_NONBLOCK);
            tcp_rx.clear();
        }
        return;
    }
    char buf[4096];
    for (;;) {
        ssize_t n = read(client_socket, buf, sizeof(buf));
        if (n > 0) {
            tcp_rx.append(buf, (size_t)n);
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(client_socket);
            client_socket = -1;
            g_peerDropped = true;
        }
        break;
    }
    while (tcp_rx.size() >= 2) {
        {
            // Same thread as poll(): never block here, leave the rest
            // buffered until the inbox drains.
            std::lock_guard<std::mutex> lock(g_inboxMutex);
            if (g_inbox.size() >= INBOX_CAPACITY) break;
        }
        size_t len = (uint8_t)tcp_rx[0] | ((size_t)(uint8_t)tcp_rx[1] << 8);
        if (tcp_rx.size() < 2 + len) break;
        std::string frame = tcp_rx.substr(2, len);
        tcp_rx.erase(0, 2 + len);
        if (!handleSimCommand(frame)) inboxPush(frame);
    }
}

#endif
