#pragma once

#include "Application.h"
#include "ui/UIKit.h"
#include <string>
#include <vector>

// Tea Race 1866 - a short, touch-only sailing strategy game.
//
// Race three rival clippers from Foochow to London over nine legs. Each leg
// you read the barometer (a forecast that is usually right) and choose a sail
// plan: shorten (safe, slow), steady, or crack on (fast, risky, tiring). The
// weather that actually blows decides how many days the leg takes and whether
// you lose spars or wet the cargo. Dilemmas crop up at sea, and you can put
// into Anjer or St Helena to repair. The first ship home sells her tea at the
// premium; damage, wet chests and a slow passage cost money.
class TeaRaceApp : public Application {
public:
    void onCreate() override;
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
    bool onBack() override;

private:
    enum Screen { TITLE, HELP, CARGO, LEG, OUTCOME, EVENT, PORT, RESULT };

    struct Rival {
        const char* name;
        int style; // 0 bold, 1 steady, 2 careful
        float day;
        int hull;
        int crew;
    };

    struct Button {
        UI::Rect rect;
        int action;
    };

    Screen m_screen = TITLE;
    uint32_t m_rng = 1;

    // Voyage state
    int m_leg = 0;
    float m_day = 0;
    int m_hull = 100;
    int m_crew = 100;
    int m_wet = 0;
    int m_money = 0;
    int m_cargo = 0;     // 0 fine first-flush, 1 congou
    int m_weather = 1;
    int m_forecast = 1;
    float m_riskMult = 1.0f;  // albatross curse, channel pilot
    int m_eventId = -1;
    bool m_eventDone = false;
    Rival m_rivals[3];
    int m_best = 0;

    // Text for the outcome screen
    std::string m_outTitle;
    std::vector<std::string> m_outLines;
    int m_nextAfterOutcome = 0;

    std::vector<Button> m_buttons;
    int m_quickDraws = 0;

    // Randomness
    uint32_t nextRand();
    float rand01();
    int randInt(int lo, int hi);
    int pickWeather(int leg);

    // Flow
    void newVoyage(int cargo);
    void beginLeg();
    void sail(int plan);
    void afterLeg();
    void chooseEvent(int option);
    void putIntoPort(bool stop);
    void finishVoyage();

    // Model
    float legSpeed(int weather, int plan, int hull, int crew) const;
    float legRisk(int weather, int plan, int crew) const;
    void simulateRivals();
    int playerRank() const;
    int voyageValue(std::vector<std::string>* breakdown) const;

    // Drawing
    void render();
    void drawTitle();
    void drawHelp();
    void drawCargo();
    void drawLeg();
    void drawOutcome();
    void drawEvent();
    void drawPort();
    void drawResult();
    void drawStandings(int y);
    void drawShipStatus(int y);
    int addButton(const UI::Rect& r, int action);
    void loadBest();
    void saveBest();
};
