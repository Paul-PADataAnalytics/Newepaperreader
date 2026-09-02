#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

// Forward declaration
class SillyNameGenerator;

// Game Constants
constexpr int MAX_HP = 100;
constexpr int STARTING_GOLD = 0;
constexpr int BASE_SKILL_VALUE = 5;
constexpr int CLASS_SKILL_BOOST = 20;
constexpr int NUM_LEGS = 5;
constexpr int EVENTS_PER_LEG = 20;
constexpr int MAX_DUNGEONS_PER_LEG = 2;
constexpr int DUNGEON_FLOORS = 5;

// Skill indices
enum class Skill : int {
    Fighting = 0,
    Defending = 1,
    Magic = 2,
    Stealth = 3,
    Salvaging = 4,
    Spotting = 5,
    Camping = 6,
    Medical = 7,
    NUM_SKILLS = 8
};

// Game State Enums
enum class GameState {
    FrontPage,
    CharacterCreation,
    Journey,
    Combat,
    Dungeon,
    LevelUp,
    Capital,
    GameWon,
    GameOver
};

enum class HeroClass {
    Hitter = 0,
    Blaster = 1,
    Hider = 2
};

struct JourneyEvent {
    enum Type {
        MonsterEncounter,
        Treasure,
        Rest,
        Trader,
        DungeonFound,
        SuperMonster,
        Fairy,
        Trap
    } type = MonsterEncounter;
    
    std::string narration;
    std::string monsterName;
    uint16_t monsterHp = 0;
    uint32_t treasureAmount = 0;
};

struct Hero {
    std::string name;
    HeroClass heroClass = HeroClass::Hitter;
    uint16_t hp = 0;
    uint16_t maxHp = 0;
    uint32_t cash = 0;
    uint8_t skills[static_cast<int>(Skill::NUM_SKILLS)] = {0};
    uint8_t currentLeg = 0;      // 0-4
    uint8_t legEventCount = 0;   // 0-19
    uint8_t dungeonsFoundInLeg = 0;
    bool superMonsterSeenInLeg = false;
    uint8_t superMonstersDefeated = 0;
    uint8_t dungeonsCleared = 0;
    JourneyEvent lastEvent;
};

struct CombatStats {
    // Monster stats
    uint8_t monster_fighting = 0;
    uint8_t monster_defending = 0;
    uint8_t monster_magic = 0;
    
    // Player combat profile
    uint16_t combat_attack = 0;
    uint16_t combat_effective_defense = 0;
    uint16_t combat_round_damage = 0;
    uint16_t combat_incoming_damage = 0;
    
    // Combat outcomes
    uint16_t combat_rounds_to_kill = 0;
    uint16_t combat_rounds_to_die = 0;
    
    // Action success percentages
    int fight_win_pct = 0;
    int sneak_win_pct = 0;
    int steal_win_pct = 0;
    int stealth_kill_win_pct = 0;
    
    // Risk bands
    std::string fight_risk_band;
    std::string sneak_risk_band;
    std::string steal_risk_band;
    std::string stealth_kill_risk_band;
};

struct CombatState {
    std::string monsterName;
    uint16_t monsterHp = 0;
    bool inCombat = false;
    bool allowRun = true;  // Regular encounters allow Run Away/skip; super monsters & dungeon bosses do not
    std::string lastCombatNarration;
    CombatStats stats;
};

struct DungeonState {
    std::string dungeonName;
    std::string bossName;
    uint8_t currentFloor = 0;  // 0-4 for floors, 5 for boss
    bool inDungeon = false;
    std::string narration;
};

class HeroAdventureEngine {
public:
    HeroAdventureEngine();
    ~HeroAdventureEngine() = default;

    // Game lifecycle
    void initNewGame(const std::string& heroName, HeroClass heroClass);
    void gameOver(const std::string& reason);
    
    // State management
    GameState getCurrentState() const { return currentState; }
    void setCurrentState(GameState state) { currentState = state; }
    
    // Hero management
    const Hero& getHero() const { return hero; }
    void setHeroClass(HeroClass cls);
    
    // Name generation
    static std::vector<std::string> generateRandomNames(int count = 10);
    static std::string getRandomName();
    
    // Journey logic
    void advanceJourneyEvent();
    bool canContinueJourney() const;
    const JourneyEvent& getCurrentEvent() const { return hero.lastEvent; }
    std::string getJourneyNarration() const { return hero.lastEvent.narration; }
    
    // Combat logic
    bool isCombatActive() const { return combat.inCombat; }
    bool canRunFromCombat() const { return combat.allowRun; }
    const std::string& getCombatNarration() const { return combat.lastCombatNarration; }
    const CombatStats& getCombatStats() const { return combat.stats; }
    void calculateCombatStats();
    void handleCombatAction(const std::string& action);
    void runAwayFromCombat();
    
    // Dungeon logic
    bool isDungeonActive() const { return dungeon.inDungeon; }
    void handleDungeonAction(const std::string& action);
    
    // Leveling
    void levelUpHero(const std::vector<Skill>& skillsToBoost);
    
    // Utility
    std::string getSkillName(Skill skill) const;
    uint16_t getEffectiveSkill(Skill skill) const; // With equipment modifiers
    int rollD100() const;
    int rollD20() const;

private:
    Hero hero;
    GameState currentState;
    CombatState combat;
    DungeonState dungeon;
    
    // Random encounters
    void generateNextJourneyEvent();
    std::string generateCombatNarration(const std::string& monsterName, const std::string& outcome);
};
