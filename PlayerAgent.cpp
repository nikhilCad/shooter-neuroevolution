#include "PlayerAgent.h"
#include <cmath>
#include <algorithm>
#include <limits>

std::vector<float> GetPlayerState(const Player &player, const std::vector<Enemy> &enemies,
                                   int screenWidth, int screenHeight)
{
    std::vector<float> state(PLAYER_AGENT_INPUT_SIZE, 0.0f);

    state[0] = player.center.x / screenWidth;
    state[1] = player.center.y / screenHeight;
    state[2] = (float)player.health / (float)player.maxHealth;

    const Enemy *nearest = nullptr;
    float nearestDistSq = std::numeric_limits<float>::max();
    int activeCount = 0;
    for (const auto &enemy : enemies)
    {
        if (!enemy.active)
            continue;
        activeCount++;
        Vector2 enemyCenter = GetEnemyCenter(enemy);
        float dx = enemyCenter.x - player.center.x;
        float dy = enemyCenter.y - player.center.y;
        float distSq = dx * dx + dy * dy;
        if (distSq < nearestDistSq)
        {
            nearestDistSq = distSq;
            nearest = &enemy;
        }
    }

    float maxDist = sqrtf((float)(screenWidth * screenWidth + screenHeight * screenHeight));
    if (nearest != nullptr)
    {
        Vector2 nearestCenter = GetEnemyCenter(*nearest);
        state[3] = (nearestCenter.x - player.center.x) / screenWidth;
        state[4] = (nearestCenter.y - player.center.y) / screenHeight;
        state[5] = sqrtf(nearestDistSq) / maxDist;
        state[6] = (float)nearest->health / (float)nearest->maxHealth;
    }
    else
    {
        state[5] = 1.0f; // no enemy nearby: report "maximally far away"
    }

    state[7] = std::min(activeCount / 10.0f, 1.0f);

    return state;
}

PlayerAction DecidePlayerAction(const NeuralNetwork &brain, const Player &player,
                                 const std::vector<Enemy> &enemies,
                                 int screenWidth, int screenHeight)
{
    std::vector<float> state = GetPlayerState(player, enemies, screenWidth, screenHeight);
    std::vector<float> output = Forward(brain, state);

    PlayerAction action;
    action.move = {tanhf(output[0]), tanhf(output[1])};
    action.aim = {tanhf(output[2]), tanhf(output[3])};
    if (fabsf(action.aim.x) < 0.001f && fabsf(action.aim.y) < 0.001f)
        action.aim = GetAimDirection(player); // keep facing the same way if undecided
    action.shoot = (1.0f / (1.0f + expf(-output[4]))) > 0.5f;

    return action;
}
