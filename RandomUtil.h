#pragma once
#include <cstdint>
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

// Reseeds the calling thread's RNG deterministically from `seed`. Goes
// through a seed_seq (feeding both 32-bit halves of the 64-bit seed) rather
// than mt19937's plain single-value seed(), which only accepts a 32-bit
// result_type and would silently truncate half the seed away.
inline void SeedRandomEngine(uint64_t seed)
{
    std::seed_seq seq{(uint32_t)(seed & 0xFFFFFFFFu), (uint32_t)(seed >> 32)};
    RandomEngine().seed(seq);
}

// Deterministically combines a base seed with two integers (e.g. generation
// and genome index) into a new seed. Used to give every genome's episode its
// own independent, reproducible random stream derived purely from "which
// genome, in which generation, under which base seed" — never from which
// thread happened to run it or when — so a sweep's results stop depending on
// scheduling or core count once every episode is seeded this way.
inline uint64_t CombineSeed(uint64_t base, int a, int b)
{
    std::seed_seq seq{(uint32_t)(base & 0xFFFFFFFFu), (uint32_t)(base >> 32), (uint32_t)a, (uint32_t)b};
    uint32_t out[2];
    seq.generate(out, out + 2);
    return ((uint64_t)out[0] << 32) | out[1];
}
