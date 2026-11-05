#define UI_ICONS_DEFINE_DATA
#include "UIKit.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "AppComm.h"
#include "PowerManager.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>

extern uint8_t* framebuffer;
extern TypographyEngine typography;

namespace UI {

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

int textWidth(const std::string& s, float size, bool bold) {
    typography.setFontSize(size);
    return typography.measureText(s) + (bold ? 1 : 0);
}

int lineHeight(float size) {
    typography.setFontSize(size);
    return typography.lineHeight();
}

void text(const std::string& s, int x, int y, float size, uint8_t color, bool bold) {
    if (s.empty()) return;
    typography.setFontSize(size);
    typography.renderLine(s.c_str(), s.size(), x, y, framebuffer, color);
    if (bold) typography.renderLine(s.c_str(), s.size(), x + 1, y, framebuffer, color);
}

void textCentered(const std::string& s, int cx, int y, float size, uint8_t color, bool bold) {
    text(s, cx - textWidth(s, size, bold) / 2, y, size, color, bold);
}

void textRight(const std::string& s, int rightX, int y, float size, uint8_t color, bool bold) {
    text(s, rightX - textWidth(s, size, bold), y, size, color, bold);
}

std::string ellipsize(const std::string& s, int maxW, float size, bool bold) {
    if (maxW <= 0) return "";
    if (textWidth(s, size, bold) <= maxW) return s;
    std::string out = s;
    std::string suffix = "...";
    while (!suffix.empty() && textWidth(suffix, size, bold) > maxW) suffix.pop_back();
    if (suffix.empty()) return "";
    while (!out.empty()) {
        // Drop a whole UTF-8 code point at a time.
        do {
            out.pop_back();
        } while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80);
        if (!out.empty() && ((unsigned char)out.back() & 0x80)) out.pop_back();
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (textWidth(out + suffix, size, bold) <= maxW) return out + suffix;
    }
    return suffix;
}

std::vector<std::string> wrapLines(const std::string& s, int w, float size) {
    std::vector<std::string> lines;
    if (s.empty() || w <= 0) return lines;
    size_t paragraphStart = 0;
    while (paragraphStart < s.size()) {
        size_t paragraphEnd = s.find('\n', paragraphStart);
        if (paragraphEnd == std::string::npos) paragraphEnd = s.size();

        std::string current;
        size_t i = paragraphStart;
        while (i < paragraphEnd) {
            while (i < paragraphEnd && s[i] == ' ') ++i;
            if (i == paragraphEnd) break;
            size_t wordEnd = s.find(' ', i);
            if (wordEnd == std::string::npos || wordEnd > paragraphEnd) wordEnd = paragraphEnd;
            const std::string word = s.substr(i, wordEnd - i);
            const std::string candidate = current.empty() ? word : current + " " + word;
            if (!current.empty() && textWidth(candidate, size) > w) {
                lines.push_back(current);
                current = word;
            } else {
                current = candidate;
            }
            i = wordEnd;
        }
        lines.push_back(current);
        if (paragraphEnd == s.size()) break;
        paragraphStart = paragraphEnd + 1;
    }
    return lines;
}

int textWrapped(const std::string& s, int x, int y, int w, float size, uint8_t color,
                int maxLines, int lineGap, bool centered) {
    if (s.empty() || w <= 0) return y;
    const int lh = lineHeight(size) + lineGap;
    std::vector<std::string> lines = wrapLines(s, w, size);

    size_t drawCount = lines.size();
    if (maxLines > 0 && drawCount > (size_t)maxLines) drawCount = (size_t)maxLines;
    for (size_t n = 0; n < drawCount; n++) {
        std::string ln = lines[n];
        const bool last = n + 1 == drawCount && drawCount < lines.size();
        if (last) {
            if (!lines[n + 1].empty()) ln += " " + lines[n + 1];
            ln = ellipsize(ln + " ...", w, size);
        }
        if (centered) textCentered(ln, x + w / 2, y, size, color);
        else text(ellipsize(ln, w, size), x, y, size, color);
        y += lh;
    }
    return y;
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

static inline void px(int x, int y, uint8_t c) { DisplayHAL::setPixel(x, y, c, framebuffer); }

static inline void span(int x0, int x1, int y, uint8_t c) {
    if (!framebuffer || y < 0 || y >= H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    if (x0 >= x1) return;
    if (x1 - x0 < 4) {
        for (int x = x0; x < x1; x++) px(x, y, c);
        return;
    }
    if (x0 & 1) {
        px(x0, y, c);
        x0++;
    }
    int xEnd = x1 & ~1;
    if (xEnd > x0) {
        uint8_t packed = (uint8_t)((c & 0x0F) | ((c & 0x0F) << 4));
        memset(framebuffer + (y * W + x0) / 2, packed, (xEnd - x0) / 2);
    }
    if (x1 & 1) {
        px(x1 - 1, y, c);
    }
}

void fillRect(int x, int y, int w, int h, uint8_t color) {
    for (int j = 0; j < h; j++) span(x, x + w, y + j, color);
}

// Horizontal inset of a rounded rect's edge on row `dy` (0 = top row).
static int cornerInset(int dy, int h, int r) {
    if (r <= 0) return 0;
    int d = -1;
    if (dy < r) d = r - dy;
    else if (dy >= h - r) d = dy - (h - r) + 1;
    if (d < 0) return 0;
    float yy = d - 0.5f;
    float dx = std::sqrt(std::max(0.0f, (float)(r * r) - yy * yy));
    return r - (int)(dx + 0.5f);
}

void fillRoundRect(int x, int y, int w, int h, int r, uint8_t color) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    for (int j = 0; j < h; j++) {
        int in = cornerInset(j, h, r);
        span(x + in, x + w - in, y + j, color);
    }
}

void strokeRoundRect(int x, int y, int w, int h, int r, int t, uint8_t color) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    int ir = r - t > 0 ? r - t : 0;
    for (int j = 0; j < h; j++) {
        int outer = cornerInset(j, h, r);
        int ij = j - t;
        if (ij < 0 || ij >= h - 2 * t) {
            span(x + outer, x + w - outer, y + j, color);
        } else {
            int inner = t + cornerInset(ij, h - 2 * t, ir);
            span(x + outer, x + inner, y + j, color);
            span(x + w - inner, x + w - outer, y + j, color);
        }
    }
}

void fillCircle(int cx, int cy, int r, uint8_t color) {
    if (r <= 0) {
        px(cx, cy, color);
        return;
    }
    int r2 = r * r;
    int dx = r;
    span(cx - r, cx + r + 1, cy, color);
    for (int dy = 1; dy <= r; dy++) {
        int dy2 = dy * dy;
        while (dx > 0 && dx * dx + dy2 > r2) dx--;
        span(cx - dx, cx + dx + 1, cy - dy, color);
        span(cx - dx, cx + dx + 1, cy + dy, color);
    }
}

void line(int x0, int y0, int x1, int y1, int t, uint8_t color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int r = t / 2;
    for (;;) {
        if (t <= 1) px(x0, y0, color);
        else fillCircle(x0, y0, r, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void hRule(int y, uint8_t color, int x0, int x1, int t) {
    fillRect(x0, y, x1 - x0, t, color);
}

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------

void icon(UIIconId id, int x, int y, int size, uint8_t ink) {
    if (id < 0 || id >= ICON_COUNT || size <= 0) return;
    const uint8_t* data = g_uiIcons[id];
    for (int oy = 0; oy < size; oy++) {
        int sy = oy * UI_ICON_SIZE / size;
        for (int ox = 0; ox < size; ox++) {
            int sx = ox * UI_ICON_SIZE / size;
            int idx = sy * UI_ICON_SIZE + sx;
            uint8_t v = (idx & 1) ? (data[idx >> 1] & 0x0F) : (data[idx >> 1] >> 4);
            if (v >= 15) continue;
            // Coverage blend toward `ink`.
            uint8_t bg = DisplayHAL::getPixel(x + ox, y + oy, framebuffer);
            int cover = 15 - v;
            uint8_t out = (uint8_t)((ink * cover + bg * (15 - cover)) / 15);
            px(x + ox, y + oy, out);
        }
    }
}

void chevronLeft(int cx, int cy, int size, uint8_t color) {
    int h = size / 2;
    line(cx + h / 2, cy - h, cx - h / 2, cy, 4, color);
    line(cx - h / 2, cy, cx + h / 2, cy + h, 4, color);
}

void chevronRight(int cx, int cy, int size, uint8_t color) {
    int h = size / 2;
    line(cx - h / 2, cy - h, cx + h / 2, cy, 3, color);
    line(cx + h / 2, cy, cx - h / 2, cy + h, 3, color);
}

void batteryIcon(int x, int y, int pct, bool charging, uint8_t color) {
    const int bw = 30, bh = 16;
    strokeRoundRect(x, y, bw, bh, 3, 2, color);
    fillRect(x + bw, y + 5, 3, bh - 10, color);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int fw = (bw - 6) * pct / 100;
    if (fw > 0) fillRect(x + 3, y + 3, fw, bh - 6, color);
    if (charging) {
        // Small lightning bolt knocked out of the fill.
        uint8_t k = pct > 40 ? WHITE : color;
        line(x + 16, y + 3, x + 12, y + 8, 2, k);
        line(x + 12, y + 8, x + 18, y + 8, 2, k);
        line(x + 18, y + 8, x + 14, y + 13, 2, k);
    }
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

void statusBar() {
    // Time (centre)
    time_t raw;
    time(&raw);
    struct tm* t = localtime(&raw);
    char timeStr[16];
    if (t && t->tm_year > 100) {
        int hr = t->tm_hour % 12;
        if (hr == 0) hr = 12;
        snprintf(timeStr, sizeof(timeStr), "%d:%02d %s", hr, t->tm_min, t->tm_hour < 12 ? "AM" : "PM");
    } else {
        snprintf(timeStr, sizeof(timeStr), "--:--");
    }
    textCentered(timeStr, W / 2, 4, FONT_STATUS, INK, true);

    // Battery (right)
    int pct = DisplayHAL::getBatteryPercent();
    bool charging = DisplayHAL::isCharging();
    batteryIcon(W - MARGIN - 33, 11, pct, charging, INK);
    textRight(std::to_string(pct) + "%", W - MARGIN - 40, 4, FONT_STATUS, INK);

    // Bluetooth (left)
    if (AppComm::isInitialized()) {
        bool connected = AppComm::isConnected();
        icon(ICON_BLUETOOTH, MARGIN - 6, 5, 26, connected ? BLACK : MID);
        if (connected) text("Connected", MARGIN + 22, 4, FONT_CAPTION, DARK);
    }
}

void header(const std::string& title, bool showBack, const std::string& action) {
    statusBar();
    int titleX = MARGIN;
    if (showBack) {
        chevronLeft(MARGIN + 10, 70, 30);
        titleX = MARGIN + 40;
    }
    int maxW = W - titleX - MARGIN;
    if (!action.empty()) {
        Rect a = headerActionRect(action);
        button(a, action, ButtonStyle::SECONDARY, FONT_SMALL);
        maxW = a.x - titleX - 12;
    }
    text(ellipsize(title, maxW, FONT_TITLE, true), titleX, 50, FONT_TITLE, BLACK, true);
    fillRect(0, HEADER_H - 2, W, 2, BLACK);
}

bool hitBack(int x, int y) {
    return x < BACK_HIT_W && y < HEADER_H;
}

Rect headerActionRect(const std::string& action) {
    int w = textWidth(action, FONT_SMALL) + 36;
    if (w < 90) w = 90;
    return Rect(W - MARGIN - w, 42, w, 52);
}

bool hitHeaderAction(int x, int y, const std::string& action) {
    if (action.empty()) return false;
    Rect r = headerActionRect(action);
    // Generous vertical hit area across the title row below the top-right status strip.
    return x >= r.x - 10 && y >= r.y && y < HEADER_H;
}

void pager(int page, int pageCount) {
    if (pageCount < 1) pageCount = 1;
    int top = H - FOOTER_H;
    fillRect(0, top, W, 2, BLACK);
    int cy = top + FOOTER_H / 2;
    bool canPrev = page > 0;
    bool canNext = page < pageCount - 1;
    chevronLeft(MARGIN + 14, cy, 26, canPrev ? BLACK : LIGHT);
    text("Prev", MARGIN + 34, cy - 14, FONT_SMALL + 2, canPrev ? INK : LIGHT);
    textRight("Next", W - MARGIN - 34, cy - 14, FONT_SMALL + 2, canNext ? INK : LIGHT);
    chevronRight(W - MARGIN - 14, cy, 26, canNext ? BLACK : LIGHT);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d of %d", page + 1, pageCount);
    textCentered(buf, W / 2, cy - 13, FONT_SMALL, DARK);
}

int hitPager(int x, int y, int page, int pageCount) {
    if (y < H - FOOTER_H) return 0;
    if (x < W / 3 && page > 0) return -1;
    if (x > W * 2 / 3 && page < pageCount - 1) return 1;
    return 0;
}

void footerActions(const std::vector<std::string>& labels) {
    int top = H - FOOTER_H;
    fillRect(0, top, W, 2, BLACK);
    int n = (int)labels.size();
    if (n == 0) return;
    int cw = W / n;
    for (int i = 0; i < n; i++) {
        int cx = i * cw + cw / 2;
        textCentered(labels[i], cx, top + FOOTER_H / 2 - 14, FONT_SMALL + 2, INK, true);
        if (i > 0) fillRect(i * cw, top + 18, 1, FOOTER_H - 36, LIGHT);
    }
}

int hitFooterAction(int x, int y, int count) {
    if (y < H - FOOTER_H || count <= 0) return -1;
    int i = x / (W / count);
    return i < count ? i : count - 1;
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

void button(const Rect& r, const std::string& label, ButtonStyle style, float size, bool enabled) {
    if (size < FONT_CAPTION) size = FONT_CAPTION;
    int radius = r.h / 2 < 14 ? r.h / 2 : 14;
    uint8_t fg = INK;
    switch (style) {
        case ButtonStyle::PRIMARY:
            fillRoundRect(r.x, r.y, r.w, r.h, radius, enabled ? BLACK : MID);
            fg = WHITE;
            break;
        case ButtonStyle::SECONDARY:
            fillRoundRect(r.x, r.y, r.w, r.h, radius, WHITE);
            strokeRoundRect(r.x, r.y, r.w, r.h, radius, 2, enabled ? BLACK : LIGHT);
            fg = enabled ? INK : MID;
            break;
        case ButtonStyle::SUBTLE:
            fillRoundRect(r.x, r.y, r.w, r.h, radius, PALE);
            fg = enabled ? INK : MID;
            break;
    }
    int lh = lineHeight(size);
    std::string l = ellipsize(label, r.w - 16, size, true);
    textCentered(l, r.x + r.w / 2, r.y + (r.h - lh) / 2, size, fg, true);
}

void toggle(int x, int y, bool on) {
    int r = TOGGLE_H / 2;
    if (on) {
        fillRoundRect(x, y, TOGGLE_W, TOGGLE_H, r, BLACK);
        fillCircle(x + TOGGLE_W - r, y + r, r - 6, WHITE);
    } else {
        fillRoundRect(x, y, TOGGLE_W, TOGGLE_H, r, WHITE);
        strokeRoundRect(x, y, TOGGLE_W, TOGGLE_H, r, 3, BLACK);
        fillCircle(x + r, y + r, r - 9, BLACK);
    }
}

void segmented(const Rect& r, const std::vector<std::string>& labels, int selected, float size) {
    if (size < FONT_CAPTION) size = FONT_CAPTION;
    int n = (int)labels.size();
    if (n == 0) return;
    int radius = 12;
    fillRoundRect(r.x, r.y, r.w, r.h, radius, WHITE);
    int lh = lineHeight(size);
    for (int i = 0; i < n; i++) {
        int sx = r.x + r.w * i / n;
        int ex = r.x + r.w * (i + 1) / n;
        if (i == selected) {
            if (i == 0 || i == n - 1) {
                // Keep the outer corners rounded.
                fillRoundRect(sx, r.y, ex - sx, r.h, radius, BLACK);
                if (i == 0 && n > 1) fillRect(ex - radius, r.y, radius, r.h, BLACK);
                if (i == n - 1 && n > 1) fillRect(sx, r.y, radius, r.h, BLACK);
            } else {
                fillRect(sx, r.y, ex - sx, r.h, BLACK);
            }
        } else if (i > 0 && i - 1 != selected) {
            fillRect(sx, r.y + 8, 2, r.h - 16, MID);
        }
        std::string l = ellipsize(labels[i], ex - sx - 8, size, i == selected);
        textCentered(l, (sx + ex) / 2, r.y + (r.h - lh) / 2, size, i == selected ? WHITE : INK, i == selected);
    }
    strokeRoundRect(r.x, r.y, r.w, r.h, radius, 2, BLACK);
}

int hitSegmented(const Rect& r, int count, int x, int y) {
    if (!r.contains(x, y) || count <= 0) return -1;
    int i = (x - r.x) * count / r.w;
    return i < count ? i : count - 1;
}

void progressBar(int x, int y, int w, int h, float fraction) {
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    fillRoundRect(x, y, w, h, h / 2, LIGHT);
    int fw = (int)(w * fraction);
    if (fw > 0) fillRoundRect(x, y, fw < h ? h : fw, h, h / 2, BLACK);
}

// ---------------------------------------------------------------------------
// Lists & sections
// ---------------------------------------------------------------------------

void sectionLabel(int y, const std::string& label) {
    std::string upper = label;
    for (char& c : upper) c = (char)toupper((unsigned char)c);
    text(upper, MARGIN, y, FONT_CAPTION, DARK, true);
}

void listRow(int y, const std::string& title, const std::string& subtitle,
             const std::string& trailing, bool chevron, float progress, UIIconId leading) {
    int x = MARGIN;
    if (leading < ICON_COUNT) {
        icon(leading, MARGIN, y + (ROW_H - 52) / 2, 52);
        x += 68;
    }
    int right = W - MARGIN;
    if (chevron) {
        chevronRight(right - 8, y + ROW_H / 2, 22, DARK);
        right -= 30;
    }
    if (!trailing.empty()) {
        int tw = textWidth(trailing, FONT_SMALL, true);
        textRight(trailing, right, y + (ROW_H - lineHeight(FONT_SMALL)) / 2, FONT_SMALL, INK, true);
        right -= tw + 16;
    }
    int maxW = right - x;
    bool hasSub = !subtitle.empty();
    int titleY = hasSub ? y + 16 : y + (ROW_H - lineHeight(FONT_BODY)) / 2;
    text(ellipsize(title, maxW, FONT_BODY), x, titleY, FONT_BODY, BLACK);
    if (hasSub) text(ellipsize(subtitle, maxW, FONT_SMALL), x, y + 52, FONT_SMALL, DARK);
    if (progress >= 0.0f) progressBar(x, y + ROW_H - 14, maxW, 5, progress);
    hRule(y + ROW_H - 1, LIGHT, MARGIN, W - MARGIN);
}

void settingLabel(int y, int h, const std::string& label, const std::string& helper) {
    int maxW = W - MARGIN * 2 - TOGGLE_W - 20;
    if (helper.empty()) {
        text(ellipsize(label, maxW, FONT_BODY), MARGIN, y + (h - lineHeight(FONT_BODY)) / 2, FONT_BODY, BLACK);
    } else {
        const int helperLine = lineHeight(FONT_CAPTION);
        const int helperLines = std::max(1, (h - lineHeight(FONT_BODY) - 4) / helperLine);
        const int helperHeight = helperLines * helperLine;
        int lh = lineHeight(FONT_BODY) + 4 + helperHeight;
        int ty = y + (h - lh) / 2;
        text(ellipsize(label, maxW, FONT_BODY), MARGIN, ty, FONT_BODY, BLACK);
        textWrapped(helper, MARGIN, ty + lineHeight(FONT_BODY) + 4, maxW, FONT_CAPTION, DARK, helperLines, 0);
    }
}

void emptyState(int cy, const std::string& title, const std::string& message) {
    textCentered(title, W / 2, cy - 40, FONT_BODY + 2, BLACK, true);
    textWrapped(message, MARGIN * 2, cy + 6, W - MARGIN * 4, FONT_SMALL, DARK, 4, 6, true);
}

int rowsPerPage(bool hasFooter, int extraTop) {
    int avail = H - HEADER_H - extraTop - (hasFooter ? FOOTER_H : 0);
    return avail / ROW_H;
}

} // namespace UI
