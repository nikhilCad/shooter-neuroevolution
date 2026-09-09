#pragma once
#include "Simulation.h"
#include <cstdint>
#include <string>
#include <vector>

// Trains one fresh NEAT population from scratch (same code path as the
// interactive run, just headless and multi-threaded like the parameter
// sweep) and snapshots the all-time-best genome at a handful of generation
// milestones along the way — so "what did the best genome look like at
// generation N" can be answered later without needing to have saved that
// exact moment during a live run (the interactive save.dat only ever holds
// the latest state, nothing in between).
struct CheckpointTrainOptions
{
    int populationSize = POPULATION_SIZE;
    float mutationRate = MUTATION_RATE;
    float mutationStrength = MUTATION_STRENGTH;
    std::vector<int> checkpointGenerations = {1, 100, 1000, 4000, 10000, 25000, 50000};
    // Same determinism guarantee as the sweep's seed (see RandomUtil.h's
    // CombineSeed): same seed + same code reproduces byte-identical genomes
    // at every checkpoint, regardless of core count/thread scheduling.
    uint64_t seed = 0;
    bool seedSpecified = false;
    std::string outDir = "recordings/checkpoints";
};

// Recognized flags: --checkpoints=1,100,1000,4000,10000,25000,50000
//                    --population=80  --mutation-rate=0.15  --mutation-strength=0.5
//                    --seed=12345 (omit for a fresh, auto-generated, reported seed)
//                    --out-dir=recordings/checkpoints
CheckpointTrainOptions ParseCheckpointTrainOptions(int argc, char **argv);

// Trains headlessly up to max(checkpointGenerations), writing
// <outDir>/gen_<N>.genome (via Genome.h's SaveGenomeToFile) for every N in
// checkpointGenerations, each holding that generation's
// Evolution::bestGenomeEver — the best genome found by any generation up to
// and including N.
void RunCheckpointTraining(const CheckpointTrainOptions &options);
