#pragma once

#include <array>
#include <cstdint>

namespace Games {

class Random {
public:
    explicit Random(uint32_t seed) : state(seed ? seed : 1) {}
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int below(int n) { return static_cast<int>(next() % n); }

private:
    uint32_t state;
};

struct MiniSudoku {
    std::array<int, 16> cells{};
    std::array<bool, 16> fixed{};
    void reset(uint32_t seed);
    bool legal(int cell, int value) const;
    bool place(int cell, int value);
    bool won() const;
    int solutions() const;
};

struct LightsOut {
    std::array<bool, 25> cells{};
    void reset(uint32_t seed);
    bool toggle(int cell);
    bool won() const;
};

struct SlidingPuzzle {
    std::array<int, 9> cells{};
    void reset(uint32_t seed);
    bool move(int cell);
    bool won() const;
};

struct WordSearch {
    static constexpr int side = 8;
    std::array<char, 64> cells{};
    std::array<bool, 6> found{};
    std::array<bool, 64> marked{};
    static const std::array<const char*, 6> words;
    void reset(uint32_t seed);
    int select(int first, int last);
    bool won() const;
};

} // namespace Games
