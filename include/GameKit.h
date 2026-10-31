#pragma once

// Shared look & feel and small utilities for the puzzle games (Word Search,
// Number Place, Crevasse, Klondike). Builds on the portrait UI kit.

#include "ui/UIKit.h"
#include <cstdint>
#include <cstdio>
#include <string>

namespace GameKit {

using UI::Rect;

constexpr float SMALL = UI::FONT_BODY - 2;  // 26 px: captions and secondary lines
constexpr float GRID_FONT = 30.0f;          // letters and numbers in a playing grid

void sectionLabel(int y, const std::string& label);

// "Previous   Page n of m   Next" along the bottom (as in the story games).
void pageFooter(int page, int pages);
// Returns -1 / +1 when Previous / Next was hit (and is enabled), else 0.
int hitPageFooter(int x, int y, int page, int pages);

// Two-line option button: bold title + caption.
void optionButton(const Rect& r, const std::string& title, const std::string& detail,
                  bool primary = false, bool enabled = true);

// Small emblem tile with a letter or number, as in the Codebreaker title.
void letterTile(const Rect& r, const std::string& s, bool dark, float size);

// Solid triangle (for flags, pointers and card suits).
void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color);

// Playing-card suit (0 spades, 1 hearts, 2 diamonds, 3 clubs) centred on
// (cx, cy), `size` px tall. Black suits are solid; red suits are drawn
// outlined with a grey fill so the two colours stay distinct on e-ink.
void suit(int s, int cx, int cy, int size);
void suitShape(int s, int cx, int cy, int size, uint8_t color);

// A marker pennant on a pole, its foot at (x, y), `size` px tall.
void pennant(int x, int y, int size, uint8_t color = UI::BLACK);

// A tick mark, for completed items.
void tick(int x, int y, int size, uint8_t color = UI::BLACK);

uint32_t nowMs();
uint32_t randomSeed();
std::string clockText(int seconds);        // "4:07", "1:02:03"
std::string plural(int n, const char* one, const char* many);

// Opens /data/<name> on the SD card (or data/data/<name> in the simulator).
FILE* openData(const char* name, const char* mode);

// Play time that only runs while a board is being played: each interaction
// adds the time since the previous one, capped so that a puzzle left open
// on the table does not count the hours it sat there.
struct PlayClock {
    uint32_t seconds = 0;
    uint32_t lastMs = 0;
    uint32_t carryMs = 0;
    bool running = false;
    static const uint32_t MAX_GAP_MS = 120000;
    void start();
    void tick();
    void stop();
};

} // namespace GameKit
