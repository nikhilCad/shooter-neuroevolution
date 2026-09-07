#pragma once
#include "NeuralNetwork.h"
#include <vector>

// Manages the population of player brains across generations: a simple
// (elitism + mutation) evolutionary strategy. Each genome plays one live
// episode; once the whole population has played, the population evolves.
struct Evolution
{
    std::vector<NeuralNetwork> population;
    std::vector<float> fitness;
    std::vector<float> episodeScores; // this generation's score per genome, parallel to fitness
    std::vector<float> episodeTimes;  // this generation's survival time per genome, parallel to fitness
    int populationSize;
    int eliteCount;
    float mutationRate;
    float mutationStrength;
    int generation;
    int currentGenomeIndex;
    float bestFitnessEver;
    int bestScoreEver;
    float bestTimeEver;

    // Stagnation tracking: how many generations in a row the smoothed trend
    // hasn't improved. Uses an exponential moving average of each
    // generation's best fitness rather than the single noisy best-ever value
    // (fitness varies a lot between replays since enemy spawns are random,
    // so one lucky episode shouldn't set a bar nothing can ever beat again).
    // Used to ramp mutation strength up automatically when the population
    // plateaus, and relax it back down once progress resumes.
    int stagnantGenerations;
    float recentBestTrend;

    // One entry per completed generation (the best any genome achieved that
    // generation), for the HUD's fitness/score/time-over-generations graphs.
    std::vector<float> fitnessHistory;
    std::vector<float> scoreHistory;
    std::vector<float> timeHistory;
};

Evolution CreateEvolution(int inputSize, int hiddenSize, int outputSize,
                           int populationSize, int eliteCount,
                           float mutationRate, float mutationStrength);

const NeuralNetwork &CurrentGenome(const Evolution &evo);

// Records the outcome of the episode just played by the current genome
// (fitness for evolution, plus score/time purely for HUD "best ever" tracking),
// then advances to the next genome — evolving to the next generation once the
// whole population has had its turn.
void FinishEpisode(Evolution &evo, float episodeFitness, int episodeScore, float episodeTime);

// Persists the whole population's weights, generation progress, best-ever
// stats, and graph history to a file, so training can resume across runs.
bool SaveEvolution(const Evolution &evo, const char *filePath);

// Reconstructs an Evolution from a file saved by SaveEvolution. Returns false
// (leaving evo untouched) if the file doesn't exist or isn't a valid save —
// the caller should fall back to CreateEvolution in that case.
bool LoadEvolution(Evolution &evo, const char *filePath);
