#include "PlayerAgent.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    struct EnemyDistance
    {
        float distSq;
        const Enemy *enemy;
    };
}

std::vector<float> GetPlayerState(const Player &player, const std::vector<Enemy> &enemies,
                                   int screenWidth, int screenHeight, float touchCooldownFrac)
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

    // 1 right after taking touch damage, decaying to 0 once touchable again —
    // lets the network know it currently has a brief grace window instead of
    // only ever seeing the health drop after the fact.
    state[9] = touchCooldownFrac;

    std::vector<EnemyDistance> distances;
    distances.reserve(enemies.size());
    int nearbyCount = 0;
    int forwardConeCount = 0;
    for (const auto &enemy : enemies)
    {
        if (!enemy.active)
            continue;
        Vector2 enemyCenter = GetEnemyCenter(enemy);
        float dx = enemyCenter.x - player.center.x;
        float dy = enemyCenter.y - player.center.y;
        float distSq = dx * dx + dy * dy;
        distances.push_back({distSq, &enemy});
        if (distSq <= PLAYER_AGENT_LOCAL_THREAT_RADIUS * PLAYER_AGENT_LOCAL_THREAT_RADIUS)
            nearbyCount++;

        // cos(angle between facing and direction-to-enemy) = forward-projected
        // component / distance — no need for atan2/acos, just the projection
        // we already compute per-enemy below, done here against every active
        // enemy (not just the 4 nearest) since a lined-up enemy far away still
        // matters for "is committing to this aim direction worth it."
        float dist = sqrtf(distSq);
        if (dist > 0.0001f && (dx * facing.x + dy * facing.y) / dist > PLAYER_AGENT_FORWARD_CONE_COS)
            forwardConeCount++;
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
            float dist = sqrtf(distances[slot].distSq);

            // Rotate world-space dx/dy (and vx/vy) into the player's facing
            // frame: bodyX is "how far ahead of my gun", bodyY is "how far
            // to the side" — the same rotation used to go from world space
            // into body space given the player's forward unit vector.
            float bodyX = dx * facing.x + dy * facing.y;
            float bodyY = -dx * facing.y + dy * facing.x;
            float bodyVX = enemy.velocity.x * facing.x + enemy.velocity.y * facing.y;
            float bodyVY = -enemy.velocity.x * facing.y + enemy.velocity.y * facing.x;

            // Component of the enemy's velocity directed straight at the
            // player (positive = closing in, negative = retreating) — a
            // fast enemy far away can be more urgent than a slow one nearby,
            // which raw distance and body-frame velocity alone don't capture.
            float closingSpeed = dist > 0.0001f ? -(enemy.velocity.x * dx + enemy.velocity.y * dy) / dist : 0.0f;

            state[base + 0] = bodyX / maxDist;
            state[base + 1] = bodyY / maxDist;
            state[base + 2] = dist / maxDist;
            state[base + 3] = (float)enemy.health / (float)enemy.maxHealth;
            state[base + 4] = bodyVX / maxDist;
            state[base + 5] = bodyVY / maxDist;
            state[base + 6] = closingSpeed / enemy.speed;
        }
        else
        {
            state[base + 2] = 1.0f; // no enemy in this slot: "maximally far away"
        }
    }

    int aggregateBase = PLAYER_AGENT_PLAYER_FEATURE_COUNT + PLAYER_AGENT_NEAREST_ENEMY_COUNT * PLAYER_AGENT_FEATURES_PER_ENEMY;
    // Nearby-enemy count is naturally small (the 4 detailed slots already
    // cover most "surrounded" cases) — normalize by a small cap so it
    // actually uses its range.
    state[aggregateBase + 0] = std::min((float)nearbyCount / 5.0f, 1.0f);
    // A narrow 20-degree cone naturally holds fewer enemies than the full
    // local-threat radius above, so it gets its own (smaller) cap too.
    state[aggregateBase + 1] = std::min((float)forwardConeCount / 3.0f, 1.0f);

    return state;
}

PlayerAction DecidePlayerAction(const Genome &brain, const Player &player,
                                 const std::vector<Enemy> &enemies,
                                 int screenWidth, int screenHeight, float touchCooldownFrac)
{
    std::vector<float> state = GetPlayerState(player, enemies, screenWidth, screenHeight, touchCooldownFrac);
    std::vector<float> output = Activate(brain, state);

    PlayerAction action;
    action.move = {tanhf(output[0]), tanhf(output[1])};
    action.aim = {tanhf(output[2]), tanhf(output[3])};
    if (fabsf(action.aim.x) < 0.001f && fabsf(action.aim.y) < 0.001f)
        action.aim = GetAimDirection(player); // keep facing the same way if undecided
    action.shoot = (1.0f / (1.0f + expf(-output[4]))) > 0.5f;

    return action;
}

const char *PlayerAgentInputLabel(int index)
{
    static const char *PLAYER_FEATURE_LABELS[] = {
        "player.x", "player.y", "player.health", "player.velocityX", "player.velocityY",
        "player.facingCos", "player.facingSin", "player.wallDistX", "player.wallDistY",
        "player.touchCooldown"};
    static const char *ENEMY_FEATURE_LABELS[] = {
        "bodyX", "bodyY", "distance", "health", "bodyVX", "bodyVY", "closingSpeed"};
    static const char *AGGREGATE_LABELS[] = {"nearbyEnemyCount", "forwardConeEnemyCount"};

    if (index < PLAYER_AGENT_PLAYER_FEATURE_COUNT)
        return PLAYER_FEATURE_LABELS[index];

    int enemyBase = PLAYER_AGENT_PLAYER_FEATURE_COUNT;
    int enemyRegionSize = PLAYER_AGENT_NEAREST_ENEMY_COUNT * PLAYER_AGENT_FEATURES_PER_ENEMY;
    if (index < enemyBase + enemyRegionSize)
    {
        int offset = index - enemyBase;
        int slot = offset / PLAYER_AGENT_FEATURES_PER_ENEMY;
        int feature = offset % PLAYER_AGENT_FEATURES_PER_ENEMY;
        thread_local char buffer[64]; // thread_local so concurrent callers (the sweep runs multi-threaded) never race on it
        snprintf(buffer, sizeof(buffer), "enemy#%d.%s", slot + 1, ENEMY_FEATURE_LABELS[feature]);
        return buffer;
    }

    int aggregateIndex = index - (enemyBase + enemyRegionSize);
    if (aggregateIndex >= 0 && aggregateIndex < PLAYER_AGENT_AGGREGATE_FEATURE_COUNT)
        return AGGREGATE_LABELS[aggregateIndex];

    return "?";
}
