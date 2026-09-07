#include "PlayerAgent.h"
#include <cmath>
#include <algorithm>

namespace
{
    struct EnemyDistance
    {
        float distSq;
        const Enemy *enemy;
    };
}

std::vector<float> GetPlayerState(const Player &player, const std::vector<Enemy> &enemies,
                                   int screenWidth, int screenHeight)
{
    std::vector<float> state(PLAYER_AGENT_INPUT_SIZE, 0.0f);

    state[0] = player.center.x / screenWidth;
    state[1] = player.center.y / screenHeight;
    state[2] = (float)player.health / (float)player.maxHealth;

    std::vector<EnemyDistance> distances;
    distances.reserve(enemies.size());
    for (const auto &enemy : enemies)
    {
        if (!enemy.active)
            continue;
        Vector2 enemyCenter = GetEnemyCenter(enemy);
        float dx = enemyCenter.x - player.center.x;
        float dy = enemyCenter.y - player.center.y;
        distances.push_back({dx * dx + dy * dy, &enemy});
    }
    std::sort(distances.begin(), distances.end(), [](const EnemyDistance &a, const EnemyDistance &b)
              { return a.distSq < b.distSq; });

    float maxDist = sqrtf((float)(screenWidth * screenWidth + screenHeight * screenHeight));

    for (int slot = 0; slot < PLAYER_AGENT_NEAREST_ENEMY_COUNT; slot++)
    {
        int base = 3 + slot * PLAYER_AGENT_FEATURES_PER_ENEMY;
        if (slot < (int)distances.size())
        {
            const Enemy &enemy = *distances[slot].enemy;
            Vector2 enemyCenter = GetEnemyCenter(enemy);
            state[base + 0] = (enemyCenter.x - player.center.x) / screenWidth;
            state[base + 1] = (enemyCenter.y - player.center.y) / screenHeight;
            state[base + 2] = sqrtf(distances[slot].distSq) / maxDist;
            state[base + 3] = (float)enemy.health / (float)enemy.maxHealth;
        }
        else
        {
            state[base + 2] = 1.0f; // no enemy in this slot: "maximally far away"
        }
    }

    int activeCountIndex = 3 + PLAYER_AGENT_NEAREST_ENEMY_COUNT * PLAYER_AGENT_FEATURES_PER_ENEMY;
    state[activeCountIndex] = std::min((float)distances.size() / 10.0f, 1.0f);

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
