#include "CodebreakerApp.h"
#include "DisplayHAL.h"
#include "ui/UIFramework.h"
#include "reader/AppStorage.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

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

// ---- Difficulty -------------------------------------------------------------

struct Level {
    const char* name;
    int slots;
    int symbols;
    bool repeats;
    const char* detail;
    int rating[4]; // day thresholds for the four better ratings
};

const Level kLevels[CodebreakerApp::LEVEL_COUNT] = {
    {"Recruit", 4, 6, false, "4 symbols from A\xE2\x80\x93" "F, never repeated", {2, 4, 5, 7}},
    {"Analyst", 4, 8, true, "4 symbols from A\xE2\x80\x93" "H, repeats possible", {3, 5, 6, 8}},
    {"Room 40", 5, 8, true, "5 symbols from A\xE2\x80\x93" "H, repeats possible", {3, 5, 7, 8}},
};

const char* kRatings[5] = {
    "Uncanny. Admiral Hall would like to know how you did it.",
    "Brilliant work. The Director of Naval Intelligence sends his compliments.",
    "Sound work. The Fleet will have it before breakfast.",
    "Cutting it fine, but the warning went out in time.",
    "By the skin of your teeth. The signal reached the Fleet with hours to spare.",
};

// ---- Intercepts (fictional) ------------------------------------------------

struct Intercept {
    const char* from;
    const char* to;
    const char* text;
    const char* aftermath;
};

const Intercept kIntercepts[] = {
    {"Fleet Command, Wilhelmshaven", "II U-Boat Flotilla",
     "U-BOAT FLOTILLA TO SAIL FROM WILHELMSHAVEN 0400 TUESDAY. DESTROYER ESCORT AT HORNS REEF.",
     "The flotilla slipped out unseen. Four steamers were sunk off the Scillies within the week."},
    {"Naval Airship Division, Nordholz", "Airships L 51 and L 53",
     "AIRSHIPS TO RAID THE HUMBER TONIGHT IF THE WIND FALLS BELOW FORCE THREE. BOMB THE DOCKS FROM 12000 FEET.",
     "No fighters were waiting. Bombs fell on the Hull docks before dawn."},
    {"Minesweeping Division, Cuxhaven", "Minelayer Nautilus",
     "MINELAYER TO LAY FIELD OFF THE TYNE AFTER DARK THURSDAY. RETURN BY THE AMRUN BANK.",
     "The field was laid undetected. A collier and a sloop struck mines off Tynemouth."},
    {"Commander-in-Chief, High Seas Fleet", "All squadrons",
     "HIGH SEAS FLEET TO RAISE STEAM FOR FULL SPEED. RENDEZVOUS WEST OF HELIGOLAND AT DAWN.",
     "The Grand Fleet lay at anchor in Scapa Flow while the enemy swept the Dogger Bank."},
    {"U 62, at sea", "Commander of U-Boats, Bruges",
     "CONVOY OF TWENTY STEAMERS SIGHTED BOUND FOR BREST. SHADOWING. ATTACK AT FIRST LIGHT.",
     "The convoy was never diverted. Three ships went down in the Bay of Biscay."},
    {"Scouting Forces, Jade", "II Scouting Group",
     "CRUISERS TO BOMBARD LOWESTOFT AT 0500. LIGHT FORCES TO SCREEN AGAINST HARWICH.",
     "Shells fell on Lowestoft at dawn. The Harwich Force arrived an hour too late."},
    {"Admiralstab, Berlin", "All stations",
     "NEW CALL SIGNS IN FORCE FROM MIDNIGHT. DESTROY ALL COPIES OF THE OLD SIGNAL BOOK.",
     "The change caught Room 40 unawares. The enemy's traffic went dark for a fortnight."},
    {"Naval Attache, Madrid", "Admiralstab, Berlin",
     "SCHOONER MARIA TO LAND RIFLES ON THE CLARE COAST SATURDAY NIGHT. SIGNAL TWO GREEN LIGHTS.",
     "The rifles came ashore unseen and vanished inland."},
    {"Flanders Flotilla, Zeebrugge", "Destroyer half-flotillas",
     "DESTROYERS TO RAID THE DOVER BARRAGE ON THE NEXT MOONLESS NIGHT. THE DRIFTERS ARE THE TARGET.",
     "The drifters on the barrage were caught by surprise. Few escaped."},
    {"Airship Base, Tondern", "All boats at sea",
     "ALL BOATS TO REPORT WIND AND GLASS BY 2200. RAID ON LONDON DEPENDS ON YOUR REPORTS.",
     "The raiders found London under clear skies, and no warning had been given."},
    {"Commander of U-Boats, Bruges", "Flanders boats",
     "U-BOATS TO CONCENTRATE ON TROOPSHIPS IN THE CHANNEL. ALL RESTRICTIONS LIFTED FROM MIDNIGHT.",
     "A troopship was torpedoed off the Owers with heavy loss of life."},
    {"Battlecruiser Squadron, Jade", "Light cruiser screen",
     "BATTLECRUISERS TO SORTIE THROUGH THE JADE ON THE EVENING TIDE. EXPECT BRITISH SCOUTS OFF TERSCHELLING.",
     "The battlecruisers were at sea a full day before anyone in London knew."},
    {"Naval Station, Kiel", "Auxiliary cruiser",
     "RAIDER TO BREAK OUT NORTH OF THE FAROES DISGUISED AS A NORWEGIAN TIMBER SHIP. COAL AT RENDEZVOUS B.",
     "The raider slipped through the blockade and reached the open Atlantic."},
};
constexpr int INTERCEPT_COUNT = sizeof(kIntercepts) / sizeof(kIntercepts[0]);

// ---- Actions ---------------------------------------------------------------

enum Action {
    A_NONE = 0,
    A_LEVEL_0, A_LEVEL_1, A_LEVEL_2,
    A_CONTINUE, A_HELP, A_HELP_BACK,
    A_SLOT_0 = 20,
    A_KEY_0 = 40,
    A_CLEAR = 60, A_SEND, A_AGAIN, A_TITLE,
};

// ---- Board geometry (portrait 540 x 960) -----------------------------------

constexpr int HIST_Y = HEADER_H + 10;
constexpr int HIST_ROW = 46;
constexpr int HIST_TILE = 34;
constexpr int HIST_TILE_GAP = 6;
constexpr int HIST_TILES_X = 112;
constexpr int LEGEND_Y = HIST_Y + CodebreakerApp::MAX_DAYS * HIST_ROW + 8;
constexpr int SLOT_Y = 626;
constexpr int SLOT_H = 80;
constexpr int INPUT_TOP = SLOT_Y - 10;
constexpr int KEY_Y = 728;
constexpr int KEY_H = 64;
constexpr int KEY_GAP = 10;
constexpr int BTN_Y = 880;
constexpr int BTN_H = 60;

enum TileStyle { TILE_NORMAL, TILE_FAINT, TILE_DARK, TILE_USED };

std::string symbolText(uint8_t s) { return std::string(1, (char)('A' + s)); }

void tile(const Rect& r, uint8_t sym, TileStyle style, float size, int border = 2) {
    int radius = r.h / 5;
    uint8_t fg = BLACK;
    switch (style) {
        case TILE_NORMAL:
            fillRoundRect(r.x, r.y, r.w, r.h, radius, WHITE);
            strokeRoundRect(r.x, r.y, r.w, r.h, radius, border, BLACK);
            break;
        case TILE_FAINT:
            fillRoundRect(r.x, r.y, r.w, r.h, radius, WHITE);
            strokeRoundRect(r.x, r.y, r.w, r.h, radius, 1, LIGHT);
            fg = MID;
            break;
        case TILE_USED:
            fillRoundRect(r.x, r.y, r.w, r.h, radius, PALE);
            strokeRoundRect(r.x, r.y, r.w, r.h, radius, 2, LIGHT);
            fg = MID;
            break;
        case TILE_DARK:
            fillRoundRect(r.x, r.y, r.w, r.h, radius, BLACK);
            fg = WHITE;
            break;
    }
    if (sym == CodebreakerApp::EMPTY) return;
    std::string s = symbolText(sym);
    textCentered(s, r.x + r.w / 2, r.y + (r.h - lineHeight(size)) / 2, size, fg, true);
}

void exactDot(int cx, int cy, int r) { fillCircle(cx, cy, r, BLACK); }

void nearDot(int cx, int cy, int r) {
    fillCircle(cx, cy, r, BLACK);
    fillCircle(cx, cy, r - (r >= 8 ? 3 : 2), WHITE);
}

void feedback(int x, int cy, int exact, int near, int n, int pitch, int r) {
    for (int i = 0; i < n; i++) {
        int cx = x + r + i * pitch;
        if (i < exact) exactDot(cx, cy, r);
        else if (i < exact + near) nearDot(cx, cy, r);
        else fillCircle(cx, cy, 3, LIGHT);
    }
}

// Two-line option button: bold title + caption.
void optionButton(const Rect& r, const std::string& title, const std::string& detail, bool primary = false) {
    if (primary) {
        fillRoundRect(r.x, r.y, r.w, r.h, 14, BLACK);
    } else {
        fillRoundRect(r.x, r.y, r.w, r.h, 14, WHITE);
        strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, BLACK);
    }
    uint8_t fg = primary ? WHITE : BLACK;
    uint8_t sub = primary ? LIGHT : DARK;
    int lh1 = lineHeight(FONT_BODY);
    int lh2 = detail.empty() ? 0 : lineHeight(GAME_CAPTION) + 6;
    int y = r.y + (r.h - lh1 - lh2) / 2;
    text(ellipsize(title, r.w - 40, FONT_BODY, true), r.x + 22, y, FONT_BODY, fg, true);
    if (!detail.empty()) text(ellipsize(detail, r.w - 40, GAME_CAPTION), r.x + 22, y + lh1 + 6, GAME_CAPTION, sub);
}

// Enciphers the intercept for display: letters and digits only, in the
// five-letter groups of a wireless signal. Purely decorative.
std::string cipherGroups(const char* plain, const uint8_t* key, int n, int salt) {
    std::string out;
    int count = 0;
    for (const char* p = plain; *p; p++) {
        char c = *p;
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= '0' && c <= '9') v = c - '0' + 7;
        else continue;
        v = (v + key[count % n] * 5 + count * 7 + salt * 3 + 11) % 26;
        if (count > 0 && count % 5 == 0) out += ' ';
        out += (char)('A' + v);
        count++;
    }
    return out;
}

std::string plural(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void CodebreakerApp::onCreate() {
#ifndef NATIVE_TESTING
    m_rng = esp_random() | 1;
#else
    m_rng = (uint32_t)time(nullptr) | 1;
#endif
    m_screen = TITLE;
    m_level = -1;
    m_finished = false;
    loadStats();
}

void CodebreakerApp::draw() {
    m_lastDrawn = -1;
    render();
}

bool CodebreakerApp::onBack() {
    switch (m_screen) {
        case TITLE:
            return false;
        case HELP:
            m_screen = m_helpReturn;
            break;
        default:
            // From the board the game stays in progress; the title offers
            // to continue it.
            m_screen = TITLE;
            break;
    }
    render();
    return true;
}

// ---------------------------------------------------------------------------
// Randomness
// ---------------------------------------------------------------------------

uint32_t CodebreakerApp::nextRand() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return m_rng;
}

int CodebreakerApp::randInt(int lo, int hi) { return lo + (int)(nextRand() % (uint32_t)(hi - lo + 1)); }

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

void CodebreakerApp::abandonGame() {
    // Walking away from a key you have already started on counts as a loss.
    if (m_level >= 0 && !m_finished && m_guessCount > 0) {
        m_played++;
        m_streak = 0;
    }
    m_level = -1;
    m_finished = false;
}

void CodebreakerApp::newGame(int level) {
    abandonGame();
    const Level& L = kLevels[level];
    m_level = level;
    m_finished = false;
    m_won = false;
    m_newBest = false;
    m_guessCount = 0;
    if (L.repeats) {
        for (int i = 0; i < L.slots; i++) m_secret[i] = (uint8_t)randInt(0, L.symbols - 1);
    } else {
        uint8_t pool[MAX_SYMBOLS];
        for (int i = 0; i < L.symbols; i++) pool[i] = (uint8_t)i;
        for (int i = L.symbols - 1; i > 0; i--) {
            int j = randInt(0, i);
            uint8_t t = pool[i];
            pool[i] = pool[j];
            pool[j] = t;
        }
        for (int i = 0; i < L.slots; i++) m_secret[i] = pool[i];
    }
    do {
        m_message = randInt(0, INTERCEPT_COUNT - 1);
    } while (m_message == m_lastMessage);
    m_lastMessage = m_message;
    clearGuess();
    saveStats();
    m_screen = BOARD;
    render();
}

bool CodebreakerApp::guessComplete() const {
    if (m_level < 0) return false;
    for (int i = 0; i < kLevels[m_level].slots; i++) if (m_current[i] == EMPTY) return false;
    return true;
}

bool CodebreakerApp::guessEmpty() const {
    if (m_level < 0) return true;
    for (int i = 0; i < kLevels[m_level].slots; i++) if (m_current[i] != EMPTY) return false;
    return true;
}

void CodebreakerApp::clearGuess() {
    for (int i = 0; i < MAX_SLOTS; i++) m_current[i] = EMPTY;
    m_sel = 0;
}

void CodebreakerApp::tapSlot(int i) {
    if (i >= kLevels[m_level].slots) return;
    if (i == m_sel && m_current[i] != EMPTY) m_current[i] = EMPTY; // tap again to clear
    m_sel = i;
    refreshInput();
}

void CodebreakerApp::tapSymbol(int s) {
    const Level& L = kLevels[m_level];
    if (s >= L.symbols) return;
    if (!L.repeats) {
        // The key never repeats a symbol, so neither may a trial key:
        // picking a symbol already placed moves it to the selected slot.
        for (int i = 0; i < L.slots; i++) if (i != m_sel && m_current[i] == s) m_current[i] = EMPTY;
    }
    m_current[m_sel] = (uint8_t)s;
    for (int k = 1; k < L.slots; k++) {
        int j = (m_sel + k) % L.slots;
        if (m_current[j] == EMPTY) {
            m_sel = j;
            break;
        }
    }
    refreshInput();
}

void CodebreakerApp::sendGuess() {
    const Level& L = kLevels[m_level];
    if (!guessComplete() || m_guessCount >= MAX_DAYS) return;
    int exact, near;
    scoreGuess(m_secret, m_current, L.slots, exact, near);
    memcpy(m_guesses[m_guessCount], m_current, MAX_SLOTS);
    m_exact[m_guessCount] = (uint8_t)exact;
    m_near[m_guessCount] = (uint8_t)near;
    m_guessCount++;
    clearGuess();
    if (exact == L.slots) {
        finishGame(true);
    } else if (m_guessCount >= MAX_DAYS) {
        finishGame(false);
    } else {
        saveStats();
        UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() {
            drawHistory();
            drawInput(false);
        });
    }
}

void CodebreakerApp::finishGame(bool won) {
    m_finished = true;
    m_won = won;
    m_played++;
    m_newBest = false;
    if (won) {
        m_wins++;
        m_streak++;
        if (m_streak > m_bestStreak) m_bestStreak = m_streak;
        if (m_best[m_level] == 0 || m_guessCount < m_best[m_level]) {
            m_newBest = m_best[m_level] != 0;
            m_best[m_level] = m_guessCount;
        }
    } else {
        m_streak = 0;
    }
    saveStats();
    m_screen = RESULT;
    render();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// /data/codebreaker.txt:
//   CB1 played wins streak bestStreak best0 best1 best2 lastMessage
//   G level message guessCount secret[slots] guesses[guessCount][slots]   (game in progress only)

void CodebreakerApp::loadStats() {
    std::string path = AppStorage::toVfsPath(AppStorage::toRuntimePath("/data/codebreaker.txt"));
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    int v[8];
    if (fscanf(f, " CB1 %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8) {
        m_played = v[0] > 0 ? v[0] : 0;
        m_wins = v[1] > 0 ? v[1] : 0;
        m_streak = v[2] > 0 ? v[2] : 0;
        m_bestStreak = v[3] > 0 ? v[3] : 0;
        for (int i = 0; i < LEVEL_COUNT; i++) m_best[i] = (v[4 + i] > 0 && v[4 + i] <= MAX_DAYS) ? v[4 + i] : 0;
        m_lastMessage = v[7];
    }
    int level, message, count;
    if (fscanf(f, " G %d %d %d", &level, &message, &count) == 3 && level >= 0 && level < LEVEL_COUNT &&
        message >= 0 && message < INTERCEPT_COUNT && count >= 0 && count < MAX_DAYS) {
        const Level& L = kLevels[level];
        bool ok = true;
        for (int i = 0; i < L.slots && ok; i++) {
            int s;
            ok = fscanf(f, "%d", &s) == 1 && s >= 0 && s < L.symbols;
            if (ok) m_secret[i] = (uint8_t)s;
        }
        for (int g = 0; g < count && ok; g++) {
            for (int i = 0; i < L.slots && ok; i++) {
                int s;
                ok = fscanf(f, "%d", &s) == 1 && s >= 0 && s < L.symbols;
                if (ok) m_guesses[g][i] = (uint8_t)s;
            }
            if (ok) {
                int e, n;
                scoreGuess(m_secret, m_guesses[g], L.slots, e, n);
                m_exact[g] = (uint8_t)e;
                m_near[g] = (uint8_t)n;
            }
        }
        if (ok) {
            m_level = level;
            m_message = message;
            m_guessCount = count;
            m_finished = false;
            clearGuess();
        }
    }
    fclose(f);
}

void CodebreakerApp::saveStats() {
    std::string path = AppStorage::toVfsPath(AppStorage::toRuntimePath("/data/codebreaker.txt"));
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "CB1 %d %d %d %d %d %d %d %d\n", m_played, m_wins, m_streak, m_bestStreak,
            m_best[0], m_best[1], m_best[2], m_lastMessage);
    if (m_level >= 0 && !m_finished) {
        const Level& L = kLevels[m_level];
        fprintf(f, "G %d %d %d", m_level, m_message, m_guessCount);
        for (int i = 0; i < L.slots; i++) fprintf(f, " %d", m_secret[i]);
        for (int g = 0; g < m_guessCount; g++)
            for (int i = 0; i < L.slots; i++) fprintf(f, " %d", m_guesses[g][i]);
        fprintf(f, "\n");
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

void CodebreakerApp::addButton(const Rect& r, int action) { m_buttons.push_back({r, action}); }

void CodebreakerApp::handleTouch(int x, int y) {
    if (m_screen == HELP) {
        if (UI::hitHeaderAction(x, y, "Done")) {
            m_screen = m_helpReturn;
            render();
            return;
        }
        if (x >= UI::MARGIN && x < UI::MARGIN + 118 && y >= UI::H - UI::FOOTER_H + 12 &&
            y < UI::H - 12 && m_helpPage > 0) {
            --m_helpPage;
            UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() {
                drawHelp();
            });
            return;
        }
        if (x >= UI::W - UI::MARGIN - 118 && x < UI::W - UI::MARGIN &&
            y >= UI::H - UI::FOOTER_H + 12 && y < UI::H - 12 && m_helpPage < 1) {
            ++m_helpPage;
            UIFramework::performFastPartialUpdate(framebuffer, 0, HEADER_H, W, H - HEADER_H, [this]() {
                drawHelp();
            });
            return;
        }
    }
    int action = A_NONE;
    for (const Button& b : m_buttons) {
        if (b.rect.contains(x, y)) {
            action = b.action;
            break;
        }
    }
    if (action == A_NONE) return;
    if (action >= A_LEVEL_0 && action <= A_LEVEL_2) {
        newGame(action - A_LEVEL_0);
        return;
    }
    if (m_screen == BOARD && action >= A_SLOT_0 && action < A_SLOT_0 + MAX_SLOTS) {
        tapSlot(action - A_SLOT_0);
        return;
    }
    if (m_screen == BOARD && action >= A_KEY_0 && action < A_KEY_0 + MAX_SYMBOLS) {
        tapSymbol(action - A_KEY_0);
        return;
    }
    switch (action) {
        case A_CONTINUE:
            if (m_level >= 0 && !m_finished) {
                m_screen = BOARD;
                render();
            }
            break;
        case A_HELP:
            m_helpReturn = m_screen;
            m_helpPage = 0;
            m_screen = HELP;
            render();
            break;
        case A_HELP_BACK:
            m_screen = m_helpReturn;
            render();
            break;
        case A_CLEAR:
            if (!guessEmpty()) {
                clearGuess();
                refreshInput();
            }
            break;
        case A_SEND:
            if (guessComplete()) sendGuess();
            break;
        case A_AGAIN:
            newGame(m_level >= 0 ? m_level : 0);
            break;
        case A_TITLE:
            m_screen = TITLE;
            render();
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void CodebreakerApp::render() {
    m_buttons.clear();
    auto content = [this]() {
        switch (m_screen) {
            case TITLE: drawTitle(); break;
            case HELP: drawHelp(); break;
            case BOARD: drawBoard(); break;
            case RESULT: drawResult(); break;
        }
    };
    UIFramework::performFullScreenDraw(framebuffer, content);
    m_lastDrawn = m_screen;
}

void CodebreakerApp::drawTitle() {
    header("Codebreaker 1917", true);
    bool inProgress = m_level >= 0 && !m_finished;
    // Without a Continue button there is room to spare; let the top breathe.
    int top = HEADER_H + (inProgress ? 20 : 56);

    // Emblem: "ROOM 40" in cipher tiles.
    const char* emblem = "ROOM40";
    int ts = 54, gap = 8, extra = 22;
    int ew = 6 * ts + 5 * gap + extra;
    int ex = (W - ew) / 2;
    for (int i = 0; i < 6; i++) {
        Rect r(ex + i * (ts + gap) + (i >= 4 ? extra : 0), top, ts, ts);
        bool dark = i >= 4;
        int radius = ts / 5;
        if (dark) fillRoundRect(r.x, r.y, r.w, r.h, radius, BLACK);
        else strokeRoundRect(r.x, r.y, r.w, r.h, radius, 2, BLACK);
        std::string ch(1, emblem[i]);
        textCentered(ch, r.x + r.w / 2, r.y + (r.h - lineHeight(32)) / 2, 32, dark ? WHITE : BLACK, true);
    }

    int y = top + ts + 22;
    y = textWrapped("The Admiralty, London, 1917. In Room 40 a handful of codebreakers read the enemy's "
                    "wireless signals. A new cipher key came into force at midnight. Recover it before "
                    "the attack date, or the Fleet sails blind.",
                    MARGIN + 4, y, W - 2 * MARGIN - 8, GAME_SMALL + 2, INK, 0, 7, true);

    y += 16;
    gameSectionLabel(y, "Service record");
    y += 28;
    if (m_played == 0) {
        text("No keys attempted yet.", MARGIN, y + 8, GAME_SMALL + 1, DARK);
    } else {
        const int values[4] = {m_played, m_wins, m_streak, m_bestStreak};
        const char* labels[4] = {"Played", "Won", "Streak", "Best streak"};
        int cw = (W - 2 * MARGIN) / 4;
        for (int i = 0; i < 4; i++) {
            int x = MARGIN + i * cw;
            text(std::to_string(values[i]), x, y, FONT_TITLE, BLACK, true);
            text(ellipsize(labels[i], cw - 8, GAME_CAPTION), x, y + lineHeight(FONT_TITLE) + 2, GAME_CAPTION, DARK);
        }
    }

    if (inProgress) {
        Rect c(MARGIN, 456, W - 2 * MARGIN, 80);
        std::string detail = std::string(kLevels[m_level].name) + "  \xC2\xB7  day " +
                             std::to_string(m_guessCount + 1) + " of " + std::to_string(MAX_DAYS);
        optionButton(c, "Continue", detail, true);
        addButton(c, A_CONTINUE);
    }

    int by = 588;
    gameSectionLabel(by - 30, inProgress ? "New key (abandons the current one)" : "Choose your post");
    for (int i = 0; i < LEVEL_COUNT; i++) {
        Rect r(MARGIN, by + i * 96, W - 2 * MARGIN, 84);
        std::string detail = kLevels[i].detail;
        if (m_best[i] > 0) detail += "  \xC2\xB7  best " + plural(m_best[i], "day", "days");
        optionButton(r, kLevels[i].name, detail, !inProgress && i == 0);
        addButton(r, A_LEVEL_0 + i);
    }
    Rect h(MARGIN, H - 24 - 60, W - 2 * MARGIN, 60);
    button(h, "How to play", ButtonStyle::SECONDARY, GAME_SMALL + 2);
    addButton(h, A_HELP);
}

void CodebreakerApp::drawHelp() {
    header("How to play", true, "Done");
    textCentered("Page " + std::to_string(m_helpPage + 1) + " of 2",
                 W / 2, HEADER_H + 18, FONT_BODY - 2, DARK);
    if (m_helpPage == 0) {
        int y = HEADER_H + 82;
        const int tw = W - 2 * MARGIN;
        y = textWrapped("Recover the secret cipher key before the attack date. Each guess costs one day; you have ten.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 12;
        y = textWrapped("Example: with key C A F E and guess A B C E, E is in the right place; A and C are in the key but elsewhere.",
                        MARGIN, y, tw, FONT_BODY, INK, 0, 10) + 12;
        const uint8_t ex[4] = {0, 1, 2, 4};
        for (int i = 0; i < 4; i++) tile(Rect(MARGIN + i * 58, y, 50, 50), ex[i], TILE_NORMAL, 28);
        feedback(MARGIN + 4 * 58 + 24, y + 25, 1, 2, 4, 30, 10);
        y += 62;
        textWrapped("Solid dots mean correct place; hollow dots mean correct symbol, different place. Feedback never identifies which symbols match.",
                    MARGIN, y, tw, FONT_BODY, INK, 0, 10);
    } else {
        const char* textRules = "Tap a slot, then a symbol to fill it. Tap a selected slot again to empty it. Recruit uses four symbols A–F without repeats; Analyst uses four A–H with repeats; Room 40 uses five A–H with repeats. Break the key to decrypt the intercept. Starting a new key part-way through counts as a loss.";
        textWrapped(textRules, MARGIN + 12, HEADER_H + 92, W - 2 * MARGIN - 24,
                    FONT_BODY, INK, 0, 14);
    }
    drawPageFooter(m_helpPage, 2);
}

int CodebreakerApp::slotSize() const {
    int n = kLevels[m_level].slots;
    int s = (W - 2 * MARGIN - (n - 1) * 14) / n;
    return s > 96 ? 96 : s;
}

Rect CodebreakerApp::slotRect(int i) const {
    int n = kLevels[m_level].slots;
    int s = slotSize();
    int total = n * s + (n - 1) * 14;
    return Rect((W - total) / 2 + i * (s + 14), SLOT_Y, s, SLOT_H);
}

Rect CodebreakerApp::keyRect(int k) const {
    int cols = kLevels[m_level].symbols / 2;
    int kw = (W - 2 * MARGIN - (cols - 1) * 12) / cols;
    int row = k / cols, col = k % cols;
    return Rect(MARGIN + col * (kw + 12), KEY_Y + row * (KEY_H + KEY_GAP), kw, KEY_H);
}

void CodebreakerApp::drawBoard() {
    header(kLevels[m_level].name, true, "Rules");
    Rect rules = headerActionRect("Rules");
    addButton(Rect(rules.x - 10, STATUS_H, W - rules.x + 10, HEADER_H - STATUS_H), A_HELP);
    drawHistory();
    drawInput(true);
}

void CodebreakerApp::drawHistory() {
    const Level& L = kLevels[m_level];
    for (int d = 0; d < MAX_DAYS; d++) {
        int y = HIST_Y + d * HIST_ROW;
        int cy = y + HIST_ROW / 2;
        bool past = d < m_guessCount;
        bool today = d == m_guessCount;
        std::string label = "Day " + std::to_string(d + 1);
        uint8_t lc = past ? DARK : (today ? BLACK : MID);
        text(label, MARGIN, cy - lineHeight(GAME_CAPTION) / 2, GAME_CAPTION, lc, today);
        if (today) {
            // Pointer to the row being composed below.
            int tx = MARGIN + 66;
            for (int k = 0; k < 7; k++) fillRect(tx + k, cy - 7 + k, 1, 15 - 2 * k, BLACK);
        }
        for (int i = 0; i < L.slots; i++) {
            Rect r(HIST_TILES_X + i * (HIST_TILE + HIST_TILE_GAP), cy - HIST_TILE / 2, HIST_TILE, HIST_TILE);
            if (past) tile(r, m_guesses[d][i], TILE_NORMAL, 24);
            else tile(r, EMPTY, TILE_FAINT, 24);
        }
        int fx = HIST_TILES_X + L.slots * (HIST_TILE + HIST_TILE_GAP) + 18;
        if (past) feedback(fx, cy, m_exact[d], m_near[d], L.slots, 24, 9);
        else feedback(fx, cy, 0, 0, L.slots, 24, 9);
        if (past) {
            std::string res;
            if (m_exact[d] == 0 && m_near[d] == 0) res = "none";
            if (!res.empty()) textRight(res, W - MARGIN, cy - lineHeight(GAME_CAPTION) / 2, GAME_CAPTION, MID);
        }
        if (d < MAX_DAYS - 1) hRule(y + HIST_ROW, PALE, MARGIN, W - MARGIN);
    }

    // Keep the compact legend on one readable line above the input.
    int ly = LEGEND_Y;
    text("Solid = placed    Hollow = elsewhere", MARGIN, ly, GAME_CAPTION, DARK);
}

void CodebreakerApp::drawInput(bool addHits) {
    const Level& L = kLevels[m_level];
    for (int i = 0; i < L.slots; i++) {
        Rect r = slotRect(i);
        bool sel = i == m_sel;
        if (m_current[i] == EMPTY) {
            fillRoundRect(r.x, r.y, r.w, r.h, 16, sel ? WHITE : PALE);
            strokeRoundRect(r.x, r.y, r.w, r.h, 16, sel ? 5 : 2, sel ? BLACK : MID);
        } else {
            tile(r, m_current[i], TILE_NORMAL, 46, sel ? 5 : 2);
        }
        if (sel) fillRoundRect(r.x + r.w / 2 - 18, r.y + r.h + 5, 36, 6, 3, BLACK);
        if (addHits) addButton(Rect(r.x - 7, r.y - 8, r.w + 14, r.h + 18), A_SLOT_0 + i);
    }
    for (int k = 0; k < L.symbols; k++) {
        Rect r = keyRect(k);
        bool used = false;
        if (!L.repeats)
            for (int i = 0; i < L.slots; i++) if (m_current[i] == k && i != m_sel) used = true;
        tile(r, (uint8_t)k, used ? TILE_USED : TILE_NORMAL, 36);
        if (addHits) addButton(Rect(r.x - 6, r.y - 5, r.w + 12, r.h + 10), A_KEY_0 + k);
    }
    int cw = (W - 2 * MARGIN - 16) / 3;
    Rect clear(MARGIN, BTN_Y, cw, BTN_H);
    Rect send(MARGIN + cw + 16, BTN_Y, W - 2 * MARGIN - cw - 16, BTN_H);
    button(clear, "Clear", ButtonStyle::SECONDARY, FONT_BODY, !guessEmpty());
    bool full = guessComplete();
    std::string label = m_guessCount == MAX_DAYS - 1 ? "Send (last day)" : "Send";
    button(send, label, ButtonStyle::PRIMARY, FONT_BODY, full);
    if (addHits) {
        addButton(clear, A_CLEAR);
        addButton(send, A_SEND);
    }
}

void CodebreakerApp::refreshInput() {
    UIFramework::performFastPartialUpdate(framebuffer, 0, INPUT_TOP, W, H - INPUT_TOP, [this]() { drawInput(false); });
}

int CodebreakerApp::drawTelegram(int y, bool readInTime) {
    const Intercept& m = kIntercepts[m_message];
    const Level& L = kLevels[m_level];
    int x = MARGIN + 18, w = W - 2 * MARGIN - 36;
    int top = y;
    int lhc = lineHeight(GAME_CAPTION);
    y += 14;
    text("WIRELESS INTERCEPT", x, y, GAME_CAPTION, BLACK, true);
    char serial[16];
    snprintf(serial, sizeof(serial), "No. %04d", 1000 + (m_message * 617 + m_secret[0] * 73 + m_secret[1] * 11) % 9000);
    textRight(serial, x + w, y, GAME_CAPTION, DARK);
    y += lhc + 6;
    text(ellipsize("From: " + std::string(m.from), w, GAME_CAPTION), x, y, GAME_CAPTION, DARK);
    y += lhc + 2;
    text(ellipsize("To: " + std::string(m.to), w, GAME_CAPTION), x, y, GAME_CAPTION, DARK);
    y += lhc + 8;
    y = textWrapped(cipherGroups(m.text, m_secret, L.slots, m_message), x, y, w, GAME_CAPTION, MID, 0, 4);
    y += 6;
    hRule(y, LIGHT, x, x + w);
    y += 12;
    text(readInTime ? "DECRYPT" : "READ TOO LATE", x, y, GAME_CAPTION, DARK, true);
    y += lhc + 8;
    uint8_t c = readInTime ? BLACK : DARK;
    const float fs = FONT_BODY - 3;
    textWrapped(m.text, x + 1, y, w - 1, fs, c, 0, 6);
    y = textWrapped(m.text, x, y, w - 1, fs, c, 0, 6);
    y += 6;
    strokeRoundRect(MARGIN, top, W - 2 * MARGIN, y - top, 12, 2, BLACK);
    return y;
}

// The guesses made, compactly, in two columns of five days.
void CodebreakerApp::drawWorking(int y, int bottom) {
    const Level& L = kLevels[m_level];
    int rows = m_guessCount > 5 ? 5 : m_guessCount;
    if (rows <= 0) return;
    int rh = (bottom - (y + 34)) / rows;
    if (rh < 38) return;
    gameSectionLabel(y, "Your working");
    y += 34;
    int ts = 30;
    int colW = (W - 2 * MARGIN) / 2;
    for (int d = 0; d < m_guessCount; d++) {
        int x = MARGIN + (d / 5) * colW;
        int cy = y + (d % 5) * rh + rh / 2;
        textRight(std::to_string(d + 1), x + 20, cy - lineHeight(GAME_CAPTION) / 2, GAME_CAPTION, DARK);
        int tx = x + 30;
        for (int i = 0; i < L.slots; i++)
            tile(Rect(tx + i * (ts + 3), cy - ts / 2, ts, ts), m_guesses[d][i], TILE_NORMAL, 24, 1);
        feedback(tx + L.slots * (ts + 3) + 6, cy, m_exact[d], m_near[d], L.slots, 16, 6);
    }
}

void CodebreakerApp::drawResult() {
    const Level& L = kLevels[m_level];
    header(m_won ? "Key recovered" : "Too late", true);
    int y = HEADER_H + 22;
    text(m_won ? "Intercept decrypted" : "The attack went ahead", MARGIN, y, FONT_TITLE, BLACK, true);
    y += lineHeight(FONT_TITLE) + 14;

    int ts = 54, gap = 10;
    for (int i = 0; i < L.slots; i++) tile(Rect(MARGIN + i * (ts + gap), y, ts, ts), m_secret[i], TILE_DARK, 32);
    if (m_won) {
        textRight("Day " + std::to_string(m_guessCount), W - MARGIN, y - 2, FONT_TITLE, BLACK, true);
        textRight(std::string("of ") + std::to_string(MAX_DAYS) + "  \xC2\xB7  " + L.name, W - MARGIN, y + 36,
                  GAME_CAPTION, DARK);
    } else {
        textRight("The key", W - MARGIN, y + 2, GAME_SMALL, DARK);
        textRight("was never broken", W - MARGIN, y + 28, GAME_SMALL, DARK);
    }
    y += ts + 18;

    if (!m_won) {
        y = textWrapped(kIntercepts[m_message].aftermath, MARGIN, y, W - 2 * MARGIN, GAME_SMALL + 1, INK, 0, 6) + 8;
    }
    y = drawTelegram(y, m_won) + 16;
    if (m_won) {
        int r = 0;
        while (r < 4 && m_guessCount > L.rating[r]) r++;
        y = textWrapped(kRatings[r], MARGIN, y, W - 2 * MARGIN, GAME_SMALL + 2, BLACK, 2, 4) + 12;
    }

    int by = H - 24 - 72;
    int sy = by - 40;
    drawWorking(y, sy - 10);

    std::string rec = "Won " + std::to_string(m_wins) + " of " + std::to_string(m_played) + "  \xC2\xB7  streak " +
                      std::to_string(m_streak);
    text(rec, MARGIN, sy, GAME_SMALL, DARK);
    if (m_won && m_newBest) textRight("New best!", W - MARGIN, sy, GAME_SMALL, BLACK, true);
    else if (m_best[m_level] > 0)
        textRight("Best " + plural(m_best[m_level], "day", "days"), W - MARGIN, sy, GAME_SMALL, DARK);

    Rect again(MARGIN, by, (W - 2 * MARGIN - 16) * 3 / 5, 72);
    Rect done(again.x + again.w + 16, by, W - 2 * MARGIN - again.w - 16, 72);
    button(again, "Next intercept", ButtonStyle::PRIMARY, FONT_BODY);
    button(done, "Title", ButtonStyle::SECONDARY, FONT_BODY);
    addButton(again, A_AGAIN);
    addButton(done, A_TITLE);
}
