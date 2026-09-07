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
    fprintf(out, "%-6s %-6s %-6s %-8s %-8s | %-9s %-10s %-9s %-10s\n",
            "pop", "hidden", "elite", "mutRate", "mutStr", "gens", "bestFit", "bestScr", "bestTime");
    fflush(out);

    struct ReadmeRow
    {
        SweepConfig config;
        std::string imagePath;
        int generationsCompleted;
        float bestFitnessEver;
        int bestScoreEver;
        float bestTimeEver;
    };
    std::vector<ReadmeRow> readmeRows;

    for (size_t i = 0; i < configs.size(); i++)
    {
        const SweepConfig &config = configs[i];
        std::string label = SweepConfigLabel(config);
        printf("[%zu/%zu] %s (elite=%d) running %d generations... ", i + 1, configs.size(),
               label.c_str(), config.eliteCount, generationBudget);
        fflush(stdout);

        Evolution evolution = RunSweepConfig(config, generationBudget, screenWidth, screenHeight);
        int generationsCompleted = evolution.generation - 1;

        printf("bestFitness=%.1f bestScore=%d bestTime=%.1f\n",
               evolution.bestFitnessEver, evolution.bestScoreEver, evolution.bestTimeEver);

        fprintf(out, "%-6d %-6d %-6d %-8.2f %-8.2f | %-9d %-10.1f %-9d %-10.1f\n",
                config.populationSize, config.hiddenSize, config.eliteCount,
                config.mutationRate, config.mutationStrength,
                generationsCompleted, evolution.bestFitnessEver, evolution.bestScoreEver, evolution.bestTimeEver);
        fflush(out);

        std::string imagePath = std::string(imageDir) + "/" + label + ".png";
        std::string title = "pop=" + std::to_string(config.populationSize) +
                             " hidden=" + std::to_string(config.hiddenSize) +
                             " elite=" + std::to_string(config.eliteCount) +
                             " (" + std::to_string(generationsCompleted) + " generations)";
        ExportGenerationHistoryImage(evolution, title.c_str(), imagePath.c_str());

        readmeRows.push_back({config, imagePath, generationsCompleted,
                               evolution.bestFitnessEver, evolution.bestScoreEver, evolution.bestTimeEver});
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
        fprintf(readme, "| Population | Hidden | Elites | Generations | Best Fitness | Best Score | Best Time |\n");
        fprintf(readme, "|---|---|---|---|---|---|---|\n");
        for (const auto &row : readmeRows)
        {
            fprintf(readme, "| %d | %d | %d | %d | %.1f | %d | %.1f |\n",
                    row.config.populationSize, row.config.hiddenSize, row.config.eliteCount,
                    row.generationsCompleted, row.bestFitnessEver, row.bestScoreEver, row.bestTimeEver);
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
