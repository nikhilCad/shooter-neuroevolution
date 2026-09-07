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
        else if (arg.rfind("--repeats=", 0) == 0)
            options.repeatCount = std::atoi(valueAfter("--repeats=").c_str());
    }
    options.repeatCount = std::max(1, options.repeatCount);
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

// Runs every (config, repeat) pair on a pool of worker threads, pulling the
// next untaken task off a shared counter — flattening repeats into the same
// work queue as configs (rather than looping repeats per-config) keeps big
// and small configs' repeats interleaved, so a handful of slow pop=80 runs
// can't strand idle threads while everything else already finished.
// Nothing here touches raylib's rendering/GL calls — SimulateStep and
// CreateEvolution are plain CPU work — so this stays safe with no window/GL
// context on the worker threads.
static void RunConfigsInParallel(const std::vector<SweepConfig> &configs, int generationBudget, int repeatCount,
                                  int screenWidth, int screenHeight,
                                  std::vector<std::vector<SweepRunResult>> &resultsByConfig)
{
    size_t taskCount = configs.size() * (size_t)repeatCount;
    unsigned int threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0)
        threadCount = 1;
    threadCount = (unsigned int)std::min((size_t)threadCount, taskCount);
    printf("Running %zu configs x %d repeats = %zu runs across %u worker threads...\n",
           configs.size(), repeatCount, taskCount, threadCount);

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
            auto start = std::chrono::steady_clock::now();
            Evolution evolution = RunSweepConfig(configs[configIndex], generationBudget, screenWidth, screenHeight);
            auto end = std::chrono::steady_clock::now();

            SweepRunResult &slot = resultsByConfig[configIndex][repeatIndex];
            slot.evolution = std::move(evolution);
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
    printf("] hidden=[");
    for (size_t i = 0; i < options.hiddenSizes.size(); i++)
        printf("%s%d", i == 0 ? "" : ",", options.hiddenSizes[i]);
    printf("] generations=%d repeats=%d eliteRatio=%.2f mutationRate=%.2f mutationStrength=%.2f (%zu configs)\n",
           options.generationBudget, repeatCount, options.eliteRatio, options.mutationRate, options.mutationStrength,
           configs.size());

    mkdir(imageDir, 0755);

    // Train every (config, repeat) in parallel first — this is pure CPU work
    // with no GL calls, so it needs no window at all. The window only gets
    // opened afterward, for the (GL-only-safe-on-one-thread) image export.
    auto sweepStart = std::chrono::steady_clock::now();
    std::vector<std::vector<SweepRunResult>> resultsByConfig(configs.size(), std::vector<SweepRunResult>(repeatCount));
    RunConfigsInParallel(configs, generationBudget, repeatCount, screenWidth, screenHeight, resultsByConfig);
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
    fprintf(out, "%-6s %-6s %-6s %-8s %-8s %-8s | %-9s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n",
            "pop", "hidden", "elite", "mutRate", "mutStr", "repeats", "gens",
            "medFit", "avgFit", "medScore", "avgScore", "medTime", "avgTime", "totalSec");
    fflush(out);

    struct ReadmeRow
    {
        ConfigSummary summary;
        std::string imagePath;
    };
    std::vector<ReadmeRow> readmeRows;

    for (size_t i = 0; i < configs.size(); i++)
    {
        const SweepConfig &config = configs[i];
        const ConfigSummary &summary = summaries[i];
        std::string label = SweepConfigLabel(config);

        fprintf(out, "%-6d %-6d %-6d %-8.2f %-8.2f %-8d | %-9d %-10.1f %-10.1f %-10d %-10.1f %-10.1f %-10.1f %-10.1f\n",
                config.populationSize, config.hiddenSize, config.eliteCount,
                config.mutationRate, config.mutationStrength, repeatCount,
                summary.generationsCompleted, summary.medianFitness, summary.meanFitness,
                summary.medianScore, summary.meanScore, summary.medianTime, summary.meanTime,
                summary.totalTrainSeconds);
        fflush(out);

        std::string imagePath = std::string(imageDir) + "/" + label + ".png";
        std::string title = "pop=" + std::to_string(config.populationSize) +
                             " hidden=" + std::to_string(config.hiddenSize) +
                             " elite=" + std::to_string(config.eliteCount) +
                             " (median run of " + std::to_string(repeatCount) +
                             ", " + std::to_string(summary.generationsCompleted) + " generations)";
        const Evolution &representativeEvolution = resultsByConfig[i][summary.representativeRepeat].evolution;
        ExportGenerationHistoryImage(representativeEvolution, title.c_str(), imagePath.c_str());

        readmeRows.push_back({summary, imagePath});
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
        fprintf(readme, "make sweep  # run this parameter sweep (override with ARGS=\"--populations=20,40 --hidden=12,24 --generations=4000 --repeats=4\")\n");
        fprintf(readme, "```\n\n");
        fprintf(readme, "Each configuration below trained %d times for %d generations each. ", repeatCount, generationBudget);
        fprintf(readme, "populations=[");
        for (size_t i = 0; i < options.populationSizes.size(); i++)
            fprintf(readme, "%s%d", i == 0 ? "" : ",", options.populationSizes[i]);
        fprintf(readme, "] hidden=[");
        for (size_t i = 0; i < options.hiddenSizes.size(); i++)
            fprintf(readme, "%s%d", i == 0 ? "" : ",", options.hiddenSizes[i]);
        fprintf(readme, "] eliteRatio=%.2f mutationRate=%.2f mutationStrength=%.2f\n\n",
                options.eliteRatio, options.mutationRate, options.mutationStrength);
        fprintf(readme, "Trained in %.1fs total (wall clock, running all configs/repeats in parallel across CPU threads).\n\n",
                totalSweepSeconds);
        fprintf(readme, "Median is the primary ranking column — `bestFitness` is already a running-max over thousands of "
                         "episodes within one run, so a single lucky episode can inflate it; the median across repeats "
                         "resists that better than the mean does. Mean is shown alongside so an outlier-prone config "
                         "(mean and median far apart) is visible rather than hidden.\n\n");
        fprintf(readme, "| Population | Hidden | Elites | Repeats | Generations | Median Fitness | Mean Fitness | "
                         "Median Score | Mean Score | Median Time | Mean Time | Total Train Time (s) |\n");
        fprintf(readme, "|---|---|---|---|---|---|---|---|---|---|---|---|\n");
        for (const auto &row : readmeRows)
        {
            const ConfigSummary &s = row.summary;
            fprintf(readme, "| %d | %d | %d | %d | %d | %.1f | %.1f | %d | %.1f | %.1f | %.1f | %.1f |\n",
                    s.config.populationSize, s.config.hiddenSize, s.config.eliteCount, repeatCount,
                    s.generationsCompleted, s.medianFitness, s.meanFitness, s.medianScore, s.meanScore,
                    s.medianTime, s.meanTime, s.totalTrainSeconds);
        }
        fprintf(readme, "\n");
        for (const auto &row : readmeRows)
        {
            std::string label = SweepConfigLabel(row.summary.config);
            fprintf(readme, "## %s\n\n![%s](%s)\n\n", label.c_str(), label.c_str(), row.imagePath.c_str());
        }
        fclose(readme);
    }

    printf("Sweep complete. Results in %s, graphs in %s/, summary in %s\n", resultsPath, imageDir, readmePath);
}
