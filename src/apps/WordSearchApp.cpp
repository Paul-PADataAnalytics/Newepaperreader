#include "WordSearchApp.h"
#include "ui/UIFramework.h"
#include <cstring>

extern uint8_t* framebuffer;

namespace {
using namespace UI;
using GameKit::SMALL;

struct Level {
    const char* name;
    int side;
    int words;
    uint8_t dirs;
    const char* detail;
};

const Level kLevels[WordSearchApp::LEVEL_COUNT] = {
    {"Easy", 8, 8, 0x03, "8 \xC3\x97 8  \xC2\xB7  across and down"},
    {"Medium", 10, 11, 0x0F, "10 \xC3\x97 10  \xC2\xB7  diagonals too"},
    {"Hard", 12, 14, 0xFF, "12 \xC3\x97 12  \xC2\xB7  any way, even backwards"},
};

enum Action {
    A_NONE = 0,
    A_LEVEL_0, A_LEVEL_1, A_LEVEL_2,
    A_CONTINUE, A_HELP, A_THEME_PREV, A_THEME_NEXT,
    A_AGAIN, A_TITLE,
};

constexpr int LINE_Y = HEADER_H + 6;
constexpr int LINE_H = 44;
constexpr int GRID_Y = HEADER_H + 54;

float letterSize(int side) { return side <= 8 ? 36.0f : (side <= 10 ? 32.0f : 28.0f); }

std::string sizeName(int level) {
    return std::to_string(kLevels[level].side) + " \xC3\x97 " + std::to_string(kLevels[level].side);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WordSearchApp::onCreate() {
    m_screen = TITLE;
    m_level = -1;
    load();
}

void WordSearchApp::onDestroy() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
}

void WordSearchApp::draw() {
    m_lastDrawn = -1;
    render();
}

bool WordSearchApp::onBack() {
    switch (m_screen) {
        case TITLE:
            return false;
        case HELP:
            m_screen = m_helpReturn;
            if (m_screen == BOARD) m_clock.start();
            break;
        default:
            leaveBoard();
            m_screen = TITLE;
            break;
    }
    render();
    return true;
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

void WordSearchApp::newPuzzle(int level) {
    const Level& L = kLevels[level];
    int theme = m_themeChoice;
    if (theme < 0 || theme >= Games::wordThemeCount()) {
        Games::Random rng(GameKit::randomSeed());
        do {
            theme = rng.below(Games::wordThemeCount());
        } while (theme == m_lastTheme && Games::wordThemeCount() > 1);
    }
    m_puzzle.generate(theme, L.side, L.words, L.dirs, GameKit::randomSeed());
    m_lastTheme = theme;
    m_level = level;
    m_finished = false;
    m_first = -1;
    m_message.clear();
    m_newBest = false;
    m_clock = GameKit::PlayClock();
    save();
    showBoard();
}

void WordSearchApp::showBoard() {
    m_screen = BOARD;
    m_first = -1;
    m_message.clear();
    m_clock.start();
    render();
}

void WordSearchApp::leaveBoard() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
    m_first = -1;
}

void WordSearchApp::finish() {
    m_clock.stop();
    m_finished = true;
    m_solved[m_level]++;
    int t = (int)m_clock.seconds;
    if (t < 1) t = 1;
    m_newBest = m_best[m_level] == 0 || t < m_best[m_level];
    if (m_newBest) m_best[m_level] = t;
    save();
    m_screen = DONE;
    render();
}

void WordSearchApp::tapGrid(int x, int y) {
    int cell = cellAt(x, y);
    if (cell < 0) return;
    m_clock.tick();
    if (m_first < 0) {
        m_first = cell;
        bool hadMessage = !m_message.empty();
        m_message.clear();
        refreshBoard(true, hadMessage, false);
        return;
    }
    if (cell == m_first) {
        m_first = -1;
        refreshBoard(true, false, false);
        return;
    }

    // Accept the tapped cell, or failing that a neighbour the finger may
    // have meant (the nearest first), if it completes a word.
    int candidates[9], n = 0;
    candidates[n++] = cell;
    const int side = m_puzzle.side, cs = cellSize();
    int r = cell / side, c = cell % side;
    struct Near { int cell, d2; } near[8];
    int nn = 0;
    Rect g = gridRect();
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            int rr = r + dr, cc = c + dc;
            if ((!dr && !dc) || rr < 0 || rr >= side || cc < 0 || cc >= side) continue;
            int cx = g.x + cc * cs + cs / 2, cy = g.y + rr * cs + cs / 2;
            int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d2 * 100 < cs * cs * 64) near[nn++] = {rr * side + cc, d2};  // within 0.8 cell
        }
    for (int i = 1; i < nn; i++)
        for (int j = i; j > 0 && near[j].d2 < near[j - 1].d2; j--) {
            Near t = near[j];
            near[j] = near[j - 1];
            near[j - 1] = t;
        }
    for (int i = 0; i < nn; i++) candidates[n++] = near[i].cell;

    int found = -1;
    bool already = false;
    for (int i = 0; i < n && found < 0; i++) {
        if (candidates[i] == m_first) continue;
        int res = m_puzzle.select(m_first, candidates[i]);
        if (res >= 0) found = res;
        else if (res == -2) already = true;
    }
    int dr, dc, len;
    bool straight = m_puzzle.straight(m_first, cell, dr, dc, len);
    char a = m_puzzle.cells[m_first], b = m_puzzle.cells[cell];
    m_first = -1;
    if (found >= 0) {
        m_message.clear();
        if (m_puzzle.won()) {
            finish();
            return;
        }
        refreshBoard(true, true, true);
        save();
        return;
    }
    // Keep the top status bar unchanged on a miss so only the deselected cell
    // needs a partial refresh instead of a bounding box spanning the whole board.
    (void)already;
    (void)straight;
    (void)a;
    (void)b;
    refreshBoard(true, false, false);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// /data/wordsearch.txt:
//   WS1 themeChoice lastTheme solved0 solved1 solved2 best0 best1 best2
//   G level theme seconds count letters            (puzzle in progress only)
//   bank len row col dr dc found                   (count lines)

void WordSearchApp::load() {
    FILE* f = GameKit::openData("wordsearch.txt", "r");
    if (!f) return;
    int v[8];
    if (fscanf(f, " WS1 %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8) {
        m_themeChoice = (v[0] >= 0 && v[0] < Games::wordThemeCount()) ? v[0] : -1;
        m_lastTheme = v[1];
        for (int i = 0; i < LEVEL_COUNT; i++) {
            m_solved[i] = v[2 + i] > 0 ? v[2 + i] : 0;
            m_best[i] = v[5 + i] > 0 ? v[5 + i] : 0;
        }
    }
    int level, theme, seconds, count;
    char letters[Games::WordSearch::MAX_SIDE * Games::WordSearch::MAX_SIDE + 1];
    if (fscanf(f, " G %d %d %d %d %144s", &level, &theme, &seconds, &count, letters) == 5 && level >= 0 &&
        level < LEVEL_COUNT && theme >= 0 && theme < Games::wordThemeCount() && count > 0 &&
        count <= Games::WordSearch::MAX_WORDS) {
        Games::WordSearch p;
        p.side = kLevels[level].side;
        p.theme = theme;
        p.count = count;
        bool ok = (int)strlen(letters) == p.side * p.side;
        if (ok) memcpy(p.cells, letters, p.side * p.side);
        const Games::WordTheme& T = Games::wordTheme(theme);
        for (int i = 0; i < count && ok; i++) {
            int bank, len, row, col, dr, dc, found;
            ok = fscanf(f, " %d %d %d %d %d %d %d", &bank, &len, &row, &col, &dr, &dc, &found) == 7 && bank >= 0 &&
                 bank < T.count && len == (int)strlen(T.words[bank]) && row >= 0 && row < p.side && col >= 0 &&
                 col < p.side && dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1 && (dr || dc) &&
                 row + dr * (len - 1) >= 0 && row + dr * (len - 1) < p.side && col + dc * (len - 1) >= 0 &&
                 col + dc * (len - 1) < p.side;
            if (ok) p.words[i] = {(uint8_t)bank, (uint8_t)len, (int8_t)row, (int8_t)col, (int8_t)dr, (int8_t)dc, found != 0};
        }
        if (ok && !p.won()) {
            m_puzzle = p;
            m_level = level;
            m_clock.seconds = seconds > 0 ? (uint32_t)seconds : 0;
            m_finished = false;
        }
    }
    fclose(f);
}

void WordSearchApp::save() {
    FILE* f = GameKit::openData("wordsearch.txt", "w");
    if (!f) return;
    fprintf(f, "WS1 %d %d %d %d %d %d %d %d\n", m_themeChoice, m_lastTheme, m_solved[0], m_solved[1], m_solved[2],
            m_best[0], m_best[1], m_best[2]);
    if (m_level >= 0 && !m_finished) {
        const Games::WordSearch& p = m_puzzle;
        std::string letters(p.cells, p.cells + p.side * p.side);
        fprintf(f, "G %d %d %u %d %s\n", m_level, p.theme, (unsigned)m_clock.seconds, p.count, letters.c_str());
        for (int i = 0; i < p.count; i++) {
            const Games::WordSearch::Word& w = p.words[i];
            fprintf(f, "%d %d %d %d %d %d %d\n", w.bank, w.len, w.row, w.col, w.dr, w.dc, w.found ? 1 : 0);
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

void WordSearchApp::addButton(const Rect& r, int action) { m_buttons.push_back({r, action}); }

void WordSearchApp::handleTouch(int x, int y) {
    if (m_screen == HELP) {
        if (hitHeaderAction(x, y, "Done")) {
            onBack();
            return;
        }
        int step = GameKit::hitPageFooter(x, y, m_helpPage, 3);
        if (step) {
            m_helpPage += step;
            UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() {
                drawHelp();
            });
        }
        return;
    }
    if (m_screen == BOARD) {
        if (hitHeaderAction(x, y, "Rules")) {
            leaveBoard();
            m_helpReturn = BOARD;
            m_helpPage = 0;
            m_screen = HELP;
            render();
            return;
        }
        Rect g = gridRect();
        if (g.contains(x, y)) tapGrid(x, y);
        return;
    }
    int action = A_NONE;
    for (const Button& b : m_buttons)
        if (b.rect.contains(x, y)) {
            action = b.action;
            break;
        }
    if (m_screen == TITLE && hitHeaderAction(x, y, "How to play")) action = A_HELP;
    switch (action) {
        case A_LEVEL_0:
        case A_LEVEL_1:
        case A_LEVEL_2:
            newPuzzle(action - A_LEVEL_0);
            break;
        case A_CONTINUE:
            if (m_level >= 0 && !m_finished) showBoard();
            break;
        case A_HELP:
            m_helpReturn = m_screen;
            m_helpPage = 0;
            m_screen = HELP;
            render();
            break;
        case A_THEME_PREV:
        case A_THEME_NEXT: {
            // Cycle: surprise, theme 0, theme 1, ...
            int n = Games::wordThemeCount() + 1;
            int i = m_themeChoice + 1;
            i = (i + (action == A_THEME_NEXT ? 1 : n - 1)) % n;
            m_themeChoice = i - 1;
            save();
            // Only the theme name changes, so the partial update stays small.
            UIFramework::performFastPartialUpdate(framebuffer, 0, 0, W, H, [this]() {
                m_buttons.clear();
                drawTitle();
            });
            break;
        }
        case A_AGAIN:
            newPuzzle(m_level >= 0 ? m_level : 0);
            break;
        case A_TITLE:
            m_screen = TITLE;
            render();
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

int WordSearchApp::cellSize() const {
    int side = m_puzzle.side > 0 ? m_puzzle.side : 8;
    int s = (W - 24) / side;
    return s > 64 ? 64 : s;
}

Rect WordSearchApp::gridRect() const {
    int cs = cellSize(), gw = cs * m_puzzle.side;
    return Rect((W - gw) / 2, GRID_Y, gw, gw);
}

int WordSearchApp::cellAt(int x, int y) const {
    Rect g = gridRect();
    if (!g.contains(x, y)) return -1;
    int cs = cellSize();
    return ((y - g.y) / cs) * m_puzzle.side + (x - g.x) / cs;
}

void WordSearchApp::cellCentre(int cell, int& cx, int& cy) const {
    Rect g = gridRect();
    int cs = cellSize();
    cx = g.x + (cell % m_puzzle.side) * cs + cs / 2;
    cy = g.y + (cell / m_puzzle.side) * cs + cs / 2;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void WordSearchApp::render() {
    m_buttons.clear();
    auto content = [this]() {
        switch (m_screen) {
            case TITLE: drawTitle(); break;
            case HELP: drawHelp(); break;
            case BOARD:
            case DONE: drawBoard(); break;
        }
    };
    // Clean full refresh on screen changes (Title <-> Board <-> Done); in-game
    // moves use localized fast partial updates via refreshBoard().
    UIFramework::performFullScreenDraw(framebuffer, content);
    m_lastDrawn = m_screen;
}

void WordSearchApp::drawTitle() {
    header("Word Search", true, "How to play");

    // Emblem: a word ringed in a grid of letter tiles.
    const char* emblem = "WORDS";
    int ts = 56, gap = 10, n = 5;
    int ew = n * ts + (n - 1) * gap;
    int ex = (W - ew) / 2, ey = HEADER_H + 26;
    line(ex + ts / 2, ey + ts / 2, ex + ew - ts / 2, ey + ts / 2, ts + 22, LIGHT);
    for (int i = 0; i < n; i++)
        GameKit::letterTile(Rect(ex + i * (ts + gap), ey, ts, ts), std::string(1, emblem[i]), i == 0, 32);

    int y = ey + ts + 30;
    y = textWrapped("Words hide in the grid: across, down, diagonally, even backwards. Tap the first letter "
                    "of a word, then the last, and ring it.",
                    MARGIN + 4, y, W - 2 * MARGIN - 8, SMALL, INK, 0, 7, true);

    y += 18;
    GameKit::sectionLabel(y, "Best times");
    y += 32;
    int cw = (W - 2 * MARGIN) / LEVEL_COUNT;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        int x = MARGIN + i * cw;
        text(m_best[i] ? GameKit::clockText(m_best[i]) : std::string("\xE2\x80\x93"), x, y, FONT_TITLE, BLACK, true);
        std::string cap = sizeName(i);
        if (m_solved[i]) cap += "  \xC2\xB7  " + std::to_string(m_solved[i]);
        text(cap, x, y + lineHeight(FONT_TITLE) + 2, SMALL, DARK);
    }
    y += lineHeight(FONT_TITLE) + lineHeight(SMALL) + 22;

    bool inProgress = m_level >= 0 && !m_finished;
    if (inProgress) {
        Rect c(MARGIN, y, W - 2 * MARGIN, 80);
        std::string detail = std::string(Games::wordTheme(m_puzzle.theme).name) + "  \xC2\xB7  " +
                             std::to_string(m_puzzle.foundCount()) + " of " + std::to_string(m_puzzle.count) +
                             " found";
        GameKit::optionButton(c, "Continue", detail, true);
        addButton(c, A_CONTINUE);
        y += 96;
    } else {
        y += 10;
    }

    // Theme picker: < Theme name >
    GameKit::sectionLabel(y, "Theme");
    y += 34;
    Rect picker(MARGIN, y, W - 2 * MARGIN, 64);
    fillRoundRect(picker.x, picker.y, picker.w, picker.h, 14, PALE);
    chevronLeft(picker.x + 30, picker.y + picker.h / 2, 24);
    chevronRight(picker.x + picker.w - 30, picker.y + picker.h / 2, 24);
    std::string name = m_themeChoice < 0 ? "Surprise me" : Games::wordTheme(m_themeChoice).name;
    textCentered(name, W / 2, picker.y + (picker.h - lineHeight(FONT_BODY)) / 2, FONT_BODY, BLACK, true);
    addButton(Rect(picker.x, picker.y - 6, picker.w / 2, picker.h + 12), A_THEME_PREV);
    addButton(Rect(picker.x + picker.w / 2, picker.y - 6, picker.w / 2, picker.h + 12), A_THEME_NEXT);
    y += 64 + 26;

    GameKit::sectionLabel(y, inProgress ? "New puzzle (replaces the one in progress)" : "New puzzle");
    y += 34;
    int bh = (H - 20 - y - 2 * 12) / 3;
    if (bh > 84) bh = 84;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        Rect r(MARGIN, y + i * (bh + 12), W - 2 * MARGIN, bh);
        GameKit::optionButton(r, kLevels[i].name, kLevels[i].detail, !inProgress && i == 0);
        addButton(r, A_LEVEL_0 + i);
    }
}

void WordSearchApp::drawHelp() {
    header("How to play", true, "Done");
    const int tw = W - 2 * MARGIN;
    int y = HEADER_H + 30;
    if (m_helpPage == 0) {
        text("Ring the hidden words", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Every word in the list below the grid is hidden in it. Tap the first letter of a word: "
                        "it turns black. Then tap its last letter.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 26;
        // Worked example: S A I L ringed in a row of letters.
        const char* row = "BSAILT";
        int cs = 64, x0 = (W - 6 * cs) / 2;
        line(x0 + cs + cs / 2, y + cs / 2, x0 + 4 * cs + cs / 2, y + cs / 2, (int)(cs * 0.78f) + 4, MID);
        line(x0 + cs + cs / 2, y + cs / 2, x0 + 4 * cs + cs / 2, y + cs / 2, (int)(cs * 0.78f), LIGHT);
        for (int i = 0; i < 6; i++)
            textCentered(std::string(1, row[i]), x0 + i * cs + cs / 2, y + (cs - lineHeight(36)) / 2, 36, BLACK, true);
        y += cs + 26;
        textWrapped("If the letters between your two taps spell a word on the list, either way round, it is "
                    "ringed in the grid and crossed off the list.",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    } else if (m_helpPage == 1) {
        text("Lines and letters", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Words run in straight lines. You may tap either end first. Letters can be shared by "
                        "two words where they cross.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 22;
        y = textWrapped("Changed your mind? Tap the black letter again to let it go.", MARGIN, y, tw, FONT_BODY, INK,
                        0, 10) + 22;
        textWrapped("A near miss is forgiven: if your second tap lands on the edge of the right letter, the word "
                    "still counts.",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    } else {
        text("Sizes and records", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        for (int i = 0; i < LEVEL_COUNT; i++) {
            text(kLevels[i].name, MARGIN, y, FONT_BODY, BLACK, true);
            y = textWrapped(std::to_string(kLevels[i].words) + " words in " + kLevels[i].detail + ".", MARGIN,
                            y + lineHeight(FONT_BODY) + 4, tw, SMALL, INK, 0, 6) + 16;
        }
        y += 6;
        textWrapped("Pick a theme on the title page, or let it surprise you. The clock runs only while you play "
                    "and waits if you step away. Your best time for each size is kept.",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    }
    GameKit::pageFooter(m_helpPage, 3);
}

void WordSearchApp::drawCapsule(int r0, int c0, int dr, int dc, int len, bool outline) {
    Rect g = gridRect();
    int cs = cellSize();
    int x0 = g.x + c0 * cs + cs / 2, y0 = g.y + r0 * cs + cs / 2;
    int x1 = x0 + dc * (len - 1) * cs, y1 = y0 + dr * (len - 1) * cs;
    int t = (int)(cs * 0.78f);
    // Pure black/white so it survives the fast partial refresh.
    if (outline) line(x0, y0, x1, y1, t + 6, BLACK);
    else line(x0, y0, x1, y1, t, WHITE);
}

void WordSearchApp::drawGrid() {
    const Games::WordSearch& p = m_puzzle;
    Rect g = gridRect();
    int cs = cellSize();
    // Rings first (all outlines, then all fills so crossings merge), letters on top.
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < p.count; i++) {
            const Games::WordSearch::Word& w = p.words[i];
            if (w.found) drawCapsule(w.row, w.col, w.dr, w.dc, w.len, pass == 0);
        }
    float fs = letterSize(p.side);
    int lh = lineHeight(fs);
    for (int i = 0; i < p.side * p.side; i++) {
        int cx = g.x + (i % p.side) * cs + cs / 2;
        int cy = g.y + (i / p.side) * cs + cs / 2;
        bool sel = i == m_first;
        if (sel) {
            fillCircle(cx, cy, cs * 47 / 100, BLACK);
            fillCircle(cx, cy, cs * 47 / 100 - 5, WHITE);
        }
        textCentered(std::string(1, p.cells[i]), cx, cy - lh / 2, fs, BLACK, true);
    }
}

void WordSearchApp::drawStatus() {
    int y = LINE_Y + (LINE_H - lineHeight(FONT_BODY)) / 2;
    std::string count = "Found " + std::to_string(m_puzzle.foundCount()) + " of " + std::to_string(m_puzzle.count);
    text(count, MARGIN, y, FONT_BODY, BLACK, true);
    if (!m_message.empty()) {
        textRight(m_message, W - MARGIN, y, FONT_BODY, DARK);
    } else if (m_level >= 0) {
        textRight(sizeName(m_level), W - MARGIN, y, FONT_BODY, DARK);
    }
}

void WordSearchApp::drawWordList(int top) {
    const Games::WordSearch& p = m_puzzle;
    hRule(top, LIGHT);
    top += 12;
    const float fs = SMALL;
    int maxW = 0;
    for (int i = 0; i < p.count; i++) {
        int tw = textWidth(p.text(i), fs);
        if (tw > maxW) maxW = tw;
    }
    int avail = W - 2 * MARGIN;
    int cols = (maxW + 16) * 3 <= avail ? 3 : 2;
    int rows = (p.count + cols - 1) / cols;
    int rh = (H - 12 - top) / (rows > 0 ? rows : 1);
    if (rh > 44) rh = 44;
    int cw = avail / cols;
    int lh = lineHeight(fs);
    // Alphabetical order makes the list easy to scan.
    int order[Games::WordSearch::MAX_WORDS];
    for (int i = 0; i < p.count; i++) order[i] = i;
    for (int i = 1; i < p.count; i++)
        for (int j = i; j > 0 && strcmp(p.text(order[j]), p.text(order[j - 1])) < 0; j--) {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
    for (int k = 0; k < p.count; k++) {
        int i = order[k];
        int col = k / rows, row = k % rows;
        int x = MARGIN + col * cw, y = top + row * rh + (rh - lh) / 2;
        const char* w = p.text(i);
        if (p.words[i].found) {
            text(w, x, y, fs, MID);
            int tw = textWidth(w, fs);
            fillRect(x - 2, y + lh / 2, tw + 4, 2, DARK);
        } else {
            text(w, x, y, fs, BLACK, true);
        }
    }
}

void WordSearchApp::drawDonePanel(int top) {
    hRule(top, LIGHT);
    int y = top + 14;
    const Games::WordTheme& T = Games::wordTheme(m_puzzle.theme);
    text("All " + std::to_string(m_puzzle.count) + " words found", MARGIN, y, FONT_TITLE - 2, BLACK, true);
    textRight(GameKit::clockText((int)m_clock.seconds), W - MARGIN, y, FONT_TITLE - 2, BLACK, true);
    y += lineHeight(FONT_TITLE - 2) + 6;
    std::string detail = std::string(T.name) + "  \xC2\xB7  " + sizeName(m_level);
    text(detail, MARGIN, y, SMALL, DARK);
    if (m_newBest) textRight("New best!", W - MARGIN, y, SMALL, BLACK, true);
    else if (m_best[m_level]) textRight("Best " + GameKit::clockText(m_best[m_level]), W - MARGIN, y, SMALL, DARK);
    y += lineHeight(SMALL) + 16;
    int perWord = (int)m_clock.seconds / (m_puzzle.count > 0 ? m_puzzle.count : 1);
    const char* remark = perWord < 8    ? "Eagle-eyed! Not a word escaped."
                         : perWord < 15 ? "Sharp work, not a letter wasted."
                         : perWord < 30 ? "Steady and sure: every word ringed."
                                        : "Patience rewarded: all found.";
    text(remark, MARGIN, y, FONT_BODY, INK);
    y += lineHeight(FONT_BODY) + 8;
    text(GameKit::plural(m_solved[m_level], "puzzle", "puzzles") + " of this size solved", MARGIN, y, SMALL, DARK);
    int by = H - 16 - 64;
    Rect again(MARGIN, by, (W - 2 * MARGIN - 16) * 3 / 5, 64);
    Rect done(again.x + again.w + 16, by, W - 2 * MARGIN - again.w - 16, 64);
    button(again, "Another puzzle", ButtonStyle::PRIMARY, FONT_BODY);
    button(done, "Title", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(again, A_AGAIN);
    addButton(done, A_TITLE);
}

void WordSearchApp::drawBoard() {
    header(Games::wordTheme(m_puzzle.theme).name, true, m_screen == BOARD ? "Rules" : "");
    drawStatus();
    drawGrid();
    Rect g = gridRect();
    strokeRoundRect(g.x - 8, g.y - 6, g.w + 16, g.h + 12, 12, 2, BLACK);
    int top = wordListTop();
    if (m_screen == DONE) drawDonePanel(top);
    else drawWordList(top);
}

int WordSearchApp::wordListTop() const {
    Rect g = gridRect();
    return g.y + g.h + 18;
}

// Combine changed board sub-regions into a single fast partial update so a
// tap never triggers multiple sequential EPD refresh cycles.
void WordSearchApp::refreshBoard(bool grid, bool status, bool words) {
    if (grid) {
        Rect g = gridRect();
        fillRect(g.x - 4, g.y - 2, g.w + 8, g.h + 4, WHITE);
        drawGrid();
        strokeRoundRect(g.x - 8, g.y - 6, g.w + 16, g.h + 12, 12, 2, BLACK);
    }
    if (status) {
        fillRect(0, LINE_Y, W, LINE_H, WHITE);
        drawStatus();
    }
    if (words) {
        int top = wordListTop();
        fillRect(0, top - 2, W, H - top + 2, WHITE);
        drawWordList(top);
    }
    DisplayHAL::updateScreenFast();
}
