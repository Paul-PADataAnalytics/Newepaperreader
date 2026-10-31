#include "GameLogic.h"
#include <algorithm>
#include <cstdlib>
#include <string>

namespace Games {

bool MiniSudoku::legal(int cell, int value) const {
    if (cell < 0 || cell >= 16 || value < 0 || value > 4) return false;
    if (value == 0) return true;
    int row = cell / 4, col = cell % 4;
    for (int i = 0; i < 16; ++i) {
        if (i == cell || cells[i] != value) continue;
        if (i / 4 == row || i % 4 == col ||
            (i / 8 == row / 2 && (i % 4) / 2 == col / 2)) return false;
    }
    return true;
}

bool MiniSudoku::place(int cell, int value) {
    if (cell < 0 || cell >= 16 || fixed[cell] || !legal(cell, value)) return false;
    cells[cell] = value;
    return true;
}

static int countSolutions(MiniSudoku& board) {
    int empty = -1;
    for (int i = 0; i < 16; ++i) {
        if (!board.legal(i, board.cells[i])) return 0;
        if (board.cells[i] == 0) empty = i;
    }
    if (empty < 0) return 1;
    int count = 0;
    for (int value = 1; value <= 4 && count < 2; ++value) {
        if (!board.legal(empty, value)) continue;
        board.cells[empty] = value;
        count += countSolutions(board);
        board.cells[empty] = 0;
    }
    return std::min(count, 2);
}

int MiniSudoku::solutions() const {
    MiniSudoku copy = *this;
    return countSolutions(copy);
}

void MiniSudoku::reset(uint32_t seed) {
    Random rng(seed);
    std::array<int, 4> digits{{1, 2, 3, 4}};
    for (int i = 3; i > 0; --i) std::swap(digits[i], digits[rng.below(i + 1)]);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) cells[r * 4 + c] = digits[(r * 2 + r / 2 + c) % 4];
    std::array<int, 16> order{};
    for (int i = 0; i < 16; ++i) order[i] = i;
    for (int i = 15; i > 0; --i) std::swap(order[i], order[rng.below(i + 1)]);
    for (int i : order) {
        int saved = cells[i];
        cells[i] = 0;
        if (solutions() != 1) cells[i] = saved;
    }
    for (int i = 0; i < 16; ++i) fixed[i] = cells[i] != 0;
}

bool MiniSudoku::won() const {
    for (int i = 0; i < 16; ++i) if (!cells[i] || !legal(i, cells[i])) return false;
    return true;
}

bool LightsOut::toggle(int cell) {
    if (cell < 0 || cell >= 25) return false;
    int row = cell / 5, col = cell % 5;
    cells[cell] = !cells[cell];
    if (row > 0) cells[cell - 5] = !cells[cell - 5];
    if (row < 4) cells[cell + 5] = !cells[cell + 5];
    if (col > 0) cells[cell - 1] = !cells[cell - 1];
    if (col < 4) cells[cell + 1] = !cells[cell + 1];
    return true;
}

void LightsOut::reset(uint32_t seed) {
    cells.fill(false);
    Random rng(seed);
    for (int i = 0; i < 25; ++i) if (rng.below(2)) toggle(i);
    if (won()) toggle(12);
}

bool LightsOut::won() const {
    return std::none_of(cells.begin(), cells.end(), [](bool on) { return on; });
}

bool SlidingPuzzle::move(int cell) {
    if (cell < 0 || cell >= 9) return false;
    int blank = static_cast<int>(std::find(cells.begin(), cells.end(), 0) - cells.begin());
    if (blank >= 9 || std::abs(blank / 3 - cell / 3) + std::abs(blank % 3 - cell % 3) != 1) return false;
    std::swap(cells[blank], cells[cell]);
    return true;
}

void SlidingPuzzle::reset(uint32_t seed) {
    for (int i = 0; i < 9; ++i) cells[i] = (i + 1) % 9;
    Random rng(seed);
    int previousBlank = -1;
    for (int step = 0; step < 80; ++step) {
        int blank = static_cast<int>(std::find(cells.begin(), cells.end(), 0) - cells.begin());
        std::array<int, 4> options{};
        int count = 0;
        for (int i = 0; i < 9; ++i)
            if (i != previousBlank &&
                std::abs(blank / 3 - i / 3) + std::abs(blank % 3 - i % 3) == 1)
                options[count++] = i;
        move(options[rng.below(count)]);
        previousBlank = blank;
    }
    if (won()) move(7);
}

bool SlidingPuzzle::won() const {
    for (int i = 0; i < 9; ++i) if (cells[i] != (i + 1) % 9) return false;
    return true;
}

const std::array<const char*, 6> WordSearch::words{{"PAPER", "BOOK", "INK", "READ", "PAGE", "STORY"}};

void WordSearch::reset(uint32_t seed) {
    Random rng(seed);
    for (char& c : cells) c = 'A' + rng.below(26);
    found.fill(false);
    marked.fill(false);
    const int slots[6][4] = {{0, 0, 1, 0}, {7, 1, -1, 1}, {0, 7, 1, -1},
                             {0, 2, 0, 1}, {2, 1, 1, 0}, {2, 7, 1, 0}};
    int symmetry = rng.below(8);
    std::array<int, 6> order{{0, 1, 2, 3, 4, 5}};
    if (rng.below(2)) std::swap(order[0], order[5]);
    if (rng.below(2)) std::swap(order[1], order[3]);
    if (rng.below(2)) std::swap(order[3], order[4]);
    for (int slot = 0; slot < 6; ++slot) {
        std::string word(words[order[slot]]);
        for (size_t i = 0; i < word.size(); ++i) {
            int x = slots[slot][0] + static_cast<int>(i) * slots[slot][2];
            int y = slots[slot][1] + static_cast<int>(i) * slots[slot][3];
            if (symmetry & 4) x = 7 - x;
            for (int rotation = 0; rotation < (symmetry & 3); ++rotation) {
                int oldX = x;
                x = 7 - y;
                y = oldX;
            }
            cells[y * 8 + x] = word[i];
        }
    }
}

int WordSearch::select(int first, int last) {
    if (first < 0 || first >= 64 || last < 0 || last >= 64 || first == last) return -1;
    int dx = last % 8 - first % 8, dy = last / 8 - first / 8;
    if (dx && dy && std::abs(dx) != std::abs(dy)) return -1;
    int length = std::max(std::abs(dx), std::abs(dy)) + 1;
    dx = (dx > 0) - (dx < 0);
    dy = (dy > 0) - (dy < 0);
    std::string candidate;
    for (int i = 0; i < length; ++i) candidate += cells[first + i * (dy * 8 + dx)];
    std::string reverse(candidate.rbegin(), candidate.rend());
    for (int i = 0; i < 6; ++i) {
        if (candidate != words[i] && reverse != words[i]) continue;
        found[i] = true;
        for (int j = 0; j < length; ++j) marked[first + j * (dy * 8 + dx)] = true;
        return i;
    }
    return -1;
}

bool WordSearch::won() const {
    return std::all_of(found.begin(), found.end(), [](bool yes) { return yes; });
}

} // namespace Games
