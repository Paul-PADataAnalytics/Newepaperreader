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
    m_textReader.closeFile();
    m_epubParser.close();
    restoreTypographyDefaults();
}

void EReaderApp::draw() {
    if (m_state == STATE_LIB) {
        drawLibrary();
    } else {
        drawReading();
    }
}

void EReaderApp::drawScanningProgressScreen(int dirsVisited) {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        header("Library", true);
        textCentered("Scanning your books...", W / 2, H / 2 - 60, FONT_BODY, BLACK, true);
    });
    updateScanProgressCount(dirsVisited);
}

void EReaderApp::updateScanProgressCount(int dirsVisited) {
    UIFramework::performFastPartialUpdate(framebuffer, 0, H / 2 - 10, W, 40, [dirsVisited]() {
        textCentered(std::to_string(dirsVisited) + " items checked", W / 2, H / 2 - 6, FONT_SMALL + 4, DARK);
    });
}

void EReaderApp::drawOpeningLibraryScreen() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, H / 2 - 40, W, 80, []() {
        fillRoundRect(MARGIN * 3, H / 2 - 36, W - MARGIN * 6, 72, 16, WHITE);
        strokeRoundRect(MARGIN * 3, H / 2 - 36, W - MARGIN * 6, 72, 16, 2, BLACK);
        textCentered("Opening library...", W / 2, H / 2 - 16, FONT_BODY, BLACK, true);
    });
}

void EReaderApp::updateLibraryItems() {
    // A persisted scan is only trusted across a sleep-resume with no content
    // changes since (see AppStorage::loadLibraryIndex) - on a fresh power-on,
    // or after any add/move/delete elsewhere marks it dirty, we fall back to
    // a real scan. This is what keeps "extremely slow" flash re-scans out of
    // the common case (every wake-from-sleep launch) while still catching a
    // swapped SD card or newly-synced books.
    const uint32_t cacheStart = millis();
    AppStorage::LibraryIndex cached = AppStorage::loadLibraryIndex("/books");
    if (cached.valid) {
        m_libraryFiles.clear();
        for (const auto& e : cached.entries) {
            FileInfo f;
            f.path = e.path;
            size_t slashPos = e.path.find_last_of('/');
            f.name = (slashPos == std::string::npos) ? e.path : e.path.substr(slashPos + 1);
            f.isDirectory = false;
            f.size = e.size;
            m_libraryFiles.push_back(f);
        }
        printf("[EReader] Index cache hit: %zu books loaded in %ums\n",
               cached.entries.size(), (unsigned)(millis() - cacheStart));
    } else {
        printf("[EReader] Index cache miss; rescanning /books\n");
        rescanLibraryFromDisk();
    }

    rebuildParsedLibraryItems();
}

void EReaderApp::rescanLibraryFromDisk() {
    m_libraryFiles.clear();
    BookCover::clearCache();

    // Recursively scan every subdirectory of /books, not just the top level,
    // so books placed in nested folders (e.g. by genre/author) are found.
    // If the scan takes longer than 250ms, show a "Scanning..." screen so the
    // device doesn't appear frozen; for scans that run much longer than that
    // (hundreds of books across many folders, or a single large flat folder -
    // scanRecursive reports progress per file as well as per directory),
    // keep the folder count updated periodically (throttled to avoid
    // spending more time redrawing than scanning) via a cheap scoped partial
    // update rather than a full redraw.
    uint32_t scanStart = millis();
    uint32_t lastProgressUpdate = 0;
    bool progressShown = false;
    int dirsVisited = 0;
    const uint32_t PROGRESS_UPDATE_INTERVAL_MS = 750;

    std::vector<FileInfo> allFiles = m_fileBrowser.scanRecursive("/books", [&]() {
        dirsVisited++;
        uint32_t now = millis();
        if (!progressShown) {
            if (now - scanStart > 250) {
                progressShown = true;
                lastProgressUpdate = now;
                drawScanningProgressScreen(dirsVisited);
            }
        } else if (now - lastProgressUpdate > PROGRESS_UPDATE_INTERVAL_MS) {
            lastProgressUpdate = now;
            updateScanProgressCount(dirsVisited);
        }
    });

    std::vector<AppStorage::LibraryIndexEntry> indexEntries;
    for (auto& f : allFiles) {
        if (f.isDirectory) continue;

        std::string lowerName = f.name;
        for (char& c : lowerName) c = tolower(c);
        bool isEpub = false;
        if (lowerName.length() >= 5 && lowerName.substr(lowerName.length() - 5) == ".epub") isEpub = true;
        else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".txt") {}
        else if (lowerName.length() >= 3 && lowerName.substr(lowerName.length() - 3) == ".md") {}
        else if (lowerName.length() >= 4 && lowerName.substr(lowerName.length() - 4) == ".rtf") {}
        else continue;

        m_libraryFiles.push_back(f);
        AppStorage::LibraryIndexEntry entry;
        entry.path = f.path;
        entry.size = (uint32_t)f.size;
        indexEntries.push_back(entry);
    }

    // Never persist a partial scan: an inaccessible directory or depth limit
    // must not make missing books disappear from every later cached launch.
    if (m_fileBrowser.scanSucceeded()) {
        AppStorage::saveLibraryIndex("/books", indexEntries);
    }
    printf("[EReader] Disk scan: %ums, %zu books, %d entries checked, complete=%d\n",
           (unsigned)(millis() - scanStart), indexEntries.size(), dirsVisited,
           m_fileBrowser.scanSucceeded() ? 1 : 0);
}

void EReaderApp::rebuildParsedLibraryItems() {
    m_parsedLibraryItems.clear();

    EBookmarkManager::getInstance().initialize();
    const auto& bmarks = EBookmarkManager::getInstance().getBookmarks();

    std::unordered_map<std::string, const EBookmark*> metadata;
    for (const auto& b : bmarks) metadata.emplace(b.isbn, &b);

    for (auto& f : m_libraryFiles) {
        std::string lowerName = f.name;
        for (char& c : lowerName) c = tolower(c);
        bool isEpub = lowerName.length() >= 5 && lowerName.substr(lowerName.length() - 5) == ".epub";

        LibraryItem item;
        item.file = f;
        item.title = f.name;
        item.author = "Unknown Author";

        size_t dotPos = item.title.find_last_of('.');
        if (dotPos != std::string::npos) {
            item.title = item.title.substr(0, dotPos);
        }

        const auto match = metadata.find(item.title);
        if (match != metadata.end()) {
            const EBookmark& b = *match->second;
            if (!b.title.empty()) item.title = b.title;
            if (!b.author.empty()) item.author = b.author;
        }

        // Use the file's own fully-qualified path (not "/books/" + name) so
        // bookmarks for books in nested subdirectories resolve correctly.
        std::string runtimePath = AppStorage::toRuntimePath(f.path);
        if (isEpub) {
            // EPUB books are split across many per-chapter sub-files, so a
            // raw byte offset compared to the whole .epub (zip) file size is
            // meaningless - use chapter-granularity progress instead, which
            // is what's actually persisted (see AppStorage::BookmarkInfo).
            AppStorage::BookmarkInfo bmark = AppStorage::loadBookmarkInfo(runtimePath);
            if (bmark.totalChapters > 0) {
                item.completionPercent = (bmark.chapterIndex * 100) / bmark.totalChapters;
                if (item.completionPercent > 100) item.completionPercent = 100;
                if (item.completionPercent == 0 && bmark.chapterIndex > 0) item.completionPercent = 1;
            } else {
                item.completionPercent = 0;
            }
        } else {
            int offset = AppStorage::loadBookmark(runtimePath);
            if (offset > 0 && f.size > 0) {
                item.completionPercent = (offset * 100) / f.size;
                if (item.completionPercent > 100) item.completionPercent = 100;
                if (item.completionPercent == 0) item.completionPercent = 1;
            } else {
                item.completionPercent = 0;
            }
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
    return (H - FOOTER_H - LIST_TOP) / ROW_H;
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
            const int y = LIST_TOP + i * ROW_H;
            BookCover::draw(item.file.path, framebuffer, MARGIN + 2, y + 10, 48, 74);
            const int textX = MARGIN + 66;
            const int right = W - MARGIN;
            const int trailingW = textWidth(trailing, FONT_BODY, true);
            const int maxW = right - trailingW - 16 - textX;
            text(ellipsize(item.title, maxW, FONT_BODY), textX, y + 12, FONT_BODY, BLACK, true);
            if (!sub.empty()) text(ellipsize(sub, maxW, FONT_SMALL + 4), textX, y + 48, FONT_SMALL + 4, DARK);
            textRight(trailing, right, y + 12, FONT_BODY, INK, true);
            if (progress >= 0.0f) progressBar(textX, y + ROW_H - 14, right - textX, 5, progress);
            hRule(y + ROW_H - 1, LIGHT, MARGIN, W - MARGIN);
        }
        pager(m_libraryPage, pages);
    });
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
        if (y >= LIST_TOP && y < H - FOOTER_H) {
            int idx = m_libraryPage * perPage + (y - LIST_TOP) / ROW_H;
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
