#pragma once

// On-device copies of the data the companion app syncs (see
// manual/11_todo_and_calendar.md for the file formats):
//   /data/todos.jsonl       todo items from NootTask and Google Tasks
//   /data/calendar.jsonl    Google Calendar events (read-only)
//   /data/todo_edits.jsonl  note edits made on the device, waiting for the
//                           phone to push them to NootTask
//
// Files are JSON Lines. They are read into one PSRAM buffer; only small
// per-line indexes live in internal RAM and each line is parsed on demand.

#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace SyncStore {

// Bumped whenever the phone delivers a new file or edits are acknowledged,
// so open apps know to reload.
uint32_t todosRevision();
uint32_t calendarRevision();
void bumpTodos();
void bumpCalendar();

// ---- Dates (local time, using the timezone sent by the phone) ---------------
// Day keys are yyyymmdd integers.
int dayKey(time_t t);
time_t dayStart(int key);
int addDays(int key, int days);
int weekday(int key); // 0 = Sunday
int todayKey();
bool clockValid();
// "Thu 5 Nov" or "Thursday 5 November" (longForm); "Today"/"Tomorrow" when
// relative and applicable.
std::string dayLabel(int key, bool longForm, bool relative);
// "9:30" / "14:05" (24 hour).
std::string timeLabel(time_t t);
// "5 Nov" from "2026-11-05"; "" when unparsable.
std::string dueLabel(const std::string& isoDate);
// Parses "YYYY-MM-DD" into a day key; 0 when unparsable.
int isoDayKey(const std::string& isoDate);
// "5 Nov 14:05" for a unix time, "never" for 0.
std::string stampLabel(uint32_t unix);

// ---- Todos ------------------------------------------------------------------

struct TodoDetail {
    std::string src;   // "nt" (NootTask, notes editable) or "gt" (Google Tasks)
    std::string id;
    std::string list;
    std::string title;
    std::string due;   // YYYY-MM-DD or ""
    std::string pri;
    std::string notes; // with any pending local edit applied
    bool done = false;
    bool editedLocally = false; // notes changed on the device, not yet pushed
    bool readOnly = false;      // "ro": encrypted or trimmed notes on the server
    bool editable() const { return src == "nt" && !readOnly; }
};

class TodoStore {
public:
    TodoStore() = default;
    ~TodoStore();
    TodoStore(const TodoStore&) = delete;
    TodoStore& operator=(const TodoStore&) = delete;

    // Loads the synced file and pending edits. False when nothing has been
    // synced yet (size() is then 0).
    bool load();
    size_t size() const { return m_items.size(); }
    bool done(size_t i) const { return m_items[i].done; }
    bool google(size_t i) const { return m_items[i].google; }
    // Cheap fields for list rows.
    std::string title(size_t i) const { return field(i, "title"); }
    std::string list(size_t i) const { return field(i, "list"); }
    std::string due(size_t i) const { return field(i, "due"); }
    bool hasLocalEdit(size_t i) const;
    TodoDetail detail(size_t i) const;
    uint32_t syncedAt() const { return m_syncedAt; }
    size_t pendingEdits() const { return m_edits.size(); }
    // Records a note edit: shown immediately on the device, pushed to
    // NootTask by the phone on the next sync.
    bool saveNotes(size_t i, const std::string& notes);

private:
    struct Item {
        uint32_t off;
        uint32_t len;
        bool done;
        bool google;
    };
    char* m_buf = nullptr;
    size_t m_len = 0;
    std::vector<Item> m_items;
    std::map<std::string, std::string> m_edits; // "src:id" -> notes
    uint32_t m_syncedAt = 0;
    void release();
    std::string field(size_t i, const char* key) const;
};

// Edits file, shared with AppComm.
bool appendTodoEdit(const std::string& src, const std::string& id, const std::string& notes);
// Drops edits with seq <= upTo; returns how many remain.
int dropTodoEditsUpTo(uint32_t upTo);
std::map<std::string, std::string> loadTodoEdits();
// SD-root path of the edits file the phone may read.
const char* todoEditsPath();
// Reads a whole (small) file by SD-root path; false when missing.
bool readSmallFile(const char* sdPath, std::string& out);

// ---- Calendar -----------------------------------------------------------

struct CalDetail {
    std::string title;
    std::string cal;
    std::string loc;
    std::string desc;
    bool allDay = false;
    time_t start = 0;
    time_t end = 0;
    int day = 0;     // first day (all-day events)
    int endDay = 0;  // last day, inclusive (all-day events)
};

class CalendarStore {
public:
    CalendarStore() = default;
    ~CalendarStore();
    CalendarStore(const CalendarStore&) = delete;
    CalendarStore& operator=(const CalendarStore&) = delete;

    bool load();
    bool empty() const { return m_events.empty(); }
    size_t size() const { return m_events.size(); }
    uint32_t syncedAt() const { return m_syncedAt; }
    // Events touching the given day: all-day first, then by start time.
    std::vector<size_t> eventsOn(int dayKey) const;
    bool hasEventsOn(int dayKey) const;
    CalDetail detail(size_t i) const;
    bool allDay(size_t i) const { return m_events[i].allDay; }
    // First day on/after `fromDay` (within `maxDays`) with events; 0 if none.
    int nextDayWithEvents(int fromDay, int maxDays) const;

private:
    struct Ev {
        uint32_t off;
        uint32_t len;
        int32_t firstDay;
        int32_t lastDay;
        int64_t sortKey; // start unix (all-day: local midnight)
        bool allDay;
    };
    char* m_buf = nullptr;
    size_t m_len = 0;
    std::vector<Ev> m_events;
    uint32_t m_syncedAt = 0;
    void release();
};

} // namespace SyncStore
