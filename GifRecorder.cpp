#include "GifRecorder.h"
#include "raylib.h"
#include "Genome.h"
#include "Simulation.h"
#include "RandomUtil.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <vector>

GifRecordOptions ParseGifRecordOptions(int argc, char **argv)
{
    GifRecordOptions options;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        auto valueAfter = [&](const std::string &prefix)
        { return arg.substr(prefix.size()); };

        if (arg.rfind("--record-gif=", 0) == 0)
            options.genomePath = valueAfter("--record-gif=");
        else if (arg.rfind("--out=", 0) == 0)
            options.outputGifPath = valueAfter("--out=");
        else if (arg.rfind("--seed=", 0) == 0)
            options.seed = strtoull(valueAfter("--seed=").c_str(), nullptr, 10);
        else if (arg.rfind("--max-seconds=", 0) == 0)
            options.maxSeconds = (float)std::atof(valueAfter("--max-seconds=").c_str());
        else if (arg.rfind("--fps=", 0) == 0)
            options.captureFps = std::atoi(valueAfter("--fps=").c_str());
    }
    return options;
}

namespace
{
    // Opens a hidden window with a GL context if one isn't already active —
    // RecordGenomeGif needs one (render textures are a GL resource) whether
    // it's called standalone or as part of a batch, but shouldn't open a
    // second one on top of whatever main.cpp already set up.
    struct ScopedHiddenWindow
    {
        bool ownsWindow;
        ScopedHiddenWindow(int width, int height)
        {
            ownsWindow = !IsWindowReady();
            if (ownsWindow)
            {
                SetConfigFlags(FLAG_WINDOW_HIDDEN);
                InitWindow(width, height, "gif recorder (hidden)");
                // A real recording captures thousands of per-frame PNG
                // save/export INFO lines otherwise — pure noise for a
                // headless batch job.
                SetTraceLogLevel(LOG_WARNING);
            }
        }
        ~ScopedHiddenWindow()
        {
            if (ownsWindow)
                CloseWindow();
        }
    };
}

bool RecordGenomeGif(const GifRecordOptions &options)
{
    Genome genome;
    if (!LoadGenomeFromFile(genome, options.genomePath.c_str()))
    {
        printf("Could not load genome from %s\n", options.genomePath.c_str());
        return false;
    }

    const int screenWidth = 800, screenHeight = 600;
    ScopedHiddenWindow window(screenWidth, screenHeight);

    int captureEveryNSteps = std::max(1, (int)std::lround((1.0 / SIMULATION_FIXED_DT) / options.captureFps));
    std::string framesDir = options.outputGifPath + ".frames";
    system(("mkdir -p '" + framesDir + "'").c_str());

    RenderTexture2D target = LoadRenderTexture(screenWidth, screenHeight);

    SeedRandomEngine(options.seed); // same determinism guarantee as PlayEpisode
    Player player;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    Episode episode;
    ResetEpisode(episode, player, bullets, enemies, screenWidth, screenHeight);

    int frameCount = 0;
    int stepIndex = 0;
    bool episodeEnded = false;
    std::vector<Vector2> killFlashes;
    while (!episodeEnded && episode.time < options.maxSeconds)
    {
        killFlashes.clear();
        episodeEnded = SimulateStep(SIMULATION_FIXED_DT, genome, player, bullets, enemies, episode,
                                    screenWidth, screenHeight, killFlashes);
        stepIndex++;
        if (stepIndex % captureEveryNSteps != 0 && !episodeEnded)
            continue;

        BeginTextureMode(target);
        ClearBackground(RAYWHITE);
        DrawPlayer(player);
        for (const auto &bullet : bullets)
            DrawCircleV(bullet.position, BULLET_RADIUS, DARKGRAY);
        for (const auto &enemy : enemies)
            DrawEnemy(enemy);
        const char *healthText = TextFormat("HP: %d/%d", player.health, player.maxHealth);
        DrawText(healthText, screenWidth - MeasureText(healthText, 20) - 10, 10, 20, MAROON);
        DrawText(TextFormat("Time %.1f  Score %d  Reward %.1f", episode.time, episode.score, episode.reward),
                 10, 10, 20, DARKGRAY);
        EndTextureMode();

        Image image = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&image); // render textures are stored bottom-up in OpenGL
        char framePath[600];
        snprintf(framePath, sizeof(framePath), "%s/frame_%05d.png", framesDir.c_str(), frameCount++);
        ExportImage(image, framePath);
        UnloadImage(image);
    }
    UnloadRenderTexture(target);

    int delayCentiseconds = std::max(2, (int)std::lround(100.0 / options.captureFps));
    std::string command = "magick -delay " + std::to_string(delayCentiseconds) + " -loop 0 '" +
                          framesDir + "/frame_*.png' '" + options.outputGifPath + "'";
    int result = system(command.c_str());
    system(("rm -rf '" + framesDir + "'").c_str()); // clean up temp frames either way

    if (result != 0)
    {
        printf("magick failed (exit %d) for %s — is ImageMagick installed and on PATH?\n",
               result, options.outputGifPath.c_str());
        return false;
    }

    printf("Recorded %s -> %s (%d frames, %.1fs episode%s, seed=%llu)\n",
           options.genomePath.c_str(), options.outputGifPath.c_str(), frameCount, episode.time,
           episodeEnded ? "" : ", capped by --max-seconds", (unsigned long long)options.seed);
    return true;
}

void RecordCheckpointGifs(const std::string &checkpointDir, const std::string &outDir,
                          uint64_t seed, float maxSeconds, int captureFps)
{
    system(("mkdir -p '" + outDir + "'").c_str());

    std::vector<std::string> genomeFiles;
    DIR *dir = opendir(checkpointDir.c_str());
    if (!dir)
    {
        printf("Could not open checkpoint directory %s\n", checkpointDir.c_str());
        return;
    }
    while (dirent *entry = readdir(dir))
    {
        std::string name = entry->d_name;
        if (name.size() > 7 && name.rfind("gen_", 0) == 0 && name.rfind(".genome") == name.size() - 7)
            genomeFiles.push_back(name);
    }
    closedir(dir);
    std::sort(genomeFiles.begin(), genomeFiles.end());

    if (genomeFiles.empty())
    {
        printf("No gen_*.genome files found in %s\n", checkpointDir.c_str());
        return;
    }

    printf("Recording %zu checkpoint(s) from %s -> %s (seed=%llu, max-seconds=%.1f, fps=%d) — pass "
           "--seed=%llu to reproduce this exact batch (same enemy-spawn pattern for every genome).\n",
           genomeFiles.size(), checkpointDir.c_str(), outDir.c_str(), (unsigned long long)seed,
           maxSeconds, captureFps, (unsigned long long)seed);

    const int screenWidth = 800, screenHeight = 600;
    ScopedHiddenWindow window(screenWidth, screenHeight); // shared across every recording in the batch

    for (const std::string &fileName : genomeFiles)
    {
        std::string baseName = fileName.substr(0, fileName.size() - 7); // strip ".genome"
        GifRecordOptions options;
        options.genomePath = checkpointDir + "/" + fileName;
        options.outputGifPath = outDir + "/" + baseName + ".gif";
        options.seed = seed;
        options.maxSeconds = maxSeconds;
        options.captureFps = captureFps;
        RecordGenomeGif(options);
    }
}
