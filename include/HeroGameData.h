#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Monster definition
struct MonsterData {
    const char* name;
    uint16_t fighting;
    uint16_t defending;
    uint16_t magic;
    uint16_t hpBase;  // fighting + defending
    uint16_t cashMin;
    uint16_t cashMax;
    uint8_t equipMin;
    uint8_t equipMax;
    uint8_t leg;      // 1-5
    bool isRelic;
    bool isBoss;
    bool isSuperMonster;
};

// House definition
struct HouseData {
    const char* name;
    uint32_t cost;
    uint16_t multiplier;
};

// Dungeon floor definition
struct DungeonFloor {
    const char* monsterName;
};

// Dungeon definition
struct DungeonData {
    const char* name;
    const char* bossName;
    DungeonFloor floors[5];  // 5 floors
};

// Leg definition
struct LegData {
    uint8_t id;
    const char* name;
    const char* superMonsterName;
    DungeonData dungeons[2];  // 2 dungeons per leg
};

// ===== MONSTERS =====
// Represents 120+ monsters from the original game
// Using compact arrays to save space

static const MonsterData MONSTERS_LEG1[] = {
    {"Giant Rat", 9, 5, 0, 14, 5, 15, 1, 2, 1, false, false, false},
    {"Rabid Bat", 10, 5, 0, 15, 5, 15, 1, 2, 1, false, false, false},
    {"Goblin", 11, 6, 0, 17, 10, 20, 1, 2, 1, false, false, false},
    {"Wild Wolf", 12, 7, 0, 19, 10, 20, 1, 2, 1, false, false, false},
    {"Forest Kobold", 13, 7, 0, 20, 10, 25, 1, 2, 1, false, false, false},
    {"Forest Spider", 14, 8, 0, 22, 15, 25, 1, 2, 1, false, false, false},
    {"Shadow Bandit", 15, 9, 0, 24, 15, 25, 1, 2, 1, false, false, false},
    {"Forest Witch", 11, 6, 7, 17, 15, 25, 1, 2, 1, false, false, false},
    {"Woodland Stalker", 14, 8, 0, 22, 15, 25, 1, 2, 1, false, false, false},
    {"Bandit", 17, 11, 0, 28, 20, 30, 2, 3, 1, false, false, false},
    {"Angry Deer", 20, 13, 0, 33, 10000, 10000, 1, 2, 1, true, false, true},
    {"Goblin King", 22, 13, 6, 35, 50, 100, 2, 3, 1, true, true, false},
    {"Bandit Lord", 24, 17, 0, 41, 60, 120, 2, 3, 1, true, true, false},
};

static const MonsterData MONSTERS_LEG2[] = {
    {"Cave Goblin", 20, 15, 0, 35, 25, 35, 2, 3, 2, false, false, false},
    {"Giant Spider", 21, 16, 0, 37, 30, 40, 3, 4, 2, false, false, false},
    {"Timber Wolf", 23, 17, 0, 40, 30, 45, 2, 3, 2, false, false, false},
    {"Harpy", 25, 18, 4, 43, 35, 50, 2, 3, 2, false, false, false},
    {"Rock Elemental", 27, 22, 0, 49, 40, 60, 2, 3, 2, false, false, false},
    {"Cliff Stalker", 22, 16, 0, 38, 25, 35, 2, 3, 2, false, false, false},
    {"Mountain Hexer", 20, 15, 8, 35, 30, 40, 2, 3, 2, false, false, false},
    {"Peak Scout", 24, 17, 0, 41, 30, 45, 2, 3, 2, false, false, false},
    {"Mountain Ogre", 28, 20, 0, 48, 40, 60, 3, 4, 2, false, false, false},
    {"Giant Bear", 29, 21, 0, 50, 10000, 10000, 1, 2, 2, true, false, true},
    {"Bandit Chief", 32, 23, 0, 55, 80, 150, 2, 3, 2, true, true, false},
    {"Spiderling", 15, 10, 0, 25, 20, 28, 1, 2, 2, false, false, false},
    {"Web Spinner", 17, 12, 0, 29, 24, 32, 2, 3, 2, false, false, false},
    {"Venomous Spider", 19, 14, 0, 33, 28, 36, 2, 3, 2, false, false, false},
    {"Cave Widow", 21, 15, 2, 36, 32, 40, 3, 4, 2, false, false, false},
    {"Brood Mother", 24, 17, 3, 41, 36, 48, 3, 4, 2, false, false, false},
};

static const MonsterData MONSTERS_LEG3[] = {
    {"Dust Elemental", 30, 24, 10, 54, 35, 45, 3, 4, 3, false, false, false},
    {"Mummy", 31, 24, 8, 55, 40, 50, 4, 5, 3, false, false, false},
    {"Dune Shade", 29, 23, 10, 52, 35, 45, 3, 4, 3, false, false, false},
    {"Tomb Hexer", 30, 24, 12, 54, 40, 50, 3, 4, 3, false, false, false},
    {"Sand Stalker", 33, 25, 0, 58, 40, 50, 3, 4, 3, false, false, false},
    {"Tomb Skeleton", 32, 25, 0, 57, 40, 50, 3, 4, 3, false, false, false},
    {"Giant Scorpion", 33, 26, 0, 59, 40, 50, 3, 4, 3, false, false, false},
    {"Tomb Raider", 34, 26, 0, 60, 45, 60, 3, 4, 3, false, false, false},
    {"Sand Serpent", 35, 27, 0, 62, 45, 60, 3, 4, 3, false, false, false},
    {"Wyvern", 38, 28, 12, 66, 10000, 10000, 1, 2, 3, true, false, true},
    {"Lich", 48, 44, 32, 92, 80, 90, 2, 3, 3, true, true, false},
};

static const MonsterData MONSTERS_LEG4[] = {
    {"River Siren", 36, 30, 16, 66, 50, 65, 4, 5, 4, false, false, false},
    {"Corpse Creeper", 37, 29, 4, 66, 50, 65, 4, 5, 4, false, false, false},
    {"Marsh Witch", 35, 29, 18, 64, 50, 65, 4, 5, 4, false, false, false},
    {"Grave Stalker", 38, 31, 0, 69, 50, 65, 4, 5, 4, false, false, false},
    {"Moonlit Assassin", 40, 33, 0, 73, 55, 70, 4, 5, 4, false, false, false},
    {"Swamp Hydra", 38, 32, 10, 70, 55, 70, 4, 5, 4, false, false, false},
    {"Shadow Gargoyle", 39, 35, 8, 74, 55, 70, 5, 6, 4, false, false, false},
    {"Vampire", 40, 32, 16, 72, 50, 60, 5, 6, 4, false, false, false},
    {"Werewolf", 41, 34, 0, 75, 60, 75, 4, 5, 4, false, false, false},
    {"Chimera", 40, 36, 24, 76, 10000, 10000, 7, 8, 4, true, false, true},
    {"Vampire Lord", 36, 28, 20, 64, 120, 200, 2, 3, 4, true, true, false},
    {"Ancient Dragon", 44, 36, 24, 80, 200, 300, 2, 3, 4, true, true, false},
};

static const MonsterData MONSTERS_LEG5[] = {
    {"Dread Warlock", 45, 38, 30, 83, 60, 80, 5, 6, 5, false, false, false},
    {"Catacomb Wraith", 46, 42, 24, 88, 65, 85, 5, 6, 5, false, false, false},
    {"Void Seer", 44, 40, 32, 84, 65, 85, 5, 6, 5, false, false, false},
    {"Catacomb Assassin", 47, 41, 0, 88, 70, 90, 5, 6, 5, false, false, false},
    {"Lich Acolyte", 45, 42, 28, 87, 65, 85, 5, 6, 5, false, false, false},
    {"Infernal Fiend", 48, 40, 18, 88, 70, 90, 5, 6, 5, false, false, false},
    {"Chaos Knight", 49, 43, 12, 92, 75, 95, 6, 7, 5, false, false, false},
    {"Abyss Golem", 50, 45, 10, 95, 80, 100, 6, 7, 5, false, false, false},
    {"Dragon", 51, 44, 25, 95, 60, 70, 6, 7, 5, false, false, false},
    {"Dark Knight", 52, 44, 12, 96, 250, 400, 2, 3, 5, true, true, false},
    {"Lich King", 60, 52, 40, 112, 300, 500, 2, 3, 5, true, true, false},
};

// ===== HOUSES =====
static const HouseData HOUSES[] = {
    {"Palace", 50000, 50},
    {"Mansion", 20000, 20},
    {"Castle", 10000, 10},
    {"Villa", 5000, 5},
    {"Cottage", 1000, 1},
};

static const int NUM_HOUSES = 5;

// ===== HELPER FUNCTIONS =====

// Get a random monster for a specific leg
const MonsterData* getRandomMonsterForLeg(int leg, bool excludeSuperMonster = true);

// Get a specific monster by name
const MonsterData* getMonsterByName(const char* name);

// Get house by index
const HouseData* getHouseByIndex(int index);

// Get house cost by name
uint32_t getHouseCost(const char* houseName);

// Get house multiplier by name
uint16_t getHouseMultiplier(const char* houseName);
