#include "GameLogic.h"
#include <cstring>

namespace Games {

// ===========================================================================
// Word Search
// ===========================================================================

const int8_t kWordDirs[8][2] = {{0, 1}, {1, 0}, {1, 1}, {-1, 1}, {0, -1}, {-1, 0}, {-1, -1}, {1, -1}};

namespace {

int iabs(int v) { return v < 0 ? -v : v; }
int isign(int v) { return (v > 0) - (v < 0); }

bool contains(const char* hay, const char* needle) { return std::strstr(hay, needle) != nullptr; }

struct Placement {
    int row, col, dir, overlap;
};

} // namespace

const char* WordSearch::text(int word) const { return wordTheme(theme).words[words[word].bank]; }

bool WordSearch::generate(int th, int sd, int wordCount, uint8_t dirMask, uint32_t seed) {
    if (th < 0 || th >= wordThemeCount()) th = 0;
    if (sd < 5) sd = 5;
    if (sd > MAX_SIDE) sd = MAX_SIDE;
    if (wordCount > MAX_WORDS) wordCount = MAX_WORDS;
    theme = th;
    side = sd;
    Random rng(seed);
    const WordTheme& T = wordTheme(th);

    int dirs[8], nd = 0;
    for (int i = 0; i < 8; i++)
        if (dirMask & (1 << i)) dirs[nd++] = i;
    if (nd == 0) dirs[nd++] = 0;

    static const int MAX_BANK = 96;
    uint8_t pool[MAX_BANK];
    int poolN = 0;
    for (int i = 0; i < T.count && poolN < MAX_BANK; i++) {
        int len = (int)std::strlen(T.words[i]);
        if (len >= 3 && len <= sd && len <= MAX_LEN) pool[poolN++] = (uint8_t)i;
    }

    // Placements are gathered into a buffer this big at most.
    static Placement cand[MAX_SIDE * MAX_SIDE * 8];
    bool ok = false;
    for (int attempt = 0; attempt < 40 && !ok; attempt++) {
        std::memset(cells, 0, sizeof(cells));
        count = 0;
        int dirUse[8] = {0};
        rng.shuffle(pool, poolN);

        // Take the first wordCount compatible words, place the longest first.
        uint8_t chosen[MAX_WORDS];
        int chosenN = 0, next = 0;
        auto compatible = [&](int bank) {
            const char* w = T.words[bank];
            for (int i = 0; i < count; i++) {
                const char* o = T.words[words[i].bank];
                if (contains(o, w) || contains(w, o)) return false;
            }
            for (int i = 0; i < chosenN; i++) {
                const char* o = T.words[chosen[i]];
                if (contains(o, w) || contains(w, o)) return false;
            }
            return true;
        };
        while (chosenN < wordCount && next < poolN) {
            if (compatible(pool[next])) chosen[chosenN++] = pool[next];
            next++;
        }
        for (int i = 1; i < chosenN; i++) {
            uint8_t v = chosen[i];
            int j = i - 1;
            while (j >= 0 && std::strlen(T.words[chosen[j]]) < std::strlen(T.words[v])) {
                chosen[j + 1] = chosen[j];
                j--;
            }
            chosen[j + 1] = v;
        }

        int ci = 0;
        while (count < wordCount) {
            int bank;
            if (ci < chosenN) {
                bank = chosen[ci++];
            } else {
                // A word would not fit: try a replacement from the pool.
                while (next < poolN && !compatible(pool[next])) next++;
                if (next >= poolN) break;
                bank = pool[next++];
            }
            const char* w = T.words[bank];
            int len = (int)std::strlen(w);
            int n = 0;
            for (int di = 0; di < nd; di++) {
                int dr = kWordDirs[dirs[di]][0], dc = kWordDirs[dirs[di]][1];
                for (int r = 0; r < sd; r++) {
                    int er = r + dr * (len - 1);
                    if (er < 0 || er >= sd) continue;
                    for (int c = 0; c < sd; c++) {
                        int ec = c + dc * (len - 1);
                        if (ec < 0 || ec >= sd) continue;
                        int overlap = 0;
                        bool fits = true;
                        for (int k = 0; k < len && fits; k++) {
                            char cur = cells[(r + dr * k) * sd + c + dc * k];
                            if (cur == w[k]) overlap++;
                            else if (cur != 0) fits = false;
                        }
                        // A crossing is welcome; lying along another word is not.
                        if (fits && overlap < len && overlap <= 2) cand[n++] = {r, c, dirs[di], overlap};
                    }
                }
            }
            if (n == 0) continue;
            // Keep the directions balanced: usually the least used direction
            // that has room, then a spot in it (crossings preferred).
            int dirCount[8] = {0};
            for (int i = 0; i < n; i++) dirCount[cand[i].dir]++;
            int dir = -1;
            if (rng.below(5) > 0) {
                int least = 1 << 30, ties = 0;
                for (int d = 0; d < 8; d++) {
                    if (!dirCount[d]) continue;
                    if (dirUse[d] < least) {
                        least = dirUse[d];
                        ties = 0;
                    }
                    if (dirUse[d] == least) ties++;
                }
                int pickTie = rng.below(ties);
                for (int d = 0; d < 8 && dir < 0; d++)
                    if (dirCount[d] && dirUse[d] == least && pickTie-- == 0) dir = d;
            } else {
                dir = cand[rng.below(n)].dir;
            }
            int total = 0;
            for (int i = 0; i < n; i++)
                if (cand[i].dir == dir) total += 1 + 3 * cand[i].overlap;
            int pick = rng.below(total);
            int sel = 0;
            for (int i = 0; i < n; i++) {
                if (cand[i].dir != dir) continue;
                pick -= 1 + 3 * cand[i].overlap;
                if (pick < 0) {
                    sel = i;
                    break;
                }
            }
            const Placement& p = cand[sel];
            int dr = kWordDirs[p.dir][0], dc = kWordDirs[p.dir][1];
            for (int k = 0; k < len; k++) cells[(p.row + dr * k) * sd + p.col + dc * k] = w[k];
            dirUse[p.dir]++;
            Word& wd = words[count++];
            wd.bank = (uint8_t)bank;
            wd.len = (uint8_t)len;
            wd.row = (int8_t)p.row;
            wd.col = (int8_t)p.col;
            wd.dr = (int8_t)dr;
            wd.dc = (int8_t)dc;
            wd.found = false;
        }
        ok = count == wordCount;
    }
    if (count == 0) return false;

    // Fill the gaps with letters from the hidden words, so the filler looks
    // like it belongs.
    char letters[MAX_WORDS * MAX_LEN];
    int ln = 0;
    for (int i = 0; i < count; i++)
        for (const char* p = text(i); *p; p++) letters[ln++] = *p;
    for (int i = 0; i < sd * sd; i++)
        if (cells[i] == 0) cells[i] = letters[rng.below(ln)];
    return ok;
}

bool WordSearch::straight(int a, int b, int& dr, int& dc, int& len) const {
    if (a < 0 || b < 0 || a >= side * side || b >= side * side || a == b) return false;
    int drow = b / side - a / side, dcol = b % side - a % side;
    if (drow != 0 && dcol != 0 && iabs(drow) != iabs(dcol)) return false;
    dr = isign(drow);
    dc = isign(dcol);
    len = (iabs(drow) > iabs(dcol) ? iabs(drow) : iabs(dcol)) + 1;
    return true;
}

int WordSearch::select(int a, int b) {
    int dr, dc, len;
    if (!straight(a, b, dr, dc, len)) return -1;
    char s[MAX_SIDE + 1];
    int r0 = a / side, c0 = a % side;
    for (int k = 0; k < len; k++) s[k] = cells[(r0 + dr * k) * side + c0 + dc * k];
    s[len] = 0;
    int result = -1;
    for (int i = 0; i < count; i++) {
        if (words[i].len != len) continue;
        const char* w = text(i);
        bool fwd = std::strcmp(s, w) == 0;
        bool rev = true;
        for (int k = 0; k < len && rev; k++) rev = s[k] == w[len - 1 - k];
        if (!fwd && !rev) continue;
        if (words[i].found) {
            result = -2;
            continue;
        }
        Word& wd = words[i];
        wd.found = true;
        // Remember the line actually chosen (it may be another copy of the
        // word), always stored reading forwards.
        if (fwd) {
            wd.row = (int8_t)r0;
            wd.col = (int8_t)c0;
            wd.dr = (int8_t)dr;
            wd.dc = (int8_t)dc;
        } else {
            wd.row = (int8_t)(r0 + dr * (len - 1));
            wd.col = (int8_t)(c0 + dc * (len - 1));
            wd.dr = (int8_t)-dr;
            wd.dc = (int8_t)-dc;
        }
        return i;
    }
    return result;
}

int WordSearch::foundCount() const {
    int n = 0;
    for (int i = 0; i < count; i++) n += words[i].found ? 1 : 0;
    return n;
}

// ===========================================================================
// Number Place
// ===========================================================================

namespace {

const uint16_t ALL = 0x3FE;  // bits 1..9

int popcount16(uint16_t v) {
    int n = 0;
    while (v) {
        v &= v - 1;
        n++;
    }
    return n;
}

int lowestDigit(uint16_t v) {
    for (int d = 1; d <= 9; d++)
        if (v & (1 << d)) return d;
    return 0;
}

int boxOf(int cell) { return (cell / 27) * 3 + (cell % 9) / 3; }

// units[u][k]: rows 0-8, columns 9-17, boxes 18-26.
struct Units {
    uint8_t cell[27][9];
    Units() {
        for (int i = 0; i < 9; i++)
            for (int k = 0; k < 9; k++) {
                cell[i][k] = (uint8_t)(i * 9 + k);
                cell[9 + i][k] = (uint8_t)(k * 9 + i);
                cell[18 + i][k] = (uint8_t)(((i / 3) * 3 + k / 3) * 9 + (i % 3) * 3 + k % 3);
            }
    }
};
const Units& units() {
    static Units u;
    return u;
}

struct Backtracker {
    uint8_t g[81];
    uint16_t row[9], col[9], box[9];
    int solutions = 0, limit = 1;
    uint8_t* out = nullptr;
    Random* rng = nullptr;

    bool load(const uint8_t* grid) {
        std::memset(row, 0, sizeof(row));
        std::memset(col, 0, sizeof(col));
        std::memset(box, 0, sizeof(box));
        for (int i = 0; i < 81; i++) {
            g[i] = grid[i];
            if (!g[i]) continue;
            uint16_t bit = (uint16_t)(1 << g[i]);
            int r = i / 9, c = i % 9, b = boxOf(i);
            if ((row[r] | col[c] | box[b]) & bit) return false;
            row[r] |= bit;
            col[c] |= bit;
            box[b] |= bit;
        }
        return true;
    }

    void search() {
        int best = -1, bestCount = 10;
        uint16_t bestMask = 0;
        for (int i = 0; i < 81; i++) {
            if (g[i]) continue;
            uint16_t m = (uint16_t)(ALL & ~(row[i / 9] | col[i % 9] | box[boxOf(i)]));
            int n = popcount16(m);
            if (n == 0) return;
            if (n < bestCount) {
                best = i;
                bestCount = n;
                bestMask = m;
                if (n == 1) break;
            }
        }
        if (best < 0) {
            if (solutions == 0 && out) std::memcpy(out, g, 81);
            solutions++;
            return;
        }
        uint8_t digits[9];
        int nd = 0;
        for (int d = 1; d <= 9; d++)
            if (bestMask & (1 << d)) digits[nd++] = (uint8_t)d;
        if (rng) rng->shuffle(digits, nd);
        int r = best / 9, c = best % 9, b = boxOf(best);
        for (int k = 0; k < nd && solutions < limit; k++) {
            uint16_t bit = (uint16_t)(1 << digits[k]);
            g[best] = digits[k];
            row[r] |= bit;
            col[c] |= bit;
            box[b] |= bit;
            search();
            row[r] &= (uint16_t)~bit;
            col[c] &= (uint16_t)~bit;
            box[b] &= (uint16_t)~bit;
            g[best] = 0;
        }
    }
};

// A pencil-and-paper solver: candidates plus the usual human techniques.
struct LogicSolver {
    uint8_t g[81];
    uint16_t cand[81];
    int filled = 0;
    bool broken = false;

    void load(const uint8_t* grid) {
        for (int i = 0; i < 81; i++) {
            g[i] = 0;
            cand[i] = ALL;
        }
        filled = 0;
        for (int i = 0; i < 81; i++)
            if (grid[i]) place(i, grid[i]);
    }

    void place(int cell, int d) {
        g[cell] = (uint8_t)d;
        cand[cell] = 0;
        filled++;
        uint16_t bit = (uint16_t)(1 << d);
        const Units& U = units();
        int us[3] = {cell / 9, 9 + cell % 9, 18 + boxOf(cell)};
        for (int k = 0; k < 3; k++)
            for (int j = 0; j < 9; j++) {
                int p = U.cell[us[k]][j];
                if (!g[p]) cand[p] &= (uint16_t)~bit;
            }
    }

    bool nakedSingle() {
        bool any = false;
        for (int i = 0; i < 81; i++) {
            if (g[i]) continue;
            if (cand[i] == 0) {
                broken = true;
                return false;
            }
            if (popcount16(cand[i]) == 1) {
                place(i, lowestDigit(cand[i]));
                any = true;
            }
        }
        return any;
    }

    bool hiddenSingle() {
        const Units& U = units();
        for (int u = 0; u < 27; u++)
            for (int d = 1; d <= 9; d++) {
                int where = -1, n = 0;
                bool placed = false;
                for (int k = 0; k < 9; k++) {
                    int c = U.cell[u][k];
                    if (g[c] == d) placed = true;
                    if (!g[c] && (cand[c] & (1 << d))) {
                        where = c;
                        n++;
                    }
                }
                if (placed) continue;
                if (n == 0) {
                    broken = true;
                    return false;
                }
                if (n == 1) {
                    place(where, d);
                    return true;
                }
            }
        return false;
    }

    // Pointing (box -> line) and claiming (line -> box).
    bool lockedCandidates() {
        const Units& U = units();
        bool any = false;
        for (int b = 0; b < 9; b++)
            for (int d = 1; d <= 9; d++) {
                uint16_t bit = (uint16_t)(1 << d);
                int rows = 0, cols = 0, n = 0;
                for (int k = 0; k < 9; k++) {
                    int c = U.cell[18 + b][k];
                    if (!g[c] && (cand[c] & bit)) {
                        rows |= 1 << (c / 9);
                        cols |= 1 << (c % 9);
                        n++;
                    }
                }
                if (n < 2) continue;
                if (popcount16((uint16_t)rows) == 1) {
                    int r = 0;
                    while (!(rows & (1 << r))) r++;
                    for (int c = 0; c < 9; c++) {
                        int cell = r * 9 + c;
                        if (boxOf(cell) != b && !g[cell] && (cand[cell] & bit)) {
                            cand[cell] &= (uint16_t)~bit;
                            any = true;
                        }
                    }
                }
                if (popcount16((uint16_t)cols) == 1) {
                    int c = 0;
                    while (!(cols & (1 << c))) c++;
                    for (int r = 0; r < 9; r++) {
                        int cell = r * 9 + c;
                        if (boxOf(cell) != b && !g[cell] && (cand[cell] & bit)) {
                            cand[cell] &= (uint16_t)~bit;
                            any = true;
                        }
                    }
                }
            }
        if (any) return true;
        for (int u = 0; u < 18; u++)
            for (int d = 1; d <= 9; d++) {
                uint16_t bit = (uint16_t)(1 << d);
                int boxes = 0, n = 0;
                for (int k = 0; k < 9; k++) {
                    int c = U.cell[u][k];
                    if (!g[c] && (cand[c] & bit)) {
                        boxes |= 1 << boxOf(c);
                        n++;
                    }
                }
                if (n < 2 || popcount16((uint16_t)boxes) != 1) continue;
                int b = 0;
                while (!(boxes & (1 << b))) b++;
                for (int k = 0; k < 9; k++) {
                    int c = U.cell[18 + b][k];
                    bool inLine = u < 9 ? c / 9 == u : c % 9 == u - 9;
                    if (!inLine && !g[c] && (cand[c] & bit)) {
                        cand[c] &= (uint16_t)~bit;
                        any = true;
                    }
                }
            }
        return any;
    }

    // Naked and hidden pairs and triples.
    bool subsets() {
        const Units& U = units();
        bool any = false;
        for (int u = 0; u < 27; u++) {
            int open[9], no = 0;
            for (int k = 0; k < 9; k++)
                if (!g[U.cell[u][k]]) open[no++] = U.cell[u][k];
            if (no < 3) continue;
            // Naked: k cells whose candidates together number k.
            for (int mask = 1; mask < (1 << no); mask++) {
                int size = popcount16((uint16_t)mask);
                if (size < 2 || size > 3 || size >= no) continue;
                uint16_t uni = 0;
                for (int i = 0; i < no; i++)
                    if (mask & (1 << i)) uni |= cand[open[i]];
                if (popcount16(uni) != size) continue;
                for (int i = 0; i < no; i++) {
                    if (mask & (1 << i)) continue;
                    if (cand[open[i]] & uni) {
                        cand[open[i]] &= (uint16_t)~uni;
                        any = true;
                    }
                }
            }
            if (any) return true;
            // Hidden: k digits that fit only k cells.
            uint16_t missing = ALL;
            for (int k = 0; k < 9; k++) missing &= (uint16_t)~(1 << g[U.cell[u][k]]);
            int digits[9], nd = 0;
            for (int d = 1; d <= 9; d++)
                if (missing & (1 << d)) digits[nd++] = d;
            for (int mask = 1; mask < (1 << nd); mask++) {
                int size = popcount16((uint16_t)mask);
                if (size < 2 || size > 3 || size >= nd) continue;
                uint16_t ds = 0;
                for (int i = 0; i < nd; i++)
                    if (mask & (1 << i)) ds |= (uint16_t)(1 << digits[i]);
                int where = 0;
                for (int i = 0; i < no; i++)
                    if (cand[open[i]] & ds) where |= 1 << i;
                if (popcount16((uint16_t)where) != size) continue;
                for (int i = 0; i < no; i++) {
                    if (!(where & (1 << i))) continue;
                    if (cand[open[i]] & ~ds) {
                        cand[open[i]] &= ds;
                        any = true;
                    }
                }
            }
            if (any) return true;
        }
        return any;
    }
};

} // namespace

int Sudoku::countSolutions(const uint8_t* grid, int limit, uint8_t* firstSolution) {
    Backtracker b;
    if (!b.load(grid)) return 0;
    b.limit = limit;
    b.out = firstSolution;
    b.search();
    return b.solutions;
}

int Sudoku::clueCount(const uint8_t* grid) {
    int n = 0;
    for (int i = 0; i < 81; i++) n += grid[i] ? 1 : 0;
    return n;
}

Sudoku::Grade Sudoku::grade(const uint8_t* puzzle) {
    LogicSolver s;
    s.load(puzzle);
    Grade hardest = NAKED_SINGLES;
    while (s.filled < 81 && !s.broken) {
        if (s.nakedSingle()) continue;
        if (s.broken) break;
        if (s.hiddenSingle()) {
            if (hardest < HIDDEN_SINGLES) hardest = HIDDEN_SINGLES;
            continue;
        }
        if (s.broken) break;
        if (s.lockedCandidates()) {
            if (hardest < LOCKED_CANDIDATES) hardest = LOCKED_CANDIDATES;
            continue;
        }
        if (s.subsets()) {
            if (hardest < SUBSETS) hardest = SUBSETS;
            continue;
        }
        break;
    }
    return (s.filled == 81 && !s.broken) ? hardest : BEYOND;
}

void Sudoku::generate(int level, uint32_t seed) {
    Random rng(seed);
    if (level < 0 || level >= LEVEL_COUNT) level = GENTLE;
    const Grade maxGrade = level == TOUGH ? SUBSETS : HIDDEN_SINGLES;
    uint8_t bestPuzzle[81], bestSolution[81];
    int bestScore = -1000;
    const int attempts = level == TOUGH ? 30 : 8;
    for (int attempt = 0; attempt < attempts; attempt++) {
        uint8_t full[81] = {0};
        Backtracker b;
        b.load(full);
        b.rng = &rng;
        b.out = full;
        b.search();

        uint8_t puzzle[81];
        std::memcpy(puzzle, full, 81);
        // Symmetric pairs (cell, 80 - cell), visited in random order.
        uint8_t order[41];
        for (int i = 0; i < 41; i++) order[i] = (uint8_t)i;
        rng.shuffle(order, 41);
        int target = level == GENTLE ? 36 + rng.below(3) : 17;
        int clues = 81;
        for (int k = 0; k < 41 && clues > target; k++) {
            int a = order[k], bb = 80 - order[k];
            uint8_t va = puzzle[a], vb = puzzle[bb];
            puzzle[a] = 0;
            puzzle[bb] = 0;
            int removed = a == bb ? 1 : 2;
            if (countSolutions(puzzle, 2) != 1 || grade(puzzle) > maxGrade) {
                puzzle[a] = va;
                puzzle[bb] = vb;
            } else {
                clues -= removed;
            }
        }
        Grade g = grade(puzzle);
        int score;
        bool good;
        if (level == GENTLE) {
            good = true;
            score = 0;
        } else if (level == MODERATE) {
            good = clues <= 30 && g == HIDDEN_SINGLES;
            score = -clues;
        } else {
            good = g >= LOCKED_CANDIDATES;
            score = (int)g * 100 - clues;
        }
        if (score > bestScore) {
            bestScore = score;
            std::memcpy(bestPuzzle, puzzle, 81);
            std::memcpy(bestSolution, full, 81);
        }
        if (good) break;
    }
    std::memcpy(givens, bestPuzzle, 81);
    std::memcpy(solution, bestSolution, 81);
}

// ===========================================================================
// Crevasse field
// ===========================================================================

void Minefield::setup(int width, int height, int mineCount) {
    w = width > MAX_W ? MAX_W : width;
    h = height > MAX_H ? MAX_H : height;
    int maxMines = w * h - 9;
    mines = mineCount > maxMines ? maxMines : mineCount;
    laid = false;
    lost = false;
    exploded = -1;
    std::memset(mine, 0, sizeof(mine));
    std::memset(adj, 0, sizeof(adj));
    std::memset(state, 0, sizeof(state));
}

int Minefield::neighbours(int cell, int* out) const {
    int r = cell / w, c = cell % w, n = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            if (!dr && !dc) continue;
            int rr = r + dr, cc = c + dc;
            if (rr >= 0 && rr < h && cc >= 0 && cc < w) out[n++] = rr * w + cc;
        }
    return n;
}

bool Minefield::lay(int safe, uint32_t seed, bool noGuess, int attempts) {
    Random rng(seed);
    int nb[8];
    for (int attempt = 0; attempt < attempts; attempt++) {
        std::memset(mine, 0, sizeof(mine));
        bool keep[MAX_CELLS] = {false};
        keep[safe] = true;
        int n = neighbours(safe, nb);
        for (int i = 0; i < n; i++) keep[nb[i]] = true;
        int cells[MAX_CELLS], nc = 0;
        for (int i = 0; i < w * h; i++)
            if (!keep[i]) cells[nc++] = i;
        rng.shuffle(cells, nc);
        for (int i = 0; i < mines && i < nc; i++) mine[cells[i]] = true;
        for (int i = 0; i < w * h; i++) {
            int k = neighbours(i, nb), a = 0;
            for (int j = 0; j < k; j++) a += mine[nb[j]] ? 1 : 0;
            adj[i] = (uint8_t)a;
        }
        laid = true;
        if (!noGuess) return false;
        Minefield trial = *this;
        trial.probe(safe);
        if (trial.solvable()) return true;
    }
    return false;
}

int Minefield::probe(int cell) {
    if (cell < 0 || cell >= w * h || state[cell] != HIDDEN) return 0;
    if (mine[cell]) {
        state[cell] = OPEN;
        lost = true;
        exploded = cell;
        return -1;
    }
    int stack[MAX_CELLS], sp = 0, opened = 0, nb[8];
    stack[sp++] = cell;
    state[cell] = OPEN;
    while (sp > 0) {
        int c = stack[--sp];
        opened++;
        if (adj[c] != 0) continue;
        int n = neighbours(c, nb);
        for (int i = 0; i < n; i++)
            if (state[nb[i]] == HIDDEN && !mine[nb[i]]) {
                state[nb[i]] = OPEN;
                stack[sp++] = nb[i];
            }
    }
    return opened;
}

int Minefield::chord(int cell) {
    if (cell < 0 || cell >= w * h || state[cell] != OPEN || adj[cell] == 0) return 0;
    int nb[8], n = neighbours(cell, nb), f = 0;
    for (int i = 0; i < n; i++) f += state[nb[i]] == FLAGGED ? 1 : 0;
    if (f != adj[cell]) return 0;
    int opened = 0;
    bool boom = false;
    for (int i = 0; i < n; i++) {
        if (state[nb[i]] != HIDDEN) continue;
        int r = probe(nb[i]);
        if (r < 0) boom = true;
        else opened += r;
    }
    return boom ? -1 : opened;
}

bool Minefield::toggleFlag(int cell) {
    if (cell < 0 || cell >= w * h || state[cell] == OPEN) return false;
    state[cell] = state[cell] == FLAGGED ? HIDDEN : FLAGGED;
    return true;
}

int Minefield::flags() const {
    int n = 0;
    for (int i = 0; i < w * h; i++) n += state[i] == FLAGGED ? 1 : 0;
    return n;
}

bool Minefield::won() const {
    if (lost || !laid) return false;
    for (int i = 0; i < w * h; i++)
        if (!mine[i] && state[i] != OPEN) return false;
    return true;
}

bool Minefield::solvable() const {
    // Works on its own copy: OPEN cells are known, `known` marks deduced
    // crevasses. Player flags are ignored.
    Minefield s = *this;
    for (int i = 0; i < w * h; i++)
        if (s.state[i] == FLAGGED) s.state[i] = HIDDEN;
    bool known[MAX_CELLS] = {false};
    int nb[8];
    int total = w * h;
    for (;;) {
        bool progress = false;
        // Single-cell rules.
        for (int i = 0; i < total; i++) {
            if (s.state[i] != OPEN || s.adj[i] == 0) continue;
            int n = neighbours(i, nb), unknown = 0, m = 0;
            for (int j = 0; j < n; j++) {
                if (s.state[nb[j]] == OPEN) continue;
                if (known[nb[j]]) m++;
                else unknown++;
            }
            if (unknown == 0) continue;
            if (m == s.adj[i]) {
                for (int j = 0; j < n; j++)
                    if (s.state[nb[j]] != OPEN && !known[nb[j]]) s.probe(nb[j]);
                progress = true;
            } else if (m + unknown == s.adj[i]) {
                for (int j = 0; j < n; j++)
                    if (s.state[nb[j]] != OPEN) known[nb[j]] = true;
                progress = true;
            }
        }
        if (s.lost) return false;
        if (progress) continue;

        // Subset rule between neighbouring numbers.
        for (int a = 0; a < total && !progress; a++) {
            if (s.state[a] != OPEN || s.adj[a] == 0) continue;
            int ua[8], na = 0, ma = 0;
            int n = neighbours(a, nb);
            for (int j = 0; j < n; j++) {
                if (s.state[nb[j]] == OPEN) continue;
                if (known[nb[j]]) ma++;
                else ua[na++] = nb[j];
            }
            if (na == 0) continue;
            int ra = s.adj[a] - ma;
            int ar = a / w, ac = a % w;
            for (int dr = -2; dr <= 2 && !progress; dr++)
                for (int dc = -2; dc <= 2 && !progress; dc++) {
                    int br = ar + dr, bc = ac + dc;
                    if ((!dr && !dc) || br < 0 || br >= h || bc < 0 || bc >= w) continue;
                    int b = br * w + bc;
                    if (s.state[b] != OPEN || s.adj[b] == 0) continue;
                    int ub[8], nbb = 0, mb = 0, nb2[8];
                    int m = neighbours(b, nb2);
                    for (int j = 0; j < m; j++) {
                        if (s.state[nb2[j]] == OPEN) continue;
                        if (known[nb2[j]]) mb++;
                        else ub[nbb++] = nb2[j];
                    }
                    if (nbb <= na) continue;
                    // Is ua a subset of ub?
                    bool subset = true;
                    for (int i = 0; i < na && subset; i++) {
                        bool in = false;
                        for (int j = 0; j < nbb; j++) in = in || ub[j] == ua[i];
                        subset = in;
                    }
                    if (!subset) continue;
                    int rb = s.adj[b] - mb;
                    int extra = nbb - na, diff = rb - ra;
                    if (diff != 0 && diff != extra) continue;
                    for (int j = 0; j < nbb; j++) {
                        bool inA = false;
                        for (int i = 0; i < na; i++) inA = inA || ua[i] == ub[j];
                        if (inA) continue;
                        if (diff == 0) s.probe(ub[j]);
                        else known[ub[j]] = true;
                    }
                    progress = true;
                }
        }
        if (s.lost) return false;
        if (progress) continue;

        // Global count: the last crevasses, or none left at all.
        int unknown = 0, found = 0;
        for (int i = 0; i < total; i++) {
            if (s.state[i] == OPEN) continue;
            if (known[i]) found++;
            else unknown++;
        }
        if (unknown == 0) break;
        if (found == mines) {
            for (int i = 0; i < total; i++)
                if (s.state[i] != OPEN && !known[i]) s.probe(i);
            continue;
        }
        if (found + unknown == mines) break;
        return false;
    }
    if (s.lost) return false;
    for (int i = 0; i < total; i++)
        if (!s.mine[i] && s.state[i] != OPEN) return false;
    return true;
}

// ===========================================================================
// Klondike
// ===========================================================================

void Klondike::deal(uint32_t seed, int drawCount, int limit) {
    Random rng(seed);
    uint8_t deck[52];
    for (int i = 0; i < 52; i++) deck[i] = (uint8_t)i;
    rng.shuffle(deck, 52);
    int k = 0;
    for (int c = 0; c < 7; c++) {
        tabN[c] = c + 1;
        down[c] = c;
        for (int i = 0; i <= c; i++) tab[c][i] = deck[k++];
    }
    stockN = 0;
    while (k < 52) stock[stockN++] = deck[k++];
    wasteN = 0;
    for (int s = 0; s < 4; s++) foundation[s] = 0;
    draw = drawCount == 3 ? 3 : 1;
    passLimit = limit < 0 ? 0 : limit;
    passes = 1;
    moves = 0;
}

bool Klondike::turnStock() {
    if (stockN > 0) {
        for (int i = 0; i < draw && stockN > 0; i++) waste[wasteN++] = stock[--stockN];
        moves++;
        return true;
    }
    if (!canRecycle()) return false;
    for (int i = 0; i < wasteN; i++) stock[i] = waste[wasteN - 1 - i];
    stockN = wasteN;
    wasteN = 0;
    passes++;
    moves++;
    return true;
}

bool Klondike::fitsTableau(uint8_t card, int col) const {
    if (tabN[col] == 0) return rank(card) == 12;
    uint8_t top = tab[col][tabN[col] - 1];
    return rank(top) == rank(card) + 1 && red(top) != red(card);
}

bool Klondike::fitsFoundation(uint8_t card) const { return foundation[suit(card)] == rank(card); }

void Klondike::flipTop(int col) {
    if (tabN[col] > 0 && down[col] >= tabN[col]) down[col] = tabN[col] - 1;
}

bool Klondike::moveFromTableau(int col, int index, int dest) {
    if (col < 0 || col > 6 || index < down[col] || index >= tabN[col]) return false;
    uint8_t card = tab[col][index];
    if (dest >= 7) {
        if (index != tabN[col] - 1 || dest - 7 != suit(card) || !fitsFoundation(card)) return false;
        foundation[suit(card)]++;
        tabN[col]--;
    } else {
        int run = tabN[col] - index;
        if (dest == col || !fitsTableau(card, dest) || tabN[dest] + run > MAX_COL) return false;
        for (int i = 0; i < run; i++) tab[dest][tabN[dest]++] = tab[col][index + i];
        tabN[col] = index;
    }
    flipTop(col);
    moves++;
    return true;
}

bool Klondike::moveFromWaste(int dest) {
    if (wasteN == 0) return false;
    uint8_t card = waste[wasteN - 1];
    if (dest >= 7) {
        if (dest - 7 != suit(card) || !fitsFoundation(card)) return false;
        foundation[suit(card)]++;
    } else {
        if (!fitsTableau(card, dest) || tabN[dest] >= MAX_COL) return false;
        tab[dest][tabN[dest]++] = card;
    }
    wasteN--;
    moves++;
    return true;
}

bool Klondike::moveFromFoundation(int s, int col) {
    if (s < 0 || s > 3 || foundation[s] == 0 || col < 0 || col > 6) return false;
    uint8_t card = (uint8_t)(s * 13 + foundation[s] - 1);
    if (!fitsTableau(card, col) || tabN[col] >= MAX_COL) return false;
    tab[col][tabN[col]++] = card;
    foundation[s]--;
    moves++;
    return true;
}

int Klondike::bestTableauDest(int col, int index) const {
    if (col < 0 || col > 6 || index < down[col] || index >= tabN[col]) return -1;
    uint8_t card = tab[col][index];
    if (index == tabN[col] - 1 && fitsFoundation(card)) return 7 + suit(card);
    for (int k = 1; k < 7; k++) {
        int d = (col + k) % 7;
        if (!fitsTableau(card, d)) continue;
        // A king already at the foot of its column gains nothing by moving.
        if (tabN[d] == 0 && index == 0) continue;
        return d;
    }
    return -1;
}

int Klondike::bestWasteDest() const {
    if (wasteN == 0) return -1;
    uint8_t card = waste[wasteN - 1];
    if (fitsFoundation(card)) return 7 + suit(card);
    for (int d = 0; d < 7; d++)
        if (fitsTableau(card, d)) return d;
    return -1;
}

int Klondike::bestFoundationDest(int s) const {
    if (s < 0 || s > 3 || foundation[s] == 0) return -1;
    uint8_t card = (uint8_t)(s * 13 + foundation[s] - 1);
    for (int d = 0; d < 7; d++)
        if (fitsTableau(card, d)) return d;
    return -1;
}

bool Klondike::hasUsefulMove() const {
    for (int c = 0; c < 7; c++) {
        if (tabN[c] == 0) continue;
        if (fitsFoundation(tab[c][tabN[c] - 1])) return true;
        for (int i = down[c]; i < tabN[c]; i++) {
            uint8_t card = tab[c][i];
            // Useful if it uncovers a face-down card, or a card that can go up.
            bool uncovers = (i == down[c] && down[c] > 0) || (i > down[c] && fitsFoundation(tab[c][i - 1]));
            if (!uncovers) continue;
            for (int d = 0; d < 7; d++)
                if (d != c && fitsTableau(card, d)) return true;
        }
    }
    // Cards that can still be turned up from the stock and waste.
    uint8_t order[52];
    int n = 0;
    bool more = passLimit == 0 || passes < passLimit;
    if (wasteN > 0) order[n++] = waste[wasteN - 1];
    if (draw == 1) {
        for (int i = stockN - 1; i >= 0; i--) order[n++] = stock[i];
        if (more)
            for (int i = 0; i < wasteN - 1; i++) order[n++] = waste[i];
    } else {
        uint8_t seq[52];
        int m = 0;
        for (int i = stockN - 1; i >= 0; i--) seq[m++] = stock[i];
        for (int i = 2; i < m; i += 3) order[n++] = seq[i];
        if (m > 0 && m % 3 != 0) order[n++] = seq[m - 1];
        if (more) {
            m = 0;
            for (int i = 0; i < wasteN; i++) seq[m++] = waste[i];
            for (int i = stockN - 1; i >= 0; i--) seq[m++] = stock[i];
            for (int i = 2; i < m; i += 3) order[n++] = seq[i];
            if (m > 0) order[n++] = seq[m - 1];
        }
    }
    for (int i = 0; i < n; i++) {
        if (fitsFoundation(order[i])) return true;
        for (int d = 0; d < 7; d++)
            if (fitsTableau(order[i], d)) return true;
    }
    return false;
}

bool Klondike::canCollect() const {
    if (stockN > 0 || wasteN > 0 || won()) return false;
    for (int c = 0; c < 7; c++)
        if (down[c] > 0) return false;
    return true;
}

bool Klondike::collectOne() {
    int best = -1, bestRank = 99;
    for (int c = 0; c < 7; c++) {
        if (tabN[c] == 0) continue;
        uint8_t card = tab[c][tabN[c] - 1];
        if (fitsFoundation(card) && rank(card) < bestRank) {
            best = c;
            bestRank = rank(card);
        }
    }
    if (best >= 0) return moveFromTableau(best, tabN[best] - 1, 7 + suit(tab[best][tabN[best] - 1]));
    if (wasteN > 0 && fitsFoundation(waste[wasteN - 1])) return moveFromWaste(7 + suit(waste[wasteN - 1]));
    return false;
}

} // namespace Games
