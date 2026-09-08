#pragma once
#include "Genome.h"
#include <vector>

// One species: a cluster of genomes whose structure is similar enough
// (GeneticDistance below the compatibility threshold) that they compete
// mainly against each other for offspring slots, rather than against the
// whole population. This is what protects a freshly-grown structural
// mutation (a brand new hidden node, say) long enough to have its weights
// tuned before it has to compete head-to-head with fully-optimized genomes.
struct Species
{
    std::vector<int> memberIndices; // indices into Evolution::population, this generation
    Genome representative;          // compared against to decide who else belongs here next generation
    float bestFitnessEver = -1e9f;
    int generationsSinceImprovement = 0;
};

// Manages the population of player brains across generations using NEAT
// (NeuroEvolution of Augmenting Topologies): genomes are variable-topology
// graphs that grow structure via mutation, speciation protects new structure
// while it's tuned, and crossover recombines two parents' genes aligned by
// historical marking (innovation number). Each genome plays one live
// episode; once the whole population has played, the population evolves.
struct Evolution
{
    std::vector<Genome> population;
    std::vector<float> fitness;
    std::vector<float> episodeScores; // this generation's score per genome, parallel to fitness
    std::vector<float> episodeTimes;  // this generation's survival time per genome, parallel to fitness
    std::vector<Species> species;
    InnovationTracker innovationTracker;
    Genome bestGenomeEver; // preserved into every generation regardless of species, so it can never be lost

    int populationSize;
    float mutationRate;
    float mutationStrength;
    int generation;
    int currentGenomeIndex;
    float bestFitnessEver;
    int bestScoreEver;
    float bestTimeEver;

    // Stagnation tracking: how many generations in a row the smoothed trend
    // hasn't improved. Uses an exponential moving average of each
    // generation's best fitness rather than the single noisiest-ever episode
    // (fitness varies a lot between replays since enemy spawns are random,
    // so one lucky episode shouldn't set a bar nothing can ever beat again).
    // Used to ramp mutation strength/rate (and structural mutation chance) up
    // automatically when the population plateaus, and relax it back down
    // once progress resumes.
    int stagnantGenerations;
    float recentBestTrend;
    // Deepest stagnantGenerations has ever reached this run — a proxy for how
    // hard the anti-stagnation mechanism has had to work, and by extension
    // (since stagnationBoost is a deterministic function of stagnantGenerations)
    // how high mutation strength/structural-mutation chance actually climbed.
    int maxStagnantGenerationsEver = 0;

    // One entry per completed generation (the best any genome achieved that
    // generation), for the HUD's fitness/score/time-over-generations graphs.
    std::vector<float> fitnessHistory;
    std::vector<float> scoreHistory;
    std::vector<float> timeHistory;

    // One entry per completed generation: species count, and population
    // averages of hidden-node/enabled-connection count — diagnostics for
    // whether the population's diversity/structure is actually developing
    // over a run, not shown on any HUD graph (yet), just logged/reported.
    std::vector<int> speciesCountHistory;
    std::vector<float> avgHiddenNodeCountHistory;
    std::vector<float> avgConnectionCountHistory;
};

Evolution CreateEvolution(int inputCount, int outputCount, int populationSize,
                           float mutationRate, float mutationStrength);

const Genome &CurrentGenome(const Evolution &evo);

// Records the outcome of the episode just played by the current genome
// (fitness for evolution, plus score/time purely for HUD "best ever" tracking),
// then advances to the next genome — evolving to the next generation once the
// whole population has had its turn.
void FinishEpisode(Evolution &evo, float episodeFitness, int episodeScore, float episodeTime);

// Persists the whole population's genomes, generation progress, best-ever
// stats, and graph history to a file, so training can resume across runs.
// Species aren't saved — they're cheap to rebuild from scratch on the first
// generation after a resume, since they only affect reproduction, not any
// genome's actual weights/structure.
bool SaveEvolution(const Evolution &evo, const char *filePath);

// Reconstructs an Evolution from a file saved by SaveEvolution. Returns false
// (leaving evo untouched) if the file doesn't exist or isn't a valid save —
// the caller should fall back to CreateEvolution in that case.
bool LoadEvolution(Evolution &evo, const char *filePath);
