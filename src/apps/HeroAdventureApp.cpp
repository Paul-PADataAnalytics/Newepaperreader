#include "HeroAdventureApp.h"
#include "HeroAdventureEngine.h"
#include "HeroGameData.h"
#include "UIJSONLoader.h"
#include "Launcher.h"
#include "ui/UIFramework.h"
#include "ui/UIKit.h"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>

extern uint8_t *framebuffer;

static UIJSONLoader uiLoader;

namespace {

constexpr int CONTENT_TOP = UI::HEADER_H + 24;
constexpr int BTN_H_STACK = 76;
constexpr int BTN_H_GRID = 72;
constexpr int BTN_GAP = 14;
constexpr int BOTTOM_PAD = 28;
constexpr int TEXT_LINE_GAP = 6;
constexpr float BUTTON_FONT = UI::FONT_BODY - 2;
const float BODY_SIZES[] = { UI::FONT_BODY, UI::FONT_BODY - 2, UI::FONT_SMALL + 2,
                             UI::FONT_SMALL, UI::FONT_CAPTION };

// A line whose letters are all upper case ("ENEMY STATS") is a heading.
bool isHeadingLine(const std::string& line) {
    bool header = false;
    for (unsigned char uc : line) {
        if (std::isalpha(uc)) {
            if (std::isupper(uc)) header = true;
            else return false;
        }
    }
    return header;
}

bool isBlank(const std::string& s) {
    for (unsigned char c : s) if (!std::isspace(c)) return false;
    return true;
}

// Same greedy word wrap as UI::textWrapped, used to measure and to draw bold lines.
std::vector<std::string> wrapLines(const std::string& s, int w, float size, bool bold) {
    std::vector<std::string> lines;
    std::string current;
    size_t i = 0;
    while (i <= s.size()) {
        size_t sp = s.find_first_of(" \n", i);
        if (sp == std::string::npos) sp = s.size();
        std::string word = s.substr(i, sp - i);
        std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && UI::textWidth(candidate, size, bold) > w) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
        if (sp < s.size() && s[sp] == '\n') {
            lines.push_back(current);
            current.clear();
        }
        i = sp + 1;
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

void substitute(std::string& s, const std::map<std::string, std::string>& vars) {
    if (s.find('{') == std::string::npos) return;
    for (const auto& var : vars) {
        std::string placeholder = "{" + var.first + "}";
        size_t pos = 0;
        while ((pos = s.find(placeholder, pos)) != std::string::npos) {
            s.replace(pos, placeholder.length(), var.second);
            pos += var.second.length();
        }
    }
}

// Screens whose buttons are a set of equal choices (no default action).
bool isChoiceScreen(const std::string& id) {
    return id == "character_creation" || id == "name_selection" || id == "level_up" ||
           id == "capital";
}

// The device font has no emoji; drop a leading emoji/icon prefix ("⚔️ Fight").
std::string stripLeadingNonAscii(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && ((unsigned char)s[i] >= 0x80 || s[i] == ' ')) i++;
    return i < s.size() ? s.substr(i) : s;
}

std::string lowerCase(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const char* SKILL_KEYS[] = { "fighting", "defending", "magic", "stealth",
                             "salvaging", "spotting", "camping", "medical" };

int skillIndexForKey(const std::string& key) {
    for (int i = 0; i < 8; i++) if (key == SKILL_KEYS[i]) return i;
    return -1;
}

} // namespace

void HeroAdventureApp::onCreate() {
    gameEngine = new HeroAdventureEngine();

    // SD.open() paths are relative to the SD mount root (no "/sd" prefix).
    // The native build ignores this and uses ./games/hero_adventure/ui.
    uiLoader.init("/games/hero_adventure/ui");

    navigationGoto("front_page");
}

void HeroAdventureApp::onDestroy() {
    if (gameEngine) {
        delete gameEngine;
        gameEngine = nullptr;
    }
}

// Called by the launcher on start and after wake: always repaint everything.
void HeroAdventureApp::draw() {
    renderScreenFull();
}

void HeroAdventureApp::update() {
    if (needsFullRefresh) {
        renderScreenFull();
    } else if (screenDirty) {
        renderScreenPartial();
    }
}

// ---------------------------------------------------------------------------
// Portrait layout
// ---------------------------------------------------------------------------

// Action performed by the header back chevron; "" means leave the app and
// "pop" means close a stacked sheet (character sheet / inventory).
std::string HeroAdventureApp::backAction() const {
    if (screenStack.size() > 1) return "pop";
    const std::string& id = currentScreen.id;
    if (id == "front_page" || id.empty()) return "";
    if (id == "name_selection") return "goto:character_creation";
    return "goto:front_page";
}

// Buttons that only duplicate the header back chevron are dropped in portrait.
bool HeroAdventureApp::isRedundantButton(const UIButton& btn) const {
    if (btn.action == "quit") return true;
    if (btn.label != "Back" && btn.label != "Menu") return false;
    std::string back = backAction();
    if (back == "pop") return btn.action.find("close_") == 0;
    return !back.empty() && btn.action == back;
}

void HeroAdventureApp::splitTitle() {
    headerTitle = currentScreen.title;
    subtitle.clear();
    const int maxW = UI::W - (UI::MARGIN + 40) - UI::MARGIN;
    if (UI::textWidth(headerTitle, UI::FONT_TITLE, true) <= maxW) return;
    size_t colon = headerTitle.find(": ");
    if (colon == std::string::npos || colon == 0) return;
    if (headerTitle.substr(0, colon).find('(') != std::string::npos) return;
    subtitle = headerTitle.substr(colon + 2);
    headerTitle = headerTitle.substr(0, colon);
}

// Buttons are stacked full width at the bottom of the screen; four or more
// become a 2-column grid (an odd last button spans both columns).
void HeroAdventureApp::layoutButtons() {
    auto& buttons = currentScreen.buttons;
    int n = (int)buttons.size();
    if (n == 0) {
        buttonsTop = UI::H - BOTTOM_PAD;
        return;
    }
    bool grid = n >= 4;
    int cols = grid ? 2 : 1;
    int rows = (n + cols - 1) / cols;
    int bh = grid ? BTN_H_GRID : BTN_H_STACK;
    int blockH = rows * bh + (rows - 1) * BTN_GAP;
    buttonsTop = UI::H - BOTTOM_PAD - blockH;
    int fullW = UI::W - 2 * UI::MARGIN;
    int colW = (fullW - (cols - 1) * BTN_GAP) / cols;
    for (int i = 0; i < n; i++) {
        int r = i / cols, c = i % cols;
        bool spanAll = grid && (n % 2 == 1) && i == n - 1;
        buttons[i].x = UI::MARGIN + c * (colW + BTN_GAP);
        buttons[i].y = buttonsTop + r * (bh + BTN_GAP);
        buttons[i].w = spanAll ? fullW : colW;
        buttons[i].h = bh;
    }
}

std::string HeroAdventureApp::buttonSignature() const {
    std::string sig;
    for (const auto& b : currentScreen.buttons) sig += b.label + "\x1f" + b.action + "\x1e";
    return sig;
}

std::vector<std::string> HeroAdventureApp::contentLines() const {
    if (currentScreen.layout == "two_column" &&
        (!currentScreen.textLinesLeft.empty() || !currentScreen.textLinesRight.empty())) {
        // Portrait has no room for columns: stack left above right.
        std::vector<std::string> lines = currentScreen.textLinesLeft.empty()
            ? currentScreen.textLines : currentScreen.textLinesLeft;
        if (!currentScreen.textLinesRight.empty()) {
            lines.push_back("");
            lines.insert(lines.end(), currentScreen.textLinesRight.begin(),
                         currentScreen.textLinesRight.end());
        }
        return lines;
    }
    return currentScreen.textLines;
}

// Lays out (and optionally draws) the story text between top and bottom.
// Returns the y below the last line.
int HeroAdventureApp::textBlock(const std::vector<std::string>& lines, int top, int bottom,
                                float size, bool draw, bool centered, int paragraphGap) const {
    const int x = UI::MARGIN;
    const int w = UI::W - 2 * UI::MARGIN;
    const int lh = UI::lineHeight(size) + TEXT_LINE_GAP;
    int y = top;
    for (const auto& line : lines) {
        if (y > top) y += paragraphGap;
        if (isBlank(line)) {
            y += lh / 2;
            continue;
        }
        bool heading = isHeadingLine(line);
        if (heading && y > top) y += 8;
        std::vector<std::string> wrapped = wrapLines(line, w, size, heading);
        int n = (int)wrapped.size();
        if (!draw) {
            y += n * lh;
            continue;
        }
        int fit = (bottom - y) / lh;
        if (fit <= 0) break;
        int shown = std::min(fit, n);
        if (heading) {
            for (int k = 0; k < shown; k++) {
                std::string s = UI::ellipsize(wrapped[k], w, size, true);
                if (centered) UI::textCentered(s, UI::W / 2, y + k * lh, size, UI::BLACK, true);
                else UI::text(s, x, y + k * lh, size, UI::BLACK, true);
            }
        } else {
            UI::textWrapped(line, x, y, w, size, UI::INK, shown < n ? shown : 0, TEXT_LINE_GAP,
                            centered);
        }
        y += shown * lh;
    }
    return y;
}

void HeroAdventureApp::drawButtons() {
    bool choice = isChoiceScreen(currentScreen.id);
    float size = currentScreen.buttons.size() >= 4 ? BUTTON_FONT - 2 : BUTTON_FONT;
    for (size_t i = 0; i < currentScreen.buttons.size(); i++) {
        const UIButton& b = currentScreen.buttons[i];
        UI::ButtonStyle style = ((!choice && i == 0) || b.selected) ? UI::ButtonStyle::PRIMARY
                                                                    : UI::ButtonStyle::SECONDARY;
        UI::button(UI::Rect(b.x, b.y, b.w, b.h), b.label, style, size);
    }
}

// Everything below the header: subtitle, story text and (optionally) buttons.
void HeroAdventureApp::drawContent(bool withButtons) {
    int top = CONTENT_TOP;
    if (!subtitle.empty()) {
        UI::text(UI::ellipsize(subtitle, UI::W - 2 * UI::MARGIN, UI::FONT_BODY, true),
                 UI::MARGIN, top - 4, UI::FONT_BODY, UI::DARK, true);
        top += UI::lineHeight(UI::FONT_BODY) + 10;
        UI::hRule(top, UI::LIGHT);
        top += 18;
    }

    bool front = currentScreen.id == "front_page";
    if (front) {
        const int iconSize = 160;
        UI::icon(ICON_GAME, (UI::W - iconSize) / 2, top + 40, iconSize);
        top += iconSize + 80;
    }

    int bottom = (currentScreen.buttons.empty() ? UI::H - BOTTOM_PAD : buttonsTop) - 24;
    std::vector<std::string> lines = contentLines();
    // Journey lines are separate facts (stats / story / progress): space them out.
    int gap = currentScreen.id == "journey" ? 14 : 0;
    float size = BODY_SIZES[0];
    for (float s : BODY_SIZES) {
        size = s;
        if (textBlock(lines, top, bottom, s, false, front, gap) <= bottom) break;
    }
    textBlock(lines, top, bottom, size, true, front, gap);

    if (withButtons) drawButtons();
}

void HeroAdventureApp::renderScreenFull() {
    updateScreenVariables();
    splitTitle();
    layoutButtons();
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        UI::header(headerTitle, true);
        drawContent(true);
    });
    drawnTitle = currentScreen.title;
    drawnButtonSig = buttonSignature();
    needsFullRefresh = false;
    screenDirty = false;
}

// In-place update (new journey event, level-up selection): only the content
// below the header is refreshed; buttons too if their labels changed.
void HeroAdventureApp::renderScreenPartial() {
    updateScreenVariables();
    splitTitle();
    layoutButtons();
    std::string sig = buttonSignature();
    if (currentScreen.title != drawnTitle) {
        renderScreenFull();
        return;
    }
    bool buttonsChanged = sig != drawnButtonSig;
    int top = UI::HEADER_H;
    int h = (buttonsChanged ? UI::H : buttonsTop) - top;
    UIFramework::performFastPartialUpdate(framebuffer, 0, top, UI::W, h, [this, buttonsChanged]() {
        drawContent(buttonsChanged);
    });
    drawnButtonSig = sig;
    screenDirty = false;
}

void HeroAdventureApp::handleTouch(int x, int y) {
    for (size_t i = 0; i < currentScreen.buttons.size(); i++) {
        const auto& btn = currentScreen.buttons[i];
        if (x >= btn.x && x < btn.x + btn.w && y >= btn.y && y < btn.y + btn.h) {
            std::string action = btn.action;  // handleAction may replace currentScreen
            handleAction(action);
            return;
        }
    }
}

bool HeroAdventureApp::onBack() {
    std::string action = backAction();
    if (action.empty()) return false;
    if (action == "pop") popScreen();
    else handleAction(action);
    return true;
}

void HeroAdventureApp::updateScreenVariables() {
    currentScreen.variables.clear();
    
    // IMPORTANT: Restore original text lines before substituting
    // This allows us to re-substitute with new variable values on each update
    currentScreen.textLines = currentScreen.textLinesOriginal;
    if (currentScreen.titleOriginal.empty() && !currentScreen.title.empty()) {
        currentScreen.titleOriginal = currentScreen.title;
    }
    currentScreen.title = currentScreen.titleOriginal;
    
    const Hero& hero = gameEngine->getHero();
    const JourneyEvent& event = gameEngine->getCurrentEvent();
    
    char buffer[512];
    
    // Basic hero info
    snprintf(buffer, sizeof(buffer), "%d", hero.hp);
    currentScreen.variables["hp"] = buffer;
    
    snprintf(buffer, sizeof(buffer), "%d", hero.maxHp);
    currentScreen.variables["max_hp"] = buffer;
    
    snprintf(buffer, sizeof(buffer), "%u", hero.cash);
    currentScreen.variables["cash"] = buffer;
    
    snprintf(buffer, sizeof(buffer), "%d", hero.currentLeg + 1);
    currentScreen.variables["leg"] = buffer;
    
    snprintf(buffer, sizeof(buffer), "%d", hero.legEventCount + 1);
    currentScreen.variables["event"] = buffer;
    
    snprintf(buffer, sizeof(buffer), "%d", hero.dungeonsFoundInLeg);
    currentScreen.variables["dungeons_found"] = buffer;
    
    // Skills
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[0]);
    currentScreen.variables["fighting"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[1]);
    currentScreen.variables["defending"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[2]);
    currentScreen.variables["magic"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[3]);
    currentScreen.variables["stealth"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[4]);
    currentScreen.variables["salvaging"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[5]);
    currentScreen.variables["spotting"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[6]);
    currentScreen.variables["camping"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", hero.skills[7]);
    currentScreen.variables["medical"] = buffer;
    
    // Journey event narration
    currentScreen.variables["event_narration"] = event.narration;
    
    // Combat variables
    currentScreen.variables["monster_name"] = event.monsterName;
    snprintf(buffer, sizeof(buffer), "%d", event.monsterHp);
    currentScreen.variables["monster_hp"] = buffer;
    currentScreen.variables["combat_narration"] = gameEngine->getCombatNarration();
    currentScreen.variables["result_text"] = combatResultText;  // Combat result outcome
    
    // Combat stats (only populated if combat is active)
    if (gameEngine->isCombatActive()) {
        const CombatStats& stats = gameEngine->getCombatStats();
        
        snprintf(buffer, sizeof(buffer), "%d", stats.monster_fighting);
        currentScreen.variables["monster_fighting"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.monster_defending);
        currentScreen.variables["monster_defending"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.monster_magic);
        currentScreen.variables["monster_magic"] = buffer;
        
        snprintf(buffer, sizeof(buffer), "%d", stats.combat_attack);
        currentScreen.variables["combat_attack"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.combat_effective_defense);
        currentScreen.variables["combat_effective_defense"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.combat_round_damage);
        currentScreen.variables["combat_round_damage"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.combat_incoming_damage);
        currentScreen.variables["combat_incoming_damage"] = buffer;
        
        snprintf(buffer, sizeof(buffer), "%d", stats.combat_rounds_to_kill);
        currentScreen.variables["combat_rounds_to_kill"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.combat_rounds_to_die);
        currentScreen.variables["combat_rounds_to_die"] = buffer;
        
        snprintf(buffer, sizeof(buffer), "%d", stats.fight_win_pct);
        currentScreen.variables["fight_win_pct"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.sneak_win_pct);
        currentScreen.variables["sneak_win_pct"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.steal_win_pct);
        currentScreen.variables["steal_win_pct"] = buffer;
        snprintf(buffer, sizeof(buffer), "%d", stats.stealth_kill_win_pct);
        currentScreen.variables["stealth_kill_win_pct"] = buffer;
        
        currentScreen.variables["fight_risk_band"] = stats.fight_risk_band;
        currentScreen.variables["sneak_risk_band"] = stats.sneak_risk_band;
        currentScreen.variables["steal_risk_band"] = stats.steal_risk_band;
        currentScreen.variables["stealth_kill_risk_band"] = stats.stealth_kill_risk_band;
        
        // Whether the "Run Away" (skip) option is available for this encounter
        currentScreen.variables["allow_run"] = gameEngine->canRunFromCombat() ? "1" : "0";
    }
    
    // Level up info
    snprintf(buffer, sizeof(buffer), "%d", (int)selectedLevelUpSkills.size());
    currentScreen.variables["skills_selected"] = buffer;
    snprintf(buffer, sizeof(buffer), "%d", 3 - (int)selectedLevelUpSkills.size());
    currentScreen.variables["skills_remaining"] = buffer;
    
    // Name options for name selection screen
    for (int i = 0; i < std::min(10, (int)currentNameOptions.size()); i++) {
        snprintf(buffer, sizeof(buffer), "name_%d", i);
        currentScreen.variables[buffer] = currentNameOptions[i];
    }
    
    // Leg name
    const char* legNames[] = {
        "Startersville to Forest Edge",
        "Forest Edge to Mountain Pass",
        "Mountain Pass to Desert Crossing",
        "Desert Crossing to Riverlands",
        "Riverlands to Capital"
    };
    if (hero.currentLeg < 5) {
        currentScreen.variables["leg_name"] = legNames[hero.currentLeg];
    }
    
    // Character sheet variables
    currentScreen.variables["hero_name"] = hero.name;
    
    // Class name
    const char* classNames[] = { "Hitter", "Blaster", "Hider" };
    if ((int)hero.heroClass >= 0 && (int)hero.heroClass < 3) {
        currentScreen.variables["hero_class"] = classNames[(int)hero.heroClass];
    }
    
    // Placeholder variables (simplified for now)
    currentScreen.variables["honor_mark"] = "0";  // TODO: Track honor in game engine
    currentScreen.variables["total_weight"] = "0";  // TODO: Track weight in game engine
    currentScreen.variables["max_weight"] = "20";  // TODO: Calculate from str
    currentScreen.variables["equipped_summary"] = "Basic gear";  // TODO: Show actual equipment
    
    // Restore any two-column text blocks into working buffers
    currentScreen.textLinesLeft = currentScreen.textLinesLeftOriginal;
    currentScreen.textLinesRight = currentScreen.textLinesRightOriginal;

    for (auto& line : currentScreen.textLines) substitute(line, currentScreen.variables);
    for (auto& line : currentScreen.textLinesLeft) substitute(line, currentScreen.variables);
    for (auto& line : currentScreen.textLinesRight) substitute(line, currentScreen.variables);
    currentScreen.title = stripLeadingNonAscii(currentScreen.title);
    substitute(currentScreen.title, currentScreen.variables);

    // Rebuild the button list from the template (buttonsOriginal): label
    // substitution, visible_if filtering and dropping buttons made redundant
    // by the header back chevron. Geometry is assigned by layoutButtons().
    if (currentScreen.buttonsOriginal.empty() && !currentScreen.buttons.empty()) {
        currentScreen.buttonsOriginal = currentScreen.buttons;
    }
    std::vector<UIButton> visibleButtons;
    for (const auto& templateBtn : currentScreen.buttonsOriginal) {
        if (!templateBtn.visibleIf.empty()) {
            auto it = currentScreen.variables.find(templateBtn.visibleIf);
            if (it == currentScreen.variables.end() || it->second != "1") continue;
        }
        if (isRedundantButton(templateBtn)) continue;

        UIButton resolved = templateBtn;
        resolved.label = stripLeadingNonAscii(resolved.label);
        substitute(resolved.label, currentScreen.variables);

        if (currentScreen.id == "level_up" && resolved.action.find("levelup:") == 0) {
            // Resolve the skill from the button label: the JSON indices do not
            // follow the Skill enum order (Spotting/Salvaging are swapped).
            std::string key = lowerCase(resolved.label);
            int idx = skillIndexForKey(key);
            if (idx >= 0) {
                resolved.action = "levelup:" + key;
                int picks = (int)std::count(selectedLevelUpSkills.begin(),
                                            selectedLevelUpSkills.end(), (Skill)idx);
                if (picks > 0) {
                    resolved.label += "  +" + std::to_string(picks * 5);
                    resolved.selected = true;
                }
            }
        }
        visibleButtons.push_back(resolved);
    }
    currentScreen.buttons = visibleButtons;
}

void HeroAdventureApp::handleAction(const std::string& action) {
    if (action.find("goto:") == 0) {
        std::string screenId = action.substr(5);
        navigationGoto(screenId);
    } 
    // Character Creation Flow
    else if (action == "new_game") {
        startNewGame();
    }
    else if (action.find("class:") == 0) {
        std::string className = action.substr(6);
        selectHeroClass(className);
    }
    else if (action.find("selectname:") == 0) {
        int nameIndex = std::stoi(action.substr(11));
        selectHeroName(nameIndex);
    }
    // Journey Flow
    else if (action == "advance_event") {
        advanceJourney();
    }
    // Combat Flow
    else if (action.find("combat:") == 0) {
        std::string combatAction = action.substr(7);
        handleCombatAction(combatAction);
    }
    // Level Up Flow
    else if (action.find("levelup:") == 0) {
        std::string skillName = action.substr(8);
        selectLevelUpSkill(skillName);
    }
    // House Selection
    else if (action.find("house:") == 0) {
        std::string houseName = action.substr(6);
        selectHouse(houseName);
    }
    // Character Sheet and Inventory
    else if (action == "open_character_sheet") {
        pushScreen("character_sheet");
    }
    else if (action == "close_character_sheet") {
        popScreen();
    }
    else if (action == "open_inventory") {
        pushScreen("inventory");
    }
    else if (action == "close_inventory") {
        popScreen();
    }
    // Combat Result Flow
    else if (action == "combat_continue") {
        // Combat result shown, now advance to next state
        if (!gameEngine->isCombatActive()) {
            // Combat ended, advance journey
            gameEngine->advanceJourneyEvent();
            GameState state = gameEngine->getCurrentState();
            
            if (state == GameState::Combat) {
                // Chained encounter rolled immediately after this one
                gameEngine->calculateCombatStats();
                navigationGoto("combat");
            } else if (state == GameState::LevelUp) {
                selectedLevelUpSkills.clear();
                navigationGoto("level_up");
            } else if (state == GameState::Capital) {
                navigationGoto("capital");
            } else if (state == GameState::GameOver) {
                navigationGoto("death");
            } else if (state == GameState::Journey) {
                navigationGoto("journey");
            }
        } else {
            // Combat continues (monster still alive) - recalc stats for the next round
            gameEngine->calculateCombatStats();
            navigationGoto("combat");
        }
    }
    // Run Away / Skip the fight (only available when allow_run is true)
    else if (action == "run_away") {
        gameEngine->runAwayFromCombat();
        navigationGoto("journey");
    }
    // General
    else if (action == "quit") {
        Launcher::getInstance().requestExit();
    }
}

void HeroAdventureApp::navigationGoto(const std::string& screenId) {
    UIScreen loaded;
    if (uiLoader.loadScreenFromFile(screenId, loaded)) {
        currentScreen = loaded;
        screenStack.clear();
        screenStack.push_back(currentScreen);
        needsFullRefresh = true;
    } else {
        printf("HeroAdventureApp: failed to load screen '%s'\n", screenId.c_str());
    }
}

void HeroAdventureApp::pushScreen(const std::string& screenId) {
    // Character sheet <-> inventory toggling: return to the open sheet instead
    // of growing the stack (keeps the back chevron a single step).
    if (screenStack.size() >= 2 && screenStack[screenStack.size() - 2].id == screenId) {
        popScreen();
        return;
    }
    UIScreen newScreen;
    if (uiLoader.loadScreenFromFile(screenId, newScreen)) {
        currentScreen = newScreen;
        screenStack.push_back(currentScreen);
        needsFullRefresh = true;
    }
}

void HeroAdventureApp::popScreen() {
    if (screenStack.size() > 1) {
        screenStack.pop_back();
        currentScreen = screenStack.back();
        needsFullRefresh = true;
    }
}

// ===== GAME FLOW HANDLERS =====

void HeroAdventureApp::startNewGame() {
    currentNameOptions = HeroAdventureEngine::generateRandomNames(10);
    selectedNameIndex = -1;
    navigationGoto("character_creation");
}

void HeroAdventureApp::selectHeroClass(const std::string& className) {
    if (className == "Hitter") {
        selectedClass = HeroClass::Hitter;
    } else if (className == "Blaster") {
        selectedClass = HeroClass::Blaster;
    } else if (className == "Hider") {
        selectedClass = HeroClass::Hider;
    }
    
    // Show name selection with the generated names
    currentScreen = UIScreen();
    currentScreen.id = "name_selection";
    currentScreen.title = "Choose Your Hero's Name";
    currentScreen.textLines = {
        "Select a name for your hero:",
        "You will become a legend known by this name."
    };
    currentScreen.textLinesOriginal = currentScreen.textLines;
    currentScreen.titleOriginal = currentScreen.title;
    currentScreen.noWrap = true; // Keep header lines on single rows for e-paper
    
    for (int i = 0; i < 10; i++) {
        UIButton btn;
        btn.label = currentNameOptions[i];
        btn.action = "selectname:" + std::to_string(i);
        currentScreen.buttons.push_back(btn);
    }
    currentScreen.buttonsOriginal = currentScreen.buttons;
    
    screenStack.clear();
    screenStack.push_back(currentScreen);
    needsFullRefresh = true;
}

void HeroAdventureApp::selectHeroName(int nameIndex) {
    if (nameIndex < 0 || nameIndex >= (int)currentNameOptions.size()) return;
    
    std::string heroName = currentNameOptions[nameIndex];
    
    // Initialize the game engine with selected class and name
    gameEngine->initNewGame(heroName, selectedClass);
    
    // Clear level up selections for this game
    selectedLevelUpSkills.clear();
    
    // Start the journey
    navigationGoto("journey");
}

void HeroAdventureApp::advanceJourney() {
    gameEngine->advanceJourneyEvent();
    GameState state = gameEngine->getCurrentState();
    
    if (state == GameState::Combat) {
        gameEngine->calculateCombatStats();  // Calculate stats before showing combat screen
        navigationGoto("combat");
    } else if (state == GameState::LevelUp) {
        selectedLevelUpSkills.clear();
        navigationGoto("level_up");
    } else if (state == GameState::Capital) {
        navigationGoto("capital");
    } else if (state == GameState::GameOver) {
        navigationGoto("death");
    } else if (state == GameState::Journey) {
        screenDirty = true;  // Refresh journey screen with new event
    }
}

void HeroAdventureApp::handleCombatAction(const std::string& action) {
    // Get the combat narration before handling the action
    std::string beforeNarration = gameEngine->getCombatNarration();
    
    // Handle the combat action
    gameEngine->handleCombatAction(action);
    
    // Get the updated narration which contains the result
    combatResultText = gameEngine->getCombatNarration();
    
    // Show combat result screen
    navigationGoto("combat_result");
}

void HeroAdventureApp::selectLevelUpSkill(const std::string& skillName) {
    // Parse skill name or index and add to selections
    Skill skill = Skill::NUM_SKILLS;
    
    // Check if it's numeric
    bool isNumeric = !skillName.empty() && (isdigit(skillName[0]));
    
    if (isNumeric) {
        // Parse as numeric index
        int skillIndex = std::atoi(skillName.c_str());
        if (skillIndex >= 0 && skillIndex < 8) {
            skill = (Skill)skillIndex;
        }
    } else {
        // Parse as string name
        if (skillName == "fighting") skill = Skill::Fighting;
        else if (skillName == "defending") skill = Skill::Defending;
        else if (skillName == "magic") skill = Skill::Magic;
        else if (skillName == "stealth") skill = Skill::Stealth;
        else if (skillName == "salvaging") skill = Skill::Salvaging;
        else if (skillName == "spotting") skill = Skill::Spotting;
        else if (skillName == "camping") skill = Skill::Camping;
        else if (skillName == "medical") skill = Skill::Medical;
    }
    
    if (skill != Skill::NUM_SKILLS) {
        selectedLevelUpSkills.push_back(skill);
        
        if (selectedLevelUpSkills.size() >= 3) {
            // Apply level up
            gameEngine->levelUpHero(selectedLevelUpSkills);
            
            const Hero& hero = gameEngine->getHero();
            if (hero.currentLeg >= NUM_LEGS - 1) {
                // Final level up done - go to capital
                navigationGoto("capital");
            } else {
                // Advance to next leg
                gameEngine->setCurrentState(GameState::Journey);
                screenDirty = true;
                navigationGoto("journey");
            }
        } else {
            // Still selecting skills
            screenDirty = true;
        }
    }
}

void HeroAdventureApp::selectHouse(const std::string& houseName) {
    const Hero& hero = gameEngine->getHero();
    
    std::string selectedHouse = houseName;
    uint32_t houseCost = getHouseCost(selectedHouse.c_str());
    uint16_t houseMultiplier = getHouseMultiplier(selectedHouse.c_str());
    
    uint32_t finalGold = 0;
    if (hero.cash >= houseCost) {
        finalGold = hero.cash - houseCost;
    } else {
        // Tavern fallback (no cost, but 50% of gold as pension)
        selectedHouse = "Tavern";
        finalGold = hero.cash / 2;
        houseMultiplier = 1;
    }
    
    uint32_t finalScore = finalGold * houseMultiplier;
    
    // Display final score screen
    char scoreBuffer[256];
    snprintf(scoreBuffer, sizeof(scoreBuffer), 
        "Final Score: %u\nGold: %u × %u = %u",
        finalScore, finalGold, houseMultiplier, finalScore);
    
    // Show game over screen with score
    currentScreen = UIScreen();
    currentScreen.id = "game_over";
    currentScreen.title = "Game Complete!";
    currentScreen.textLines = {
        "You retire to the " + std::string(houseName),
        "",
        "Final Gold: " + std::to_string(finalGold),
        "House Multiplier: " + std::to_string(houseMultiplier),
        "",
        "FINAL SCORE: " + std::to_string(finalScore),
        "",
        "Well done, hero!"
    };
    currentScreen.textLinesOriginal = currentScreen.textLines;
    currentScreen.titleOriginal = currentScreen.title;
    
    UIButton backBtn;
    backBtn.label = "Back to Menu";
    backBtn.action = "goto:front_page";
    currentScreen.buttons.push_back(backBtn);
    currentScreen.buttonsOriginal = currentScreen.buttons;
    
    screenStack.clear();
    screenStack.push_back(currentScreen);
    needsFullRefresh = true;
}
