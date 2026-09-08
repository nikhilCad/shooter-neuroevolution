#include "Evolution.h"
#include "raylib.h"
#include "RandomUtil.h"
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <cstdint>

namespace
{
    const uint32_t SAVE_MAGIC = 0x4C4F5645; // 'EVOL'
    const uint32_t SAVE_VERSION = 8;        // v8: added enemy velocity (vx,vy) to the per-enemy input features, so old saves' input size no longer matches

    template <typename T>
    void WriteValue(FILE *file, const T &value)
    {
        fwrite(&value, sizeof(T), 1, file);
    }

    template <typename T>
    bool ReadValue(FILE *file, T &value)
    {
        return fread(&value, sizeof(T), 1, file) == 1;
    }

    void WriteFloatVector(FILE *file, const std::vector<float> &values)
    {
        WriteValue(file, (uint32_t)values.size());
        if (!values.empty())
            fwrite(values.data(), sizeof(float), values.size(), file);
    }

    bool ReadFloatVector(FILE *file, std::vector<float> &values)
    {
        uint32_t count = 0;
        if (!ReadValue(file, count))
            return false;
        values.resize(count);
        if (count == 0)
            return true;
        return fread(values.data(), sizeof(float), count, file) == count;
    }

    void WriteNeuralNetwork(FILE *file, const NeuralNetwork &net)
    {
        WriteValue(file, (int32_t)net.inputSize);
        WriteValue(file, (int32_t)net.hiddenSize);
        WriteValue(file, (int32_t)net.outputSize);
        WriteFloatVector(file, net.weights);
    }

    bool ReadNeuralNetwork(FILE *file, NeuralNetwork &net)
    {
        int32_t inputSize, hiddenSize, outputSize;
        if (!ReadValue(file, inputSize) || !ReadValue(file, hiddenSize) || !ReadValue(file, outputSize))
            return false;
        net.inputSize = inputSize;
        net.hiddenSize = hiddenSize;
        net.outputSize = outputSize;
        return ReadFloatVector(file, net.weights);
    }
}

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
    evo.bestScoreEver = 0;
    evo.bestTimeEver = 0.0f;
    evo.stagnantGenerations = 0;
    evo.recentBestTrend = -1e9f;

    evo.population.reserve(populationSize);
    for (int i = 0; i < populationSize; i++)
        evo.population.push_back(CreateNeuralNetwork(inputSize, hiddenSize, outputSize));
    evo.fitness.assign(populationSize, 0.0f);
    evo.episodeScores.assign(populationSize, 0.0f);
    evo.episodeTimes.assign(populationSize, 0.0f);

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

    // Record this generation's best-of on each metric for the HUD graphs,
    // before the per-genome arrays get reset for the next generation.
    evo.fitnessHistory.push_back(evo.fitness[order[0]]);
    evo.scoreHistory.push_back(*std::max_element(evo.episodeScores.begin(), evo.episodeScores.end()));
    evo.timeHistory.push_back(*std::max_element(evo.episodeTimes.begin(), evo.episodeTimes.end()));

    // Stagnation tracking: compare this generation's best against a smoothed
    // trend of recent generations, not the single noisiest-ever episode —
    // enemy spawns are randomized every episode, so one lucky replay can spike
    // far above what the underlying policy actually earns on average, and
    // comparing against that spike forever would make "no improvement" look
    // permanent even while the population keeps genuinely getting better.
    float thisGenBest = evo.fitness[order[0]];
    const float STAGNATION_EPSILON = 0.01f;
    bool improved = thisGenBest > evo.recentBestTrend + STAGNATION_EPSILON;
    evo.recentBestTrend = (evo.recentBestTrend <= -1e8f)
                               ? thisGenBest
                               : evo.recentBestTrend * 0.9f + thisGenBest * 0.1f;
    evo.stagnantGenerations = improved ? 0 : evo.stagnantGenerations + 1;

    // Ramps from 1x (no boost) up to 2x over the first ~50 stagnant
    // generations — enough of a kick to escape a shallow local optimum
    // without turning reproduction into mostly-random search — then keeps
    // climbing (much more slowly) up to 6x by ~250 stagnant generations for
    // the rare, much deeper plateau that 2x alone can't break out of.
    float stagnationBoost = 1.0f + std::min(evo.stagnantGenerations / 50.0f, 5.0f);

    std::vector<NeuralNetwork> nextGeneration;
    nextGeneration.reserve(evo.populationSize);

    // Elitism: the best performers carry over unchanged
    for (int i = 0; i < evo.eliteCount && i < evo.populationSize; i++)
        nextGeneration.push_back(evo.population[order[i]]);

    // A handful of fully-random "immigrants" every generation, so genetic
    // diversity can't collapse to small mutations of the same few ancestors
    // forever. More of them get injected the longer the population has been
    // stuck, since that's a sign the current gene pool has run out of nearby
    // improvements to find.
    int immigrantCount = std::max(1, (int)(evo.populationSize / 10 * stagnationBoost));
    for (int i = 0; i < immigrantCount && (int)nextGeneration.size() < evo.populationSize; i++)
    {
        const NeuralNetwork &shape = evo.population[0];
        nextGeneration.push_back(CreateNeuralNetwork(shape.inputSize, shape.hiddenSize, shape.outputSize));
    }

    // Fill the rest with mutated clones of the elites, alternating between
    // fine-tuning steps (normal strength) and bigger exploratory jumps (extra
    // strength), both scaled up by the stagnation boost so the search isn't
    // limited to only ever taking tiny steps once things plateau.
    float mutationRate = std::min(evo.mutationRate * stagnationBoost, 0.5f);
    while ((int)nextGeneration.size() < evo.populationSize)
    {
        int parentIndex = order[RandomInt(0, evo.eliteCount - 1)];
        bool exploratory = (nextGeneration.size() % 2) == 0;
        float strength = (exploratory ? evo.mutationStrength * 4.0f : evo.mutationStrength) * stagnationBoost;
        nextGeneration.push_back(MutateNetwork(evo.population[parentIndex], mutationRate, strength));
    }

    evo.population = nextGeneration;
    evo.fitness.assign(evo.populationSize, 0.0f);
    evo.episodeScores.assign(evo.populationSize, 0.0f);
    evo.episodeTimes.assign(evo.populationSize, 0.0f);
    evo.generation++;
    evo.currentGenomeIndex = 0;
}

void FinishEpisode(Evolution &evo, float episodeFitness, int episodeScore, float episodeTime)
{
    evo.fitness[evo.currentGenomeIndex] = episodeFitness;
    evo.episodeScores[evo.currentGenomeIndex] = (float)episodeScore;
    evo.episodeTimes[evo.currentGenomeIndex] = episodeTime;
    evo.bestFitnessEver = std::max(evo.bestFitnessEver, episodeFitness);
    evo.bestScoreEver = std::max(evo.bestScoreEver, episodeScore);
    evo.bestTimeEver = std::max(evo.bestTimeEver, episodeTime);

    evo.currentGenomeIndex++;
    if (evo.currentGenomeIndex >= evo.populationSize)
        EvolvePopulation(evo);
}

bool SaveEvolution(const Evolution &evo, const char *filePath)
{
    FILE *file = fopen(filePath, "wb");
    if (!file)
        return false;

    WriteValue(file, SAVE_MAGIC);
    WriteValue(file, SAVE_VERSION);

    WriteValue(file, (int32_t)evo.populationSize);
    WriteValue(file, (int32_t)evo.eliteCount);
    WriteValue(file, evo.mutationRate);
    WriteValue(file, evo.mutationStrength);
    WriteValue(file, (int32_t)evo.generation);
    WriteValue(file, (int32_t)evo.currentGenomeIndex);
    WriteValue(file, evo.bestFitnessEver);
    WriteValue(file, (int32_t)evo.bestScoreEver);
    WriteValue(file, evo.bestTimeEver);
    WriteValue(file, (int32_t)evo.stagnantGenerations);
    WriteValue(file, evo.recentBestTrend);

    WriteFloatVector(file, evo.fitnessHistory);
    WriteFloatVector(file, evo.scoreHistory);
    WriteFloatVector(file, evo.timeHistory);

    WriteFloatVector(file, evo.fitness);
    WriteFloatVector(file, evo.episodeScores);
    WriteFloatVector(file, evo.episodeTimes);

    for (const NeuralNetwork &net : evo.population)
        WriteNeuralNetwork(file, net);

    fclose(file);
    return true;
}

bool LoadEvolution(Evolution &evo, const char *filePath)
{
    FILE *file = fopen(filePath, "rb");
    if (!file)
        return false;

    uint32_t magic = 0, version = 0;
    bool ok = ReadValue(file, magic) && magic == SAVE_MAGIC &&
              ReadValue(file, version) && version == SAVE_VERSION;

    Evolution loaded{};
    if (ok)
    {
        int32_t populationSize = 0, eliteCount = 0, generation = 0, currentGenomeIndex = 0, bestScoreEver = 0;
        int32_t stagnantGenerations = 0;
        ok = ok && ReadValue(file, populationSize);
        ok = ok && ReadValue(file, eliteCount);
        ok = ok && ReadValue(file, loaded.mutationRate);
        ok = ok && ReadValue(file, loaded.mutationStrength);
        ok = ok && ReadValue(file, generation);
        ok = ok && ReadValue(file, currentGenomeIndex);
        ok = ok && ReadValue(file, loaded.bestFitnessEver);
        ok = ok && ReadValue(file, bestScoreEver);
        ok = ok && ReadValue(file, loaded.bestTimeEver);
        ok = ok && ReadValue(file, stagnantGenerations);
        ok = ok && ReadValue(file, loaded.recentBestTrend);

        ok = ok && ReadFloatVector(file, loaded.fitnessHistory);
        ok = ok && ReadFloatVector(file, loaded.scoreHistory);
        ok = ok && ReadFloatVector(file, loaded.timeHistory);

        ok = ok && ReadFloatVector(file, loaded.fitness);
        ok = ok && ReadFloatVector(file, loaded.episodeScores);
        ok = ok && ReadFloatVector(file, loaded.episodeTimes);

        loaded.populationSize = populationSize;
        loaded.eliteCount = eliteCount;
        loaded.generation = generation;
        loaded.currentGenomeIndex = currentGenomeIndex;
        loaded.bestScoreEver = bestScoreEver;
        loaded.stagnantGenerations = stagnantGenerations;

        if (ok && populationSize > 0)
        {
            loaded.population.reserve(populationSize);
            for (int32_t i = 0; i < populationSize && ok; i++)
            {
                NeuralNetwork net;
                ok = ReadNeuralNetwork(file, net);
                if (ok)
                    loaded.population.push_back(net);
            }
        }

        ok = ok && (int32_t)loaded.population.size() == populationSize &&
             (int32_t)loaded.fitness.size() == populationSize;
    }

    fclose(file);
    if (!ok)
        return false;

    evo = loaded;
    return true;
}
