#include "CrevasseApp.h"
#include "ui/UIFramework.h"
#include <cstring>

extern uint8_t* framebuffer;

namespace {
using namespace UI;
using GameKit::SMALL;

struct Level {
    const char* name;
    int w, h, crevasses, cell;
    const char* detail;
};

const Level kLevels[CrevasseApp::LEVEL_COUNT] = {
    {"The Barrier", 8, 10, 10, 60, "8 \xC3\x97 10  \xC2\xB7  10 crevasses on the ice shelf"},
    {"The Plateau", 9, 12, 20, 56, "9 \xC3\x97 12  \xC2\xB7  20 crevasses under the sastrugi"},
    {"Beardmore Glacier", 9, 13, 28, 54, "9 \xC3\x97 13  \xC2\xB7  28 crevasses: a maze"},
};

const char* kWinStories[] = {
    "The depot flag stands on the skyline. Your party sledges in, every man and dog accounted for.",
    "Across without a stumble. The ponies are fed, the tent is pitched and the cocoa is on.",
    "The last crevasse is flagged and the way is clear. The Polar Party will find its stores waiting.",
};
const char* kLossStories[] = {
    "A snow bridge gave way beneath the sledge. The team hauled it out by the traces, but the depot must wait.",
    "Down went the lead dog into the blue dark. He came up shaken but unhurt; the crossing is off for today.",
    "The crust broke with a crack like a rifle shot. Roped together, you all climbed out and turned back.",
};

enum Action {
    A_NONE = 0,
    A_LEVEL_0, A_LEVEL_1, A_LEVEL_2,
    A_CONTINUE, A_HELP, A_PROBE, A_FLAG, A_AGAIN, A_TITLE,
};

constexpr int LINE_Y = HEADER_H + 6;
constexpr int LINE_H = 44;
constexpr int FIELD_Y = HEADER_H + 54;
constexpr int TOOL_H = 64;
constexpr int TOOL_Y = H - 16 - TOOL_H;

void crack(int x, int y, int w, int h, int t, uint8_t color) {
    int px[4] = {x + w * 12 / 100, x + w * 38 / 100, x + w * 62 / 100, x + w * 88 / 100};
    int py[4] = {y + h * 62 / 100, y + h * 36 / 100, y + h * 64 / 100, y + h * 38 / 100};
    for (int i = 0; i < 3; i++) line(px[i], py[i], px[i + 1], py[i + 1], t, color);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void CrevasseApp::onCreate() {
    m_screen = TITLE;
    m_level = -1;
    load();
}

void CrevasseApp::onDestroy() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
}

void CrevasseApp::draw() {
    m_lastDrawn = -1;
    render();
}

bool CrevasseApp::onBack() {
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

void CrevasseApp::abandon() {
    // Turning back from a field already set foot on counts as a failed crossing.
    if (m_level >= 0 && !m_finished && m_field.laid) m_played[m_level]++;
    m_level = -1;
}

void CrevasseApp::newField(int level) {
    abandon();
    const Level& L = kLevels[level];
    m_field.setup(L.w, L.h, L.crevasses);
    m_level = level;
    m_finished = false;
    m_won = false;
    m_flagMode = false;
    m_newBest = false;
    m_clock = GameKit::PlayClock();
    save();
    showBoard();
}

void CrevasseApp::showBoard() {
    m_screen = BOARD;
    m_message.clear();
    m_clock.start();
    render();
}

void CrevasseApp::leaveBoard() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
}

void CrevasseApp::tapCell(int cell) {
    m_clock.tick();
    Games::Minefield& f = m_field;
    std::string before = m_message;
    int flagsBefore = f.flags();
    m_message.clear();
    int result = 0;
    if (!f.laid) {
        if (m_flagMode) {
            m_message = "Probe first: the first step is safe";
        } else {
            f.lay(cell, GameKit::randomSeed(), true);
            result = f.probe(cell);
        }
    } else if (f.state[cell] == Games::Minefield::OPEN) {
        result = f.chord(cell);
        if (result == 0 && f.adj[cell] > 0) {
            int nb[8], n = f.neighbours(cell, nb), hidden = 0;
            for (int i = 0; i < n; i++) hidden += f.state[nb[i]] == Games::Minefield::HIDDEN ? 1 : 0;
            if (hidden) m_message = "Flag its " + GameKit::plural(f.adj[cell], "crevasse", "crevasses") + " first";
        }
    } else if (m_flagMode) {
        f.toggleFlag(cell);
    } else if (f.state[cell] == Games::Minefield::FLAGGED) {
        m_message = "Flagged. Use Flag to lift it";
    } else {
        result = f.probe(cell);
    }
    if (result < 0) {
        finish(false);
        return;
    }
    if (f.won()) {
        finish(true);
        return;
    }
    save();
    refreshField();
    if (m_message != before || f.flags() != flagsBefore) refreshStatus();
}

void CrevasseApp::finish(bool won) {
    m_clock.stop();
    m_finished = true;
    m_won = won;
    m_played[m_level]++;
    m_newBest = false;
    if (won) {
        m_wins[m_level]++;
        int t = (int)m_clock.seconds;
        if (t < 1) t = 1;
        m_newBest = m_best[m_level] == 0 || t < m_best[m_level];
        if (m_newBest) m_best[m_level] = t;
    }
    m_story = (int)(GameKit::randomSeed() % 3);
    save();
    m_screen = RESULT;
    render();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// /data/crevasse.txt:
//   CV1 played0 played1 played2 wins0 wins1 wins2 best0 best1 best2
//   G level seconds laid flagMode cells     (field in progress only; one
//   letter per cell: 'a' + state, + 4 if a crevasse)

void CrevasseApp::load() {
    FILE* f = GameKit::openData("crevasse.txt", "r");
    if (!f) return;
    int v[9];
    if (fscanf(f, " CV1 %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
               &v[8]) == 9) {
        for (int i = 0; i < LEVEL_COUNT; i++) {
            m_played[i] = v[i] > 0 ? v[i] : 0;
            m_wins[i] = v[3 + i] > 0 ? v[3 + i] : 0;
            m_best[i] = v[6 + i] > 0 ? v[6 + i] : 0;
        }
    }
    int level, seconds, laid, flagMode;
    char cells[Games::Minefield::MAX_CELLS + 1];
    if (fscanf(f, " G %d %d %d %d %117s", &level, &seconds, &laid, &flagMode, cells) == 5 && level >= 0 &&
        level < LEVEL_COUNT) {
        const Level& L = kLevels[level];
        Games::Minefield fl;
        fl.setup(L.w, L.h, L.crevasses);
        bool ok = (int)strlen(cells) == L.w * L.h;
        int mines = 0;
        for (int i = 0; i < L.w * L.h && ok; i++) {
            int c = cells[i] - 'a';
            ok = c >= 0 && c < 8 && (c & 3) <= 2;
            fl.mine[i] = (c & 4) != 0;
            fl.state[i] = (uint8_t)(c & 3);
            mines += fl.mine[i] ? 1 : 0;
            if (fl.mine[i] && fl.state[i] == Games::Minefield::OPEN) ok = false;
        }
        if (ok && laid && mines == L.crevasses) {
            int nb[8];
            for (int i = 0; i < L.w * L.h; i++) {
                int n = fl.neighbours(i, nb), a = 0;
                for (int j = 0; j < n; j++) a += fl.mine[nb[j]] ? 1 : 0;
                fl.adj[i] = (uint8_t)a;
            }
            fl.laid = true;
        } else if (ok && !laid) {
            fl.setup(L.w, L.h, L.crevasses);
        } else {
            ok = false;
        }
        if (ok) {
            m_field = fl;
            m_level = level;
            m_clock.seconds = seconds > 0 ? (uint32_t)seconds : 0;
            m_flagMode = flagMode != 0;
            m_finished = false;
        }
    }
    fclose(f);
}

void CrevasseApp::save() {
    FILE* f = GameKit::openData("crevasse.txt", "w");
    if (!f) return;
    fprintf(f, "CV1 %d %d %d %d %d %d %d %d %d\n", m_played[0], m_played[1], m_played[2], m_wins[0], m_wins[1],
            m_wins[2], m_best[0], m_best[1], m_best[2]);
    if (m_level >= 0 && !m_finished) {
        const Games::Minefield& fl = m_field;
        std::string cells;
        for (int i = 0; i < fl.w * fl.h; i++) cells += (char)('a' + fl.state[i] + (fl.mine[i] ? 4 : 0));
        fprintf(f, "G %d %u %d %d %s\n", m_level, (unsigned)m_clock.seconds, fl.laid ? 1 : 0, m_flagMode ? 1 : 0,
                cells.c_str());
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

void CrevasseApp::addButton(const Rect& r, int action) { m_buttons.push_back({r, action}); }

int CrevasseApp::cellSize() const { return kLevels[m_level < 0 ? 0 : m_level].cell; }

Rect CrevasseApp::gridRect() const {
    int cs = cellSize();
    return Rect((W - cs * m_field.w) / 2, FIELD_Y, cs * m_field.w, cs * m_field.h);
}

void CrevasseApp::handleTouch(int x, int y) {
    if (m_screen == HELP) {
        if (hitHeaderAction(x, y, "Done")) {
            onBack();
            return;
        }
        int step = GameKit::hitPageFooter(x, y, m_helpPage, 3);
        if (step) {
            m_helpPage += step;
            render();
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
        if (g.contains(x, y)) {
            int cs = cellSize();
            tapCell(((y - g.y) / cs) * m_field.w + (x - g.x) / cs);
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
    switch (action) {
        case A_LEVEL_0:
        case A_LEVEL_1:
        case A_LEVEL_2:
            newField(action - A_LEVEL_0);
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
        case A_PROBE:
        case A_FLAG:
            if (m_flagMode != (action == A_FLAG)) {
                m_flagMode = action == A_FLAG;
                m_clock.tick();
                UIFramework::performFastPartialUpdate(framebuffer, 0, TOOL_Y - 4, W, TOOL_H + 8,
                                                      [this]() { drawTools(false); });
            }
            break;
        case A_AGAIN:
            newField(m_level >= 0 ? m_level : 0);
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

void CrevasseApp::render() {
    m_buttons.clear();
    auto content = [this]() {
        switch (m_screen) {
            case TITLE: drawTitle(); break;
            case HELP: drawHelp(); break;
            case BOARD: drawBoard(); break;
            case RESULT: drawResult(); break;
        }
    };
    if (m_screen != m_lastDrawn) UIFramework::performFullScreenDraw(framebuffer, content);
    else UIFramework::performQuickFullScreenDraw(framebuffer, content);
    m_lastDrawn = m_screen;
}

void CrevasseApp::drawTitle() {
    header("Crevasse 1911", true, "How to play");

    // Emblem: a strip of snow with a flag planted on a crevasse.
    const int ts = 56, n = 5;
    int ex = (W - n * ts) / 2, ey = HEADER_H + 24;
    for (int i = 0; i < n; i++) {
        Rect r(ex + i * ts, ey, ts, ts);
        if (i == 0 || i == 1) {
            fillRect(r.x, r.y, r.w, r.h, WHITE);
            strokeRoundRect(r.x, r.y, r.w, r.h, 0, 1, LIGHT);
            if (i == 1) textCentered("1", r.x + ts / 2, r.y + (ts - lineHeight(32)) / 2, 32, BLACK, true);
        } else {
            fillRect(r.x, r.y, r.w, r.h, LIGHT);
            fillRect(r.x, r.y, r.w, 2, WHITE);
            fillRect(r.x, r.y, 2, r.h, WHITE);
            fillRect(r.x, r.y + r.h - 2, r.w, 2, MID);
            fillRect(r.x + r.w - 2, r.y, 2, r.h, MID);
            if (i == 2) GameKit::pennant(r.x + ts * 2 / 5, r.y + ts * 4 / 5, ts * 3 / 5);
        }
    }
    strokeRoundRect(ex - 3, ey - 3, n * ts + 6, ts + 6, 4, 3, BLACK);

    int y = ey + ts + 26;
    y = textWrapped("Antarctica, 1911. Your sledge party must cross the ice to lay a depot, but any step may "
                    "hide a crevasse under a crust of snow. Sound the way and flag the danger.",
                    MARGIN + 4, y, W - 2 * MARGIN - 8, SMALL, INK, 0, 7, true);

    y += 16;
    GameKit::sectionLabel(y, "Crossings");
    y += 32;
    int cw = (W - 2 * MARGIN) / LEVEL_COUNT;
    const char* shortNames[LEVEL_COUNT] = {"Barrier", "Plateau", "Glacier"};
    for (int i = 0; i < LEVEL_COUNT; i++) {
        int x = MARGIN + i * cw;
        std::string v = m_played[i] ? std::to_string(m_wins[i]) + "/" + std::to_string(m_played[i]) : "\xE2\x80\x93";
        text(v, x, y, FONT_TITLE, BLACK, true);
        std::string cap = shortNames[i];
        if (m_best[i]) cap += "  " + GameKit::clockText(m_best[i]);
        text(cap, x, y + lineHeight(FONT_TITLE) + 2, SMALL, DARK);
    }
    y += lineHeight(FONT_TITLE) + lineHeight(SMALL) + 22;

    bool inProgress = m_level >= 0 && !m_finished;
    if (inProgress) {
        Rect c(MARGIN, y, W - 2 * MARGIN, 80);
        int open = 0, safe = m_field.w * m_field.h - m_field.mines;
        for (int i = 0; i < m_field.w * m_field.h; i++) open += m_field.state[i] == Games::Minefield::OPEN ? 1 : 0;
        std::string detail = std::string(kLevels[m_level].name) + "  \xC2\xB7  " +
                             std::to_string(open * 100 / (safe > 0 ? safe : 1)) + "% across";
        GameKit::optionButton(c, "Continue", detail, true);
        addButton(c, A_CONTINUE);
        y += 96;
    } else {
        y += 10;
    }
    GameKit::sectionLabel(y, inProgress ? "New field (abandons this crossing)" : "Choose your route");
    y += 34;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        Rect r(MARGIN, y + i * 96, W - 2 * MARGIN, 84);
        GameKit::optionButton(r, kLevels[i].name, kLevels[i].detail, !inProgress && i == 0);
        addButton(r, A_LEVEL_0 + i);
    }
}

void CrevasseApp::drawHelp() {
    header("How to play", true, "Done");
    const int tw = W - 2 * MARGIN;
    int y = HEADER_H + 30;
    if (m_helpPage == 0) {
        text("Sounding the snow", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Tap a square of snow to probe it. Your first probe is always safe and opens a patch "
                        "of firm ice.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        y = textWrapped("A number tells how many of the eight squares around it hide a crevasse.", MARGIN, y, tw,
                        FONT_BODY, INK, 0, 10) + 24;
        // Example: a 3 x 3 patch, a 1 in the middle with one crevasse beside it.
        const int cs = 64;
        int x0 = (W - 3 * cs) / 2;
        for (int i = 0; i < 9; i++) {
            Rect r(x0 + (i % 3) * cs, y + (i / 3) * cs, cs, cs);
            bool hidden = i == 2 || i == 5 || i == 8;
            if (hidden) {
                fillRect(r.x, r.y, r.w, r.h, LIGHT);
                fillRect(r.x, r.y, r.w, 2, WHITE);
                fillRect(r.x, r.y + r.h - 2, r.w, 2, MID);
                fillRect(r.x + r.w - 2, r.y, 2, r.h, MID);
            } else {
                strokeRoundRect(r.x, r.y, r.w, r.h, 0, 1, LIGHT);
            }
            if (i == 5) GameKit::pennant(r.x + cs * 2 / 5, r.y + cs * 4 / 5, cs * 3 / 5);
            const char* num = i == 1 ? "1" : (i == 4 ? "1" : (i == 7 ? "1" : ""));
            if (*num) textCentered(num, r.x + cs / 2, r.y + (cs - lineHeight(34)) / 2, 34, BLACK, true);
        }
        y += 3 * cs + 16;
        textWrapped("Three 1s, three hidden squares: the crevasse must be the middle one.", MARGIN, y, tw,
                    FONT_BODY, INK, 0, 10);
    } else if (m_helpPage == 1) {
        text("Flags and short cuts", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Switch to Flag at the bottom of the screen to plant a pennant on a square you know is a "
                        "crevasse; tap it again to lift it. Flagged squares are never probed by accident.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        textWrapped("Tap a number whose crevasses are all flagged to probe all of its other neighbours at once. "
                    "Be sure of your flags!",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    } else {
        text("Routes and records", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        for (int i = 0; i < LEVEL_COUNT; i++) {
            text(kLevels[i].name, MARGIN, y, FONT_BODY, BLACK, true);
            y = textWrapped(kLevels[i].detail, MARGIN, y + lineHeight(FONT_BODY) + 4, tw, SMALL, INK, 0, 6) + 14;
        }
        y += 8;
        textWrapped("Every field can be crossed by reasoning alone; you never have to guess. Clear every safe "
                    "square to reach the depot. Turning back part-way counts as a failed crossing.",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    }
    GameKit::pageFooter(m_helpPage, 3);
}

void CrevasseApp::drawCell(const Rect& r, int cell, bool reveal) {
    const Games::Minefield& f = m_field;
    uint8_t st = f.state[cell];
    bool mine = f.laid && f.mine[cell];
    if (st == Games::Minefield::OPEN && !mine) {
        fillRect(r.x, r.y, r.w, r.h, WHITE);
        strokeRoundRect(r.x, r.y, r.w, r.h, 0, 1, LIGHT);
        if (f.adj[cell]) {
            float fs = r.w >= 54 ? 34.0f : (r.w >= 44 ? 30.0f : 24.0f);
            textCentered(std::to_string(f.adj[cell]), r.x + r.w / 2, r.y + (r.h - lineHeight(fs)) / 2, fs, BLACK,
                         true);
        }
        return;
    }
    if (reveal && mine) {
        bool fatal = cell == f.exploded;
        fillRect(r.x, r.y, r.w, r.h, fatal ? BLACK : WHITE);
        strokeRoundRect(r.x, r.y, r.w, r.h, 0, 1, LIGHT);
        crack(r.x, r.y, r.w, r.h, r.w / 6, fatal ? WHITE : BLACK);
        if (st == Games::Minefield::FLAGGED) GameKit::pennant(r.x + r.w / 8, r.y + r.h * 9 / 20, r.h * 2 / 5, DARK);
        return;
    }
    // Untouched snow: a raised block.
    fillRect(r.x, r.y, r.w, r.h, LIGHT);
    fillRect(r.x, r.y, r.w, 2, WHITE);
    fillRect(r.x, r.y, 2, r.h, WHITE);
    fillRect(r.x, r.y + r.h - 2, r.w, 2, MID);
    fillRect(r.x + r.w - 2, r.y, 2, r.h, MID);
    if (st == Games::Minefield::FLAGGED) {
        GameKit::pennant(r.x + r.w * 2 / 5, r.y + r.h * 4 / 5, r.h * 3 / 5);
        if (reveal && !mine) {
            line(r.x + 8, r.y + 8, r.x + r.w - 8, r.y + r.h - 8, 4, BLACK);
            line(r.x + r.w - 8, r.y + 8, r.x + 8, r.y + r.h - 8, 4, BLACK);
        }
    }
}

void CrevasseApp::drawField() {
    Rect g = gridRect();
    int cs = cellSize();
    for (int i = 0; i < m_field.w * m_field.h; i++)
        drawCell(Rect(g.x + (i % m_field.w) * cs, g.y + (i / m_field.w) * cs, cs, cs), i, false);
    strokeRoundRect(g.x - 3, g.y - 3, g.w + 6, g.h + 6, 4, 3, BLACK);
}

void CrevasseApp::drawStatus() {
    int y = LINE_Y + (LINE_H - lineHeight(FONT_BODY)) / 2;
    int flags = m_field.flags();
    std::string left = std::to_string(m_field.mines) + " crevasses";
    if (flags) left += "  \xC2\xB7  " + std::to_string(flags) + " flagged";
    text(left, MARGIN, y, FONT_BODY, BLACK, true);
    std::string right = !m_message.empty() ? m_message : (!m_field.laid ? "First probe is safe" : "");
    if (!right.empty()) {
        int room = W - 2 * MARGIN - textWidth(left, FONT_BODY, true) - 16;
        float fs = textWidth(right, FONT_BODY) <= room ? FONT_BODY : SMALL;
        textRight(ellipsize(right, room, fs), W - MARGIN, y + (fs < FONT_BODY ? 2 : 0), fs, DARK);
    }
}

void CrevasseApp::drawTools(bool addHits) {
    Rect seg(MARGIN, TOOL_Y, W - 2 * MARGIN, TOOL_H);
    segmented(seg, {"Probe", "Flag"}, m_flagMode ? 1 : 0, FONT_BODY);
    if (addHits) {
        addButton(Rect(seg.x, seg.y - 8, seg.w / 2, seg.h + 16), A_PROBE);
        addButton(Rect(seg.x + seg.w / 2, seg.y - 8, seg.w / 2, seg.h + 16), A_FLAG);
    }
}

void CrevasseApp::drawBoard() {
    header(kLevels[m_level].name, true, "Rules");
    drawStatus();
    drawField();
    drawTools(true);
}

void CrevasseApp::refreshField() {
    Rect g = gridRect();
    UIFramework::performFastPartialUpdate(framebuffer, g.x, g.y, g.w, g.h, [this]() { drawField(); });
}

void CrevasseApp::refreshStatus() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, LINE_Y, W, LINE_H, [this]() { drawStatus(); });
}

void CrevasseApp::drawMap(int x, int y, int cs) {
    const Games::Minefield& f = m_field;
    for (int i = 0; i < f.w * f.h; i++) drawCell(Rect(x + (i % f.w) * cs, y + (i / f.w) * cs, cs, cs), i, true);
    strokeRoundRect(x - 3, y - 3, f.w * cs + 6, f.h * cs + 6, 4, 3, BLACK);
}

void CrevasseApp::drawResult() {
    header(m_won ? "Depot reached" : "Into the crevasse", true);
    int y = HEADER_H + 20;
    text(m_won ? "Across the ice" : "The crossing failed", MARGIN, y, FONT_TITLE - 2, BLACK, true);
    if (m_won) textRight(GameKit::clockText((int)m_clock.seconds), W - MARGIN, y, FONT_TITLE - 2, BLACK, true);
    y += lineHeight(FONT_TITLE - 2) + 8;
    y = textWrapped(m_won ? kWinStories[m_story] : kLossStories[m_story], MARGIN, y, W - 2 * MARGIN, SMALL, INK, 3,
                    6) + 16;

    // The field revealed, small enough to leave room for the record.
    int bottom = H - 16 - 64 - 90;
    int cs = (bottom - y) / m_field.h;
    int maxW = (W - 2 * MARGIN) / m_field.w;
    if (cs > maxW) cs = maxW;
    if (cs > 44) cs = 44;
    drawMap((W - cs * m_field.w) / 2, y, cs);
    y += cs * m_field.h + 18;

    std::string rec = std::string(kLevels[m_level].name) + ": crossed " + std::to_string(m_wins[m_level]) + " in " +
                      GameKit::plural(m_played[m_level], "attempt", "attempts");
    text(rec, MARGIN, y, SMALL, DARK);
    y += lineHeight(SMALL) + 6;
    if (m_won && m_newBest) text("New best time!", MARGIN, y, SMALL, BLACK, true);
    else if (m_best[m_level]) text("Best time " + GameKit::clockText(m_best[m_level]), MARGIN, y, SMALL, DARK);

    int by = H - 16 - 64;
    Rect again(MARGIN, by, (W - 2 * MARGIN - 16) * 3 / 5, 64);
    Rect done(again.x + again.w + 16, by, W - 2 * MARGIN - again.w - 16, 64);
    button(again, m_won ? "Next crossing" : "Try again", ButtonStyle::PRIMARY, FONT_BODY);
    button(done, "Title", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(again, A_AGAIN);
    addButton(done, A_TITLE);
}
