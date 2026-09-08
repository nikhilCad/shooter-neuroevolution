#pragma once
#include "Simulation.h"
#include <vector>

// All of the sweep's behavior is driven by CLI flags instead of hardcoded
// values — see ParseSweepOptions for the flag list, or run `make sweep` with
// no ARGS to use these defaults. There's no hidden-layer-size axis anymore:
// NEAT genomes start with zero hidden nodes and grow structure via mutation,
// so topology isn't a fixed knob the way it was for the old dense network —
// population size is the only axis left to sweep.
struct SweepOptions
{
    std::vector<int> populationSizes = {20, 40, 60, 80};
    int generationBudget = 4000;
    float mutationRate = MUTATION_RATE;
    float mutationStrength = MUTATION_STRENGTH;
    // Each config is trained this many times (different random draws) so a
    // single lucky/unlucky run can't be mistaken for a config's real merit —
    // the sweep reports the median (and mean) across the repeats.
    int repeatCount = 4;
};

// Recognized flags: --populations=20,40,60  --generations=4000
//                    --mutation-rate=0.15  --mutation-strength=0.5  --repeats=4
SweepOptions ParseSweepOptions(int argc, char **argv);

// Runs every population size back-to-back, each for options.generationBudget
// generations, with no visible window. Writes sweep_results.txt, one PNG per
// config under sweep_images/, and an auto-generated README.md tying it all
// together.
void RunParameterSweep(const SweepOptions &options);
