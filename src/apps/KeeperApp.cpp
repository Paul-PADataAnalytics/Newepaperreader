#include "KeeperApp.h"
#include "ui/UIFramework.h"
#include "reader/AppStorage.h"
#include <algorithm>
#include <cstdio>
#include <ctime>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#endif

extern uint8_t* framebuffer;

namespace {
using namespace UI;
constexpr float GAME_SMALL = FONT_BODY - 2;
constexpr float GAME_CAPTION = FONT_BODY - 2;

void gameSectionLabel(int y, const std::string& label) {
    text(label, MARGIN, y, FONT_BODY - 2, DARK, true);
}

void drawPageFooter(int page, int pages) {
    fillRect(0, H - FOOTER_H, W, 2, BLACK);
    Rect previous(MARGIN, H - FOOTER_H + 12, 118, 64);
    Rect next(W - MARGIN - 118, H - FOOTER_H + 12, 118, 64);
    button(previous, "Previous", ButtonStyle::SECONDARY, FONT_BODY - 2, page > 0);
    button(next, "Next", ButtonStyle::SECONDARY, FONT_BODY - 2, page + 1 < pages);
    textCentered("Page " + std::to_string(page + 1) + " of " + std::to_string(pages),
                 W / 2, H - FOOTER_H + 27, FONT_BODY - 2, DARK);
}

// Balanced with a Monte Carlo model: careful play survives ~98% of winters
// with ~2 wrecks; never rowing ashore averages ~7.5 wrecks, always sleeping
// ~4.7, and careless play is taken off the rock in about a quarter of games.
constexpr int DAYS = 21;
enum Weather { CALM, FRESH, GALE, STORM, FOG };
const int kWeatherWeight[5] = {3, 4, 3, 2, 2};
const int kFish[5] = {3, 2, 1, 0, 1};
const int kShips[5] = {2, 2, 1, 1, 2};
const float kWreckIfDark[5] = {0.1f, 0.2f, 0.4f, 0.6f, 0.6f};
const char* kWeatherName[5] = {"Calm", "Fresh breeze", "Gale", "Storm", "Fog"};
const char* kWeatherDay[5] = {
    "A still, grey sea. The gulls sit on the water.",
    "A fresh westerly. White horses out beyond the reef.",
    "A gale from the south-west. Spray bursts over the gallery.",
    "A full storm. The whole tower shudders with every sea.",
    "Thick fog. You can barely see the foot of the tower.",
};
const char* kShipNames[] = {
    "the brig Mary Ann", "the collier Grace", "the schooner Kittiwake", "the steamer Lady of Avenel",
    "the barque Hesperus", "the trawler Good Intent", "the packet Mona", "the ketch Providence",
    "the coaster Jenny Lind", "the fishing smack Brothers",
};

enum Task { T_FISH, T_GATHER, T_TRIM, T_REST, T_ROW };
enum Action {
    A_NEW = 1, A_CONTINUE, A_HELP, A_BACK,
    A_TASK_FISH, A_TASK_GATHER, A_TASK_TRIM, A_TASK_REST, A_TASK_ROW,
    A_LIGHT, A_WATCH, A_SLEEP,
    A_ROCKETS, A_BOAT, A_PRAY,
    A_NEXT, A_DONE,
};

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int oilPerNight(int w) { return (w == STORM || w == FOG) ? 3 : 2; }
int coalPerNight(int w) { return (w == GALE || w == STORM) ? 2 : 1; }

// Two-line option button (bold title + caption), optionally disabled.
void optionButton(const Rect& r, const std::string& title, const std::string& detail, bool primary, bool enabled = true) {
    if (primary && enabled) {
        fillRoundRect(r.x, r.y, r.w, r.h, 14, BLACK);
    } else {
        fillRoundRect(r.x, r.y, r.w, r.h, 14, WHITE);
        strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, enabled ? BLACK : LIGHT);
    }
    uint8_t fg = primary && enabled ? WHITE : (enabled ? BLACK : MID);
    uint8_t sub = primary && enabled ? LIGHT : (enabled ? DARK : LIGHT);
    int lh1 = lineHeight(FONT_BODY - 2);
    int lh2 = detail.empty() ? 0 : lineHeight(GAME_CAPTION) + 4;
    int y = r.y + (r.h - lh1 - lh2) / 2;
    text(ellipsize(title, r.w - 32, FONT_BODY - 2, true), r.x + 18, y, FONT_BODY - 2, fg, true);
    if (!detail.empty()) text(ellipsize(detail, r.w - 32, GAME_CAPTION), r.x + 18, y + lh1 + 4, GAME_CAPTION, sub);
}

void meter(int y, const std::string& label, int pct) {
    text(label, MARGIN, y, GAME_SMALL, INK);
    textRight(std::to_string(pct) + "%", W - MARGIN, y, GAME_SMALL, pct < 35 ? BLACK : INK, true);
    progressBar(MARGIN + 130, y + 9, W - 2 * MARGIN - 130 - 70, 8, pct / 100.0f);
}
} // namespace

// ---------------------------------------------------------------------------

void KeeperApp::onCreate() {
#ifndef NATIVE_TESTING
    m_rng = esp_random() | 1;
#else
    m_rng = (uint32_t)time(nullptr) | 1;
#endif
    m_screen = TITLE;
    loadBest();
}

void KeeperApp::draw() {
    m_quickDraws = 0;
    render();
}

bool KeeperApp::onBack() {
    if (m_screen == TITLE) return false;
    if (m_screen == END) m_inProgress = false;
    m_screen = TITLE;
    render();
    return true;
}

uint32_t KeeperApp::nextRand() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return m_rng;
}
float KeeperApp::rand01() { return (nextRand() & 0xFFFFFF) / 16777216.0f; }
int KeeperApp::randInt(int lo, int hi) { return lo + (int)(nextRand() % (uint32_t)(hi - lo + 1)); }

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

void KeeperApp::newGame() {
    m_inProgress = true;
    m_takenOff = false;
    m_day = 0;
    m_weather = FRESH;
    m_stormYesterday = false;
    m_oil = 26;
    m_food = 6;
    m_coal = 8;
    m_spirit = 70;
    m_lamp = 100;
    m_rockets = 3;
    m_safe = m_wrecks = m_lives = 0;
    beginDay();
}

void KeeperApp::beginDay() {
    if (rand01() >= 0.45f) {
        int total = 0;
        for (int w : kWeatherWeight) total += w;
        int r = randInt(0, total - 1);
        for (int i = 0; i < 5; i++) {
            if (r < kWeatherWeight[i]) { m_weather = i; break; }
            r -= kWeatherWeight[i];
        }
    }
    m_blocks = 2;
    m_dayLog.clear();
    m_screen = DAY;
    render();
}

void KeeperApp::doTask(int task) {
    switch (task) {
        case T_FISH: {
            if (m_weather == STORM) return;
            int n = kFish[m_weather] + randInt(0, 1);
            m_food += n;
            m_dayLog.push_back(n == 0 ? "Fished off the rocks. Nothing." :
                               "Fished off the rocks: " + std::to_string(n) + (n == 1 ? " day" : " days") + " of food.");
            break;
        }
        case T_GATHER: {
            if (m_weather == STORM) return;
            int n = 2 + (m_stormYesterday ? 3 : 0) + randInt(0, 1);
            m_coal += n;
            m_dayLog.push_back(m_stormYesterday ? "Last night's sea left wreckage on the shingle: +" + std::to_string(n) + " fuel."
                                                : "Gathered driftwood and sea-coal: +" + std::to_string(n) + " fuel.");
            break;
        }
        case T_TRIM:
            m_lamp = std::min(100, m_lamp + 40);
            m_dayLog.push_back("Trimmed the wick and polished the lens. Lamp " + std::to_string(m_lamp) + "%.");
            break;
        case T_REST:
            m_spirit = std::min(100, m_spirit + 12);
            m_dayLog.push_back("Rested, read, and wrote a letter home. Spirit +12.");
            break;
        case T_ROW:
            if (m_blocks < 2 || (m_weather != CALM && m_weather != FRESH)) return;
            if (m_weather == FRESH && rand01() < 0.2f) {
                m_spirit -= 25;
                m_dayLog.push_back("The boat broached in the surf off the slip. You got back soaked and empty-handed. Spirit -25.");
            } else {
                m_oil += 15;
                m_food += 6;
                m_coal += 5;
                m_rockets += 1;
                m_spirit = std::min(100, m_spirit + 10);
                m_dayLog.push_back("Rowed to the harbour: oil +15, food +6, fuel +5, a rocket, and news from the village. Spirit +10.");
            }
            m_blocks = 0;
            render();
            return;
    }
    m_blocks--;
    render();
}

void KeeperApp::resolveNight(bool watch) {
    m_watch = watch;
    m_report.clear();
    int w = m_weather;
    int needOil = oilPerNight(w);
    bool noOil = m_oil < needOil;
    m_oil = std::max(0, m_oil - needOil);
    m_food -= 1;
    m_coal -= coalPerNight(w);
    m_spirit += (watch ? -10 : 3) - 3;
    if (m_food < 0) {
        m_food = 0;
        m_spirit -= 15;
        m_report.push_back("No food left. You went to bed hungry. Spirit -15.");
    }
    if (m_coal < 0) {
        m_coal = 0;
        m_spirit -= 15;
        m_report.push_back("No fuel for the stove. A bitter, freezing night. Spirit -15.");
    }

    float fail = (100 - m_lamp) / 200.0f * (watch ? 0.3f : 1.0f);
    bool dark = noOil || rand01() < fail;
    m_lamp = std::max(0, m_lamp - (w == STORM ? 25 : 15));

    int ships = kShips[w] + randInt(0, 1);
    int lost = 0;
    std::string lostName;
    for (int s = 0; s < ships; s++) {
        float p = dark ? kWreckIfDark[w] : 0.0f;
        if (w == FOG && !watch) p += 0.3f;
        if (rand01() < p) {
            lost++;
            lostName = kShipNames[randInt(0, 9)];
        }
    }
    m_wrecks += lost;
    m_safe += ships - lost;

    if (noOil) m_report.push_back("The oil ran out in the small hours and the light went dark.");
    else if (dark) m_report.push_back(watch ? "The wick guttered and the light failed for an hour before you could relight it."
                                            : "The wick guttered while you slept. The light was dark for hours.");
    if (lost > 0) {
        m_reportTitle = lost == 1 ? "A wreck on the reef" : std::to_string(lost) + " ships lost";
        m_report.push_back("At first light you saw what was left of " + lostName + " on the reef.");
    } else {
        m_reportTitle = dark ? "A lucky night" : (ships == 1 ? "One ship passed safely" : std::to_string(ships) + " ships passed safely");
    }
    if (w == FOG) m_report.push_back(watch ? "You rang the fog bell all night. Your arms ache." : "You slept through the fog. No bell rang.");
    else if (watch) m_report.push_back("You kept watch until dawn.");

    // A ship in distress?
    if ((w == GALE || w == STORM) && rand01() < (w == STORM ? 0.35f : 0.2f)) {
        m_distressShip = kShipNames[randInt(0, 9)];
        m_screen = DISTRESS;
        render();
        return;
    }
    finishNight();
}

void KeeperApp::resolveDistress(int choice) {
    if (choice == A_ROCKETS && m_rockets > 0) {
        m_rockets--;
        if (rand01() < 0.75f) {
            int lives = randInt(8, 20);
            m_lives += lives;
            m_report.push_back("Your rockets lit the reef and " + m_distressShip + " clawed off into deep water. " +
                               std::to_string(lives) + " lives saved.");
        } else {
            m_wrecks++;
            m_report.push_back("The rockets flared, but " + m_distressShip + " could not answer her helm and struck.");
        }
    } else if (choice == A_BOAT && m_spirit >= 30) {
        if (rand01() < 0.65f) {
            int lives = randInt(8, 20);
            m_lives += lives;
            m_spirit -= 10;
            m_report.push_back("You launched the boat into the teeth of it and took " + std::to_string(lives) +
                               " souls off " + m_distressShip + " before she broke up. Exhausted. Spirit -10.");
        } else {
            m_wrecks++;
            m_spirit -= 25;
            m_report.push_back("The boat was beaten back again and again. " + m_distressShip + " was lost. Spirit -25.");
        }
    } else {
        if (rand01() < 0.4f) {
            m_report.push_back("You watched, helpless, as " + m_distressShip + " somehow wore round and stood clear.");
        } else {
            m_wrecks++;
            m_report.push_back("You watched, helpless, as " + m_distressShip + " drove onto the reef.");
        }
    }
    m_reportTitle = "A night of storm";
    finishNight();
}

void KeeperApp::finishNight() {
    m_stormYesterday = m_weather == GALE || m_weather == STORM;
    m_spirit = clampi(m_spirit, -100, 100);
    if (m_spirit <= 0) {
        m_takenOff = true;
        m_spirit = 0;
        m_wrecks += 3; // the light goes untended until a relief is sent
    }
    m_screen = REPORT;
    render();
}

int KeeperApp::score() const {
    int s = m_safe * 10 + m_lives * 5 - m_wrecks * 50 + (m_takenOff ? 0 : 100);
    return s < 0 ? 0 : s;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void KeeperApp::loadBest() {
    m_best = 0;
    std::string path = AppStorage::toVfsPath(AppStorage::toRuntimePath("/data/keeper_best.txt"));
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    if (fscanf(f, "%d", &m_best) != 1) m_best = 0;
    fclose(f);
}

void KeeperApp::saveBest() {
    std::string path = AppStorage::toVfsPath(AppStorage::toRuntimePath("/data/keeper_best.txt"));
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "%d\n", m_best);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

void KeeperApp::handleTouch(int x, int y) {
    if (m_screen == HELP) {
        if (UI::hitHeaderAction(x, y, "Done")) {
            m_screen = TITLE;
            render();
            return;
        }
        if (x >= UI::MARGIN && x < UI::MARGIN + 118 && y >= UI::H - UI::FOOTER_H + 12 &&
            y < UI::H - 12 && m_helpPage > 0) {
            --m_helpPage;
            render();
            return;
        }
        if (x >= UI::W - UI::MARGIN - 118 && x < UI::W - UI::MARGIN &&
            y >= UI::H - UI::FOOTER_H + 12 && y < UI::H - 12 && m_helpPage < 1) {
            ++m_helpPage;
            render();
            return;
        }
    }
    int action = 0;
    for (const Button& b : m_buttons) {
        if (b.rect.contains(x, y)) { action = b.action; break; }
    }
    switch (action) {
        case A_NEW: newGame(); break;
        case A_CONTINUE: m_screen = DAY; render(); break;
        case A_HELP: m_helpPage = 0; m_screen = HELP; render(); break;
        case A_BACK: m_screen = TITLE; render(); break;
        case A_TASK_FISH: doTask(T_FISH); break;
        case A_TASK_GATHER: doTask(T_GATHER); break;
        case A_TASK_TRIM: doTask(T_TRIM); break;
        case A_TASK_REST: doTask(T_REST); break;
        case A_TASK_ROW: doTask(T_ROW); break;
        case A_LIGHT: m_screen = NIGHT; render(); break;
        case A_WATCH: resolveNight(true); break;
        case A_SLEEP: resolveNight(false); break;
        case A_ROCKETS: case A_BOAT: case A_PRAY: resolveDistress(action); break;
        case A_NEXT:
            if (m_takenOff || m_day + 1 >= DAYS) {
                m_inProgress = false;
                if (score() > m_best) { m_best = score(); saveBest(); }
                m_screen = END;
                render();
            } else {
                m_day++;
                beginDay();
            }
            break;
        case A_DONE: m_screen = TITLE; render(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void KeeperApp::render() {
    m_buttons.clear();
    auto content = [this]() {
        switch (m_screen) {
            case TITLE: drawTitle(); break;
            case HELP: drawHelp(); break;
            case DAY: drawDay(); break;
            case NIGHT: drawNight(); break;
            case DISTRESS: drawDistress(); break;
            case REPORT: drawReport(); break;
            case END: drawEnd(); break;
        }
    };
    if (m_screen == TITLE || m_screen == END || m_quickDraws >= 5) {
        m_quickDraws = 0;
        UIFramework::performFullScreenDraw(framebuffer, content);
    } else {
        m_quickDraws++;
        UIFramework::performQuickFullScreenDraw(framebuffer, content);
    }
}

void KeeperApp::drawSupplies(int y) {
    gameSectionLabel(y, "Stores");
    y += 30;
    struct Cell { const char* label; std::string value; std::string hint; };
    int oilNights = m_oil / oilPerNight(m_weather);
    Cell cells[4] = {
        {"Lamp oil", std::to_string(m_oil) + " gal", "~" + std::to_string(oilNights) + " nights"},
        {"Food", std::to_string(m_food) + (m_food == 1 ? " day" : " days"), ""},
        {"Fuel", std::to_string(m_coal), "-" + std::to_string(coalPerNight(m_weather)) + " tonight"},
        {"Rockets", std::to_string(m_rockets), ""},
    };
    int cw = (W - 2 * MARGIN) / 4;
    for (int i = 0; i < 4; i++) {
        int x = MARGIN + i * cw;
        text(ellipsize(cells[i].label, cw - 8, GAME_CAPTION), x, y, GAME_CAPTION, DARK);
        text(ellipsize(cells[i].value, cw - 8, FONT_BODY - 2, true), x, y + 30, FONT_BODY - 2, BLACK, true);
        if (!cells[i].hint.empty())
            text(ellipsize(cells[i].hint, cw - 8, GAME_CAPTION), x, y + 60, GAME_CAPTION, DARK);
    }
    y += 90;
    meter(y, "Spirit", m_spirit);
    meter(y + 32, "Lamp", m_lamp);
}

void KeeperApp::drawTitle() {
    header("The Keeper", true);
    icon(ICON_LIGHTHOUSE, (W - 200) / 2, 130, 200);
    textWrapped("Winter, 1891. You are alone on the Skerry light, a tower on a rock off a bad coast. "
                "The relief boat comes in twenty-one days. Keep the light burning. Keep your wits.",
                MARGIN + 10, 360, W - 2 * MARGIN - 20, FONT_BODY - 2, INK, 0, 8, true);
    if (m_best > 0) textCentered("Best winter: " + std::to_string(m_best) + " points", W / 2, 560, GAME_SMALL, DARK, true);
    int y = H - 300;
    if (m_inProgress) {
        Rect c(MARGIN, y, W - 2 * MARGIN, 76);
        optionButton(c, "Continue", "Day " + std::to_string(m_day + 1) + " of 21", false);
        addButton(c, A_CONTINUE);
    }
    y += 92;
    Rect n(MARGIN, y, W - 2 * MARGIN, 76);
    button(n, m_inProgress ? "Start a new winter" : "Begin", ButtonStyle::PRIMARY, FONT_BODY);
    addButton(n, A_NEW);
    Rect h(MARGIN, y + 92, W - 2 * MARGIN, 64);
    button(h, "How to play", ButtonStyle::SECONDARY, GAME_SMALL + 2);
    addButton(h, A_HELP);
}

void KeeperApp::drawHelp() {
    header("How to play", true, "Done");
    textCentered("Page " + std::to_string(m_helpPage + 1) + " of 2",
                 W / 2, HEADER_H + 18, FONT_BODY - 2, DARK);
    const char* pages[] = {
        "Survive 21 days and nights on the rock, and lose as few ships as you can. Each day, use two tasks: fish, gather fuel (more after storms), trim the lamp, or rest. In calm or fresh weather, row to the harbour for supplies.",
        "Each night the lamp burns oil and the stove burns fuel. Hunger, cold and keeping watch wear down your spirit. Tend the lamp; watch for ships and ring the fog bell. In gales, rescue ships in distress. If your spirit breaks, you are taken off the rock."
    };
    textWrapped(pages[m_helpPage], MARGIN + 18, HEADER_H + 92, W - 2 * MARGIN - 36,
                FONT_BODY, INK, 0, 14);
    drawPageFooter(m_helpPage, 2);
}

void KeeperApp::drawDay() {
    header("Day " + std::to_string(m_day + 1) + " of 21", true);
    int y = HEADER_H + 16;
    Rect card(MARGIN, y, W - 2 * MARGIN, 132);
    fillRoundRect(card.x, card.y, card.w, card.h, 14, PALE);
    text(kWeatherName[m_weather], card.x + 18, card.y + 12, FONT_TITLE - 2, BLACK, true);
    textWrapped(kWeatherDay[m_weather], card.x + 18, card.y + 56, card.w - 36, GAME_SMALL, INK, 2, 4);

    drawSupplies(y + 146);

    int ly = y + 146 + 30 + 90 + 72;
    gameSectionLabel(ly, m_blocks > 0 ? (m_blocks == 2 ? "Two tasks today" : "One more task") : "Today");
    ly += 30;
    if (m_spirit < 30) {
        ly = textWrapped("You are close to breaking. Rest, or row ashore for company.", MARGIN, ly, W - 2 * MARGIN,
                         GAME_SMALL, BLACK, 2, 4) + 8;
    }
    for (const std::string& l : m_dayLog) ly = textWrapped(l, MARGIN, ly, W - 2 * MARGIN, GAME_SMALL, INK, 2, 4) + 6;

    int gy = H - 290;
    if (m_blocks <= 0) {
        Rect light(MARGIN, H - 120, W - 2 * MARGIN, 84);
        optionButton(light, "Dusk: light the lamp", "Climb the 96 steps for the night", true);
        addButton(light, A_LIGHT);
        return;
    }
    bool storm = m_weather == STORM;
    bool canRow = m_blocks == 2 && (m_weather == CALM || m_weather == FRESH);
    int cw = (W - 2 * MARGIN - 12) / 2;
    Rect fish(MARGIN, gy, cw, 80), gather(MARGIN + cw + 12, gy, cw, 80);
    Rect trim(MARGIN, gy + 90, cw, 80), rest(MARGIN + cw + 12, gy + 90, cw, 80);
    Rect row(MARGIN, gy + 180, W - 2 * MARGIN, 80);
    optionButton(fish, "Fish", storm ? "Too rough today" : "About " + std::to_string(kFish[m_weather]) + "-" + std::to_string(kFish[m_weather] + 1) + " days' food", false, !storm);
    optionButton(gather, "Gather fuel", storm ? "Too rough today" : (m_stormYesterday ? "Wreckage on the shore!" : "About 2-3 fuel"), false, !storm);
    optionButton(trim, "Trim the lamp", "Lamp +40%", false);
    optionButton(rest, "Rest", "Spirit +12", false);
    std::string rowDetail = canRow ? (m_weather == FRESH ? "Whole day. Oil, food, fuel. 1 in 5 chance of a soaking"
                                                         : "Whole day. Oil, food, fuel, a rocket")
                                   : (m_blocks < 2 ? "Takes the whole day" : "Not in this sea");
    optionButton(row, "Row to the harbour", rowDetail, false, canRow);
    if (!storm) { addButton(fish, A_TASK_FISH); addButton(gather, A_TASK_GATHER); }
    addButton(trim, A_TASK_TRIM);
    addButton(rest, A_TASK_REST);
    if (canRow) addButton(row, A_TASK_ROW);
}

void KeeperApp::drawNight() {
    header("Night " + std::to_string(m_day + 1), true);
    int y = HEADER_H + 24;
    text("Dusk", MARGIN, y, FONT_TITLE + 4, BLACK, true);
    y += 64;
    static const char* kNight[5] = {
        "A clear, still night. Ships will be about.",
        "A cold, bright night with a fresh wind.",
        "The gale is rising. Few ships will be out, and those that are will be in trouble.",
        "A black, roaring night. Anything caught on this coast is in danger.",
        "Fog. Ships will be feeling their way along the coast, listening for the bell.",
    };
    y = textWrapped(kNight[m_weather], MARGIN, y, W - 2 * MARGIN, FONT_BODY - 2, INK, 0, 6);
    std::string burn = "Tonight: " + std::to_string(oilPerNight(m_weather)) + " gal of oil, " +
                       std::to_string(coalPerNight(m_weather)) + " fuel, 1 day of food.";
    y = textWrapped(burn, MARGIN, y + 10, W - 2 * MARGIN, GAME_SMALL, DARK, 0, 6);
    drawSupplies(y + 30);

    bool fog = m_weather == FOG;
    Rect watch(MARGIN, H - 250, W - 2 * MARGIN, 96);
    Rect sleep(MARGIN, H - 140, W - 2 * MARGIN, 96);
    optionButton(watch, fog ? "Ring the fog bell all night" : "Keep watch all night",
                 std::string(fog ? "Ships can hear you. Spirit -13" : "Catch the lamp if it gutters. Spirit -13") +
                     (m_spirit <= 20 ? " (you may break)" : ""), true);
    optionButton(sleep, "Sleep", fog ? "No bell. Spirit +0" : "Hope the lamp holds. Spirit +0", false);
    addButton(watch, A_WATCH);
    addButton(sleep, A_SLEEP);
}

void KeeperApp::drawDistress() {
    header("Night " + std::to_string(m_day + 1), true);
    int y = HEADER_H + 24;
    text("Ship in distress!", MARGIN, y, FONT_TITLE + 4, BLACK, true);
    y += 64;
    y = textWrapped("Through the spray you see " + m_distressShip +
                    ", her sails in rags, driving towards the reef. She is firing guns for help.",
                    MARGIN, y, W - 2 * MARGIN, FONT_BODY - 2, INK, 0, 6);
    meter(y + 30, "Spirit", m_spirit);
    text("Rockets: " + std::to_string(m_rockets), MARGIN, y + 70, GAME_SMALL, INK, true);

    bool rockets = m_rockets > 0;
    bool boat = m_spirit >= 30;
    Rect r1(MARGIN, H - 360, W - 2 * MARGIN, 96);
    Rect r2(MARGIN, H - 250, W - 2 * MARGIN, 96);
    Rect r3(MARGIN, H - 140, W - 2 * MARGIN, 96);
    optionButton(r1, "Fire rockets to guide her", rockets ? "Good odds (3 in 4). Uses a rocket" : "No rockets left", true, rockets);
    optionButton(r2, "Launch the boat", boat ? "Fair odds (2 in 3). Costs spirit, more if you fail" : "You haven't the strength left", false, boat);
    optionButton(r3, "Watch and pray", "She might claw off (2 in 5)", false);
    if (rockets) addButton(r1, A_ROCKETS);
    if (boat) addButton(r2, A_BOAT);
    addButton(r3, A_PRAY);
}

void KeeperApp::drawReport() {
    bool last = m_takenOff || m_day + 1 >= DAYS;
    header(last ? "The last night" : "Morning, day " + std::to_string(m_day + 2), true);
    int y = HEADER_H + 24;
    y = textWrapped(m_reportTitle, MARGIN, y, W - 2 * MARGIN, FONT_TITLE + 4, BLACK, 2, 4);
    y += 16;
    for (const std::string& l : m_report) y = textWrapped(l, MARGIN, y, W - 2 * MARGIN, GAME_SMALL + 2, INK, 3, 6) + 12;
    if (m_takenOff) {
        y = textWrapped("Something in you gave way in the night. A passing trawler found the light dark and you "
                        "talking to the gulls. They took you off the rock.",
                        MARGIN, y + 6, W - 2 * MARGIN, GAME_SMALL + 2, BLACK, 0, 6) + 12;
    }
    int ty = std::max(y + 10, H - 420);
    gameSectionLabel(ty, "Tally");
    text("Ships safe " + std::to_string(m_safe), MARGIN, ty + 30, GAME_SMALL, INK, true);
    textCentered("Wrecks " + std::to_string(m_wrecks), W / 2, ty + 30, GAME_SMALL, m_wrecks ? BLACK : INK, true);
    textRight("Lives saved " + std::to_string(m_lives), W - MARGIN, ty + 30, GAME_SMALL, INK, true);
    drawSupplies(ty + 74);
    Rect b(MARGIN, H - 100, W - 2 * MARGIN, 72);
    button(b, last ? "The end of the watch" : "Continue", ButtonStyle::PRIMARY, FONT_BODY);
    addButton(b, A_NEXT);
}

void KeeperApp::drawEnd() {
    header(m_takenOff ? "Taken off the rock" : "The relief boat", true);
    int y = HEADER_H + 24;
    icon(ICON_LIGHTHOUSE, MARGIN, y, 90);
    std::string headline = m_takenOff ? "Your watch is over" : "You kept the light";
    textWrapped(headline, MARGIN + 110, y + 10, W - 2 * MARGIN - 110, FONT_TITLE, BLACK, 2, 4);
    y += 120;
    if (!m_takenOff) y = textWrapped("On the twenty-first morning the relief boat came round the point.",
                                     MARGIN, y, W - 2 * MARGIN, GAME_SMALL + 2, INK, 0, 6) + 16;
    else y += 10;

    auto row = [&](const std::string& label, const std::string& value) {
        text(label, MARGIN, y, FONT_BODY - 2, INK);
        textRight(value, W - MARGIN, y, FONT_BODY - 2, BLACK, true);
        hRule(y + 42);
        y += 52;
    };
    row("Days on the rock", std::to_string(m_day + 1));
    row("Ships guided safely", std::to_string(m_safe));
    row("Lives saved", std::to_string(m_lives));
    row("Ships lost", std::to_string(m_wrecks));
    y += 6;
    text("Score", MARGIN, y, FONT_BODY + 4, BLACK, true);
    textRight(std::to_string(score()), W - MARGIN, y, FONT_BODY + 4, BLACK, true);
    y += 60;
    const char* rating = m_takenOff ? "The board of Trinity House will find another keeper."
                       : m_wrecks == 0 ? "Not a ship lost all winter. They will name a lifeboat after you."
                       : m_wrecks <= 2 ? "A fine keeper. The coast is safer for you."
                       : m_wrecks <= 5 ? "A hard winter. The board will want a report."
                                       : "The light failed too often. The fishermen won't meet your eye.";
    y = textWrapped(rating, MARGIN, y, W - 2 * MARGIN, FONT_BODY - 2, BLACK, 3, 6);
    text(score() > 0 && score() >= m_best ? "New best!" : "Best " + std::to_string(m_best), MARGIN, y + 12, GAME_SMALL, DARK, true);

    Rect again(MARGIN, H - 100, (W - 2 * MARGIN - 16) / 2, 72);
    Rect done(again.x + again.w + 16, H - 100, again.w, 72);
    button(again, "Another winter", ButtonStyle::PRIMARY, GAME_SMALL + 2);
    button(done, "Done", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(again, A_NEW);
    addButton(done, A_DONE);
}
