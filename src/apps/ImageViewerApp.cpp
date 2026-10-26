#include "ImageViewerApp.h"
#include "DisplayHAL.h"
#include "Launcher.h"
#include "SystemSettings.h"
#include "reader/AppStorage.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

// Route stb_image's allocations to PSRAM on the ESP32.
#ifndef NATIVE_TESTING
#define STBI_MALLOC(sz)        heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)
#define STBI_REALLOC(p,newsz)  heap_caps_realloc(p, newsz, MALLOC_CAP_SPIRAM)
#define STBI_FREE(p)           heap_caps_free(p)
#else
#define STBI_MALLOC(sz)        malloc(sz)
#define STBI_REALLOC(p,newsz)  realloc(p,newsz)
#define STBI_FREE(p)           free(p)
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_LINEAR
#include "stb_image.h"

extern uint8_t* framebuffer;

namespace {
using namespace UI;

const char* kImageDir = "/images";
const char* kCacheDir = "/data/imgcache";
constexpr size_t FB_BYTES = (size_t)W * H / 2;
constexpr size_t ROW_BYTES = (size_t)W / 2;
constexpr long long MAX_PIXELS = 24LL * 1000 * 1000;
constexpr int RENDER_CHUNKS = 4;
constexpr int GAMMA_STEPS = 1020;
constexpr size_t READ_BUF = 16 * 1024;

// Conversion screen layout
constexpr int CARD_Y = HEADER_H + 48;
constexpr int CARD_H = 330;
constexpr int CARD_PAD = 24;
constexpr int ERROR_CARD_H = 400;
const Rect kProgressArea(MARGIN + CARD_PAD, CARD_Y + 128, W - 2 * MARGIN - 2 * CARD_PAD, 124);
const Rect kErrorButton(MARGIN + CARD_PAD, CARD_Y + ERROR_CARD_H - 64 - CARD_PAD, W - 2 * MARGIN - 2 * CARD_PAD, 64);

const char* kViewerActions[] = {"Prev", "Rotate", "Next"};

void* bigAlloc(size_t n) {
#ifndef NATIVE_TESTING
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
#else
    return malloc(n);
#endif
}

void bigFree(void* p) {
    if (!p) return;
#ifndef NATIVE_TESTING
    heap_caps_free(p);
#else
    free(p);
#endif
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool endsWith(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string formatSize(size_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    else snprintf(buf, sizeof(buf), "%u KB", (unsigned)((bytes + 1023) / 1024));
    return buf;
}

std::string formatMegapixels(long long pixels) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f MP", pixels / 1000000.0);
    return buf;
}

std::string dims(int w, int h) {
    return std::to_string(w) + " \xC3\x97 " + std::to_string(h);
}

// ---------------------------------------------------------------------------
// Buffered file reader feeding stb_image through callbacks, so the
// compressed JPEG never has to sit in memory next to the decoded pixels.
// ---------------------------------------------------------------------------

struct JpegReader {
#ifndef NATIVE_TESTING
    File file;
#else
    FILE* file = nullptr;
#endif
    size_t fileSize = 0;
    size_t filePos = 0;
    uint8_t* buf = nullptr;
    size_t len = 0;
    size_t pos = 0;

    bool open(const std::string& path) {
        std::string rp = AppStorage::toRuntimePath(path);
#ifndef NATIVE_TESTING
        file = SD.open(rp.c_str(), FILE_READ);
        if (!file || file.isDirectory()) return false;
        fileSize = file.size();
#else
        file = fopen(rp.c_str(), "rb");
        if (!file) return false;
        fseek(file, 0, SEEK_END);
        fileSize = (size_t)ftell(file);
        fseek(file, 0, SEEK_SET);
#endif
        buf = (uint8_t*)bigAlloc(READ_BUF);
        return buf != nullptr && fileSize > 0;
    }

    void close() {
#ifndef NATIVE_TESTING
        if (file) file.close();
#else
        if (file) fclose(file);
        file = nullptr;
#endif
        bigFree(buf);
        buf = nullptr;
    }

    ~JpegReader() { close(); }

    bool refill() {
        pos = 0;
#ifndef NATIVE_TESTING
        int n = file.read(buf, READ_BUF);
        len = n > 0 ? (size_t)n : 0;
#else
        len = fread(buf, 1, READ_BUF, file);
#endif
        filePos += len;
        return len > 0;
    }

    static int read(void* user, char* data, int size) {
        JpegReader* r = (JpegReader*)user;
        int done = 0;
        while (done < size) {
            if (r->pos >= r->len && !r->refill()) break;
            size_t n = std::min((size_t)(size - done), r->len - r->pos);
            memcpy(data + done, r->buf + r->pos, n);
            r->pos += n;
            done += (int)n;
        }
        return done;
    }

    static void skip(void* user, int n) {
        JpegReader* r = (JpegReader*)user;
        if (n <= 0) return;
        size_t avail = r->len - r->pos;
        if ((size_t)n <= avail) {
            r->pos += n;
            return;
        }
        size_t target = r->filePos + (n - avail);
        if (target > r->fileSize) target = r->fileSize;
        r->pos = r->len = 0;
        r->filePos = target;
#ifndef NATIVE_TESTING
        r->file.seek(target);
#else
        fseek(r->file, (long)target, SEEK_SET);
#endif
    }

    static int eof(void* user) {
        JpegReader* r = (JpegReader*)user;
        return r->pos >= r->len && r->filePos >= r->fileSize;
    }

    stbi_io_callbacks callbacks() const {
        stbi_io_callbacks cb;
        cb.read = &JpegReader::read;
        cb.skip = &JpegReader::skip;
        cb.eof = &JpegReader::eof;
        return cb;
    }
};

// Resampling taps for one axis. Downscaling uses exact area averaging (each
// source pixel weighted by how much of it the destination pixel covers);
// upscaling uses bilinear interpolation.
void buildTaps(int src, int dst, std::vector<int>& start, std::vector<int>& count,
               std::vector<int>& offset, std::vector<float>& weights) {
    start.assign(dst, 0);
    count.assign(dst, 0);
    offset.assign(dst, 0);
    weights.clear();
    double ratio = (double)src / dst;
    for (int d = 0; d < dst; d++) {
        offset[d] = (int)weights.size();
        if (ratio >= 1.0) {
            double a = d * ratio;
            double b = std::min((double)src, (d + 1) * ratio);
            int i0 = (int)std::floor(a);
            int i1 = std::min(src, (int)std::ceil(b));
            start[d] = i0;
            for (int i = i0; i < i1; i++) {
                double wgt = std::min((double)i + 1, b) - std::max((double)i, a);
                weights.push_back((float)(wgt / ratio));
            }
            count[d] = i1 - i0;
        } else {
            double c = (d + 0.5) * ratio - 0.5;
            if (c < 0) c = 0;
            if (c > src - 1) c = src - 1;
            int i0 = (int)std::floor(c);
            float f = (float)(c - i0);
            start[d] = i0;
            if (i0 + 1 < src && f > 0.0f) {
                weights.push_back(1.0f - f);
                weights.push_back(f);
                count[d] = 2;
            } else {
                weights.push_back(1.0f);
                count[d] = 1;
            }
        }
    }
}

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

void ensureCacheDir() {
#ifndef NATIVE_TESTING
    if (!SD.exists("/data")) SD.mkdir("/data");
    if (!SD.exists(kCacheDir)) SD.mkdir(kCacheDir);
#else
    mkdir(AppStorage::toRuntimePath("/data").c_str(), 0755);
    mkdir(AppStorage::toRuntimePath(kCacheDir).c_str(), 0755);
#endif
}

void removeFile(const std::string& path) {
    std::string rp = AppStorage::toRuntimePath(path);
#ifndef NATIVE_TESTING
    if (SD.exists(rp.c_str())) SD.remove(rp.c_str());
#else
    remove(rp.c_str());
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ImageViewerApp::~ImageViewerApp() {
    freeBuffers();
}

void ImageViewerApp::onCreate() {
    m_state = STATE_BROWSER;
    m_step = STEP_IDLE;
    m_page = 0;
    m_index = -1;
    m_rotated = false;
    m_rotatePref = false;
    m_overlay = false;
    updateFileList();
}

void ImageViewerApp::onDestroy() {
    freeBuffers();
}

void ImageViewerApp::freeDecode() {
    if (m_gray) {
        stbi_image_free(m_gray);
        m_gray = nullptr;
    }
    std::vector<int>().swap(m_tapStartX);
    std::vector<int>().swap(m_tapCountX);
    std::vector<int>().swap(m_tapOffX);
    std::vector<int>().swap(m_tapStartY);
    std::vector<int>().swap(m_tapCountY);
    std::vector<int>().swap(m_tapOffY);
    std::vector<float>().swap(m_tapWeightX);
    std::vector<float>().swap(m_tapWeightY);
    std::vector<float>().swap(m_rowAcc);
    std::vector<int>().swap(m_errCur);
    std::vector<int>().swap(m_errNext);
    std::vector<int16_t>().swap(m_gammaLut);
}

void ImageViewerApp::freeBuffers() {
    freeDecode();
    bigFree(m_frame);
    m_frame = nullptr;
}

void ImageViewerApp::updateFileList() {
    m_files.clear();
    m_fileBrowser.setRoot(kImageDir);
    for (const FileInfo& f : m_fileBrowser.getFiles()) {
        if (f.isDirectory) continue;
        std::string name = lower(f.name);
        if (endsWith(name, ".jpg") || endsWith(name, ".jpeg")) m_files.push_back(f);
    }
    std::sort(m_files.begin(), m_files.end(), [](const FileInfo& a, const FileInfo& b) {
        return lower(a.name) < lower(b.name);
    });
}

int ImageViewerApp::pageCount() const {
    int per = rowsPerPage(true);
    int pages = ((int)m_files.size() + per - 1) / per;
    return pages < 1 ? 1 : pages;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void ImageViewerApp::draw() {
    switch (m_state) {
        case STATE_BROWSER: drawBrowser(); break;
        case STATE_CONVERTING: drawConverting(true); break;
        case STATE_ERROR: drawError(); break;
        case STATE_VIEW: drawViewer(); break;
    }
}

void ImageViewerApp::drawBrowser() {
    int pages = pageCount();
    if (m_page >= pages) m_page = pages - 1;
    if (m_page < 0) m_page = 0;

    UIFramework::performFullScreenDraw(framebuffer, [this, pages]() {
        header("Pictures", true);
        if (m_files.empty()) {
            emptyState(H / 2, "No pictures yet", "Copy JPEG photos into /images on the SD card.");
            return;
        }
        int per = rowsPerPage(true);
        int start = m_page * per;
        for (int i = 0; i < per && start + i < (int)m_files.size(); i++) {
            const FileInfo& f = m_files[start + i];
            listRow(HEADER_H + i * ROW_H, f.name, "JPEG  \xC2\xB7  " + formatSize(f.size), "", true, -1.0f, ICON_IMAGE);
        }
        if (pages > 1) pager(m_page, pages);
    });
}

void ImageViewerApp::drawProgressArea() {
    const Rect& a = kProgressArea;
    text(ellipsize(m_status, a.w, FONT_SMALL), a.x, a.y + 4, FONT_SMALL, INK);
    progressBar(a.x, a.y + 52, a.w, 14, m_progress / 100.0f);
    textRight(std::to_string(m_progress) + "%", a.x + a.w, a.y + 82, FONT_CAPTION, DARK, true);
}

void ImageViewerApp::drawConverting(bool fullRefresh) {
    if (!fullRefresh) {
        const Rect& a = kProgressArea;
        UIFramework::performFastPartialUpdate(framebuffer, a.x, a.y, a.w, a.h, [this]() { drawProgressArea(); });
        return;
    }
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        header("Pictures", true);
        int cx = MARGIN, cw = W - 2 * MARGIN;
        strokeRoundRect(cx, CARD_Y, cw, CARD_H, 18, 2, LIGHT);

        int ix = cx + CARD_PAD;
        icon(ICON_IMAGE, ix, CARD_Y + 26, 64);
        int tx = ix + 64 + 18;
        int tw = cx + cw - CARD_PAD - tx;
        std::string name = m_index >= 0 && m_index < (int)m_files.size() ? m_files[m_index].name : "";
        text(ellipsize(name, tw, FONT_BODY, true), tx, CARD_Y + 28, FONT_BODY, BLACK, true);
        std::string sub = m_rotated ? "Preparing rotated view" : "Preparing for e-paper";
        text(ellipsize(sub, tw, FONT_SMALL), tx, CARD_Y + 66, FONT_SMALL, DARK);
        hRule(CARD_Y + 112, LIGHT, ix, cx + cw - CARD_PAD);

        drawProgressArea();

        int level = SystemSettings::get().imageBrightness;
        std::string note = std::string("Brightness: ") + SystemSettings::imageBrightnessLabel(level) + "  (Settings > Display)";
        text(ellipsize(note, cw - 2 * CARD_PAD, FONT_CAPTION), ix, CARD_Y + CARD_H - 52, FONT_CAPTION, DARK);

        textWrapped("Converted pictures are cached, so they open instantly next time.",
                    MARGIN * 2, CARD_Y + CARD_H + 32, W - MARGIN * 4, FONT_SMALL, DARK, 3, 6, true);
    });
}

void ImageViewerApp::drawError() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        header("Pictures", true);
        int cx = MARGIN, cw = W - 2 * MARGIN;
        strokeRoundRect(cx, CARD_Y, cw, ERROR_CARD_H, 18, 2, LIGHT);
        int ix = cx + CARD_PAD;
        icon(ICON_IMAGE, ix, CARD_Y + 26, 64, MID);
        int tx = ix + 64 + 18;
        int tw = cx + cw - CARD_PAD - tx;
        text(ellipsize(m_errorTitle, tw, FONT_BODY, true), tx, CARD_Y + 28, FONT_BODY, BLACK, true);
        std::string name = m_index >= 0 && m_index < (int)m_files.size() ? m_files[m_index].name : "";
        text(ellipsize(name, tw, FONT_SMALL), tx, CARD_Y + 66, FONT_SMALL, DARK);
        hRule(CARD_Y + 112, LIGHT, ix, cx + cw - CARD_PAD);
        textWrapped(m_errorMessage, ix, CARD_Y + 132, cw - 2 * CARD_PAD, FONT_SMALL, INK, 4);
        button(kErrorButton, "Back to Pictures", ButtonStyle::PRIMARY);
    });
}

static void drawViewerHeader(const std::string& title) {
    fillRect(0, 0, W, HEADER_H, WHITE);
    header(title, true);
}

static void drawViewerFooter() {
    fillRect(0, H - FOOTER_H, W, FOOTER_H, WHITE);
    footerActions({kViewerActions[0], kViewerActions[1], kViewerActions[2]});
}

void ImageViewerApp::drawViewer() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        if (m_frame) memcpy(framebuffer, m_frame, FB_BYTES);
        if (m_overlay) {
            drawViewerHeader(m_files[m_index].name);
            drawViewerFooter();
        }
    });
}

void ImageViewerApp::drawOverlay(bool show) {
    m_overlay = show;
    // Hiding puts the picture's own rows back; the frame is stored in
    // framebuffer layout, so whole rows can be copied as bytes.
    UIFramework::performFastPartialUpdate(framebuffer, 0, 0, W, HEADER_H, [this, show]() {
        if (show) drawViewerHeader(m_files[m_index].name);
        else if (m_frame) memcpy(framebuffer, m_frame, HEADER_H * ROW_BYTES);
    });
    UIFramework::performFastPartialUpdate(framebuffer, 0, H - FOOTER_H, W, FOOTER_H, [this, show]() {
        if (show) drawViewerFooter();
        else if (m_frame) {
            size_t off = (size_t)(H - FOOTER_H) * ROW_BYTES;
            memcpy(framebuffer + off, m_frame + off, FOOTER_H * ROW_BYTES);
        }
    });
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void ImageViewerApp::goToBrowser() {
    freeBuffers();
    m_state = STATE_BROWSER;
    m_step = STEP_IDLE;
    m_overlay = false;
    if (m_index >= 0) m_page = m_index / rowsPerPage(true);
    drawBrowser();
}

bool ImageViewerApp::onBack() {
    if (m_state == STATE_BROWSER) return false;
    goToBrowser();
    return true;
}

void ImageViewerApp::fail(const std::string& title, const std::string& message) {
    freeBuffers();
    m_step = STEP_IDLE;
    m_state = STATE_ERROR;
    m_errorTitle = title;
    m_errorMessage = message;
    drawError();
}

void ImageViewerApp::handleTouch(int x, int y) {
    switch (m_state) {
        case STATE_BROWSER: {
            int pages = pageCount();
            int dir = hitPager(x, y, m_page, pages);
            if (pages > 1 && dir != 0) {
                m_page += dir;
                drawBrowser();
                return;
            }
            if (y < HEADER_H || (pages > 1 && y >= H - FOOTER_H)) return;
            int per = rowsPerPage(true);
            int row = (y - HEADER_H) / ROW_H;
            int idx = m_page * per + row;
            if (row < per && idx < (int)m_files.size()) {
                m_rotatePref = false;
                m_overlay = false;
                openPicture(idx, false);
            }
            return;
        }
        case STATE_CONVERTING:
            return;
        case STATE_ERROR:
            goToBrowser();
            return;
        case STATE_VIEW:
            break;
    }

    if (!m_overlay) {
        drawOverlay(true);
        return;
    }
    if (y < HEADER_H) return;
    if (y < H - FOOTER_H) {
        drawOverlay(false);
        return;
    }
    int n = (int)m_files.size();
    int action = hitFooterAction(x, y, 3);
    switch (action) {
        case 0:
        case 2: {
            if (n <= 1) return;
            int next = (m_index + (action == 0 ? n - 1 : 1)) % n;
            bool rotate = false;
            if (m_rotatePref) {
                int w = 0, h = 0, comp = 0;
                rotate = probeSize(m_files[next].path, w, h, comp) && w > h;
            }
            openPicture(next, rotate);
            return;
        }
        case 1:
            m_rotatePref = !m_rotated;
            showRotated(!m_rotated);
            return;
        default:
            return;
    }
}

void ImageViewerApp::showRotated(bool rotated) {
    openPicture(m_index, rotated);
}

void ImageViewerApp::openPicture(int index, bool rotated) {
    if (index < 0 || index >= (int)m_files.size()) return;
    freeDecode();
    m_index = index;
    m_rotated = rotated;
    m_cachePath = cachePathFor(m_files[index], SystemSettings::get().imageBrightness, rotated);

    if (!ensureFrame()) {
        fail("Not enough memory", "There is not enough free memory to show a picture right now.");
        return;
    }
    if (loadCache()) {
        m_state = STATE_VIEW;
        m_step = STEP_IDLE;
        drawViewer();
        return;
    }

    m_state = STATE_CONVERTING;
    m_step = STEP_PROBE;
    m_progress = 0;
    m_status = "Reading picture...";
    drawConverting(true);
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

std::string ImageViewerApp::cachePathFor(const FileInfo& file, int brightness, bool rotated) const {
    std::string stem = file.name;
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::string safe;
    for (char c : stem) {
        if (safe.size() >= 40) break;
        safe += isalnum((unsigned char)c) || c == '-' ? c : '_';
    }
    char tail[64];
    // The hash keeps names that sanitise to the same stem apart.
    snprintf(tail, sizeof(tail), "_%08x_%lu_b%d_r%d.raw", (unsigned)fnv1a(file.name),
             (unsigned long)file.size, brightness, rotated ? 1 : 0);
    return std::string(kCacheDir) + "/" + safe + tail;
}

bool ImageViewerApp::ensureFrame() {
    if (!m_frame) m_frame = (uint8_t*)bigAlloc(FB_BYTES);
    return m_frame != nullptr;
}

bool ImageViewerApp::loadCache() {
    std::string rp = AppStorage::toRuntimePath(m_cachePath);
    bool ok = false;
#ifndef NATIVE_TESTING
    if (!SD.exists(rp.c_str())) return false;
    File f = SD.open(rp.c_str(), FILE_READ);
    if (!f) return false;
    if (f.size() == FB_BYTES) ok = f.read(m_frame, FB_BYTES) == FB_BYTES;
    f.close();
#else
    FILE* f = fopen(rp.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    if ((size_t)ftell(f) == FB_BYTES) {
        fseek(f, 0, SEEK_SET);
        ok = fread(m_frame, 1, FB_BYTES, f) == FB_BYTES;
    }
    fclose(f);
#endif
    return ok;
}

void ImageViewerApp::saveCache() {
    ensureCacheDir();
    std::string rp = AppStorage::toRuntimePath(m_cachePath);
#ifndef NATIVE_TESTING
    if (SD.exists(rp.c_str())) SD.remove(rp.c_str());
    File f = SD.open(rp.c_str(), FILE_WRITE);
    if (f) {
        bool ok = f.write(m_frame, FB_BYTES) == FB_BYTES;
        f.close();
        if (!ok) SD.remove(rp.c_str());
    }
#else
    FILE* f = fopen(rp.c_str(), "wb");
    if (f) {
        bool ok = fwrite(m_frame, 1, FB_BYTES, f) == FB_BYTES;
        fclose(f);
        if (!ok) remove(rp.c_str());
    }
#endif
    // Frames rendered at other brightness levels are now stale.
    int level = SystemSettings::get().imageBrightness;
    for (int b = 0; b <= 3; b++) {
        if (b != level) removeFile(cachePathFor(m_files[m_index], b, m_rotated));
    }
}

// ---------------------------------------------------------------------------
// Conversion pipeline (one step per update() so touches stay responsive)
// ---------------------------------------------------------------------------

bool ImageViewerApp::probeSize(const std::string& path, int& w, int& h, int& comp) const {
    JpegReader reader;
    if (!reader.open(path)) return false;
    stbi_io_callbacks cb = reader.callbacks();
    return stbi_info_from_callbacks(&cb, &reader, &w, &h, &comp) != 0 && w > 0 && h > 0;
}

void ImageViewerApp::update() {
    if (m_state != STATE_CONVERTING) return;
    switch (m_step) {
        case STEP_PROBE:
            stepProbe();
            break;
        case STEP_DECODE:
            stepDecode();
            break;
        case STEP_RENDER: {
            int chunk = (m_dstH + RENDER_CHUNKS - 1) / RENDER_CHUNKS;
            renderRows(chunk);
            if (m_renderRow >= m_dstH) {
                freeDecode();
                m_step = STEP_SAVE;
                m_progress = 95;
                m_status = "Saving to cache...";
            } else {
                m_progress = 40 + 55 * m_renderRow / m_dstH;
            }
            drawConverting(false);
            break;
        }
        case STEP_SAVE:
            saveCache();
            m_progress = 100;
            m_step = STEP_IDLE;
            m_state = STATE_VIEW;
            drawViewer();
            break;
        default:
            break;
    }
}

void ImageViewerApp::stepProbe() {
    int w = 0, h = 0, comp = 0;
    if (!probeSize(m_files[m_index].path, w, h, comp)) {
        fail("Can't open this picture", "The file could not be read as a JPEG image. It may be damaged or use an unsupported format.");
        return;
    }
    long long pixels = (long long)w * h;
    if (pixels > MAX_PIXELS) {
        fail("Picture too large", "This photo is " + dims(w, h) + " (" + formatMegapixels(pixels) +
             "). Pictures up to 24 MP are supported; please resize it before copying it to the card.");
        return;
    }
#ifndef NATIVE_TESTING
    // stb_image holds every decoded component plane plus the output at once.
    size_t need = (size_t)(pixels * (comp == 1 ? 2 : 5) / 2) + 128 * 1024;
    size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (need > freeBytes || (size_t)pixels + 64 > largest) {
        long long fit = (long long)(freeBytes > 128 * 1024 ? freeBytes - 128 * 1024 : 0) * 2 / (comp == 1 ? 2 : 5);
        fail("Picture too large", "This photo is " + dims(w, h) + " (" + formatMegapixels(pixels) +
             "), which needs more memory than is free. Resize it to about " + formatMegapixels(fit) + " or less.");
        return;
    }
#endif
    m_srcW = w;
    m_srcH = h;
    m_srcComp = comp;
    m_status = "Decoding " + dims(w, h) + " photo...";
    m_progress = 10;
    m_step = STEP_DECODE;
    drawConverting(false);
}

void ImageViewerApp::stepDecode() {
    JpegReader reader;
    if (!reader.open(m_files[m_index].path)) {
        fail("Can't open this picture", "The file could not be read from the SD card.");
        return;
    }
    stbi_io_callbacks cb = reader.callbacks();
    int w = 0, h = 0, comp = 0;
    m_gray = stbi_load_from_callbacks(&cb, &reader, &w, &h, &comp, 1);
    reader.close();
    if (!m_gray) {
        const char* why = stbi_failure_reason();
        if (why && strcmp(why, "outofmem") == 0) {
            fail("Picture too large", "There was not enough memory to decode this " + dims(m_srcW, m_srcH) +
                 " photo. Resize it to a smaller size and try again.");
        } else {
            fail("Can't open this picture", std::string("This JPEG could not be decoded (") + (why ? why : "unknown error") + ").");
        }
        return;
    }
    m_srcW = w;
    m_srcH = h;
    prepareRender();
    m_status = "Resizing and dithering...";
    m_progress = 40;
    m_step = STEP_RENDER;
    drawConverting(false);
}

void ImageViewerApp::prepareRender() {
    // Work in "view" coordinates: when rotated, the picture is turned 90
    // degrees clockwise, so its width and height swap.
    int vw = m_rotated ? m_srcH : m_srcW;
    int vh = m_rotated ? m_srcW : m_srcH;
    double scale = std::min((double)W / vw, (double)H / vh);
    m_dstW = std::max(1, std::min(W, (int)std::lround(vw * scale)));
    m_dstH = std::max(1, std::min(H, (int)std::lround(vh * scale)));
    m_dstX = (W - m_dstW) / 2;
    m_dstY = (H - m_dstH) / 2;

    buildTaps(vw, m_dstW, m_tapStartX, m_tapCountX, m_tapOffX, m_tapWeightX);
    buildTaps(vh, m_dstH, m_tapStartY, m_tapCountY, m_tapOffY, m_tapWeightY);
    m_rowAcc.assign(m_dstW, 0.0f);
    m_errCur.assign(m_dstW + 2, 0);
    m_errNext.assign(m_dstW + 2, 0);

    // Gamma on normalised luma, expressed in 1/16ths of a grey level (0..240)
    // so the dither can carry sub-level error.
    float gamma = SystemSettings::imageGamma();
    m_gammaLut.resize(GAMMA_STEPS + 1);
    for (int i = 0; i <= GAMMA_STEPS; i++) {
        float v = std::pow((float)i / GAMMA_STEPS, gamma);
        m_gammaLut[i] = (int16_t)std::lround(v * 240.0f);
    }

    memset(m_frame, 0xFF, FB_BYTES);
    m_renderRow = 0;
}

void ImageViewerApp::renderRows(int count) {
    const int iw = m_srcW;
    const int ih = m_srcH;
    int end = std::min(m_dstH, m_renderRow + count);
    for (int dy = m_renderRow; dy < end; dy++) {
        // Resample one destination row.
        std::fill(m_rowAcc.begin(), m_rowAcc.end(), 0.0f);
        for (int ty = 0; ty < m_tapCountY[dy]; ty++) {
            int vy = m_tapStartY[dy] + ty;
            float wy = m_tapWeightY[m_tapOffY[dy] + ty];
            for (int dx = 0; dx < m_dstW; dx++) {
                const float* wx = &m_tapWeightX[m_tapOffX[dx]];
                int vx = m_tapStartX[dx];
                int n = m_tapCountX[dx];
                float sum = 0.0f;
                if (!m_rotated) {
                    const uint8_t* p = m_gray + (size_t)vy * iw + vx;
                    for (int k = 0; k < n; k++) sum += wx[k] * p[k];
                } else {
                    // View (vx, vy) is source (vy, ih - 1 - vx).
                    const uint8_t* p = m_gray + (size_t)(ih - 1 - vx) * iw + vy;
                    for (int k = 0; k < n; k++) sum += wx[k] * p[-(ptrdiff_t)k * iw];
                }
                m_rowAcc[dx] += wy * sum;
            }
        }

        // Gamma + Floyd-Steinberg to 16 levels, serpentine to avoid drift.
        bool ltr = (dy & 1) == 0;
        int* cur = m_errCur.data() + 1;
        int* nxt = m_errNext.data() + 1;
        for (int i = 0; i < m_dstW; i++) {
            int dx = ltr ? i : m_dstW - 1 - i;
            int li = (int)(m_rowAcc[dx] * (GAMMA_STEPS / 255.0f) + 0.5f);
            li = std::max(0, std::min(GAMMA_STEPS, li));
            int val = m_gammaLut[li] + cur[dx];
            int q = val <= 0 ? 0 : (val + 8) >> 4;
            if (q > 15) q = 15;
            int err = val - q * 16;
            int e1 = err / 16, e3 = err * 3 / 16, e5 = err * 5 / 16;
            int e7 = err - e1 - e3 - e5;
            int f = ltr ? 1 : -1;
            cur[dx + f] += e7;
            nxt[dx - f] += e3;
            nxt[dx] += e5;
            nxt[dx + f] += e1;
            DisplayHAL::setPixel(m_dstX + dx, m_dstY + dy, (uint8_t)q, m_frame);
        }
        m_errCur.swap(m_errNext);
        std::fill(m_errNext.begin(), m_errNext.end(), 0);
    }
    m_renderRow = end;
}
