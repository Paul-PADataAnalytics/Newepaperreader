#include "TodoApp.h"
#include "AppComm.h"
#include "PowerManager.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"

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

const Rect kFilter(MARGIN, HEADER_H + 12, W - 2 * MARGIN, 52);
constexpr int INFO_Y = HEADER_H + 74;
constexpr int LIST_TOP = HEADER_H + 108;
constexpr int LIST_BOTTOM = H - FOOTER_H;
constexpr int SECTION_H = 46;
constexpr int ITEM_H = 88;
constexpr float NOTES_FONT = 26.0f;
const char* kEdit = "Edit notes";

void checkbox(int x, int y, bool done) {
    strokeRoundRect(x, y, 30, 30, 5, 2, BLACK);
    if (done) {
        line(x + 6, y + 15, x + 12, y + 22, 3, BLACK);
        line(x + 12, y + 22, x + 25, y + 7, 3, BLACK);
    }
}
} // namespace

void TodoApp::onCreate() {
    m_mode = MODE_LIST;
    reload();
}

void TodoApp::reload() {
    m_store.load();
    m_storeRev = SyncStore::todosRevision();
    buildRows();
}

void TodoApp::buildRows() {
    m_rows.clear();
    m_pages.clear();
    std::string lastGroup;
    for (size_t i = 0; i < m_store.size(); i++) {
        bool google = m_store.google(i);
        if (m_filter == FILTER_NOOT && google) continue;
        if (m_filter == FILTER_GOOGLE && !google) continue;
        if (!m_showDone && m_store.done(i)) continue;
        std::string list = m_store.list(i);
        std::string group = std::string(google ? "g:" : "n:") + list;
        if (group != lastGroup) {
            std::string label = list.empty() ? (google ? "Google Tasks" : "NootTask") : list;
            if (m_filter == FILTER_ALL) label += google ? "  \xC2\xB7  Google" : "  \xC2\xB7  NootTask";
            m_rows.push_back({true, 0, label, SECTION_H});
            lastGroup = group;
        }
        m_rows.push_back({false, i, "", ITEM_H});
    }
    size_t first = 0;
    int used = 0;
    for (size_t r = 0; r < m_rows.size(); r++) {
        int h = m_rows[r].h;
        // Keep a section heading with its first item.
        int need = m_rows[r].section ? h + ITEM_H : h;
        if (used > 0 && used + need > LIST_BOTTOM - LIST_TOP) {
            m_pages.push_back({first, r});
            first = r;
            used = 0;
        }
        used += h;
    }
    if (first < m_rows.size()) m_pages.push_back({first, m_rows.size()});
    if (m_pages.empty()) m_pages.push_back({0, 0});
    if (m_page >= (int)m_pages.size()) m_page = (int)m_pages.size() - 1;
    if (m_page < 0) m_page = 0;
}

std::string TodoApp::actionLabel() const {
    if (m_mode == MODE_LIST) return m_showDone ? "Hide done" : "Show done";
    if (m_mode == MODE_DETAIL) {
        SyncStore::TodoDetail d = m_store.detail(m_item);
        return d.editable() ? kEdit : "";
    }
    return "";
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

void TodoApp::drawList() {
    header("Todos", true, actionLabel());
    segmented(kFilter, {"All", "NootTask", "Google"}, m_filter);
    std::string info = "Synced " + SyncStore::stampLabel(m_store.syncedAt());
    if (m_store.pendingEdits() > 0) {
        info += "  \xC2\xB7  " + std::to_string(m_store.pendingEdits()) + " edit" +
                (m_store.pendingEdits() == 1 ? "" : "s") + " to send";
    }
    text(ellipsize(info, W - 2 * MARGIN, FONT_CAPTION), MARGIN, INFO_Y, FONT_CAPTION, DARK);

    if (m_store.size() == 0) {
        emptyState(H / 2 + 40, "No todos yet",
                   "Turn on Bluetooth in Settings and connect the NER Companion app. "
                   "Todos from NootTask and Google Tasks sync each time it connects.");
        return;
    }
    if (m_rows.empty()) {
        emptyState(H / 2 + 40, "Nothing to show",
                   m_showDone ? "No items match this filter." : "All done here. Tap Show done to see finished items.");
        return;
    }

    int y = LIST_TOP;
    const auto& pg = m_pages[m_page];
    for (size_t r = pg.first; r < pg.second; r++) {
        const DisplayRow& row = m_rows[r];
        if (row.section) {
            sectionLabel(y + 16, ellipsize(row.label, W - 2 * MARGIN, FONT_CAPTION, true));
        } else {
            SyncStore::TodoDetail d = m_store.detail(row.item);
            checkbox(MARGIN, y + (ITEM_H - 30) / 2, d.done);
            int tx = MARGIN + 48;
            int tw = W - MARGIN - 30 - tx;
            std::string sub;
            std::string due = SyncStore::dueLabel(d.due);
            if (!due.empty()) sub = due;
            if (!d.pri.empty() && d.pri != "medium") sub += (sub.empty() ? "" : "  \xC2\xB7  ") + d.pri + " priority";
            if (!d.notes.empty()) sub += (sub.empty() ? "" : "  \xC2\xB7  ") + std::string("Notes");
            if (d.editedLocally) sub += "  \xC2\xB7  edited";
            int titleY = sub.empty() ? y + (ITEM_H - lineHeight(FONT_BODY)) / 2 : y + 12;
            text(ellipsize(d.title, tw, FONT_BODY), tx, titleY, FONT_BODY, d.done ? DARK : BLACK);
            if (d.done) {
                int sw = textWidth(ellipsize(d.title, tw, FONT_BODY), FONT_BODY);
                fillRect(tx, titleY + lineHeight(FONT_BODY) / 2 + 2, sw, 2, DARK);
            }
            if (!sub.empty()) text(ellipsize(sub, tw, FONT_SMALL), tx, y + 50, FONT_SMALL, DARK);
            chevronRight(W - MARGIN - 8, y + ITEM_H / 2, 20, DARK);
            hRule(y + ITEM_H - 1, LIGHT, MARGIN, W - MARGIN);
        }
        y += row.h;
    }
    pager(m_page, (int)m_pages.size());
}

// ---------------------------------------------------------------------------
// Detail
// ---------------------------------------------------------------------------

void TodoApp::openDetail(size_t item) {
    m_item = item;
    SyncStore::TodoDetail d = m_store.detail(item);
    m_itemKey = d.src + ":" + d.id;
    m_notesPage = 0;
    m_mode = MODE_DETAIL;
    prepareDetail();
    draw();
}

void TodoApp::prepareDetail() {
    SyncStore::TodoDetail d = m_store.detail(m_item);
    // Title block height decides where the notes start.
    std::vector<std::string> titleLines = wrapLines(d.title, W - 2 * MARGIN, FONT_TITLE);
    int titleCount = titleLines.size() > 3 ? 3 : (int)titleLines.size();
    m_notesTop = HEADER_H + 24 + titleCount * (lineHeight(FONT_TITLE) + 4) + 150;
    m_noteLines = wrapLines(d.notes, W - 2 * MARGIN, NOTES_FONT);
    int lh = lineHeight(NOTES_FONT) + 6;
    m_notesPerPage = (LIST_BOTTOM - 8 - m_notesTop) / lh;
    if (m_notesPerPage < 1) m_notesPerPage = 1;
    int pages = ((int)m_noteLines.size() + m_notesPerPage - 1) / m_notesPerPage;
    if (pages < 1) pages = 1;
    if (m_notesPage >= pages) m_notesPage = pages - 1;
}

void TodoApp::drawDetail() {
    SyncStore::TodoDetail d = m_store.detail(m_item);
    header(d.src == "nt" ? "NootTask" : "Google Tasks", true, actionLabel());
    int y = textWrapped(d.title, MARGIN, HEADER_H + 24, W - 2 * MARGIN, FONT_TITLE, BLACK, 3, 4);

    std::string meta = d.done ? "Done" : "Open";
    std::string due = SyncStore::dueLabel(d.due);
    if (!due.empty()) meta += "  \xC2\xB7  Due " + due;
    if (!d.pri.empty()) meta += "  \xC2\xB7  " + d.pri + " priority";
    text(ellipsize(meta, W - 2 * MARGIN, FONT_SMALL + 2), MARGIN, y + 8, FONT_SMALL + 2, INK, true);
    text(ellipsize(d.list, W - 2 * MARGIN, FONT_SMALL), MARGIN, y + 44, FONT_SMALL, DARK);

    int notesLabelY = m_notesTop - 36;
    hRule(notesLabelY - 14);
    std::string label = "Notes";
    if (d.editedLocally) label += "  \xC2\xB7  edited here, sends on next sync";
    else if (!d.editable()) label += "  \xC2\xB7  read-only";
    sectionLabel(notesLabelY, ellipsize(label, W - 2 * MARGIN, FONT_CAPTION, true));

    const int lh = lineHeight(NOTES_FONT) + 6;
    if (m_noteLines.empty()) {
        text(d.editable() ? "No notes. Tap Edit notes to add some." : "No notes.", MARGIN, m_notesTop, NOTES_FONT, DARK);
    }
    int first = m_notesPage * m_notesPerPage;
    for (int i = 0; i < m_notesPerPage && first + i < (int)m_noteLines.size(); i++) {
        text(m_noteLines[first + i], MARGIN, m_notesTop + i * lh, NOTES_FONT, INK);
    }
    int pages = ((int)m_noteLines.size() + m_notesPerPage - 1) / m_notesPerPage;
    if (pages > 1) pager(m_notesPage, pages);
}

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

void TodoApp::draw() {
    if (m_mode == MODE_EDIT) {
        m_editor.draw();
        return;
    }
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        if (m_mode == MODE_LIST) drawList();
        else drawDetail();
    });
}

bool TodoApp::onBack() {
    switch (m_mode) {
    case MODE_EDIT:
        // Back keeps the changes (like most note apps); Discard drops them.
        if (m_editor.changed()) m_store.saveNotes(m_item, m_editor.text());
        m_mode = MODE_DETAIL;
        prepareDetail();
        draw();
        return true;
    case MODE_DETAIL:
        m_mode = MODE_LIST;
        buildRows();
        draw();
        return true;
    default:
        return false;
    }
}

void TodoApp::onBeforeSleep() {
    // Sleeping (button or auto-sleep) mid-edit keeps the text, like Back.
    if (m_mode == MODE_EDIT && m_editor.changed()) m_store.saveNotes(m_item, m_editor.text());
}

void TodoApp::onInputBatchEnd() {
    if (m_mode == MODE_EDIT) m_editor.flush();
}

void TodoApp::handleTouch(int x, int y) {
    if (m_mode == MODE_EDIT) {
        if (m_editor.handleTouch(x, y) == TextEditor::Result::DISCARD) {
            m_mode = MODE_DETAIL;
            prepareDetail();
            draw();
        }
        return;
    }

    if (m_mode == MODE_DETAIL) {
        SyncStore::TodoDetail d = m_store.detail(m_item);
        if (d.editable() && hitHeaderAction(x, y, kEdit)) {
            m_mode = MODE_EDIT;
            m_editor.open("Edit notes", d.notes);
            m_editor.draw();
            return;
        }
        int pages = ((int)m_noteLines.size() + m_notesPerPage - 1) / m_notesPerPage;
        int step = pages > 1 ? hitPager(x, y, m_notesPage, pages) : 0;
        if (step) {
            m_notesPage += step;
            UIFramework::performFastPartialUpdate(framebuffer, 0, m_notesTop - 4, W, H - m_notesTop + 4,
                                                  [this]() { drawDetail(); });
        }
        return;
    }

    // List
    if (hitHeaderAction(x, y, actionLabel())) {
        m_showDone = !m_showDone;
        m_page = 0;
        buildRows();
        draw();
        return;
    }
    int f = hitSegmented(kFilter, 3, x, y);
    if (f >= 0) {
        if (f != m_filter) {
            m_filter = (Filter)f;
            m_page = 0;
            buildRows();
            UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() { drawList(); });
        }
        return;
    }
    int step = hitPager(x, y, m_page, (int)m_pages.size());
    if (step) {
        m_page += step;
        UIFramework::performFastPartialUpdate(framebuffer, 0, LIST_TOP, W, H - LIST_TOP, [this]() { drawList(); });
        return;
    }
    if (y >= LIST_TOP && y < LIST_BOTTOM && !m_rows.empty()) {
        int ry = LIST_TOP;
        const auto& pg = m_pages[m_page];
        for (size_t r = pg.first; r < pg.second; r++) {
            if (y >= ry && y < ry + m_rows[r].h) {
                if (!m_rows[r].section) openDetail(m_rows[r].item);
                return;
            }
            ry += m_rows[r].h;
        }
    }
}

void TodoApp::update() {
    if (m_mode == MODE_EDIT) {
        m_editor.update();
        return;
    }
    uint32_t now = millis();
    if (now - m_lastRevCheck < 1000) return;
    m_lastRevCheck = now;
    if (SyncStore::todosRevision() == m_storeRev) return;
    reload();
    if (m_mode == MODE_DETAIL) {
        bool found = false;
        for (size_t i = 0; i < m_store.size(); i++) {
            SyncStore::TodoDetail d = m_store.detail(i);
            if (d.src + ":" + d.id == m_itemKey) {
                m_item = i;
                found = true;
                break;
            }
        }
        if (found) prepareDetail();
        else m_mode = MODE_LIST;
    }
    draw();
}

uint32_t TodoApp::msUntilUpdate() const {
    if (m_mode == MODE_EDIT) return m_editor.msUntilUpdate();
    return AppComm::isInitialized() ? 1000 : UINT32_MAX;
}
