// Host-side checks for the puzzle generators and rules in GameLogic.
//
//   g++ -std=c++11 -O2 -Iinclude -o test_game_logic tools/test_game_logic.cpp
//       src/apps/GameLogic.cpp src/apps/WordSearchThemes.cpp && ./test_game_logic
// (one command line)
//
// Exits non-zero on the first failure and prints generation timings.

#include "GameLogic.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace Games;

static int failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                     \
            std::printf("\n");                            \
            failures++;                                   \
        }                                                 \
    } while (0)

static double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static void testThemes() {
    for (int t = 0; t < wordThemeCount(); t++) {
        const WordTheme& T = wordTheme(t);
        CHECK(T.count >= 20, "theme %s has only %d words", T.name, T.count);
        std::set<std::string> seen;
        for (int i = 0; i < T.count; i++) {
            const char* w = T.words[i];
            int len = (int)std::strlen(w);
            CHECK(len >= 3 && len <= WordSearch::MAX_LEN, "%s: bad length %s", T.name, w);
            for (const char* p = w; *p; p++) CHECK(*p >= 'A' && *p <= 'Z', "%s: bad letter in %s", T.name, w);
            CHECK(seen.insert(w).second, "%s: duplicate %s", T.name, w);
        }
    }
}

static void testWordSearch() {
    const int sides[3] = {8, 10, 12};
    const int counts[3] = {8, 11, 14};
    const uint8_t masks[3] = {0x03, 0x0F, 0xFF};
    double worst = 0;
    int short_ = 0;
    for (int level = 0; level < 3; level++) {
        for (int t = 0; t < wordThemeCount(); t++) {
            for (uint32_t seed = 1; seed <= 40; seed++) {
                WordSearch ws;
                auto t0 = std::chrono::steady_clock::now();
                bool ok = ws.generate(t, sides[level], counts[level], masks[level], seed * 7919 + t);
                double ms = msSince(t0);
                if (ms > worst) worst = ms;
                if (!ok) short_++;
                CHECK(ws.count >= counts[level] - 2, "theme %d side %d placed only %d", t, sides[level], ws.count);
                for (int i = 0; i < ws.side * ws.side; i++)
                    CHECK(ws.cells[i] >= 'A' && ws.cells[i] <= 'Z', "unfilled cell");
                // Every word must sit where it says, in an allowed direction.
                for (int i = 0; i < ws.count; i++) {
                    const WordSearch::Word& w = ws.words[i];
                    int dir = -1;
                    for (int d = 0; d < 8; d++)
                        if (kWordDirs[d][0] == w.dr && kWordDirs[d][1] == w.dc) dir = d;
                    CHECK(dir >= 0 && (masks[level] & (1 << dir)), "direction not allowed");
                    for (int k = 0; k < w.len; k++)
                        CHECK(ws.cells[(w.row + w.dr * k) * ws.side + w.col + w.dc * k] == ws.text(i)[k],
                              "word %s not in grid", ws.text(i));
                }
                // Select every word, alternately backwards; then the puzzle is won.
                for (int i = 0; i < ws.count; i++) {
                    const WordSearch::Word w = ws.words[i];
                    int a = w.row * ws.side + w.col;
                    int b = (w.row + w.dr * (w.len - 1)) * ws.side + w.col + w.dc * (w.len - 1);
                    int r = (i % 2) ? ws.select(b, a) : ws.select(a, b);
                    CHECK(r >= 0 || ws.words[i].found, "select failed for %s", ws.text(i));
                    CHECK(ws.select(a, b) == -2, "re-select should report already found");
                }
                CHECK(ws.won(), "not won after finding all");
                CHECK(ws.select(0, 0) == -1, "single cell must not match");
            }
        }
    }
    // A bent line never matches.
    WordSearch ws;
    ws.generate(0, 10, 11, 0xFF, 99);
    int dr, dc, len;
    CHECK(!ws.straight(0, 12, dr, dc, len), "knight move counted as straight");
    CHECK(ws.straight(0, 99, dr, dc, len) && len == 10 && dr == 1 && dc == 1, "diagonal");
    std::printf("word search: worst generation %.2f ms, %d grids short of words\n", worst, short_);
}

static bool validSolution(const uint8_t* g) {
    for (int u = 0; u < 9; u++) {
        int r = 0, c = 0, b = 0;
        for (int k = 0; k < 9; k++) {
            r |= 1 << g[u * 9 + k];
            c |= 1 << g[k * 9 + u];
            b |= 1 << g[((u / 3) * 3 + k / 3) * 9 + (u % 3) * 3 + k % 3];
        }
        if (r != 0x3FE || c != 0x3FE || b != 0x3FE) return false;
    }
    return true;
}

static void testSudoku() {
    const char* names[3] = {"Gentle", "Moderate", "Tough"};
    for (int level = 0; level < 3; level++) {
        double worst = 0, total = 0;
        int clues = 0, minClues = 81, maxClues = 0;
        int grades[5] = {0};
        const int N = 25;
        for (int i = 0; i < N; i++) {
            Sudoku s;
            auto t0 = std::chrono::steady_clock::now();
            s.generate(level, 1234 + i * 101 + level);
            double ms = msSince(t0);
            total += ms;
            if (ms > worst) worst = ms;
            CHECK(validSolution(s.solution), "invalid solution");
            uint8_t sol[81];
            CHECK(Sudoku::countSolutions(s.givens, 3, sol) == 1, "%s puzzle not unique", names[level]);
            CHECK(std::memcmp(sol, s.solution, 81) == 0, "solution mismatch");
            for (int k = 0; k < 81; k++) CHECK(!s.givens[k] || s.givens[k] == s.solution[k], "given mismatch");
            for (int k = 0; k < 81; k++)
                CHECK((s.givens[k] != 0) == (s.givens[80 - k] != 0), "not symmetric");
            int c = Sudoku::clueCount(s.givens);
            clues += c;
            if (c < minClues) minClues = c;
            if (c > maxClues) maxClues = c;
            Sudoku::Grade g = Sudoku::grade(s.givens);
            grades[g]++;
            CHECK(g != Sudoku::BEYOND, "%s puzzle needs guessing", names[level]);
            if (level < 2) CHECK(g <= Sudoku::HIDDEN_SINGLES, "%s puzzle too hard (grade %d)", names[level], g);
        }
        std::printf("sudoku %-8s: clues %d-%d (avg %.1f), grades singles %d/%d locked %d subsets %d, avg %.1f ms, worst %.1f ms\n",
                    names[level], minClues, maxClues, clues / (double)N, grades[0], grades[1], grades[2], grades[3],
                    total / N, worst);
    }
    // A known puzzle with two solutions is reported as such.
    uint8_t empty[81] = {0};
    CHECK(Sudoku::countSolutions(empty, 2) == 2, "empty grid should have many solutions");
}

static void testMinefield() {
    struct L { int w, h, m; } levels[3] = {{8, 10, 10}, {9, 12, 20}, {9, 13, 28}};
    for (int li = 0; li < 3; li++) {
        int guaranteed = 0;
        double worst = 0, total = 0;
        const int N = 40;
        for (int i = 0; i < N; i++) {
            Minefield f;
            f.setup(levels[li].w, levels[li].h, levels[li].m);
            int first = (f.h / 2) * f.w + f.w / 2;
            auto t0 = std::chrono::steady_clock::now();
            bool ng = f.lay(first, 777 + i * 31 + li, true);
            double ms = msSince(t0);
            total += ms;
            if (ms > worst) worst = ms;
            guaranteed += ng ? 1 : 0;
            int count = 0, nb[8];
            for (int k = 0; k < f.w * f.h; k++) count += f.mine[k];
            CHECK(count == levels[li].m, "wrong crevasse count %d", count);
            CHECK(!f.mine[first], "first probe on a crevasse");
            int n = f.neighbours(first, nb);
            for (int k = 0; k < n; k++) CHECK(!f.mine[nb[k]], "crevasse next to first probe");
            CHECK(f.probe(first) > 1, "first probe should open an area");
            // Clear the field by playing perfectly (we know the answer).
            for (int k = 0; k < f.w * f.h; k++)
                if (f.mine[k]) f.toggleFlag(k);
            for (int k = 0; k < f.w * f.h; k++)
                if (!f.mine[k]) CHECK(f.probe(k) >= 0, "safe probe hit a crevasse");
            CHECK(f.won(), "field not won");
        }
        std::printf("crevasse %dx%d/%d: %d of %d guess-free, avg %.1f ms, worst %.1f ms\n", levels[li].w, levels[li].h,
                    levels[li].m, guaranteed, N, total / N, worst);
    }
    // Chording.
    Minefield f;
    f.setup(9, 12, 18);
    f.lay(0, 5, false);
    f.probe(0);
    int chorded = 0;
    for (int k = 0; k < f.w * f.h && !chorded; k++) {
        if (f.state[k] != Minefield::OPEN || f.adj[k] == 0) continue;
        int nb[8], n = f.neighbours(k, nb), hidden = 0;
        for (int j = 0; j < n; j++) hidden += f.state[nb[j]] == Minefield::HIDDEN && !f.mine[nb[j]];
        if (!hidden) continue;
        CHECK(f.chord(k) == 0, "chord without flags should do nothing");
        for (int j = 0; j < n; j++)
            if (f.mine[nb[j]] && f.state[nb[j]] == Minefield::HIDDEN) f.toggleFlag(nb[j]);
        chorded = f.chord(k);
        CHECK(chorded > 0, "chord with flags should open cells");
    }
    // A wrong flag makes a chord fatal.
    f.setup(9, 12, 18);
    f.lay(0, 6, false);
    f.probe(0);
    std::printf("crevasse: chord opened %d cells\n", chorded);
}

static void testKlondike() {
    int wins = 0, stuck = 0;
    const int N = 300;
    for (int i = 0; i < N; i++) {
        Klondike k;
        k.deal(4242 + i, 1, 0);
        int seen[52] = {0};
        for (int c = 0; c < 7; c++)
            for (int j = 0; j < k.tabN[c]; j++) seen[k.tab[c][j]]++;
        for (int j = 0; j < k.stockN; j++) seen[k.stock[j]]++;
        for (int j = 0; j < 52; j++) CHECK(seen[j] == 1, "card %d dealt %d times", j, seen[j]);
        CHECK(k.stockN == 24, "stock size");
        // Greedy autoplayer with the tap-to-move rules.
        int idle = 0;
        for (int step = 0; step < 3000 && !k.won(); step++) {
            if (k.canCollect()) {
                while (k.collectOne()) {
                }
                break;
            }
            bool moved = false;
            for (int c = 0; c < 7 && !moved; c++) {
                if (k.tabN[c] == 0) continue;
                int d = k.bestTableauDest(c, k.tabN[c] - 1);
                if (d >= 7) moved = k.moveFromTableau(c, k.tabN[c] - 1, d);
            }
            if (!moved) {
                int d = k.bestWasteDest();
                if (d >= 0) moved = k.moveFromWaste(d);
            }
            for (int c = 0; c < 7 && !moved; c++) {
                if (k.down[c] == 0 || k.tabN[c] == 0) continue;
                int d = k.bestTableauDest(c, k.down[c]);
                if (d >= 0 && d < 7) moved = k.moveFromTableau(c, k.down[c], d);
            }
            if (moved) {
                idle = 0;
                continue;
            }
            if (++idle > 60 || !k.turnStock()) break;
        }
        if (k.won()) wins++;
        else if (!k.hasUsefulMove()) stuck++;
        int total = k.foundationCards() + k.stockN + k.wasteN;
        for (int c = 0; c < 7; c++) total += k.tabN[c];
        CHECK(total == 52, "cards lost: %d", total);
    }
    std::printf("klondike: greedy player won %d of %d draw-one deals; %d reported stuck\n", wins, N, stuck);

    // Rules.
    Klondike k;
    k.deal(1, 3, 0);
    CHECK(k.turnStock() && k.wasteN == 3 && k.stockN == 21, "draw three");
    while (k.stockN) k.turnStock();
    CHECK(k.canRecycle() && k.turnStock() && k.stockN == 24 && k.wasteN == 0, "recycle");
    Klondike lim;
    lim.deal(1, 1, 1);
    while (lim.stockN) lim.turnStock();
    CHECK(!lim.canRecycle(), "single pass should not recycle");
}

int main() {
    testThemes();
    testWordSearch();
    testSudoku();
    testMinefield();
    testKlondike();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all game logic checks passed\n");
    return 0;
}
