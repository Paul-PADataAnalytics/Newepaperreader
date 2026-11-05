#include "SoftKeyboard.h"
#include <cctype>

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

using namespace UI;

namespace {
constexpr int SIDE = 4;
constexpr int GAP = 6;
constexpr int KEY_H = 78;
constexpr int ROW_GAP = 8;
constexpr float UNIT = (W - 2 * SIDE) / 10.0f;
constexpr float KEY_FONT = 32.0f;
constexpr float KEY_FONT_SMALL = 24.0f;
constexpr uint32_t DOUBLE_TAP_MS = 450;

int rowY(int row) { return SoftKeyboard::TOP + ROW_GAP + row * (KEY_H + ROW_GAP); }
} // namespace

SoftKeyboard::SoftKeyboard() { build(); }

void SoftKeyboard::build() {
    m_keys.clear();
    struct Spec {
        Kind kind;
        const char* label;
        float units;
    };
    auto addRow = [this](int row, float startUnits, const std::vector<Spec>& specs) {
        float u = startUnits;
        for (const Spec& s : specs) {
            int x0 = SIDE + (int)(u * UNIT + 0.5f);
            int x1 = SIDE + (int)((u + s.units) * UNIT + 0.5f);
            Key k;
            k.kind = s.kind;
            k.label = s.label;
            k.r = Rect(x0 + GAP / 2, rowY(row), x1 - x0 - GAP, KEY_H);
            m_keys.push_back(k);
            u += s.units;
        }
    };
    auto chars = [](const char* const* labels, int n, float units) {
        std::vector<Spec> v;
        for (int i = 0; i < n; i++) v.push_back({K_CHAR, labels[i], units});
        return v;
    };

    if (m_layer == LETTERS) {
        static const char* r0[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
        static const char* r1[] = {"A", "S", "D", "F", "G", "H", "J", "K", "L"};
        static const char* r2[] = {"Z", "X", "C", "V", "B", "N", "M"};
        addRow(0, 0, chars(r0, 10, 1));
        addRow(1, 0.5f, chars(r1, 9, 1));
        std::vector<Spec> row2 = {{K_SHIFT, "", 1.5f}};
        for (const Spec& s : chars(r2, 7, 1)) row2.push_back(s);
        row2.push_back({K_BACKSPACE, "", 1.5f});
        addRow(2, 0, row2);
        addRow(3, 0, {{K_TO_NUMBERS, "123", 1.5f}, {K_CHAR, ",", 1}, {K_SPACE, "space", 5},
                      {K_CHAR, ".", 1}, {K_ENTER, "", 1.5f}});
    } else {
        static const char* n0[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
        static const char* n1[] = {"-", "/", ":", ";", "(", ")", "\xC2\xA3", "&", "@", "\""};
        static const char* s0[] = {"[", "]", "{", "}", "#", "%", "^", "*", "+", "="};
        static const char* s1[] = {"_", "\\", "|", "~", "<", ">", "\xE2\x82\xAC", "$", "\xC2\xA5", "\xE2\x80\xA2"};
        static const char* p2[] = {".", ",", "?", "!", "'"};
        const bool numbers = m_layer == NUMBERS;
        addRow(0, 0, chars(numbers ? n0 : s0, 10, 1));
        addRow(1, 0, chars(numbers ? n1 : s1, 10, 1));
        std::vector<Spec> row2 = {{numbers ? K_TO_SYMBOLS : K_TO_NUMBERS, numbers ? "#+=" : "123", 1.5f}};
        for (const Spec& s : chars(p2, 5, 1.4f)) row2.push_back(s);
        row2.push_back({K_BACKSPACE, "", 1.5f});
        addRow(2, 0, row2);
        addRow(3, 0, {{K_TO_LETTERS, "ABC", 1.5f}, {K_CHAR, ",", 1}, {K_SPACE, "space", 5},
                      {K_CHAR, ".", 1}, {K_ENTER, "", 1.5f}});
    }
}

void SoftKeyboard::drawKey(const Key& k) const {
    const Rect& r = k.r;
    const int cx = r.x + r.w / 2;
    const int cy = r.y + r.h / 2;
    bool inverted = false;
    if (k.kind == K_SHIFT) {
        Shift shown = m_shift != SHIFT_OFF ? m_shift : (m_autoShift ? SHIFT_ONCE : SHIFT_OFF);
        inverted = shown != SHIFT_OFF;
        if (inverted) fillRoundRect(r.x, r.y, r.w, r.h, 10, BLACK);
        else strokeRoundRect(r.x, r.y, r.w, r.h, 10, 2, BLACK);
        const uint8_t c = inverted ? WHITE : BLACK;
        // Up arrow outline
        const int top = cy - 18, mid = cy - 2, bot = cy + 14;
        line(cx, top, cx - 16, mid, 3, c);
        line(cx, top, cx + 16, mid, 3, c);
        line(cx - 16, mid, cx - 7, mid, 3, c);
        line(cx + 16, mid, cx + 7, mid, 3, c);
        line(cx - 7, mid, cx - 7, bot, 3, c);
        line(cx + 7, mid, cx + 7, bot, 3, c);
        line(cx - 7, bot, cx + 7, bot, 3, c);
        if (shown == SHIFT_LOCK) line(cx - 10, bot + 7, cx + 10, bot + 7, 3, c);
        return;
    }
    strokeRoundRect(r.x, r.y, r.w, r.h, 10, 2, BLACK);
    switch (k.kind) {
    case K_BACKSPACE: {
        const int l = cx - 22, rt = cx + 20, t = cy - 14, b = cy + 14;
        line(l, cy, l + 13, t, 3, BLACK);
        line(l, cy, l + 13, b, 3, BLACK);
        line(l + 13, t, rt, t, 3, BLACK);
        line(l + 13, b, rt, b, 3, BLACK);
        line(rt, t, rt, b, 3, BLACK);
        line(cx - 2, cy - 7, cx + 12, cy + 7, 3, BLACK);
        line(cx - 2, cy + 7, cx + 12, cy - 7, 3, BLACK);
        break;
    }
    case K_ENTER: {
        const int rt = cx + 18, l = cx - 18;
        line(rt, cy - 16, rt, cy + 6, 3, BLACK);
        line(rt, cy + 6, l, cy + 6, 3, BLACK);
        line(l, cy + 6, l + 10, cy - 4, 3, BLACK);
        line(l, cy + 6, l + 10, cy + 16, 3, BLACK);
        break;
    }
    default: {
        const bool small = k.kind != K_CHAR;
        const float size = small ? KEY_FONT_SMALL : KEY_FONT;
        textCentered(k.label, cx, r.y + (r.h - lineHeight(size)) / 2, size, BLACK, !small);
        break;
    }
    }
}

void SoftKeyboard::draw() {
    fillRect(0, TOP, W, HEIGHT, WHITE);
    hRule(TOP, BLACK, 0, W, 2);
    for (const Key& k : m_keys) drawKey(k);
    m_drawnLayer = m_layer;
    m_drawnShift = m_shift != SHIFT_OFF ? m_shift : (m_autoShift ? SHIFT_ONCE : SHIFT_OFF);
}

bool SoftKeyboard::redraw(bool* layerChanged) {
    if (layerChanged) *layerChanged = false;
    if (m_drawnLayer != m_layer) {
        draw();
        if (layerChanged) *layerChanged = true;
        return true;
    }
    Shift shown = m_shift != SHIFT_OFF ? m_shift : (m_autoShift ? SHIFT_ONCE : SHIFT_OFF);
    if (m_layer != LETTERS || shown == m_drawnShift) return false;
    for (const Key& k : m_keys) {
        if (k.kind != K_SHIFT) continue;
        fillRect(k.r.x - 2, k.r.y - 2, k.r.w + 4, k.r.h + 4, WHITE);
        drawKey(k);
    }
    m_drawnShift = shown;
    return true;
}

const SoftKeyboard::Key* SoftKeyboard::keyAt(int x, int y) const {
    if (y < TOP) return nullptr;
    // Nearest key, so taps in the gaps between keys still count.
    const Key* best = nullptr;
    int bestD = 1 << 30;
    for (const Key& k : m_keys) {
        int dx = x < k.r.x ? k.r.x - x : (x >= k.r.x + k.r.w ? x - (k.r.x + k.r.w - 1) : 0);
        int dy = y < k.r.y ? k.r.y - y : (y >= k.r.y + k.r.h ? y - (k.r.y + k.r.h - 1) : 0);
        int d = dx * dx + dy * dy;
        if (d < bestD) {
            bestD = d;
            best = &k;
        }
    }
    return bestD <= 24 * 24 ? best : nullptr;
}

void SoftKeyboard::setSentenceStart(bool start) { m_autoShift = start; }

SoftKeyboard::Result SoftKeyboard::press(int x, int y) {
    Result res;
    const Key* k = keyAt(x, y);
    if (!k) return res;
    const Shift before = m_shift != SHIFT_OFF ? m_shift : (m_autoShift ? SHIFT_ONCE : SHIFT_OFF);
    switch (k->kind) {
    case K_CHAR: {
        res.action = Action::INSERT;
        res.text = k->label;
        if (m_layer == LETTERS && res.text.size() == 1 && isalpha((unsigned char)res.text[0])) {
            bool upper = before != SHIFT_OFF;
            if (!upper) res.text[0] = (char)tolower((unsigned char)res.text[0]);
            if (m_shift == SHIFT_ONCE) m_shift = SHIFT_OFF;
            m_autoShift = false;
        }
        break;
    }
    case K_SHIFT: {
        uint32_t now = nowMs();
        if (before == SHIFT_OFF) {
            m_shift = SHIFT_ONCE;
        } else if (before == SHIFT_ONCE && now - m_lastShiftMs < DOUBLE_TAP_MS) {
            m_shift = SHIFT_LOCK;
        } else {
            m_shift = SHIFT_OFF;
            m_autoShift = false;
        }
        m_lastShiftMs = now;
        break;
    }
    case K_BACKSPACE:
        res.action = Action::BACKSPACE;
        break;
    case K_SPACE:
        res.action = Action::INSERT;
        res.text = " ";
        break;
    case K_ENTER:
        res.action = Action::ENTER;
        break;
    case K_TO_LETTERS:
        m_layer = LETTERS;
        build();
        break;
    case K_TO_NUMBERS:
        m_layer = NUMBERS;
        build();
        break;
    case K_TO_SYMBOLS:
        m_layer = SYMBOLS;
        build();
        break;
    }
    const Shift after = m_shift != SHIFT_OFF ? m_shift : (m_autoShift ? SHIFT_ONCE : SHIFT_OFF);
    res.redrawKeys = m_layer != m_drawnLayer || after != before;
    return res;
}
