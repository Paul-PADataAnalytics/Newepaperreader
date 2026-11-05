#pragma once

// Portrait on-screen keyboard (540 px wide, docked at the bottom).
//
// Built for speed on e-paper: letter keys always show capitals, so Shift
// only redraws the Shift key; key presses give no visual flash; the host
// redraws once per batch of presses (see TextEditor / Application::
// wantsQueuedTaps()). All drawing is pure black on white so the fast
// direct-update waveform renders it cleanly.

#include "UIKit.h"
#include <string>
#include <vector>

class SoftKeyboard {
public:
    static constexpr int HEIGHT = 352;
    static constexpr int TOP = UI::H - HEIGHT;

    enum class Action { NONE, INSERT, BACKSPACE, ENTER };
    struct Result {
        Action action = Action::NONE;
        std::string text;      // for INSERT
        bool redrawKeys = false; // layer or shift state changed
    };

    SoftKeyboard();

    // Draws the whole keyboard into the framebuffer (no flush).
    void draw();
    // Redraws only what changed since the last draw()/redraw (shift key, or
    // everything after a layer switch). Returns false when nothing changed;
    // `layerChanged` reports a full keyboard redraw.
    bool redraw(bool* layerChanged = nullptr);

    bool contains(int x, int y) const { return y >= TOP; }
    Result press(int x, int y);

    // Auto-capitalisation: the host tells the keyboard whether the next
    // letter starts a sentence.
    void setSentenceStart(bool start);

private:
    enum Layer { LETTERS = 0, NUMBERS, SYMBOLS };
    enum Kind { K_CHAR, K_SHIFT, K_BACKSPACE, K_SPACE, K_ENTER, K_TO_LETTERS, K_TO_NUMBERS, K_TO_SYMBOLS };
    struct Key {
        Kind kind;
        std::string label;
        UI::Rect r;
    };
    enum Shift { SHIFT_OFF = 0, SHIFT_ONCE, SHIFT_LOCK };

    std::vector<Key> m_keys;
    Layer m_layer = LETTERS;
    Shift m_shift = SHIFT_OFF;
    bool m_autoShift = false;
    uint32_t m_lastShiftMs = 0;

    Layer m_drawnLayer = LETTERS;
    Shift m_drawnShift = SHIFT_OFF;

    void build();
    void drawKey(const Key& k) const;
    const Key* keyAt(int x, int y) const;
};
