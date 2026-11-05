#pragma once

// Full-screen text editor with the on-screen keyboard (portrait).
//
//   header: "<  Title                 [Discard]"   (back = keep changes)
//   text area (word-wrapped, tap to place the cursor, scrolls)
//   SoftKeyboard
//
// Typing is designed to be as fast as the panel allows: key presses only
// edit the in-memory text; the host app calls flush() once per batch of
// presses (Application::onInputBatchEnd), which re-renders just the rows
// that changed and pushes them with DisplayHAL::updateScreenDirect(). After
// a pause, settle() restores anti-aliased text and clears any ghosting.

#include "SoftKeyboard.h"
#include <cstdint>
#include <string>
#include <vector>

class TextEditor {
public:
    enum class Result { NONE, DONE, DISCARD };

    void open(const std::string& title, const std::string& text);
    const std::string& text() const { return m_text; }
    bool changed() const { return m_text != m_original; }

    // Full redraw (full refresh).
    void draw();
    // Handles one tap; only edits state. Call flush() after a batch.
    Result handleTouch(int x, int y);
    // Pushes all pending changes to the panel with one direct update.
    void flush();
    // Call from Application::update(): cleans up after a typing pause.
    void update();
    uint32_t msUntilUpdate() const;

private:
    struct Line {
        size_t start;
        size_t len;
    };
    struct Row {
        std::string text;
        int cursorX = -1;
        bool valid = false;
    };

    std::string m_title;
    std::string m_text;
    std::string m_original;
    size_t m_cursor = 0;
    std::vector<Line> m_lines;
    std::vector<Row> m_shown;
    int m_firstLine = 0;
    bool m_textDirty = false;
    bool m_layoutDirty = true;
    bool m_keysDirty = false;
    SoftKeyboard m_keyboard;

    uint32_t m_lastFlushMs = 0;
    int m_directUpdates = 0;
    bool m_keysChangedSinceSettle = false;

    void layout();
    int cursorLine() const;
    int cursorXOnLine(int line) const;
    int visibleRows() const;
    void ensureCursorVisible();
    void renderRows(bool force);
    void placeCursor(int x, int y);
    void updateSentenceStart();
    void insert(const std::string& s);
    void backspace();
    void settle();
};
