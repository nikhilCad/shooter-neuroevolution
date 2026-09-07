#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include "NeuralNetwork.h"
#include "Evolution.h"
#include "PlayerAgent.h"
#include <vector>
#include <algorithm>

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

// --- Reward shaping for the evolving player brain ---
static const float REWARD_SURVIVE_PER_SECOND = 1.0f;
static const float REWARD_PER_HIT = 2.0f;
static const float REWARD_PER_KILL = 8.0f;
static const float REWARD_DEATH_PENALTY = 30.0f;

// --- Evolution config ---
static const int POPULATION_SIZE = 20;
static const int HIDDEN_SIZE = 12;
static const int ELITE_COUNT = 4;
static const float MUTATION_RATE = 0.15f;
static const float MUTATION_STRENGTH = 0.5f;
static const float EPISODE_MAX_TIME = 20.0f;

struct Episode
{
    float time = 0.0f;
    float reward = 0.0f;
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

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        float deltaTime = GetFrameTime();
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
                        episode.reward += REWARD_PER_KILL;
                    break;
                }
            }
        }

        // Enemy vs player touch damage
        episode.enemyTouchTimer -= deltaTime;
        for (auto &enemy : enemies)
        {
            if (!enemy.active)
                continue;
            if (episode.enemyTouchTimer <= 0.0f && CheckCollisionRecs(GetPlayerRect(player), GetEnemyRect(enemy)))
            {
                DamagePlayer(player, (int)ENEMY_TOUCH_DAMAGE);
                episode.reward -= ENEMY_TOUCH_DAMAGE;
                episode.enemyTouchTimer = ENEMY_TOUCH_COOLDOWN;
            }
        }

        // Remove dead bullets/enemies
        bullets.erase(
            std::remove_if(bullets.begin(), bullets.end(), [](const Bullet &b)
                            { return !b.active; }),
            bullets.end());
        enemies.erase(
            std::remove_if(enemies.begin(), enemies.end(), [](const Enemy &e)
                            { return !e.active; }),
            enemies.end());

        // End the episode on death or timeout, then hand fitness to evolution
        // and move on to the next genome (or the next generation) live.
        bool died = player.health <= 0;
        if (died || episode.time >= EPISODE_MAX_TIME)
        {
            if (died)
                episode.reward -= REWARD_DEATH_PENALTY;

            FinishEpisode(evolution, episode.reward);
            ResetEpisode(episode, player, bullets, enemies, screenWidth, screenHeight);
        }

        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawPlayer(player);

        for (const auto &bullet : bullets)
            DrawCircleV(bullet.position, BULLET_RADIUS, DARKGRAY);

        for (const auto &enemy : enemies)
            DrawEnemy(enemy);

        // Player health, top right
        const char *healthText = TextFormat("HP: %d/%d", player.health, player.maxHealth);
        int healthTextWidth = MeasureText(healthText, 20);
        DrawText(healthText, screenWidth - healthTextWidth - 10, 10, 20, MAROON);

        // Evolution HUD
        DrawText(TextFormat("Generation %d", evolution.generation), 10, 10, 20, DARKGRAY);
        DrawText(TextFormat("Genome %d / %d", evolution.currentGenomeIndex + 1, evolution.populationSize), 10, 34, 20, DARKGRAY);
        DrawText(TextFormat("Time %.1f / %.0f", episode.time, EPISODE_MAX_TIME), 10, 58, 20, DARKGRAY);
        DrawText(TextFormat("Reward %.1f", episode.reward), 10, 82, 20, DARKGRAY);
        DrawText(TextFormat("Best fitness ever %.1f", evolution.bestFitnessEver), 10, 106, 20, DARKGRAY);

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
