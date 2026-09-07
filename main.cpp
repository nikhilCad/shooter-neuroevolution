#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include <vector>
#include <algorithm>

// make dev

struct Bullet
{
    Vector2 position;
    Vector2 velocity;
    bool active;
};

int main()
{
    // Initialize the window
    const int screenWidth = 800;
    const int screenHeight = 600;
    InitWindow(screenWidth, screenHeight, "raylib - WASD Square Movement");

    Player player = CreatePlayer(
        {(float)screenWidth / 2, (float)screenHeight / 2},
        50.0f,  // size
        300.0f, // speed
        100,    // max health
        20.0f,  // gun length
        10.0f); // gun width

    const float enemyTouchDamage = 10.0f;
    const float enemyTouchCooldown = 0.6f;
    float enemyTouchTimer = 0.0f;

    // Bullet properties
    const float bulletRadius = 4.0f;
    const float bulletSpeed = 500.0f;
    const float shootCooldown = 0.2f;
    const int bulletDamage = 1;
    float shootTimer = 0.0f;
    std::vector<Bullet> bullets;

    // Enemy properties
    const float enemySize = 30.0f;
    const float enemySpeed = 100.0f;
    const int enemyMaxHealth = 3;
    const float enemySpawnInterval = 1.2f;
    float enemySpawnTimer = 0.0f;
    std::vector<Enemy> enemies;

    SetTargetFPS(60);

    // Main game loop
    while (!WindowShouldClose())
    {
        float deltaTime = GetFrameTime();

        UpdatePlayer(player, deltaTime, screenWidth, screenHeight);

        // Shooting (fires toward the direction the player is facing)
        shootTimer -= deltaTime;
        if (IsKeyDown(KEY_SPACE) && shootTimer <= 0.0f)
        {
            Vector2 aim = GetAimDirection(player);
            Bullet bullet;
            bullet.position = GetGunTip(player);
            bullet.velocity = {aim.x * bulletSpeed, aim.y * bulletSpeed};
            bullet.active = true;
            bullets.push_back(bullet);
            shootTimer = shootCooldown;
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
        enemySpawnTimer -= deltaTime;
        if (enemySpawnTimer <= 0.0f)
        {
            enemies.push_back(SpawnEnemy(screenWidth, screenHeight, enemySize, enemySpeed, enemyMaxHealth));
            enemySpawnTimer = enemySpawnInterval;
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
            Rectangle bulletRect = {bullet.position.x - bulletRadius, bullet.position.y - bulletRadius, bulletRadius * 2, bulletRadius * 2};
            for (auto &enemy : enemies)
            {
                if (!enemy.active)
                    continue;
                if (CheckCollisionRecs(bulletRect, GetEnemyRect(enemy)))
                {
                    bullet.active = false;
                    DamageEnemy(enemy, bulletDamage);
                    break;
                }
            }
        }

        // Enemy vs player touch damage
        enemyTouchTimer -= deltaTime;
        for (auto &enemy : enemies)
        {
            if (!enemy.active)
                continue;
            if (enemyTouchTimer <= 0.0f && CheckCollisionRecs(GetPlayerRect(player), GetEnemyRect(enemy)))
            {
                DamagePlayer(player, (int)enemyTouchDamage);
                enemyTouchTimer = enemyTouchCooldown;
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

        // Draw
        BeginDrawing();
        ClearBackground(RAYWHITE);

        DrawPlayer(player);

        for (const auto &bullet : bullets)
            DrawCircleV(bullet.position, bulletRadius, DARKGRAY);

        for (const auto &enemy : enemies)
            DrawEnemy(enemy);

        // Player health, top right
        const char *healthText = TextFormat("HP: %d/%d", player.health, player.maxHealth);
        int healthTextWidth = MeasureText(healthText, 20);
        DrawText(healthText, screenWidth - healthTextWidth - 10, 10, 20, MAROON);

        DrawText("WASD/Arrows to move, mouse to aim, SPACE to shoot", 10, 10, 20, DARKGRAY);
        EndDrawing();
    }

    // Close window and OpenGL context
    CloseWindow();

    return 0;
}
