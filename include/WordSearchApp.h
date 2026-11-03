#pragma once

#include "Application.h"
#include "GameKit.h"
#include "GameLogic.h"
#include <vector>

// Word Search - themed letter grids. Tap the first and last letters of a
// hidden word; if the straight line between them spells a word on the list
// (either way round) it is ringed and crossed off.
class WordSearchApp : public Application {
public:
    static const int LEVEL_COUNT = 3;

    void onCreate() override;
    void onDestroy() override;
    void draw() override;
    void handleTouch(int x, int y) override;
    bool onBack() override;

private:
    enum Screen { TITLE, HELP, BOARD, DONE };
    struct Button {
        UI::Rect rect;
        int action;
    };

    Screen m_screen = TITLE;
    Screen m_helpReturn = TITLE;
    int m_helpPage = 0;
    int m_lastDrawn = -1;
    int m_quickDraws = 0;
    std::vector<Button> m_buttons;

    // Puzzle (m_level < 0 when none is in progress)
    Games::WordSearch m_puzzle;
    int m_level = -1;
    bool m_finished = false;
    int m_first = -1;           // highlighted first letter, or -1
    std::string m_message;      // gentle feedback in the status line
    GameKit::PlayClock m_clock;
    bool m_newBest = false;

    // Records (persisted)
    int m_themeChoice = -1;     // -1 = surprise me
    int m_lastTheme = -1;
    int m_solved[LEVEL_COUNT] = {0, 0, 0};
    int m_best[LEVEL_COUNT] = {0, 0, 0};  // seconds, 0 = none

    void newPuzzle(int level);
    void tapGrid(int x, int y);
    void finish();
    void showBoard();
    void leaveBoard();

    // Geometry
    int cellSize() const;
    UI::Rect gridRect() const;
    int cellAt(int x, int y) const;
    void cellCentre(int cell, int& cx, int& cy) const;

    // Drawing
    void render();
    void drawTitle();
    void drawHelp();
    void drawBoard();
    void drawStatus();
    void drawGrid();
    void drawWordList(int top);
    void drawDonePanel(int top);
    void drawCapsule(int r0, int c0, int dr, int dc, int len, bool outline);
    void refreshBoard(bool grid, bool status, bool words);
    int wordListTop() const;
    void addButton(const UI::Rect& r, int action);

    // Persistence
    void load();
    void save();
};
