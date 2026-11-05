#include "TextEditor.h"
#include "UIFramework.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include <cstdlib>

#ifndef NATIVE_TESTING
#include <Arduino.h>
static uint32_t nowMs() { return millis(); }
#else
#include <chrono>
static uint32_t nowMs() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
#endif

extern uint8_t* framebuffer;
extern TypographyEngine typography;

using namespace UI;

namespace {
constexpr float EDIT_FONT = 28.0f;
constexpr int TEXT_TOP = HEADER_H + 12;
constexpr int TEXT_BOTTOM = SoftKeyboard::TOP - 10;
constexpr int TEXT_X = MARGIN;
constexpr int TEXT_W = W - 2 * MARGIN - 6;
constexpr int CURSOR_W = 3;
// Pause after the last key press before the anti-aliased clean-up redraw.
constexpr uint32_t SETTLE_MS = 3000;
const char* kDiscard = "Discard";

int lineH() { return lineHeight(EDIT_FONT) + 6; }

int measure(const std::string& s, size_t start, size_t len) {
    if (len == 0) return 0;
    return typography.measureText(s.c_str() + start, len);
}

size_t nextCodePoint(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    i++;
    while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
    return i;
}
} // namespace

void TextEditor::open(const std::string& title, const std::string& text) {
    m_title = title;
    m_text = text;
    m_original = text;
    m_cursor = m_text.size();
    m_firstLine = 0;
    m_layoutDirty = true;
    m_keysDirty = false;
    m_directUpdates = 0;
    m_keysChangedSinceSettle = false;
    m_shown.clear();
    m_keyboard = SoftKeyboard();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void TextEditor::layout() {
    typography.setFontSize(EDIT_FONT);
    const int spaceW = typography.measureText(" ", 1);
    m_lines.clear();
    size_t p = 0;
    const size_t n = m_text.size();
    while (true) {
        size_t paraEnd = m_text.find('\n', p);
        if (paraEnd == std::string::npos) paraEnd = n;
        size_t lineStart = p;
        int lineW = 0;
        size_t i = p;
        while (i < paraEnd) {
            // Leading spaces of a word belong to the current line.
            size_t spaces = 0;
            while (i + spaces < paraEnd && m_text[i + spaces] == ' ') spaces++;
            size_t wordStart = i + spaces;
            size_t wordEnd = wordStart;
            while (wordEnd < paraEnd && m_text[wordEnd] != ' ') wordEnd++;
            int wordW = measure(m_text, wordStart, wordEnd - wordStart);
            int add = (int)spaces * spaceW + wordW;
            if (lineW + add <= TEXT_W || wordStart == wordEnd) {
                lineW += add;
                i = wordEnd;
                continue;
            }
            if (i > lineStart && wordW <= TEXT_W) {
                // Wrap before the word; the spaces stay at the end of this line.
                m_lines.push_back({lineStart, wordStart - lineStart});
                lineStart = wordStart;
                lineW = wordW;
                i = wordEnd;
                continue;
            }
            // A word wider than the line: break it by characters.
            size_t c = wordStart;
            int w = lineW + (int)spaces * spaceW;
            while (c < wordEnd) {
                size_t nx = nextCodePoint(m_text, c);
                int cw = measure(m_text, c, nx - c);
                if (w + cw > TEXT_W && c > lineStart) {
                    m_lines.push_back({lineStart, c - lineStart});
                    lineStart = c;
                    w = 0;
                }
                w += cw;
                c = nx;
            }
            lineW = w;
            i = wordEnd;
        }
        m_lines.push_back({lineStart, paraEnd - lineStart});
        if (paraEnd >= n) break;
        p = paraEnd + 1;
    }
}

int TextEditor::cursorLine() const {
    int line = 0;
    for (size_t i = 0; i < m_lines.size(); i++) {
        if (m_lines[i].start <= m_cursor) line = (int)i;
        else break;
    }
    return line;
}

int TextEditor::cursorXOnLine(int line) const {
    const Line& l = m_lines[line];
    size_t len = m_cursor > l.start ? m_cursor - l.start : 0;
    if (len > l.len) len = l.len;
    typography.setFontSize(EDIT_FONT);
    int x = TEXT_X + measure(m_text, l.start, len);
    if (x > TEXT_X + TEXT_W) x = TEXT_X + TEXT_W;
    return x;
}

int TextEditor::visibleRows() const { return (TEXT_BOTTOM - TEXT_TOP) / lineH(); }

void TextEditor::ensureCursorVisible() {
    const int rows = visibleRows();
    const int cl = cursorLine();
    if (cl < m_firstLine) m_firstLine = cl;
    if (cl >= m_firstLine + rows) m_firstLine = cl - rows + 1;
    int maxFirst = (int)m_lines.size() - rows;
    if (maxFirst < 0) maxFirst = 0;
    if (m_firstLine > maxFirst) m_firstLine = maxFirst;
    if (m_firstLine < 0) m_firstLine = 0;
}

void TextEditor::updateSentenceStart() {
    size_t i = m_cursor;
    bool sawSpace = false;
    while (i > 0 && m_text[i - 1] == ' ') {
        i--;
        sawSpace = true;
    }
    bool start = false;
    if (i == 0) {
        start = true;
    } else {
        char c = m_text[i - 1];
        start = c == '\n' || (sawSpace && (c == '.' || c == '!' || c == '?'));
    }
    m_keyboard.setSentenceStart(start);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void TextEditor::renderRows(bool force) {
    const int rows = visibleRows();
    const int lh = lineH();
    if ((int)m_shown.size() != rows) {
        m_shown.assign(rows, Row());
        force = true;
    }
    const int cl = cursorLine();
    typography.setFontSize(EDIT_FONT);
    for (int r = 0; r < rows; r++) {
        int idx = m_firstLine + r;
        Row row;
        row.valid = true;
        if (idx < (int)m_lines.size()) {
            row.text = m_text.substr(m_lines[idx].start, m_lines[idx].len);
            if (idx == cl) row.cursorX = cursorXOnLine(idx);
        }
        const Row& old = m_shown[r];
        if (!force && old.valid && old.text == row.text && old.cursorX == row.cursorX) continue;
        const int y = TEXT_TOP + r * lh;
        fillRect(0, y, W, lh, WHITE);
        if (!row.text.empty()) {
            typography.setFontSize(EDIT_FONT);
            typography.renderLine(row.text.c_str(), row.text.size(), TEXT_X, y + 3, framebuffer, BLACK);
        }
        if (row.cursorX >= 0) fillRect(row.cursorX, y + 3, CURSOR_W, lh - 6, BLACK);
        m_shown[r] = row;
    }
    // Scroll marks on the right edge when text is above or below the view.
    const int markX = W - 10;
    fillRect(markX, TEXT_TOP, 6, rows * lh, WHITE);
    if (m_firstLine > 0) fillRect(markX, TEXT_TOP, 6, 24, BLACK);
    if (m_firstLine + rows < (int)m_lines.size()) fillRect(markX, TEXT_TOP + rows * lh - 24, 6, 24, BLACK);
}

void TextEditor::draw() {
    if (m_layoutDirty) {
        layout();
        m_layoutDirty = false;
        ensureCursorVisible();
    }
    updateSentenceStart();
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        header(m_title, true, kDiscard);
        renderRows(true);
        m_keyboard.draw();
    });
    m_keysDirty = false;
    m_directUpdates = 0;
    m_keysChangedSinceSettle = false;
}

void TextEditor::flush() {
    if (m_layoutDirty) {
        layout();
        m_layoutDirty = false;
        ensureCursorVisible();
    }
    updateSentenceStart();
    renderRows(false);
    // Key labels are anti-aliased: after a layer switch the clean-up pass
    // includes the keyboard. The Shift key is pure black/white.
    bool layerChanged = false;
    m_keyboard.redraw(&layerChanged);
    if (layerChanged) m_keysChangedSinceSettle = true;
    m_keysDirty = false;
    DisplayHAL::updateScreenDirect();
    m_lastFlushMs = nowMs();
    m_directUpdates++;
}

void TextEditor::settle() {
    m_directUpdates = 0;
    const int top = TEXT_TOP - 4;
    int bottom = TEXT_BOTTOM + 4;
    if (m_keysChangedSinceSettle) bottom = H;
    m_keysChangedSinceSettle = false;
    DisplayHAL::invalidateRegion(0, top, W, bottom - top);
    DisplayHAL::updateScreenFast();
}

void TextEditor::update() {
    if (m_directUpdates > 0 && nowMs() - m_lastFlushMs >= SETTLE_MS) settle();
}

uint32_t TextEditor::msUntilUpdate() const {
    if (m_directUpdates == 0) return UINT32_MAX;
    uint32_t since = nowMs() - m_lastFlushMs;
    return since >= SETTLE_MS ? 0 : SETTLE_MS - since;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void TextEditor::insert(const std::string& s) {
    m_text.insert(m_cursor, s);
    m_cursor += s.size();
    m_layoutDirty = true;
}

void TextEditor::backspace() {
    if (m_cursor == 0) return;
    size_t p = m_cursor - 1;
    while (p > 0 && ((unsigned char)m_text[p] & 0xC0) == 0x80) p--;
    m_text.erase(p, m_cursor - p);
    m_cursor = p;
    m_layoutDirty = true;
}

void TextEditor::placeCursor(int x, int y) {
    if (m_layoutDirty) {
        layout();
        m_layoutDirty = false;
        ensureCursorVisible();
    }
    int row = (y - TEXT_TOP) / lineH();
    if (row < 0) row = 0;
    int idx = m_firstLine + row;
    if (idx >= (int)m_lines.size()) {
        m_cursor = m_text.size();
        return;
    }
    const Line& l = m_lines[idx];
    typography.setFontSize(EDIT_FONT);
    size_t best = l.start;
    int bestD = abs(x - TEXT_X);
    size_t p = l.start;
    while (p < l.start + l.len) {
        p = nextCodePoint(m_text, p);
        int d = abs(x - (TEXT_X + measure(m_text, l.start, p - l.start)));
        if (d <= bestD) {
            bestD = d;
            best = p;
        }
    }
    m_cursor = best;
}

TextEditor::Result TextEditor::handleTouch(int x, int y) {
    if (hitHeaderAction(x, y, kDiscard)) return Result::DISCARD;
    if (m_keyboard.contains(x, y)) {
        SoftKeyboard::Result r = m_keyboard.press(x, y);
        switch (r.action) {
        case SoftKeyboard::Action::INSERT:
            insert(r.text);
            break;
        case SoftKeyboard::Action::BACKSPACE:
            backspace();
            break;
        case SoftKeyboard::Action::ENTER:
            insert("\n");
            break;
        default:
            break;
        }
        if (r.redrawKeys) m_keysDirty = true;
        // Later taps in the same batch must see this one's effect on
        // auto-capitalisation.
        if (r.action != SoftKeyboard::Action::NONE) updateSentenceStart();
        return Result::NONE;
    }
    if (y >= TEXT_TOP && y < TEXT_BOTTOM) {
        placeCursor(x, y);
        updateSentenceStart();
        return Result::NONE;
    }
    return Result::NONE;
}
