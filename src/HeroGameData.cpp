#include "HeroGameData.h"
#include "HeroAdventureEngine.h"
#include <cstring>
#include <cstdlib>

#ifndef NATIVE_TESTING
#include <Arduino.h>
#else
#include <random>
#endif

const MonsterData* getMonsterByName(const char* name) {
    if (!name) return nullptr;
    
    // Search all monster arrays
    for (int i = 0; i < 13; i++) {
        if (strcmp(MONSTERS_LEG1[i].name, name) == 0) return &MONSTERS_LEG1[i];
    }
    for (int i = 0; i < 16; i++) {
        if (strcmp(MONSTERS_LEG2[i].name, name) == 0) return &MONSTERS_LEG2[i];
    }
    for (int i = 0; i < 11; i++) {
        if (strcmp(MONSTERS_LEG3[i].name, name) == 0) return &MONSTERS_LEG3[i];
    }
    for (int i = 0; i < 12; i++) {
        if (strcmp(MONSTERS_LEG4[i].name, name) == 0) return &MONSTERS_LEG4[i];
    }
    for (int i = 0; i < 11; i++) {
        if (strcmp(MONSTERS_LEG5[i].name, name) == 0) return &MONSTERS_LEG5[i];
    }
    
    return nullptr;
}

const MonsterData* getRandomMonsterForLeg(int leg, bool excludeSuperMonster) {
    const MonsterData* monsters = nullptr;
    int count = 0;
    
    switch (leg) {
        case 1:
            monsters = MONSTERS_LEG1;
            count = 13;
            break;
        case 2:
            monsters = MONSTERS_LEG2;
            count = 16;
            break;
        case 3:
            monsters = MONSTERS_LEG3;
            count = 11;
            break;
        case 4:
            monsters = MONSTERS_LEG4;
            count = 12;
            break;
        case 5:
            monsters = MONSTERS_LEG5;
            count = 11;
            break;
        default:
            return nullptr;
    }
    
    if (!monsters || count == 0) return nullptr;
    
    // For now, pick a simple deterministic selection based on event number
    // In real gameplay, use proper random selection with exclusions
    int idx = rand() % count;
    
    // Skip super monsters if requested
    if (excludeSuperMonster && monsters[idx].isSuperMonster) {
        idx = (idx + 1) % count;
    }
    
    return &monsters[idx];
}

const HouseData* getHouseByIndex(int index) {
    if (index < 0 || index >= NUM_HOUSES) return nullptr;
    return &HOUSES[index];
}

uint32_t getHouseCost(const char* houseName) {
    if (!houseName) return 0;
    
    for (int i = 0; i < NUM_HOUSES; i++) {
        if (strcmp(HOUSES[i].name, houseName) == 0) {
            return HOUSES[i].cost;
        }
    }
    
    return 0;
}

uint16_t getHouseMultiplier(const char* houseName) {
    if (!houseName) return 0;
    
    for (int i = 0; i < NUM_HOUSES; i++) {
        if (strcmp(HOUSES[i].name, houseName) == 0) {
            return HOUSES[i].multiplier;
        }
    }
    
    return 0;
}
