#include "NumberPlaceApp.h"
#include "ui/UIFramework.h"
#include <cstring>

extern uint8_t* framebuffer;

namespace {
using namespace UI;
using GameKit::SMALL;

struct Level {
    const char* name;
    const char* detail;
};

const Level kLevels[NumberPlaceApp::LEVEL_COUNT] = {
    {"Gentle", "Plenty of clues; singles all the way"},
    {"Moderate", "Fewer clues; look for hidden singles"},
    {"Tough", "Pencil marks and sharper logic needed"},
};

enum Action {
    A_NONE = 0,
    A_LEVEL_0, A_LEVEL_1, A_LEVEL_2,
    A_CONTINUE, A_HELP,
    A_DIGIT_1 = 10,  // .. 18
    A_PEN = 20, A_PENCIL, A_ERASE, A_UNDO, A_HINT,
    A_AGAIN, A_TITLE,
};

// Board geometry (portrait 540 x 960)
constexpr int CELL = 56;
constexpr int GRID = CELL * 9;
constexpr int GX = (W - GRID) / 2;
constexpr int GY = HEADER_H + 14;
constexpr int KEY_Y = GY + GRID + 22;
constexpr int KEY_H = 84;
constexpr int KEY_GAP = 4;
constexpr int KEY_W = (GRID - 8 * KEY_GAP) / 9;
constexpr int TOOL_Y = KEY_Y + KEY_H + 20;
constexpr int TOOL_H = 64;
constexpr int STATUS_Y = TOOL_Y + TOOL_H + 20;
constexpr int CONTROLS_TOP = KEY_Y - 6;
constexpr size_t UNDO_DEPTH = 60;

int rowOf(int c) { return c / 9; }
int colOf(int c) { return c % 9; }
int boxOf(int c) { return (c / 27) * 3 + (c % 9) / 3; }

Rect cellRect(int c) { return Rect(GX + colOf(c) * CELL, GY + rowOf(c) * CELL, CELL, CELL); }

Rect keyRect(int d) { return Rect(GX + (d - 1) * (KEY_W + KEY_GAP), KEY_Y, KEY_W, KEY_H); }

const char* ordinalRemark(int level, int seconds) {
    int par[3] = {8 * 60, 15 * 60, 25 * 60};
    if (seconds < par[level] / 2) return "Lightning work. The compositor hasn't even inked the next page.";
    if (seconds < par[level]) return "A clean solve, in good time. Ready for the next issue.";
    return "Every number in its place. Logic, not luck.";
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void NumberPlaceApp::onCreate() {
    m_screen = TITLE;
    m_level = -1;
    load();
}

void NumberPlaceApp::onDestroy() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
}

void NumberPlaceApp::draw() {
    m_lastDrawn = -1;
    render();
}

bool NumberPlaceApp::onBack() {
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

void NumberPlaceApp::newPuzzle(int level) {
    Games::Sudoku s;
    s.generate(level, GameKit::randomSeed());
    memcpy(m_givens, s.givens, 81);
    memcpy(m_solution, s.solution, 81);
    memcpy(m_values, s.givens, 81);
    memset(m_marks, 0, sizeof(m_marks));
    m_level = level;
    m_finished = false;
    m_hints = 0;
    m_pencil = false;
    m_newBest = false;
    m_undo.clear();
    m_clock = GameKit::PlayClock();
    save();
    showBoard();
}

void NumberPlaceApp::showBoard() {
    m_screen = BOARD;
    m_sel = -1;
    m_message.clear();
    m_clock.start();
    render();
}

void NumberPlaceApp::leaveBoard() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
}

int NumberPlaceApp::filledCount() const {
    int n = 0;
    for (int i = 0; i < 81; i++) n += m_values[i] ? 1 : 0;
    return n;
}

int NumberPlaceApp::digitCount(int d) const {
    int n = 0;
    for (int i = 0; i < 81; i++) n += m_values[i] == d ? 1 : 0;
    return n;
}

bool NumberPlaceApp::conflicts(int cell) const {
    int v = m_values[cell];
    if (!v) return false;
    for (int i = 0; i < 81; i++) {
        if (i == cell || m_values[i] != v) continue;
        if (rowOf(i) == rowOf(cell) || colOf(i) == colOf(cell) || boxOf(i) == boxOf(cell)) return true;
    }
    return false;
}

void NumberPlaceApp::pushUndo() {
    if (m_undo.size() >= UNDO_DEPTH) m_undo.erase(m_undo.begin());
    Snapshot s;
    memcpy(s.values, m_values, sizeof(s.values));
    memcpy(s.marks, m_marks, sizeof(s.marks));
    m_undo.push_back(s);
}

void NumberPlaceApp::place(int cell, int d) {
    m_values[cell] = (uint8_t)d;
    m_marks[cell] = 0;
    if (!d) return;
    // Pencil marks for this digit in the same row, column and box go.
    for (int i = 0; i < 81; i++)
        if (rowOf(i) == rowOf(cell) || colOf(i) == colOf(cell) || boxOf(i) == boxOf(cell))
            m_marks[i] &= (uint16_t) ~(1 << d);
}

void NumberPlaceApp::tapCell(int cell) {
    m_clock.tick();
    int oldSel = m_sel;
    m_sel = cell == m_sel ? -1 : cell;
    bool hadMessage = !m_message.empty();
    m_message.clear();
    uint8_t oldVal = oldSel >= 0 ? m_values[oldSel] : 0;
    uint8_t newVal = m_sel >= 0 ? m_values[m_sel] : 0;
    uint16_t oldMarks = (m_pencil && oldSel >= 0 && !oldVal) ? m_marks[oldSel] : 0;
    uint16_t newMarks = (m_pencil && m_sel >= 0 && !newVal) ? m_marks[m_sel] : 0;
    bool oldErase = oldSel >= 0 && !m_givens[oldSel] && (m_values[oldSel] || m_marks[oldSel]);
    bool newErase = m_sel >= 0 && !m_givens[m_sel] && (m_values[m_sel] || m_marks[m_sel]);
    bool controlsChanged = hadMessage || oldVal != newVal || oldMarks != newMarks || oldErase != newErase;
    refreshBoard(true, controlsChanged);
}

void NumberPlaceApp::tapDigit(int d) {
    m_clock.tick();
    if (m_sel < 0) {
        m_message = "Tap a square first";
        refreshBoard(false, true);
        return;
    }
    if (m_givens[m_sel]) {
        m_message = "That number was printed";
        refreshBoard(false, true);
        return;
    }
    m_message.clear();
    pushUndo();
    if (m_pencil) {
        if (m_values[m_sel]) {
            m_undo.pop_back();
            m_message = "Erase the number first";
            refreshBoard(false, true);
            return;
        }
        m_marks[m_sel] ^= (uint16_t)(1 << d);
    } else {
        place(m_sel, m_values[m_sel] == d ? 0 : d);
    }
    afterChange(true);
}

void NumberPlaceApp::erase() {
    m_clock.tick();
    if (m_sel < 0 || m_givens[m_sel] || (!m_values[m_sel] && !m_marks[m_sel])) return;
    pushUndo();
    m_values[m_sel] = 0;
    m_marks[m_sel] = 0;
    m_message.clear();
    afterChange(true);
}

void NumberPlaceApp::undo() {
    m_clock.tick();
    if (m_undo.empty()) {
        m_message = "Nothing to undo";
        refreshBoard(false, true);
        return;
    }
    const Snapshot& s = m_undo.back();
    memcpy(m_values, s.values, sizeof(m_values));
    memcpy(m_marks, s.marks, sizeof(m_marks));
    m_undo.pop_back();
    m_message.clear();
    afterChange(true);
}

void NumberPlaceApp::hint() {
    m_clock.tick();
    // A wrong number is the most useful thing to point out.
    for (int i = 0; i < 81; i++) {
        if (!m_givens[i] && m_values[i] && m_values[i] != m_solution[i]) {
            m_hints++;
            m_sel = i;
            m_message = "This " + std::to_string(m_values[i]) + " is wrong";
            afterChange(true);
            return;
        }
    }
    // Otherwise fill the selected square, or the most constrained one.
    int target = (m_sel >= 0 && !m_values[m_sel]) ? m_sel : -1;
    if (target < 0) {
        int bestFree = 10;
        for (int i = 0; i < 81; i++) {
            if (m_values[i]) continue;
            uint16_t used = 0;
            for (int j = 0; j < 81; j++)
                if (m_values[j] && (rowOf(i) == rowOf(j) || colOf(i) == colOf(j) || boxOf(i) == boxOf(j)))
                    used |= (uint16_t)(1 << m_values[j]);
            int free = 0;
            for (int d = 1; d <= 9; d++) free += (used & (1 << d)) ? 0 : 1;
            if (free < bestFree) {
                bestFree = free;
                target = i;
            }
        }
    }
    if (target < 0) return;
    pushUndo();
    m_hints++;
    place(target, m_solution[target]);
    m_sel = target;
    m_message = "Hint: a " + std::to_string(m_solution[target]) + " goes here";
    afterChange(true);
}

void NumberPlaceApp::afterChange(bool gridChanged) {
    if (filledCount() == 81) {
        bool ok = true;
        for (int i = 0; i < 81 && ok; i++) ok = !conflicts(i);
        if (ok) {
            finish();
            return;
        }
        m_message = "Full, but some numbers clash";
    }
    refreshBoard(gridChanged, true);
    save();
}

void NumberPlaceApp::finish() {
    m_clock.stop();
    m_finished = true;
    m_solved[m_level]++;
    int t = (int)m_clock.seconds;
    if (t < 1) t = 1;
    m_newBest = m_hints == 0 && (m_best[m_level] == 0 || t < m_best[m_level]);
    if (m_newBest) m_best[m_level] = t;
    m_undo.clear();
    save();
    m_screen = RESULT;
    render();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// /data/numberplace.txt:
//   NP1 solved0 solved1 solved2 best0 best1 best2
//   G level seconds hints pencil givens solution values   (in progress only)
//   marks[81] as hex

void NumberPlaceApp::load() {
    FILE* f = GameKit::openData("numberplace.txt", "r");
    if (!f) return;
    int v[6];
    if (fscanf(f, " NP1 %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        for (int i = 0; i < LEVEL_COUNT; i++) {
            m_solved[i] = v[i] > 0 ? v[i] : 0;
            m_best[i] = v[3 + i] > 0 ? v[3 + i] : 0;
        }
    }
    int level, seconds, hints, pencil;
    char g[82], s[82], val[82];
    if (fscanf(f, " G %d %d %d %d %81s %81s %81s", &level, &seconds, &hints, &pencil, g, s, val) == 7 &&
        level >= 0 && level < LEVEL_COUNT && strlen(g) == 81 && strlen(s) == 81 && strlen(val) == 81) {
        bool ok = true;
        uint8_t gv[81], sv[81], vv[81];
        uint16_t mk[81];
        for (int i = 0; i < 81 && ok; i++) {
            gv[i] = (uint8_t)(g[i] - '0');
            sv[i] = (uint8_t)(s[i] - '0');
            vv[i] = (uint8_t)(val[i] - '0');
            ok = gv[i] <= 9 && sv[i] >= 1 && sv[i] <= 9 && vv[i] <= 9 && (!gv[i] || (gv[i] == sv[i] && vv[i] == gv[i]));
        }
        for (int i = 0; i < 81 && ok; i++) {
            unsigned m;
            ok = fscanf(f, " %x", &m) == 1;
            mk[i] = (uint16_t)(m & 0x3FE);
        }
        if (ok) {
            memcpy(m_givens, gv, 81);
            memcpy(m_solution, sv, 81);
            memcpy(m_values, vv, 81);
            memcpy(m_marks, mk, sizeof(mk));
            m_level = level;
            m_clock.seconds = seconds > 0 ? (uint32_t)seconds : 0;
            m_hints = hints > 0 ? hints : 0;
            m_pencil = pencil != 0;
            m_finished = false;
        }
    }
    fclose(f);
}

void NumberPlaceApp::save() {
    FILE* f = GameKit::openData("numberplace.txt", "w");
    if (!f) return;
    fprintf(f, "NP1 %d %d %d %d %d %d\n", m_solved[0], m_solved[1], m_solved[2], m_best[0], m_best[1], m_best[2]);
    if (m_level >= 0 && !m_finished) {
        char g[82], s[82], v[82];
        for (int i = 0; i < 81; i++) {
            g[i] = (char)('0' + m_givens[i]);
            s[i] = (char)('0' + m_solution[i]);
            v[i] = (char)('0' + m_values[i]);
        }
        g[81] = s[81] = v[81] = 0;
        fprintf(f, "G %d %u %d %d %s %s %s\n", m_level, (unsigned)m_clock.seconds, m_hints, m_pencil ? 1 : 0, g, s, v);
        for (int i = 0; i < 81; i++) fprintf(f, "%x%c", m_marks[i], (i % 9 == 8) ? '\n' : ' ');
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

void NumberPlaceApp::addButton(const Rect& r, int action) { m_buttons.push_back({r, action}); }

void NumberPlaceApp::handleTouch(int x, int y) {
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
        if (x >= GX && x < GX + GRID && y >= GY && y < GY + GRID) {
            tapCell(((y - GY) / CELL) * 9 + (x - GX) / CELL);
            return;
        }
    }
    int action = A_NONE;
    for (const Button& b : m_buttons)
        if (b.rect.contains(x, y)) {
            action = b.action;
            break;
        }
    if (m_screen == TITLE && hitHeaderAction(x, y, "How to play")) action = A_HELP;
    if (m_screen == BOARD && action >= A_DIGIT_1 && action < A_DIGIT_1 + 9) {
        tapDigit(action - A_DIGIT_1 + 1);
        return;
    }
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
        case A_PEN:
        case A_PENCIL:
            if (m_pencil != (action == A_PENCIL)) {
                m_pencil = action == A_PENCIL;
                m_message.clear();
                refreshBoard(false, true);
            }
            break;
        case A_ERASE:
            erase();
            break;
        case A_UNDO:
            undo();
            break;
        case A_HINT:
            hint();
            break;
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
// Drawing
// ---------------------------------------------------------------------------

void NumberPlaceApp::render() {
    m_buttons.clear();
    auto content = [this]() {
        switch (m_screen) {
            case TITLE: drawTitle(); break;
            case HELP: drawHelp(); break;
            case BOARD: drawBoard(); break;
            case RESULT: drawResult(); break;
        }
    };
    UIFramework::performFullScreenDraw(framebuffer, content);
    m_lastDrawn = m_screen;
}

void NumberPlaceApp::drawTitle() {
    header("Number Place", true, "How to play");

    // Emblem: one box of the grid.
    const int ec = 46, ew = ec * 3;
    int ex = (W - ew) / 2, ey = HEADER_H + 22;
    const char* digits[9] = {"5", "", "3", "", "1", "", "7", "", "9"};
    fillRect(ex, ey, ew, ew, WHITE);
    for (int i = 0; i < 9; i++) {
        bool dark = i == 4;
        if (dark) fillRect(ex + ec, ey + ec, ec, ec, BLACK);
        if (*digits[i])
            textCentered(digits[i], ex + (i % 3) * ec + ec / 2, ey + (i / 3) * ec + (ec - lineHeight(30)) / 2, 30,
                         dark ? WHITE : BLACK, true);
    }
    for (int i = 1; i < 3; i++) {
        fillRect(ex + i * ec, ey, 1, ew, DARK);
        fillRect(ex, ey + i * ec, ew, 1, DARK);
    }
    strokeRoundRect(ex - 2, ey - 2, ew + 4, ew + 4, 4, 3, BLACK);

    int y = ey + ew + 22;
    y = textWrapped("New York, 1979. In the back pages of a puzzle magazine appears something new: nine boxes, "
                    "a scatter of numbers and no arithmetic at all. They call it Number Place.",
                    MARGIN + 4, y, W - 2 * MARGIN - 8, SMALL, INK, 0, 7, true);

    y += 16;
    GameKit::sectionLabel(y, "Best times");
    y += 32;
    int cw = (W - 2 * MARGIN) / LEVEL_COUNT;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        int x = MARGIN + i * cw;
        text(m_best[i] ? GameKit::clockText(m_best[i]) : std::string("\xE2\x80\x93"), x, y, FONT_TITLE, BLACK, true);
        std::string cap = kLevels[i].name;
        if (m_solved[i]) cap += "  \xC2\xB7  " + std::to_string(m_solved[i]);
        text(cap, x, y + lineHeight(FONT_TITLE) + 2, SMALL, DARK);
    }
    y += lineHeight(FONT_TITLE) + lineHeight(SMALL) + 22;

    bool inProgress = m_level >= 0 && !m_finished;
    if (inProgress) {
        Rect c(MARGIN, y, W - 2 * MARGIN, 80);
        std::string detail = std::string(kLevels[m_level].name) + "  \xC2\xB7  " + std::to_string(filledCount()) +
                             " of 81  \xC2\xB7  " + GameKit::clockText((int)m_clock.seconds);
        GameKit::optionButton(c, "Continue", detail, true);
        addButton(c, A_CONTINUE);
        y += 96;
    } else {
        y += 10;
    }
    GameKit::sectionLabel(y, inProgress ? "New puzzle (replaces the one in progress)" : "New puzzle");
    y += 34;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        Rect r(MARGIN, y + i * 96, W - 2 * MARGIN, 84);
        GameKit::optionButton(r, kLevels[i].name, kLevels[i].detail, !inProgress && i == 0);
        addButton(r, A_LEVEL_0 + i);
    }
}

void NumberPlaceApp::drawHelp() {
    header("How to play", true, "Done");
    const int tw = W - 2 * MARGIN;
    int y = HEADER_H + 30;
    if (m_helpPage == 0) {
        text("One of each, everywhere", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Fill the grid so that every row, every column and every 3 \xC3\x97 3 box holds the numbers 1 "
                        "to 9, once each.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        y = textWrapped("The printed numbers (on grey) are fixed. Every puzzle has exactly one solution, and it "
                        "can always be found by reasoning alone.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        textWrapped("Tap a square, then a number below the grid. Tap the same number again to take it out.", MARGIN,
                    y, tw, FONT_BODY, INK, 0, 10);
    } else if (m_helpPage == 1) {
        text("Pencil marks", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Switch to Pencil to jot small candidate numbers in a square; tap a number again to rub "
                        "it out. Placing a number in Pen clears that number's pencil marks from its row, column "
                        "and box.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 24;
        // Example square with pencil marks.
        Rect r((W - 3 * CELL) / 2, y, 3 * CELL, 3 * CELL);
        strokeRoundRect(r.x, r.y, r.w, r.h, 6, 3, BLACK);
        const int marks[] = {1, 4, 6, 9};
        for (int m : marks) {
            int cx = r.x + ((m - 1) % 3) * CELL + CELL / 2, cy = r.y + ((m - 1) / 3) * CELL + CELL / 2;
            textCentered(std::to_string(m), cx, cy - lineHeight(36) / 2, 36, DARK);
        }
        y += r.h + 24;
        textWrapped("Numbers that clash with another in their row, column or box are ringed.", MARGIN, y, tw,
                    FONT_BODY, INK, 0, 10);
    } else {
        text("Help and records", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Undo steps back through your moves. Hint points out a wrong number if there is one; "
                        "otherwise it fills the selected square, or the one with fewest possibilities.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        y = textWrapped("The clock runs only while you play. Best times count only puzzles solved without hints.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        textWrapped("Gentle puzzles fall to singles. Moderate needs hidden singles: the only square in a row, "
                    "column or box where a number fits. Tough needs pencil marks.",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    }
    GameKit::pageFooter(m_helpPage, 3);
}

void NumberPlaceApp::drawGrid() {
    const float big = 36.0f, small = 24.0f;
    int lhBig = lineHeight(big), lhSmall = lineHeight(small);
    for (int i = 0; i < 81; i++) {
        Rect r = cellRect(i);
        bool sel = i == m_sel;
        if (m_givens[i]) fillRect(r.x, r.y, r.w, r.h, PALE);
        if (sel) {
            fillRect(r.x + 1, r.y + 1, r.w - 1, r.h - 1, LIGHT);
            strokeRoundRect(r.x + 1, r.y + 1, r.w - 1, r.h - 1, 3, 3, BLACK);
        }
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        if (m_values[i]) {
            if (!m_givens[i] && conflicts(i)) {
                fillCircle(cx, cy, 24, BLACK);
                fillCircle(cx, cy, 21, sel ? LIGHT : WHITE);
            }
            textCentered(std::to_string(m_values[i]), cx, cy - lhBig / 2, big, m_givens[i] ? BLACK : INK, m_givens[i]);
        } else if (m_marks[i]) {
            for (int d = 1; d <= 9; d++) {
                if (!(m_marks[i] & (1 << d))) continue;
                int mx = r.x + 12 + ((d - 1) % 3) * 16, my = r.y + 11 + ((d - 1) / 3) * 16;
                textCentered(std::to_string(d), mx, my - lhSmall / 2, small, BLACK);
            }
        }
    }
    for (int i = 0; i <= 9; i++) {
        bool thick = i % 3 == 0;
        int t = thick ? 3 : 1;
        uint8_t c = thick ? BLACK : MID;
        fillRect(GX + i * CELL - (thick ? 1 : 0), GY, t, GRID + 1, c);
        fillRect(GX, GY + i * CELL - (thick ? 1 : 0), GRID + 1, t, c);
    }
}

void NumberPlaceApp::drawControls(bool addHits) {
    for (int d = 1; d <= 9; d++) {
        Rect r = keyRect(d);
        bool done = digitCount(d) >= 9;
        bool marked = m_sel >= 0 && !m_values[m_sel] && (m_marks[m_sel] & (1 << d));
        bool current = m_sel >= 0 && m_values[m_sel] == d;
        if (current || (m_pencil && marked)) {
            fillRoundRect(r.x, r.y, r.w, r.h, 12, BLACK);
            textCentered(std::to_string(d), r.x + r.w / 2, r.y + (r.h - lineHeight(36)) / 2, 36, WHITE, true);
        } else {
            fillRoundRect(r.x, r.y, r.w, r.h, 12, done ? PALE : WHITE);
            strokeRoundRect(r.x, r.y, r.w, r.h, 12, 2, done ? LIGHT : BLACK);
            textCentered(std::to_string(d), r.x + r.w / 2, r.y + (r.h - lineHeight(36)) / 2, 36, done ? MID : BLACK,
                         true);
        }
        if (addHits) addButton(Rect(r.x - 2, r.y - 8, r.w + 4, r.h + 14), A_DIGIT_1 + d - 1);
    }
    // Pen | Pencil, Erase, Undo
    Rect seg(GX, TOOL_Y, 216, TOOL_H);
    segmented(seg, {"Pen", "Pencil"}, m_pencil ? 1 : 0, FONT_BODY - 2);
    int bw = (GRID - seg.w - 24) / 2;
    Rect er(seg.x + seg.w + 12, TOOL_Y, bw, TOOL_H);
    Rect un(er.x + bw + 12, TOOL_Y, GRID - seg.w - 24 - bw, TOOL_H);
    bool canErase = m_sel >= 0 && !m_givens[m_sel] && (m_values[m_sel] || m_marks[m_sel]);
    button(er, "Erase", ButtonStyle::SECONDARY, FONT_BODY - 2, canErase);
    button(un, "Undo", ButtonStyle::SECONDARY, FONT_BODY - 2, !m_undo.empty());
    Rect hi(GX + GRID - 140, STATUS_Y, 140, 60);
    button(hi, "Hint", ButtonStyle::SECONDARY, FONT_BODY - 2);
    int ty = STATUS_Y + (60 - lineHeight(FONT_BODY)) / 2;
    if (!m_message.empty()) {
        text(ellipsize(m_message, hi.x - GX - 12, FONT_BODY), GX, ty, FONT_BODY, BLACK, true);
    } else {
        std::string status = std::to_string(filledCount()) + " of 81 filled";
        if (m_hints) status += "  \xC2\xB7  " + GameKit::plural(m_hints, "hint", "hints");
        text(ellipsize(status, hi.x - GX - 12, FONT_BODY), GX, ty, FONT_BODY, DARK);
    }
    if (addHits) {
        addButton(Rect(seg.x, seg.y, seg.w / 2, seg.h), A_PEN);
        addButton(Rect(seg.x + seg.w / 2, seg.y, seg.w / 2, seg.h), A_PENCIL);
        addButton(er, A_ERASE);
        addButton(un, A_UNDO);
        addButton(hi, A_HINT);
    }
}

void NumberPlaceApp::drawBoard() {
    header(kLevels[m_level].name, true, "Rules");
    drawGrid();
    drawControls(true);
}

void NumberPlaceApp::refreshBoard(bool grid, bool controls) {
    if (grid) {
        fillRect(GX - 2, GY - 2, GRID + 5, GRID + 5, WHITE);
        drawGrid();
    }
    if (controls) {
        fillRect(0, CONTROLS_TOP, W, H - CONTROLS_TOP, WHITE);
        drawControls(false);
    }
    DisplayHAL::updateScreenFast();
}

void NumberPlaceApp::drawMiniGrid(int x, int y, int cs) {
    int lh = lineHeight(24);
    for (int i = 0; i < 81; i++) {
        int cx = x + colOf(i) * cs + cs / 2, cy = y + rowOf(i) * cs + cs / 2;
        if (m_givens[i]) fillRect(x + colOf(i) * cs, y + rowOf(i) * cs, cs, cs, PALE);
        textCentered(std::to_string(m_values[i]), cx, cy - lh / 2, 24, m_givens[i] ? BLACK : DARK, m_givens[i]);
    }
    for (int i = 0; i <= 9; i++) {
        bool thick = i % 3 == 0;
        fillRect(x + i * cs - (thick ? 1 : 0), y, thick ? 3 : 1, 9 * cs + 1, thick ? BLACK : LIGHT);
        fillRect(x, y + i * cs - (thick ? 1 : 0), 9 * cs + 1, thick ? 3 : 1, thick ? BLACK : LIGHT);
    }
}

void NumberPlaceApp::drawResult() {
    header("Solved", true);
    int y = HEADER_H + 22;
    text("Every number in its place", MARGIN, y, FONT_TITLE - 2, BLACK, true);
    y += lineHeight(FONT_TITLE - 2) + 18;
    const int cs = 40;
    drawMiniGrid((W - 9 * cs) / 2, y, cs);
    y += 9 * cs + 24;

    text(GameKit::clockText((int)m_clock.seconds), MARGIN, y, 52, BLACK, true);
    int ty = y + 4;
    textRight(kLevels[m_level].name, W - MARGIN, ty, FONT_BODY, BLACK, true);
    ty += lineHeight(FONT_BODY) + 4;
    if (m_newBest) textRight("New best time!", W - MARGIN, ty, SMALL, BLACK, true);
    else if (m_hints) textRight("With " + GameKit::plural(m_hints, "hint", "hints"), W - MARGIN, ty, SMALL, DARK);
    else if (m_best[m_level]) textRight("Best " + GameKit::clockText(m_best[m_level]), W - MARGIN, ty, SMALL, DARK);
    y += lineHeight(52) + 14;
    const char* remark = m_hints ? "Solved, with a glance or two at the answers page."
                                 : ordinalRemark(m_level, (int)m_clock.seconds);
    y = textWrapped(remark, MARGIN, y, W - 2 * MARGIN, FONT_BODY, INK, 2, 6) + 6;
    text(GameKit::plural(m_solved[m_level], "puzzle", "puzzles") + " solved at this level", MARGIN, y, SMALL, DARK);

    int by = H - 16 - 64;
    Rect again(MARGIN, by, (W - 2 * MARGIN - 16) * 3 / 5, 64);
    Rect done(again.x + again.w + 16, by, W - 2 * MARGIN - again.w - 16, 64);
    button(again, "Another puzzle", ButtonStyle::PRIMARY, FONT_BODY);
    button(done, "Title", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(again, A_AGAIN);
    addButton(done, A_TITLE);
}
