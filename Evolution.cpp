#include "Evolution.h"
#include "RandomUtil.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>

namespace
{
    const uint32_t SAVE_MAGIC = 0x4C4F5645; // 'EVOL'
    const uint32_t SAVE_VERSION = 12;       // v12: replaced activeEnemyCount input with forwardConeEnemyCount

    // Roughly this many genomes per species is the target SpeciatePopulation
    // tries to maintain by nudging Evolution::compatibilityThreshold up/down
    // each generation — scaled by population size (not a flat species count)
    // so a small population still ends up with species comfortably above
    // SPECIES_CHAMPION_MIN_SIZE on average, rather than every config in a
    // sweep converging on the same absolute species count regardless of how
    // many genomes actually have to be split across them.
    const int TARGET_MEMBERS_PER_SPECIES = 16;
    const int MIN_TARGET_SPECIES_COUNT = 4;
    const float COMPATIBILITY_THRESHOLD_STEP = 0.1f;
    const float COMPATIBILITY_THRESHOLD_MIN = 0.3f;
    const float COMPATIBILITY_THRESHOLD_MAX = 20.0f;
    // A species needs at least this many members before its champion is
    // copied into the next generation unchanged.
    const int SPECIES_CHAMPION_MIN_SIZE = 5;
    // A species that hasn't improved in this many generations stops getting
    // offspring (unless it's the species currently holding the best genome).
    const int SPECIES_STAGNATION_LIMIT = 15;
    const float PROBABILITY_ADD_CONNECTION = 0.08f;
    const float PROBABILITY_ADD_NODE = 0.03f;
    const float CROSSOVER_RATE = 0.75f;
    const float STAGNATION_EPSILON = 0.01f;

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

    void WriteIntVector(FILE *file, const std::vector<int> &values)
    {
        WriteValue(file, (uint32_t)values.size());
        for (int v : values)
            WriteValue(file, (int32_t)v);
    }

    bool ReadIntVector(FILE *file, std::vector<int> &values)
    {
        uint32_t count = 0;
        if (!ReadValue(file, count))
            return false;
        values.resize(count);
        for (uint32_t i = 0; i < count; i++)
        {
            int32_t v = 0;
            if (!ReadValue(file, v))
                return false;
            values[i] = v;
        }
        return true;
    }

    void WriteGenome(FILE *file, const Genome &genome)
    {
        WriteValue(file, (int32_t)genome.inputCount);
        WriteValue(file, (int32_t)genome.outputCount);
        WriteValue(file, (uint32_t)genome.nodes.size());
        for (const auto &n : genome.nodes)
        {
            WriteValue(file, (int32_t)n.id);
            WriteValue(file, (int32_t)n.type);
        }
        WriteValue(file, (uint32_t)genome.connections.size());
        for (const auto &c : genome.connections)
        {
            WriteValue(file, (int32_t)c.inNode);
            WriteValue(file, (int32_t)c.outNode);
            WriteValue(file, c.weight);
            WriteValue(file, (uint8_t)(c.enabled ? 1 : 0));
            WriteValue(file, (int32_t)c.innovation);
        }
    }

    bool ReadGenome(FILE *file, Genome &genome)
    {
        int32_t inputCount = 0, outputCount = 0;
        uint32_t nodeCount = 0, connectionCount = 0;
        if (!ReadValue(file, inputCount) || !ReadValue(file, outputCount) || !ReadValue(file, nodeCount))
            return false;
        genome.inputCount = inputCount;
        genome.outputCount = outputCount;

        genome.nodes.resize(nodeCount);
        for (auto &n : genome.nodes)
        {
            int32_t id = 0, type = 0;
            if (!ReadValue(file, id) || !ReadValue(file, type))
                return false;
            n.id = id;
            n.type = (NodeType)type;
        }

        if (!ReadValue(file, connectionCount))
            return false;
        genome.connections.resize(connectionCount);
        for (auto &c : genome.connections)
        {
            int32_t inNode = 0, outNode = 0, innovation = 0;
            float weight = 0.0f;
            uint8_t enabled = 0;
            if (!ReadValue(file, inNode) || !ReadValue(file, outNode) || !ReadValue(file, weight) ||
                !ReadValue(file, enabled) || !ReadValue(file, innovation))
                return false;
            c.inNode = inNode;
            c.outNode = outNode;
            c.weight = weight;
            c.enabled = enabled != 0;
            c.innovation = innovation;
        }
        return true;
    }

    // Fitness-proportional pick among `indices`, shifted so the group's
    // lowest fitness still gets a small non-zero chance even if negative.
    int PickWeightedIndex(const std::vector<int> &indices, const std::vector<float> &fitness)
    {
        float minFitness = fitness[indices[0]];
        for (int i : indices)
            minFitness = std::min(minFitness, fitness[i]);
        float shift = -minFitness + 1.0f;

        float total = 0.0f;
        for (int i : indices)
            total += fitness[i] + shift;

        std::uniform_real_distribution<float> dist(0.0f, total);
        float pick = dist(RandomEngine());
        float running = 0.0f;
        for (int i : indices)
        {
            running += fitness[i] + shift;
            if (pick <= running)
                return i;
        }
        return indices.back();
    }

    // Assigns each genome in evo.population to a species (compared against
    // each existing species' representative, first match wins), dropping
    // species nothing landed in and refreshing every survivor's
    // representative to a random current member so species can drift over
    // time instead of being pinned forever to whoever founded them.
    void SpeciatePopulation(Evolution &evo)
    {
        for (auto &s : evo.species)
            s.memberIndices.clear();

        for (int i = 0; i < (int)evo.population.size(); i++)
        {
            Species *match = nullptr;
            for (auto &s : evo.species)
            {
                if (GeneticDistance(evo.population[i], s.representative) < evo.compatibilityThreshold)
                {
                    match = &s;
                    break;
                }
            }
            if (!match)
            {
                Species newSpecies;
                newSpecies.representative = evo.population[i];
                evo.species.push_back(newSpecies);
                match = &evo.species.back();
            }
            match->memberIndices.push_back(i);
        }

        evo.species.erase(std::remove_if(evo.species.begin(), evo.species.end(),
                                          [](const Species &s)
                                          { return s.memberIndices.empty(); }),
                           evo.species.end());

        for (auto &s : evo.species)
            s.representative = evo.population[s.memberIndices[RandomInt(0, (int)s.memberIndices.size() - 1)]];

        // Nudge the threshold toward whatever would have produced the target
        // species count this generation — small, one-step-at-a-time
        // adjustments so it settles rather than oscillates, since genome
        // sizes (and so GeneticDistance's scale) keep drifting all run.
        int targetSpeciesCount = std::max(MIN_TARGET_SPECIES_COUNT, evo.populationSize / TARGET_MEMBERS_PER_SPECIES);
        if ((int)evo.species.size() > targetSpeciesCount)
            evo.compatibilityThreshold = std::min(COMPATIBILITY_THRESHOLD_MAX,
                                                  evo.compatibilityThreshold + COMPATIBILITY_THRESHOLD_STEP);
        else if ((int)evo.species.size() < targetSpeciesCount)
            evo.compatibilityThreshold = std::max(COMPATIBILITY_THRESHOLD_MIN,
                                                  evo.compatibilityThreshold - COMPATIBILITY_THRESHOLD_STEP);
    }

    void UpdateSpeciesStagnation(Evolution &evo)
    {
        for (auto &s : evo.species)
        {
            float bestThisGen = -1e9f;
            for (int i : s.memberIndices)
                bestThisGen = std::max(bestThisGen, evo.fitness[i]);

            if (bestThisGen > s.bestFitnessEver + STAGNATION_EPSILON)
            {
                s.bestFitnessEver = bestThisGen;
                s.generationsSinceImprovement = 0;
            }
            else
            {
                s.generationsSinceImprovement++;
            }
        }
    }

    // Splits totalSlots offspring across species, proportional to each
    // species' total fitness-shared fitness (dividing by species size is
    // what protects a small young species from being swamped by one big
    // one). A species stuck past SPECIES_STAGNATION_LIMIT gets nothing
    // unless it's the one holding the best genome — that species is never
    // fully extinguished.
    std::vector<int> AllocateOffspringCounts(const Evolution &evo, int totalSlots, int bestSpeciesIndex)
    {
        size_t speciesCount = evo.species.size();
        std::vector<float> adjustedFitnessSum(speciesCount, 0.0f);
        std::vector<bool> active(speciesCount, false);

        float grandTotal = 0.0f;
        for (size_t s = 0; s < speciesCount; s++)
        {
            const Species &species = evo.species[s];
            bool stagnant = species.generationsSinceImprovement > SPECIES_STAGNATION_LIMIT;
            active[s] = !stagnant || (int)s == bestSpeciesIndex;
            if (!active[s])
                continue;

            float sum = 0.0f;
            for (int i : species.memberIndices)
                sum += evo.fitness[i] / (float)species.memberIndices.size();
            adjustedFitnessSum[s] = sum;
            grandTotal += sum;
        }

        std::vector<int> counts(speciesCount, 0);
        if (totalSlots <= 0)
            return counts;

        if (grandTotal <= 0.0f)
        {
            int activeCount = 0;
            for (bool a : active)
                activeCount += a ? 1 : 0;
            if (activeCount == 0)
                return counts;
            int share = totalSlots / activeCount;
            for (size_t s = 0; s < speciesCount; s++)
                if (active[s])
                    counts[s] = share;
            return counts;
        }

        std::vector<float> exact(speciesCount, 0.0f);
        int allocated = 0;
        for (size_t s = 0; s < speciesCount; s++)
        {
            if (!active[s])
                continue;
            exact[s] = adjustedFitnessSum[s] / grandTotal * (float)totalSlots;
            counts[s] = (int)exact[s];
            allocated += counts[s];
        }

        // Largest-remainder rounding so the counts sum to exactly totalSlots.
        std::vector<size_t> order(speciesCount);
        for (size_t s = 0; s < speciesCount; s++)
            order[s] = s;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
                  { return (exact[a] - counts[a]) > (exact[b] - counts[b]); });
        for (size_t k = 0; allocated < totalSlots && k < order.size(); k++)
        {
            if (!active[order[k]])
                continue;
            counts[order[k]]++;
            allocated++;
        }
        return counts;
    }

    void EvolvePopulation(Evolution &evo)
    {
        int bestIndex = 0;
        for (int i = 1; i < (int)evo.fitness.size(); i++)
            if (evo.fitness[i] > evo.fitness[bestIndex])
                bestIndex = i;
        float thisGenBest = evo.fitness[bestIndex];

        evo.fitnessHistory.push_back(thisGenBest);
        evo.scoreHistory.push_back(*std::max_element(evo.episodeScores.begin(), evo.episodeScores.end()));
        evo.timeHistory.push_back(*std::max_element(evo.episodeTimes.begin(), evo.episodeTimes.end()));

        // bestFitnessEver is already updated per-episode in FinishEpisode, so
        // by now it's max(everything before this generation, thisGenBest) —
        // thisGenBest >= it exactly when this generation matched or set the
        // all-time record, which is when the saved best genome needs refreshing.
        if (thisGenBest >= evo.bestFitnessEver)
            evo.bestGenomeEver = evo.population[bestIndex];

        // Stagnation tracking: compare this generation's best against a
        // smoothed trend of recent generations, not the single noisiest-ever
        // episode — enemy spawns are randomized every episode, so one lucky
        // replay can spike far above what the underlying policy actually
        // earns on average, and comparing against that spike forever would
        // make "no improvement" look permanent even while genuinely improving.
        bool improved = thisGenBest > evo.recentBestTrend + STAGNATION_EPSILON;
        evo.recentBestTrend = (evo.recentBestTrend <= -1e8f)
                                   ? thisGenBest
                                   : evo.recentBestTrend * 0.9f + thisGenBest * 0.1f;
        evo.stagnantGenerations = improved ? 0 : evo.stagnantGenerations + 1;
        evo.maxStagnantGenerationsEver = std::max(evo.maxStagnantGenerationsEver, evo.stagnantGenerations);

        // Ramps from 1x (no boost) up to 2x at 50 stagnant generations, then
        // keeps climbing (much more slowly) up to its cap of 3x by 100
        // stagnant generations for a deeper plateau that 2x alone can't
        // break out of. Capped lower than an earlier 6x: at that strength,
        // disruptive weight noise landed hard enough — even on the species
        // holding the current best genome (see the exemption below) — to
        // actively prevent it from ever being refined further, rather than
        // just helping genuinely-stuck species escape a plateau.
        float stagnationBoost = 1.0f + std::min(evo.stagnantGenerations / 50.0f, 2.0f);

        SpeciatePopulation(evo);
        UpdateSpeciesStagnation(evo);

        evo.speciesCountHistory.push_back((int)evo.species.size());
        {
            long long totalHidden = 0, totalConnections = 0;
            for (const Genome &g : evo.population)
            {
                for (const auto &n : g.nodes)
                    if (n.type == NodeType::Hidden)
                        totalHidden++;
                for (const auto &c : g.connections)
                    if (c.enabled)
                        totalConnections++;
            }
            evo.avgHiddenNodeCountHistory.push_back((float)totalHidden / (float)evo.population.size());
            evo.avgConnectionCountHistory.push_back((float)totalConnections / (float)evo.population.size());
        }

        int bestSpeciesIndex = -1;
        for (int s = 0; s < (int)evo.species.size(); s++)
            for (int i : evo.species[s].memberIndices)
                if (i == bestIndex)
                    bestSpeciesIndex = s;

        // A handful of fully-random immigrants every generation, so genetic
        // diversity can't collapse to descendants of the same few ancestors
        // forever — more of them the longer the population's been stuck.
        // Reserved outside the species allocation, alongside one slot for
        // the all-time-best genome, which is protected no matter what
        // happens to species/fitness sharing that generation.
        int immigrantCount = std::max(1, (int)(evo.populationSize / 10 * stagnationBoost));
        int reservedSlots = 1 + immigrantCount;
        int speciesSlots = std::max(0, evo.populationSize - reservedSlots);

        std::vector<int> offspringCounts = AllocateOffspringCounts(evo, speciesSlots, bestSpeciesIndex);

        std::vector<Genome> nextGeneration;
        nextGeneration.reserve(evo.populationSize);
        nextGeneration.push_back(evo.bestGenomeEver);

        for (size_t s = 0; s < evo.species.size(); s++)
        {
            const Species &species = evo.species[s];
            int slots = offspringCounts[s];
            if (slots <= 0)
                continue;

            // The species holding the current all-time-best genome is
            // exempted from the stagnation boost — it needs to keep
            // fine-tuning a genuinely good genome at the normal mutation
            // intensity, not have it blasted by noise meant to help
            // genuinely-stuck species escape a plateau. Every other species
            // still rides the full (now-capped) boost, for both weight
            // mutation and structural mutation chance alike.
            float speciesBoost = ((int)s == bestSpeciesIndex) ? 1.0f : stagnationBoost;
            float mutationRate = std::min(evo.mutationRate * speciesBoost, 0.5f);
            float mutationStrength = evo.mutationStrength * speciesBoost;

            std::vector<int> ranked = species.memberIndices;
            std::sort(ranked.begin(), ranked.end(), [&](int a, int b)
                      { return evo.fitness[a] > evo.fitness[b]; });

            int remaining = slots;
            if ((int)species.memberIndices.size() >= SPECIES_CHAMPION_MIN_SIZE && remaining > 0)
            {
                nextGeneration.push_back(evo.population[ranked[0]]); // species champion, unchanged
                remaining--;
            }

            for (int k = 0; k < remaining; k++)
            {
                Genome child;
                if (species.memberIndices.size() >= 2 && RandomInt(0, 99) < (int)(CROSSOVER_RATE * 100.0f))
                {
                    int parentAIdx = PickWeightedIndex(species.memberIndices, evo.fitness);
                    int parentBIdx = PickWeightedIndex(species.memberIndices, evo.fitness);
                    child = Crossover(evo.population[parentAIdx], evo.fitness[parentAIdx],
                                       evo.population[parentBIdx], evo.fitness[parentBIdx]);
                }
                else
                {
                    child = evo.population[PickWeightedIndex(species.memberIndices, evo.fitness)];
                }

                MutateWeights(child, mutationRate, mutationStrength);
                if (RandomInt(0, 999) < (int)(PROBABILITY_ADD_CONNECTION * speciesBoost * 1000.0f))
                    MutateAddConnection(child, evo.innovationTracker);
                if (RandomInt(0, 999) < (int)(PROBABILITY_ADD_NODE * speciesBoost * 1000.0f))
                    MutateAddNode(child, evo.innovationTracker);

                nextGeneration.push_back(std::move(child));
            }
        }

        for (int i = 0; i < immigrantCount && (int)nextGeneration.size() < evo.populationSize; i++)
            nextGeneration.push_back(CreateMinimalGenome(evo.bestGenomeEver.inputCount,
                                                          evo.bestGenomeEver.outputCount, evo.innovationTracker));

        // Rounding/species-extinction edge cases can leave the count short —
        // top up with mutated clones of the all-time best so population size
        // never drifts. Mutated at the base (unboosted) rate/strength, same
        // reasoning as the best species' exemption above: these clones exist
        // to gently vary a genuinely good genome, not to take a big
        // stagnation-escape risk with it.
        while ((int)nextGeneration.size() < evo.populationSize)
        {
            Genome extra = evo.bestGenomeEver;
            MutateWeights(extra, evo.mutationRate, evo.mutationStrength);
            nextGeneration.push_back(std::move(extra));
        }
        nextGeneration.resize(evo.populationSize);

        evo.population = std::move(nextGeneration);
        evo.fitness.assign(evo.populationSize, 0.0f);
        evo.episodeScores.assign(evo.populationSize, 0.0f);
        evo.episodeTimes.assign(evo.populationSize, 0.0f);
        evo.generation++;
        evo.currentGenomeIndex = 0;
    }
}

Evolution CreateEvolution(int inputCount, int outputCount, int populationSize,
                           float mutationRate, float mutationStrength)
{
    Evolution evo;
    evo.populationSize = populationSize;
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
        evo.population.push_back(CreateMinimalGenome(inputCount, outputCount, evo.innovationTracker));
    evo.fitness.assign(populationSize, 0.0f);
    evo.episodeScores.assign(populationSize, 0.0f);
    evo.episodeTimes.assign(populationSize, 0.0f);
    evo.bestGenomeEver = evo.population[0];

    return evo;
}

const Genome &CurrentGenome(const Evolution &evo)
{
    return evo.population[evo.currentGenomeIndex];
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
    WriteValue(file, evo.mutationRate);
    WriteValue(file, evo.mutationStrength);
    WriteValue(file, (int32_t)evo.generation);
    WriteValue(file, (int32_t)evo.currentGenomeIndex);
    WriteValue(file, evo.bestFitnessEver);
    WriteValue(file, (int32_t)evo.bestScoreEver);
    WriteValue(file, evo.bestTimeEver);
    WriteValue(file, (int32_t)evo.stagnantGenerations);
    WriteValue(file, evo.recentBestTrend);
    WriteValue(file, (int32_t)evo.maxStagnantGenerationsEver);
    WriteValue(file, (int32_t)evo.innovationTracker.nextNodeId);
    WriteValue(file, (int32_t)evo.innovationTracker.nextInnovationNumber);

    WriteFloatVector(file, evo.fitnessHistory);
    WriteFloatVector(file, evo.scoreHistory);
    WriteFloatVector(file, evo.timeHistory);
    WriteIntVector(file, evo.speciesCountHistory);
    WriteFloatVector(file, evo.avgHiddenNodeCountHistory);
    WriteFloatVector(file, evo.avgConnectionCountHistory);

    WriteFloatVector(file, evo.fitness);
    WriteFloatVector(file, evo.episodeScores);
    WriteFloatVector(file, evo.episodeTimes);

    WriteGenome(file, evo.bestGenomeEver);
    for (const Genome &g : evo.population)
        WriteGenome(file, g);

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
        int32_t populationSize = 0, generation = 0, currentGenomeIndex = 0, bestScoreEver = 0;
        int32_t stagnantGenerations = 0, maxStagnantGenerationsEver = 0, nextNodeId = 0, nextInnovationNumber = 0;
        ok = ok && ReadValue(file, populationSize);
        ok = ok && ReadValue(file, loaded.mutationRate);
        ok = ok && ReadValue(file, loaded.mutationStrength);
        ok = ok && ReadValue(file, generation);
        ok = ok && ReadValue(file, currentGenomeIndex);
        ok = ok && ReadValue(file, loaded.bestFitnessEver);
        ok = ok && ReadValue(file, bestScoreEver);
        ok = ok && ReadValue(file, loaded.bestTimeEver);
        ok = ok && ReadValue(file, stagnantGenerations);
        ok = ok && ReadValue(file, loaded.recentBestTrend);
        ok = ok && ReadValue(file, maxStagnantGenerationsEver);
        ok = ok && ReadValue(file, nextNodeId);
        ok = ok && ReadValue(file, nextInnovationNumber);

        ok = ok && ReadFloatVector(file, loaded.fitnessHistory);
        ok = ok && ReadFloatVector(file, loaded.scoreHistory);
        ok = ok && ReadFloatVector(file, loaded.timeHistory);
        ok = ok && ReadIntVector(file, loaded.speciesCountHistory);
        ok = ok && ReadFloatVector(file, loaded.avgHiddenNodeCountHistory);
        ok = ok && ReadFloatVector(file, loaded.avgConnectionCountHistory);

        ok = ok && ReadFloatVector(file, loaded.fitness);
        ok = ok && ReadFloatVector(file, loaded.episodeScores);
        ok = ok && ReadFloatVector(file, loaded.episodeTimes);

        loaded.populationSize = populationSize;
        loaded.generation = generation;
        loaded.currentGenomeIndex = currentGenomeIndex;
        loaded.bestScoreEver = bestScoreEver;
        loaded.stagnantGenerations = stagnantGenerations;
        loaded.maxStagnantGenerationsEver = maxStagnantGenerationsEver;
        loaded.innovationTracker.nextNodeId = nextNodeId;
        loaded.innovationTracker.nextInnovationNumber = nextInnovationNumber;

        ok = ok && ReadGenome(file, loaded.bestGenomeEver);

        if (ok && populationSize > 0)
        {
            loaded.population.reserve(populationSize);
            for (int32_t i = 0; i < populationSize && ok; i++)
            {
                Genome g;
                ok = ReadGenome(file, g);
                if (ok)
                    loaded.population.push_back(std::move(g));
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
