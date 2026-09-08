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
    state[3] = player.velocity.x / player.speed;
    state[4] = player.velocity.y / player.speed;

    // Facing, as a unit vector — lets the network sense how far its own
    // gradually-turning rotation still is from the aim direction it commands.
    Vector2 facing = GetAimDirection(player);
    state[5] = facing.x;
    state[6] = facing.y;

    // Distance from the player's own edge to the nearest wall on each axis,
    // 0 = touching it, 1 = screen center — an explicit "am I cornered"
    // signal instead of something the network has to infer from raw x/y.
    float half = player.size / 2.0f;
    float wallDistX = std::min(player.center.x - half, screenWidth - half - player.center.x);
    float wallDistY = std::min(player.center.y - half, screenHeight - half - player.center.y);
    state[7] = wallDistX / (screenWidth / 2.0f);
    state[8] = wallDistY / (screenHeight / 2.0f);

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
        int base = PLAYER_AGENT_PLAYER_FEATURE_COUNT + slot * PLAYER_AGENT_FEATURES_PER_ENEMY;
        if (slot < (int)distances.size())
        {
            const Enemy &enemy = *distances[slot].enemy;
            Vector2 enemyCenter = GetEnemyCenter(enemy);
            float dx = enemyCenter.x - player.center.x;
            float dy = enemyCenter.y - player.center.y;

            // Rotate world-space dx/dy (and vx/vy) into the player's facing
            // frame: bodyX is "how far ahead of my gun", bodyY is "how far
            // to the side" — the same rotation used to go from world space
            // into body space given the player's forward unit vector.
            float bodyX = dx * facing.x + dy * facing.y;
            float bodyY = -dx * facing.y + dy * facing.x;
            float bodyVX = enemy.velocity.x * facing.x + enemy.velocity.y * facing.y;
            float bodyVY = -enemy.velocity.x * facing.y + enemy.velocity.y * facing.x;

            state[base + 0] = bodyX / maxDist;
            state[base + 1] = bodyY / maxDist;
            state[base + 2] = sqrtf(distances[slot].distSq) / maxDist;
            state[base + 3] = (float)enemy.health / (float)enemy.maxHealth;
            state[base + 4] = bodyVX / maxDist;
            state[base + 5] = bodyVY / maxDist;
        }
        else
        {
            state[base + 2] = 1.0f; // no enemy in this slot: "maximally far away"
        }
    }

    int activeCountIndex = PLAYER_AGENT_PLAYER_FEATURE_COUNT + PLAYER_AGENT_NEAREST_ENEMY_COUNT * PLAYER_AGENT_FEATURES_PER_ENEMY;
    state[activeCountIndex] = std::min((float)distances.size() / 10.0f, 1.0f);

    return state;
}

PlayerAction DecidePlayerAction(const Genome &brain, const Player &player,
                                 const std::vector<Enemy> &enemies,
                                 int screenWidth, int screenHeight)
{
    std::vector<float> state = GetPlayerState(player, enemies, screenWidth, screenHeight);
    std::vector<float> output = Activate(brain, state);

    PlayerAction action;
    action.move = {tanhf(output[0]), tanhf(output[1])};
    action.aim = {tanhf(output[2]), tanhf(output[3])};
    if (fabsf(action.aim.x) < 0.001f && fabsf(action.aim.y) < 0.001f)
        action.aim = GetAimDirection(player); // keep facing the same way if undecided
    action.shoot = (1.0f / (1.0f + expf(-output[4]))) > 0.5f;

    return action;
}
