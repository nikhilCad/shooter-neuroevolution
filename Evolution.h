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
    int populationSize;
    int eliteCount;
    float mutationRate;
    float mutationStrength;
    int generation;
    int currentGenomeIndex;
    float bestFitnessEver;
};

Evolution CreateEvolution(int inputSize, int hiddenSize, int outputSize,
                           int populationSize, int eliteCount,
                           float mutationRate, float mutationStrength);

const NeuralNetwork &CurrentGenome(const Evolution &evo);

// Records the fitness (accumulated reward) of the episode just played by the
// current genome, then advances to the next genome — evolving to the next
// generation once the whole population has had its turn.
void FinishEpisode(Evolution &evo, float episodeFitness);
