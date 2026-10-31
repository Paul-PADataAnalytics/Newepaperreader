#pragma once

#include "Application.h"
#include "ui/UIKit.h"
#include <string>
#include <vector>

// The Keeper - a short survival game for the e-paper reader.
//
// Winter 1891, a lighthouse on a rock off the coast. Twenty-one days until
// the relief boat. Each day you have two tasks (fish, gather fuel from the
// shore, trim the lamp, rest, or spend the whole day rowing ashore for
// supplies when the sea allows). Each night you keep watch or sleep, the
// lamp burns oil, the cold burns coal, and ships pass the reef. When the
// light fails, ships are lost; in gales, ships in distress need help.
class KeeperApp : public Application {
public:
    void onCreate() override;
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
    bool onBack() override;

private:
    enum Screen { TITLE, HELP, DAY, NIGHT, DISTRESS, REPORT, END };

    struct Button {
        UI::Rect rect;
        int action;
    };

    Screen m_screen = TITLE;
    uint32_t m_rng = 1;
    bool m_inProgress = false;

    // State
    int m_day = 0;          // 0-based
    int m_weather = 1;
    bool m_stormYesterday = false;
    int m_oil = 0, m_food = 0, m_coal = 0, m_spirit = 0, m_lamp = 0, m_rockets = 0;
    int m_blocks = 2;       // tasks left today
    std::vector<std::string> m_dayLog;

    // Tallies
    int m_safe = 0, m_wrecks = 0, m_lives = 0;
    int m_best = 0;
    int m_helpPage = 0;
    bool m_takenOff = false;

    // Night resolution
    bool m_watch = false;
    std::string m_reportTitle;
    std::vector<std::string> m_report;
    std::string m_distressShip;

    std::vector<Button> m_buttons;
    int m_quickDraws = 0;

    uint32_t nextRand();
    float rand01();
    int randInt(int lo, int hi);

    void newGame();
    void beginDay();
    void doTask(int task);
    void resolveNight(bool watch);
    void resolveDistress(int choice);
    void finishNight();
    int score() const;

    void render();
    void drawTitle();
    void drawHelp();
    void drawDay();
    void drawNight();
    void drawDistress();
    void drawReport();
    void drawEnd();
    void drawSupplies(int y);
    void addButton(const UI::Rect& r, int action) { m_buttons.push_back({r, action}); }
    void loadBest();
    void saveBest();
};
