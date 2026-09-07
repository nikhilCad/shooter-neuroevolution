#pragma once
#include "Simulation.h"
#include <vector>

// All of the sweep's behavior is driven by CLI flags instead of hardcoded
// values — see ParseSweepOptions for the flag list, or run `make sweep` with
// no ARGS to use these defaults.
struct SweepOptions
{
    std::vector<int> populationSizes = {20, 40, 60};
    std::vector<int> hiddenSizes = {12, 24};
    int generationBudget = 4000;
    float eliteRatio = 0.2f;
    float mutationRate = MUTATION_RATE;
    float mutationStrength = MUTATION_STRENGTH;
};

// Recognized flags: --populations=20,40,60  --hidden=12,24  --generations=4000
//                    --elite-ratio=0.2  --mutation-rate=0.15  --mutation-strength=0.5
SweepOptions ParseSweepOptions(int argc, char **argv);

// Runs every (population size x hidden size) combination back-to-back, each
// for options.generationBudget generations, with no visible window. Writes
// sweep_results.txt, one PNG per config under sweep_images/, and an
// auto-generated README.md tying it all together.
void RunParameterSweep(const SweepOptions &options);
