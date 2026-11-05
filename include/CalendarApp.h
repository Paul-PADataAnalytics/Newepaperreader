#pragma once

#include "Application.h"
#include "sync/SyncStore.h"
#include <string>
#include <vector>

// Read-only Google Calendar, synced by the companion app. Sleeping while
// the calendar is open shows the coming agenda on the sleep screen, redrawn
// shortly after midnight each day.
class CalendarApp : public Application {
public:
    void onCreate() override;
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
    void update() override;
    uint32_t msUntilUpdate() const override;
    bool onBack() override;

    bool drawSleepScreen() override;
    uint32_t sleepScreenRefreshSec() const override;

private:
    enum View { VIEW_AGENDA = 0, VIEW_DAY, VIEW_MONTH };
    struct Row {
        bool dayHeader;
        int day;
        size_t ev;
        int h;
    };

    SyncStore::CalendarStore m_store;
    uint32_t m_storeRev = 0;
    View m_view = VIEW_AGENDA;
    bool m_detail = false;
    size_t m_event = 0;
    int m_detailDay = 0;
    int m_day = 0;   // day shown in the day view
    int m_month = 0; // yyyymm shown in the month view
    int m_page = 0;
    std::vector<Row> m_rows;
    std::vector<std::pair<size_t, size_t>> m_pages;
    uint32_t m_lastRevCheck = 0;
    int m_lastToday = 0;

    void reload();
    void buildRows();
    void drawBody();
    void drawRows(int top);
    void drawMonth();
    void drawDetail();
    void redrawBody();
    int monthCellDay(int x, int y) const;
};
