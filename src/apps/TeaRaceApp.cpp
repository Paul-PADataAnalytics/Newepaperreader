#include "TeaRaceApp.h"
#include "DisplayHAL.h"
#include "ui/UIFramework.h"
#include "reader/AppStorage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#endif

extern uint8_t* framebuffer;

namespace {
using namespace UI;

// ---- Voyage data ----------------------------------------------------------
// Tuned with a Monte Carlo model: sensible play averages ~£3,100 and wins
// about a third of races; always cracking on is a high-variance gamble;
// always shortening sail never wins.

struct LegInfo {
    const char* name;
    float baseDays;
    int weatherWeights[5]; // calm, fair, fresh, gale, storm
    const char* intro;
};

const LegInfo kLegs[] = {
    {"Down the Min River", 6, {2, 4, 3, 1, 0}, "Loaded to the hatches, she drops down the river past the Pagoda Anchorage."},
    {"South China Sea", 14, {1, 3, 3, 2, 1}, "Typhoon season. Reefs, pirates and the rest of the fleet."},
    {"Sunda Strait", 4, {3, 4, 2, 1, 0}, "The narrow gateway to the Indian Ocean. Anjer lies ahead."},
    {"Indian Ocean", 16, {0, 3, 5, 2, 0}, "The long run across the trade winds."},
    {"Cape of Good Hope", 8, {0, 1, 3, 4, 2}, "The Agulhas current and the Cape rollers. Ships are lost here."},
    {"South Atlantic", 10, {1, 4, 4, 1, 0}, "The south-east trades carry her north towards St Helena."},
    {"The Doldrums", 9, {6, 3, 1, 0, 0}, "The calms of the equator. Every puff of wind counts."},
    {"North Atlantic", 14, {1, 3, 3, 2, 1}, "Past the Azores and into the westerlies."},
    {"The Channel", 6, {1, 3, 3, 2, 1}, "The Lizard, then a dash up-Channel to the Downs and Gravesend."},
};
constexpr int LEG_COUNT = 9;
constexpr int PORT_LEG_ANJER = 2;
constexpr int PORT_LEG_HELENA = 5;

const float kSpeed[5] = {0.55f, 1.0f, 1.2f, 1.3f, 0.95f};
const float kRisk[5] = {0.0f, 0.03f, 0.07f, 0.17f, 0.32f};
const float kSailSpeed[3] = {0.85f, 1.0f, 1.15f};
const float kSailRisk[3] = {0.3f, 1.0f, 2.6f};
const int kSailCrew[3] = {5, -2, -9};
const float kCalmSpeed[3] = {0.9f, 1.0f, 1.2f};
const int kCalmCrew[3] = {3, 0, -4};

const char* kForecast[5] = {
    "The glass is high and steady. Light airs, perhaps a flat calm.",
    "The glass is steady. Fair sailing weather.",
    "The glass is easing. A fresh breeze is building.",
    "The glass is falling. A gale is coming.",
    "The glass is plunging. A storm is on its way!",
};
const char* kActual[5] = {
    "The wind died away and the sails hung slack.",
    "Fair winds and blue water.",
    "A fresh breeze carried her along at a fine clip.",
    "A full gale blew, with big seas running.",
    "A storm: mountainous seas and screaming rigging.",
};
const char* kSailName[3] = {"Shorten sail", "Steady as she goes", "Crack on!"};

const char* kCargoName[2] = {"fine first-flush", "congou"};
const int kCargoChests[2] = {400, 600};
const int kCargoBase[2] = {3300, 2800};
const float kCargoRankMult[2][4] = {{1.25f, 1.0f, 0.85f, 0.75f}, {1.1f, 1.0f, 0.95f, 0.9f}};

// Events
enum EventId { EV_PIRATES, EV_BECALMED, EV_FEVER, EV_LEAK, EV_RIVAL, EV_ALBATROSS, EV_GRUMBLING, EV_WHALER, EV_PILOT, EV_COUNT };

// Actions (button ids)
enum Action {
    A_NEW = 1, A_CONTINUE_VOYAGE, A_HELP, A_BACK_TITLE,
    A_CARGO_FINE, A_CARGO_CONGOU,
    A_SAIL_0, A_SAIL_1, A_SAIL_2,
    A_NEXT,
    A_OPTION_0, A_OPTION_1, A_OPTION_2,
    A_PORT_STOP, A_PORT_SAIL,
    A_DONE,
};

std::string fmt1(float v) {
    char b[16];
    snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

std::string money(int v) {
    int a = v < 0 ? -v : v;
    char digits[16];
    snprintf(digits, sizeof(digits), "%d", a);
    std::string grouped;
    int len = (int)strlen(digits);
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) grouped += ',';
        grouped += digits[i];
    }
    return std::string(v < 0 ? "-" : "") + "\xC2\xA3" + grouped;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

uint32_t s_eventsSeen = 0;
} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void TeaRaceApp::onCreate() {
#ifndef NATIVE_TESTING
    m_rng = esp_random() | 1;
#else
    m_rng = (uint32_t)time(nullptr) | 1;
#endif
    m_screen = TITLE;
    m_leg = -1;
    loadBest();
}

void TeaRaceApp::draw() {
    m_quickDraws = 0;
    render();
}

bool TeaRaceApp::onBack() {
    switch (m_screen) {
        case TITLE:
            return false;
        case RESULT:
            m_leg = -1;
            m_screen = TITLE;
            break;
        default:
            // Mid-voyage the chevron returns to the title, which offers to
            // continue - nothing is lost by a stray tap.
            m_screen = TITLE;
            break;
    }
    render();
    return true;
}

// ---------------------------------------------------------------------------
// Randomness
// ---------------------------------------------------------------------------

uint32_t TeaRaceApp::nextRand() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return m_rng;
}

float TeaRaceApp::rand01() { return (nextRand() & 0xFFFFFF) / 16777216.0f; }

int TeaRaceApp::randInt(int lo, int hi) { return lo + (int)(nextRand() % (uint32_t)(hi - lo + 1)); }

int TeaRaceApp::pickWeather(int leg) {
    const int* w = kLegs[leg].weatherWeights;
    int total = 0;
    for (int i = 0; i < 5; i++) total += w[i];
    int r = randInt(0, total - 1);
    for (int i = 0; i < 5; i++) {
        if (r < w[i]) return i;
        r -= w[i];
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

float TeaRaceApp::legSpeed(int weather, int plan, int hull, int crew) const {
    float sail = weather == 0 ? kCalmSpeed[plan] : kSailSpeed[plan];
    return kSpeed[weather] * sail * (0.45f + 0.55f * hull / 100.0f) * (0.8f + 0.2f * crew / 100.0f);
}

float TeaRaceApp::legRisk(int weather, int plan, int crew) const {
    if (weather == 0) return 0.0f;
    float r = kRisk[weather] * kSailRisk[plan] * (1.0f + (100 - crew) / 150.0f) * m_riskMult;
    return r > 0.95f ? 0.95f : r;
}

void TeaRaceApp::simulateRivals() {
    for (Rival& r : m_rivals) {
        int plan;
        if (r.style == 0) plan = m_forecast < 3 ? 2 : 1;
        else if (r.style == 1) plan = m_forecast < 4 ? 1 : 0;
        else plan = m_forecast <= 1 ? 2 : (m_forecast < 3 ? 1 : 0);
        float speed = legSpeed(m_weather, plan, r.hull, r.crew);
        r.day += kLegs[m_leg].baseDays / speed * (0.97f + rand01() * 0.06f);
        float risk = m_weather == 0 ? 0.0f
                   : kRisk[m_weather] * kSailRisk[plan] * (1.0f + (100 - r.crew) / 150.0f);
        if (rand01() < risk) {
            int dmg = m_weather < 4 ? randInt(15, 30) : randInt(25, 45);
            if (r.hull - dmg < 25) r.day += 4;
            r.hull -= dmg;
        }
        if (m_weather >= 3) r.hull -= 2;
        r.hull = clampi(r.hull, 10, 100);
        r.crew = clampi(r.crew + (m_weather == 0 ? kCalmCrew[plan] : kSailCrew[plan]), 5, 100);
        if ((m_leg == PORT_LEG_ANJER || m_leg == PORT_LEG_HELENA) && r.hull < 55) {
            r.day += 2.5f;
            r.hull = clampi(r.hull + 35, 0, 100);
            r.crew = clampi(r.crew + 30, 0, 100);
        }
    }
}

int TeaRaceApp::playerRank() const {
    int rank = 1;
    for (const Rival& r : m_rivals) if (r.day < m_day) rank++;
    return rank;
}

int TeaRaceApp::voyageValue(std::vector<std::string>* breakdown) const {
    int rank = playerRank();
    float base = (float)kCargoBase[m_cargo];
    float mult = kCargoRankMult[m_cargo][rank - 1];
    float wet = std::min(0.6f, m_wet / 100.0f);
    float stale = std::max(0.0f, m_day - 95.0f) * 0.006f;
    float value = base * mult * (1.0f - wet) * (1.0f - stale) + m_money;
    if (breakdown) {
        breakdown->push_back(std::to_string(kCargoChests[m_cargo]) + " chests of " + kCargoName[m_cargo] + ": " + money((int)base));
        static const char* kPlace[4] = {"First home", "Second", "Third", "Last"};
        char m[16];
        snprintf(m, sizeof(m), "%.2f", mult);
        breakdown->push_back(std::string(kPlace[rank - 1]) + ": price x" + m);
        if (m_wet > 0) breakdown->push_back("Water-damaged chests: -" + std::to_string((int)(wet * 100)) + "%");
        if (stale > 0) breakdown->push_back("Tea past its best after " + std::to_string((int)m_day) + " days: -" + std::to_string((int)(stale * 100 + 0.5f)) + "%");
        if (m_money != 0) breakdown->push_back("Expenses at sea: " + money(m_money));
    }
    return (int)(value + 0.5f);
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

void TeaRaceApp::newVoyage(int cargo) {
    m_cargo = cargo;
    m_leg = 0;
    m_day = 0;
    m_hull = 100;
    m_crew = 100;
    m_wet = 0;
    m_money = 0;
    m_riskMult = 1.0f;
    s_eventsSeen = 0;
    m_rivals[0] = {"Ariel", 0, 0, 100, 100};
    m_rivals[1] = {"Fiery Cross", 1, 0, 100, 100};
    m_rivals[2] = {"Serica", 2, 0, 100, 100};
    beginLeg();
}

void TeaRaceApp::beginLeg() {
    m_weather = pickWeather(m_leg);
    m_forecast = m_weather;
    if (rand01() > 0.7f) m_forecast = clampi(m_weather + (rand01() < 0.5f ? -1 : 1), 0, 4);
    if (m_leg == LEG_COUNT - 1 && !(s_eventsSeen & (1u << EV_PILOT))) {
        s_eventsSeen |= 1u << EV_PILOT;
        m_eventId = EV_PILOT;
        m_screen = EVENT;
    } else {
        m_screen = LEG;
    }
    render();
}

void TeaRaceApp::sail(int plan) {
    const LegInfo& leg = kLegs[m_leg];
    float speed = legSpeed(m_weather, plan, m_hull, m_crew);
    float days = leg.baseDays / speed * (0.97f + rand01() * 0.06f);
    float risk = legRisk(m_weather, plan, m_crew);

    m_outLines.clear();
    m_outLines.push_back(kActual[m_weather]);

    if (rand01() < risk) {
        int dmg = m_weather < 4 ? randInt(15, 30) : randInt(25, 45);
        int wet = randInt(2, 8);
        static const char* kDamage[] = {
            "The fore topmast carried away with a crack like a cannon.",
            "A sea came green over the bow and stove in the boats.",
            "The main topsail split from head to foot.",
            "She broached and lay over on her beam ends before righting.",
        };
        m_outLines.push_back(kDamage[randInt(0, 3)]);
        if (m_hull - dmg < 25) {
            days += 4;
            m_outLines.push_back("Dismasted! Rigging a jury mast cost four days.");
        }
        m_hull -= dmg;
        m_wet += wet;
        m_outLines.push_back("Hull & rigging -" + std::to_string(dmg) + "%. Water in the hold: +" + std::to_string(wet) + "% wet chests.");
    } else if (m_weather >= 3) {
        m_outLines.push_back(plan == 2 ? "She took a hammering but held together." : "She rode it out well.");
    }
    if (m_weather >= 3) m_hull -= 2;
    m_hull = clampi(m_hull, 10, 100);
    int crewDelta = m_weather == 0 ? kCalmCrew[plan] : kSailCrew[plan];
    m_crew = clampi(m_crew + crewDelta, 5, 100);
    if (crewDelta < -5) m_outLines.push_back("The watches are worn out from constant sail handling.");
    else if (crewDelta > 0) m_outLines.push_back("An easy passage let the crew rest.");

    m_day += days;
    if (m_riskMult < 1.0f) m_riskMult = 1.0f; // the pilot only helps for one leg
    simulateRivals();

    // Rival news
    for (const Rival& r : m_rivals) {
        if (r.hull < 60 && rand01() < 0.4f) {
            m_outLines.push_back(std::string("A passing ship reports ") + r.name + " limping with damaged spars.");
            break;
        }
    }

    m_outTitle = fmt1(days) + " days";
    m_nextAfterOutcome = 1;
    m_screen = OUTCOME;
    render();
}

void TeaRaceApp::afterLeg() {
    if (m_leg == PORT_LEG_ANJER || m_leg == PORT_LEG_HELENA) {
        m_screen = PORT;
        render();
        return;
    }
    // A dilemma on roughly a third of legs.
    if (m_leg < LEG_COUNT - 1 && rand01() < 0.4f) {
        int candidates[EV_COUNT];
        int n = 0;
        auto ok = [&](int id) { return !(s_eventsSeen & (1u << id)); };
        if (ok(EV_PIRATES) && m_leg <= 2) candidates[n++] = EV_PIRATES;
        if (ok(EV_BECALMED) && (m_leg == 6 || m_weather == 0)) candidates[n++] = EV_BECALMED;
        if (ok(EV_FEVER) && m_leg >= 2 && m_leg <= 4) candidates[n++] = EV_FEVER;
        if (ok(EV_LEAK) && m_hull < 70) candidates[n++] = EV_LEAK;
        if (ok(EV_RIVAL) && m_leg >= 1) candidates[n++] = EV_RIVAL;
        if (ok(EV_ALBATROSS) && (m_leg == 4 || m_leg == 5)) candidates[n++] = EV_ALBATROSS;
        if (ok(EV_GRUMBLING) && m_crew < 45) candidates[n++] = EV_GRUMBLING;
        if (ok(EV_WHALER) && m_leg >= 5 && m_leg <= 7) candidates[n++] = EV_WHALER;
        if (n > 0) {
            m_eventId = candidates[randInt(0, n - 1)];
            s_eventsSeen |= 1u << m_eventId;
            m_screen = EVENT;
            render();
            return;
        }
    }
    m_leg++;
    if (m_leg >= LEG_COUNT) finishVoyage();
    else beginLeg();
}

void TeaRaceApp::putIntoPort(bool stop) {
    m_outLines.clear();
    m_eventId = -1;
    const char* port = m_leg == PORT_LEG_ANJER ? "Anjer" : "St Helena";
    if (stop) {
        m_day += 2.5f;
        m_hull = clampi(m_hull + 35, 0, 100);
        m_crew = clampi(m_crew + 30, 0, 100);
        m_money -= 40;
        m_outTitle = std::string("Two days at ") + port;
        m_outLines.push_back("New spars sent up, sails mended, fresh water and fruit for the hands.");
        m_outLines.push_back("Hull & rigging +35%. Crew +30%. Cost " + money(40) + ".");
    } else {
        m_outTitle = std::string("Past ") + port;
        m_outLines.push_back("She stood on without stopping. Every hour counts.");
    }
    m_leg++;
    m_nextAfterOutcome = 2;
    m_screen = OUTCOME;
    render();
}

void TeaRaceApp::chooseEvent(int option) {
    m_outLines.clear();
    switch (m_eventId) {
        case EV_PIRATES:
            if (option == 0) {
                if (rand01() < 0.6f) {
                    m_outTitle = "Outran them";
                    m_outLines.push_back("With every stitch set she left the lorcha wallowing astern.");
                } else {
                    m_outTitle = "Boarded!";
                    m_hull -= 20;
                    m_wet += 5;
                    m_outLines.push_back("They caught her in a squall. The hands drove them off, but the ship is knocked about and chests were broached.");
                    m_outLines.push_back("Hull & rigging -20%. Wet chests +5%.");
                }
            } else if (option == 1) {
                m_crew -= 10;
                m_outTitle = "A show of force";
                m_outLines.push_back("The guns were run out and the lorcha sheered off. Standing to quarters all night tired the crew.");
                m_outLines.push_back("Crew -10%.");
            } else {
                m_money -= 150;
                m_outTitle = "Paid off";
                m_outLines.push_back("Silver changed hands and the pirates let her pass. The owners will not be pleased. " + money(-150) + ".");
            }
            break;
        case EV_BECALMED:
            if (option == 0) {
                m_crew -= 12;
                m_day -= 1.5f;
                m_outTitle = "Towed by the boats";
                m_outLines.push_back("The hands pulled her through the calm until a breeze found the topsails. Saved 1.5 days. Crew -12%.");
            } else {
                m_day += 1.5f;
                m_crew += 5;
                m_outTitle = "Whistling for wind";
                m_outLines.push_back("The crew rested while she drifted. Lost 1.5 days. Crew +5%.");
            }
            break;
        case EV_FEVER:
            if (option == 0) {
                m_crew -= 15;
                m_outTitle = "Pressed on";
                m_outLines.push_back("The fever ran through the fo'c'sle. Short-handed watches for a week. Crew -15%.");
            } else {
                m_day += 3;
                m_crew += 5;
                m_outTitle = "Quarantined in a cove";
                m_outLines.push_back("Three days at anchor and the fever burned out. Lost 3 days. Crew +5%.");
            }
            break;
        case EV_LEAK:
            if (option == 0) {
                m_crew -= 12;
                m_outTitle = "Pumps manned";
                m_outLines.push_back("Watch after watch at the pumps kept the hold dry. Crew -12%.");
            } else if (option == 1) {
                m_day += 2;
                m_hull += 5;
                m_outTitle = "Eased her";
                m_outLines.push_back("Under easy sail the seams closed up and the carpenter caulked them. Lost 2 days. Hull +5%.");
            } else {
                m_wet += 8;
                m_outTitle = "Let her leak";
                m_outLines.push_back("The water found its way to the lower tier of chests. Wet chests +8%.");
            }
            break;
        case EV_RIVAL: {
            Rival* nearest = &m_rivals[0];
            for (Rival& r : m_rivals) if (std::fabs(r.day - m_day) < std::fabs(nearest->day - m_day)) nearest = &r;
            if (option == 0) {
                m_day -= 1.2f;
                m_outTitle = std::string("Raced ") + nearest->name;
                m_outLines.push_back("Royals and stunsails set, the two ships ran side by side for a day and a night. Gained 1.2 days.");
                if (rand01() < 0.3f) {
                    m_hull -= 15;
                    m_outLines.push_back("The main topgallant mast went over the side. Hull & rigging -15%.");
                }
            } else {
                m_outTitle = "Let her go";
                m_outLines.push_back(std::string(nearest->name) + " drew ahead and dropped below the horizon. A long way still to go.");
            }
            break;
        }
        case EV_ALBATROSS:
            if (option == 0) {
                m_crew += 3;
                m_outTitle = "Good luck";
                m_outLines.push_back("The great bird followed in her wake for days. The hands took it as a fine omen. Crew +3%.");
            } else {
                m_crew += 8;
                m_riskMult = 1.3f;
                m_outTitle = "Albatross for dinner";
                m_outLines.push_back("A welcome change from salt pork. Crew +8%. But the old hands are muttering about bad luck...");
            }
            break;
        case EV_GRUMBLING:
            if (option == 0) {
                m_money -= 30;
                m_crew += 15;
                m_outTitle = "Spliced the mainbrace";
                m_outLines.push_back("A double tot of rum all round. Crew +15%. " + money(-30) + ".");
            } else if (rand01() < 0.5f) {
                m_crew -= 10;
                m_outTitle = "Sullen hands";
                m_outLines.push_back("The crew turned to slowly and sullenly. Crew -10%.");
            } else {
                m_crew += 5;
                m_outTitle = "Order restored";
                m_outLines.push_back("A few hard words and the grumbling stopped. Crew +5%.");
            }
            break;
        case EV_WHALER:
            if (option == 0) {
                m_money -= 20;
                m_crew += 10;
                m_outTitle = "Gammed with the whaler";
                m_outLines.push_back("Tea for fresh water, potatoes and news. Crew +10%. " + money(-20) + ".");
                const Rival* lead = &m_rivals[0];
                for (const Rival& r : m_rivals) if (r.day < lead->day) lead = &r;
                m_outLines.push_back(std::string("The whaler spoke ") + lead->name + " a few days ago, going well.");
            } else {
                m_outTitle = "Sailed on";
                m_outLines.push_back("No time for gossip.");
            }
            break;
        case EV_PILOT:
            if (option == 0) {
                m_money -= 40;
                m_riskMult *= 0.5f;
                m_outTitle = "Pilot aboard";
                m_outLines.push_back("A Deal pilot came out in his lugger. He knows every sandbank in the Channel. Half the risk this leg. " + money(-40) + ".");
            } else {
                m_outTitle = "Own reckoning";
                m_outLines.push_back("You know these waters. Saved the pilot's fee.");
            }
            break;
        default:
            break;
    }
    m_hull = clampi(m_hull, 10, 100);
    m_crew = clampi(m_crew, 5, 100);
    if (m_eventId == EV_PILOT) {
        m_nextAfterOutcome = 3;
    } else {
        m_nextAfterOutcome = 2;
    }
    m_screen = OUTCOME;
    render();
}

void TeaRaceApp::finishVoyage() {
    int value = voyageValue(nullptr);
    if (value > m_best) {
        m_best = value;
        saveBest();
    }
    m_screen = RESULT;
    render();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void TeaRaceApp::loadBest() {
    m_best = 0;
    std::string path = AppStorage::toVfsPath(AppStorage::toRuntimePath("/data/tearace_best.txt"));
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    if (fscanf(f, "%d", &m_best) != 1) m_best = 0;
    fclose(f);
}

void TeaRaceApp::saveBest() {
    std::string path = AppStorage::toVfsPath(AppStorage::toRuntimePath("/data/tearace_best.txt"));
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "%d\n", m_best);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

int TeaRaceApp::addButton(const Rect& r, int action) {
    m_buttons.push_back({r, action});
    return action;
}

void TeaRaceApp::handleTouch(int x, int y) {
    int action = 0;
    for (const Button& b : m_buttons) {
        if (b.rect.contains(x, y)) {
            action = b.action;
            break;
        }
    }
    switch (action) {
        case A_NEW: m_screen = CARGO; render(); break;
        case A_CONTINUE_VOYAGE: m_screen = m_leg >= 0 && m_leg < LEG_COUNT ? LEG : TITLE; render(); break;
        case A_HELP: m_screen = HELP; render(); break;
        case A_BACK_TITLE: m_screen = TITLE; render(); break;
        case A_CARGO_FINE: newVoyage(0); break;
        case A_CARGO_CONGOU: newVoyage(1); break;
        case A_SAIL_0: case A_SAIL_1: case A_SAIL_2: sail(action - A_SAIL_0); break;
        case A_NEXT:
            if (m_nextAfterOutcome == 1) {
                afterLeg();
            } else if (m_nextAfterOutcome == 3) {
                m_screen = LEG;
                render();
            } else {
                if (m_screen == OUTCOME && m_eventId >= 0 && m_eventId != EV_PILOT) m_leg++;
                m_eventId = -1;
                if (m_leg >= LEG_COUNT) finishVoyage();
                else beginLeg();
            }
            break;
        case A_OPTION_0: case A_OPTION_1: case A_OPTION_2: chooseEvent(action - A_OPTION_0); break;
        case A_PORT_STOP: putIntoPort(true); break;
        case A_PORT_SAIL: putIntoPort(false); break;
        case A_DONE: m_leg = -1; m_screen = TITLE; render(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void TeaRaceApp::render() {
    m_buttons.clear();
    auto content = [this]() {
        switch (m_screen) {
            case TITLE: drawTitle(); break;
            case HELP: drawHelp(); break;
            case CARGO: drawCargo(); break;
            case LEG: drawLeg(); break;
            case OUTCOME: drawOutcome(); break;
            case EVENT: drawEvent(); break;
            case PORT: drawPort(); break;
            case RESULT: drawResult(); break;
        }
    };
    // Flash-free refreshes between turns, with a clean one now and then.
    if (m_screen == TITLE || m_screen == RESULT || m_quickDraws >= 5) {
        m_quickDraws = 0;
        UIFramework::performFullScreenDraw(framebuffer, content);
    } else {
        m_quickDraws++;
        UIFramework::performQuickFullScreenDraw(framebuffer, content);
    }
}

namespace {
// Two-line option button: bold title + caption.
void optionButton(const Rect& r, const std::string& title, const std::string& detail, bool primary = false) {
    if (primary) fillRoundRect(r.x, r.y, r.w, r.h, 14, BLACK);
    else {
        fillRoundRect(r.x, r.y, r.w, r.h, 14, WHITE);
        strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, BLACK);
    }
    uint8_t fg = primary ? WHITE : BLACK;
    uint8_t sub = primary ? LIGHT : DARK;
    int lh1 = lineHeight(FONT_BODY);
    int lh2 = detail.empty() ? 0 : lineHeight(FONT_CAPTION) + 6;
    int y = r.y + (r.h - lh1 - lh2) / 2;
    text(ellipsize(title, r.w - 40, FONT_BODY, true), r.x + 22, y, FONT_BODY, fg, true);
    if (!detail.empty()) text(ellipsize(detail, r.w - 40, FONT_CAPTION), r.x + 22, y + lh1 + 6, FONT_CAPTION, sub);
}

void meter(int y, const std::string& label, int pct, bool lowIsBad = true) {
    text(label, MARGIN, y, FONT_SMALL, INK);
    std::string v = std::to_string(pct) + "%";
    textRight(v, W - MARGIN, y, FONT_SMALL, (lowIsBad ? pct < 40 : pct > 15) ? BLACK : INK, true);
    progressBar(MARGIN + 190, y + 9, W - 2 * MARGIN - 190 - 70, 8, pct / 100.0f);
}
} // namespace

void TeaRaceApp::drawStandings(int y) {
    sectionLabel(y, "The race");
    y += 30;
    struct Entry { const char* name; float day; bool you; };
    Entry e[4] = {{"Taeping (you)", m_day, true}};
    for (int i = 0; i < 3; i++) e[i + 1] = {m_rivals[i].name, m_rivals[i].day, false};
    std::sort(e, e + 4, [](const Entry& a, const Entry& b) { return a.day < b.day; });
    for (int i = 0; i < 4; i++) {
        std::string pos = std::to_string(i + 1) + ".";
        text(pos, MARGIN, y, FONT_SMALL, INK, e[i].you);
        text(e[i].name, MARGIN + 34, y, FONT_SMALL, e[i].you ? BLACK : INK, e[i].you);
        float d = e[i].day - e[0].day;
        std::string gap = i == 0 ? "leading" : (d < 0.05f ? "level" : "+" + fmt1(d) + " days");
        textRight(gap, W - MARGIN, y, FONT_SMALL, e[i].you ? BLACK : DARK, e[i].you);
        y += 32;
    }
}

void TeaRaceApp::drawShipStatus(int y) {
    sectionLabel(y, "Your ship");
    meter(y + 30, "Hull & rigging", m_hull);
    meter(y + 62, "Crew", m_crew);
    meter(y + 94, "Wet chests", std::min(m_wet, 100), false);
}

void TeaRaceApp::drawTitle() {
    header("Tea Race 1866", true);
    icon(ICON_SHIP, (W - 220) / 2, 130, 220);
    textWrapped("Foochow, May 1866. The new season's tea is loaded and the fastest clippers in the world "
                "race for London. The first ship home sells at the premium.",
                MARGIN + 10, 380, W - 2 * MARGIN - 20, FONT_BODY - 2, INK, 0, 8, true);
    if (m_best > 0) textCentered("Best voyage: " + money(m_best), W / 2, 560, FONT_SMALL, DARK, true);

    int y = H - 300;
    if (m_leg >= 0 && m_leg < LEG_COUNT) {
        Rect c(MARGIN, y, W - 2 * MARGIN, 76);
        optionButton(c, "Continue voyage", "Leg " + std::to_string(m_leg + 1) + " of 9, day " + std::to_string((int)m_day));
        addButton(c, A_CONTINUE_VOYAGE);
    }
    y += 92;
    Rect n(MARGIN, y, W - 2 * MARGIN, 76);
    button(n, m_leg >= 0 && m_leg < LEG_COUNT ? "New voyage" : "Set sail", ButtonStyle::PRIMARY, FONT_BODY);
    addButton(n, A_NEW);
    Rect h(MARGIN, y + 92, W - 2 * MARGIN, 64);
    button(h, "How to play", ButtonStyle::SECONDARY, FONT_SMALL + 2);
    addButton(h, A_HELP);
}

void TeaRaceApp::drawHelp() {
    header("How to play", true);
    int y = HEADER_H + 24;
    const char* para[] = {
        "Race three rival clippers from Foochow to London over nine legs.",
        "Each leg, read the glass (the barometer). It is right about seven times in ten.",
        "Then choose your sail plan. Cracking on is fastest but risks damage and tires the crew. "
        "Shortening sail is safe but slow. The odds are shown on every button.",
        "Damage slows the ship and wets the cargo. A tired crew makes more mistakes.",
        "You can put into Anjer and St Helena to repair, at a cost in days and money.",
        "First home sells the tea at the premium. Fine first-flush pays most but suffers if you are late; "
        "congou pays less but is steadier.",
    };
    for (const char* p : para) y = textWrapped(p, MARGIN, y, W - 2 * MARGIN, FONT_SMALL + 1, INK, 0, 6) + 18;
    Rect b(MARGIN, H - 110, W - 2 * MARGIN, 72);
    button(b, "Back", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(b, A_BACK_TITLE);
}

void TeaRaceApp::drawCargo() {
    header("Choose your cargo", true);
    textWrapped("The hongs are offering two grades this season. You can only carry one.",
                MARGIN, HEADER_H + 24, W - 2 * MARGIN, FONT_SMALL + 1, INK, 0, 6);
    Rect fine(MARGIN, HEADER_H + 110, W - 2 * MARGIN, 250);
    Rect congou(MARGIN, HEADER_H + 390, W - 2 * MARGIN, 250);
    auto card = [](const Rect& r, const char* title, const char* body, const char* odds) {
        strokeRoundRect(r.x, r.y, r.w, r.h, 18, 2, BLACK);
        text(title, r.x + 24, r.y + 24, FONT_TITLE - 4, BLACK, true);
        textWrapped(body, r.x + 24, r.y + 76, r.w - 48, FONT_SMALL, INK, 4, 6);
        text(odds, r.x + 24, r.y + r.h - 46, FONT_CAPTION, DARK, true);
    };
    card(fine, "Fine first-flush", "400 chests of the season's best leaf. London pays a fortune for the first cargo home, "
                                   "but the price falls away fast if you are beaten.",
         "Worth \xC2\xA3""2,500 to \xC2\xA3""4,100");
    card(congou, "Congou", "600 chests of good, sturdy tea. A smaller premium for winning, "
                           "but a fair price whoever gets home first.",
         "Worth \xC2\xA3""2,500 to \xC2\xA3""3,100");
    addButton(fine, A_CARGO_FINE);
    addButton(congou, A_CARGO_CONGOU);
}

void TeaRaceApp::drawLeg() {
    const LegInfo& leg = kLegs[m_leg];
    header(leg.name, true);
    int y = HEADER_H + 10;
    text("Leg " + std::to_string(m_leg + 1) + " of 9", MARGIN, y, FONT_SMALL, DARK, true);
    textRight("Day " + std::to_string((int)m_day) + (m_money ? "   " + money(m_money) : ""), W - MARGIN, y, FONT_SMALL, DARK, true);
    // Voyage progress with a little ship.
    int barY = y + 66;
    float frac = (float)m_leg / LEG_COUNT;
    progressBar(MARGIN, barY, W - 2 * MARGIN, 6, frac);
    int sx = MARGIN + (int)((W - 2 * MARGIN - 34) * frac);
    icon(ICON_SHIP, sx, barY - 32, 30);
    textWrapped(leg.intro, MARGIN, barY + 16, W - 2 * MARGIN, FONT_CAPTION + 1, DARK, 2, 4);

    drawStandings(HEADER_H + 136);
    drawShipStatus(HEADER_H + 300);

    // The glass
    int gy = HEADER_H + 432;
    Rect g(MARGIN, gy, W - 2 * MARGIN, 110);
    fillRoundRect(g.x, g.y, g.w, g.h, 14, PALE);
    text("THE GLASS", g.x + 18, g.y + 12, FONT_CAPTION, DARK, true);
    textWrapped(kForecast[m_forecast], g.x + 18, g.y + 40, g.w - 36, FONT_SMALL + 1, BLACK, 2, 4);

    // Sail plans
    int by = gy + 118;
    for (int p = 0; p < 3; p++) {
        float days = leg.baseDays / legSpeed(m_forecast, p, m_hull, m_crew);
        int risk = (int)(legRisk(m_forecast, p, m_crew) * 100 + 0.5f);
        int crew = m_forecast == 0 ? kCalmCrew[p] : kSailCrew[p];
        std::string detail = "~" + fmt1(days) + " days  \xC2\xB7  " + std::to_string(risk) + "% damage risk  \xC2\xB7  crew " +
                             (crew > 0 ? "+" : "") + std::to_string(crew) + "%";
        Rect r(MARGIN, by + p * 88, W - 2 * MARGIN, 78);
        optionButton(r, kSailName[p], detail, p == 2);
        addButton(r, A_SAIL_0 + p);
    }
}

void TeaRaceApp::drawOutcome() {
    std::string where = m_leg < LEG_COUNT ? kLegs[std::min(m_leg, LEG_COUNT - 1)].name : "Home waters";
    header(m_nextAfterOutcome == 1 ? where : "At sea", true);
    int y = HEADER_H + 30;
    text(m_outTitle, MARGIN, y, FONT_TITLE + 6, BLACK, true);
    y += 70;
    for (const std::string& l : m_outLines) y = textWrapped(l, MARGIN, y, W - 2 * MARGIN, FONT_SMALL + 2, INK, 3, 6) + 12;
    int standY = std::max(y + 10, H - 470);
    drawStandings(standY);
    drawShipStatus(standY + 166);
    Rect b(MARGIN, H - 100, W - 2 * MARGIN, 72);
    button(b, "Continue", ButtonStyle::PRIMARY, FONT_BODY);
    addButton(b, A_NEXT);
}

void TeaRaceApp::drawEvent() {
    header("At sea", true);
    struct Opt { const char* title; const char* detail; };
    const char* title = "";
    const char* body = "";
    Opt opts[3] = {};
    int n = 2;
    std::string rivalName;
    switch (m_eventId) {
        case EV_PIRATES:
            title = "Pirates!";
            body = "A lorcha with no colours is closing fast, her decks crowded with men.";
            opts[0] = {"Run before the wind", "Usually outruns them. If not: damage"};
            opts[1] = {"Run out the guns", "They will sheer off. Crew -10%"};
            opts[2] = {"Pay them off", "\xC2\xA3""150 of the owners' silver"};
            n = 3;
            break;
        case EV_BECALMED:
            title = "Becalmed";
            body = "Not a breath of wind. The sea is like oil and the pitch bubbles in the deck seams.";
            opts[0] = {"Tow with the boats", "Gain 1.5 days. Crew -12%"};
            opts[1] = {"Whistle for a wind", "Lose 1.5 days. Crew +5%"};
            break;
        case EV_FEVER:
            title = "Fever aboard";
            body = "Three of the hands are down with a fever and the cook looks pale.";
            opts[0] = {"Press on", "Crew -15%"};
            opts[1] = {"Anchor in a quiet cove", "Lose 3 days. Crew +5%"};
            break;
        case EV_LEAK:
            title = "She's leaking";
            body = "The carpenter reports two feet of water in the well and rising. The strained seams are working.";
            opts[0] = {"Man the pumps", "Crew -12%"};
            opts[1] = {"Ease her and caulk", "Lose 2 days. Hull +5%"};
            opts[2] = {"Let her leak", "Wet chests +8%"};
            n = 3;
            break;
        case EV_RIVAL: {
            const Rival* nearest = &m_rivals[0];
            for (const Rival& r : m_rivals) if (std::fabs(r.day - m_day) < std::fabs(nearest->day - m_day)) nearest = &r;
            rivalName = nearest->name;
            title = "Sail ho!";
            body = "Topsails on the horizon. Through the glass you make out a rival clipper.";
            opts[0] = {"Race her", "Gain 1.2 days. 30% chance of damage"};
            opts[1] = {"Let her go", "No risk"};
            break;
        }
        case EV_ALBATROSS:
            title = "An albatross";
            body = "A great wandering albatross has followed in the wake since the Cape.";
            opts[0] = {"Leave it be", "The hands like the omen. Crew +3%"};
            opts[1] = {"Shoot it for the pot", "Crew +8%. The old hands won't like it"};
            break;
        case EV_GRUMBLING:
            title = "Grumbling in the fo'c'sle";
            body = "The hands are worn out and muttering. The mate thinks there could be trouble.";
            opts[0] = {"Splice the mainbrace", "Crew +15%. \xC2\xA3""30"};
            opts[1] = {"Stand firm", "Even odds: crew -10% or +5%"};
            break;
        case EV_WHALER:
            title = "A whaler";
            body = "A New Bedford whaler, three years out, signals that she would like to gam.";
            opts[0] = {"Heave to and gam", "\xC2\xA3""20 for water and news. Crew +10%"};
            opts[1] = {"Sail on", "No delay"};
            break;
        case EV_PILOT:
            title = "Off the Lizard";
            body = "Home waters at last. A Deal lugger is beating out, offering a Channel pilot.";
            opts[0] = {"Take a pilot", "\xC2\xA3""40. Half the risk this leg"};
            opts[1] = {"Trust your own reckoning", "Save the fee"};
            break;
        default:
            break;
    }
    int y = HEADER_H + 30;
    text(title, MARGIN, y, FONT_TITLE + 6, BLACK, true);
    y += 70;
    y = textWrapped(body, MARGIN, y, W - 2 * MARGIN, FONT_BODY - 2, INK, 0, 6);
    if (!rivalName.empty()) y = textWrapped("It is " + rivalName + ".", MARGIN, y + 8, W - 2 * MARGIN, FONT_BODY - 2, BLACK, 0, 6);
    drawShipStatus(std::max(y + 30, HEADER_H + 330));
    int by = H - 40 - n * 100;
    for (int i = 0; i < n; i++) {
        Rect r(MARGIN, by + i * 100, W - 2 * MARGIN, 88);
        optionButton(r, opts[i].title, opts[i].detail, i == 0);
        addButton(r, A_OPTION_0 + i);
    }
}

void TeaRaceApp::drawPort() {
    bool anjer = m_leg == PORT_LEG_ANJER;
    header(anjer ? "Anjer Roads" : "St Helena", true);
    int y = HEADER_H + 30;
    text(anjer ? "Anjer, Java" : "Jamestown, St Helena", MARGIN, y, FONT_TITLE + 2, BLACK, true);
    y += 64;
    y = textWrapped(anjer ? "Bumboats crowd round with fruit, fowls and fresh water. The chandler has spars and canvas, at a price."
                          : "A lonely rock in the South Atlantic, with a dockyard, fresh water and fresh beef.",
                    MARGIN, y, W - 2 * MARGIN, FONT_BODY - 2, INK, 0, 6);
    drawStandings(y + 30);
    drawShipStatus(y + 200);
    Rect stop(MARGIN, H - 240, W - 2 * MARGIN, 92);
    Rect go(MARGIN, H - 136, W - 2 * MARGIN, 92);
    optionButton(stop, "Put in for repairs", "2.5 days, \xC2\xA3""40. Hull +35%, crew +30%");
    optionButton(go, "Sail on", "Keep racing", true);
    addButton(stop, A_PORT_STOP);
    addButton(go, A_PORT_SAIL);
}

void TeaRaceApp::drawResult() {
    header("London", true);
    int rank = playerRank();
    std::vector<std::string> breakdown;
    int value = voyageValue(&breakdown);
    int y = HEADER_H + 24;
    static const char* kHeadline[4] = {"First home!", "Second home", "Third home", "Last home"};
    text(kHeadline[rank - 1], MARGIN, y, FONT_TITLE + 8, BLACK, true);
    y += 66;
    text("Docked at Gravesend on day " + std::to_string((int)(m_day + 0.5f)), MARGIN, y, FONT_SMALL + 1, DARK);
    drawStandings(y + 50);
    y += 50 + 30 + 4 * 32 + 20;
    sectionLabel(y, "The owners' account");
    y += 34;
    for (const std::string& l : breakdown) {
        y = textWrapped(l, MARGIN, y, W - 2 * MARGIN, FONT_SMALL, INK, 2, 4) + 6;
    }
    hRule(y + 4, BLACK, MARGIN, W - MARGIN, 2);
    text("Total", MARGIN, y + 16, FONT_BODY + 4, BLACK, true);
    textRight(money(value), W - MARGIN, y + 16, FONT_BODY + 4, BLACK, true);
    y += 70;
    const char* rating = value >= 3800 ? "A legend of the China trade."
                       : value >= 3200 ? "The owners will fight over you."
                       : value >= 2700 ? "A sound, seamanlike passage."
                       : value >= 2200 ? "The owners are disappointed."
                                       : "The insurers would like a word.";
    textWrapped(rating, MARGIN, y, W - 2 * MARGIN, FONT_BODY - 2, BLACK, 2, 4);
    if (value >= m_best) textRight("New best!", W - MARGIN, y + 40, FONT_SMALL, BLACK, true);
    else textRight("Best " + money(m_best), W - MARGIN, y + 40, FONT_SMALL, DARK);

    Rect again(MARGIN, H - 100, (W - 2 * MARGIN - 16) / 2, 72);
    Rect done(again.x + again.w + 16, H - 100, again.w, 72);
    button(again, "Sail again", ButtonStyle::PRIMARY, FONT_BODY);
    button(done, "Done", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(again, A_NEW);
    addButton(done, A_DONE);
}
