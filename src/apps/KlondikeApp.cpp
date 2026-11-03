#include "KlondikeApp.h"
#include "ui/UIFramework.h"
#include <cstring>

extern uint8_t* framebuffer;

namespace {
using namespace UI;
using GameKit::SMALL;
using Games::Klondike;

struct Level {
    const char* name;
    int draw;
    int passes;  // 0 = unlimited
    const char* detail;
};

const Level kLevels[KlondikeApp::LEVEL_COUNT] = {
    {"Cheechako", 1, 0, "Newcomer: turn one card at a time"},
    {"Sourdough", 3, 0, "Old hand: turn three at a time"},
    {"Bonanza", 3, 3, "Three at a time, three passes only"},
};

const char* kRank[13] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
const char* kRankName[13] = {"ace", "2", "3", "4", "5", "6", "7", "8", "9", "10", "jack", "queen", "king"};
const char* kSuitName[4] = {"spades", "hearts", "diamonds", "clubs"};

enum Action {
    A_NONE = 0,
    A_LEVEL_0, A_LEVEL_1, A_LEVEL_2,
    A_CONTINUE, A_HELP, A_UNDO, A_COLLECT, A_CONCEDE, A_AGAIN, A_TITLE,
};

// Table geometry (portrait 540 x 960)
constexpr int CW = 68;
constexpr int CH = 96;
constexpr int GAP = 7;
constexpr int LEFT = (W - 7 * CW - 6 * GAP) / 2;
constexpr int TOP_Y = HEADER_H + 12;
constexpr int TAB_Y = TOP_Y + CH + 18;
constexpr int BAR_H = 60;
constexpr int BAR_Y = H - 14 - BAR_H;
constexpr int TAB_BOTTOM = BAR_Y - 10;
constexpr int DOWN_OFF = 14;
constexpr int UP_OFF = 40;
constexpr int FAN = 30;
constexpr size_t UNDO_DEPTH = 50;

int colX(int c) { return LEFT + c * (CW + GAP); }

std::string cardName(uint8_t c) {
    return std::string(kRankName[Klondike::rank(c)]) + " of " + kSuitName[Klondike::suit(c)];
}

void drawCard(int x, int y, uint8_t card, bool full) {
    fillRoundRect(x, y, CW, CH, 7, WHITE);
    strokeRoundRect(x, y, CW, CH, 7, 2, BLACK);
    int r = Klondike::rank(card), s = Klondike::suit(card);
    text(kRank[r], x + 6, y + 2, 28, BLACK, true);
    GameKit::suit(s, x + CW - 15, y + 19, 20);
    if (full) {
        if (r >= 10) {
            // Court cards: the letter large, framed.
            strokeRoundRect(x + 10, y + 40, CW - 20, CH - 48, 4, 1, MID);
            textCentered(kRank[r], x + CW / 2, y + 44, 36, BLACK, true);
        } else {
            GameKit::suit(s, x + CW / 2, y + 64, 40);
        }
    }
}

void drawBack(int x, int y) {
    fillRoundRect(x, y, CW, CH, 7, DARK);
    strokeRoundRect(x, y, CW, CH, 7, 2, BLACK);
    strokeRoundRect(x + 5, y + 5, CW - 10, CH - 10, 4, 1, LIGHT);
}

void drawSlot(int x, int y) {
    fillRoundRect(x, y, CW, CH, 7, PALE);
    strokeRoundRect(x, y, CW, CH, 7, 2, LIGHT);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void KlondikeApp::onCreate() {
    m_screen = TITLE;
    m_level = -1;
    load();
}

void KlondikeApp::onDestroy() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
}

void KlondikeApp::draw() {
    m_lastDrawn = -1;
    render();
}

bool KlondikeApp::onBack() {
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

void KlondikeApp::abandon() {
    // A deal walked away from after the first move counts as lost.
    if (m_level >= 0 && !m_finished && m_game.moves > 0) {
        m_played[m_level]++;
        m_streak = 0;
    }
    m_level = -1;
}

void KlondikeApp::newDeal(int level) {
    abandon();
    const Level& L = kLevels[level];
    m_game.deal(GameKit::randomSeed(), L.draw, L.passes);
    m_level = level;
    m_finished = false;
    m_won = false;
    m_stuck = false;
    m_newBest = false;
    m_undo.clear();
    m_clock = GameKit::PlayClock();
    save();
    showBoard();
}

void KlondikeApp::showBoard() {
    m_screen = BOARD;
    m_message.clear();
    m_stuck = !m_game.hasUsefulMove();
    if (m_stuck) m_message = "No useful moves left";
    m_clock.start();
    render();
}

void KlondikeApp::leaveBoard() {
    if (m_screen == BOARD) {
        m_clock.stop();
        save();
    }
}

int KlondikeApp::columnOffset(int col) const {
    int up = m_game.tabN[col] - m_game.down[col];
    if (up <= 1) return UP_OFF;
    int avail = TAB_BOTTOM - TAB_Y - CH - m_game.down[col] * DOWN_OFF;
    int off = avail / (up - 1);
    if (off > UP_OFF) off = UP_OFF;
    if (off < 22) off = 22;
    return off;
}

int KlondikeApp::cardY(int col, int index) const {
    int down = m_game.down[col];
    int d = index < down ? index : down;
    int u = index > down ? index - down : 0;
    return TAB_Y + d * DOWN_OFF + u * columnOffset(col);
}

void KlondikeApp::tapBoard(int x, int y) {
    m_clock.tick();
    Klondike& g = m_game;
    if (m_undo.size() >= UNDO_DEPTH) m_undo.erase(m_undo.begin());
    m_undo.push_back(g);
    bool moved = false;
    std::string fail;

    if (y >= TOP_Y && y < TOP_Y + CH + 8) {
        if (x < colX(1) - GAP / 2) {
            moved = g.turnStock();
            if (!moved) fail = g.wasteN ? "No more passes through the stock" : "The stock is empty";
        } else if (x < colX(3) - GAP / 2) {
            if (g.wasteN == 0) {
                fail = "Tap the stock to turn a card";
            } else {
                int d = g.bestWasteDest();
                moved = d >= 0 && g.moveFromWaste(d);
                if (!moved) fail = "Nowhere for the " + cardName(g.waste[g.wasteN - 1]);
            }
        } else {
            int s = (x - colX(3) + GAP / 2) / (CW + GAP);
            if (s > 3) s = 3;
            if (g.foundation[s] > 0) {
                int d = g.bestFoundationDest(s);
                moved = d >= 0 && g.moveFromFoundation(s, d);
                if (!moved) fail = "Nowhere for the " + cardName((uint8_t)(s * 13 + g.foundation[s] - 1));
            }
        }
    } else if (y >= TAB_Y - 8 && y < BAR_Y - 4) {
        int c = (x - LEFT + GAP / 2) / (CW + GAP);
        if (c < 0) c = 0;
        if (c > 6) c = 6;
        if (g.tabN[c] > 0 && y >= TAB_Y - 8) {
            int i = g.tabN[c] - 1;
            while (i > 0 && y < cardY(c, i)) i--;
            if (i < g.down[c]) {
                fail = "Uncover that card first";
            } else {
                int d = g.bestTableauDest(c, i);
                moved = d >= 0 && g.moveFromTableau(c, i, d);
                if (!moved) fail = "Nowhere for the " + cardName(g.tab[c][i]);
            }
        }
    }
    if (!moved) m_undo.pop_back();
    if (moved || !fail.empty()) afterMove(moved, fail);
}

void KlondikeApp::afterMove(bool moved, const std::string& failMessage) {
    if (moved) {
        if (m_game.won()) {
            finish(true);
            return;
        }
        m_stuck = !m_game.hasUsefulMove();
        m_message = m_stuck ? "No useful moves left" : "";
        refreshTable();
        save();
    } else {
        m_message = failMessage;
        UIFramework::performFastPartialUpdate(framebuffer, 0, BAR_Y - 4, W, H - BAR_Y + 4, [this]() {
            m_buttons.clear();
            drawBar(true);
        });
    }
}

void KlondikeApp::collect() {
    m_clock.tick();
    m_undo.push_back(m_game);
    while (m_game.collectOne()) {
    }
    if (m_game.won()) finish(true);
    else afterMove(true, "");
}

void KlondikeApp::finish(bool won) {
    m_clock.stop();
    m_finished = true;
    m_won = won;
    m_played[m_level]++;
    m_newBest = false;
    if (won) {
        m_wins[m_level]++;
        m_streak++;
        if (m_streak > m_bestStreak) m_bestStreak = m_streak;
        int t = (int)m_clock.seconds;
        if (t < 1) t = 1;
        bool fasterTime = m_best[m_level] == 0 || t < m_best[m_level];
        bool fewerMoves = m_fewest[m_level] == 0 || m_game.moves < m_fewest[m_level];
        if (fasterTime) m_best[m_level] = t;
        if (fewerMoves) m_fewest[m_level] = m_game.moves;
        m_newBest = fasterTime || fewerMoves;
    } else {
        m_streak = 0;
    }
    m_undo.clear();
    save();
    m_screen = RESULT;
    render();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// /data/klondike.txt:
//   KL1 played[3] wins[3] best[3] fewest[3] streak bestStreak
//   G level seconds moves passes f0 f1 f2 f3 stockN stock... wasteN waste...
//     then per column: n down cards...                    (deal in progress)

void KlondikeApp::load() {
    FILE* f = GameKit::openData("klondike.txt", "r");
    if (!f) return;
    int v[14];
    if (fscanf(f, " KL1 %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
               &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13]) == 14) {
        for (int i = 0; i < LEVEL_COUNT; i++) {
            m_played[i] = v[i] > 0 ? v[i] : 0;
            m_wins[i] = v[3 + i] > 0 ? v[3 + i] : 0;
            m_best[i] = v[6 + i] > 0 ? v[6 + i] : 0;
            m_fewest[i] = v[9 + i] > 0 ? v[9 + i] : 0;
        }
        m_streak = v[12] > 0 ? v[12] : 0;
        m_bestStreak = v[13] > 0 ? v[13] : 0;
    }
    int level, seconds, moves, passes;
    if (fscanf(f, " G %d %d %d %d", &level, &seconds, &moves, &passes) == 4 && level >= 0 && level < LEVEL_COUNT) {
        Klondike g;
        g.deal(1, kLevels[level].draw, kLevels[level].passes);
        g.moves = moves > 0 ? moves : 0;
        g.passes = passes > 0 ? passes : 1;
        bool seen[52] = {false};
        int total = 0;
        auto card = [&](uint8_t& out) {
            int c;
            if (fscanf(f, "%d", &c) != 1 || c < 0 || c > 51 || seen[c]) return false;
            seen[c] = true;
            out = (uint8_t)c;
            total++;
            return true;
        };
        bool ok = true;
        for (int s = 0; s < 4 && ok; s++) {
            ok = fscanf(f, "%d", &g.foundation[s]) == 1 && g.foundation[s] >= 0 && g.foundation[s] <= 13;
            for (int r = 0; r < g.foundation[s] && ok; r++) {
                ok = !seen[s * 13 + r];
                seen[s * 13 + r] = true;
                total++;
            }
        }
        ok = ok && fscanf(f, "%d", &g.stockN) == 1 && g.stockN >= 0 && g.stockN <= 52;
        for (int i = 0; ok && i < g.stockN; i++) ok = card(g.stock[i]);
        ok = ok && fscanf(f, "%d", &g.wasteN) == 1 && g.wasteN >= 0 && g.wasteN <= 52;
        for (int i = 0; ok && i < g.wasteN; i++) ok = card(g.waste[i]);
        for (int c = 0; c < 7 && ok; c++) {
            ok = fscanf(f, "%d %d", &g.tabN[c], &g.down[c]) == 2 && g.tabN[c] >= 0 && g.tabN[c] <= Klondike::MAX_COL &&
                 g.down[c] >= 0 && (g.down[c] < g.tabN[c] || (g.down[c] == 0 && g.tabN[c] == 0));
            for (int i = 0; ok && i < g.tabN[c]; i++) ok = card(g.tab[c][i]);
        }
        if (ok && total == 52 && !g.won()) {
            m_game = g;
            m_level = level;
            m_clock.seconds = seconds > 0 ? (uint32_t)seconds : 0;
            m_finished = false;
        }
    }
    fclose(f);
}

void KlondikeApp::save() {
    FILE* f = GameKit::openData("klondike.txt", "w");
    if (!f) return;
    fprintf(f, "KL1 %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", m_played[0], m_played[1], m_played[2], m_wins[0],
            m_wins[1], m_wins[2], m_best[0], m_best[1], m_best[2], m_fewest[0], m_fewest[1], m_fewest[2], m_streak,
            m_bestStreak);
    if (m_level >= 0 && !m_finished) {
        const Klondike& g = m_game;
        fprintf(f, "G %d %u %d %d %d %d %d %d\n", m_level, (unsigned)m_clock.seconds, g.moves, g.passes,
                g.foundation[0], g.foundation[1], g.foundation[2], g.foundation[3]);
        fprintf(f, "%d", g.stockN);
        for (int i = 0; i < g.stockN; i++) fprintf(f, " %d", g.stock[i]);
        fprintf(f, "\n%d", g.wasteN);
        for (int i = 0; i < g.wasteN; i++) fprintf(f, " %d", g.waste[i]);
        fprintf(f, "\n");
        for (int c = 0; c < 7; c++) {
            fprintf(f, "%d %d", g.tabN[c], g.down[c]);
            for (int i = 0; i < g.tabN[c]; i++) fprintf(f, " %d", g.tab[c][i]);
            fprintf(f, "\n");
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

void KlondikeApp::addButton(const Rect& r, int action) { m_buttons.push_back({r, action}); }

void KlondikeApp::handleTouch(int x, int y) {
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
    if (m_screen == BOARD && hitHeaderAction(x, y, "Rules")) {
        leaveBoard();
        m_helpReturn = BOARD;
        m_helpPage = 0;
        m_screen = HELP;
        render();
        return;
    }
    int action = A_NONE;
    for (const Button& b : m_buttons)
        if (b.rect.contains(x, y)) {
            action = b.action;
            break;
        }
    if (m_screen == TITLE && hitHeaderAction(x, y, "How to play")) action = A_HELP;
    if (m_screen == BOARD && action == A_NONE) {
        if (y >= HEADER_H && y < BAR_Y - 4) tapBoard(x, y);
        return;
    }
    switch (action) {
        case A_LEVEL_0:
        case A_LEVEL_1:
        case A_LEVEL_2:
            newDeal(action - A_LEVEL_0);
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
        case A_UNDO:
            m_clock.tick();
            if (!m_undo.empty()) {
                m_game = m_undo.back();
                m_undo.pop_back();
                m_stuck = !m_game.hasUsefulMove();
                m_message = m_stuck ? "No useful moves left" : "";
                save();
                refreshTable();
            }
            break;
        case A_COLLECT:
            collect();
            break;
        case A_CONCEDE:
            finish(false);
            break;
        case A_AGAIN:
            newDeal(m_level >= 0 ? m_level : 0);
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

void KlondikeApp::render() {
    m_buttons.clear();
    auto content = [this]() {
        switch (m_screen) {
            case TITLE: drawTitle(); break;
            case HELP: drawHelp(); break;
            case BOARD: drawBoard(); break;
            case RESULT: drawResult(); break;
        }
    };
    m_partials = 0;
    UIFramework::performFullScreenDraw(framebuffer, content);
    m_lastDrawn = m_screen;
}

void KlondikeApp::refreshTable() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() {
        m_buttons.clear();
        drawTable();
        drawBar(true);
    });
}

void KlondikeApp::drawTitle() {
    header("Klondike 1897", true, "How to play");

    // Emblem: the four aces, fanned.
    int ex = (W - (4 * CW + 3 * 30)) / 2, ey = HEADER_H + 22;
    const uint8_t aces[4] = {0, 13, 26, 39};
    for (int i = 0; i < 4; i++) drawCard(ex + i * (CW + 30), ey + (i == 1 || i == 2 ? 0 : 10), aces[i], true);

    int y = ey + CH + 32;
    y = textWrapped("Dawson City, winter of 1897. The creeks are frozen, the claims snowed in, and in every cabin "
                    "someone is laying out patience. Bring all four suits home, ace to king.",
                    MARGIN + 4, y, W - 2 * MARGIN - 8, SMALL, INK, 0, 7, true);

    y += 16;
    GameKit::sectionLabel(y, "Your record");
    y += 32;
    int played = m_played[0] + m_played[1] + m_played[2];
    int wins = m_wins[0] + m_wins[1] + m_wins[2];
    if (played == 0) {
        text("No hands played yet.", MARGIN, y + 8, SMALL, DARK);
        y += 50;
    } else {
        const int values[4] = {played, wins, m_streak, m_bestStreak};
        const char* labels[4] = {"Played", "Won", "Streak", "Best streak"};
        int cw = (W - 2 * MARGIN) / 4;
        for (int i = 0; i < 4; i++) {
            int x = MARGIN + i * cw;
            text(std::to_string(values[i]), x, y, FONT_TITLE, BLACK, true);
            text(ellipsize(labels[i], cw - 8, SMALL), x, y + lineHeight(FONT_TITLE) + 2, SMALL, DARK);
        }
        y += lineHeight(FONT_TITLE) + lineHeight(SMALL) + 8;
    }
    y += 14;

    bool inProgress = m_level >= 0 && !m_finished;
    if (inProgress) {
        Rect c(MARGIN, y, W - 2 * MARGIN, 80);
        std::string detail = std::string(kLevels[m_level].name) + "  \xC2\xB7  " +
                             std::to_string(m_game.foundationCards()) + " of 52 home  \xC2\xB7  move " +
                             std::to_string(m_game.moves);
        GameKit::optionButton(c, "Continue", detail, true);
        addButton(c, A_CONTINUE);
        y += 96;
    }
    GameKit::sectionLabel(y, inProgress ? "New deal (forfeits this one)" : "Deal a hand");
    y += 34;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        Rect r(MARGIN, y + i * 96, W - 2 * MARGIN, 84);
        std::string detail = kLevels[i].detail;
        if (m_best[i]) detail = "Best " + GameKit::clockText(m_best[i]) + ", " + std::to_string(m_fewest[i]) + " moves";
        GameKit::optionButton(r, kLevels[i].name, detail, !inProgress && i == 0);
        addButton(r, A_LEVEL_0 + i);
    }
}

void KlondikeApp::drawHelp() {
    header("How to play", true, "Done");
    const int tw = W - 2 * MARGIN;
    int y = HEADER_H + 30;
    if (m_helpPage == 0) {
        text("Bring the suits home", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Build each suit on its pile at the top right, from ace up to king. All 52 cards home "
                        "wins the hand.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        y = textWrapped("In the seven columns, build downwards in alternating colours: a red 7 on a black 8.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        const int cx = (W - 2 * CW - 60) / 2;
        drawCard(cx, y, 7, false);       // 8 of spades
        drawCard(cx, y + UP_OFF, 19, true);  // 7 of hearts
        drawCard(cx + CW + 60, y, 26, true); // ace of diamonds
        y += UP_OFF + CH + 16;
        textWrapped("Red suits are grey-hearted; black suits are solid.", MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    } else if (m_helpPage == 1) {
        text("Tap to move", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Tap a card and it goes to the best place: home to its suit if it can, otherwise onto "
                        "another column. Tap it again to try the next column.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        y = textWrapped("Tap a card part-way down a column to move it with the cards on top of it. Only a king "
                        "can fill an empty column.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 20;
        textWrapped("Tap a suit pile to bring its top card back down. Undo takes back a move.", MARGIN, y, tw,
                    FONT_BODY, INK, 0, 10);
    } else {
        text("The stock", MARGIN, y, FONT_TITLE - 2, BLACK, true);
        y += 56;
        y = textWrapped("Tap the stock (top left) to turn cards onto the waste; when it is empty, tap it again "
                        "to turn the waste back over.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 16;
        for (int i = 0; i < LEVEL_COUNT; i++) {
            text(kLevels[i].name, MARGIN, y, FONT_BODY, BLACK, true);
            y = textWrapped(kLevels[i].detail, MARGIN, y + lineHeight(FONT_BODY) + 2, tw, SMALL, INK, 0, 6) + 10;
        }
        y += 6;
        textWrapped("When every card is face up, Collect finishes the hand. If no useful move is left, you may "
                    "concede.",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    }
    GameKit::pageFooter(m_helpPage, 3);
}

void KlondikeApp::drawTable() {
    const Klondike& g = m_game;
    // Stock
    if (g.stockN > 0) {
        drawBack(colX(0), TOP_Y);
        std::string n = std::to_string(g.stockN);
        textCentered(n, colX(0) + CW / 2, TOP_Y + (CH - lineHeight(SMALL)) / 2, SMALL, WHITE, true);
    } else {
        drawSlot(colX(0), TOP_Y);
        if (g.canRecycle()) {
            int cx = colX(0) + CW / 2, cy = TOP_Y + CH / 2;
            fillCircle(cx, cy, 18, DARK);
            fillCircle(cx, cy, 13, PALE);
            fillRect(cx, cy - 20, 20, 20, PALE);
            GameKit::fillTriangle(cx + 2, cy - 24, cx + 2, cy - 6, cx + 14, cy - 15, DARK);
        }
    }
    // Waste (fanned for draw three)
    if (g.wasteN == 0) {
        drawSlot(colX(1), TOP_Y);
    } else {
        int show = g.draw == 3 ? (g.wasteN < 3 ? g.wasteN : 3) : 1;
        for (int k = 0; k < show; k++) {
            int i = g.wasteN - show + k;
            drawCard(colX(1) + k * FAN, TOP_Y, g.waste[i], k == show - 1);
        }
    }
    // Foundations
    for (int s = 0; s < 4; s++) {
        int x = colX(3 + s);
        if (g.foundation[s] == 0) {
            drawSlot(x, TOP_Y);
            GameKit::suitShape(s, x + CW / 2, TOP_Y + CH / 2, 36, LIGHT);
        } else {
            drawCard(x, TOP_Y, (uint8_t)(s * 13 + g.foundation[s] - 1), true);
        }
    }
    // Columns
    for (int c = 0; c < 7; c++) {
        int x = colX(c);
        if (g.tabN[c] == 0) {
            strokeRoundRect(x, TAB_Y, CW, CH, 7, 2, LIGHT);
            continue;
        }
        for (int i = 0; i < g.tabN[c]; i++) {
            int y = cardY(c, i);
            if (i < g.down[c]) drawBack(x, y);
            else drawCard(x, y, g.tab[c][i], i == g.tabN[c] - 1);
        }
    }
}

void KlondikeApp::drawBar(bool addHits) {
    Rect undo(MARGIN, BAR_Y, 120, BAR_H);
    button(undo, "Undo", ButtonStyle::SECONDARY, FONT_BODY - 2, !m_undo.empty());
    if (addHits) addButton(undo, A_UNDO);
    Rect right(W - MARGIN - 140, BAR_Y, 140, BAR_H);
    bool collect = m_game.canCollect();
    if (collect) {
        button(right, "Collect", ButtonStyle::PRIMARY, FONT_BODY - 2);
        if (addHits) addButton(right, A_COLLECT);
    } else if (m_stuck) {
        button(right, "Concede", ButtonStyle::SECONDARY, FONT_BODY - 2);
        if (addHits) addButton(right, A_CONCEDE);
    }
    int left = undo.x + undo.w + 12;
    int room = (collect || m_stuck ? right.x - 12 : W - MARGIN) - left;
    std::string msg = m_message;
    uint8_t color = BLACK;
    if (msg.empty() && m_game.passLimit > 0) {
        color = DARK;
        msg = "Pass " + std::to_string(m_game.passes) + " of " + std::to_string(m_game.passLimit);
    }
    if (!msg.empty()) {
        float fs = textWidth(msg, FONT_BODY - 2) <= room ? FONT_BODY - 2 : SMALL - 2;
        if (textWidth(msg, fs) > room) {
            textWrapped(msg, left, BAR_Y + 2, room, SMALL - 2, color, 2, 0, true);
        } else {
            textCentered(msg, left + room / 2, BAR_Y + (BAR_H - lineHeight(fs)) / 2, fs, color, color == BLACK);
        }
    }
}

void KlondikeApp::drawBoard() {
    header(kLevels[m_level].name, true, "Rules");
    drawTable();
    drawBar(true);
}

void KlondikeApp::drawResult() {
    header(m_won ? "Struck gold" : "Claim worked out", true);
    int y = HEADER_H + 22;
    text(m_won ? "All four suits home" : "The cards ran dry", MARGIN, y, FONT_TITLE - 2, BLACK, true);
    y += lineHeight(FONT_TITLE - 2) + 18;

    int gap = 24, ex = (W - (4 * CW + 3 * gap)) / 2;
    for (int s = 0; s < 4; s++) {
        int x = ex + s * (CW + gap);
        if (m_game.foundation[s] == 0) {
            drawSlot(x, y);
            GameKit::suitShape(s, x + CW / 2, y + CH / 2, 36, LIGHT);
        } else {
            drawCard(x, y, (uint8_t)(s * 13 + m_game.foundation[s] - 1), true);
        }
    }
    y += CH + 24;

    if (m_won) {
        text(GameKit::clockText((int)m_clock.seconds), MARGIN, y, 52, BLACK, true);
        textRight(std::to_string(m_game.moves) + " moves", W - MARGIN, y + 4, FONT_BODY, BLACK, true);
        textRight(kLevels[m_level].name, W - MARGIN, y + 4 + lineHeight(FONT_BODY) + 4, SMALL, DARK);
        y += lineHeight(52) + 14;
        y = textWrapped("The stove is roaring and the claim has paid out. Tonight Dawson City drinks to your luck.",
                        MARGIN, y, W - 2 * MARGIN, FONT_BODY, INK, 3, 6) + 12;
    } else {
        y = textWrapped(std::to_string(m_game.foundationCards()) + " of 52 cards came home. The candle's burned "
                        "down, but there's always another claim up the creek.",
                        MARGIN, y, W - 2 * MARGIN, FONT_BODY, INK, 3, 6) + 12;
    }
    if (m_won && m_newBest) {
        text("A new record for " + std::string(kLevels[m_level].name) + "!", MARGIN, y, FONT_BODY, BLACK, true);
        y += lineHeight(FONT_BODY) + 8;
    }
    y += 12;
    GameKit::sectionLabel(y, "Records  \xC2\xB7  streak " + std::to_string(m_streak) + ", best " +
                                 std::to_string(m_bestStreak));
    y += 36;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        hRule(y - 6, PALE);
        text(kLevels[i].name, MARGIN, y, FONT_BODY, i == m_level ? BLACK : INK, i == m_level);
        std::string won = "won " + std::to_string(m_wins[i]) + " of " + std::to_string(m_played[i]);
        textRight(won, W - MARGIN, y, FONT_BODY, i == m_level ? BLACK : DARK, i == m_level);
        y += lineHeight(FONT_BODY) + 2;
        std::string best = m_best[i] ? "best " + GameKit::clockText(m_best[i]) + ", fewest " +
                                           std::to_string(m_fewest[i]) + " moves"
                                     : "not yet won";
        text(best, MARGIN, y, SMALL, DARK);
        y += lineHeight(SMALL) + 16;
    }

    int by = H - 16 - 64;
    Rect again(MARGIN, by, (W - 2 * MARGIN - 16) * 3 / 5, 64);
    Rect done(again.x + again.w + 16, by, W - 2 * MARGIN - again.w - 16, 64);
    button(again, "Deal again", ButtonStyle::PRIMARY, FONT_BODY);
    button(done, "Title", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(again, A_AGAIN);
    addButton(done, A_TITLE);
}
