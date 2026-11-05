#include "CalendarApp.h"
#include "AppComm.h"
#include "DisplayHAL.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include <cstdio>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

extern uint8_t* framebuffer;

namespace {
using namespace UI;

const Rect kViews(MARGIN, HEADER_H + 12, W - 2 * MARGIN, 52);
constexpr int BODY_TOP = HEADER_H + 76;
constexpr int LIST_BOTTOM = H - FOOTER_H;
constexpr int DAY_H = 50;
constexpr int EVENT_H = 84;
constexpr int EMPTY_H = 60;
constexpr int TIME_W = 100;
constexpr size_t NO_EVENT = (size_t)-1;
constexpr int AGENDA_DAYS = 120;
const char* kToday = "Today";

// Month grid
constexpr int MONTH_TITLE_Y = BODY_TOP + 6;
constexpr int WEEKDAY_Y = BODY_TOP + 70;
constexpr int GRID_TOP = BODY_TOP + 104;
constexpr int CELL_W = (W - 2 * MARGIN) / 7;
constexpr int CELL_H = (H - 16 - GRID_TOP) / 6;

std::string headerLabel(int day) {
    std::string rel = SyncStore::dayLabel(day, false, true);
    std::string full = SyncStore::dayLabel(day, true, false);
    if (rel == "Today" || rel == "Tomorrow" || rel == "Yesterday") return rel + "  \xC2\xB7  " + full;
    return full;
}

int firstOfMonth(int yyyymm) { return yyyymm * 100 + 1; }

std::string monthTitle(int yyyymm) {
    static const char* kMonths[] = {"January", "February", "March", "April", "May", "June", "July",
                                    "August", "September", "October", "November", "December"};
    int m = yyyymm % 100;
    if (m < 1 || m > 12) return "";
    return std::string(kMonths[m - 1]) + " " + std::to_string(yyyymm / 100);
}

int addMonths(int yyyymm, int n) {
    int y = yyyymm / 100, m = yyyymm % 100 - 1 + n;
    y += m >= 0 ? m / 12 : (m - 11) / 12;
    m = ((m % 12) + 12) % 12;
    return y * 100 + m + 1;
}

// Monday-first column of a day.
int mondayColumn(int day) { return (SyncStore::weekday(day) + 6) % 7; }

// Time column text for an event on `day`.
void timeColumn(const SyncStore::CalDetail& d, int day, std::string& top, std::string& bottom) {
    if (d.allDay) {
        top = "All day";
        bottom = "";
        return;
    }
    top = SyncStore::dayKey(d.start) == day ? SyncStore::timeLabel(d.start) : "Cont.";
    bottom = d.end > d.start && SyncStore::dayKey(d.end - 1) == day ? SyncStore::timeLabel(d.end) : "";
}

void eventRow(int y, int h, const SyncStore::CalDetail& d, int day, float titleSize) {
    std::string top, bottom;
    timeColumn(d, day, top, bottom);
    text(top, MARGIN, y + 12, FONT_SMALL, BLACK, true);
    if (!bottom.empty()) text(bottom, MARGIN, y + 44, FONT_SMALL, DARK);
    const int tx = MARGIN + TIME_W;
    const int tw = W - MARGIN - tx;
    std::string sub = !d.loc.empty() ? d.loc : d.cal;
    text(ellipsize(d.title, tw, titleSize, true), tx, sub.empty() ? y + (h - lineHeight(titleSize)) / 2 : y + 10,
         titleSize, BLACK, true);
    if (!sub.empty()) text(ellipsize(sub, tw, FONT_SMALL), tx, y + 46, FONT_SMALL, DARK);
}

std::string whenText(const SyncStore::CalDetail& d) {
    if (d.allDay) {
        std::string s = SyncStore::dayLabel(d.day, false, false);
        if (d.endDay > d.day) s += " \xE2\x80\x93 " + SyncStore::dayLabel(d.endDay, false, false);
        return s + "  \xC2\xB7  All day";
    }
    int sd = SyncStore::dayKey(d.start);
    int ed = d.end > d.start ? SyncStore::dayKey(d.end - 1) : sd;
    std::string s = SyncStore::dayLabel(sd, false, false) + "  \xC2\xB7  " + SyncStore::timeLabel(d.start);
    if (d.end > d.start) {
        s += " \xE2\x80\x93 ";
        if (ed != sd) s += SyncStore::dayLabel(ed, false, false) + " ";
        s += SyncStore::timeLabel(d.end);
    }
    return s;
}
} // namespace

void CalendarApp::onCreate() {
    m_view = VIEW_AGENDA;
    m_detail = false;
    m_lastToday = SyncStore::todayKey();
    m_day = m_lastToday;
    m_month = m_day / 100;
    reload();
}

void CalendarApp::reload() {
    m_store.load();
    m_storeRev = SyncStore::calendarRevision();
    buildRows();
}

void CalendarApp::buildRows() {
    m_rows.clear();
    m_pages.clear();
    if (m_view == VIEW_AGENDA) {
        int start = SyncStore::clockValid() ? SyncStore::todayKey() : 0;
        if (!start) start = m_store.nextDayWithEvents(19700101, 400000);
        if (start) {
            int day = start;
            for (int n = 0; n < AGENDA_DAYS && day; n++) {
                std::vector<size_t> evs = m_store.eventsOn(day);
                if (!evs.empty() || day == start) {
                    m_rows.push_back({true, day, NO_EVENT, DAY_H});
                    if (evs.empty()) m_rows.push_back({false, day, NO_EVENT, EMPTY_H});
                    for (size_t e : evs) m_rows.push_back({false, day, e, EVENT_H});
                }
                int next = m_store.nextDayWithEvents(SyncStore::addDays(day, 1), AGENDA_DAYS);
                if (!next) break;
                day = next;
                if (day > SyncStore::addDays(start, AGENDA_DAYS)) break;
            }
        }
    } else if (m_view == VIEW_DAY) {
        std::vector<size_t> evs = m_store.eventsOn(m_day);
        if (evs.empty()) m_rows.push_back({false, m_day, NO_EVENT, EMPTY_H});
        for (size_t e : evs) m_rows.push_back({false, m_day, e, EVENT_H});
    }
    const int top = m_view == VIEW_DAY ? BODY_TOP + 56 : BODY_TOP + 4;
    size_t first = 0;
    int used = 0;
    for (size_t r = 0; r < m_rows.size(); r++) {
        int need = m_rows[r].dayHeader ? m_rows[r].h + EVENT_H : m_rows[r].h;
        if (used > 0 && used + need > LIST_BOTTOM - top) {
            m_pages.push_back({first, r});
            first = r;
            used = 0;
        }
        used += m_rows[r].h;
    }
    if (first < m_rows.size()) m_pages.push_back({first, m_rows.size()});
    if (m_pages.empty()) m_pages.push_back({0, 0});
    if (m_page >= (int)m_pages.size()) m_page = (int)m_pages.size() - 1;
    if (m_page < 0) m_page = 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void CalendarApp::drawRows(int top) {
    int y = top;
    const auto& pg = m_pages[m_page];
    for (size_t r = pg.first; r < pg.second; r++) {
        const Row& row = m_rows[r];
        if (row.dayHeader) {
            sectionLabel(y + 18, headerLabel(row.day));
        } else if (row.ev == NO_EVENT) {
            text("Nothing scheduled", MARGIN + TIME_W, y + 14, FONT_SMALL + 2, DARK);
            hRule(y + row.h - 1, LIGHT, MARGIN, W - MARGIN);
        } else {
            eventRow(y, row.h, m_store.detail(row.ev), row.day, FONT_BODY);
            hRule(y + row.h - 1, LIGHT, MARGIN, W - MARGIN);
        }
        y += row.h;
    }
}

void CalendarApp::drawMonth() {
    chevronLeft(MARGIN + 14, MONTH_TITLE_Y + 28, 28);
    chevronRight(W - MARGIN - 14, MONTH_TITLE_Y + 28, 28);
    textCentered(monthTitle(m_month), W / 2, MONTH_TITLE_Y + 10, FONT_BODY + 4, BLACK, true);
    static const char* kWd[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (int c = 0; c < 7; c++) {
        textCentered(kWd[c], MARGIN + c * CELL_W + CELL_W / 2, WEEKDAY_Y, FONT_CAPTION, DARK, true);
    }
    const int first = firstOfMonth(m_month);
    const int offset = mondayColumn(first);
    const int today = SyncStore::clockValid() ? SyncStore::todayKey() : 0;
    for (int cell = 0; cell < 42; cell++) {
        int day = SyncStore::addDays(first, cell - offset);
        int col = cell % 7, row = cell / 7;
        int x = MARGIN + col * CELL_W, y = GRID_TOP + row * CELL_H;
        bool inMonth = day / 100 == m_month;
        if (col > 0) fillRect(x, y + 6, 1, CELL_H - 12, LIGHT);
        hRule(y, LIGHT, x, x + CELL_W);
        std::string num = std::to_string(day % 100);
        if (day == today) {
            fillCircle(x + CELL_W / 2, y + 26, 20, BLACK);
            textCentered(num, x + CELL_W / 2, y + 26 - lineHeight(FONT_SMALL + 2) / 2, FONT_SMALL + 2, WHITE, true);
        } else {
            textCentered(num, x + CELL_W / 2, y + 26 - lineHeight(FONT_SMALL + 2) / 2, FONT_SMALL + 2,
                         inMonth ? BLACK : MID, inMonth);
        }
        size_t count = m_store.eventsOn(day).size();
        if (count > 0) {
            int dots = count > 3 ? 3 : (int)count;
            int dx = x + CELL_W / 2 - (dots * 14 - 4) / 2;
            for (int i = 0; i < dots; i++) fillCircle(dx + i * 14 + 5, y + 62, 5, inMonth ? BLACK : MID);
            if (count > 3) textCentered("+" + std::to_string(count - 3), x + CELL_W / 2, y + 72, FONT_CAPTION - 4, DARK);
        }
    }
}

void CalendarApp::drawDetail() {
    SyncStore::CalDetail d = m_store.detail(m_event);
    header("Event", true);
    int y = textWrapped(d.title, MARGIN, HEADER_H + 24, W - 2 * MARGIN, FONT_TITLE, BLACK, 3, 4);
    text(ellipsize(whenText(d), W - 2 * MARGIN, FONT_SMALL + 2), MARGIN, y + 10, FONT_SMALL + 2, INK, true);
    y += 52;
    if (!d.loc.empty()) y = textWrapped(d.loc, MARGIN, y, W - 2 * MARGIN, FONT_SMALL + 2, INK, 2, 4) + 4;
    if (!d.cal.empty()) {
        text(ellipsize("Calendar: " + d.cal, W - 2 * MARGIN, FONT_SMALL), MARGIN, y, FONT_SMALL, DARK);
        y += 40;
    }
    if (!d.desc.empty()) {
        hRule(y + 4);
        y += 22;
        int lh = lineHeight(FONT_SMALL + 2) + 6;
        int maxLines = (H - 24 - y) / lh;
        if (maxLines > 0) textWrapped(d.desc, MARGIN, y, W - 2 * MARGIN, FONT_SMALL + 2, INK, maxLines, 6);
    }
}

void CalendarApp::drawBody() {
    segmented(kViews, {"Agenda", "Day", "Month"}, m_view);
    if (m_store.empty() && m_view != VIEW_MONTH) {
        emptyState(H / 2 + 40, "No calendar yet",
                   "Turn on Bluetooth in Settings and connect the NER Companion app. "
                   "Your Google Calendar syncs each time it connects.");
        return;
    }
    switch (m_view) {
    case VIEW_AGENDA:
        drawRows(BODY_TOP + 4);
        pager(m_page, (int)m_pages.size());
        break;
    case VIEW_DAY: {
        int titleW = W - 2 * MARGIN;
        if (m_pages.size() > 1) {
            std::string more = "More " + std::to_string(m_page + 1) + "/" + std::to_string(m_pages.size()) + " >";
            textRight(more, W - MARGIN, BODY_TOP + 14, FONT_CAPTION, INK, true);
            titleW -= textWidth(more, FONT_CAPTION, true) + 16;
        }
        text(ellipsize(headerLabel(m_day), titleW, FONT_BODY, true), MARGIN, BODY_TOP + 10, FONT_BODY, BLACK, true);
        drawRows(BODY_TOP + 56);
        footerActions({"< Previous day", "Next day >"});
        break;
    }
    case VIEW_MONTH:
        drawMonth();
        break;
    }
}

void CalendarApp::draw() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        if (m_detail) {
            drawDetail();
            return;
        }
        header("Calendar", true, kToday);
        drawBody();
    });
}

void CalendarApp::redrawBody() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() { drawBody(); });
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool CalendarApp::onBack() {
    if (m_detail) {
        m_detail = false;
        draw();
        return true;
    }
    if (m_view == VIEW_DAY) {
        m_view = VIEW_AGENDA;
        m_page = 0;
        buildRows();
        draw();
        return true;
    }
    return false;
}

int CalendarApp::monthCellDay(int x, int y) const {
    if (y < GRID_TOP || y >= GRID_TOP + 6 * CELL_H || x < MARGIN || x >= MARGIN + 7 * CELL_W) return 0;
    int cell = ((y - GRID_TOP) / CELL_H) * 7 + (x - MARGIN) / CELL_W;
    int first = firstOfMonth(m_month);
    return SyncStore::addDays(first, cell - mondayColumn(first));
}

void CalendarApp::handleTouch(int x, int y) {
    if (m_detail) return;
    if (hitHeaderAction(x, y, kToday)) {
        m_day = SyncStore::todayKey();
        m_month = m_day / 100;
        m_page = 0;
        buildRows();
        redrawBody();
        return;
    }
    int v = hitSegmented(kViews, 3, x, y);
    if (v >= 0) {
        if (v != m_view) {
            m_view = (View)v;
            m_page = 0;
            buildRows();
            redrawBody();
        }
        return;
    }
    if (m_view == VIEW_MONTH) {
        if (y >= MONTH_TITLE_Y && y < WEEKDAY_Y) {
            if (x < W / 3) m_month = addMonths(m_month, -1);
            else if (x > W * 2 / 3) m_month = addMonths(m_month, 1);
            else return;
            redrawBody();
            return;
        }
        int day = monthCellDay(x, y);
        if (day) {
            m_day = day;
            m_view = VIEW_DAY;
            m_page = 0;
            buildRows();
            redrawBody();
        }
        return;
    }
    if (m_view == VIEW_DAY) {
        if (m_pages.size() > 1 && y >= BODY_TOP && y < BODY_TOP + 56 && x > W * 2 / 3) {
            m_page = (m_page + 1) % (int)m_pages.size();
            redrawBody();
            return;
        }
        int a = hitFooterAction(x, y, 2);
        if (a >= 0) {
            m_day = SyncStore::addDays(m_day, a == 0 ? -1 : 1);
            m_month = m_day / 100;
            m_page = 0;
            buildRows();
            redrawBody();
            return;
        }
    } else {
        int step = hitPager(x, y, m_page, (int)m_pages.size());
        if (step) {
            m_page += step;
            redrawBody();
            return;
        }
    }
    const int top = m_view == VIEW_DAY ? BODY_TOP + 56 : BODY_TOP + 4;
    if (y < top || y >= LIST_BOTTOM || m_rows.empty()) return;
    int ry = top;
    const auto& pg = m_pages[m_page];
    for (size_t r = pg.first; r < pg.second; r++) {
        if (y >= ry && y < ry + m_rows[r].h) {
            const Row& row = m_rows[r];
            if (row.dayHeader && m_view == VIEW_AGENDA) {
                m_day = row.day;
                m_month = m_day / 100;
                m_view = VIEW_DAY;
                m_page = 0;
                buildRows();
                redrawBody();
            } else if (!row.dayHeader && row.ev != NO_EVENT) {
                m_event = row.ev;
                m_detailDay = row.day;
                m_detail = true;
                draw();
            }
            return;
        }
        ry += m_rows[r].h;
    }
}

void CalendarApp::update() {
    uint32_t now = millis();
    if (now - m_lastRevCheck < 1000) return;
    m_lastRevCheck = now;
    int today = SyncStore::todayKey();
    bool dayRolled = today != m_lastToday;
    if (SyncStore::calendarRevision() == m_storeRev && !dayRolled) return;
    m_lastToday = today;
    reload();
    if (m_detail && m_event >= m_store.size()) m_detail = false;
    draw();
}

uint32_t CalendarApp::msUntilUpdate() const { return AppComm::isInitialized() ? 1000 : 60000; }

// ---------------------------------------------------------------------------
// Sleep screen
// ---------------------------------------------------------------------------

bool CalendarApp::drawSleepScreen() {
    const bool clock = SyncStore::clockValid();
    const int today = clock ? SyncStore::todayKey() : m_store.nextDayWithEvents(19700101, 400000);
    const int battery = DisplayHAL::getBatteryPercent();
    UIFramework::performFullScreenDraw(framebuffer, [&]() {
        int y = 26;
        if (today) {
            std::string full = SyncStore::dayLabel(today, true, false); // "Thursday 5 November"
            size_t sp = full.find(' ');
            text(full.substr(0, sp), MARGIN, y, 56.0f, BLACK, true);
            text(full.substr(sp + 1) + " " + std::to_string(today / 10000), MARGIN, y + 70, FONT_BODY + 2, DARK);
        } else {
            text("Calendar", MARGIN, y, 56.0f, BLACK, true);
        }
        y = 148;
        hRule(y, BLACK, MARGIN, W - MARGIN, 2);
        y += 14;

        const int bottom = H - 120;
        bool truncated = false;
        if (today) {
            int day = today;
            for (int n = 0; n < 60 && day && !truncated; n++) {
                std::vector<size_t> evs = m_store.eventsOn(day);
                if (!evs.empty() || day == today) {
                    if (y + DAY_H + 70 > bottom) {
                        truncated = true;
                        break;
                    }
                    sectionLabel(y + 16, headerLabel(day));
                    y += DAY_H;
                    if (evs.empty()) {
                        text("Nothing scheduled", MARGIN + TIME_W, y + 6, FONT_SMALL + 2, DARK);
                        y += 52;
                    }
                    for (size_t e : evs) {
                        if (y + 76 > bottom) {
                            truncated = true;
                            break;
                        }
                        eventRow(y, 76, m_store.detail(e), day, FONT_BODY);
                        y += 76;
                    }
                }
                day = m_store.nextDayWithEvents(SyncStore::addDays(day, 1), 60);
            }
        }
        if (m_store.empty()) {
            textWrapped("No events synced yet. Connect the NER Companion app to sync your Google Calendar.",
                        MARGIN, y + 20, W - 2 * MARGIN, FONT_SMALL + 2, DARK, 4, 6);
        } else if (truncated) {
            text("More events in the Calendar app", MARGIN + TIME_W, bottom - 4, FONT_CAPTION, DARK);
        }

        hRule(H - 96, LIGHT, MARGIN * 3, W - MARGIN * 3, 2);
        textCentered("Sleeping  \xC2\xB7  press the side button to wake", W / 2, H - 82, FONT_CAPTION, DARK);
        textCentered(std::to_string(battery) + "%  \xC2\xB7  calendar synced " + SyncStore::stampLabel(m_store.syncedAt()),
                     W / 2, H - 48, FONT_CAPTION, DARK);
    });
    return true;
}

uint32_t CalendarApp::sleepScreenRefreshSec() const {
    if (!SyncStore::clockValid()) return 0;
    time_t now = time(nullptr);
    time_t midnight = SyncStore::dayStart(SyncStore::addDays(SyncStore::todayKey(), 1));
    long secs = (long)(midnight - now) + 90;
    if (secs < 60) secs = 60;
    return (uint32_t)secs;
}
