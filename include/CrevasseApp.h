#pragma once

#include "Application.h"
#include "GameKit.h"
#include "GameLogic.h"
#include <vector>

// Crevasse 1911 - Minesweeper on the Antarctic ice. Sound the snow for
// hidden crevasses, flag them, and probe a safe way across to the depot.
// Every field can be crossed by reasoning alone.
class CrevasseApp : public Application {
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

    Screen m_screen = TITLE;
    Screen m_helpReturn = TITLE;
    int m_helpPage = 0;
    int m_lastDrawn = -1;
    std::vector<Button> m_buttons;

    // Field (m_level < 0 when none is in progress)
    Games::Minefield m_field;
    int m_level = -1;
    bool m_finished = false;
    bool m_won = false;
    bool m_flagMode = false;
    std::string m_message;
    GameKit::PlayClock m_clock;
    bool m_newBest = false;
    int m_story = 0;

    // Records (persisted)
    int m_played[LEVEL_COUNT] = {0, 0, 0};
    int m_wins[LEVEL_COUNT] = {0, 0, 0};
    int m_best[LEVEL_COUNT] = {0, 0, 0};  // seconds

    void newField(int level);
    void showBoard();
    void leaveBoard();
    void abandon();
    void tapCell(int cell);
    void finish(bool won);

    int cellSize() const;
    UI::Rect gridRect() const;

    // Drawing
    void render();
    void drawTitle();
    void drawHelp();
    void drawBoard();
    void drawStatus();
    void drawField();
    void drawTools(bool addHits);
    void drawResult();
    void drawCell(const UI::Rect& r, int cell, bool reveal);
    void drawMap(int x, int y, int cs);
    void refreshBoard(bool field, bool status);
    void addButton(const UI::Rect& r, int action);

    // Persistence
    void load();
    void save();
};
