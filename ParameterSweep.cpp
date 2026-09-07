#include "ParameterSweep.h"
#include "HistoryGraph.h"
#include "PlayerAgent.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

struct SweepConfig
{
    int populationSize;
    int hiddenSize;
    int eliteCount;
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
        else if (arg.rfind("--hidden=", 0) == 0)
            options.hiddenSizes = ParseIntList(valueAfter("--hidden="));
        else if (arg.rfind("--generations=", 0) == 0)
            options.generationBudget = std::atoi(valueAfter("--generations=").c_str());
        else if (arg.rfind("--elite-ratio=", 0) == 0)
            options.eliteRatio = (float)std::atof(valueAfter("--elite-ratio=").c_str());
        else if (arg.rfind("--mutation-rate=", 0) == 0)
            options.mutationRate = (float)std::atof(valueAfter("--mutation-rate=").c_str());
        else if (arg.rfind("--mutation-strength=", 0) == 0)
            options.mutationStrength = (float)std::atof(valueAfter("--mutation-strength=").c_str());
    }
    return options;
}

static std::vector<SweepConfig> BuildSweepConfigs(const SweepOptions &options)
{
    std::vector<SweepConfig> configs;
    for (int populationSize : options.populationSizes)
    {
        int eliteCount = std::max(1, (int)std::round(populationSize * options.eliteRatio));
        for (int hiddenSize : options.hiddenSizes)
            configs.push_back({populationSize, hiddenSize, eliteCount, options.mutationRate, options.mutationStrength});
    }
    return configs;
}

static std::string SweepConfigLabel(const SweepConfig &config)
{
    return "pop" + std::to_string(config.populationSize) + "_hidden" + std::to_string(config.hiddenSize);
}

static Evolution RunSweepConfig(const SweepConfig &config, int generationBudget, int screenWidth, int screenHeight)
{
    Evolution evolution = CreateEvolution(PLAYER_AGENT_INPUT_SIZE, config.hiddenSize, PLAYER_AGENT_OUTPUT_SIZE,
                                           config.populationSize, config.eliteCount,
                                           config.mutationRate, config.mutationStrength);

    Player player;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    Episode episode;
    ResetEpisode(episode, player, bullets, enemies, screenWidth, screenHeight);

    while (evolution.generation <= generationBudget)
    {
        std::vector<Vector2> killFlashes; // unused outside rendering; discarded here
        SimulateStep(SIMULATION_FIXED_DT, evolution, player, bullets, enemies, episode, screenWidth, screenHeight, killFlashes);
    }

    return evolution;
}

struct SweepRunResult
{
    Evolution evolution;
    double trainSeconds;
};

// Runs every config's training on a pool of worker threads (dynamically
// pulling the next untaken config off a shared counter, since bigger
// populations take far longer than smaller ones and static split-in-half
// assignment would leave threads idle). Nothing here touches raylib's
// rendering/GL calls — SimulateStep and CreateEvolution are plain CPU work —
// so this stays safe without a window/GL context on the worker threads.
static void RunConfigsInParallel(const std::vector<SweepConfig> &configs, int generationBudget,
                                  int screenWidth, int screenHeight, std::vector<SweepRunResult> &results)
{
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0)
        threadCount = 1;
    threadCount = (unsigned int)std::min((size_t)threadCount, configs.size());
    printf("Running %zu configs across %u worker threads...\n", configs.size(), threadCount);

    std::atomic<size_t> nextIndex{0};
    std::atomic<size_t> completedCount{0};
    std::mutex printMutex;

    auto worker = [&]()
    {
        size_t i;
        while ((i = nextIndex.fetch_add(1)) < configs.size())
        {
            // steady_clock is a monotonic wall-clock timer, so this measures
            // real elapsed time for THIS config specifically, regardless of
            // how many other configs are training concurrently on other
            // threads — a CPU-time clock would double-count across threads
            // and misreport how long the batch actually took.
            auto start = std::chrono::steady_clock::now();
            Evolution evolution = RunSweepConfig(configs[i], generationBudget, screenWidth, screenHeight);
            auto end = std::chrono::steady_clock::now();

            results[i].evolution = std::move(evolution);
            results[i].trainSeconds = std::chrono::duration<double>(end - start).count();

            size_t doneSoFar = completedCount.fetch_add(1) + 1;
            std::string label = SweepConfigLabel(configs[i]);
            std::lock_guard<std::mutex> lock(printMutex);
            printf("[%zu/%zu] %s done in %.1fs bestFitness=%.1f bestScore=%d bestTime=%.1f\n",
                   doneSoFar, configs.size(), label.c_str(), results[i].trainSeconds,
                   results[i].evolution.bestFitnessEver, results[i].evolution.bestScoreEver, results[i].evolution.bestTimeEver);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (unsigned int t = 0; t < threadCount; t++)
        workers.emplace_back(worker);
    for (auto &w : workers)
        w.join();
}

void RunParameterSweep(const SweepOptions &options)
{
    const int screenWidth = 800;
    const int screenHeight = 600;
    const int generationBudget = options.generationBudget;
    const char *imageDir = "sweep_images";

    std::vector<SweepConfig> configs = BuildSweepConfigs(options);

    printf("Sweep options: populations=[");
    for (size_t i = 0; i < options.populationSizes.size(); i++)
        printf("%s%d", i == 0 ? "" : ",", options.populationSizes[i]);
    printf("] hidden=[");
    for (size_t i = 0; i < options.hiddenSizes.size(); i++)
        printf("%s%d", i == 0 ? "" : ",", options.hiddenSizes[i]);
    printf("] generations=%d eliteRatio=%.2f mutationRate=%.2f mutationStrength=%.2f (%zu configs)\n",
           options.generationBudget, options.eliteRatio, options.mutationRate, options.mutationStrength, configs.size());

    mkdir(imageDir, 0755);

    // Train every config in parallel first — this is pure CPU work with no
    // GL calls, so it needs no window at all. The window only gets opened
    // afterward, for the (GL-only-safe-on-one-thread) image export pass.
    auto sweepStart = std::chrono::steady_clock::now();
    std::vector<SweepRunResult> results(configs.size());
    RunConfigsInParallel(configs, generationBudget, screenWidth, screenHeight, results);
    double totalSweepSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - sweepStart).count();
    printf("All configs trained in %.1fs total (wall clock)\n", totalSweepSeconds);

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
    fprintf(out, "%-6s %-6s %-6s %-8s %-8s | %-9s %-10s %-9s %-10s %-10s\n",
            "pop", "hidden", "elite", "mutRate", "mutStr", "gens", "bestFit", "bestScr", "bestTime", "trainSec");
    fflush(out);

    struct ReadmeRow
    {
        SweepConfig config;
        std::string imagePath;
        int generationsCompleted;
        float bestFitnessEver;
        int bestScoreEver;
        float bestTimeEver;
        double trainSeconds;
    };
    std::vector<ReadmeRow> readmeRows;

    for (size_t i = 0; i < configs.size(); i++)
    {
        const SweepConfig &config = configs[i];
        const Evolution &evolution = results[i].evolution;
        double trainSeconds = results[i].trainSeconds;
        std::string label = SweepConfigLabel(config);
        int generationsCompleted = evolution.generation - 1;

        fprintf(out, "%-6d %-6d %-6d %-8.2f %-8.2f | %-9d %-10.1f %-9d %-10.1f %-10.1f\n",
                config.populationSize, config.hiddenSize, config.eliteCount,
                config.mutationRate, config.mutationStrength,
                generationsCompleted, evolution.bestFitnessEver, evolution.bestScoreEver, evolution.bestTimeEver,
                trainSeconds);
        fflush(out);

        std::string imagePath = std::string(imageDir) + "/" + label + ".png";
        std::string title = "pop=" + std::to_string(config.populationSize) +
                             " hidden=" + std::to_string(config.hiddenSize) +
                             " elite=" + std::to_string(config.eliteCount) +
                             " (" + std::to_string(generationsCompleted) + " generations)";
        ExportGenerationHistoryImage(evolution, title.c_str(), imagePath.c_str());

        readmeRows.push_back({config, imagePath, generationsCompleted,
                               evolution.bestFitnessEver, evolution.bestScoreEver, evolution.bestTimeEver, trainSeconds});
    }

    fclose(out);
    CloseWindow();

    const char *readmePath = "README.md";
    FILE *readme = fopen(readmePath, "w");
    if (readme)
    {
        fprintf(readme, "# Parameter Sweep Results\n\n");
        fprintf(readme, "## Usage\n\n");
        fprintf(readme, "```\n");
        fprintf(readme, "make dev    # build and play the game interactively\n");
        fprintf(readme, "make sweep  # run this parameter sweep (override with ARGS=\"--populations=20,40 --hidden=12,24 --generations=4000\")\n");
        fprintf(readme, "```\n\n");
        fprintf(readme, "Each configuration below trained for %d generations. ", generationBudget);
        fprintf(readme, "populations=[");
        for (size_t i = 0; i < options.populationSizes.size(); i++)
            fprintf(readme, "%s%d", i == 0 ? "" : ",", options.populationSizes[i]);
        fprintf(readme, "] hidden=[");
        for (size_t i = 0; i < options.hiddenSizes.size(); i++)
            fprintf(readme, "%s%d", i == 0 ? "" : ",", options.hiddenSizes[i]);
        fprintf(readme, "] eliteRatio=%.2f mutationRate=%.2f mutationStrength=%.2f\n\n",
                options.eliteRatio, options.mutationRate, options.mutationStrength);
        fprintf(readme, "Trained in %.1fs total (wall clock, running configs in parallel across CPU threads).\n\n",
                totalSweepSeconds);
        fprintf(readme, "| Population | Hidden | Elites | Generations | Best Fitness | Best Score | Best Time | Train Time (s) |\n");
        fprintf(readme, "|---|---|---|---|---|---|---|---|\n");
        for (const auto &row : readmeRows)
        {
            fprintf(readme, "| %d | %d | %d | %d | %.1f | %d | %.1f | %.1f |\n",
                    row.config.populationSize, row.config.hiddenSize, row.config.eliteCount,
                    row.generationsCompleted, row.bestFitnessEver, row.bestScoreEver, row.bestTimeEver,
                    row.trainSeconds);
        }
        fprintf(readme, "\n");
        for (const auto &row : readmeRows)
        {
            std::string label = SweepConfigLabel(row.config);
            fprintf(readme, "## %s\n\n![%s](%s)\n\n", label.c_str(), label.c_str(), row.imagePath.c_str());
        }
        fclose(readme);
    }

    printf("Sweep complete. Results in %s, graphs in %s/, summary in %s\n", resultsPath, imageDir, readmePath);
}
