#include "EBookmarkApp.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include "AppComm.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <ctime>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <chrono>
#endif

using namespace UI;

extern uint8_t *framebuffer;

namespace {

const Rect kSortRect(MARGIN, HEADER_H + 16, W - 2 * MARGIN, 50);
constexpr int LIST_TOP = HEADER_H + 82;
const char* const kSortLabels[] = {"Author", "Genre", "Progress"};
const LibrarySort kSortModes[] = {LibrarySort::AUTHOR, LibrarySort::GENRE, LibrarySort::COMPLETION};

constexpr float FONT_BOOK_TITLE = 34.0f;
constexpr float FONT_PAGE_BIG = 60.0f;
constexpr float FONT_PERCENT = 40.0f;
constexpr float FONT_STAT = 30.0f;

constexpr int ADJUST_COUNT = 4;
const int kAdjustDelta[ADJUST_COUNT] = {-10, -1, 1, 10};
const char* const kAdjustLabels[ADJUST_COUNT] = {"\xE2\x88\x92" "10", "\xE2\x88\x92" "1", "+1", "+10"};
constexpr int ADJUST_H = 64;
constexpr int ADJUST_GAP = 12;
constexpr int HISTORY_ROW_H = 52;
constexpr int HISTORY_MAX = 6;
constexpr uint32_t COMMIT_DELAY_MS = 1500;

const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

uint32_t nowMs() {
#ifndef NATIVE_TESTING
    return millis();
#else
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

// Timestamps before 2001 come from an unset RTC or a missing value.
bool validTs(uint32_t ts) { return ts > 1000000000u; }

std::string lower(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = (char)tolower((unsigned char)c);
    return o;
}

bool knownAuthor(const std::string& a) {
    return !a.empty() && a != "Unknown Author" && a != "Unknown";
}

float fraction(const EBookmark& b, int page) {
    if (b.totalPages <= 0) return 0.0f;
    float f = (float)page / (float)b.totalPages;
    return f > 1.0f ? 1.0f : (f < 0.0f ? 0.0f : f);
}

bool finished(const EBookmark& b) { return b.totalPages > 0 && b.currentPage >= b.totalPages; }

// Empty keys sort after everything else.
int compareKey(const std::string& a, const std::string& b) {
    if (a.empty() != b.empty()) return a.empty() ? 1 : -1;
    std::string la = lower(a), lb = lower(b);
    return la < lb ? -1 : (la > lb ? 1 : 0);
}

// Days between two local calendar dates (b - a).
int dayDiff(time_t a, time_t b) {
    struct tm ta = *localtime(&a);
    struct tm tb = *localtime(&b);
    ta.tm_hour = tb.tm_hour = 12;
    ta.tm_min = tb.tm_min = ta.tm_sec = tb.tm_sec = 0;
    ta.tm_isdst = tb.tm_isdst = -1;
    double d = difftime(mktime(&tb), mktime(&ta)) / 86400.0;
    return (int)(d < 0 ? d - 0.5 : d + 0.5);
}

std::string formatTime(const struct tm& t) {
    char buf[16];
    int hr = t.tm_hour % 12;
    if (hr == 0) hr = 12;
    snprintf(buf, sizeof(buf), "%d:%02d %s", hr, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
    return buf;
}

// "Today", "Yesterday", "3 days ago", "Oct 21", "Oct 2025"
std::string formatLastRead(uint32_t ts) {
    if (!validTs(ts)) return "\xE2\x80\x94";
    time_t raw = ts;
    struct tm t = *localtime(&raw);
    time_t now = time(nullptr);
    char buf[32];
    if (validTs((uint32_t)now)) {
        int d = dayDiff(raw, now);
        if (d == 0) return "Today";
        if (d == 1) return "Yesterday";
        if (d > 1 && d < 7) {
            snprintf(buf, sizeof(buf), "%d days ago", d);
            return buf;
        }
        struct tm n = *localtime(&now);
        if (n.tm_year != t.tm_year) {
            snprintf(buf, sizeof(buf), "%s %d", kMonths[t.tm_mon], t.tm_year + 1900);
            return buf;
        }
    }
    snprintf(buf, sizeof(buf), "%s %d", kMonths[t.tm_mon], t.tm_mday);
    return buf;
}

// "Today, 9:41 PM", "Yesterday, 8:02 AM", "Oct 21, 9:41 PM", "Oct 21, 2025"
std::string formatHistoryDate(uint32_t ts) {
    if (!validTs(ts)) return "Unknown date";
    time_t raw = ts;
    struct tm t = *localtime(&raw);
    time_t now = time(nullptr);
    char buf[48];
    if (validTs((uint32_t)now)) {
        int d = dayDiff(raw, now);
        if (d == 0) return "Today, " + formatTime(t);
        if (d == 1) return "Yesterday, " + formatTime(t);
        struct tm n = *localtime(&now);
        if (n.tm_year != t.tm_year) {
            snprintf(buf, sizeof(buf), "%s %d, %d", kMonths[t.tm_mon], t.tm_mday, t.tm_year + 1900);
            return buf;
        }
    }
    snprintf(buf, sizeof(buf), "%s %d, ", kMonths[t.tm_mon], t.tm_mday);
    return buf + formatTime(t);
}

std::string pagesLabel(int page, int total) {
    char buf[32];
    if (total > 0) snprintf(buf, sizeof(buf), "p. %d/%d", page, total);
    else snprintf(buf, sizeof(buf), "p. %d", page);
    return buf;
}

int listRowsPerPage() { return (H - FOOTER_H - LIST_TOP) / ROW_H; }

Rect adjustRect(int top, int i) {
    int bw = (W - 2 * MARGIN - (ADJUST_COUNT - 1) * ADJUST_GAP) / ADJUST_COUNT;
    return Rect(MARGIN + i * (bw + ADJUST_GAP), top, bw, ADJUST_H);
}

// Vertical positions inside the detail body, relative to its top.
struct BodyLayout {
    int pageY, barY, adjustY, statsRuleY, statsY, historyRuleY, historyLabelY, historyY;
};

BodyLayout bodyLayout(int top) {
    BodyLayout l;
    l.pageY = top;
    l.barY = top + lineHeight(FONT_PAGE_BIG) + 10;
    l.adjustY = l.barY + 14 + 30;
    l.statsRuleY = l.adjustY + ADJUST_H + 26;
    l.statsY = l.statsRuleY + 18;
    l.historyRuleY = l.statsY + lineHeight(FONT_STAT) + lineHeight(FONT_CAPTION) + 24;
    l.historyLabelY = l.historyRuleY + 18;
    l.historyY = l.historyLabelY + lineHeight(FONT_CAPTION) + 8;
    return l;
}

} // namespace

void EBookmarkApp::onCreate() {
    m_state = STATE_LIST;
    m_ebookmarkPage = 0;
    m_selectedIsbn.clear();
    m_librarySort = LibrarySort::AUTHOR;
    m_pendingPage = -1;
    EBookmarkManager::getInstance().initialize();
    m_seenRevision = AppComm::bookmarksRevision();
    updateEBookmarkItems();
}

void EBookmarkApp::onDestroy() {
    commitPendingPage();
}

void EBookmarkApp::draw() {
    if (m_state == STATE_DETAIL && selectedBook()) {
        drawEBookmarkDetail(true);
    } else {
        m_state = STATE_LIST;
        drawEBookmarkLibrary(true);
    }
}

void EBookmarkApp::update() {
    if (m_pendingPage >= 0 && nowMs() - m_pendingSinceMs >= COMMIT_DELAY_MS) {
        if (commitPendingPage() && m_state == STATE_DETAIL) drawEBookmarkDetail(false);
    }

    // Incoming phone changes are applied centrally by AppComm::poll(); just
    // redraw when the database revision moves on.
    uint32_t rev = AppComm::bookmarksRevision();
    if (rev != m_seenRevision) {
        m_seenRevision = rev;
        commitPendingPage();
        updateEBookmarkItems();
        draw();
    }
}

uint32_t EBookmarkApp::msUntilUpdate() const {
    // Phone changes arrive over BLE, which keeps the device awake anyway.
    if (m_pendingPage < 0) return UINT32_MAX;
    uint32_t waited = nowMs() - m_pendingSinceMs;
    return waited >= COMMIT_DELAY_MS ? 0 : COMMIT_DELAY_MS - waited;
}

bool EBookmarkApp::onBack() {
    commitPendingPage();
    if (m_state == STATE_DETAIL) {
        m_state = STATE_LIST;
        m_selectedIsbn.clear();
        drawEBookmarkLibrary(true);
        return true;
    }
    return false;
}

void EBookmarkApp::updateEBookmarkItems() {
    m_sortedEBookmarks = EBookmarkManager::getInstance().getBookmarks();
    const LibrarySort sort = m_librarySort;
    std::stable_sort(m_sortedEBookmarks.begin(), m_sortedEBookmarks.end(),
        [sort](const EBookmark& a, const EBookmark& b) {
            int c = 0;
            if (sort == LibrarySort::COMPLETION) {
                // In progress (furthest along first), then not started, then finished.
                auto rank = [](const EBookmark& e) {
                    if (finished(e)) return 2;
                    return e.currentPage > 0 ? 0 : 1;
                };
                int ra = rank(a), rb = rank(b);
                if (ra != rb) return ra < rb;
                float fa = fraction(a, a.currentPage), fb = fraction(b, b.currentPage);
                if (fa != fb) return fa > fb;
            } else if (sort == LibrarySort::GENRE) {
                c = compareKey(a.genre, b.genre);
                if (c == 0) c = compareKey(knownAuthor(a.author) ? a.author : "", knownAuthor(b.author) ? b.author : "");
            } else {
                c = compareKey(knownAuthor(a.author) ? a.author : "", knownAuthor(b.author) ? b.author : "");
            }
            if (c != 0) return c < 0;
            return compareKey(a.title, b.title) < 0;
        });
}

int EBookmarkApp::listPageCount() const {
    int perPage = listRowsPerPage();
    int pages = ((int)m_sortedEBookmarks.size() + perPage - 1) / perPage;
    return pages < 1 ? 1 : pages;
}

const EBookmark* EBookmarkApp::selectedBook() const {
    if (m_selectedIsbn.empty()) return nullptr;
    for (const auto& b : EBookmarkManager::getInstance().getBookmarks()) {
        if (b.isbn == m_selectedIsbn) return &b;
    }
    return nullptr;
}

int EBookmarkApp::displayedPage(const EBookmark& book) const {
    return m_pendingPage >= 0 ? m_pendingPage : book.currentPage;
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

void EBookmarkApp::drawEBookmarkLibrary(bool fullRefresh) {
    updateEBookmarkItems();
    int pages = listPageCount();
    if (m_ebookmarkPage >= pages) m_ebookmarkPage = pages - 1;
    if (m_ebookmarkPage < 0) m_ebookmarkPage = 0;

    if (fullRefresh) {
        UIFramework::performFullScreenDraw(framebuffer, [this]() {
            header("Bookmarks", true);
            drawListContent();
        });
    } else {
        UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H,
                                              [this]() { drawListContent(); });
    }
}

void EBookmarkApp::drawListContent() {
    if (m_sortedEBookmarks.empty()) {
        emptyState(H / 2, "No books yet",
                   "Scan or add books in the companion app's My Books tab and they will appear here.");
        return;
    }

    int sel = 0;
    for (int i = 0; i < 3; i++) {
        if (kSortModes[i] == m_librarySort) sel = i;
    }
    segmented(kSortRect, {kSortLabels[0], kSortLabels[1], kSortLabels[2]}, sel);

    int perPage = listRowsPerPage();
    int start = m_ebookmarkPage * perPage;
    for (int i = 0; i < perPage && start + i < (int)m_sortedEBookmarks.size(); i++) {
        const EBookmark& book = m_sortedEBookmarks[start + i];
        std::string author = knownAuthor(book.author) ? book.author : "Unknown author";
        std::string sub = author;
        if (m_librarySort == LibrarySort::GENRE) {
            sub = (book.genre.empty() ? std::string("No genre") : book.genre) + "  \xC2\xB7  " + author;
        }
        std::string trailing = finished(book) ? "Finished" : pagesLabel(book.currentPage, book.totalPages);
        float progress = book.totalPages > 0 ? fraction(book, book.currentPage) : -1.0f;
        std::string title = book.title.empty() ? "Untitled" : book.title;
        listRow(LIST_TOP + i * ROW_H, title, sub, trailing, false, progress);
    }
    pager(m_ebookmarkPage, listPageCount());
}

void EBookmarkApp::handleListTouch(int x, int y) {
    if (m_sortedEBookmarks.empty()) return;

    int seg = hitSegmented(kSortRect, 3, x, y);
    if (seg >= 0) {
        if (kSortModes[seg] != m_librarySort) {
            m_librarySort = kSortModes[seg];
            m_ebookmarkPage = 0;
            drawEBookmarkLibrary(false);
        }
        return;
    }

    int pages = listPageCount();
    int dir = hitPager(x, y, m_ebookmarkPage, pages);
    if (dir != 0) {
        m_ebookmarkPage += dir;
        drawEBookmarkLibrary(false);
        return;
    }

    int perPage = listRowsPerPage();
    if (y < LIST_TOP || y >= LIST_TOP + perPage * ROW_H || y >= H - FOOTER_H) return;
    int index = m_ebookmarkPage * perPage + (y - LIST_TOP) / ROW_H;
    if (index < 0 || index >= (int)m_sortedEBookmarks.size()) return;

    m_selectedIsbn = m_sortedEBookmarks[index].isbn;
    m_pendingPage = -1;
    if (!selectedBook()) {
        m_selectedIsbn.clear();
        return;
    }
    m_state = STATE_DETAIL;
    drawEBookmarkDetail(true);
}

// ---------------------------------------------------------------------------
// Detail
// ---------------------------------------------------------------------------

void EBookmarkApp::drawEBookmarkDetail(bool fullRefresh) {
    const EBookmark* book = selectedBook();
    if (!book) return;

    if (fullRefresh) {
        UIFramework::performFullScreenDraw(framebuffer, [this, book]() {
            header("Book details", true);
            m_detailBodyTop = drawDetailTitleBlock(*book);
            drawDetailBody(*book, m_detailBodyTop);
        });
    } else {
        int top = m_detailBodyTop - 8;
        UIFramework::performFastPartialUpdate(framebuffer, 0, top, W, H - top,
                                              [this, book]() { drawDetailBody(*book, m_detailBodyTop); });
    }
}

int EBookmarkApp::drawDetailTitleBlock(const EBookmark& book) {
    int y = HEADER_H + 22;
    std::string title = book.title.empty() ? "Untitled" : book.title;
    y = textWrapped(title, MARGIN, y, W - 2 * MARGIN, FONT_BOOK_TITLE, BLACK, 3, 4);

    std::string author = knownAuthor(book.author) ? "by " + book.author : "Unknown author";
    text(ellipsize(author, W - 2 * MARGIN, FONT_BODY), MARGIN, y + 4, FONT_BODY, DARK);
    y += 4 + lineHeight(FONT_BODY);

    std::string meta = book.genre;
    if (!book.isbn.empty()) {
        if (!meta.empty()) meta += "  \xC2\xB7  ";
        meta += "ISBN " + book.isbn;
    }
    if (!meta.empty()) {
        text(ellipsize(meta, W - 2 * MARGIN, FONT_CAPTION), MARGIN, y + 6, FONT_CAPTION, DARK);
        y += 6 + lineHeight(FONT_CAPTION);
    }
    return y + 30;
}

void EBookmarkApp::drawDetailBody(const EBookmark& book, int top) {
    const BodyLayout l = bodyLayout(top);
    const int page = displayedPage(book);
    const bool knownTotal = book.totalPages > 0;
    EBookmarkManager& mgr = EBookmarkManager::getInstance();

    // Progress: big page number, "of N pages", percent and bar.
    std::string pageStr = std::to_string(page);
    text(pageStr, MARGIN, l.pageY, FONT_PAGE_BIG, BLACK, true);
    int pw = textWidth(pageStr, FONT_PAGE_BIG, true);
    int smallY = l.pageY + lineHeight(FONT_PAGE_BIG) - lineHeight(FONT_BODY) - 6;
    std::string ofStr = knownTotal ? "of " + std::to_string(book.totalPages) + " pages" : "pages read";
    text(ofStr, MARGIN + pw + 12, smallY, FONT_BODY, DARK);
    if (knownTotal) {
        int pct = (int)(fraction(book, page) * 100.0f + 0.5f);
        if (pct == 100 && page < book.totalPages) pct = 99;
        std::string pctStr = std::to_string(pct) + "%";
        int py = l.pageY + lineHeight(FONT_PAGE_BIG) - lineHeight(FONT_PERCENT) - 2;
        textRight(pctStr, W - MARGIN, py, FONT_PERCENT, BLACK, true);
        progressBar(MARGIN, l.barY, W - 2 * MARGIN, 14, fraction(book, page));
    } else {
        text("Total pages unknown \xE2\x80\x94 set it in the companion app.", MARGIN, l.barY - 4, FONT_CAPTION, DARK);
    }

    // Quick adjust.
    int maxPage = knownTotal ? book.totalPages : INT_MAX;
    for (int i = 0; i < ADJUST_COUNT; i++) {
        int d = kAdjustDelta[i];
        bool enabled = d < 0 ? page > 0 : page < maxPage;
        button(adjustRect(l.adjustY, i), kAdjustLabels[i], ButtonStyle::SECONDARY, FONT_BODY, enabled);
    }

    // Stats.
    hRule(l.statsRuleY);
    char avgBuf[16];
    float avg = mgr.getAvgPagesPerSitting(book);
    if (avg <= 0.0f) snprintf(avgBuf, sizeof(avgBuf), "\xE2\x80\x94");
    else if (avg >= 10.0f) snprintf(avgBuf, sizeof(avgBuf), "%.0f", avg);
    else snprintf(avgBuf, sizeof(avgBuf), "%.1f", avg);
    int days = mgr.getDaysReading(book);
    uint32_t lastTs = book.history.empty() ? 0 : book.history.back().timestamp;
    const std::string values[3] = {days > 0 ? std::to_string(days) : "\xE2\x80\x94", avgBuf, formatLastRead(lastTs)};
    const char* const labels[3] = {days == 1 ? "Day reading" : "Days reading", "Pages / sitting", "Last read"};
    int colW = (W - 2 * MARGIN) / 3;
    for (int i = 0; i < 3; i++) {
        int x = MARGIN + i * colW;
        text(ellipsize(values[i], colW - 12, FONT_STAT, true), x, l.statsY, FONT_STAT, BLACK, true);
        text(ellipsize(labels[i], colW - 8, FONT_CAPTION), x,
             l.statsY + lineHeight(FONT_STAT) + 4, FONT_CAPTION, DARK);
    }

    // Recent history, newest first.
    hRule(l.historyRuleY);
    sectionLabel(l.historyLabelY, "Recent activity");
    const auto& hist = book.history;
    if (hist.empty()) {
        text("No reading history yet.", MARGIN, l.historyY + 12, FONT_SMALL, DARK);
        return;
    }
    int rows = (H - 16 - l.historyY) / HISTORY_ROW_H;
    if (rows > HISTORY_MAX) rows = HISTORY_MAX;
    int ty = (HISTORY_ROW_H - lineHeight(FONT_SMALL)) / 2;
    for (int r = 0; r < rows && r < (int)hist.size(); r++) {
        int idx = (int)hist.size() - 1 - r;
        int y = l.historyY + r * HISTORY_ROW_H;
        const HistoryEntry& e = hist[idx];
        text(formatHistoryDate(e.timestamp), MARGIN, y + ty, FONT_SMALL, INK);
        textRight("p. " + std::to_string(e.page), W - MARGIN, y + ty, FONT_SMALL, BLACK, true);
        int delta = e.page - (idx > 0 ? hist[idx - 1].page : 0);
        if (delta != 0) {
            std::string ds = (delta > 0 ? "+" : "\xE2\x88\x92") + std::to_string(delta > 0 ? delta : -delta);
            textRight(ds, W - MARGIN - 120, y + ty, FONT_SMALL, DARK);
        }
        if (r + 1 < rows && idx > 0) hRule(y + HISTORY_ROW_H - 1, PALE);
    }
}

void EBookmarkApp::handleDetailTouch(int x, int y) {
    const BodyLayout l = bodyLayout(m_detailBodyTop);
    for (int i = 0; i < ADJUST_COUNT; i++) {
        Rect r = adjustRect(l.adjustY, i);
        // Slightly generous hit area around each button.
        Rect hit(r.x - ADJUST_GAP / 2, r.y - 12, r.w + ADJUST_GAP, r.h + 24);
        if (hit.contains(x, y)) {
            adjustPage(kAdjustDelta[i]);
            return;
        }
    }
}

void EBookmarkApp::adjustPage(int delta) {
    const EBookmark* book = selectedBook();
    if (!book) return;
    int cur = displayedPage(*book);
    int next = cur + delta;
    if (next < 0) next = 0;
    if (book->totalPages > 0 && next > book->totalPages) next = book->totalPages;
    if (next == cur) return;

    m_pendingPage = next == book->currentPage ? -1 : next;
    m_pendingSinceMs = nowMs();
    drawEBookmarkDetail(false);
}

bool EBookmarkApp::commitPendingPage() {
    if (m_pendingPage < 0) return false;
    int page = m_pendingPage;
    m_pendingPage = -1;
    const EBookmark* book = selectedBook();
    if (!book || book->currentPage == page) return false;
    EBookmarkManager::getInstance().updatePage(book->isbn, page);
    updateEBookmarkItems();
    return true;
}

void EBookmarkApp::handleTouch(int x, int y) {
    if (m_state == STATE_LIST) {
        handleListTouch(x, y);
    } else {
        handleDetailTouch(x, y);
    }
}
