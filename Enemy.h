#pragma once
#include "raylib.h"

struct Enemy
{
    Vector2 position; // top-left corner
    Vector2 velocity; // current world-space velocity, set each UpdateEnemy call
    float size;
    float speed;
    int health;
    int maxHealth;
    bool active;
};

// Spawns at a random angle around playerCenter, at a fixed radius (half the
// screen diagonal) — not at a random point along a fixed screen edge — so
// hiding in a corner doesn't increase the average spawn-to-player travel
// distance and thin out how many enemies are simultaneously in range.
Enemy SpawnEnemy(Vector2 playerCenter, int screenWidth, int screenHeight, float size, float speed, int maxHealth);
void UpdateEnemy(Enemy &enemy, float deltaTime, Vector2 targetCenter);
void DrawEnemy(const Enemy &enemy);
Rectangle GetEnemyRect(const Enemy &enemy);
Vector2 GetEnemyCenter(const Enemy &enemy);
void DamageEnemy(Enemy &enemy, int amount);
