#include "GamesApp.h"
#include "GameKit.h"
#include "Launcher.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"

extern uint8_t* framebuffer;

namespace {
using namespace UI;

// Games without a system icon get a small drawn emblem.
enum Emblem { EMBLEM_ICON, EMBLEM_WORDS, EMBLEM_GRID, EMBLEM_FLAG, EMBLEM_CARDS };

struct GameEntry {
    int app;
    const char* title;
    const char* blurb;
    UIIconId icon;
    Emblem emblem;
};

const GameEntry kGames[] = {
    {APP_TEARACE, "Tea Race 1866", "Race a clipper from China to London", ICON_SHIP, EMBLEM_ICON},
    {APP_KEEPER, "The Keeper", "Keep a lighthouse through winter", ICON_LIGHTHOUSE, EMBLEM_ICON},
    {APP_CODEBREAKER, "Codebreaker 1917", "Break the enemy cipher", ICON_KEY, EMBLEM_ICON},
    {APP_WORDSEARCH, "Word Search", "Ring the words hidden in the grid", ICON_GAMES, EMBLEM_WORDS},
    {APP_NUMBERPLACE, "Number Place", "Sudoku, as it first appeared in 1979", ICON_GAMES, EMBLEM_GRID},
    {APP_CREVASSE, "Crevasse 1911", "Flag the crevasses, cross the ice", ICON_GAMES, EMBLEM_FLAG},
    {APP_KLONDIKE, "Klondike 1897", "Patience in a gold-rush cabin", ICON_GAMES, EMBLEM_CARDS},
};
constexpr int kGameCount = sizeof(kGames) / sizeof(kGames[0]);
constexpr int kRowY = HEADER_H + 4;
constexpr int kRowH = 92;
constexpr int kEmblem = 54;

void drawEmblem(const GameEntry& g, int x, int y) {
    const int s = kEmblem;
    switch (g.emblem) {
        case EMBLEM_ICON:
            icon(g.icon, x, y, s);
            break;
        case EMBLEM_WORDS: {
            strokeRoundRect(x + 2, y + 2, s - 4, s - 4, 8, 3, BLACK);
            line(x + 15, y + s - 15, x + s - 15, y + 15, 16, BLACK);
            line(x + 15, y + s - 15, x + s - 15, y + 15, 10, LIGHT);
            fillCircle(x + 15, y + s - 15, 6, BLACK);
            break;
        }
        case EMBLEM_GRID: {
            int c = (s - 6) / 3;
            fillRect(x + 3 + c, y + 3, c, c, BLACK);
            fillRect(x + 3 + 2 * c, y + 3 + 2 * c, c, c, BLACK);
            fillRect(x + 3, y + 3 + c, c, c, DARK);
            for (int i = 1; i < 3; i++) {
                fillRect(x + 3 + i * c, y + 3, 2, 3 * c, BLACK);
                fillRect(x + 3, y + 3 + i * c, 3 * c, 2, BLACK);
            }
            strokeRoundRect(x + 2, y + 2, s - 4, s - 4, 4, 3, BLACK);
            break;
        }
        case EMBLEM_FLAG: {
            GameKit::fillTriangle(x, y + s - 4, x + s, y + s - 4, x + s / 2, y + s - 20, LIGHT);
            line(x + s / 2 - 6, y + s - 6, x + s / 2 + 2, y + s - 14, 3, BLACK);
            GameKit::pennant(x + s / 2 - 4, y + s - 14, 40);
            break;
        }
        case EMBLEM_CARDS: {
            fillRoundRect(x + 2, y + 2, 34, 46, 5, WHITE);
            strokeRoundRect(x + 2, y + 2, 34, 46, 5, 2, BLACK);
            fillRoundRect(x + 18, y + 8, 34, 46, 5, WHITE);
            strokeRoundRect(x + 18, y + 8, 34, 46, 5, 2, BLACK);
            GameKit::suit(0, x + 35, y + 31, 22);
            break;
        }
    }
}

} // namespace

void GamesApp::draw() {
    UIFramework::performQuickFullScreenDraw(framebuffer, [this]() {
        header("Games", true);
        for (int i = 0; i < kGameCount; ++i) {
            int y = kRowY + i * kRowH;
            drawEmblem(kGames[i], MARGIN, y + 18);
            text(ellipsize(kGames[i].title, W - 146, FONT_BODY, true), MARGIN + 74, y + 10, FONT_BODY, BLACK, true);
            text(ellipsize(kGames[i].blurb, W - 146, FONT_BODY - 2), MARGIN + 74, y + 48, FONT_BODY - 2, DARK);
            chevronRight(W - MARGIN - 10, y + 44, 20, DARK);
            hRule(y + kRowH - 1);
        }
    });
}

void GamesApp::handleTouch(int x, int y) {
    (void)x;
    if (y < kRowY) return;
    int row = (y - kRowY) / kRowH;
    if (row < 0 || row >= kGameCount) return;
    Launcher::getInstance().requestLaunch(kGames[row].app, APP_GAMES);
}
