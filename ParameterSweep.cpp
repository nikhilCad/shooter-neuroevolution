#include "ParameterSweep.h"
#include "HistoryGraph.h"
#include "GenomeVisualizer.h"
#include "PlayerAgent.h"
#include "Genome.h"
#include "RandomUtil.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <random>

struct SweepConfig
{
    int populationSize;
    float mutationRate;
    float mutationStrength;
};

static std::vector<int> ParseIntList(const std::string &csv)
{
    std::vector<int> values;
    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ','))
        if (!token.empty())
            values.push_back(std::atoi(token.c_str()));
    return values;
}

SweepOptions ParseSweepOptions(int argc, char **argv)
{
    SweepOptions options;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto valueAfter = [&](const std::string &prefix)
        { return arg.substr(prefix.size()); };

        if (arg.rfind("--populations=", 0) == 0)
            options.populationSizes = ParseIntList(valueAfter("--populations="));
        else if (arg.rfind("--generations=", 0) == 0)
            options.generationBudget = std::atoi(valueAfter("--generations=").c_str());
        else if (arg.rfind("--mutation-rate=", 0) == 0)
            options.mutationRate = (float)std::atof(valueAfter("--mutation-rate=").c_str());
        else if (arg.rfind("--mutation-strength=", 0) == 0)
            options.mutationStrength = (float)std::atof(valueAfter("--mutation-strength=").c_str());
        else if (arg.rfind("--repeats=", 0) == 0)
            options.repeatCount = std::atoi(valueAfter("--repeats=").c_str());
        else if (arg.rfind("--seed=", 0) == 0)
        {
            options.seed = strtoull(valueAfter("--seed=").c_str(), nullptr, 10);
            options.seedSpecified = true;
        }
    }
    options.repeatCount = std::max(1, options.repeatCount);
    if (!options.seedSpecified)
    {
        std::random_device rd;
        options.seed = ((uint64_t)rd() << 32) | rd();
    }
    return options;
}

static std::vector<SweepConfig> BuildSweepConfigs(const SweepOptions &options)
{
    std::vector<SweepConfig> configs;
    for (int populationSize : options.populationSizes)
        configs.push_back({populationSize, options.mutationRate, options.mutationStrength});
    return configs;
}

static std::string SweepConfigLabel(const SweepConfig &config)
{
    return "pop" + std::to_string(config.populationSize);
}

// Trains one config for generationBudget generations. Every genome in a
// generation is fully independent until fitness is aggregated, so instead of
// playing them one at a time, this fans each generation out across up to
// innerThreadBudget threads (each claiming genome indices off a shared
// counter and playing that genome's episode in its own local game state via
// PlayEpisode), then sequentially replays the results into `evolution`
// through FinishEpisode so its normal per-genome bookkeeping (and the
// EvolvePopulation it triggers once the whole population's in) is unaffected
// by however the work was actually scheduled.
struct SweepRunOutput
{
    Evolution evolution;
    EpisodeOutcome bestOutcome; // the single best episode's full outcome (reward breakdown, accuracy) — an
                                // exact match to evolution.bestFitnessEver, not a replay under new RNG draws
};

// `runSeed` (already unique per config+repeat — see RunConfigsInParallel)
// makes this whole run reproducible independent of thread count/scheduling:
// each genome's episode is seeded from (runSeed, generation, genomeIndex) via
// CombineSeed, so it never matters which physical thread actually played it.
// Mutation/crossover/selection (inside CreateEvolution/EvolvePopulation) all
// run on THIS function's own thread, whose RNG would otherwise inherit
// whatever state PlayEpisode's calls happened to leave it in when
// innerThreadBudget is 1 (i.e. this thread runs PlayEpisode inline instead of
// spawning separate ones) — reseeding it explicitly from (runSeed,
// generation) right before every EvolvePopulation call keeps that
// deterministic too, and independent of innerThreadBudget.
static SweepRunOutput RunSweepConfig(const SweepConfig &config, int generationBudget, int screenWidth, int screenHeight,
                                      unsigned int innerThreadBudget, uint64_t runSeed)
{
    SeedRandomEngine(runSeed); // deterministic initial population
    Evolution evolution = CreateEvolution(PLAYER_AGENT_INPUT_SIZE, PLAYER_AGENT_OUTPUT_SIZE,
                                           config.populationSize,
                                           config.mutationRate, config.mutationStrength);

    unsigned int threadCount = std::max(1u, std::min(innerThreadBudget, (unsigned int)config.populationSize));
    std::vector<EpisodeOutcome> outcomes(config.populationSize);
    EpisodeOutcome bestOutcome{};
    bestOutcome.fitness = -1e9f;

    while (evolution.generation <= generationBudget)
    {
        int generation = evolution.generation; // captured before FinishEpisode can advance it below
        std::atomic<int> nextGenomeIndex{0};
        auto worker = [&]()
        {
            int i;
            while ((i = nextGenomeIndex.fetch_add(1)) < evolution.populationSize)
                outcomes[i] = PlayEpisode(evolution.population[i], screenWidth, screenHeight,
                                          CombineSeed(runSeed, generation, i));
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

        SeedRandomEngine(CombineSeed(runSeed, generation, -1)); // see function comment
        for (int i = 0; i < evolution.populationSize; i++)
        {
            if (outcomes[i].fitness >= bestOutcome.fitness)
                bestOutcome = outcomes[i];
            FinishEpisode(evolution, outcomes[i].fitness, outcomes[i].score, outcomes[i].time);
        }
    }

    return {std::move(evolution), bestOutcome};
}

struct SweepRunResult
{
    Evolution evolution;
    double trainSeconds;
    EpisodeOutcome bestOutcome;
};

// Runs every (config, repeat) pair on a pool of worker threads, pulling the
// next untaken task off a shared counter — flattening repeats into the same
// work queue as configs (rather than looping repeats per-config) keeps big
// and small configs' repeats interleaved, so a handful of slow pop=80 runs
// can't strand idle threads while everything else already finished.
// Nothing here touches raylib's rendering/GL calls — SimulateStep and
// CreateEvolution are plain CPU work — so this stays safe with no window/GL
// context on the worker threads.
static void RunConfigsInParallel(const std::vector<SweepConfig> &configs, int generationBudget, int repeatCount,
                                  int screenWidth, int screenHeight, uint64_t sweepSeed,
                                  std::vector<std::vector<SweepRunResult>> &resultsByConfig)
{
    size_t taskCount = configs.size() * (size_t)repeatCount;
    unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0)
        hardwareThreads = 1;
    unsigned int threadCount = (unsigned int)std::min((size_t)hardwareThreads, taskCount);

    // Each outer worker thread runs one (config, repeat) task to completion
    // before picking up the next, so at most `threadCount` tasks ever train
    // concurrently — split the machine's cores evenly across them for
    // *inner* (per-generation, per-genome) parallelism too. That way a sweep
    // with few tasks (e.g. one population size, one repeat) still uses every
    // core instead of leaving most of them idle, without oversubscribing a
    // sweep that already has enough tasks to saturate every core on its own
    // (in which case this comes out to 1, i.e. no inner parallelism at all).
    unsigned int innerThreadBudget = std::max(1u, hardwareThreads / threadCount);

    printf("Running %zu configs x %d repeats = %zu runs across %u worker threads (%u inner threads each)...\n",
           configs.size(), repeatCount, taskCount, threadCount, innerThreadBudget);

    std::atomic<size_t> nextTask{0};
    std::atomic<size_t> completedCount{0};
    std::mutex printMutex;

    auto worker = [&]()
    {
        size_t task;
        while ((task = nextTask.fetch_add(1)) < taskCount)
        {
            size_t configIndex = task / (size_t)repeatCount;
            size_t repeatIndex = task % (size_t)repeatCount;

            // steady_clock is a monotonic wall-clock timer, so this measures
            // real elapsed time for THIS run specifically, regardless of how
            // many other runs are training concurrently on other threads —
            // a CPU-time clock would double-count across threads and
            // misreport how long any of this actually took.
            uint64_t runSeed = CombineSeed(sweepSeed, (int)configIndex, (int)repeatIndex);
            auto start = std::chrono::steady_clock::now();
            SweepRunOutput output = RunSweepConfig(configs[configIndex], generationBudget, screenWidth, screenHeight,
                                                    innerThreadBudget, runSeed);
            auto end = std::chrono::steady_clock::now();

            SweepRunResult &slot = resultsByConfig[configIndex][repeatIndex];
            slot.evolution = std::move(output.evolution);
            slot.bestOutcome = output.bestOutcome;
            slot.trainSeconds = std::chrono::duration<double>(end - start).count();

            size_t doneSoFar = completedCount.fetch_add(1) + 1;
            std::string label = SweepConfigLabel(configs[configIndex]);
            std::lock_guard<std::mutex> lock(printMutex);
            printf("[%zu/%zu] %s run %zu/%d done in %.1fs bestFitness=%.1f bestScore=%d bestTime=%.1f\n",
                   doneSoFar, taskCount, label.c_str(), repeatIndex + 1, repeatCount, slot.trainSeconds,
                   slot.evolution.bestFitnessEver, slot.evolution.bestScoreEver, slot.evolution.bestTimeEver);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; t++)
        workers.emplace_back(worker);
    for (auto &w : workers)
        w.join();
}

static float Median(std::vector<float> values)
{
    std::sort(values.begin(), values.end());
    size_t n = values.size();
    return (n % 2 == 1) ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0f;
}

static float Mean(const std::vector<float> &values)
{
    float sum = 0.0f;
    for (float v : values)
        sum += v;
    return sum / (float)values.size();
}

// Aggregated view of one config's repeated runs. bestFitnessEver is already
// a running-max over thousands of episodes within a single run, so it's
// inherently prone to capturing one lucky episode — the median across
// repeats resists that the same way; the mean is reported alongside so a
// config whose mean and median diverge a lot (i.e. it's outlier-prone) is
// visible rather than hidden.
struct ConfigSummary
{
    SweepConfig config;
    int generationsCompleted;
    float medianFitness, meanFitness;
    int medianScore;
    float meanScore;
    float medianTime, meanTime;
    double totalTrainSeconds;
    size_t representativeRepeat; // which repeat's graph gets exported
};

static ConfigSummary SummarizeConfig(const SweepConfig &config, const std::vector<SweepRunResult> &repeats)
{
    std::vector<float> fitnesses, scores, times;
    double totalTrainSeconds = 0.0;
    for (const auto &r : repeats)
    {
        fitnesses.push_back(r.evolution.bestFitnessEver);
        scores.push_back((float)r.evolution.bestScoreEver);
        times.push_back(r.evolution.bestTimeEver);
        totalTrainSeconds += r.trainSeconds;
    }

    // The "representative" run for the exported graph: whichever repeat's
    // fitness sits at (or just below, for an even count) the median, so the
    // graph shown actually matches a real run rather than an averaged one.
    std::vector<size_t> order(repeats.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b)
              { return fitnesses[a] < fitnesses[b]; });
    size_t representativeRepeat = order[(order.size() - 1) / 2];

    ConfigSummary summary;
    summary.config = config;
    summary.generationsCompleted = repeats[representativeRepeat].evolution.generation - 1;
    summary.medianFitness = Median(fitnesses);
    summary.meanFitness = Mean(fitnesses);
    summary.medianScore = (int)std::round(Median(scores));
    summary.meanScore = Mean(scores);
    summary.medianTime = Median(times);
    summary.meanTime = Mean(times);
    summary.totalTrainSeconds = totalTrainSeconds;
    summary.representativeRepeat = representativeRepeat;
    return summary;
}

// Tracks, per real input node id, how much the whole sweep's final
// populations actually rely on it: total |weight| and sample count across
// every enabled connection originating from that input, in every genome, in
// every (config, repeat)'s final population. An input whose average comes
// out near zero across the whole sweep is a candidate to drop from
// GetPlayerState — fewer inputs means fewer connections for every genome to
// evaluate, which speeds up every single Activate call.
struct InputUsageStats
{
    std::vector<double> totalAbsWeight;
    std::vector<long long> sampleCount;
};

static InputUsageStats ComputeInputUsageStats(const std::vector<std::vector<SweepRunResult>> &resultsByConfig)
{
    InputUsageStats stats;
    stats.totalAbsWeight.assign(PLAYER_AGENT_INPUT_SIZE, 0.0);
    stats.sampleCount.assign(PLAYER_AGENT_INPUT_SIZE, 0);

    for (const auto &repeats : resultsByConfig)
        for (const auto &result : repeats)
            for (const Genome &genome : result.evolution.population)
                for (const ConnectionGene &c : genome.connections)
                    // inNode < PLAYER_AGENT_INPUT_SIZE excludes the bias node
                    // (id == inputCount) and any hidden node (id beyond that).
                    if (c.enabled && c.inNode >= 0 && c.inNode < PLAYER_AGENT_INPUT_SIZE)
                    {
                        stats.totalAbsWeight[c.inNode] += fabs(c.weight);
                        stats.sampleCount[c.inNode]++;
                    }

    return stats;
}

static double AvgAbsWeight(const InputUsageStats &stats, int index)
{
    return stats.sampleCount[index] > 0 ? stats.totalAbsWeight[index] / (double)stats.sampleCount[index] : 0.0;
}

void RunParameterSweep(const SweepOptions &options)
{
    const int screenWidth = 800;
    const int screenHeight = 600;
    const int generationBudget = options.generationBudget;
    const int repeatCount = options.repeatCount;
    const char *imageDir = "sweep_images";

    std::vector<SweepConfig> configs = BuildSweepConfigs(options);

    printf("Sweep options: populations=[");
    for (size_t i = 0; i < options.populationSizes.size(); i++)
        printf("%s%d", i == 0 ? "" : ",", options.populationSizes[i]);
    printf("] generations=%d repeats=%d mutationRate=%.2f mutationStrength=%.2f (%zu configs)\n",
           options.generationBudget, repeatCount, options.mutationRate, options.mutationStrength,
           configs.size());
    printf("Seed: %llu%s — pass --seed=%llu to reproduce this exact run (independent of core count/scheduling).\n",
           (unsigned long long)options.seed, options.seedSpecified ? "" : " (auto-generated)",
           (unsigned long long)options.seed);

    mkdir(imageDir, 0755);

    // Train every (config, repeat) in parallel first — this is pure CPU work
    // with no GL calls, so it needs no window at all. The window only gets
    // opened afterward, for the (GL-only-safe-on-one-thread) image export.
    auto sweepStart = std::chrono::steady_clock::now();
    std::vector<std::vector<SweepRunResult>> resultsByConfig(configs.size(), std::vector<SweepRunResult>(repeatCount));
    RunConfigsInParallel(configs, generationBudget, repeatCount, screenWidth, screenHeight, options.seed, resultsByConfig);
    double totalSweepSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - sweepStart).count();
    printf("All configs trained in %.1fs total (wall clock)\n", totalSweepSeconds);

    std::vector<ConfigSummary> summaries;
    summaries.reserve(configs.size());
    for (size_t i = 0; i < configs.size(); i++)
        summaries.push_back(SummarizeConfig(configs[i], resultsByConfig[i]));

    // A hidden window is still needed for the offscreen render texture used
    // to export graph images — nothing is ever actually shown.
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(820, (int)GetHistoryPanelsTotalHeight() + 20, "sweep (hidden)");

    const char *resultsPath = "sweep_results.txt";
    FILE *out = fopen(resultsPath, "w");
    if (!out)
    {
        printf("Could not open %s for writing\n", resultsPath);
        CloseWindow();
        return;
    }
    fprintf(out, "%-6s %-8s %-8s %-8s | %-9s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n",
            "pop", "mutRate", "mutStr", "repeats", "gens",
            "medFit", "avgFit", "medScore", "avgScore", "medTime", "avgTime", "totalSec");
    fflush(out);

    struct SummaryRow
    {
        ConfigSummary summary;
        std::string imagePath;
    };
    std::vector<SummaryRow> summaryRows;

    for (size_t i = 0; i < configs.size(); i++)
    {
        const SweepConfig &config = configs[i];
        const ConfigSummary &summary = summaries[i];
        std::string label = SweepConfigLabel(config);

        fprintf(out, "%-6d %-8.2f %-8.2f %-8d | %-9d %-10.1f %-10.1f %-10d %-10.1f %-10.1f %-10.1f %-10.1f\n",
                config.populationSize,
                config.mutationRate, config.mutationStrength, repeatCount,
                summary.generationsCompleted, summary.medianFitness, summary.meanFitness,
                summary.medianScore, summary.meanScore, summary.medianTime, summary.meanTime,
                summary.totalTrainSeconds);
        fflush(out);

        std::string imagePath = std::string(imageDir) + "/" + label + ".png";
        std::string title = "pop=" + std::to_string(config.populationSize) +
                             " (median run of " + std::to_string(repeatCount) +
                             ", " + std::to_string(summary.generationsCompleted) + " generations)";
        const Evolution &representativeEvolution = resultsByConfig[i][summary.representativeRepeat].evolution;
        ExportGenerationHistoryImage(representativeEvolution, title.c_str(), imagePath.c_str());

        summaryRows.push_back({summary, imagePath});
    }

    // The single fittest genome found anywhere in the whole sweep (across
    // every config and repeat), so its network diagram can be embedded in
    // the summary doc alongside the input-usage stats below.
    const Evolution *fittestEvolution = nullptr;
    const EpisodeOutcome *fittestOutcome = nullptr;
    std::string fittestLabel;
    size_t fittestRepeat = 0;
    for (size_t i = 0; i < resultsByConfig.size(); i++)
        for (size_t r = 0; r < resultsByConfig[i].size(); r++)
        {
            const Evolution &evo = resultsByConfig[i][r].evolution;
            if (!fittestEvolution || evo.bestFitnessEver > fittestEvolution->bestFitnessEver)
            {
                fittestEvolution = &evo;
                fittestOutcome = &resultsByConfig[i][r].bestOutcome;
                fittestLabel = SweepConfigLabel(configs[i]);
                fittestRepeat = r;
            }
        }

    std::string fittestImagePath = std::string(imageDir) + "/fittest_genome.png";
    if (fittestEvolution)
    {
        std::string fittestTitle = "Fittest genome overall: " + fittestLabel + " run " +
                                    std::to_string(fittestRepeat + 1) + " (fitness " +
                                    std::to_string((int)fittestEvolution->bestFitnessEver) + ")";
        ExportGenomeVisualizationImage(fittestEvolution->bestGenomeEver, fittestTitle.c_str(), fittestImagePath.c_str());
    }

    InputUsageStats inputUsage = ComputeInputUsageStats(resultsByConfig);
    std::vector<int> inputRanking(PLAYER_AGENT_INPUT_SIZE);
    for (int i = 0; i < PLAYER_AGENT_INPUT_SIZE; i++)
        inputRanking[i] = i;
    std::sort(inputRanking.begin(), inputRanking.end(), [&](int a, int b)
              { return AvgAbsWeight(inputUsage, a) > AvgAbsWeight(inputUsage, b); });

    // Captured into std::strings immediately, one call at a time —
    // PlayerAgentInputLabel returns enemy-feature labels through a shared
    // thread_local buffer, so calling it twice within the same expression
    // (e.g. as two printf arguments) would let the second call overwrite the
    // first before printf ever reads either pointer.
    std::string mostReliedLabel = PlayerAgentInputLabel(inputRanking.front());
    std::string leastReliedLabel = PlayerAgentInputLabel(inputRanking.back());
    printf("Most relied-on input: %s (avg |weight| %.3f). Least relied-on: %s (avg |weight| %.3f).\n",
           mostReliedLabel.c_str(), AvgAbsWeight(inputUsage, inputRanking.front()),
           leastReliedLabel.c_str(), AvgAbsWeight(inputUsage, inputRanking.back()));

    fclose(out);
    CloseWindow();

    const char *summaryDocPath = "RESULTS.md";
    FILE *summaryDoc = fopen(summaryDocPath, "w");
    if (summaryDoc)
    {
        fprintf(summaryDoc, "# Parameter Sweep Results\n\n");
        fprintf(summaryDoc, "See [ARCHITECTURE.md](ARCHITECTURE.md) for the current NEAT design. "
                         "An earlier fixed-topology approach's architecture, sweep results, and graphs are "
                         "preserved in [archive/v1-fixed-topology/](archive/v1-fixed-topology/) for reference.\n\n");
        fprintf(summaryDoc, "## Usage\n\n");
        fprintf(summaryDoc, "```\n");
        fprintf(summaryDoc, "make dev    # build and play the game interactively\n");
        fprintf(summaryDoc, "make sweep  # run this parameter sweep (override with ARGS=\"--populations=20,40,60,80 --generations=4000 --repeats=4 --seed=42\")\n");
        fprintf(summaryDoc, "```\n\n");
        fprintf(summaryDoc, "Each configuration below trained %d times for %d generations each. ", repeatCount, generationBudget);
        fprintf(summaryDoc, "populations=[");
        for (size_t i = 0; i < options.populationSizes.size(); i++)
            fprintf(summaryDoc, "%s%d", i == 0 ? "" : ",", options.populationSizes[i]);
        fprintf(summaryDoc, "] mutationRate=%.2f mutationStrength=%.2f\n\n",
                options.mutationRate, options.mutationStrength);
        fprintf(summaryDoc, "Trained in %.1fs total (wall clock, running all configs/repeats in parallel across CPU threads).\n\n",
                totalSweepSeconds);
        fprintf(summaryDoc, "Seed: `%llu` — every genome's episode and every mutation/crossover/selection decision is "
                             "deterministically derived from this, so re-running with `--seed=%llu` (same code, same "
                             "other flags) reproduces this exact run byte-for-byte, regardless of core count or thread "
                             "scheduling. Different repeats of the same config still get independent draws (the seed "
                             "is combined with the config/repeat index first).\n\n",
                (unsigned long long)options.seed, (unsigned long long)options.seed);
        fprintf(summaryDoc, "Median is the primary ranking column — `bestFitness` is already a running-max over thousands of "
                         "episodes within one run, so a single lucky episode can inflate it; the median across repeats "
                         "resists that better than the mean does. Mean is shown alongside so an outlier-prone config "
                         "(mean and median far apart) is visible rather than hidden.\n\n");
        fprintf(summaryDoc, "| Population | Repeats | Generations | Median Fitness | Mean Fitness | "
                         "Median Score | Mean Score | Median Time | Mean Time | Total Train Time (s) |\n");
        fprintf(summaryDoc, "|---|---|---|---|---|---|---|---|---|---|\n");
        for (const auto &row : summaryRows)
        {
            const ConfigSummary &s = row.summary;
            fprintf(summaryDoc, "| %d | %d | %d | %.1f | %.1f | %d | %.1f | %.1f | %.1f | %.1f |\n",
                    s.config.populationSize, repeatCount,
                    s.generationsCompleted, s.medianFitness, s.meanFitness, s.medianScore, s.meanScore,
                    s.medianTime, s.meanTime, s.totalTrainSeconds);
        }
        fprintf(summaryDoc, "\n");
        for (const auto &row : summaryRows)
        {
            std::string label = SweepConfigLabel(row.summary.config);
            fprintf(summaryDoc, "## %s\n\n![%s](%s)\n\n", label.c_str(), label.c_str(), row.imagePath.c_str());
        }

        fprintf(summaryDoc, "## Input usage\n\n");
        fprintf(summaryDoc, "Average |weight| of enabled connections from each input, across every genome in "
                             "every config/repeat's final population — a rough \"how much does the evolved "
                             "population actually rely on this input\" signal. An input sitting near zero here "
                             "across the whole sweep is a candidate to drop from `GetPlayerState` (fewer inputs "
                             "means fewer connections for every genome to evaluate, speeding up every `Activate` "
                             "call) — but check this holds up across more than one sweep before cutting anything, "
                             "since a single run's population can converge on ignoring a genuinely useful input "
                             "just by chance.\n\n");
        fprintf(summaryDoc, "**Most relied on:** `%s` (avg |weight| %.3f). **Least relied on:** `%s` (avg |weight| %.3f).\n\n",
                mostReliedLabel.c_str(), AvgAbsWeight(inputUsage, inputRanking.front()),
                leastReliedLabel.c_str(), AvgAbsWeight(inputUsage, inputRanking.back()));
        fprintf(summaryDoc, "| Input | Avg \\|weight\\| | Samples |\n");
        fprintf(summaryDoc, "|---|---|---|\n");
        for (int index : inputRanking)
        {
            std::string label = PlayerAgentInputLabel(index); // captured before the next call reuses the shared buffer
            fprintf(summaryDoc, "| %s | %.3f | %lld |\n", label.c_str(), AvgAbsWeight(inputUsage, index),
                    inputUsage.sampleCount[index]);
        }
        fprintf(summaryDoc, "\n");

        if (fittestEvolution)
        {
            fprintf(summaryDoc, "## Fittest genome overall\n\n");
            fprintf(summaryDoc, "`%s` run %zu — fitness %.1f, score %d, time %.1f\n\n",
                    fittestLabel.c_str(), fittestRepeat + 1, fittestEvolution->bestFitnessEver,
                    fittestEvolution->bestScoreEver, fittestEvolution->bestTimeEver);
            fprintf(summaryDoc, "![fittest genome](%s)\n\n", fittestImagePath.c_str());

            fprintf(summaryDoc, "### Diagnostics for that run\n\n");

            if (fittestOutcome)
            {
                float accuracy = fittestOutcome->shotsFired > 0
                                     ? (float)fittestOutcome->hits / (float)fittestOutcome->shotsFired * 100.0f
                                     : 0.0f;
                fprintf(summaryDoc, "**Reward breakdown** (of that fittest episode's %.1f total): "
                                     "survival %.1f, hits %.1f, kills %.1f, touch penalty -%.1f, death penalty -%.1f.\n\n",
                        fittestOutcome->fitness, fittestOutcome->rewardSurvival, fittestOutcome->rewardHits,
                        fittestOutcome->rewardKills, fittestOutcome->rewardTouchPenalty, fittestOutcome->rewardDeathPenalty);
                fprintf(summaryDoc, "**Shot accuracy:** %d/%d (%.1f%%).\n\n",
                        fittestOutcome->hits, fittestOutcome->shotsFired, accuracy);
            }

            if (!fittestEvolution->speciesCountHistory.empty())
            {
                auto [minIt, maxIt] = std::minmax_element(fittestEvolution->speciesCountHistory.begin(),
                                                           fittestEvolution->speciesCountHistory.end());
                fprintf(summaryDoc, "**Species count:** %d at the final generation (ranged %d-%d over the run).\n\n",
                        fittestEvolution->speciesCountHistory.back(), *minIt, *maxIt);
            }
            if (!fittestEvolution->avgHiddenNodeCountHistory.empty())
            {
                fprintf(summaryDoc, "**Population complexity at the final generation:** avg %.1f hidden nodes, "
                                     "avg %.1f enabled connections per genome.\n\n",
                        fittestEvolution->avgHiddenNodeCountHistory.back(),
                        fittestEvolution->avgConnectionCountHistory.back());
            }
            float peakBoost = 1.0f + std::min(fittestEvolution->maxStagnantGenerationsEver / 50.0f, 5.0f);
            fprintf(summaryDoc, "**Deepest stagnation reached:** %d generations without improving "
                                 "(mutation/structural-mutation boost peaked around %.1fx).\n\n",
                    fittestEvolution->maxStagnantGenerationsEver, peakBoost);
        }

        fclose(summaryDoc);
    }

    printf("Sweep complete. Results in %s, graphs in %s/, summary in %s\n", resultsPath, imageDir, summaryDocPath);
}
