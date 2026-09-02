#include "HeroAdventureEngine.h"
#include "HeroGameData.h"
#include "SillyNames.h"
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <random>
static std::mt19937 rng(time(nullptr));
#endif

HeroAdventureEngine::HeroAdventureEngine() 
    : currentState(GameState::FrontPage) {
    // NOTE: hero/combat/dungeon are properly default-constructed via their
    // member initializers (see HeroAdventureEngine.h). Do NOT memset() these -
    // they contain std::string members, and memset()'ing a constructed
    // std::string corrupts its internal representation (undefined behavior,
    // causes crashes later when the string is assigned/destroyed).
    
#ifdef NATIVE_TESTING
    rng.seed(time(nullptr));
#endif
}

std::vector<std::string> HeroAdventureEngine::generateRandomNames(int count) {
    std::vector<std::string> result;
    count = std::min(count, NUM_SILLY_NAMES);
    
    // Create a vector of all indices and shuffle
    std::vector<int> indices;
    for (int i = 0; i < NUM_SILLY_NAMES; i++) {
        indices.push_back(i);
    }
    
#ifdef NATIVE_TESTING
    std::shuffle(indices.begin(), indices.end(), rng);
#else
    for (int i = 0; i < NUM_SILLY_NAMES - 1; i++) {
        int j = i + (rand() % (NUM_SILLY_NAMES - i));
        std::swap(indices[i], indices[j]);
    }
#endif
    
    for (int i = 0; i < count; i++) {
        result.push_back(SILLY_HERO_NAMES[indices[i]]);
    }
    
    return result;
}

std::string HeroAdventureEngine::getRandomName() {
    auto names = generateRandomNames(1);
    return names.empty() ? "Hero" : names[0];
}

void HeroAdventureEngine::initNewGame(const std::string& heroName, HeroClass heroClass) {
    hero.name = heroName;
    hero.heroClass = heroClass;
    hero.hp = MAX_HP;
    hero.maxHp = MAX_HP;
    hero.cash = STARTING_GOLD;
    hero.currentLeg = 0;
    hero.legEventCount = 0;
    hero.dungeonsFoundInLeg = 0;
    hero.superMonsterSeenInLeg = false;
    hero.superMonstersDefeated = 0;
    hero.dungeonsCleared = 0;
    
    // Initialize base skills
    for (int i = 0; i < static_cast<int>(Skill::NUM_SKILLS); i++) {
        hero.skills[i] = BASE_SKILL_VALUE;
    }
    
    // Apply class bonuses
    switch (heroClass) {
        case HeroClass::Hitter:
            hero.skills[static_cast<int>(Skill::Fighting)] += CLASS_SKILL_BOOST;
            hero.skills[static_cast<int>(Skill::Defending)] += CLASS_SKILL_BOOST;
            hero.skills[static_cast<int>(Skill::Camping)] += CLASS_SKILL_BOOST;
            break;
        case HeroClass::Blaster:
            hero.skills[static_cast<int>(Skill::Magic)] += CLASS_SKILL_BOOST;
            hero.skills[static_cast<int>(Skill::Spotting)] += CLASS_SKILL_BOOST;
            hero.skills[static_cast<int>(Skill::Medical)] += CLASS_SKILL_BOOST;
            break;
        case HeroClass::Hider:
            hero.skills[static_cast<int>(Skill::Stealth)] += CLASS_SKILL_BOOST;
            hero.skills[static_cast<int>(Skill::Salvaging)] += CLASS_SKILL_BOOST;
            hero.skills[static_cast<int>(Skill::Spotting)] += CLASS_SKILL_BOOST;
            break;
    }
    
    currentState = GameState::Journey;
}

void HeroAdventureEngine::gameOver(const std::string& reason) {
    currentState = GameState::GameOver;
}

void HeroAdventureEngine::setHeroClass(HeroClass cls) {
    hero.heroClass = cls;
}

void HeroAdventureEngine::advanceJourneyEvent() {
    if (hero.legEventCount >= EVENTS_PER_LEG - 1) {
        if (hero.currentLeg >= NUM_LEGS - 1) {
            currentState = GameState::Capital;
        } else {
            currentState = GameState::LevelUp;
        }
        return;
    }
    
    hero.legEventCount++;
    generateNextJourneyEvent();
}

bool HeroAdventureEngine::canContinueJourney() const {
    return hero.hp > 0 && currentState == GameState::Journey;
}

void HeroAdventureEngine::generateNextJourneyEvent() {
    // Determine event type based on random roll and leg progression
    int roll = rollD100();
    const MonsterData* monster = nullptr;
    
    // Event distribution:
    // 1-40: Monster encounter
    // 41-55: Rest/healing
    // 56-65: Treasure
    // 66-75: Dungeon discovery
    // 76-85: Wandering merchant
    // 86-95: Super monster (50% chance if not already seen)
    // 96-100: Fairy encounter
    
    if (roll <= 40) {
        // Monster encounter
        monster = getRandomMonsterForLeg(hero.currentLeg + 1);
        if (monster) {
            hero.lastEvent.type = JourneyEvent::MonsterEncounter;
            hero.lastEvent.monsterName = monster->name;
            hero.lastEvent.monsterHp = monster->hpBase;
            hero.lastEvent.narration = "You encounter a " + std::string(monster->name) + "! Prepare for combat!";
            combat.inCombat = true;
            combat.allowRun = true;  // Regular encounters can be skipped via Run Away
            combat.monsterName = monster->name;
            combat.monsterHp = monster->hpBase;
            currentState = GameState::Combat;
        }
    } else if (roll <= 55) {
        // Rest event
        hero.lastEvent.type = JourneyEvent::Rest;
        int healAmount = 20 + rollD20();
        hero.hp = std::min((int)hero.maxHp, (int)hero.hp + healAmount);
        hero.lastEvent.narration = "You find a safe place to rest and heal " + std::to_string(healAmount) + " HP.";
    } else if (roll <= 65) {
        // Treasure event
        hero.lastEvent.type = JourneyEvent::Treasure;
        uint32_t treasure = 25 + (rollD100() * 2);
        hero.cash += treasure;
        hero.lastEvent.treasureAmount = treasure;
        hero.lastEvent.narration = "You discover " + std::to_string(treasure) + " gold coins hidden in the terrain!";
    } else if (roll <= 75 && hero.dungeonsFoundInLeg < MAX_DUNGEONS_PER_LEG) {
        // Dungeon discovery (if spotting skill check passes)
        uint16_t spotSkill = getEffectiveSkill(Skill::Spotting);
        if (spotSkill + rollD20() > 30) {
            hero.lastEvent.type = JourneyEvent::DungeonFound;
            hero.dungeonsFoundInLeg++;
            hero.lastEvent.narration = "Your keen senses detect a hidden dungeon! Enter to face its dangers?";
        } else {
            hero.lastEvent.type = JourneyEvent::Rest;
            hero.lastEvent.narration = "You travel without incident today.";
        }
    } else if (roll <= 85) {
        // Wandering merchant
        hero.lastEvent.type = JourneyEvent::Trader;
        hero.lastEvent.narration = "A wandering merchant offers to trade goods. You exchange 50 gold for a random item.";
        if (hero.cash >= 50) hero.cash -= 50;
    } else if (roll <= 95 && !hero.superMonsterSeenInLeg) {
        // Super monster (unique per leg)
        hero.superMonsterSeenInLeg = true;
        hero.lastEvent.type = JourneyEvent::SuperMonster;
        
        const char* superMonsterNames[] = {
            "Angry Deer", "Giant Bear", "Wyvern", "Chimera", "Dragon"
        };
        std::string monsterName = superMonsterNames[hero.currentLeg];
        monster = getMonsterByName(monsterName.c_str());
        
        if (monster) {
            hero.lastEvent.monsterName = monster->name;
            hero.lastEvent.monsterHp = monster->hpBase;
            hero.lastEvent.narration = "A legendary " + std::string(monster->name) + " appears! This is a legendary foe!";
            combat.inCombat = true;
            combat.allowRun = false;  // Super monsters cannot be skipped - must fight or resolve
            combat.monsterName = monster->name;
            combat.monsterHp = monster->hpBase;
            currentState = GameState::Combat;
        }
    } else {
        // Fairy encounter or default rest
        hero.lastEvent.type = JourneyEvent::Fairy;
        int bonus = 10 + rollD20();
        hero.cash += bonus;
        hero.lastEvent.narration = "A fairy grants you " + std::to_string(bonus) + " gold for your kindness!";
    }
}

void HeroAdventureEngine::calculateCombatStats() {
    if (!combat.inCombat) return;
    
    const MonsterData* monster = getMonsterByName(combat.monsterName.c_str());
    if (!monster) return;
    
    // Monster stats
    combat.stats.monster_fighting = monster->fighting;
    combat.stats.monster_defending = monster->defending;
    combat.stats.monster_magic = monster->magic;
    
    // Player combat profile
    uint16_t playerFighting = getEffectiveSkill(Skill::Fighting);
    uint16_t playerDefending = getEffectiveSkill(Skill::Defending);
    uint16_t playerStealth = getEffectiveSkill(Skill::Stealth);
    uint16_t playerSalvaging = getEffectiveSkill(Skill::Salvaging);
    
    // Combat attack = player fighting vs monster defending
    combat.stats.combat_attack = playerFighting;
    combat.stats.combat_effective_defense = playerDefending;
    
    // Average damage per round
    // Fight: (player_fighting + avg_roll) - (monster_defending + avg_roll) / 2 damage
    combat.stats.combat_round_damage = std::max(3, (int)(playerFighting - monster->defending) / 2 + 5);
    combat.stats.combat_incoming_damage = std::max(2, (int)(monster->fighting - playerDefending) / 3 + 2);
    
    // Rounds to kill/die
    combat.stats.combat_rounds_to_kill = (combat.monsterHp + combat.stats.combat_round_damage - 1) / combat.stats.combat_round_damage;
    combat.stats.combat_rounds_to_die = (hero.hp + combat.stats.combat_incoming_damage - 1) / combat.stats.combat_incoming_damage;
    
    // Fight win percentage: based on relative stats
    // Higher player fighting vs monster defending = higher win %
    int fightDiff = playerFighting - monster->defending;
    combat.stats.fight_win_pct = 50 + (fightDiff * 2);  // Each +1 difference = +2% win chance
    combat.stats.fight_win_pct = std::max(10, std::min(90, combat.stats.fight_win_pct));
    
    // Sneak win percentage: based on stealth vs defending (needs stealth > defending + 10)
    int sneakDiff = playerStealth - (monster->defending + 10);
    combat.stats.sneak_win_pct = 30 + (sneakDiff * 3);
    combat.stats.sneak_win_pct = std::max(5, std::min(85, combat.stats.sneak_win_pct));
    
    // Steal win percentage: based on salvaging vs defending (needs salvaging > defending + 15)
    int stealDiff = playerSalvaging - (monster->defending + 15);
    combat.stats.steal_win_pct = 20 + (stealDiff * 2);
    combat.stats.steal_win_pct = std::max(5, std::min(75, combat.stats.steal_win_pct));
    
    // Stealth kill win percentage: needs stealth + salvaging > defending + 20
    int killDiff = (playerStealth + playerSalvaging) - (monster->defending + 20);
    combat.stats.stealth_kill_win_pct = 15 + (killDiff * 2);
    combat.stats.stealth_kill_win_pct = std::max(3, std::min(60, combat.stats.stealth_kill_win_pct));
    
    // Risk bands (High, Medium, Low, Very Low)
    auto getRiskBand = [](int percent) -> std::string {
        if (percent >= 70) return "Low";
        if (percent >= 50) return "Medium";
        if (percent >= 30) return "High";
        return "Very High";
    };
    
    combat.stats.fight_risk_band = getRiskBand(combat.stats.fight_win_pct);
    combat.stats.sneak_risk_band = getRiskBand(combat.stats.sneak_win_pct);
    combat.stats.steal_risk_band = getRiskBand(combat.stats.steal_win_pct);
    combat.stats.stealth_kill_risk_band = getRiskBand(combat.stats.stealth_kill_win_pct);
}

void HeroAdventureEngine::handleCombatAction(const std::string& action) {
    if (!combat.inCombat) return;
    
    const MonsterData* monster = getMonsterByName(combat.monsterName.c_str());
    if (!monster) return;
    
    bool playerWins = false;
    std::string outcome;
    
    if (action == "fight") {
        // Combat: Fighting skill vs Monster defending
        uint16_t playerSkill = getEffectiveSkill(Skill::Fighting);
        uint16_t playerRoll = playerSkill + rollD20();
        uint16_t monsterRoll = monster->defending + rollD20();
        
        if (playerRoll > monsterRoll) {
            playerWins = true;
            uint16_t damage = std::max(5, (int)(playerRoll - monsterRoll) / 2);
            combat.monsterHp = (combat.monsterHp > damage) ? combat.monsterHp - damage : 0;
            outcome = "You hit for " + std::to_string(damage) + " damage!";
        } else {
            uint16_t damage = std::max(2, (int)(monsterRoll - playerRoll) / 3);
            hero.hp = (hero.hp > damage) ? hero.hp - damage : 0;
            outcome = "Monster hits for " + std::to_string(damage) + " damage!";
        }
    } else if (action == "sneak") {
        // Sneak: Stealth skill check to evade
        uint16_t stealthSkill = getEffectiveSkill(Skill::Stealth);
        if (stealthSkill + rollD20() > monster->defending + 15) {
            playerWins = true;
            outcome = "You successfully sneaked away!";
            combat.inCombat = false;
        } else {
            uint16_t damage = monster->fighting / 2;
            hero.hp = (hero.hp > damage) ? hero.hp - damage : 0;
            outcome = "Sneak failed! Monster attacks for " + std::to_string(damage) + " damage!";
        }
    } else if (action == "steal") {
        // Steal: Salvaging skill to rob the monster
        uint16_t salvageSkill = getEffectiveSkill(Skill::Salvaging);
        if (salvageSkill + rollD20() > monster->defending + 20) {
            playerWins = true;
            uint32_t stolen = (monster->cashMin + monster->cashMax) / 2;
            hero.cash += stolen;
            outcome = "You stole " + std::to_string(stolen) + " gold!";
            combat.inCombat = false;
        } else {
            uint16_t damage = monster->fighting;
            hero.hp = (hero.hp > damage) ? hero.hp - damage : 0;
            outcome = "Steal failed! Monster attacks for " + std::to_string(damage) + " damage!";
        }
    } else if (action == "stealth_kill") {
        // Stealth Kill: Stealth + Salvaging check (assassination)
        uint16_t stealthSkill = getEffectiveSkill(Skill::Stealth);
        uint16_t salvageSkill = getEffectiveSkill(Skill::Salvaging);
        if ((stealthSkill + salvageSkill + rollD20()) > (monster->defending + 20)) {
            playerWins = true;
            outcome = "You silently assassinated the " + std::string(monster->name) + "!";
            uint32_t treasure = monster->cashMin + (rollD100() % (monster->cashMax - monster->cashMin + 1));
            hero.cash += treasure;
            outcome += " You found " + std::to_string(treasure) + " gold!";
            combat.inCombat = false;
            combat.monsterHp = 0;
        } else {
            uint16_t damage = monster->fighting;
            hero.hp = (hero.hp > damage) ? hero.hp - damage : 0;
            outcome = "Assassination failed! Monster attacks for " + std::to_string(damage) + " damage!";
        }
    } else if (action == "flee") {
        // Flee: Stealth skill check
        uint16_t stealthSkill = getEffectiveSkill(Skill::Stealth);
        if (stealthSkill + rollD20() > monster->fighting + 10) {
            playerWins = true;
            outcome = "You successfully fled!";
            combat.inCombat = false;
        } else {
            uint16_t damage = monster->fighting * 2 / 3;
            hero.hp = (hero.hp > damage) ? hero.hp - damage : 0;
            outcome = "Failed to flee! Monster attacks for " + std::to_string(damage) + " damage!";
        }
    }
    
    // Check if monster is defeated
    if (combat.monsterHp <= 0) {
        playerWins = true;
        combat.inCombat = false;
        uint32_t treasure = monster->cashMin + (rollD100() % (monster->cashMax - monster->cashMin + 1));
        hero.cash += treasure;
        outcome = "Victory! You defeated the " + std::string(monster->name) + " and gained " + std::to_string(treasure) + " gold!";
        
        if (monster->isSuperMonster) {
            hero.superMonstersDefeated++;
        }
    }
    
    // Check if player died
    if (hero.hp <= 0) {
        combat.inCombat = false;
        currentState = GameState::GameOver;
        outcome = "You were defeated by the " + std::string(monster->name) + "...";
    }
    
    // If combat has ended (won, escaped, or resolved) and player survived, return to journey
    if (!combat.inCombat && currentState == GameState::Combat) {
        currentState = GameState::Journey;
    }
    
    combat.lastCombatNarration = outcome;
}

void HeroAdventureEngine::runAwayFromCombat() {
    combat.inCombat = false;
    combat.lastCombatNarration = "You slip away and avoid the encounter.";
    if (currentState == GameState::Combat) {
        currentState = GameState::Journey;
    }
}

void HeroAdventureEngine::handleDungeonAction(const std::string& action) {
    // Dungeon logic will be implemented
}

void HeroAdventureEngine::levelUpHero(const std::vector<Skill>& skillsToBoost) {
    for (Skill skill : skillsToBoost) {
        if (skill != Skill::NUM_SKILLS) {
            hero.skills[static_cast<int>(skill)] = std::min(100, 
                (int)hero.skills[static_cast<int>(skill)] + 5);
        }
    }
}

std::string HeroAdventureEngine::getSkillName(Skill skill) const {
    switch (skill) {
        case Skill::Fighting: return "Fighting";
        case Skill::Defending: return "Defending";
        case Skill::Magic: return "Magic";
        case Skill::Stealth: return "Stealth";
        case Skill::Salvaging: return "Salvaging";
        case Skill::Spotting: return "Spotting";
        case Skill::Camping: return "Camping";
        case Skill::Medical: return "Medical";
        default: return "Unknown";
    }
}

uint16_t HeroAdventureEngine::getEffectiveSkill(Skill skill) const {
    // TODO: Apply equipment modifiers
    return hero.skills[static_cast<int>(skill)];
}

int HeroAdventureEngine::rollD100() const {
#ifdef NATIVE_TESTING
    return 1 + (rng() % 100);
#else
    return 1 + (rand() % 100);
#endif
}

int HeroAdventureEngine::rollD20() const {
#ifdef NATIVE_TESTING
    return 1 + (rng() % 20);
#else
    return 1 + (rand() % 20);
#endif
}
