#pragma once

// Plain C++ rules and generators for the puzzle games. No drawing and no
// Arduino dependencies, so tools/test_game_logic.cpp can exercise them on
// the host.

#include <cstdint>

namespace Games {

class Random {
public:
    explicit Random(uint32_t seed) : state(seed ? seed : 1) {}
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int below(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
    template <typename T>
    void shuffle(T* a, int n) {
        for (int i = n - 1; i > 0; --i) {
            int j = below(i + 1);
            T t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
    }

private:
    uint32_t state;
};

// ---------------------------------------------------------------------------
// Word Search
// ---------------------------------------------------------------------------

struct WordTheme {
    const char* name;
    const char* const* words;  // upper case A-Z, 3-10 letters
    int count;
};

int wordThemeCount();
const WordTheme& wordTheme(int index);

// The eight directions, as (row, column) steps: E, S, SE, NE, W, N, NW, SW.
extern const int8_t kWordDirs[8][2];

struct WordSearch {
    static const int MAX_SIDE = 12;
    static const int MAX_WORDS = 16;
    static const int MAX_LEN = 10;

    struct Word {
        uint8_t bank;    // index into the theme's word list
        uint8_t len;
        int8_t row, col; // first letter of the line where it sits (or was found)
        int8_t dr, dc;
        bool found;
    };

    int side = 0;
    int theme = 0;
    int count = 0;
    char cells[MAX_SIDE * MAX_SIDE] = {0};
    Word words[MAX_WORDS] = {};

    // Places `wordCount` words from `theme` into a side x side grid using
    // the directions in `dirMask` (bit i = kWordDirs[i]), then fills the
    // rest with letters drawn from the hidden words. Deterministic for a
    // given seed. Returns false only if the theme cannot fill the grid.
    bool generate(int theme, int side, int wordCount, uint8_t dirMask, uint32_t seed);

    const char* text(int word) const;

    // True if a and b (cell indices) lie on one row, column or diagonal.
    bool straight(int a, int b, int& dr, int& dc, int& len) const;

    // The player tapped first and last letters a and b. If the letters
    // between them spell a word still to be found (in either direction) it
    // is marked found and its index returned; -2 if they spell a word
    // already found, -1 otherwise.
    int select(int a, int b);

    int foundCount() const;
    bool won() const { return count > 0 && foundCount() == count; }
};

// ---------------------------------------------------------------------------
// Number Place (Sudoku)
// ---------------------------------------------------------------------------

struct Sudoku {
    enum Level { GENTLE = 0, MODERATE, TOUGH, LEVEL_COUNT };
    // Hardest technique the logical solver needed.
    enum Grade { NAKED_SINGLES = 0, HIDDEN_SINGLES, LOCKED_CANDIDATES, SUBSETS, BEYOND };

    uint8_t solution[81] = {0};
    uint8_t givens[81] = {0};  // 0 = blank

    // Counts solutions up to `limit` (backtracking with bitmasks).
    static int countSolutions(const uint8_t* grid, int limit, uint8_t* firstSolution = nullptr);
    // Solves like a person would (singles, locked candidates, naked and
    // hidden pairs/triples) and reports the hardest step needed, or BEYOND.
    static Grade grade(const uint8_t* puzzle);
    static int clueCount(const uint8_t* grid);

    // Generates a puzzle with a unique solution graded for `level`.
    void generate(int level, uint32_t seed);
};

// ---------------------------------------------------------------------------
// Crevasse field (Minesweeper)
// ---------------------------------------------------------------------------

struct Minefield {
    static const int MAX_W = 9;
    static const int MAX_H = 13;
    static const int MAX_CELLS = MAX_W * MAX_H;
    enum State : uint8_t { HIDDEN = 0, OPEN = 1, FLAGGED = 2 };

    int w = 0, h = 0, mines = 0;
    bool laid = false;       // mines are laid on the first probe
    bool lost = false;
    int exploded = -1;
    bool mine[MAX_CELLS] = {false};
    uint8_t adj[MAX_CELLS] = {0};
    uint8_t state[MAX_CELLS] = {0};

    void setup(int width, int height, int mineCount);
    // Lays the mines, keeping `safe` and its neighbours clear. With
    // `noGuess`, retries until the field can be cleared by deduction alone
    // from that first probe (falls back to the last layout tried).
    // Returns true if the layout is guaranteed guess-free.
    bool lay(int safe, uint32_t seed, bool noGuess, int attempts = 400);
    // Opens a cell (flood-filling blanks). Returns cells opened, -1 on a mine.
    int probe(int cell);
    // On an open number whose flags are all placed, opens the rest of its
    // neighbours. Returns cells opened, -1 on a mine, 0 if not applicable.
    int chord(int cell);
    bool toggleFlag(int cell);
    int flags() const;
    int neighbours(int cell, int* out) const;
    bool won() const;
    // Logic solver used by lay(): can every safe cell be opened from the
    // current position without guessing?
    bool solvable() const;
};

// ---------------------------------------------------------------------------
// Klondike patience
// ---------------------------------------------------------------------------

struct Klondike {
    // card = suit * 13 + rank, rank 0 = ace ... 12 = king.
    // Suits: 0 spades, 1 hearts, 2 diamonds, 3 clubs (hearts and diamonds red).
    static const uint8_t NONE = 0xFF;
    static const int MAX_COL = 19;
    static int rank(uint8_t c) { return c % 13; }
    static int suit(uint8_t c) { return c / 13; }
    static bool red(uint8_t c) { return suit(c) == 1 || suit(c) == 2; }

    uint8_t stock[52];
    int stockN = 0;
    uint8_t waste[52];
    int wasteN = 0;
    int foundation[4] = {0, 0, 0, 0};  // cards on each suit's pile
    uint8_t tab[7][MAX_COL];
    int tabN[7] = {0};
    int down[7] = {0};                 // face-down cards at the bottom of each column
    int draw = 1;
    int passLimit = 0;                 // passes through the stock; 0 = unlimited
    int passes = 1;
    int moves = 0;

    void deal(uint32_t seed, int drawCount, int passLimit);

    // Stock: turns over 1 or 3 cards, or turns the waste back over.
    bool turnStock();
    bool canRecycle() const { return stockN == 0 && wasteN > 0 && (passLimit == 0 || passes < passLimit); }

    bool fitsTableau(uint8_t card, int col) const;
    bool fitsFoundation(uint8_t card) const;

    // Destinations: 0..6 tableau columns, 7..10 foundations (by suit).
    bool moveFromTableau(int col, int index, int dest);
    bool moveFromWaste(int dest);
    bool moveFromFoundation(int s, int col);

    // Tap-to-move: the best legal destination for a tapped card (foundation
    // first for a single card, otherwise the next column to the right that
    // takes it, so repeated taps cycle between alternatives). -1 if none.
    int bestTableauDest(int col, int index) const;
    int bestWasteDest() const;
    int bestFoundationDest(int s) const;

    // True if any card can still go somewhere useful (including cards
    // waiting in the stock).
    bool hasUsefulMove() const;
    // Everything face up and the stock exhausted: the rest is a formality.
    bool canCollect() const;
    // Moves one card to the foundations (for Collect). False when done.
    bool collectOne();
    bool won() const { return foundation[0] + foundation[1] + foundation[2] + foundation[3] == 52; }
    int foundationCards() const { return foundation[0] + foundation[1] + foundation[2] + foundation[3]; }

private:
    void flipTop(int col);
};

} // namespace Games
