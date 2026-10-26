#include "GamesApp.h"
#include "Launcher.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"

extern uint8_t* framebuffer;

namespace {
using namespace UI;

struct GameEntry {
    int app;
    const char* title;
    const char* blurb;
    UIIconId icon;
};

const GameEntry kGames[] = {
    {APP_TEARACE, "Tea Race 1866", "Race a clipper from China to London", ICON_SHIP},
    {APP_KEEPER, "The Keeper", "Twenty-one nights alone on a lighthouse", ICON_LIGHTHOUSE},
};
constexpr int kGameCount = sizeof(kGames) / sizeof(kGames[0]);
constexpr int LIST_Y = HEADER_H + 8;
constexpr int GAME_ROW_H = 120;
} // namespace

void GamesApp::draw() {
    UIFramework::performFullScreenDraw(framebuffer, []() {
        header("Games", true);
        for (int i = 0; i < kGameCount; i++) {
            int y = LIST_Y + i * GAME_ROW_H;
            icon(kGames[i].icon, MARGIN, y + (GAME_ROW_H - 72) / 2, 72);
            text(kGames[i].title, MARGIN + 92, y + 30, FONT_BODY + 2, BLACK, true);
            text(ellipsize(kGames[i].blurb, W - MARGIN * 2 - 92 - 30, FONT_SMALL), MARGIN + 92, y + 70, FONT_SMALL, DARK);
            chevronRight(W - MARGIN - 8, y + GAME_ROW_H / 2, 22, DARK);
            hRule(y + GAME_ROW_H - 1);
        }
        textWrapped("Short games made for e-paper: everything is a tap, and a game fits in a coffee break.",
                    MARGIN * 2, H - 120, W - MARGIN * 4, FONT_CAPTION + 1, MID, 3, 6, true);
    });
}

void GamesApp::handleTouch(int x, int y) {
    (void)x;
    if (y < LIST_Y) return;
    int i = (y - LIST_Y) / GAME_ROW_H;
    if (i >= 0 && i < kGameCount) Launcher::getInstance().requestLaunch(kGames[i].app, APP_GAMES);
}
