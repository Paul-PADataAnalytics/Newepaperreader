#pragma once

// Portrait UI kit: shared look & feel for every screen (540 x 960).
//
// Layout of a standard screen:
//
//   y=0    +----------------------------------+
//          | BT       9:41            85% [=] |  status strip (STATUS_H)
//   y=36   +----------------------------------+
//          | <  Title                 [Action]|  title row
//   y=104  +==================================+  2px rule (HEADER_H)
//          |                                  |
//          |   content                        |
//          |                                  |
//   H-88   +----------------------------------+
//          |  < Prev     Page 1 of 3   Next > |  optional footer (FOOTER_H)
//   y=960  +----------------------------------+
//
// All colours are 4-bit greys (0 = black ... 15 = white). Every draw call
// renders into the global framebuffer; wrap them in
// UIFramework::performFullScreenDraw / performFastPartialUpdate to flush.

#include <cstdint>
#include <string>
#include <vector>
#include "ui_icons.h"

namespace UI {

// Palette
constexpr uint8_t BLACK = 0x0;
constexpr uint8_t INK   = 0x2;  // body text on white
constexpr uint8_t DARK  = 0x5;  // secondary text
constexpr uint8_t MID   = 0x9;  // disabled text, faint rules
constexpr uint8_t LIGHT = 0xC;  // separators, tracks
constexpr uint8_t PALE  = 0xE;  // subtle fills
constexpr uint8_t WHITE = 0xF;

// Metrics
constexpr int W = 540;
constexpr int H = 960;
constexpr int MARGIN = 24;
constexpr int STATUS_H = 36;
constexpr int HEADER_H = 104;
constexpr int FOOTER_H = 88;
constexpr int ROW_H = 96;
constexpr int BACK_HIT_W = 110;

// Type scale
constexpr float FONT_STATUS = 24.0f;
constexpr float FONT_TITLE = 34.0f;
constexpr float FONT_BODY = 28.0f;
constexpr float FONT_SMALL = 24.0f;
constexpr float FONT_CAPTION = 24.0f;

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    Rect() = default;
    Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class ButtonStyle { PRIMARY, SECONDARY, SUBTLE };

// ---- Text --------------------------------------------------------------
int textWidth(const std::string& s, float size, bool bold = false);
int lineHeight(float size);
void text(const std::string& s, int x, int y, float size, uint8_t color = INK, bool bold = false);
void textCentered(const std::string& s, int cx, int y, float size, uint8_t color = INK, bool bold = false);
void textRight(const std::string& s, int rightX, int y, float size, uint8_t color = INK, bool bold = false);
std::string ellipsize(const std::string& s, int maxW, float size, bool bold = false);
// Word-wraps into lines no wider than w ('\n' starts a new line).
std::vector<std::string> wrapLines(const std::string& s, int w, float size);
// Word-wraps into a column; returns the y just below the last line drawn.
int textWrapped(const std::string& s, int x, int y, int w, float size, uint8_t color = INK,
                int maxLines = 0, int lineGap = 6, bool centered = false);

// ---- Shapes ------------------------------------------------------------
void fillRect(int x, int y, int w, int h, uint8_t color);
void fillRoundRect(int x, int y, int w, int h, int r, uint8_t color);
void strokeRoundRect(int x, int y, int w, int h, int r, int t, uint8_t color);
void fillCircle(int cx, int cy, int r, uint8_t color);
void line(int x0, int y0, int x1, int y1, int t, uint8_t color);
void hRule(int y, uint8_t color = LIGHT, int x0 = MARGIN, int x1 = W - MARGIN, int t = 1);

// ---- Icons -------------------------------------------------------------
// Draws a generated line icon scaled to `size` px (multiply-blended so it
// works on any background).
void icon(UIIconId id, int x, int y, int size, uint8_t ink = BLACK);
void chevronLeft(int cx, int cy, int size, uint8_t color = BLACK);
void chevronRight(int cx, int cy, int size, uint8_t color = BLACK);
void batteryIcon(int x, int y, int pct, bool charging, uint8_t color = BLACK);

// ---- Chrome ------------------------------------------------------------
void statusBar();
// Status strip + title row + rule. `action` (optional) draws an outlined
// text button at the right of the title row.
void header(const std::string& title, bool showBack, const std::string& action = "");
bool hitBack(int x, int y);
bool hitHeaderAction(int x, int y, const std::string& action);
Rect headerActionRect(const std::string& action);

// Bottom pager: "< Prev   Page n of m   Next >". Returns -1/+1 on hit, else 0.
void pager(int page, int pageCount);
int hitPager(int x, int y, int page, int pageCount);

// Bottom bar of equal-width actions.
void footerActions(const std::vector<std::string>& labels);
int hitFooterAction(int x, int y, int count);

// ---- Controls ----------------------------------------------------------
void button(const Rect& r, const std::string& label, ButtonStyle style = ButtonStyle::SECONDARY,
            float size = FONT_SMALL + 2, bool enabled = true);
void toggle(int x, int y, bool on);
constexpr int TOGGLE_W = 88;
constexpr int TOGGLE_H = 48;
void segmented(const Rect& r, const std::vector<std::string>& labels, int selected, float size = FONT_SMALL);
int hitSegmented(const Rect& r, int count, int x, int y);
void progressBar(int x, int y, int w, int h, float fraction);

// ---- Lists & sections --------------------------------------------------
void sectionLabel(int y, const std::string& label);
// One list row of height ROW_H at `y`. `progress` < 0 hides the bar.
void listRow(int y, const std::string& title, const std::string& subtitle,
             const std::string& trailing = "", bool chevron = false, float progress = -1.0f,
             UIIconId leading = ICON_COUNT);
// A label + optional helper text on the left of a settings row; the
// control is drawn by the caller on the right.
void settingLabel(int y, int h, const std::string& label, const std::string& helper = "");
void emptyState(int cy, const std::string& title, const std::string& message);

// Number of list rows that fit between the header and an optional footer.
int rowsPerPage(bool hasFooter = true, int extraTop = 0);

} // namespace UI
