#include "Checkpoint.h"
#include "Evolution.h"
#include "PlayerAgent.h"
#include "Genome.h"
#include "RandomUtil.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <thread>

namespace
{
    std::vector<int> ParseIntList(const std::string &csv)
    {
        std::vector<int> values;
        std::stringstream stream(csv);
        std::string token;
        while (std::getline(stream, token, ','))
            if (!token.empty())
                values.push_back(std::atoi(token.c_str()));
        return values;
    }
}

CheckpointTrainOptions ParseCheckpointTrainOptions(int argc, char **argv)
{
    CheckpointTrainOptions options;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto valueAfter = [&](const std::string &prefix)
        { return arg.substr(prefix.size()); };

        if (arg.rfind("--checkpoints=", 0) == 0)
            options.checkpointGenerations = ParseIntList(valueAfter("--checkpoints="));
        else if (arg.rfind("--population=", 0) == 0)
            options.populationSize = std::atoi(valueAfter("--population=").c_str());
        else if (arg.rfind("--mutation-rate=", 0) == 0)
            options.mutationRate = (float)std::atof(valueAfter("--mutation-rate=").c_str());
        else if (arg.rfind("--mutation-strength=", 0) == 0)
            options.mutationStrength = (float)std::atof(valueAfter("--mutation-strength=").c_str());
        else if (arg.rfind("--seed=", 0) == 0)
        {
            options.seed = strtoull(valueAfter("--seed=").c_str(), nullptr, 10);
            options.seedSpecified = true;
        }
        else if (arg.rfind("--out-dir=", 0) == 0)
            options.outDir = valueAfter("--out-dir=");
    }

    std::sort(options.checkpointGenerations.begin(), options.checkpointGenerations.end());
    options.checkpointGenerations.erase(
        std::unique(options.checkpointGenerations.begin(), options.checkpointGenerations.end()),
        options.checkpointGenerations.end());

    if (!options.seedSpecified)
    {
        std::random_device rd;
        options.seed = ((uint64_t)rd() << 32) | rd();
    }
    return options;
}

// Trains exactly one population (no sweep across configs/repeats), fanning
// each generation's episodes out across every available core the same way
// ParameterSweep's RunSweepConfig does, so 50000 generations at pop=80
// finishes in a reasonable wall-clock time instead of running single-threaded.
void RunCheckpointTraining(const CheckpointTrainOptions &options)
{
    const int screenWidth = 800, screenHeight = 600;
    if (options.checkpointGenerations.empty())
    {
        printf("No --checkpoints given, nothing to do.\n");
        return;
    }
    int maxGeneration = options.checkpointGenerations.back();

    mkdir(options.outDir.c_str(), 0755);

    printf("Checkpoint training: population=%d mutationRate=%.2f mutationStrength=%.2f maxGeneration=%d\n",
           options.populationSize, options.mutationRate, options.mutationStrength, maxGeneration);
    printf("Checkpoints: [");
    for (size_t i = 0; i < options.checkpointGenerations.size(); i++)
        printf("%s%d", i == 0 ? "" : ",", options.checkpointGenerations[i]);
    printf("] -> %s/gen_<N>.genome\n", options.outDir.c_str());
    printf("Seed: %llu%s — pass --seed=%llu to reproduce this exact run.\n",
           (unsigned long long)options.seed, options.seedSpecified ? "" : " (auto-generated)",
           (unsigned long long)options.seed);

    SeedRandomEngine(options.seed); // deterministic initial population
    Evolution evolution = CreateEvolution(PLAYER_AGENT_INPUT_SIZE, PLAYER_AGENT_OUTPUT_SIZE,
                                           options.populationSize, options.mutationRate, options.mutationStrength);

    unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0)
        hardwareThreads = 1;
    unsigned int threadCount = std::max(1u, std::min(hardwareThreads, (unsigned int)options.populationSize));

    std::vector<EpisodeOutcome> outcomes(options.populationSize);
    size_t nextCheckpointIndex = 0;
    auto start = std::chrono::steady_clock::now();

    // Tracks the single genome (and the exact seed that reproduces its
    // episode) that set the highest score anyone's ever gotten, up through
    // whatever generation a checkpoint falls on — independent of
    // bestGenomeEver, which is chosen by fitness, not raw score. The two
    // routinely diverge: a reckless, high-kill-rate genome can post a huge
    // score while dying too soon to also have the best fitness. Saved
    // alongside (never replacing) the regular fitness-based checkpoint, as
    // gen_score_<N>.genome + gen_score_<N>.seed.
    Genome bestScoreGenomeEver;
    int bestScoreValueEver = -1;
    uint64_t bestScoreSeedEver = 0;
    bool haveBestScoreGenome = false;

    while (evolution.generation <= maxGeneration)
    {
        int generation = evolution.generation; // captured before FinishEpisode can advance it below
        std::atomic<int> nextGenomeIndex{0};
        auto worker = [&]()
        {
            int i;
            while ((i = nextGenomeIndex.fetch_add(1)) < evolution.populationSize)
                outcomes[i] = PlayEpisode(evolution.population[i], screenWidth, screenHeight,
                                          CombineSeed(options.seed, generation, i));
        };

        if (threadCount <= 1)
        {
            worker();
        }
        else
        {
            std::vector<std::thread> workers;
            workers.reserve(threadCount);
            for (unsigned int t = 0; t < threadCount; t++)
                workers.emplace_back(worker);
            for (auto &w : workers)
                w.join();
        }

        // Reseed deterministically before the mutation/crossover/selection
        // decisions FinishEpisode's EvolvePopulation call makes — same reason
        // as ParameterSweep's RunSweepConfig: keeps the outcome independent
        // of thread count/scheduling.
        SeedRandomEngine(CombineSeed(options.seed, generation, -1));
        for (int i = 0; i < evolution.populationSize; i++)
        {
            // Captured before FinishEpisode's EvolvePopulation call (on the
            // last i) overwrites evolution.population with the next
            // generation — evolution.population[i] is still this episode's
            // genome right now.
            if (!haveBestScoreGenome || outcomes[i].score > bestScoreValueEver)
            {
                bestScoreValueEver = outcomes[i].score;
                bestScoreGenomeEver = evolution.population[i];
                bestScoreSeedEver = CombineSeed(options.seed, generation, i);
                haveBestScoreGenome = true;
            }
            FinishEpisode(evolution, outcomes[i].fitness, outcomes[i].score, outcomes[i].time);
        }

        // evolution.generation is now generation+1 (EvolvePopulation ran
        // inside the last FinishEpisode call above) — bestGenomeEver already
        // reflects everything through the end of `generation`.
        while (nextCheckpointIndex < options.checkpointGenerations.size() &&
               options.checkpointGenerations[nextCheckpointIndex] <= generation)
        {
            int g = options.checkpointGenerations[nextCheckpointIndex];
            char path[512];
            snprintf(path, sizeof(path), "%s/gen_%05d.genome", options.outDir.c_str(), g);
            bool saved = SaveGenomeToFile(evolution.bestGenomeEver, path);
            printf("[checkpoint] gen %d -> %s (%s) bestFitnessEver=%.1f bestScoreEver=%d bestTimeEver=%.1f\n",
                   g, path, saved ? "saved" : "FAILED", evolution.bestFitnessEver,
                   evolution.bestScoreEver, evolution.bestTimeEver);

            // Additional, separate snapshot: whichever genome/episode set the
            // highest score so far (not the same genome as bestGenomeEver in
            // general — see the comment where these are tracked above). The
            // seed sidecar is what lets a later replay reproduce that exact
            // episode instead of just seeing "some genome, some random game."
            char scoreGenomePath[512], scoreSeedPath[512];
            snprintf(scoreGenomePath, sizeof(scoreGenomePath), "%s/gen_score_%05d.genome", options.outDir.c_str(), g);
            snprintf(scoreSeedPath, sizeof(scoreSeedPath), "%s/gen_score_%05d.seed", options.outDir.c_str(), g);
            bool scoreGenomeSaved = SaveGenomeToFile(bestScoreGenomeEver, scoreGenomePath);
            bool scoreSeedSaved = false;
            if (FILE *seedFile = fopen(scoreSeedPath, "w"))
            {
                scoreSeedSaved = fprintf(seedFile, "%llu\n", (unsigned long long)bestScoreSeedEver) > 0;
                fclose(seedFile);
            }
            printf("[checkpoint] gen %d -> %s + %s (%s) topScoreEver=%d\n",
                   g, scoreGenomePath, scoreSeedPath, (scoreGenomeSaved && scoreSeedSaved) ? "saved" : "FAILED",
                   bestScoreValueEver);

            nextCheckpointIndex++;
        }

        if (generation % 500 == 0 || generation == maxGeneration)
        {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            // Same stagnationBoost formula as EvolvePopulation (Evolution.cpp)
            // — surfaced here so a long flat stretch of bestFitnessEver is
            // diagnosable live: is mutation already maxed out and just
            // hasn't found a breakthrough yet, or is it still ramping up?
            float stagnationBoost = 1.0f + std::min(evolution.stagnantGenerations / 50.0f, 5.0f);
            int speciesCount = evolution.speciesCountHistory.empty() ? 0 : evolution.speciesCountHistory.back();
            float avgHidden = evolution.avgHiddenNodeCountHistory.empty() ? 0.0f : evolution.avgHiddenNodeCountHistory.back();
            float avgConnections = evolution.avgConnectionCountHistory.empty() ? 0.0f : evolution.avgConnectionCountHistory.back();
            printf("... gen %d/%d bestFitnessEver=%.1f stagnant=%d(boost=%.1fx) species=%d avgHidden=%.1f avgConn=%.1f (%.1fs elapsed)\n",
                   generation, maxGeneration, evolution.bestFitnessEver, evolution.stagnantGenerations,
                   stagnationBoost, speciesCount, avgHidden, avgConnections, elapsed);
        }
    }

    double totalSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf("Checkpoint training complete in %.1fs. %zu genome file(s) in %s/\n",
           totalSeconds, options.checkpointGenerations.size(), options.outDir.c_str());
}
