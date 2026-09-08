#include "Player.h"
#include <cmath>
#include <algorithm>

namespace
{
    // Shortest signed distance (degrees, in (-180, 180]) from `from` to `to`,
    // so turning always takes the short way round instead of the long way
    // whenever the raw angle difference crosses the +-180 wrap.
    float DeltaAngleDeg(float from, float to)
    {
        float diff = fmodf(to - from, 360.0f);
        if (diff > 180.0f)
            diff -= 360.0f;
        else if (diff < -180.0f)
            diff += 360.0f;
        return diff;
    }
}

Player CreatePlayer(Vector2 startCenter, float size, float speed, int maxHealth, float gunLength, float gunWidth,
                    float turnSpeedDegPerSec, float acceleration)
{
    Player player;
    player.center = startCenter;
    player.velocity = {0.0f, 0.0f};
    player.size = size;
    player.speed = speed;
    player.acceleration = acceleration;
    player.rotation = 0.0f;
    player.turnSpeedDegPerSec = turnSpeedDegPerSec;
    player.gunLength = gunLength;
    player.gunWidth = gunWidth;
    player.health = maxHealth;
    player.maxHealth = maxHealth;
    return player;
}

void UpdatePlayer(Player &player, Vector2 moveDir, Vector2 aimDir, float deltaTime, int screenWidth, int screenHeight)
{
    // Ease velocity toward the commanded direction instead of setting position
    // from it directly — the agent's raw move output can flip sign frame to
    // frame (e.g. when the nearest-enemy ranking swaps), which without easing
    // snapped the body back and forth instantly and looked like a physics glitch.
    Vector2 targetVelocity = {moveDir.x * player.speed, moveDir.y * player.speed};
    Vector2 velocityDelta = {targetVelocity.x - player.velocity.x, targetVelocity.y - player.velocity.y};
    float maxDeltaLen = player.acceleration * deltaTime;
    float deltaLen = sqrtf(velocityDelta.x * velocityDelta.x + velocityDelta.y * velocityDelta.y);
    if (deltaLen > maxDeltaLen)
    {
        float scale = maxDeltaLen / deltaLen;
        velocityDelta.x *= scale;
        velocityDelta.y *= scale;
    }
    player.velocity.x += velocityDelta.x;
    player.velocity.y += velocityDelta.y;

    player.center.x += player.velocity.x * deltaTime;
    player.center.y += player.velocity.y * deltaTime;

    ClampPlayerToScreen(player, screenWidth, screenHeight);

    // Turn toward the aim direction at a capped rate instead of snapping to
    // it, since the agent's chosen aim can flip between enemies frame to frame.
    if (fabsf(aimDir.x) > 0.0001f || fabsf(aimDir.y) > 0.0001f)
    {
        float targetRotation = atan2f(aimDir.y, aimDir.x) * RAD2DEG;
        float delta = DeltaAngleDeg(player.rotation, targetRotation);
        float maxStep = player.turnSpeedDegPerSec * deltaTime;
        delta = std::max(-maxStep, std::min(maxStep, delta));
        player.rotation += delta;
    }
}

Vector2 ClampCenterToScreen(Vector2 center, float halfSize, int screenWidth, int screenHeight)
{
    if (center.x < halfSize)
        center.x = halfSize;
    if (center.y < halfSize)
        center.y = halfSize;
    if (center.x > screenWidth - halfSize)
        center.x = screenWidth - halfSize;
    if (center.y > screenHeight - halfSize)
        center.y = screenHeight - halfSize;
    return center;
}

void ClampPlayerToScreen(Player &player, int screenWidth, int screenHeight)
{
    player.center = ClampCenterToScreen(player.center, player.size / 2.0f, screenWidth, screenHeight);
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
