#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include "NeuralNetwork.h"
#include "Evolution.h"
#include "PlayerAgent.h"
#include <vector>
#include <algorithm>
#include <cmath>

// make dev

struct Bullet
{
    Vector2 position;
    Vector2 velocity;
    bool active;
};

// --- Player config ---
static const float PLAYER_SIZE = 50.0f;
static const float PLAYER_SPEED = 300.0f;
static const int PLAYER_MAX_HEALTH = 100;
static const float GUN_LENGTH = 20.0f;
static const float GUN_WIDTH = 10.0f;

// --- Bullet config ---
static const float BULLET_RADIUS = 4.0f;
static const float BULLET_SPEED = 500.0f;
static const float SHOOT_COOLDOWN = 0.2f;
static const int BULLET_DAMAGE = 1;

// --- Enemy config ---
static const float ENEMY_SIZE = 30.0f;
static const float ENEMY_SPEED = 100.0f;
static const int ENEMY_MAX_HEALTH = 3;
static const float ENEMY_SPAWN_INTERVAL = 1.2f;
static const float ENEMY_TOUCH_DAMAGE = 10.0f;
static const float ENEMY_TOUCH_COOLDOWN = 0.6f;

// --- Score (a human-readable score, separate from the ML reward signal) ---
static const int SCORE_PER_KILL = 100;

// --- Reward shaping for the evolving player brain ---
// Score (kills) is the dominant signal; survival time is only a small trickle
// so a passive agent that never kills anything can't out-earn an aggressive
// one just by running out the clock (there's no episode time limit anymore).
static const float REWARD_SURVIVE_PER_SECOND = 0.05f;
static const float REWARD_PER_HIT = 2.0f;
static const float REWARD_PER_KILL = 20.0f;
static const float REWARD_DEATH_PENALTY = 30.0f;

// --- Evolution config ---
static const int POPULATION_SIZE = 20;
static const int HIDDEN_SIZE = 12;
static const int ELITE_COUNT = 4;
static const float MUTATION_RATE = 0.15f;
static const float MUTATION_STRENGTH = 0.5f;

struct Episode
{
    float time = 0.0f;
    float reward = 0.0f;
    int score = 0;
    float shootTimer = 0.0f;
    float enemySpawnTimer = 0.0f;
    float enemyTouchTimer = 0.0f;
};

static void ResetEpisode(Episode &episode, Player &player, std::vector<Bullet> &bullets,
                         std::vector<Enemy> &enemies, int screenWidth, int screenHeight)
{
    player = CreatePlayer({(float)screenWidth / 2, (float)screenHeight / 2},
                          PLAYER_SIZE, PLAYER_SPEED, PLAYER_MAX_HEALTH, GUN_LENGTH, GUN_WIDTH);
    bullets.clear();
    enemies.clear();
    episode = Episode{};
}

// Pushes two overlapping circular bodies apart along the line between their
// centers, splitting the overlap evenly between them.
static void SeparateCircles(Vector2 &centerA, float radiusA, Vector2 &centerB, float radiusB)
{
    Vector2 diff = {centerB.x - centerA.x, centerB.y - centerA.y};
    float dist = sqrtf(diff.x * diff.x + diff.y * diff.y);
    float minDist = radiusA + radiusB;
    if (dist >= minDist)
        return;

    Vector2 dir = dist > 0.0001f ? Vector2{diff.x / dist, diff.y / dist} : Vector2{1.0f, 0.0f};
    float pushEach = (minDist - dist) / 2.0f;
    centerA.x -= dir.x * pushEach;
    centerA.y -= dir.y * pushEach;
    centerB.x += dir.x * pushEach;
    centerB.y += dir.y * pushEach;
}

// Keeps enemies from stacking on top of one another
static void ResolveEnemyEnemyCollisions(std::vector<Enemy> &enemies)
{
    for (size_t i = 0; i < enemies.size(); i++)
    {
        if (!enemies[i].active)
            continue;
        for (size_t j = i + 1; j < enemies.size(); j++)
        {
            if (!enemies[j].active)
                continue;

            Vector2 centerI = GetEnemyCenter(enemies[i]);
            Vector2 centerJ = GetEnemyCenter(enemies[j]);
            SeparateCircles(centerI, enemies[i].size / 2.0f, centerJ, enemies[j].size / 2.0f);
            enemies[i].position = {centerI.x - enemies[i].size / 2.0f, centerI.y - enemies[i].size / 2.0f};
            enemies[j].position = {centerJ.x - enemies[j].size / 2.0f, centerJ.y - enemies[j].size / 2.0f};
        }
    }
}

// Advances the simulation by one fixed timestep. Called multiple times per
// rendered frame when fast-forwarding, so game logic stays deterministic
// regardless of how many steps are packed into a real frame.
static void SimulateStep(float deltaTime, Evolution &evolution, Player &player,
                         std::vector<Bullet> &bullets, std::vector<Enemy> &enemies,
                         Episode &episode, int screenWidth, int screenHeight,
                         std::vector<Vector2> &killFlashes)
{
    episode.time += deltaTime;
    episode.reward += REWARD_SURVIVE_PER_SECOND * deltaTime;

    // The current generation's genome controls the player this episode
    const NeuralNetwork &brain = CurrentGenome(evolution);
    PlayerAction action = DecidePlayerAction(brain, player, enemies, screenWidth, screenHeight);

    UpdatePlayer(player, action.move, action.aim, deltaTime, screenWidth, screenHeight);

    // Shooting
    episode.shootTimer -= deltaTime;
    if (action.shoot && episode.shootTimer <= 0.0f)
    {
        Vector2 aim = GetAimDirection(player);
        Bullet bullet;
        bullet.position = GetGunTip(player);
        bullet.velocity = {aim.x * BULLET_SPEED, aim.y * BULLET_SPEED};
        bullet.active = true;
        bullets.push_back(bullet);
        episode.shootTimer = SHOOT_COOLDOWN;
    }

    // Update bullets
    for (auto &bullet : bullets)
    {
        if (!bullet.active)
            continue;
        bullet.position.x += bullet.velocity.x * deltaTime;
        bullet.position.y += bullet.velocity.y * deltaTime;
        if (bullet.position.x < 0 || bullet.position.x > screenWidth ||
            bullet.position.y < 0 || bullet.position.y > screenHeight)
            bullet.active = false;
    }

    // Spawn enemies at a random location outside the screen
    episode.enemySpawnTimer -= deltaTime;
    if (episode.enemySpawnTimer <= 0.0f)
    {
        enemies.push_back(SpawnEnemy(screenWidth, screenHeight, ENEMY_SIZE, ENEMY_SPEED, ENEMY_MAX_HEALTH));
        episode.enemySpawnTimer = ENEMY_SPAWN_INTERVAL;
    }

    // Update enemies: always chase the player's current position
    for (auto &enemy : enemies)
    {
        if (!enemy.active)
            continue;
        UpdateEnemy(enemy, deltaTime, player.center);
    }

    // Enemies push each other apart instead of overlapping
    ResolveEnemyEnemyCollisions(enemies);

    // Bullet vs enemy collisions
    for (auto &bullet : bullets)
    {
        if (!bullet.active)
            continue;
        Rectangle bulletRect = {bullet.position.x - BULLET_RADIUS, bullet.position.y - BULLET_RADIUS,
                                BULLET_RADIUS * 2, BULLET_RADIUS * 2};
        for (auto &enemy : enemies)
        {
            if (!enemy.active)
                continue;
            if (CheckCollisionRecs(bulletRect, GetEnemyRect(enemy)))
            {
                bullet.active = false;
                DamageEnemy(enemy, BULLET_DAMAGE);
                episode.reward += REWARD_PER_HIT;
                if (!enemy.active)
                {
                    episode.reward += REWARD_PER_KILL;
                    episode.score += SCORE_PER_KILL;
                    // Point-blank kills can spawn, travel, and resolve within a
                    // single fast-forwarded batch of steps and never get drawn
                    // as a visible bullet — flash the kill so it's always seen.
                    killFlashes.push_back(GetEnemyCenter(enemy));
                }
                break;
            }
        }
    }

    // Enemy vs player: push both bodies apart so they stop overlapping, and
    // damage the player on contact (rate-limited by a cooldown)
    episode.enemyTouchTimer -= deltaTime;
    for (auto &enemy : enemies)
    {
        if (!enemy.active)
            continue;

        Vector2 enemyCenter = GetEnemyCenter(enemy);
        Vector2 playerCenter = player.center;
        float distBefore = sqrtf((enemyCenter.x - playerCenter.x) * (enemyCenter.x - playerCenter.x) +
                                 (enemyCenter.y - playerCenter.y) * (enemyCenter.y - playerCenter.y));
        bool touching = distBefore < player.size / 2.0f + enemy.size / 2.0f;

        SeparateCircles(playerCenter, player.size / 2.0f, enemyCenter, enemy.size / 2.0f);
        player.center = playerCenter;
        enemy.position = {enemyCenter.x - enemy.size / 2.0f, enemyCenter.y - enemy.size / 2.0f};

        if (touching && episode.enemyTouchTimer <= 0.0f)
        {
            DamagePlayer(player, (int)ENEMY_TOUCH_DAMAGE);
            episode.reward -= ENEMY_TOUCH_DAMAGE;
            episode.enemyTouchTimer = ENEMY_TOUCH_COOLDOWN;
        }
    }
    ClampPlayerToScreen(player, screenWidth, screenHeight);

    // Remove dead bullets/enemies
    bullets.erase(
        std::remove_if(bullets.begin(), bullets.end(), [](const Bullet &b)
                       { return !b.active; }),
        bullets.end());
    enemies.erase(
        std::remove_if(enemies.begin(), enemies.end(), [](const Enemy &e)
                       { return !e.active; }),
        enemies.end());

    // End the episode only when the player dies, then hand fitness to
    // evolution and move on to the next genome (or the next generation) live.
    if (player.health <= 0)
    {
        episode.reward -= REWARD_DEATH_PENALTY;
        FinishEpisode(evolution, episode.reward, episode.score, episode.time);
        ResetEpisode(episode, player, bullets, enemies, screenWidth, screenHeight);
    }
}

// Draws one line graph of a per-generation metric inside `area`, auto-scaling
// the y-axis to the history's own min/max, plus a second line tracking the
// running best-so-far value in a different color.
static void DrawHistoryGraph(Rectangle area, const std::vector<float> &history, const char *label,
                             Color color, Color maxColor)
{
    DrawRectangleRec(area, Fade(BLACK, 0.05f));
    DrawRectangleLinesEx(area, 1, DARKGRAY);
    DrawText(label, (int)area.x + 6, (int)area.y + 4, 16, BLACK);
    DrawText("value", (int)area.x + area.width - 90, (int)area.y + 4, 14, color);
    DrawText("best so far", (int)area.x + area.width - 40, (int)area.y + 4, 14, maxColor);

    if (history.size() < 2)
    {
        DrawText("Not enough generations yet", (int)area.x + 6, (int)(area.y + area.height / 2), 14, GRAY);
        return;
    }

    std::vector<float> runningMax(history.size());
    runningMax[0] = history[0];
    for (size_t i = 1; i < history.size(); i++)
        runningMax[i] = std::max(runningMax[i - 1], history[i]);

    float minV = history[0];
    float maxV = history[0];
    for (float v : history)
    {
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }
    if (maxV - minV < 0.001f)
        maxV = minV + 1.0f; // avoid a flat divide-by-zero range

    float graphTop = area.y + 24.0f;
    float graphHeight = area.height - 30.0f;

    auto toPoints = [&](const std::vector<float> &values)
    {
        std::vector<Vector2> points(values.size());
        for (size_t i = 0; i < values.size(); i++)
        {
            float t = (float)i / (float)(values.size() - 1);
            float normalized = (values[i] - minV) / (maxV - minV);
            points[i].x = area.x + t * area.width;
            points[i].y = graphTop + graphHeight - normalized * graphHeight;
        }
        return points;
    };

    std::vector<Vector2> maxPoints = toPoints(runningMax);
    DrawLineStrip(maxPoints.data(), (int)maxPoints.size(), maxColor);

    std::vector<Vector2> valuePoints = toPoints(history);
    DrawLineStrip(valuePoints.data(), (int)valuePoints.size(), color);

    DrawText(TextFormat("max %.1f", maxV), (int)area.x + 6, (int)graphTop - 2, 12, GRAY);
    DrawText(TextFormat("min %.1f", minV), (int)area.x + 6, (int)(graphTop + graphHeight - 10), 12, GRAY);
}

int main()
{
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "raylib - ML Player Evolution");

    Evolution evolution = CreateEvolution(PLAYER_AGENT_INPUT_SIZE, HIDDEN_SIZE, PLAYER_AGENT_OUTPUT_SIZE,
                                          POPULATION_SIZE, ELITE_COUNT, MUTATION_RATE, MUTATION_STRENGTH);

    Player player;
    std::vector<Bullet> bullets;
    std::vector<Enemy> enemies;
    Episode episode;
    ResetEpisode(episode, player, bullets, enemies, screenWidth, screenHeight);

    // Fast-forward control: cycles through these multipliers, running that many
    // fixed-timestep simulation steps per rendered frame so training speeds up
    // without breaking bullet/enemy collisions.
    static const int SPEED_LEVELS[] = {1, 2, 16, 128, 512, 4096, 49152};
    static const int SPEED_LEVEL_COUNT = sizeof(SPEED_LEVELS) / sizeof(SPEED_LEVELS[0]);
    static const float FIXED_DT = 1.0f / 60.0f;
    int speedLevelIndex = 0;
    Rectangle speedButtonRect = {(float)screenWidth - 130.0f, 40.0f, 120.0f, 30.0f};

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), speedButtonRect))
            speedLevelIndex = (speedLevelIndex + 1) % SPEED_LEVEL_COUNT;

        // Kills that happen mid-batch (common at high fast-forward speeds,
        // especially point-blank ones whose bullet never survives to be
        // drawn) still get flashed here so every kill is visibly confirmed.
        std::vector<Vector2> killFlashes;
        int stepsThisFrame = SPEED_LEVELS[speedLevelIndex];
        for (int step = 0; step < stepsThisFrame; step++)
            SimulateStep(FIXED_DT, evolution, player, bullets, enemies, episode, screenWidth, screenHeight, killFlashes);

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

            const float panelMargin = 20.0f;
            const float panelWidth = screenWidth - panelMargin * 2;
            const float panelHeight = 150.0f;
            const float panelGap = 20.0f;

            DrawText(TextFormat("Generation history (%d completed)", (int)evolution.fitnessHistory.size()),
                     (int)panelMargin, 10, 20, BLACK);

            Rectangle fitnessArea = {panelMargin, 40.0f, panelWidth, panelHeight};
            Rectangle scoreArea = {panelMargin, fitnessArea.y + panelHeight + panelGap, panelWidth, panelHeight};
            Rectangle timeArea = {panelMargin, scoreArea.y + panelHeight + panelGap, panelWidth, panelHeight};

            DrawHistoryGraph(fitnessArea, evolution.fitnessHistory, "Fitness per generation", RED, GOLD);
            DrawHistoryGraph(scoreArea, evolution.scoreHistory, "Score per generation", BLUE, GOLD);
            DrawHistoryGraph(timeArea, evolution.timeHistory, "Time survived per generation", DARKGREEN, GOLD);
        }

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
