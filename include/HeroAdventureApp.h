#pragma once

#include "Application.h"
#include "HeroAdventureEngine.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// Forward declarations

struct UIButton {
    int x, y, w, h;
    std::string label;
    std::string action;
    std::string visibleIf;  // Optional: name of a boolean variable ("1"/"0") gating visibility
    bool selected = false;  // Drawn highlighted (e.g. a picked level-up skill)
};

struct UIScreen {
    std::string id;
    std::string title;
    std::string titleOriginal;  // Store original unsubstituted title (may contain {placeholders})

    // Single-column fallback (original representation)
    std::vector<std::string> textLines;
    std::vector<std::string> textLinesOriginal;  // Store original unsubstituted text

    // Optional two-column layout support: left/right text blocks
    std::vector<std::string> textLinesLeft;
    std::vector<std::string> textLinesLeftOriginal;
    std::vector<std::string> textLinesRight;
    std::vector<std::string> textLinesRightOriginal;

    std::vector<UIButton> buttons;
    std::vector<UIButton> buttonsOriginal;  // Full button set before visible_if filtering
    std::map<std::string, std::string> variables; // For substitution

    // Layout metadata
    std::string layout = "single"; // "single" or "two_column"
    float leftRatio = 0.45f;        // fraction of content width used by left column when two_column
    bool noWrap = false;            // If true, attempt to keep lines on a single row by shrinking to fit
};

class HeroAdventureApp : public Application {
public:
    HeroAdventureApp() = default;
    virtual ~HeroAdventureApp() = default;

    virtual void onCreate() override;
    virtual void onDestroy() override;
    virtual void draw() override;
    virtual void handleTouch(int x, int y) override;
    virtual void update() override;
    virtual bool onBack() override;

private:
    HeroAdventureEngine* gameEngine = nullptr;
    UIScreen currentScreen;
    std::vector<UIScreen> screenStack;

    bool screenDirty = true;
    bool needsFullRefresh = true;

    // Portrait layout state (recomputed on every render)
    std::string headerTitle;      // title shown in the header row
    std::string subtitle;         // overflow part of a long title, shown under the header
    int buttonsTop = 0;           // y of the first button row
    std::string drawnTitle;       // title of the last full render
    std::string drawnButtonSig;   // labels/actions of the last rendered buttons

    // Game state tracking
    HeroClass selectedClass = HeroClass::Hitter;
    std::vector<std::string> currentNameOptions;
    int selectedNameIndex = -1;
    std::vector<Skill> selectedLevelUpSkills;
    std::string combatResultText;  // Store combat outcome text

    // Helper methods
    void renderScreenFull();
    void renderScreenPartial();
    void updateScreenVariables();
    void handleAction(const std::string& action);
    void navigationGoto(const std::string& screenId);
    void pushScreen(const std::string& screenId);
    void popScreen();

    // Portrait layout helpers
    std::string backAction() const;
    bool isRedundantButton(const UIButton& btn) const;
    void splitTitle();
    void layoutButtons();
    std::string buttonSignature() const;
    std::vector<std::string> contentLines() const;
    int textBlock(const std::vector<std::string>& lines, int top, int bottom, float size,
                  bool draw, bool centered, int paragraphGap) const;
    void drawContent(bool withButtons);
    void drawButtons();

    // Game flow handlers
    void startNewGame();
    void selectHeroClass(const std::string& className);
    void selectHeroName(int nameIndex);
    void advanceJourney();
    void handleCombatAction(const std::string& action);
    void selectLevelUpSkill(const std::string& skillName);
    void selectHouse(const std::string& houseName);
};
