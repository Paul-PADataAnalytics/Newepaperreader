#pragma once

#include "Application.h"
#include "ui/UIKit.h"
#include <stdint.h>
#include <vector>

// Codebreaker 1917 - a touch-only code-breaking puzzle (Mastermind).
//
// Room 40, the Admiralty's codebreaking section. An enemy cipher key - a
// short sequence of cipher symbols - must be recovered before the attack
// date. Each trial key costs a day; after each one you learn how many
// symbols are in the right place (solid dots) and how many are right but
// misplaced (hollow dots). Break the key and the intercept is read in time.
class CodebreakerApp : public Application {
public:
    static const int MAX_SLOTS = 5;
    static const int MAX_SYMBOLS = 8;
    static const int MAX_DAYS = 10;
    static const int LEVEL_COUNT = 3;
    static const uint8_t EMPTY = 0xFF;

    void onCreate() override;
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
    bool onBack() override;

    // Standard Mastermind scoring, correct with repeated symbols: `exact` is
    // the number of symbols in the right position, `near` the number of
    // further symbols present in the key but in a different position.
    static void scoreGuess(const uint8_t* secret, const uint8_t* guess, int n, int& exact, int& near) {
        int cs[MAX_SYMBOLS] = {0};
        int cg[MAX_SYMBOLS] = {0};
        exact = 0;
        near = 0;
        for (int i = 0; i < n; i++) {
            if (secret[i] == guess[i]) {
                exact++;
            } else {
                if (secret[i] < MAX_SYMBOLS) cs[secret[i]]++;
                if (guess[i] < MAX_SYMBOLS) cg[guess[i]]++;
            }
        }
        for (int s = 0; s < MAX_SYMBOLS; s++) near += cs[s] < cg[s] ? cs[s] : cg[s];
    }

private:
    enum Screen { TITLE, HELP, BOARD, RESULT };

    struct Button {
        UI::Rect rect;
        int action;
    };

    Screen m_screen = TITLE;
    Screen m_helpReturn = TITLE;
    uint32_t m_rng = 1;

    // Game in progress (m_level < 0 when there is none)
    int m_level = -1;
    int m_message = 0;
    uint8_t m_secret[MAX_SLOTS] = {0};
    uint8_t m_guesses[MAX_DAYS][MAX_SLOTS] = {{0}};
    uint8_t m_exact[MAX_DAYS] = {0};
    uint8_t m_near[MAX_DAYS] = {0};
    int m_guessCount = 0;
    uint8_t m_current[MAX_SLOTS] = {0};
    int m_sel = 0;

    // Set once the game has ended; the board then stays readable on the
    // result screen but can no longer be continued.
    bool m_finished = false;
    bool m_won = false;
    bool m_newBest = false;

    // Service record (persisted)
    int m_played = 0;
    int m_wins = 0;
    int m_streak = 0;
    int m_bestStreak = 0;
    int m_best[LEVEL_COUNT] = {0, 0, 0};
    int m_lastMessage = -1;

    std::vector<Button> m_buttons;
    int m_quickDraws = 0;
    int m_lastDrawn = -1;

    uint32_t nextRand();
    int randInt(int lo, int hi);

    // Flow
    void newGame(int level);
    void abandonGame();
    void sendGuess();
    void finishGame(bool won);
    void tapSlot(int i);
    void tapSymbol(int s);
    void clearGuess();
    bool guessComplete() const;
    bool guessEmpty() const;

    // Drawing
    void render();
    void drawTitle();
    void drawHelp();
    void drawBoard();
    void drawResult();
    void drawHistory();
    void drawInput(bool addHits);
    void refreshInput();
    int drawTelegram(int y, bool readInTime);
    void drawWorking(int y, int bottom);
    int slotSize() const;
    UI::Rect slotRect(int i) const;
    UI::Rect keyRect(int s) const;
    void addButton(const UI::Rect& r, int action);

    // Persistence
    void loadStats();
    void saveStats();
};
