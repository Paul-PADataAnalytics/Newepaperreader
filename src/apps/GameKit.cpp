#include "GameKit.h"
#include "reader/AppStorage.h"
#include <chrono>
#include <ctime>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#endif

namespace GameKit {
using namespace UI;

void sectionLabel(int y, const std::string& label) { text(label, MARGIN, y, SMALL, DARK, true); }

namespace {
const int PAGER_BTN_W = 118;
Rect previousRect() { return Rect(MARGIN, H - FOOTER_H + 12, PAGER_BTN_W, 64); }
Rect nextRect() { return Rect(W - MARGIN - PAGER_BTN_W, H - FOOTER_H + 12, PAGER_BTN_W, 64); }
} // namespace

void pageFooter(int page, int pages) {
    fillRect(0, H - FOOTER_H, W, 2, BLACK);
    button(previousRect(), "Previous", ButtonStyle::SECONDARY, SMALL, page > 0);
    button(nextRect(), "Next", ButtonStyle::SECONDARY, SMALL, page + 1 < pages);
    textCentered("Page " + std::to_string(page + 1) + " of " + std::to_string(pages), W / 2,
                 H - FOOTER_H + 27, SMALL, DARK);
}

int hitPageFooter(int x, int y, int page, int pages) {
    Rect p = previousRect(), n = nextRect();
    if (y < H - FOOTER_H) return 0;
    if (x >= p.x - 12 && x < p.x + p.w + 30 && page > 0) return -1;
    if (x >= n.x - 30 && x < n.x + n.w + 12 && page + 1 < pages) return 1;
    return 0;
}

void optionButton(const Rect& r, const std::string& title, const std::string& detail, bool primary, bool enabled) {
    if (primary) {
        fillRoundRect(r.x, r.y, r.w, r.h, 14, enabled ? BLACK : MID);
    } else {
        fillRoundRect(r.x, r.y, r.w, r.h, 14, WHITE);
        strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, enabled ? BLACK : LIGHT);
    }
    uint8_t fg = primary ? WHITE : (enabled ? BLACK : MID);
    uint8_t sub = primary ? LIGHT : (enabled ? DARK : MID);
    int lh1 = lineHeight(FONT_BODY);
    int lh2 = detail.empty() ? 0 : lineHeight(SMALL) + 6;
    int y = r.y + (r.h - lh1 - lh2) / 2;
    text(ellipsize(title, r.w - 40, FONT_BODY, true), r.x + 22, y, FONT_BODY, fg, true);
    if (!detail.empty()) text(ellipsize(detail, r.w - 40, SMALL), r.x + 22, y + lh1 + 6, SMALL, sub);
}

void letterTile(const Rect& r, const std::string& s, bool dark, float size) {
    int radius = r.h / 5;
    if (dark) {
        fillRoundRect(r.x, r.y, r.w, r.h, radius, BLACK);
    } else {
        fillRoundRect(r.x, r.y, r.w, r.h, radius, WHITE);
        strokeRoundRect(r.x, r.y, r.w, r.h, radius, 2, BLACK);
    }
    textCentered(s, r.x + r.w / 2, r.y + (r.h - lineHeight(size)) / 2, size, dark ? WHITE : BLACK, true);
}

void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) {
    int minY = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxY = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    const int xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    for (int y = minY; y <= maxY; y++) {
        float lo = 1e9f, hi = -1e9f;
        for (int e = 0; e < 3; e++) {
            int ax = xs[e], ay = ys[e], bx = xs[(e + 1) % 3], by = ys[(e + 1) % 3];
            if (ay == by) {
                if (y == ay) {
                    if (ax < lo) lo = (float)ax;
                    if (bx < lo) lo = (float)bx;
                    if (ax > hi) hi = (float)ax;
                    if (bx > hi) hi = (float)bx;
                }
                continue;
            }
            if ((y < ay && y < by) || (y > ay && y > by)) continue;
            float x = ax + (float)(y - ay) * (bx - ax) / (float)(by - ay);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
        if (hi >= lo) fillRect((int)(lo + 0.5f), y, (int)(hi + 0.5f) - (int)(lo + 0.5f) + 1, 1, color);
    }
}

void tick(int x, int y, int size, uint8_t color) {
    int t = size / 6 < 3 ? 3 : size / 6;
    line(x, y + size / 2, x + size / 3, y + size - t / 2, t, color);
    line(x + size / 3, y + size - t / 2, x + size, y, t, color);
}

void suitShape(int s, int cx, int cy, int size, uint8_t color) {
    float z = (float)size;
    auto P = [&](float f) { return (int)(f * z + (f < 0 ? -0.5f : 0.5f)); };
    switch (s) {
        case 1:  // heart
            fillCircle(cx - P(0.24f), cy - P(0.16f), P(0.27f), color);
            fillCircle(cx + P(0.24f), cy - P(0.16f), P(0.27f), color);
            fillTriangle(cx - P(0.5f), cy - P(0.08f), cx + P(0.5f), cy - P(0.08f), cx, cy + P(0.5f), color);
            break;
        case 2:  // diamond
            fillTriangle(cx, cy - P(0.5f), cx - P(0.38f), cy, cx + P(0.38f), cy, color);
            fillTriangle(cx, cy + P(0.5f), cx - P(0.38f), cy, cx + P(0.38f), cy, color);
            break;
        case 0:  // spade
            fillCircle(cx - P(0.24f), cy + P(0.08f), P(0.25f), color);
            fillCircle(cx + P(0.24f), cy + P(0.08f), P(0.25f), color);
            fillTriangle(cx - P(0.49f), cy + P(0.02f), cx + P(0.49f), cy + P(0.02f), cx, cy - P(0.5f), color);
            fillTriangle(cx, cy + P(0.1f), cx - P(0.18f), cy + P(0.5f), cx + P(0.18f), cy + P(0.5f), color);
            break;
        default:  // club
            fillCircle(cx, cy - P(0.24f), P(0.22f), color);
            fillCircle(cx - P(0.25f), cy + P(0.08f), P(0.22f), color);
            fillCircle(cx + P(0.25f), cy + P(0.08f), P(0.22f), color);
            fillCircle(cx, cy, P(0.12f), color);
            fillTriangle(cx, cy, cx - P(0.18f), cy + P(0.5f), cx + P(0.18f), cy + P(0.5f), color);
            break;
    }
}

void suit(int s, int cx, int cy, int size) {
    if (s == 1 || s == 2) {
        suitShape(s, cx, cy, size, BLACK);
        int inset = size >= 30 ? 6 : 5;
        suitShape(s, cx, cy, size - inset, LIGHT);
    } else {
        suitShape(s, cx, cy, size, BLACK);
    }
}

void pennant(int x, int y, int size, uint8_t color) {
    int pole = size / 14 < 2 ? 2 : size / 14;
    fillRect(x, y - size, pole, size, color);
    fillTriangle(x + pole, y - size, x + pole, y - size + size * 11 / 20, x + pole + size * 3 / 5,
                 y - size + size * 11 / 40, color);
    fillRect(x - size / 5, y - pole, size * 2 / 5 + pole, pole, color);
}

uint32_t nowMs() {
#ifndef NATIVE_TESTING
    return millis();
#else
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

uint32_t randomSeed() {
#ifndef NATIVE_TESTING
    return esp_random() | 1;
#else
    return ((uint32_t)time(nullptr) * 2654435761u) ^ nowMs() ^ 0x5bd1e995u;
#endif
}

std::string clockText(int seconds) {
    if (seconds < 0) seconds = 0;
    char buf[24];
    int h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h > 0) snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

FILE* openData(const char* name, const char* mode) {
    std::string path = AppStorage::toVfsPath(AppStorage::toRuntimePath(std::string("/data/") + name));
    return fopen(path.c_str(), mode);
}

void PlayClock::start() {
    lastMs = nowMs();
    running = true;
}

void PlayClock::tick() {
    if (!running) return;
    uint32_t now = nowMs();
    uint32_t gap = now - lastMs;
    if (gap > MAX_GAP_MS) gap = MAX_GAP_MS;
    carryMs += gap;
    seconds += carryMs / 1000;
    carryMs %= 1000;
    lastMs = now;
}

void PlayClock::stop() {
    tick();
    running = false;
}

} // namespace GameKit
