#include "HeroAdventureApp.h"
#include "HeroAdventureEngine.h"
#include "HeroGameData.h"
#include "UIJSONLoader.h"
#include "DisplayHAL.h"
#include "TypographyEngine.h"
#include "ui/UIFramework.h"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>

extern uint8_t *framebuffer;
extern TypographyEngine typography;

static UIJSONLoader uiLoader;
static const char* SCREEN_IDS[] = {
    "front_page",
    "character_creation",
    "journey",
    "combat",
    "loot_screen",
    "level_up",
    "capital",
    "death",
    "high_scores"
};

void HeroAdventureApp::onCreate() {
    gameEngine = new HeroAdventureEngine();
    
    // Initialize UI loader
    uiLoader.init("/sd/games/hero_adventure/ui");
    
    // Load first screen
    navigationGoto("front_page");
}

void HeroAdventureApp::onDestroy() {
    if (gameEngine) {
        delete gameEngine;
        gameEngine = nullptr;
    }
}

void HeroAdventureApp::draw() {
    if (needsFullRefresh) {
        renderScreenFull();
        needsFullRefresh = false;
        screenDirty = false;
    } else if (screenDirty) {
        renderScreenPartial();
        screenDirty = false;
    }
}

void HeroAdventureApp::renderScreenFull() {
    UIFramework::performFullScreenDraw(framebuffer, [this]() {
        updateScreenVariables();
        
        int w = DisplayHAL::getWidth();
        int h = DisplayHAL::getHeight();
        uint8_t fg = UIFramework::getForegroundColor();
        
        // Draw title bar
        UIFramework::drawTopBar(framebuffer, currentScreen.title.c_str(), 0);
        
        // Draw main content
        int y = 60;
        int contentHeight = h - 120;
        int contentWidth = w - 40;

        auto renderBlock = [&](const std::vector<std::string>& lines, int startX, int blockWidth) {
            int by = 60;
            for (const auto& line : lines) {
                if (by >= h - 80) break;
                bool isHeader = false;
                int letters = 0;
                for (unsigned char uc : line) {
                    if (std::isalpha(uc)) {
                        letters++;
                        if (std::isupper(uc)) isHeader = true;
                        else { isHeader = false; break; }
                    }
                }
                float baseFontSize = (isHeader && letters > 0) ? 26.0f : 22.0f;

                if (currentScreen.noWrap) {
                    // Try to fit the whole line on a single row by shrinking the font if needed
                    typography.setFontSize(baseFontSize);
                    int measured = typography.measureText(line);
                    float fontSize = baseFontSize;
                    if (measured > blockWidth) {
                        float scale = (float)blockWidth / (float)measured * 0.95f;
                        fontSize = std::max(12.0f, baseFontSize * scale);
                        typography.setFontSize(fontSize);
                    }
                    int labelW = typography.measureText(line);
                    int x = startX + (blockWidth - labelW) / 2;
                    typography.renderText(line, x, by, framebuffer, fg);
                    by += (isHeader ? 40 : 32);
                    by += 4;
                } else {
                    // Normal wrapping behavior
                    typography.setFontSize(baseFontSize);
                    std::string rem = line;
                    while (!rem.empty() && by < h - 80) {
                        int fit = (int)rem.size();
                        while (fit > 0 && typography.measureText(rem.substr(0, fit)) > blockWidth) fit--;
                        if (fit <= 0) fit = 1;
                        size_t split = rem.rfind(' ', fit);
                        std::string part;
                        if (split == std::string::npos || split == 0) {
                            part = rem.substr(0, fit);
                            rem = rem.substr(fit);
                        } else {
                            part = rem.substr(0, split);
                            rem = rem.substr(split + 1);
                        }
                        typography.renderText(part, startX, by, framebuffer, fg);
                        by += (isHeader ? 40 : 32);
                    }
                    by += 4;
                }
            }
        };

        if (currentScreen.layout == "two_column" && (!currentScreen.textLinesLeft.empty() || !currentScreen.textLinesRight.empty())) {
            int gap = 16;
            int leftW = static_cast<int>(contentWidth * currentScreen.leftRatio);
            int rightW = contentWidth - leftW - gap;
            int leftX = 20;
            int rightX = 20 + leftW + gap;

            // Prefer explicit left/right content; fall back to single-column if missing
            if (!currentScreen.textLinesLeft.empty()) renderBlock(currentScreen.textLinesLeft, leftX, leftW);
            else renderBlock(currentScreen.textLines, leftX, leftW);

            if (!currentScreen.textLinesRight.empty()) renderBlock(currentScreen.textLinesRight, rightX, rightW);
        } else {
            // Single-column render (legacy) — reuse renderBlock logic
            renderBlock(currentScreen.textLines, 20, contentWidth);
        }

        // Draw buttons
        int numButtons = currentScreen.buttons.size();
        if (numButtons > 0) {
            // Special-case: name selection screen shows a 2-column grid of name buttons
            if (currentScreen.id == "name_selection") {
                int cols = 2;
                int rows = (numButtons + cols - 1) / cols;
                int buttonSpacing = 10;
                int buttonHeight = 48;
                int totalContentW = w - 40;
                int buttonWidth = (totalContentW - (cols - 1) * buttonSpacing) / cols;
                int startY = 120;

                for (int idx = 0; idx < numButtons; idx++) {
                    int col = idx % cols;
                    int row = idx / cols;
                    int buttonX = 20 + col * (buttonWidth + buttonSpacing);
                    int buttonY = startY + row * (buttonHeight + buttonSpacing);

                    currentScreen.buttons[idx].x = buttonX;
                    currentScreen.buttons[idx].y = buttonY;
                    currentScreen.buttons[idx].w = buttonWidth;
                    currentScreen.buttons[idx].h = buttonHeight;

                    UIFramework::drawButton(framebuffer, buttonX, buttonY, buttonWidth, buttonHeight, "");

                    // Label scaling to avoid wrapping for long generated names
                    float btnBaseFont = 18.0f;
                    typography.setFontSize(btnBaseFont);
                    int labelW = typography.measureText(currentScreen.buttons[idx].label);
                    float fontSize = btnBaseFont;
                    if (labelW > buttonWidth - 8) {
                        float scale = (float)(buttonWidth - 8) / (float)labelW;
                        fontSize = std::max(10.0f, btnBaseFont * scale);
                        typography.setFontSize(fontSize);
                        labelW = typography.measureText(currentScreen.buttons[idx].label);
                    }
                    int labelX = buttonX + (buttonWidth - labelW) / 2;
                    int labelY = buttonY + (buttonHeight - (int)fontSize) / 2;
                    typography.renderText(currentScreen.buttons[idx].label, labelX, labelY, framebuffer, fg);
                }
            } else {
                int buttonY = h - 70;
                int buttonHeight = 60;
                int buttonSpacing = 10;
                int totalButtonWidth = w - 40;
                int buttonWidth = (totalButtonWidth - (numButtons - 1) * buttonSpacing) / numButtons;

                for (size_t i = 0; i < currentScreen.buttons.size(); i++) {
                    int buttonX = 20 + i * (buttonWidth + buttonSpacing);
                    currentScreen.buttons[i].x = buttonX;
                    currentScreen.buttons[i].y = buttonY;
                    currentScreen.buttons[i].w = buttonWidth;
                    currentScreen.buttons[i].h = buttonHeight;

                    UIFramework::drawButton(framebuffer, buttonX, buttonY, buttonWidth, buttonHeight, "");

                    // Button label: scale font down if it's too long to fit the button
                    float btnBaseFont = 18.0f;
                    typography.setFontSize(btnBaseFont);
                    int labelW = typography.measureText(currentScreen.buttons[i].label);
                    float fontSize = btnBaseFont;
                    if (labelW > buttonWidth - 8) {
                        float scale = (float)(buttonWidth - 8) / (float)labelW;
                        fontSize = std::max(10.0f, btnBaseFont * scale);
                        typography.setFontSize(fontSize);
                        labelW = typography.measureText(currentScreen.buttons[i].label);
                    }
                    int labelX = buttonX + (buttonWidth - labelW) / 2;
                    int labelY = buttonY + (buttonHeight - (int)fontSize) / 2;
                    typography.renderText(currentScreen.buttons[i].label, labelX, labelY, framebuffer, fg);
                }
            }
        }
    });
}

void HeroAdventureApp::renderScreenPartial() {
    // For in-screen updates (like stat changes during combat)
    // Only redraw the affected area
    int w = DisplayHAL::getWidth();
    int h = DisplayHAL::getHeight();
    
    UIFramework::performFastPartialUpdate(framebuffer, 0, 60, w, h - 140, [this, w, h]() {
        updateScreenVariables();
        uint8_t fg = UIFramework::getForegroundColor();
        
        int y = 60;
        int contentHeight = h - 120;
        int contentWidth = w - 40;

        if (currentScreen.layout == "two_column" && (!currentScreen.textLinesLeft.empty() || !currentScreen.textLinesRight.empty())) {
            int gap = 16;
            int leftW = static_cast<int>(contentWidth * currentScreen.leftRatio);
            int rightW = contentWidth - leftW - gap;
            int leftX = 20;
            int rightX = 20 + leftW + gap;

            auto renderCol = [&](const std::vector<std::string>& lines, int startX, int blockWidth){
                int by = 60;
                for (const auto& line : lines) {
                    if (by >= h - 80) break;
                    bool isHeader = false;
                    int letters = 0;
                    for (unsigned char uc : line) {
                        if (std::isalpha(uc)) {
                            letters++;
                            if (std::isupper(uc)) isHeader = true;
                            else { isHeader = false; break; }
                        }
                    }
                    float baseFontSize = (isHeader && letters > 0) ? 26.0f : 22.0f;

                    if (currentScreen.noWrap) {
                        typography.setFontSize(baseFontSize);
                        int measured = typography.measureText(line);
                        float fontSize = baseFontSize;
                        if (measured > blockWidth) {
                            float scale = (float)blockWidth / (float)measured * 0.95f;
                            fontSize = std::max(12.0f, baseFontSize * scale);
                            typography.setFontSize(fontSize);
                        }
                        int labelW = typography.measureText(line);
                        int x = startX + (blockWidth - labelW) / 2;
                        typography.renderText(line, x, by, framebuffer, fg);
                        by += (isHeader ? 40 : 32);
                        by += 4;
                    } else {
                        typography.setFontSize(baseFontSize);
                        std::string rem = line;
                        while (!rem.empty() && by < h - 80) {
                            int fit = (int)rem.size();
                            while (fit > 0 && typography.measureText(rem.substr(0, fit)) > blockWidth) fit--;
                            if (fit <= 0) fit = 1;
                            size_t split = rem.rfind(' ', fit);
                            std::string part;
                            if (split == std::string::npos || split == 0) {
                                part = rem.substr(0, fit);
                                rem = rem.substr(fit);
                            } else {
                                part = rem.substr(0, split);
                                rem = rem.substr(split + 1);
                            }
                            typography.renderText(part, startX, by, framebuffer, fg);
                            by += (isHeader ? 40 : 32);
                        }
                        by += 4;
                    }
                }
            };

            if (!currentScreen.textLinesLeft.empty()) renderCol(currentScreen.textLinesLeft, leftX, leftW);
            else renderCol(currentScreen.textLines, leftX, leftW);

            if (!currentScreen.textLinesRight.empty()) renderCol(currentScreen.textLinesRight, rightX, rightW);
        } else {
            // Single-column fallback — render like a column
            auto renderSingle = [&](const std::vector<std::string>& lines, int startX, int blockWidth){
                int by = 60;
                for (const auto& line : lines) {
                    if (by >= h - 80) break;
                    bool isHeader = false;
                    int letters = 0;
                    for (unsigned char uc : line) {
                        if (std::isalpha(uc)) {
                            letters++;
                            if (std::isupper(uc)) isHeader = true;
                            else { isHeader = false; break; }
                        }
                    }
                    float baseFontSize = (isHeader && letters > 0) ? 26.0f : 22.0f;

                    if (currentScreen.noWrap) {
                        typography.setFontSize(baseFontSize);
                        int measured = typography.measureText(line);
                        float fontSize = baseFontSize;
                        if (measured > blockWidth) {
                            float scale = (float)blockWidth / (float)measured * 0.95f;
                            fontSize = std::max(12.0f, baseFontSize * scale);
                            typography.setFontSize(fontSize);
                        }
                        int labelW = typography.measureText(line);
                        int x = startX + (blockWidth - labelW) / 2;
                        typography.renderText(line, x, by, framebuffer, fg);
                        by += (isHeader ? 40 : 32);
                        by += 4;
                    } else {
                        typography.setFontSize(baseFontSize);
                        std::string rem = line;
                        while (!rem.empty() && by < h - 80) {
                            int fit = (int)rem.size();
                            while (fit > 0 && typography.measureText(rem.substr(0, fit)) > blockWidth) fit--;
                            if (fit <= 0) fit = 1;
                            size_t split = rem.rfind(' ', fit);
                            std::string part;
                            if (split == std::string::npos || split == 0) {
                                part = rem.substr(0, fit);
                                rem = rem.substr(fit);
                            } else {
                                part = rem.substr(0, split);
                                rem = rem.substr(split + 1);
                            }
                            typography.renderText(part, startX, by, framebuffer, fg);
                            by += (isHeader ? 40 : 32);
                        }
                        by += 4;
                    }
                }
            };

            renderSingle(currentScreen.textLines, 20, contentWidth);
        }
    });
}

void HeroAdventureApp::handleTouch(int x, int y) {
    printf("HeroAdventureApp::handleTouch(%d, %d) - screen has %zu buttons\n", x, y, currentScreen.buttons.size());
    
    // Check which button was pressed
    for (size_t i = 0; i < currentScreen.buttons.size(); i++) {
        const auto& btn = currentScreen.buttons[i];
        printf("  Button %zu: '%s' at (%d-%d, %d-%d)\n", i, btn.label.c_str(), 
               btn.x, btn.x + btn.w, btn.y, btn.y + btn.h);
        
        if (x >= btn.x && x < btn.x + btn.w &&
            y >= btn.y && y < btn.y + btn.h) {
            printf("    ✓ HIT! action='%s'\n", btn.action.c_str());
            handleAction(btn.action);
            return;
        }
    }
    printf("  ✗ NO BUTTON HIT\n");
}

void HeroAdventureApp::update() {
    // Check if we need to redraw
    if (needsFullRefresh || screenDirty) {
        draw();
    }
    
    // Called periodically for game logic updates
    if (gameEngine && gameEngine->getCurrentState() == GameState::Journey) {
        // Could handle auto-advance or other periodic updates
    }
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

    // Substitute variables in single-column text
    for (auto& line : currentScreen.textLines) {
        for (const auto& var : currentScreen.variables) {
            size_t pos = 0;
            while ((pos = line.find("{" + var.first + "}", pos)) != std::string::npos) {
                line.replace(pos, var.first.length() + 2, var.second);
                pos += var.second.length();
            }
        }
    }

    // Substitute variables in left/right columns
    for (auto& line : currentScreen.textLinesLeft) {
        for (const auto& var : currentScreen.variables) {
            size_t pos = 0;
            while ((pos = line.find("{" + var.first + "}", pos)) != std::string::npos) {
                line.replace(pos, var.first.length() + 2, var.second);
                pos += var.second.length();
            }
        }
    }
    for (auto& line : currentScreen.textLinesRight) {
        for (const auto& var : currentScreen.variables) {
            size_t pos = 0;
            while ((pos = line.find("{" + var.first + "}", pos)) != std::string::npos) {
                line.replace(pos, var.first.length() + 2, var.second);
                pos += var.second.length();
            }
        }
    }

    // Substitute variables in the title bar too (e.g. "Encounter: {monster_name}")
    for (const auto& var : currentScreen.variables) {
        size_t pos = 0;
        while ((pos = currentScreen.title.find("{" + var.first + "}", pos)) != std::string::npos) {
            currentScreen.title.replace(pos, var.first.length() + 2, var.second);
            pos += var.second.length();
        }
    }
    
    // Rebuild the button list from the original template (buttonsOriginal),
    // applying label substitution and visible_if filtering. Geometry (x,y,w,h)
    // is preserved from the previous frame for buttons that already existed,
    // since only renderScreenFull() re-lays out button positions -
    // renderScreenPartial() must not invalidate existing touch hit regions.
    //
    // Defensive fallback: if a screen was built manually without populating
    // buttonsOriginal, treat the current buttons as the template so nothing
    // gets wiped out.
    if (currentScreen.buttonsOriginal.empty() && !currentScreen.buttons.empty()) {
        currentScreen.buttonsOriginal = currentScreen.buttons;
    }
    std::map<std::string, UIButton> previousByAction;
    for (const auto& b : currentScreen.buttons) {
        previousByAction[b.action] = b;
    }
    
    std::vector<UIButton> visibleButtons;
    for (const auto& templateBtn : currentScreen.buttonsOriginal) {
        bool isVisible = templateBtn.visibleIf.empty();
        if (!isVisible) {
            auto it = currentScreen.variables.find(templateBtn.visibleIf);
            isVisible = (it != currentScreen.variables.end() && it->second == "1");
        }
        if (!isVisible) continue;
        
        UIButton resolved = templateBtn;  // carries original label template + action + visibleIf
        for (const auto& var : currentScreen.variables) {
            size_t pos = 0;
            std::string placeholder = "{" + var.first + "}";
            while ((pos = resolved.label.find(placeholder, pos)) != std::string::npos) {
                resolved.label.replace(pos, placeholder.length(), var.second);
                pos += var.second.length();
            }
        }
        
        // Preserve previously computed geometry if this action already existed
        auto prevIt = previousByAction.find(resolved.action);
        if (prevIt != previousByAction.end()) {
            resolved.x = prevIt->second.x;
            resolved.y = prevIt->second.y;
            resolved.w = prevIt->second.w;
            resolved.h = prevIt->second.h;
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
        extern void exitToSystemLauncher();
        exitToSystemLauncher();
    }
}

void HeroAdventureApp::navigationGoto(const std::string& screenId) {
    printf("HeroAdventureApp::navigationGoto('%s')\n", screenId.c_str());
    fflush(stdout);
    if (uiLoader.loadScreenFromFile(screenId, currentScreen)) {
        printf("  Successfully loaded screen '%s' with %zu buttons\n", screenId.c_str(), currentScreen.buttons.size());
        fflush(stdout);
        screenStack.clear();
        screenStack.push_back(currentScreen);
        needsFullRefresh = true;
    } else {
        printf("  FAILED to load screen '%s'\n", screenId.c_str());
        fflush(stdout);
    }
}

void HeroAdventureApp::pushScreen(const std::string& screenId) {
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

void HeroAdventureApp::loadScreenFromJSON(const std::string& screenId) {
    uiLoader.loadScreenFromFile(screenId, currentScreen);
}

void HeroAdventureApp::renderCurrentScreen() {
    // Abstraction for when we need to explicitly re-render
    screenDirty = true;
}

void HeroAdventureApp::drawTextBlock(int startY, int maxWidth) {
    // Helper for complex text rendering
}

void HeroAdventureApp::drawButtonRow(std::vector<UIButton>& buttons) {
    // Helper for button layout
}

// ===== GAME FLOW HANDLERS =====

void HeroAdventureApp::startNewGame() {
    printf("HeroAdventureApp::startNewGame()\n");
    fflush(stdout);
    // Generate 10 random hero names
    currentNameOptions = HeroAdventureEngine::generateRandomNames(10);
    printf("  Generated %zu hero names\n", currentNameOptions.size());
    fflush(stdout);
    selectedNameIndex = -1;
    
    // Show class selection screen
    printf("  Calling navigationGoto('character_creation')\n");
    fflush(stdout);
    navigationGoto("character_creation");
    printf("  navigationGoto returned\n");
    fflush(stdout);
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
