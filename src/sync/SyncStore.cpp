#include "SyncStore.h"
#include "reader/AppStorage.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#endif

namespace SyncStore {

namespace {

const char* kTodosPath = "/data/todos.jsonl";
const char* kCalendarPath = "/data/calendar.jsonl";
const char* kEditsPath = "/data/todo_edits.jsonl";

uint32_t s_todosRev = 1;
uint32_t s_calendarRev = 1;

std::string fsPath(const char* sdPath) {
    return AppStorage::toVfsPath(AppStorage::toRuntimePath(sdPath));
}

char* allocBig(size_t n) {
#ifndef NATIVE_TESTING
    char* p = (char*)ps_malloc(n);
    if (p) return p;
#endif
    return (char*)malloc(n);
}

// Reads a whole file into a fresh (PSRAM) buffer, NUL-terminated.
bool loadFile(const char* sdPath, char*& buf, size_t& len) {
    buf = nullptr;
    len = 0;
    FILE* f = fopen(fsPath(sdPath).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return false;
    }
    buf = allocBig((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    len = fread(buf, 1, (size_t)size, f);
    buf[len] = 0;
    fclose(f);
    return len > 0;
}

// Calls fn(offset, length) for every non-empty line.
template <typename Fn>
void forEachLine(const char* buf, size_t len, Fn fn) {
    size_t start = 0;
    while (start < len) {
        const char* nl = (const char*)memchr(buf + start, '\n', len - start);
        size_t end = nl ? (size_t)(nl - buf) : len;
        size_t lineLen = end - start;
        if (lineLen > 0 && buf[end - 1] == '\r') lineLen--;
        if (lineLen > 0) fn(start, lineLen);
        start = end + 1;
    }
}

bool parseLine(const char* buf, uint32_t off, uint32_t len, JsonDocument& doc) {
    // const input: ArduinoJson copies strings instead of editing the buffer.
    return !deserializeJson(doc, (const char*)(buf + off), (size_t)len);
}

std::string editKey(const std::string& src, const std::string& id) { return src + ":" + id; }

const char* kMonths[] = {"January", "February", "March", "April", "May", "June", "July",
                         "August", "September", "October", "November", "December"};
const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

} // namespace

uint32_t todosRevision() { return s_todosRev; }
uint32_t calendarRevision() { return s_calendarRev; }
void bumpTodos() { s_todosRev++; }
void bumpCalendar() { s_calendarRev++; }

// ---------------------------------------------------------------------------
// Dates
// ---------------------------------------------------------------------------

int dayKey(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    return (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

static struct tm tmForKey(int key, int hour) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = key / 10000 - 1900;
    tmv.tm_mon = (key / 100) % 100 - 1;
    tmv.tm_mday = key % 100;
    tmv.tm_hour = hour;
    tmv.tm_isdst = -1;
    return tmv;
}

time_t dayStart(int key) {
    struct tm tmv = tmForKey(key, 0);
    return mktime(&tmv);
}

int addDays(int key, int days) {
    struct tm tmv = tmForKey(key, 12);
    tmv.tm_mday += days;
    time_t t = mktime(&tmv);
    return dayKey(t);
}

int weekday(int key) {
    struct tm tmv = tmForKey(key, 12);
    mktime(&tmv);
    return tmv.tm_wday;
}

int todayKey() { return dayKey(time(nullptr)); }

bool clockValid() { return time(nullptr) > 1600000000; }

std::string dayLabel(int key, bool longForm, bool relative) {
    if (relative && clockValid()) {
        int today = todayKey();
        if (key == today) return "Today";
        if (key == addDays(today, 1)) return "Tomorrow";
        if (key == addDays(today, -1)) return "Yesterday";
    }
    int wd = weekday(key);
    int mon = (key / 100) % 100 - 1;
    if (mon < 0 || mon > 11) return "";
    char buf[48];
    if (longForm) {
        snprintf(buf, sizeof(buf), "%s %d %s", kDays[wd], key % 100, kMonths[mon]);
    } else {
        snprintf(buf, sizeof(buf), "%.3s %d %.3s", kDays[wd], key % 100, kMonths[mon]);
    }
    return buf;
}

std::string timeLabel(time_t t) {
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", tmv.tm_hour, tmv.tm_min);
    return buf;
}

int isoDayKey(const std::string& iso) {
    int y = 0, m = 0, d = 0;
    if (iso.size() < 10 || sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return 0;
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    return y * 10000 + m * 100 + d;
}

std::string dueLabel(const std::string& iso) {
    int key = isoDayKey(iso);
    if (!key) return "";
    if (clockValid()) {
        int today = todayKey();
        if (key == today) return "Today";
        if (key == addDays(today, 1)) return "Tomorrow";
        if (key < today) return "Overdue " + std::to_string(key % 100) + " " + std::string(kMonths[(key / 100) % 100 - 1]).substr(0, 3);
    }
    return std::to_string(key % 100) + " " + std::string(kMonths[(key / 100) % 100 - 1]).substr(0, 3);
}

std::string stampLabel(uint32_t epoch) {
    if (!epoch) return "never";
    time_t t = (time_t)epoch;
    int key = dayKey(t);
    return std::to_string(key % 100) + " " + std::string(kMonths[(key / 100) % 100 - 1]).substr(0, 3) + " " +
           timeLabel(t);
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

const char* todoEditsPath() { return kEditsPath; }

bool readSmallFile(const char* sdPath, std::string& out) {
    out.clear();
    FILE* f = fopen(fsPath(sdPath).c_str(), "rb");
    if (!f) return false;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

static bool writeSmallFile(const char* sdPath, const std::string& data) {
    FILE* f = fopen(fsPath(sdPath).c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::map<std::string, std::string> loadTodoEdits() {
    std::map<std::string, std::string> edits;
    std::string data;
    if (!readSmallFile(kEditsPath, data)) return edits;
    forEachLine(data.data(), data.size(), [&](size_t off, size_t len) {
        JsonDocument doc;
        if (!parseLine(data.data(), (uint32_t)off, (uint32_t)len, doc)) return;
        std::string src = doc["src"] | "";
        std::string id = doc["id"] | "";
        if (src.empty() || id.empty()) return;
        edits[editKey(src, id)] = doc["notes"] | "";
    });
    return edits;
}

bool appendTodoEdit(const std::string& src, const std::string& id, const std::string& notes) {
    uint32_t seq = 0;
    std::string data;
    if (readSmallFile(kEditsPath, data)) {
        forEachLine(data.data(), data.size(), [&](size_t off, size_t len) {
            JsonDocument doc;
            if (!parseLine(data.data(), (uint32_t)off, (uint32_t)len, doc)) return;
            uint32_t s = doc["seq"] | 0;
            if (s > seq) seq = s;
        });
    }
    JsonDocument doc;
    doc["seq"] = seq + 1;
    doc["src"] = src;
    doc["id"] = id;
    doc["notes"] = notes;
    doc["ts"] = (uint32_t)time(nullptr);
    std::string line;
    serializeJson(doc, line);
    line += '\n';
    if (!data.empty() && data.back() != '\n') line.insert(0, "\n");
    FILE* f = fopen(fsPath(kEditsPath).c_str(), "ab");
    if (!f) return false;
    bool ok = fwrite(line.data(), 1, line.size(), f) == line.size();
    fclose(f);
    return ok;
}

int dropTodoEditsUpTo(uint32_t upTo) {
    std::string data;
    if (!readSmallFile(kEditsPath, data)) return 0;
    std::string kept;
    int remaining = 0;
    forEachLine(data.data(), data.size(), [&](size_t off, size_t len) {
        JsonDocument doc;
        if (!parseLine(data.data(), (uint32_t)off, (uint32_t)len, doc)) return;
        uint32_t s = doc["seq"] | 0;
        if (s <= upTo) return;
        kept.append(data, off, len);
        kept += '\n';
        remaining++;
    });
    if (remaining == 0) {
        remove(fsPath(kEditsPath).c_str());
    } else {
        writeSmallFile(kEditsPath, kept);
    }
    return remaining;
}

// ---------------------------------------------------------------------------
// Todos
// ---------------------------------------------------------------------------

TodoStore::~TodoStore() { release(); }

void TodoStore::release() {
    free(m_buf);
    m_buf = nullptr;
    m_len = 0;
    m_items.clear();
    m_syncedAt = 0;
}

bool TodoStore::load() {
    release();
    m_edits = loadTodoEdits();
    if (!loadFile(kTodosPath, m_buf, m_len)) return false;
    forEachLine(m_buf, m_len, [&](size_t off, size_t len) {
        JsonDocument doc;
        if (!parseLine(m_buf, (uint32_t)off, (uint32_t)len, doc)) return;
        std::string k = doc["k"] | "";
        if (k == "meta") {
            m_syncedAt = doc["ts"] | 0;
        } else if (k == "item") {
            Item it;
            it.off = (uint32_t)off;
            it.len = (uint32_t)len;
            it.done = doc["done"] | false;
            it.google = std::string(doc["src"] | "") == "gt";
            m_items.push_back(it);
        }
    });
    return !m_items.empty();
}

std::string TodoStore::field(size_t i, const char* key) const {
    if (i >= m_items.size()) return "";
    JsonDocument doc;
    if (!parseLine(m_buf, m_items[i].off, m_items[i].len, doc)) return "";
    return doc[key] | "";
}

bool TodoStore::hasLocalEdit(size_t i) const {
    if (m_edits.empty() || i >= m_items.size()) return false;
    JsonDocument doc;
    if (!parseLine(m_buf, m_items[i].off, m_items[i].len, doc)) return false;
    return m_edits.count(editKey(doc["src"] | "", doc["id"] | "")) > 0;
}

TodoDetail TodoStore::detail(size_t i) const {
    TodoDetail d;
    if (i >= m_items.size()) return d;
    JsonDocument doc;
    if (!parseLine(m_buf, m_items[i].off, m_items[i].len, doc)) return d;
    d.src = doc["src"] | "";
    d.id = doc["id"] | "";
    d.list = doc["list"] | "";
    d.title = doc["title"] | "";
    d.due = doc["due"] | "";
    d.pri = doc["pri"] | "";
    d.notes = doc["notes"] | "";
    d.done = doc["done"] | false;
    d.readOnly = doc["ro"] | false;
    auto e = m_edits.find(editKey(d.src, d.id));
    if (e != m_edits.end()) {
        d.notes = e->second;
        d.editedLocally = true;
    }
    return d;
}

bool TodoStore::saveNotes(size_t i, const std::string& notes) {
    TodoDetail d = detail(i);
    if (!d.editable() || d.id.empty()) return false;
    if (!appendTodoEdit(d.src, d.id, notes)) return false;
    m_edits[editKey(d.src, d.id)] = notes;
    return true;
}

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------

CalendarStore::~CalendarStore() { release(); }

void CalendarStore::release() {
    free(m_buf);
    m_buf = nullptr;
    m_len = 0;
    m_events.clear();
    m_syncedAt = 0;
}

bool CalendarStore::load() {
    release();
    if (!loadFile(kCalendarPath, m_buf, m_len)) return false;
    forEachLine(m_buf, m_len, [&](size_t off, size_t len) {
        JsonDocument doc;
        if (!parseLine(m_buf, (uint32_t)off, (uint32_t)len, doc)) return;
        std::string k = doc["k"] | "";
        if (k == "meta") {
            m_syncedAt = doc["ts"] | 0;
            return;
        }
        if (k != "ev") return;
        Ev ev;
        ev.off = (uint32_t)off;
        ev.len = (uint32_t)len;
        ev.allDay = doc["allDay"] | false;
        if (ev.allDay) {
            ev.firstDay = doc["day"] | 0;
            ev.lastDay = doc["endDay"] | ev.firstDay;
            if (ev.firstDay < 19700101) return;
            if (ev.lastDay < ev.firstDay) ev.lastDay = ev.firstDay;
            ev.sortKey = (int64_t)dayStart(ev.firstDay);
        } else {
            int64_t start = (int64_t)(doc["start"] | 0u);
            int64_t end = (int64_t)(doc["end"] | (uint32_t)start);
            if (start <= 0) return;
            if (end < start) end = start;
            ev.sortKey = start;
            ev.firstDay = dayKey((time_t)start);
            // An event ending exactly at midnight doesn't spill into that day.
            ev.lastDay = end > start ? dayKey((time_t)(end - 1)) : ev.firstDay;
        }
        m_events.push_back(ev);
    });
    std::stable_sort(m_events.begin(), m_events.end(),
                     [](const Ev& a, const Ev& b) { return a.sortKey < b.sortKey; });
    return !m_events.empty();
}

std::vector<size_t> CalendarStore::eventsOn(int key) const {
    std::vector<size_t> out;
    for (size_t i = 0; i < m_events.size(); i++) {
        if (m_events[i].firstDay <= key && m_events[i].lastDay >= key && m_events[i].allDay) out.push_back(i);
    }
    for (size_t i = 0; i < m_events.size(); i++) {
        if (m_events[i].firstDay <= key && m_events[i].lastDay >= key && !m_events[i].allDay) out.push_back(i);
    }
    return out;
}

bool CalendarStore::hasEventsOn(int key) const {
    for (const auto& e : m_events) {
        if (e.firstDay <= key && e.lastDay >= key) return true;
    }
    return false;
}

int CalendarStore::nextDayWithEvents(int fromDay, int maxDays) const {
    int best = 0;
    for (const auto& e : m_events) {
        if (e.lastDay < fromDay) continue;
        int d = e.firstDay < fromDay ? fromDay : e.firstDay;
        if (!best || d < best) best = d;
    }
    if (best && best > addDays(fromDay, maxDays)) return 0;
    return best;
}

CalDetail CalendarStore::detail(size_t i) const {
    CalDetail d;
    if (i >= m_events.size()) return d;
    JsonDocument doc;
    if (!parseLine(m_buf, m_events[i].off, m_events[i].len, doc)) return d;
    d.title = doc["title"] | "";
    if (d.title.empty()) d.title = "(No title)";
    d.cal = doc["cal"] | "";
    d.loc = doc["loc"] | "";
    d.desc = doc["desc"] | "";
    d.allDay = m_events[i].allDay;
    d.day = m_events[i].firstDay;
    d.endDay = m_events[i].lastDay;
    if (!d.allDay) {
        d.start = (time_t)(doc["start"] | 0u);
        d.end = (time_t)(doc["end"] | (uint32_t)d.start);
    }
    return d;
}

} // namespace SyncStore
