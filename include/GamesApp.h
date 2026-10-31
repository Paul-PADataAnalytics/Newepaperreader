#pragma once

#include "Application.h"
#include "GameLogic.h"
#include <string>

// Games hub and compact touch games that stay within the existing hub.
class GamesApp : public Application {
public:
    void onCreate() override;
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
    bool onBack() override;

private:
    enum Screen { HUB, PLAY, HELP };
    enum Game { NONE, SUDOKU, LIGHTS, SLIDING, WORD_SEARCH };

    Screen m_screen = HUB;
    Game m_game = NONE;
    Games::MiniSudoku m_sudoku;
    Games::LightsOut m_lights;
    Games::SlidingPuzzle m_sliding;
    Games::WordSearch m_wordSearch;
    uint32_t m_seed = 1;
    int m_selected = -1;
    int m_wordFirst = -1;
    int m_helpPage = 0;
    std::string m_feedback;

    void startGame(Game game);
    void resetGame();
    void drawHub();
    void drawGame();
    void drawHelp();
    void finishTouchGame();
};
