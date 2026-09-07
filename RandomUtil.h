#pragma once
#include <random>

// Thread-local RNG: each thread gets its own independent generator, so
// concurrent callers (e.g. the parameter sweep running configs in parallel)
// never touch shared mutable RNG state and need no locking.
inline std::mt19937 &RandomEngine()
{
    thread_local std::mt19937 engine(std::random_device{}());
    return engine;
}

inline int RandomInt(int minValue, int maxValue)
{
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(RandomEngine());
}
