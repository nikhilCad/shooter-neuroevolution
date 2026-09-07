#include "Simulation.h"
#include "PlayerAgent.h"
#include "NeuralNetwork.h"
#include <cmath>
#include <algorithm>

void ResetEpisode(Episode &episode, Player &player, std::vector<Bullet> &bullets,
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

void SimulateStep(float deltaTime, Evolution &evolution, Player &player,
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
