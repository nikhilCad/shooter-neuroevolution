#include "Evolution.h"
#include "raylib.h"
#include <algorithm>
#include <numeric>

Evolution CreateEvolution(int inputSize, int hiddenSize, int outputSize,
                           int populationSize, int eliteCount,
                           float mutationRate, float mutationStrength)
{
    Evolution evo;
    evo.populationSize = populationSize;
    evo.eliteCount = eliteCount;
    evo.mutationRate = mutationRate;
    evo.mutationStrength = mutationStrength;
    evo.generation = 1;
    evo.currentGenomeIndex = 0;
    evo.bestFitnessEver = -1e9f;

    evo.population.reserve(populationSize);
    for (int i = 0; i < populationSize; i++)
        evo.population.push_back(CreateNeuralNetwork(inputSize, hiddenSize, outputSize));
    evo.fitness.assign(populationSize, 0.0f);

    return evo;
}

const NeuralNetwork &CurrentGenome(const Evolution &evo)
{
    return evo.population[evo.currentGenomeIndex];
}

static void EvolvePopulation(Evolution &evo)
{
    // Rank genome indices by fitness, descending
    std::vector<int> order(evo.populationSize);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b)
              { return evo.fitness[a] > evo.fitness[b]; });

    std::vector<NeuralNetwork> nextGeneration;
    nextGeneration.reserve(evo.populationSize);

    // Elitism: the best performers carry over unchanged
    for (int i = 0; i < evo.eliteCount && i < evo.populationSize; i++)
        nextGeneration.push_back(evo.population[order[i]]);

    // Fill the rest of the population with mutated clones of the elites
    while ((int)nextGeneration.size() < evo.populationSize)
    {
        int parentIndex = order[GetRandomValue(0, evo.eliteCount - 1)];
        nextGeneration.push_back(MutateNetwork(evo.population[parentIndex], evo.mutationRate, evo.mutationStrength));
    }

    evo.population = nextGeneration;
    evo.fitness.assign(evo.populationSize, 0.0f);
    evo.generation++;
    evo.currentGenomeIndex = 0;
}

void FinishEpisode(Evolution &evo, float episodeFitness)
{
    evo.fitness[evo.currentGenomeIndex] = episodeFitness;
    evo.bestFitnessEver = std::max(evo.bestFitnessEver, episodeFitness);

    evo.currentGenomeIndex++;
    if (evo.currentGenomeIndex >= evo.populationSize)
        EvolvePopulation(evo);
}
