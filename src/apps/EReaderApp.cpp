#include "EReaderApp.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include "reader/AppStorage.h"
#include "reader/BookCover.h"
#include "embedded_font.h"
#include "Launcher.h"
#include "ui/UIKit.h"
#include "SystemSettings.h"
#include "EBookmarkManager.h"
#include "PowerManager.h"
#include <cstring>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <stdio.h>
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
#endif

extern uint8_t *framebuffer;
extern TypographyEngine typography;

float EReaderApp::s_readingFontSize = 28.0f;
LibrarySort EReaderApp::s_librarySort = LibrarySort::COMPLETION;

void EReaderApp::setReadingFontSize(float pt) {
    s_readingFontSize = pt;
}

float EReaderApp::getReadingFontSize() {
    return s_readingFontSize;
}

// HTML Helpers
static void appendNormalizedChar(std::string& out, char c) {
    if (c == '\r' || c == '\t') {
        c = ' ';
    }
    if (c == '\n') {
        if (!out.empty() && out.back() != '\n') {
            out.push_back('\n');
        }
        return;
    }
    if (isspace(static_cast<unsigned char>(c))) {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
            out.push_back(' ');
        }
        return;
    }
    out.push_back(c);
}

static void appendBlockBreak(std::string& out) {
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    if (out.empty() || out.back() != '\n') {
        out.push_back('\n');
    }
}

static std::string decodeHtmlEntity(const std::string& entity) {
    if (entity == "nbsp") return " ";
    if (entity == "amp") return "&";
    if (entity == "lt") return "<";
    if (entity == "gt") return ">";
    if (entity == "quot") return "\"";
    if (entity == "apos") return "'";
    if (entity == "lsquo" || entity == "rsquo") return "'";
    if (entity == "ldquo" || entity == "rdquo") return "\"";
    if (entity == "mdash" || entity == "ndash") return "-";
    if (entity.size() > 1 && entity[0] == '#') {
        int base = 10;
        size_t start = 1;
        if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
            base = 16;
            start = 2;
        }
        char* end = nullptr;
        long value = strtol(entity.c_str() + start, &end, base);
        if (end && *end == '\0') {
            if (value == 160) return " ";
            if (value >= 32 && value <= 126) return std::string(1, static_cast<char>(value));
            if (value == 8216 || value == 8217 || value == 39) return "'"; // '
            if (value == 8220 || value == 8221) return "\""; // "
            if (value == 8211 || value == 8212) return "-"; // -
        }
    }
    return " ";
}

static char* stripHTML(const char* html, size_t len, size_t& outLen) {
    if (!html) {
        outLen = 0;
        return nullptr;
    }
    std::string text;
    text.reserve(len);
    for (size_t i = 0; i < len; i++) {
        char c = html[i];
        if (c == '<') {
            size_t tagEnd = i + 1;
            while (tagEnd < len && html[tagEnd] != '>') {
                tagEnd++;
            }
            std::string tag(html + i + 1, html + tagEnd);
            std::string lowerTag;
            lowerTag.reserve(tag.size());
            for (char ch : tag) {
                lowerTag.push_back(static_cast<char>(tolower(static_cast<unsigned char>(ch))));
            }
            if (lowerTag.rfind("br", 0) == 0 ||
                lowerTag.rfind("/p", 0) == 0 ||
                lowerTag.rfind("/div", 0) == 0 ||
                lowerTag.rfind("/h", 0) == 0 ||
                lowerTag.rfind("li", 0) == 0 ||
                lowerTag.rfind("/li", 0) == 0 ||
                lowerTag.rfind("/tr", 0) == 0) {
                appendBlockBreak(text);
            }
            i = tagEnd;
            continue;
        }
        if (c == '&') {
            size_t entityEnd = i + 1;
            while (entityEnd < len && html[entityEnd] != ';' && entityEnd - i <= 10) {
                entityEnd++;
            }
            if (entityEnd < len && html[entityEnd] == ';') {
                std::string entity(html + i + 1, html + entityEnd);
                std::string decoded = decodeHtmlEntity(entity);
                for (char dc : decoded) {
                    appendNormalizedChar(text, dc);
                }
                i = entityEnd;
                continue;
            }
        }
        appendNormalizedChar(text, c);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) {
        text.pop_back();
    }
    outLen = text.size();
#ifndef NATIVE_TESTING
    char* out = (char*)ps_malloc(outLen + 1);
    if (!out) out = (char*)malloc(outLen + 1);
#else
    char* out = (char*)malloc(outLen + 1);
#endif
    if (!out) {
        outLen = 0;
        return nullptr;
    }
    memcpy(out, text.c_str(), outLen);
    out[outLen] = '\0';
    return out;
}

std::string EReaderApp::htmlToText(const char* html, size_t len) {
    size_t outLen = 0;
    char* text = stripHTML(html, len, outLen);
    std::string result(text ? text : "", outLen);
    free(text);
    return result;
}

static size_t countReadableChars(const char* text, size_t len) {
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (!isspace(static_cast<unsigned char>(text[i]))) {
            count++;
        }
    }
    return count;
}

static bool isPreferredEpubChapter(const std::string& chapterPath) {
    std::string lower = chapterPath;
    for (char& c : lower) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (lower.find("cover") != std::string::npos ||
        lower.find("title") != std::string::npos ||
        lower.find("toc") != std::string::npos ||
        lower.find("contents") != std::string::npos ||
        lower.find("copyright") != std::string::npos ||
        lower.find("newsletter") != std::string::npos ||
        lower.find("abouttheauthor") != std::string::npos ||
        lower.find("acknowledg") != std::string::npos ||
        lower.find("adcard") != std::string::npos ||
        lower.find("torad") != std::string::npos ||
        lower.find("mini_toc") != std::string::npos ||
        lower.find("nav") != std::string::npos) {
        return false;
    }
    return lower.find("chapter") != std::string::npos ||
           lower.find("prologue") != std::string::npos ||
           lower.find("part") != std::string::npos;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
namespace {
using namespace UI;
constexpr int READ_MARGIN_X = 36;
constexpr int READ_TOP = 124;
constexpr int READ_BOTTOM = 108;
constexpr int MENU_PANEL_H = 330;
const Rect kSortRect(MARGIN, HEADER_H + 16, W - 2 * MARGIN, 50);
constexpr int LIST_TOP = HEADER_H + 82;
// Library rows are taller than UI::ROW_H to show a clearly visible cover.
constexpr int LIBRARY_ROW_H = 136;
constexpr int COVER_W = BookCover::THUMB_W;
constexpr int COVER_H = BookCover::THUMB_H;
constexpr int COVER_TOP = (LIBRARY_ROW_H - COVER_H) / 2;
const char* kSortLabels[] = {"Progress", "Title", "Author"};

Rect fontMinusRect() { return Rect(W - MARGIN - 250, H - MENU_PANEL_H + 22, 84, 58); }
Rect fontPlusRect() { return Rect(W - MARGIN - 84, H - MENU_PANEL_H + 22, 84, 58); }
// Reading menu chapter row: 0 = previous, 1 = contents, 2 = next.
Rect menuChapterRect(int i) {
    const int gap = 12;
    const int w = (W - 2 * MARGIN - 2 * gap) / 3;
    return Rect(MARGIN + i * (w + gap), H - MENU_PANEL_H + 150, w, 58);
}
const Rect kContentsButton(W - MARGIN - 132, 44, 132, 52);
} // namespace

// App Lifecycles
void EReaderApp::onCreate() {
    m_state = STATE_LIB;
    m_libraryPage = 0;
    m_menuOpen = false;
    m_chapterPopup = false;

    // Feedback before any SD work (cache check, per-book bookmark reads).
    drawOpeningLibraryScreen();
    updateLibraryItems();
}

void EReaderApp::onDestroy() {
    if (m_state == STATE_READ) {
        saveCurrentPosition();
        rememberLastBook();
    }
    if (m_currentBookText) {
        free(m_currentBookText);
        m_currentBookText = nullptr;
        m_currentBookTextLen = 0;
    }
    // Library covers are only wanted while the list is on screen; the open
    // book's own cover (for the sleep screen) may still finish.
    BookCover::cancelPending();
    requestCurrentBookCover();
    m_textReader.closeFile();
    m_epubParser.close();
    restoreTypographyDefaults();
    if (m_catalogDirty) LibraryCatalog::save(m_records);
    if (m_coverWorkBusy) {
        PowerManager::setBusy(false);
        m_coverWorkBusy = false;
    }
}

void EReaderApp::draw() {
    if (m_state == STATE_LIB) {
        drawLibrary();
    } else {
        drawReading();
    }
}

void EReaderApp::updateOpeningLibraryScreen(const std::string& detail) {
    UIFramework::performFastPartialUpdate(framebuffer, 0, H / 2 - 40, W, 80, [&detail]() {
        fillRoundRect(MARGIN * 3, H / 2 - 36, W - MARGIN * 6, 72, 16, WHITE);
        strokeRoundRect(MARGIN * 3, H / 2 - 36, W - MARGIN * 6, 72, 16, 2, BLACK);
        textCentered(detail, W / 2, H / 2 - 16, FONT_BODY, BLACK, true);
    });
}

void EReaderApp::drawOpeningLibraryScreen() {
    updateOpeningLibraryScreen("Opening library...");
}

void EReaderApp::updateLibraryItems() {
    // A persisted scan is only trusted across a sleep-resume (or after this
    // boot already scanned) with no content changes since - see
    // AppStorage::loadLibraryIndex. Otherwise walk /books again; the walk is
    // cheap now (one pass over the directory records) and per-book details
    // come from LibraryCatalog, so only new or changed books are opened.
    const uint32_t start = millis();
    AppStorage::LibraryIndex cached = AppStorage::loadLibraryIndex("/books");
    std::unordered_map<std::string, FileInfo> bookmarkFiles;
    const bool walked = !cached.valid;
    m_libraryListComplete = true;
    if (cached.valid) {
        m_libraryFiles.clear();
        for (const auto& e : cached.entries) {
            FileInfo f;
            f.path = e.path;
            size_t slashPos = e.path.find_last_of('/');
            f.name = (slashPos == std::string::npos) ? e.path : e.path.substr(slashPos + 1);
            f.isDirectory = false;
            f.size = e.size;
            f.mtime = e.mtime;
            m_libraryFiles.push_back(f);
        }
        printf("[EReader] Index cache hit: %zu books loaded in %ums\n",
               cached.entries.size(), (unsigned)(millis() - start));
    } else {
        rescanLibraryFromDisk(bookmarkFiles);
        m_libraryListComplete = m_fileBrowser.scanSucceeded();
    }

    syncLibraryCatalog(walked ? &bookmarkFiles : nullptr, "");
    printf("[EReader] Library ready in %ums\n", (unsigned)(millis() - start));
}

void EReaderApp::rescanLibraryFromDisk(std::unordered_map<std::string, FileInfo>& bookmarkFiles) {
    m_libraryFiles.clear();
    // Recursively scan every subdirectory of /books, not just the top level,
    // so books placed in nested folders (e.g. by genre/author) are found.
    // Only a very large tree takes long enough to need a progress count.
    const uint32_t scanStart = millis();
    uint32_t lastShown = 0;
    int entries = 0;
    std::vector<FileInfo> allFiles = m_fileBrowser.scanRecursive("/books", [&]() {
        entries++;
        const uint32_t now = millis();
        if (now - scanStart > 1500 && now - lastShown > 2500) {
            lastShown = now;
            updateOpeningLibraryScreen("Scanning... " + std::to_string(entries) + " files");
        }
    });

    std::vector<AppStorage::LibraryIndexEntry> indexEntries;
    for (auto& f : allFiles) {
        if (f.isDirectory) continue;

        std::string lowerName = f.name;
        for (char& c : lowerName) c = tolower(c);
        auto endsWith = [&lowerName](const char* suffix) {
            const size_t n = strlen(suffix);
            return lowerName.size() >= n && lowerName.compare(lowerName.size() - n, n, suffix) == 0;
        };
        if (endsWith(".bmk")) {
            bookmarkFiles[f.path] = f;
            continue;
        }
        if (!endsWith(".epub") && !endsWith(".txt") && !endsWith(".md") && !endsWith(".rtf")) continue;

        m_libraryFiles.push_back(f);
        AppStorage::LibraryIndexEntry entry;
        entry.path = f.path;
        entry.size = (uint32_t)f.size;
        entry.mtime = f.mtime;
        indexEntries.push_back(entry);
    }

    // Never persist a partial scan: an inaccessible directory or depth limit
    // must not make missing books disappear from every later cached launch.
    if (m_fileBrowser.scanSucceeded()) {
        AppStorage::saveLibraryIndex("/books", indexEntries);
    }
    printf("[EReader] Disk scan: %ums, %zu books, %d entries checked, complete=%d\n",
           (unsigned)(millis() - scanStart), indexEntries.size(), entries,
           m_fileBrowser.scanSucceeded() ? 1 : 0);
}

void EReaderApp::syncLibraryCatalog(const std::unordered_map<std::string, FileInfo>* bookmarkFiles,
                                    const std::string& refreshBookmarkFor) {
    // First-time EPUB parsing can take a while for a big new library on the
    // SD card: keep the "Opening library" card updated, but sparingly
    // (every e-ink refresh costs far more than parsing a book).
    const uint32_t start = millis();
    uint32_t lastShown = 0;
    LibraryCatalog::SyncStats stats;
    m_records = LibraryCatalog::sync(m_libraryFiles, bookmarkFiles, refreshBookmarkFor,
        [&](int done, int total) {
            const uint32_t now = millis();
            if (done < total && now - start > 1000 && now - lastShown > 2500) {
                lastShown = now;
                updateOpeningLibraryScreen("Reading books... " + std::to_string(done) + " of " + std::to_string(total));
            }
        }, &stats);
    LibraryCatalog::save(m_records);
    m_catalogDirty = false;
    if (stats.removed > 0 && m_libraryListComplete && !m_records.empty()) {
        // Books were deleted or replaced: drop their cached covers.
        std::vector<std::string> keep;
        for (const auto& record : m_records) keep.push_back(record.coverId());
        BookCover::pruneCache(keep);
    }
    printf("[EReader] Catalog: %zu books, %d parsed, %d bookmarks read, %ums\n",
           m_records.size(), stats.parsed, stats.bookmarksRead, (unsigned)(millis() - start));
    buildLibraryItems();
}

void EReaderApp::rebuildParsedLibraryItems() {
    syncLibraryCatalog(nullptr, AppStorage::toSdPath(m_currentBookPath));
}

void EReaderApp::buildLibraryItems() {
    m_parsedLibraryItems.clear();

    // Companion-app metadata (keyed by file name) wins over the EPUB's own.
    EBookmarkManager::getInstance().initialize();
    const auto& bmarks = EBookmarkManager::getInstance().getBookmarks();
    std::unordered_map<std::string, const EBookmark*> metadata;
    for (const auto& b : bmarks) metadata.emplace(b.isbn, &b);

    for (size_t i = 0; i < m_libraryFiles.size() && i < m_records.size(); i++) {
        const FileInfo& f = m_libraryFiles[i];
        const LibraryCatalog::Record& record = m_records[i];
        const bool isEpub = BookCover::isEpub(f.path);

        LibraryItem item;
        item.file = f;
        item.title = f.name;
        size_t dotPos = item.title.find_last_of('.');
        if (dotPos != std::string::npos) item.title = item.title.substr(0, dotPos);
        const std::string stem = item.title;
        item.author = "Unknown Author";
        if (!record.title.empty()) item.title = record.title;
        if (!record.author.empty()) item.author = record.author;

        const auto match = metadata.find(stem);
        if (match != metadata.end()) {
            const EBookmark& b = *match->second;
            if (!b.title.empty()) item.title = b.title;
            if (!b.author.empty()) item.author = b.author;
        }

        item.coverState = isEpub ? record.cover : (uint8_t)BookCover::STATE_NONE;
        item.coverId = isEpub ? record.coverId() : "";

        const AppStorage::BookmarkInfo& bmark = record.bookmark;
        item.completionPercent = 0;
        if (record.hasBookmark && isEpub) {
            // EPUB books are split across many per-chapter sub-files, so a
            // raw byte offset compared to the whole .epub (zip) file size is
            // meaningless - use chapter-granularity progress instead, which
            // is what's actually persisted (see AppStorage::BookmarkInfo).
            if (bmark.totalChapters > 0) {
                item.completionPercent = (bmark.chapterIndex * 100) / bmark.totalChapters;
                if (item.completionPercent > 100) item.completionPercent = 100;
                if (item.completionPercent == 0 && bmark.chapterIndex > 0) item.completionPercent = 1;
            }
        } else if (record.hasBookmark && bmark.offset > 0 && f.size > 0) {
            item.completionPercent = (int)(((int64_t)bmark.offset * 100) / (int64_t)f.size);
            if (item.completionPercent > 100) item.completionPercent = 100;
            if (item.completionPercent == 0) item.completionPercent = 1;
        }

        if (item.completionPercent >= 97) {
            item.completionPercent = -100;
        }

        m_parsedLibraryItems.push_back(item);
    }

    sortLibraryItems();
}

void EReaderApp::sortLibraryItems() {
    // Progress order: books in progress (most-read first), then unread,
    // then finished (completionPercent == -100).
    auto lower = [](const std::string& s) {
        std::string o = s;
        for (char& c : o) c = (char)tolower((unsigned char)c);
        return o;
    };
    std::stable_sort(m_parsedLibraryItems.begin(), m_parsedLibraryItems.end(),
        [&](const LibraryItem& a, const LibraryItem& b) {
            if (s_librarySort == LibrarySort::COMPLETION) {
                if (a.completionPercent != b.completionPercent) return a.completionPercent > b.completionPercent;
                return lower(a.title) < lower(b.title);
            }
            if (s_librarySort == LibrarySort::AUTHOR) {
                std::string la = lower(a.author), lb = lower(b.author);
                if (la != lb) return la < lb;
            }
            return lower(a.title) < lower(b.title);
        });
}

int EReaderApp::libraryRowsPerPage() const {
    return (H - FOOTER_H - LIST_TOP) / LIBRARY_ROW_H;
}

void EReaderApp::drawLibraryCover(const LibraryItem& item, int x, int y) {
    int cx = 0, cy = 0, cw = 0, ch = 0;
    if (item.coverState == BookCover::STATE_READY &&
        BookCover::drawThumbnail(item.coverId, framebuffer, x, y, COVER_W, COVER_H, &cx, &cy, &cw, &ch)) {
        // Hairline edge so pale covers don't melt into the page.
        strokeRoundRect(cx - 1, cy - 1, cw + 2, ch + 2, 0, 1, MID);
        return;
    }
    // Placeholder "book": also shown while the cover is still being made.
    fillRoundRect(x + 4, y, COVER_W - 8, COVER_H, 6, PALE);
    strokeRoundRect(x + 4, y, COVER_W - 8, COVER_H, 6, 2, LIGHT);
    std::string ext;
    size_t dot = item.file.name.find_last_of('.');
    if (dot != std::string::npos) ext = item.file.name.substr(dot + 1);
    for (char& c : ext) c = (char)toupper((unsigned char)c);
    textCentered(ext, x + COVER_W / 2, y + COVER_H / 2 - 14, FONT_SMALL, DARK, true);
}

void EReaderApp::requestVisibleCovers() {
    const int perPage = libraryRowsPerPage();
    const int start = m_libraryPage * perPage;
    BookCover::cancelPending();
    for (int i = 0; i < perPage && start + i < (int)m_parsedLibraryItems.size(); i++) {
        const LibraryItem& item = m_parsedLibraryItems[start + i];
        if (item.coverState == BookCover::STATE_UNKNOWN && !item.coverId.empty()) {
            BookCover::request(item.file.path, item.coverId);
        }
    }
}

void EReaderApp::requestCurrentBookCover() {
    if (m_currentBookPath.empty() || !BookCover::isEpub(m_currentBookPath)) return;
    BookCover::cancelPending();
    if (!m_epubParser.mayHaveCover()) return;
    const std::string sdPath = AppStorage::toSdPath(m_currentBookPath);
    const std::string id = BookCover::cacheIdForFile(sdPath);
    if (!id.empty() && !BookCover::hasLarge(id)) BookCover::request(sdPath, id);
}

void EReaderApp::update() {
    BookCover::pump();
    std::string id;
    BookCover::State state;
    while (BookCover::takeResult(id, state)) {
        for (auto& record : m_records) {
            if (record.cover != state && record.coverId() == id) {
                record.cover = state;
                m_catalogDirty = true;
            }
        }
        const int perPage = libraryRowsPerPage();
        for (size_t i = 0; i < m_parsedLibraryItems.size(); i++) {
            LibraryItem& item = m_parsedLibraryItems[i];
            if (item.coverId != id) continue;
            item.coverState = state;
            const int row = (int)i - m_libraryPage * perPage;
            if (m_state == STATE_LIB && state == BookCover::STATE_READY && row >= 0 && row < perPage) {
                const int x = MARGIN;
                const int y = LIST_TOP + row * LIBRARY_ROW_H + COVER_TOP;
                // One pixel larger than the box: the cover's hairline edge.
                UIFramework::performFastPartialUpdate(framebuffer, x - 1, y - 1, COVER_W + 2, COVER_H + 2, [this, &item, x, y]() {
                    drawLibraryCover(item, x, y);
                });
            }
        }
    }

    const bool coversBusy = BookCover::busy();
    // Full clock while a cover decodes (the main loop would drop to 80MHz).
    if (coversBusy) PowerManager::boostCpu();
    if (m_catalogDirty && !coversBusy) {
        LibraryCatalog::save(m_records);
        m_catalogDirty = false;
    }
    // Keep the CPU out of light sleep while covers decode in the background.
    if (coversBusy != m_coverWorkBusy) {
        m_coverWorkBusy = coversBusy;
        PowerManager::setBusy(coversBusy);
    }
}

uint32_t EReaderApp::msUntilUpdate() const {
    return (m_coverWorkBusy || BookCover::busy()) ? 0 : UINT32_MAX;
}

void EReaderApp::drawLibrary() {
    // Sorting is in-memory. Bookmark metadata is rebuilt only after the scan
    // and when returning from a book, not on every redraw/page tap.
    sortLibraryItems();

    int perPage = libraryRowsPerPage();
    int pages = ((int)m_parsedLibraryItems.size() + perPage - 1) / perPage;
    if (pages < 1) pages = 1;
    if (m_libraryPage >= pages) m_libraryPage = pages - 1;

    UIFramework::performFullScreenDraw(framebuffer, [this, perPage, pages]() {
        std::string title = "Library";
        if (!m_parsedLibraryItems.empty()) title += "  (" + std::to_string(m_parsedLibraryItems.size()) + ")";
        header(title, true);

        int sel = s_librarySort == LibrarySort::TITLE ? 1 : (s_librarySort == LibrarySort::AUTHOR ? 2 : 0);
        segmented(kSortRect, {kSortLabels[0], kSortLabels[1], kSortLabels[2]}, sel, FONT_BODY);

        if (m_parsedLibraryItems.empty()) {
            textCentered("No books yet", W / 2, H / 2 - 36, FONT_BODY + 2, BLACK, true);
            textWrapped("Copy EPUB, TXT, MD or RTF files into /books on the SD card, or send them from the companion app's Library tab.",
                        MARGIN * 2, H / 2 + 8, W - MARGIN * 4, FONT_SMALL + 4, DARK, 4, 8, true);
            return;
        }

        int start = m_libraryPage * perPage;
        for (int i = 0; i < perPage && start + i < (int)m_parsedLibraryItems.size(); i++) {
            const LibraryItem& item = m_parsedLibraryItems[start + i];
            std::string trailing;
            float progress = -1.0f;
            if (item.completionPercent == -100) {
                trailing = "Done";
            } else if (item.completionPercent > 0) {
                trailing = std::to_string(item.completionPercent) + "%";
                progress = item.completionPercent / 100.0f;
            } else {
                trailing = "New";
            }
            std::string sub = item.author == "Unknown Author" ? "" : item.author;
            if (sub.empty()) {
                // No metadata: show where it lives and what it is instead.
                std::string ext;
                size_t dot = item.file.name.find_last_of('.');
                if (dot != std::string::npos) ext = item.file.name.substr(dot + 1);
                for (char& c : ext) c = (char)toupper((unsigned char)c);
                char size[24];
                if (item.file.size >= 1024 * 1024) snprintf(size, sizeof(size), "%.1f MB", item.file.size / 1048576.0);
                else snprintf(size, sizeof(size), "%u KB", (unsigned)((item.file.size + 1023) / 1024));
                std::string folder;
                size_t lastSlash = item.file.path.find_last_of('/');
                if (lastSlash != std::string::npos && lastSlash > 7) { // deeper than "/books/"
                    std::string dir = item.file.path.substr(0, lastSlash);
                    folder = dir.substr(dir.find_last_of('/') + 1) + "  \xC2\xB7  ";
                }
                sub = folder + ext + "  \xC2\xB7  " + size;
            }
            const int y = LIST_TOP + i * LIBRARY_ROW_H;
            drawLibraryCover(item, MARGIN, y + COVER_TOP);
            const int textX = MARGIN + COVER_W + 18;
            const int right = W - MARGIN;
            const int trailingW = textWidth(trailing, FONT_BODY, true);
            const int titleW = right - trailingW - 16 - textX;
            // Up to two title lines; the second is ellipsized.
            std::string line1 = item.title, line2;
            if (textWidth(line1, FONT_BODY, true) > titleW) {
                size_t cut = std::string::npos;
                for (size_t sp = item.title.find(' '); sp != std::string::npos; sp = item.title.find(' ', sp + 1)) {
                    if (textWidth(item.title.substr(0, sp), FONT_BODY, true) > titleW) break;
                    cut = sp;
                }
                if (cut != std::string::npos) {
                    line1 = item.title.substr(0, cut);
                    line2 = ellipsize(item.title.substr(cut + 1), right - textX, FONT_BODY, true);
                } else {
                    line1 = ellipsize(item.title, titleW, FONT_BODY, true);
                }
            }
            int ty = y + 14;
            text(line1, textX, ty, FONT_BODY, BLACK, true);
            ty += lineHeight(FONT_BODY) + 4;
            if (!line2.empty()) {
                text(line2, textX, ty, FONT_BODY, BLACK, true);
                ty += lineHeight(FONT_BODY) + 4;
            }
            text(ellipsize(sub, right - textX, FONT_SMALL + 4), textX, ty + 2, FONT_SMALL + 4, DARK);
            textRight(trailing, right, y + 14, FONT_BODY, INK, true);
            if (progress >= 0.0f) progressBar(textX, y + LIBRARY_ROW_H - 22, right - textX, 6, progress);
            hRule(y + LIBRARY_ROW_H - 1, LIGHT, MARGIN, W - MARGIN);
        }
        pager(m_libraryPage, pages);
    });
    requestVisibleCovers();
}

void EReaderApp::prepareTypographyForReading() {
    typography.setTopMargin(READ_TOP);
    typography.setBottomMargin(READ_BOTTOM);
    typography.setHorizontalMargins(READ_MARGIN_X, READ_MARGIN_X);
    typography.setFontSize(getReadingFontSize());
}

void EReaderApp::restoreTypographyDefaults() {
    typography.setTopMargin(20);
    typography.setBottomMargin(20);
    typography.setHorizontalMargins(20, 20);
}

float EReaderApp::currentProgress() const {
    if (m_textReader.isOpen()) {
        size_t size = m_textReader.getFileSize();
        if (size == 0) return 0.0f;
        float p = (float)(m_textReader.getPosition() + (size_t)m_currentReadingOffset) / (float)size;
        return p > 1.0f ? 1.0f : p;
    }
    if (m_chapters.empty() || m_currentChapterIndex < 0) {
        return m_currentBookTextLen ? (float)m_currentReadingOffset / (float)m_currentBookTextLen : 0.0f;
    }
    float within = m_currentBookTextLen ? (float)m_currentReadingOffset / (float)m_currentBookTextLen : 0.0f;
    float p = (m_currentChapterIndex + within) / (float)m_chapters.size();
    return p > 1.0f ? 1.0f : p;
}

void EReaderApp::rememberLastBook() {
    if (m_currentBookPath.empty()) return;
    AppStorage::LastBook last;
    last.path = m_currentBookPath;
    last.title = m_currentBookTitle;
    last.author = m_currentBookAuthor;
    last.percent = (int)(currentProgress() * 100.0f + 0.5f);
    last.valid = true;
    AppStorage::saveLastBook(last);
}

void EReaderApp::drawReading(bool quick) {
    int every = SystemSettings::get().fullRefreshEvery;
    if (quick && every > 1 && m_turnsSinceFullRefresh + 1 < every) {
        m_turnsSinceFullRefresh++;
    } else {
        quick = false;
        m_turnsSinceFullRefresh = 0;
    }
    // May read the TOC/chapter file once, so do it before drawing starts.
    const std::string chapterLabel = currentChapterLabel();
    auto render = [this, chapterLabel]() {
        prepareTypographyForReading();

        if (m_currentBookText) {
            static float s_lastFontSize = getReadingFontSize();
            if (s_lastFontSize != getReadingFontSize()) {
                m_pageHistory.clear();
                s_lastFontSize = getReadingFontSize();
            }
            size_t remainingLen = m_currentBookTextLen - m_currentReadingOffset;
            typography.setFontSize(getReadingFontSize());
            typography.renderText(m_currentBookText + m_currentReadingOffset, remainingLen,
                                  READ_MARGIN_X, READ_TOP, framebuffer, BLACK);
        } else {
            textCentered("Couldn't open this book", W / 2, H / 2 - 36, FONT_BODY + 2, BLACK, true);
            textWrapped("The file may be damaged or in an unsupported format.",
                        MARGIN * 2, H / 2 + 8, W - MARGIN * 4, FONT_SMALL + 4, DARK, 3, 8, true);
        }

        // Standard portrait chrome keeps the reading text clear of the title
        // and gives chapter selection a large, explicit touch target.
        statusBar();
        chevronLeft(MARGIN + 10, 70, 30);
        text(ellipsize(m_currentBookTitle, kContentsButton.x - MARGIN - 56, FONT_TITLE, true),
             MARGIN + 40, 50, FONT_TITLE, BLACK, true);
        if (m_currentBookText) {
            button(kContentsButton, "Contents", ButtonStyle::SECONDARY, FONT_SMALL + 4);
        }
        fillRect(0, HEADER_H - 2, W, 2, BLACK);

        float progress = currentProgress();
        int pct = (int)(progress * 100.0f + 0.5f);
        if (!chapterLabel.empty()) {
            text(ellipsize(chapterLabel, W - 2 * READ_MARGIN_X - 100, FONT_SMALL + 4),
                 READ_MARGIN_X, H - 74, FONT_SMALL + 4, DARK);
        }
        textRight(std::to_string(pct) + "%", W - READ_MARGIN_X, H - 74, FONT_SMALL + 4, DARK);
        fillRect(READ_MARGIN_X, H - 24, W - 2 * READ_MARGIN_X, 2, LIGHT);
        fillRect(READ_MARGIN_X, H - 25, (int)((W - 2 * READ_MARGIN_X) * progress), 4, BLACK);

        if (m_menuOpen) drawReadingMenu();
    };
    if (quick) UIFramework::performQuickFullScreenDraw(framebuffer, render);
    else UIFramework::performFullScreenDraw(framebuffer, render);
}

void EReaderApp::drawReadingMenu() {
    // Top: standard header with a back chevron to the library.
    fillRect(0, 0, W, HEADER_H, WHITE);
    header(m_currentBookTitle, true);

    // Bottom panel: text size and position.
    int top = H - MENU_PANEL_H;
    fillRect(0, top, W, MENU_PANEL_H, WHITE);
    fillRect(0, top, W, 2, BLACK);
    text("Text size", MARGIN, top + 38, FONT_BODY, BLACK, true);
    Rect minus = fontMinusRect();
    Rect plus = fontPlusRect();
    float size = getReadingFontSize();
    button(minus, "A-", ButtonStyle::SECONDARY, FONT_SMALL + 4, size > 16.0f);
    button(plus, "A+", ButtonStyle::SECONDARY, FONT_BODY, size < 48.0f);
    textCentered(std::to_string((int)size), (minus.x + minus.w + plus.x) / 2, top + 38, FONT_BODY, BLACK, true);

    hRule(top + 98, LIGHT);
    text("Chapter", MARGIN, top + 108, FONT_BODY, BLACK, true);
    const std::string chapter = currentChapterLabel();
    const int chapterX = MARGIN + textWidth("Chapter", FONT_BODY, true) + 20;
    if (!chapter.empty()) {
        textRight(ellipsize(chapter, W - MARGIN - chapterX, FONT_SMALL + 4), W - MARGIN, top + 110,
                  FONT_SMALL + 4, DARK);
    }
    const int prev = adjacentContentsEntry(false);
    const int next = adjacentContentsEntry(true);
    button(menuChapterRect(0), "Previous", ButtonStyle::SECONDARY, FONT_SMALL + 4, prev != -1);
    button(menuChapterRect(1), "Contents", ButtonStyle::SECONDARY, FONT_SMALL + 4);
    button(menuChapterRect(2), "Next", ButtonStyle::SECONDARY, FONT_SMALL + 4, next != -1);

    float progress = currentProgress();
    text(std::to_string((int)(progress * 100.0f + 0.5f)) + "% read", MARGIN, top + 230, FONT_SMALL + 4, INK);
    progressBar(MARGIN, top + 268, W - 2 * MARGIN, 8, progress);
    textCentered("Tap the page to keep reading", W / 2, top + 290, FONT_SMALL + 4, MID);
}

void EReaderApp::leaveReading() {
    saveCurrentPosition();
    rememberLastBook();
    m_textReader.closeFile();
    m_state = STATE_LIB;
    m_menuOpen = false;
    m_chapterPopup = false;
    restoreTypographyDefaults();
    rebuildParsedLibraryItems();
    drawLibrary();
}

void EReaderApp::turnPage(bool forward) {
    prepareTypographyForReading();
    if (forward) {
        if (m_textReader.isOpen()) {
            size_t nextStart = typography.findNextPageStart(m_currentBookText, m_currentBookTextLen, m_currentReadingOffset);
            if (nextStart > (size_t)m_currentReadingOffset && nextStart < m_currentBookTextLen) {
                m_pageHistory.push_back(m_currentReadingOffset);
                m_currentReadingOffset = nextStart;
            } else {
                m_pageHistory.clear();
                m_textReader.nextPage();
                std::string text = m_textReader.getPageText();
                if (m_currentBookText) free(m_currentBookText);
                m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
                m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
                if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
                m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
                if (m_currentBookText) memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
                m_currentReadingOffset = 0;
            }
        } else {
            size_t nextStart = typography.findNextPageStart(m_currentBookText, m_currentBookTextLen, m_currentReadingOffset);
            if (nextStart > (size_t)m_currentReadingOffset && nextStart < m_currentBookTextLen) {
                m_pageHistory.push_back(m_currentReadingOffset);
                m_currentReadingOffset = nextStart;
            } else if (m_currentChapterIndex >= 0 && m_currentChapterIndex + 1 < (int)m_chapters.size()) {
                if (loadChapter(m_currentChapterIndex + 1)) {
                    m_currentReadingOffset = 0;
                }
            }
        }
    } else {
        if (m_textReader.isOpen()) {
            if (!m_pageHistory.empty()) {
                m_currentReadingOffset = m_pageHistory.back();
                m_pageHistory.pop_back();
            } else if (m_textReader.getPosition() > 0) {
                m_textReader.prevPage();
                std::string text = m_textReader.getPageText();
                if (m_currentBookText) free(m_currentBookText);
                m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
                m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
                if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
                m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
                if (m_currentBookText) {
                    memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
                    m_currentReadingOffset = typography.findPreviousPageStart(m_currentBookText, m_currentBookTextLen, m_currentBookTextLen);
                }
            } else {
                m_currentReadingOffset = 0;
            }
        } else {
            if (!m_pageHistory.empty()) {
                m_currentReadingOffset = m_pageHistory.back();
                m_pageHistory.pop_back();
            } else if (m_currentReadingOffset == 0 && m_currentChapterIndex > 0) {
                if (loadChapter(m_currentChapterIndex - 1)) {
                    m_currentReadingOffset = typography.findPreviousPageStart(
                        m_currentBookText, m_currentBookTextLen, m_currentBookTextLen);
                    m_pageHistory.clear();
                }
            } else {
                m_currentReadingOffset = typography.findPreviousPageStart(m_currentBookText, m_currentBookTextLen, m_currentReadingOffset);
            }
        }
    }
    saveCurrentPosition();
    drawReading(true);
}

bool EReaderApp::loadChapter(int index) {
    if (m_chapters.empty() || index < 0 || index >= (int)m_chapters.size()) return false;

    size_t size = 0;
    char* html = m_epubParser.getFileContent(m_chapters[index], size);
    if (!html) return false;
    
    size_t strippedLen = 0;
    char* stripped = stripHTML(html, size, strippedLen);
    if (stripped) resolveContentsAnchors(index, html, size, stripped, strippedLen);
    free(html);
    if (!stripped) return false;
    
    if (m_currentBookText) {
        free(m_currentBookText);
    }
    m_currentBookText = stripped;
    m_currentBookTextLen = strippedLen;
    m_currentChapterIndex = index;
    m_pageHistory.clear();
    return true;
}

void EReaderApp::openBook(int index) {
    if (index < 0 || index >= (int)m_libraryFiles.size()) return;
    
    m_currentBookTitle = "";
    m_currentBookAuthor = "";
    for (const auto& item : m_parsedLibraryItems) {
        if (item.file.path == m_libraryFiles[index].path) {
            m_currentBookTitle = item.title;
            m_currentBookAuthor = item.author;
            break;
        }
    }
    if (m_currentBookTitle.empty()) {
        m_currentBookTitle = m_libraryFiles[index].name;
        size_t dotPos = m_currentBookTitle.find_last_of('.');
        if (dotPos != std::string::npos) {
            m_currentBookTitle = m_currentBookTitle.substr(0, dotPos);
        }
        m_currentBookAuthor = "Unknown Author";
    }
    
    std::string path = AppStorage::toRuntimePath(m_libraryFiles[index].path);
    openBookAtRuntimePath(path);
}

void EReaderApp::resumeAtPath(const std::string& runtimePath) {
    // Derive a title/author the same way openBook() falls back to when a
    // book isn't found in parsed library metadata - the library may not
    // have been scanned yet this boot (resuming right after a deep-sleep
    // wake, before the user has ever opened the library view).
    m_currentBookTitle = runtimePath;
    size_t slashPos = runtimePath.find_last_of('/');
    if (slashPos != std::string::npos) {
        m_currentBookTitle = runtimePath.substr(slashPos + 1);
    }
    size_t dotPos = m_currentBookTitle.find_last_of('.');
    if (dotPos != std::string::npos) {
        m_currentBookTitle = m_currentBookTitle.substr(0, dotPos);
    }
    m_currentBookAuthor = "Unknown Author";

    openBookAtRuntimePath(runtimePath);
}

void EReaderApp::saveCurrentPosition() {
    if (m_state != STATE_READ || m_currentBookPath.empty()) return;

    if (m_textReader.isOpen()) {
        // TextReader paginates in large chunks (see TextReader::getPageText()):
        // m_textReader.getPosition() is the absolute file offset of the START
        // of the currently loaded chunk, and m_currentReadingOffset is the
        // on-screen sub-page's offset WITHIN that chunk. The exact absolute
        // file position of the page currently on screen is their sum.
        AppStorage::saveBookmark(m_currentBookPath,
            (int)(m_textReader.getPosition() + (size_t)m_currentReadingOffset));
    } else {
        // EPUB (or any non-TextReader path): m_currentReadingOffset is an
        // offset into the CURRENTLY LOADED CHAPTER's text only - chapters
        // are separate sub-files, so the chapter index must be persisted
        // too, or resume will reopen whichever chapter the "find preferred
        // starting chapter" heuristic guesses and misapply this offset there.
        AppStorage::saveBookmark(m_currentBookPath, m_currentReadingOffset,
            m_currentChapterIndex, (int)m_chapters.size());
    }
}

void EReaderApp::openBookAtRuntimePath(const std::string& path) {
    AppScreens::drawLoading(framebuffer, m_currentBookTitle.c_str());
    m_textReader.closeFile();
    m_epubParser.close();
    m_chapters.clear();
    m_currentChapterIndex = -1;
    m_chapterError.clear();
    m_chapterPage = 0;
    m_chapterPopup = false;
    m_contents.clear();
    m_contentsLoaded = false;
    m_contentsFromSpine = false;

    bool isText = false;
    if (path.length() >= 4 && (path.substr(path.length() - 4) == ".txt" || path.substr(path.length() - 4) == ".rtf" || path.substr(path.length() - 4) == ".RTF")) isText = true;
    if (path.length() >= 3 && path.substr(path.length() - 3) == ".md") isText = true;

    // Loaded up front (before opening EPUB chapters) so a saved chapter
    // index can steer which chapter gets loaded below, instead of always
    // re-running the "guess a starting chapter" heuristic meant only for a
    // book that's never been opened before.
    AppStorage::BookmarkInfo bmark = AppStorage::loadBookmarkInfo(path);
    bool hasValidSavedEpubPosition = false;

    if (m_currentBookText) {
        free(m_currentBookText);
        m_currentBookText = nullptr;
        m_currentBookTextLen = 0;
    }

    if (isText) {
        if (m_textReader.openFile(path.c_str())) {
            std::string text = m_textReader.getPageText();
            m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
            m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
            if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
            m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
            if (m_currentBookText) {
                memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
            }
        }
    } else if (m_epubParser.open(path)) {
        m_chapters = m_epubParser.getChapterList();
        // The running footer names the current chapter, so the TOC is
        // needed for the first page anyway; reading it before any chapter
        // loads also lets loadChapter() resolve TOC anchors in passing.
        ensureContents();
        if (!m_epubParser.getTitle().empty()) m_currentBookTitle = m_epubParser.getTitle();
        if (!m_epubParser.getAuthor().empty()) m_currentBookAuthor = m_epubParser.getAuthor();
        m_currentChapterIndex = m_chapters.empty() ? -1 : 0;

        // If we have a saved chapter for THIS exact chapter layout (guard
        // against a stale bookmark from a different/edited copy of a
        // similarly-named book), jump straight to it instead of guessing.
        bool haveSavedChapter = bmark.chapterIndex >= 0 &&
            bmark.chapterIndex < (int)m_chapters.size() &&
            bmark.totalChapters == (int)m_chapters.size();
        if (haveSavedChapter) {
            hasValidSavedEpubPosition = loadChapter(bmark.chapterIndex);
        }

        if (!m_currentBookText) {
            char* fallbackText = nullptr;
            size_t fallbackLen = 0;
            int fallbackIndex = 0;
            for (int i = 0; i < (int)m_chapters.size(); i++) {
                size_t size = 0;
                char* html = m_epubParser.getFileContent(m_chapters[i], size);
                if (!html) continue;

                size_t strippedLen = 0;
                char* stripped = stripHTML(html, size, strippedLen);
                free(html);
                if (!stripped) continue;

                size_t readableChars = countReadableChars(stripped, strippedLen);
                if (isPreferredEpubChapter(m_chapters[i]) && readableChars >= 40) {
                    m_currentBookText = stripped;
                    m_currentBookTextLen = strippedLen;
                    m_currentChapterIndex = i;
                    break;
                }
                if (!fallbackText && readableChars > 0) {
                    fallbackText = stripped;
                    fallbackLen = strippedLen;
                    fallbackIndex = i;
                } else {
                    free(stripped);
                }
            }
            if (!m_currentBookText && fallbackText) {
                m_currentBookText = fallbackText;
                m_currentBookTextLen = fallbackLen;
                m_currentChapterIndex = fallbackIndex;
            }
        }
    }

    m_currentBookPath = path;
    int savedOffset = bmark.offset;
    if (!isText && !hasValidSavedEpubPosition) savedOffset = 0;

    if (isText && m_textReader.isOpen()) {
        m_textReader.setPosition(savedOffset);
        std::string text = m_textReader.getPageText();
        if (m_currentBookText) free(m_currentBookText);
        m_currentBookTextLen = text.length();
#ifndef NATIVE_TESTING
        m_currentBookText = (char*)ps_malloc(m_currentBookTextLen + 1);
        if (!m_currentBookText) m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#else
        m_currentBookText = (char*)malloc(m_currentBookTextLen + 1);
#endif
        if (m_currentBookText) {
            memcpy(m_currentBookText, text.c_str(), m_currentBookTextLen + 1);
        }
        m_currentReadingOffset = 0;
    } else {
        m_currentReadingOffset = savedOffset;
        if (m_currentReadingOffset < 0) m_currentReadingOffset = 0;
        if (m_currentReadingOffset > (int)m_currentBookTextLen) m_currentReadingOffset = 0;
        m_pageHistory.clear();
    }

    m_state = STATE_READ;
    m_menuOpen = false;
    rememberLastBook();
    requestCurrentBookCover();
    drawReading();
}

void EReaderApp::handleTouch(int x, int y) {
    if (m_state == STATE_LIB) {
        int seg = hitSegmented(kSortRect, 3, x, y);
        if (seg >= 0) {
            LibrarySort next = seg == 0 ? LibrarySort::COMPLETION : (seg == 1 ? LibrarySort::TITLE : LibrarySort::AUTHOR);
            if (next != s_librarySort) {
                s_librarySort = next;
                m_libraryPage = 0;
                drawLibrary();
            }
            return;
        }
        int perPage = libraryRowsPerPage();
        int pages = ((int)m_parsedLibraryItems.size() + perPage - 1) / perPage;
        int dir = hitPager(x, y, m_libraryPage, pages);
        if (dir != 0) {
            m_libraryPage += dir;
            drawLibrary();
            return;
        }
        if (y >= LIST_TOP && y < LIST_TOP + perPage * LIBRARY_ROW_H) {
            int idx = m_libraryPage * perPage + (y - LIST_TOP) / LIBRARY_ROW_H;
            if (idx >= 0 && idx < (int)m_parsedLibraryItems.size()) {
                std::string targetPath = m_parsedLibraryItems[idx].file.path;
                for (size_t i = 0; i < m_libraryFiles.size(); i++) {
                    if (m_libraryFiles[i].path == targetPath) {
                        openBook((int)i);
                        break;
                    }
                }
            }
        }
        return;
    }

    // Reading
    if (m_chapterPopup) {
        handleContentsTouch(x, y);
        return;
    }

    if (m_menuOpen) {
        if (hitBack(x, y)) {
            leaveReading();
            return;
        }
        if (y >= H - MENU_PANEL_H) {
            float size = getReadingFontSize();
            float next = size;
            if (fontMinusRect().contains(x, y) && size > 16.0f) next = size - 2.0f;
            if (fontPlusRect().contains(x, y) && size < 48.0f) next = size + 2.0f;
            if (next != size) {
                setReadingFontSize(next);
                SystemSettings::get().readingFontSize = next;
                SystemSettings::save();
                m_pageHistory.clear();
                drawReading();
                return;
            }
            if (menuChapterRect(1).contains(x, y)) {
                openContents();
                return;
            }
            if (menuChapterRect(0).contains(x, y)) goToAdjacentChapter(false);
            if (menuChapterRect(2).contains(x, y)) goToAdjacentChapter(true);
            return;
        }
        if (y < HEADER_H) return;
        m_menuOpen = false;
        drawReading();
        return;
    }

    if (y < HEADER_H) {
        if (hitBack(x, y)) {
            leaveReading();
            return;
        }
        if (m_currentBookText && x >= kContentsButton.x && y >= STATUS_H) {
            openContents();
            return;
        }
        m_menuOpen = true;
        // Only the overlay areas change - no need for a full-page flash.
        UIFramework::performFastPartialUpdate(framebuffer, 0, 0, W, HEADER_H, [this]() {
            header(m_currentBookTitle, true);
        });
        UIFramework::performFastPartialUpdate(framebuffer, 0, H - MENU_PANEL_H, W, MENU_PANEL_H, [this]() {
            int top = H - MENU_PANEL_H;
            fillRect(0, top, W, MENU_PANEL_H, WHITE);
            drawReadingMenu();
        });
        return;
    }
    turnPage(x >= W * 35 / 100);
}
