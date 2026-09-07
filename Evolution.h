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
