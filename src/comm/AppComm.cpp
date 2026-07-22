#include "AppComm.h"
#include "EBookmarkManager.h"
#include <ArduinoJson.h>
#include <mutex>
#include <string>
#include <cstring>

#ifndef NATIVE_TESTING
#include <sys/time.h>
#include <time.h>
#endif

#define MAX_RING_SLOTS 8
#define MAX_MSG_BYTES 512

static char g_msg_ring[MAX_RING_SLOTS][MAX_MSG_BYTES];
static int g_ring_head = 0;
static int g_ring_tail = 0;
static int g_ring_count = 0;
static std::mutex g_ring_mutex;

static std::string read_buffer = "";
static bool is_comm_init = false;

static void ring_push(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    if (g_ring_count >= MAX_RING_SLOTS) {
        g_ring_tail = (g_ring_tail + 1) % MAX_RING_SLOTS;
        g_ring_count--;
    }
    size_t copy_len = len < (MAX_MSG_BYTES - 1) ? len : (MAX_MSG_BYTES - 1);
    memcpy(g_msg_ring[g_ring_head], data, copy_len);
    g_msg_ring[g_ring_head][copy_len] = '\0';
    g_ring_head = (g_ring_head + 1) % MAX_RING_SLOTS;
    g_ring_count++;
}

static bool ring_pop(std::string& out_msg) {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    if (g_ring_count == 0) return false;
    out_msg = g_msg_ring[g_ring_tail];
    g_ring_tail = (g_ring_tail + 1) % MAX_RING_SLOTS;
    g_ring_count--;
    return true;
}

static bool ring_has_data() {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    return g_ring_count > 0;
}

static void processIncomingSystemMessage(const std::string& msg) {
    if (msg.empty()) return;

    if (msg.find("\"TIME\"") != std::string::npos) {
        JsonDocument doc;
        if (!deserializeJson(doc, msg)) {
            std::string type = doc["t"] | "";
            if (type == "TIME") {
                uint32_t val = doc["val"] | 0;
                if (val > 0) {
#ifndef NATIVE_TESTING
                    struct timeval tv;
                    tv.tv_sec = val;
                    tv.tv_usec = 0;
                    settimeofday(&tv, NULL);
                    Serial.printf("AppComm: System RTC clock set to %u\n", (unsigned int)val);
#else
                    printf("AppComm: Native system RTC clock set to %u\n", (unsigned int)val);
#endif
                }
            }
        }
    } else if (msg.find("\"BOOK\"") != std::string::npos) {
        JsonDocument doc;
        if (!deserializeJson(doc, msg)) {
            std::string type = doc["t"] | "";
            if (type == "BOOK") {
                std::string isbn = doc["isbn"] | "";
                std::string title = doc["title"] | "";
                std::string author = doc["author"] | "";
                std::string genre = doc["genre"] | "";
                int page = doc["page"] | 0;
                int total = doc["total"] | 0;
                if (!isbn.empty()) {
                    EBookmarkManager::getInstance().initialize();
                    EBookmarkManager::getInstance().addOrUpdateBook(isbn, title, author, genre, page, total);
                    EBookmarkManager::getInstance().save();
#ifndef NATIVE_TESTING
                    Serial.printf("AppComm: Auto-synced book '%s' p.%d/%d\n", title.c_str(), page, total);
#else
                    printf("AppComm: Auto-synced book '%s' p.%d/%d\n", title.c_str(), page, total);
#endif
                }
            }
        }
    } else if (msg.find("\"GET_BOOKMARKS\"") != std::string::npos) {
        EBookmarkManager::getInstance().initialize();
        const auto& books = EBookmarkManager::getInstance().getBookmarks();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& b : books) {
            JsonObject obj = arr.add<JsonObject>();
            obj["isbn"] = b.isbn;
            obj["title"] = b.title;
            obj["author"] = b.author;
            obj["genre"] = b.genre;
            obj["page"] = b.currentPage;
            obj["total"] = b.totalPages;
            uint32_t latestTs = 0;
            for (const auto& h : b.history) {
                if (h.timestamp > latestTs) latestTs = h.timestamp;
            }
            obj["ts"] = latestTs;
        }
        std::string jsonOut;
        serializeJson(doc, jsonOut);
        AppComm::setReadBuffer(jsonOut);
#ifndef NATIVE_TESTING
        Serial.printf("AppComm: Sent %d bookmarks to companion app\n", (int)books.size());
#else
        printf("AppComm: Sent %d bookmarks to companion app\n", (int)books.size());
#endif
    }
}

#ifdef NATIVE_TESTING

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

static int server_fd = -1;
static int client_socket = -1;

void AppComm::init() {
    is_comm_init = true;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        return;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(9876);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return;
    }

    fcntl(server_fd, F_SETFL, O_NONBLOCK);
    std::cout << "Native TCP Mock listening on port 9876..." << std::endl;
}

void AppComm::deinit() {
    is_comm_init = false;
    if (client_socket >= 0) {
        close(client_socket);
        client_socket = -1;
    }
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    std::cout << "Native TCP Mock stopped." << std::endl;
}

void AppComm::poll() {
    if (server_fd < 0) return;

    if (client_socket < 0) {
        struct sockaddr_in address;
        int addrlen = sizeof(address);
        client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (client_socket >= 0) {
            std::cout << "Client connected to TCP Mock" << std::endl;
            fcntl(client_socket, F_SETFL, O_NONBLOCK);
        }
    } else {
        char buffer[2048] = {0};
        int valread = read(client_socket, buffer, sizeof(buffer) - 1);
        if (valread > 0) {
            std::string msg(buffer, valread);
            std::cout << "Received from app: " << msg << std::endl;
            
            if (msg == "READ") {
                sendData(read_buffer);
            } else {
                ring_push(buffer, valread);
                std::string ack = "ACK: " + msg;
                sendData(ack);
            }
        } else if (valread == 0) {
            std::cout << "Client disconnected" << std::endl;
            close(client_socket);
            client_socket = -1;
        }
    }

    std::string msg;
    while (ring_pop(msg)) {
        processIncomingSystemMessage(msg);
    }
}

void AppComm::sendData(const std::string& data) {
    if (client_socket >= 0) {
        send(client_socket, data.c_str(), data.length(), 0);
    }
}

bool AppComm::hasData() {
    return ring_has_data();
}

std::string AppComm::getNextMessage() {
    std::string msg;
    if (ring_pop(msg)) {
        processIncomingSystemMessage(msg);
        return msg;
    }
    return "";
}

void AppComm::setReadBuffer(const std::string& data) {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    read_buffer = data;
}

std::string AppComm::getReadBuffer() {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    return read_buffer;
}

void AppComm::startFileUpload(const std::string& name, int totalChunks) {
    std::cout << "Native mock: start file upload " << name << " (" << totalChunks << " chunks)" << std::endl;
}

void AppComm::appendFileChunk(int chunkIndex, const std::vector<uint8_t>& data) {
    std::cout << "Native mock: received chunk " << chunkIndex << " (" << data.size() << " bytes)" << std::endl;
}

bool AppComm::finishFileUpload() {
    std::cout << "Native mock: finish file upload" << std::endl;
    return true;
}

void AppComm::cancelFileUpload() {
    std::cout << "Native mock: cancel file upload" << std::endl;
}

#else

// Real ESP32 implementation (BLE)
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <SD.h>

static BLECharacteristic* pWriteChar = nullptr;
static BLECharacteristic* pReadChar = nullptr;

static std::string file_upload_name;
static std::vector<uint8_t> file_upload_buffer;
static int file_upload_expected_chunks = -1;
static int file_upload_received_chunks = 0;

class MyWriteCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0) {
            ring_push(value.c_str(), value.length());
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        Serial.println("AppComm: Companion app connected!");
    }
    void onDisconnect(BLEServer* pServer) override {
        Serial.println("AppComm: Companion app disconnected. Auto-restarting advertising...");
        BLEDevice::startAdvertising();
    }
};

void AppComm::init() {
    if (is_comm_init) {
        Serial.println("AppComm BLE server already initialized.");
        return;
    }
    Serial.println("AppComm BLE server initializing as system background service...");
    BLEDevice::init("EPD-Reader");
    BLEServer* pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService("12345678-1234-1234-1234-123456789abc");

    pWriteChar = pService->createCharacteristic(
        "12345678-1234-1234-1234-123456789001",
        BLECharacteristic::PROPERTY_WRITE
    );
    pWriteChar->setCallbacks(new MyWriteCallbacks());

    pReadChar = pService->createCharacteristic(
        "12345678-1234-1234-1234-123456789002",
        BLECharacteristic::PROPERTY_READ
    );
    pReadChar->setValue("");

    pService->start();

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID("12345678-1234-1234-1234-123456789abc");
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    is_comm_init = true;
    Serial.println("AppComm BLE system service active and advertising as 'EPD-Reader'");
}

void AppComm::deinit() {
    if (!is_comm_init) {
        return;
    }
    is_comm_init = false;
    Serial.println("AppComm BLE server deinitializing...");
    BLEDevice::getAdvertising()->stop();
    BLEDevice::deinit(true);
    pWriteChar = nullptr;
    pReadChar = nullptr;
}

void AppComm::poll() {
    std::string msg;
    while (ring_pop(msg)) {
        processIncomingSystemMessage(msg);
    }
}

void AppComm::sendData(const std::string& data) {
    setReadBuffer(data);
}

bool AppComm::hasData() {
    return ring_has_data();
}

std::string AppComm::getNextMessage() {
    std::string msg;
    if (ring_pop(msg)) {
        processIncomingSystemMessage(msg);
        return msg;
    }
    return "";
}

void AppComm::setReadBuffer(const std::string& data) {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    read_buffer = data;
    if (pReadChar) {
        pReadChar->setValue(read_buffer);
    }
}

std::string AppComm::getReadBuffer() {
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    return read_buffer;
}

void AppComm::startFileUpload(const std::string& name, int totalChunks) {
    file_upload_name = name;
    file_upload_expected_chunks = totalChunks;
    file_upload_received_chunks = 0;
    file_upload_buffer.clear();
    Serial.printf("Starting BLE file upload: %s (%d chunks)\n", name.c_str(), totalChunks);
}

void AppComm::appendFileChunk(int chunkIndex, const std::vector<uint8_t>& data) {
    if (chunkIndex != file_upload_received_chunks) {
        Serial.printf("BLE file chunk out of order: expected %d got %d\n",
                      file_upload_received_chunks, chunkIndex);
        return;
    }
    file_upload_buffer.insert(file_upload_buffer.end(), data.begin(), data.end());
    file_upload_received_chunks++;
}

bool AppComm::finishFileUpload() {
    if (file_upload_expected_chunks > 0 &&
        file_upload_received_chunks != file_upload_expected_chunks) {
        Serial.printf("BLE file upload incomplete: %d/%d chunks\n",
                      file_upload_received_chunks, file_upload_expected_chunks);
        return false;
    }

    std::string path = "/books/" + file_upload_name;
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) {
        Serial.printf("Failed to open %s for writing\n", path.c_str());
        return false;
    }
    f.write(file_upload_buffer.data(), file_upload_buffer.size());
    f.close();

    Serial.printf("Saved uploaded file: %s (%u bytes)\n",
                  path.c_str(), file_upload_buffer.size());
    cancelFileUpload();
    return true;
}

void AppComm::cancelFileUpload() {
    file_upload_name.clear();
    file_upload_buffer.clear();
    file_upload_expected_chunks = -1;
    file_upload_received_chunks = 0;
}

#endif

bool AppComm::isInitialized() {
    return is_comm_init;
}
