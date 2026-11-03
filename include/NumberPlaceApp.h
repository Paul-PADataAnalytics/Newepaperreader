#pragma once

#include "Application.h"
#include "GameKit.h"
#include "GameLogic.h"
#include <vector>

// Number Place - 9x9 Sudoku, as it first appeared in a New York puzzle
// magazine in 1979. Generated puzzles with a unique solution, graded by the
// techniques needed; pencil marks, undo and hints.
class NumberPlaceApp : public Application {
public:
    static const int LEVEL_COUNT = 3;

    void onCreate() override;
    void onDestroy() override;
    void draw() override;
    void handleTouch(int x, int y) override;
    bool onBack() override;

private:
    enum Screen { TITLE, HELP, BOARD, RESULT };
    struct Button {
        UI::Rect rect;
        int action;
    };
    struct Snapshot {
        uint8_t values[81];
        uint16_t marks[81];
    };

    Screen m_screen = TITLE;
    Screen m_helpReturn = TITLE;
    int m_helpPage = 0;
    int m_lastDrawn = -1;
    std::vector<Button> m_buttons;

    // Puzzle (m_level < 0 when none is in progress)
    int m_level = -1;
    bool m_finished = false;
    uint8_t m_givens[81] = {0};
    uint8_t m_solution[81] = {0};
    uint8_t m_values[81] = {0};
    uint16_t m_marks[81] = {0};
    int m_sel = -1;
    bool m_pencil = false;
    int m_hints = 0;
    std::string m_message;
    std::vector<Snapshot> m_undo;
    GameKit::PlayClock m_clock;
    bool m_newBest = false;

    // Records (persisted)
    int m_solved[LEVEL_COUNT] = {0, 0, 0};
    int m_best[LEVEL_COUNT] = {0, 0, 0};  // seconds, unaided solves only

    void newPuzzle(int level);
    void showBoard();
    void leaveBoard();
    void tapCell(int cell);
    void tapDigit(int d);
    void erase();
    void undo();
    void hint();
    void pushUndo();
    void place(int cell, int d);
    void afterChange(bool gridChanged);
    void finish();
    bool conflicts(int cell) const;
    int filledCount() const;
    int digitCount(int d) const;

    // Drawing
    void render();
    void drawTitle();
    void drawHelp();
    void drawBoard();
    void drawGrid();
    void drawControls(bool addHits);
    void drawResult();
    void drawMiniGrid(int x, int y, int cs);
    void refreshBoard(bool grid, bool controls);
    void addButton(const UI::Rect& r, int action);

    // Persistence
    void load();
    void save();
};
