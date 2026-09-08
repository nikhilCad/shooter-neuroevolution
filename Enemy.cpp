#include "Enemy.h"
#include "RandomUtil.h"
#include <cmath>

Enemy SpawnEnemy(int screenWidth, int screenHeight, float size, float speed, int maxHealth)
{
    Enemy enemy;
    enemy.size = size;
    enemy.speed = speed;
    enemy.velocity = {0.0f, 0.0f};
    enemy.health = maxHealth;
    enemy.maxHealth = maxHealth;
    enemy.active = true;

    // Spawn at a random position just outside one of the four screen edges
    int side = RandomInt(0, 3); // 0=top, 1=bottom, 2=left, 3=right
    switch (side)
    {
    case 0:
        enemy.position = {(float)RandomInt(0, screenWidth - (int)size), -size};
        break;
    case 1:
        enemy.position = {(float)RandomInt(0, screenWidth - (int)size), (float)screenHeight};
        break;
    case 2:
        enemy.position = {-size, (float)RandomInt(0, screenHeight - (int)size)};
        break;
    default:
        enemy.position = {(float)screenWidth, (float)RandomInt(0, screenHeight - (int)size)};
        break;
    }
    return enemy;
}

void UpdateEnemy(Enemy &enemy, float deltaTime, Vector2 targetCenter)
{
    // Chase whatever position is passed in (the player's current center)
    Vector2 center = {enemy.position.x + enemy.size / 2.0f, enemy.position.y + enemy.size / 2.0f};
    Vector2 direction = {targetCenter.x - center.x, targetCenter.y - center.y};
    float length = sqrtf(direction.x * direction.x + direction.y * direction.y);
    if (length > 0.0001f)
    {
        direction.x /= length;
        direction.y /= length;
    }
    enemy.velocity = {direction.x * enemy.speed, direction.y * enemy.speed};
    enemy.position.x += enemy.velocity.x * deltaTime;
    enemy.position.y += enemy.velocity.y * deltaTime;
}

void DrawEnemy(const Enemy &enemy)
{
    DrawRectangleV(enemy.position, {enemy.size, enemy.size}, GREEN);

    const char *healthText = TextFormat("%d", enemy.health);
    int textWidth = MeasureText(healthText, 14);
    DrawText(healthText,
             (int)(enemy.position.x + enemy.size / 2.0f - textWidth / 2.0f),
             (int)(enemy.position.y - 16),
             14, BLACK);
}

Rectangle GetEnemyRect(const Enemy &enemy)
{
    return {enemy.position.x, enemy.position.y, enemy.size, enemy.size};
}

Vector2 GetEnemyCenter(const Enemy &enemy)
{
    return {enemy.position.x + enemy.size / 2.0f, enemy.position.y + enemy.size / 2.0f};
}

void DamageEnemy(Enemy &enemy, int amount)
{
    enemy.health -= amount;
    if (enemy.health <= 0)
    {
        enemy.health = 0;
        enemy.active = false;
    }
}
