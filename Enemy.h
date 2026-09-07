#pragma once
#include "raylib.h"

struct Enemy
{
    Vector2 position; // top-left corner
    float size;
    float speed;
    int health;
    int maxHealth;
    bool active;
};

Enemy SpawnEnemy(int screenWidth, int screenHeight, float size, float speed, int maxHealth);
void UpdateEnemy(Enemy &enemy, float deltaTime, Vector2 targetCenter);
void DrawEnemy(const Enemy &enemy);
Rectangle GetEnemyRect(const Enemy &enemy);
Vector2 GetEnemyCenter(const Enemy &enemy);
void DamageEnemy(Enemy &enemy, int amount);
