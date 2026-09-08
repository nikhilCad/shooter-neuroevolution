#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include "Evolution.h"
#include "PlayerAgent.h"
#include "Simulation.h"
#include "HistoryGraph.h"
#include "ParameterSweep.h"
#include <vector>
#include <cstring>

// make dev

// Weights + generation progress + best-ever stats + graph history, so
// training resumes across runs instead of restarting from scratch each time.
// Delete this file to start a fresh run.
static const char *SAVE_FILE_PATH = "save.dat";
static const float AUTO_SAVE_INTERVAL = 5.0f;

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--sweep") == 0)
        {
            RunParameterSweep(ParseSweepOptions(argc, argv));
            return 0;
        }
    }

    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "raylib - ML Player Evolution");

    Evolution evolution;
    if (!LoadEvolution(evolution, SAVE_FILE_PATH))
        evolution = CreateEvolution(PLAYER_AGENT_INPUT_SIZE, HIDDEN_SIZE, PLAYER_AGENT_OUTPUT_SIZE,
                                    POPULATION_SIZE, ELITE_COUNT, MUTATION_RATE, MUTATION_STRENGTH);
    float autoSaveTimer = 0.0f;

    Player player;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    Episode episode;
    ResetEpisode(episode, player, bullets, enemies, screenWidth, screenHeight);

    // Fast-forward control: cycles through these multipliers, running that many
    // fixed-timestep simulation steps per rendered frame so training speeds up
    // without breaking bullet/enemy collisions.
    static const int SPEED_LEVELS[] = {1, 2, 16, 128, 512, 4096, 49152, 150152};
    static const int SPEED_LEVEL_COUNT = sizeof(SPEED_LEVELS) / sizeof(SPEED_LEVELS[0]);
    int speedLevelIndex = 0;
    Rectangle speedButtonRect = {(float)screenWidth - 130.0f, 40.0f, 120.0f, 30.0f};

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), speedButtonRect))
            speedLevelIndex = (speedLevelIndex + 1) % SPEED_LEVEL_COUNT;

        // Autosave every AUTO_SAVE_INTERVAL real (wall-clock) seconds, regardless
        // of the fast-forward speed, so progress survives across launches.
        autoSaveTimer += GetFrameTime();
        if (autoSaveTimer >= AUTO_SAVE_INTERVAL)
        {
            SaveEvolution(evolution, SAVE_FILE_PATH);
            autoSaveTimer = 0.0f;
        }

        // Kills that happen mid-batch (common at high fast-forward speeds,
        // especially point-blank ones whose bullet never survives to be
        // drawn) still get flashed here so every kill is visibly confirmed.
        std::vector<Vector2> killFlashes;
        int stepsThisFrame = SPEED_LEVELS[speedLevelIndex];
        for (int step = 0; step < stepsThisFrame; step++)
            SimulateStep(SIMULATION_FIXED_DT, evolution, player, bullets, enemies, episode, screenWidth, screenHeight, killFlashes);

        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawPlayer(player);

        for (const auto &bullet : bullets)
            DrawCircleV(bullet.position, BULLET_RADIUS, DARKGRAY);

        for (const auto &killPosition : killFlashes)
        {
            DrawCircleLines((int)killPosition.x, (int)killPosition.y, 18.0f, ORANGE);
            DrawCircleLines((int)killPosition.x, (int)killPosition.y, 10.0f, ORANGE);
        }

        for (const auto &enemy : enemies)
            DrawEnemy(enemy);

        // Player health, top right
        const char *healthText = TextFormat("HP: %d/%d", player.health, player.maxHealth);
        int healthTextWidth = MeasureText(healthText, 20);
        DrawText(healthText, screenWidth - healthTextWidth - 10, 10, 20, MAROON);

        // Speed button, top right (click to cycle through the speed levels)
        DrawRectangleRec(speedButtonRect, LIGHTGRAY);
        DrawRectangleLinesEx(speedButtonRect, 2, DARKGRAY);
        const char *speedText = TextFormat("Speed: %dx", SPEED_LEVELS[speedLevelIndex]);
        int speedTextWidth = MeasureText(speedText, 20);
        DrawText(speedText,
                 (int)(speedButtonRect.x + speedButtonRect.width / 2 - speedTextWidth / 2),
                 (int)(speedButtonRect.y + speedButtonRect.height / 2 - 10),
                 20, DARKGRAY);

        // Evolution HUD
        DrawText(TextFormat("Generation %d", evolution.generation), 10, 10, 20, DARKGRAY);
        DrawText(TextFormat("Genome %d / %d", evolution.currentGenomeIndex + 1, evolution.populationSize), 10, 34, 20, DARKGRAY);
        DrawText(TextFormat("Time %.1f", episode.time), 10, 58, 20, DARKGRAY);
        DrawText(TextFormat("Reward %.1f", episode.reward), 10, 82, 20, DARKGRAY);
        DrawText(TextFormat("Best fitness ever %.1f", evolution.bestFitnessEver), 10, 106, 20, DARKGRAY);
        DrawText(TextFormat("Score: %d", episode.score), 10, 130, 20, BLACK);
        DrawText(TextFormat("Best score ever: %d", evolution.bestScoreEver), 10, 154, 20, DARKGRAY);
        DrawText(TextFormat("Best time ever: %.1f", evolution.bestTimeEver), 10, 178, 20, DARKGRAY);

        // Hold Tab to see fitness/score/time plotted across generations
        if (IsKeyDown(KEY_TAB))
        {
            DrawRectangle(0, 0, screenWidth, screenHeight, Fade(RAYWHITE, 0.92f));
            DrawGenerationHistoryPanels(screenWidth, evolution,
                                        TextFormat("Generation history (%d completed)", (int)evolution.fitnessHistory.size()));
        }

        EndDrawing();
    }

    SaveEvolution(evolution, SAVE_FILE_PATH); // catch anything since the last autosave tick
    CloseWindow();

    return 0;
}
