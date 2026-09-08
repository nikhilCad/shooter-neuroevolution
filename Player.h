#pragma once
#include "raylib.h"

struct Player
{
    Vector2 center;
    Vector2 velocity; // eased toward the commanded direction, so the agent
                      // flipping its move output frame to frame doesn't
                      // snap the body back and forth instantly
    float size;
    float speed;
    float acceleration; // max change in velocity per second
    float rotation; // degrees, faces the mouse cursor
    float turnSpeedDegPerSec; // max rotation rate, so aim turns instead of snapping
    float gunLength;
    float gunWidth;
    int health;
    int maxHealth;
};

Player CreatePlayer(Vector2 startCenter, float size, float speed, int maxHealth, float gunLength, float gunWidth,
                    float turnSpeedDegPerSec, float acceleration);
// Eases the player's velocity toward moveDir * speed (rather than setting
// position from it directly) and turns the player toward aimDir at up to
// turnSpeedDegPerSec, clamping it inside the screen. Both directions come
// from the controlling agent.
void UpdatePlayer(Player &player, Vector2 moveDir, Vector2 aimDir, float deltaTime, int screenWidth, int screenHeight);
// Keeps the player's body inside the screen bounds. Exposed so callers can
// re-clamp after nudging the player during collision resolution.
void ClampPlayerToScreen(Player &player, int screenWidth, int screenHeight);
// Same bounds check as ClampPlayerToScreen, for a bare center point. Lets
// collision resolution measure how much of a push a wall would swallow
// before actually applying it to the player.
Vector2 ClampCenterToScreen(Vector2 center, float halfSize, int screenWidth, int screenHeight);
void DrawPlayer(const Player &player);
Rectangle GetPlayerRect(const Player &player);
Vector2 GetAimDirection(const Player &player);
Vector2 GetGunTip(const Player &player);
void DamagePlayer(Player &player, int amount);
