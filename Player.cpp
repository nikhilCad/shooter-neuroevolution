#include "Player.h"
#include <cmath>

Player CreatePlayer(Vector2 startCenter, float size, float speed, int maxHealth, float gunLength, float gunWidth)
{
    Player player;
    player.center = startCenter;
    player.size = size;
    player.speed = speed;
    player.rotation = 0.0f;
    player.gunLength = gunLength;
    player.gunWidth = gunWidth;
    player.health = maxHealth;
    player.maxHealth = maxHealth;
    return player;
}

void UpdatePlayer(Player &player, Vector2 moveDir, Vector2 aimDir, float deltaTime, int screenWidth, int screenHeight)
{
    player.center.x += moveDir.x * player.speed * deltaTime;
    player.center.y += moveDir.y * player.speed * deltaTime;

    // Keep the player inside the window (screen collision)
    float half = player.size / 2.0f;
    if (player.center.x < half)
        player.center.x = half;
    if (player.center.y < half)
        player.center.y = half;
    if (player.center.x > screenWidth - half)
        player.center.x = screenWidth - half;
    if (player.center.y > screenHeight - half)
        player.center.y = screenHeight - half;

    // Face the aim direction
    if (fabsf(aimDir.x) > 0.0001f || fabsf(aimDir.y) > 0.0001f)
        player.rotation = atan2f(aimDir.y, aimDir.x) * RAD2DEG;
}

Vector2 GetAimDirection(const Player &player)
{
    float rad = player.rotation * DEG2RAD;
    return {cosf(rad), sinf(rad)};
}

void DrawPlayer(const Player &player)
{
    Rectangle bodyRect = {player.center.x, player.center.y, player.size, player.size};
    Vector2 bodyOrigin = {player.size / 2.0f, player.size / 2.0f};
    DrawRectanglePro(bodyRect, bodyOrigin, player.rotation, BLUE);

    // Gun mounted on the side of the square, rotated to face the aim direction
    float gunDistance = player.size / 2.0f + player.gunLength / 2.0f;
    Vector2 aim = GetAimDirection(player);
    Vector2 gunCenter = {
        player.center.x + aim.x * gunDistance,
        player.center.y + aim.y * gunDistance};
    Rectangle gunRect = {gunCenter.x, gunCenter.y, player.gunLength, player.gunWidth};
    Vector2 gunOrigin = {player.gunLength / 2.0f, player.gunWidth / 2.0f};
    DrawRectanglePro(gunRect, gunOrigin, player.rotation, GRAY);
}

Rectangle GetPlayerRect(const Player &player)
{
    return {player.center.x - player.size / 2.0f, player.center.y - player.size / 2.0f, player.size, player.size};
}

Vector2 GetGunTip(const Player &player)
{
    Vector2 aim = GetAimDirection(player);
    float distance = player.size / 2.0f + player.gunLength;
    return {player.center.x + aim.x * distance, player.center.y + aim.y * distance};
}

void DamagePlayer(Player &player, int amount)
{
    player.health -= amount;
    if (player.health < 0)
        player.health = 0;
}
