#pragma once

#include "Application.h"
#include "GameKit.h"
#include "GameLogic.h"
#include <vector>

// Klondike 1897 - patience (solitaire) in a Yukon cabin during the gold
// rush. Tap a card to send it to the best place; tap the stock to turn.
class KlondikeApp : public Application {
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

    // Deal (m_level < 0 when none is in progress)
    Games::Klondike m_game;
    int m_level = -1;
    bool m_finished = false;
    bool m_won = false;
    bool m_stuck = false;
    std::string m_message;
    std::vector<Games::Klondike> m_undo;
    GameKit::PlayClock m_clock;
    bool m_newBest = false;
    int m_partials = 0;

    // Records (persisted)
    int m_played[LEVEL_COUNT] = {0, 0, 0};
    int m_wins[LEVEL_COUNT] = {0, 0, 0};
    int m_best[LEVEL_COUNT] = {0, 0, 0};    // seconds
    int m_fewest[LEVEL_COUNT] = {0, 0, 0};  // moves
    int m_streak = 0;
    int m_bestStreak = 0;

    void newDeal(int level);
    void abandon();
    void showBoard();
    void leaveBoard();
    void tapBoard(int x, int y);
    void afterMove(bool moved, const std::string& failMessage);
    void collect();
    void finish(bool won);

    // Geometry
    int columnOffset(int col) const;
    int cardY(int col, int index) const;

    // Drawing
    void render();
    void drawTitle();
    void drawHelp();
    void drawBoard();
    void drawTable();
    void drawBar(bool addHits);
    void drawResult();
    void refreshTable();
    void addButton(const UI::Rect& r, int action);

    // Persistence
    void load();
    void save();
};
