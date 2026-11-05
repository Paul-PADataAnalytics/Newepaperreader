#pragma once

#include "Application.h"
#include "sync/SyncStore.h"
#include "ui/TextEditor.h"
#include <string>
#include <vector>

// Todo list synced by the companion app from NootTask and Google Tasks.
// NootTask notes can be edited here (on-screen keyboard); edits are pushed
// back to NootTask by the phone on its next connection. Google Tasks items
// are read-only.
class TodoApp : public Application {
public:
    void onCreate() override;
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
    void update() override;
    uint32_t msUntilUpdate() const override;
    bool onBack() override;
    void onBeforeSleep() override;

    bool wantsQueuedTaps() const override { return m_mode == MODE_EDIT; }
    void onInputBatchEnd() override;

private:
    enum Mode { MODE_LIST, MODE_DETAIL, MODE_EDIT };
    enum Filter { FILTER_ALL = 0, FILTER_NOOT, FILTER_GOOGLE };
    struct DisplayRow {
        bool section;
        size_t item;
        std::string label;
        int h;
    };

    SyncStore::TodoStore m_store;
    uint32_t m_storeRev = 0;
    Mode m_mode = MODE_LIST;
    Filter m_filter = FILTER_ALL;
    bool m_showDone = false;
    int m_page = 0;
    std::vector<DisplayRow> m_rows;
    std::vector<std::pair<size_t, size_t>> m_pages; // [first, end) into m_rows

    // Detail
    size_t m_item = 0;
    std::string m_itemKey; // src:id, to find the item again after a sync
    int m_notesPage = 0;
    std::vector<std::string> m_noteLines;
    int m_notesPerPage = 0;
    int m_notesTop = 0;

    TextEditor m_editor;
    uint32_t m_lastRevCheck = 0;

    void reload();
    void buildRows();
    void drawList();
    void drawDetail();
    void prepareDetail();
    std::string actionLabel() const;
    void openDetail(size_t item);
};
