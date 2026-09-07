#pragma once
#include "raylib.h"

struct Player
{
    Vector2 center;
    float size;
    float speed;
    float rotation; // degrees, faces the mouse cursor
    float gunLength;
    float gunWidth;
    int health;
    int maxHealth;
};

Player CreatePlayer(Vector2 startCenter, float size, float speed, int maxHealth, float gunLength, float gunWidth);
void UpdatePlayer(Player &player, float deltaTime, int screenWidth, int screenHeight);
void DrawPlayer(const Player &player);
Rectangle GetPlayerRect(const Player &player);
Vector2 GetAimDirection(const Player &player);
Vector2 GetGunTip(const Player &player);
void DamagePlayer(Player &player, int amount);
