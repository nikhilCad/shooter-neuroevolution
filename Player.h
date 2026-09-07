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
// Moves the player by moveDir (roughly unit length) and turns it to face aimDir,
// clamping it inside the screen. Both directions come from the controlling agent.
void UpdatePlayer(Player &player, Vector2 moveDir, Vector2 aimDir, float deltaTime, int screenWidth, int screenHeight);
void DrawPlayer(const Player &player);
Rectangle GetPlayerRect(const Player &player);
Vector2 GetAimDirection(const Player &player);
Vector2 GetGunTip(const Player &player);
void DamagePlayer(Player &player, int amount);
