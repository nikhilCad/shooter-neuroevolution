#pragma once
#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include "Genome.h"
#include <vector>

struct Bullet
{
    Vector2 position;
    Vector2 velocity;
    bool active;
};

struct Episode
{
    float time = 0.0f;
    float reward = 0.0f;
    int score = 0;
    float shootTimer = 0.0f;
    float enemySpawnTimer = 0.0f;
    float enemyTouchTimer = 0.0f;

    // Reward broken down by source, purely for diagnostics/logging — the sum
    // of these always equals `reward`, since every place that changes reward
    // adds to the matching field here too. touch/death penalties are stored
    // as positive magnitudes (points lost), not negative, so a report reads
    // as "touch penalty: 75.0" rather than a confusing "-75.0".
    float rewardSurvival = 0.0f;
    float rewardHits = 0.0f;
    float rewardKills = 0.0f;
    float rewardTouchPenalty = 0.0f;
    float rewardDeathPenalty = 0.0f;

    int shotsFired = 0;
    int hits = 0;
};

// --- Player config ---
static const float PLAYER_SIZE = 50.0f;
static const float PLAYER_SPEED = 300.0f;
static const int PLAYER_MAX_HEALTH = 100;
static const float GUN_LENGTH = 20.0f;
static const float GUN_WIDTH = 10.0f;
// Gun turns to face the aim direction instead of snapping to it instantly.
static const float PLAYER_TURN_SPEED_DEG_PER_SEC = 540.0f;
// Velocity eases toward the commanded direction instead of snapping to it;
// reaches full PLAYER_SPEED from a standstill in ~0.12s.
static const float PLAYER_ACCELERATION = 2500.0f;

// --- Bullet config ---
static const float BULLET_RADIUS = 4.0f;
static const float BULLET_SPEED = 500.0f;
static const float SHOOT_COOLDOWN = 0.2f;
static const int BULLET_DAMAGE = 1;

// --- Enemy config ---
static const float ENEMY_SIZE = 30.0f;
static const float ENEMY_SPEED = 100.0f;
static const int ENEMY_MAX_HEALTH = 3;
static const float ENEMY_SPAWN_INTERVAL = 1.2f;
static const float ENEMY_TOUCH_DAMAGE = 10.0f;
static const float ENEMY_TOUCH_COOLDOWN = 0.6f;

// --- Score (a human-readable score, separate from the ML reward signal) ---
static const int SCORE_PER_KILL = 100;

// --- Reward shaping for the evolving player brain ---
// Score (kills) is the dominant signal; survival time is only a small trickle
// so a passive agent that never kills anything can't out-earn an aggressive
// one just by running out the clock (there's no episode time limit).
static const float REWARD_SURVIVE_PER_SECOND = 0.05f;
static const float REWARD_PER_HIT = 2.0f;
static const float REWARD_PER_KILL = 20.0f;
static const float REWARD_DEATH_PENALTY = 30.0f;
// Kept separate from ENEMY_TOUCH_DAMAGE (which only controls HP loss) and set
// well above it so bumping into enemies is heavily discouraged even though
// each individual touch is rate-limited by ENEMY_TOUCH_COOLDOWN.
static const float REWARD_ENEMY_TOUCH_PENALTY = 25.0f;

// --- Evolution config (interactive-play defaults; the sweep overrides these) ---
// population=80 was the empirical winner of a 16-config sweep of the old
// fixed-topology network (see archive/v1-fixed-topology/RESULTS.md); kept as the NEAT population size
// too. There's no HIDDEN_SIZE/ELITE_COUNT anymore — NEAT genomes start with
// zero hidden nodes and grow structure via mutation, and elitism is now
// per-species (see SPECIES_CHAMPION_MIN_SIZE in Evolution.cpp) plus one
// always-preserved all-time-best genome, rather than a flat top-N.
static const int POPULATION_SIZE = 80;
static const float MUTATION_RATE = 0.15f;
static const float MUTATION_STRENGTH = 0.5f;

static const float SIMULATION_FIXED_DT = 1.0f / 60.0f;

void ResetEpisode(Episode &episode, Player &player, std::vector<Bullet> &bullets,
                  std::vector<Enemy> &enemies, int screenWidth, int screenHeight);

// Advances the simulation by one fixed timestep (SIMULATION_FIXED_DT) for a
// specific genome's episode. Called multiple times per rendered frame when
// fast-forwarding — or back-to-back with no rendering at all, e.g. by the
// parameter sweep — so game logic stays deterministic regardless of how many
// steps run before anything (if anything) gets drawn.
//
// Returns true once the episode ends (the player died) — episode.reward/
// score/time are already finalized (including the death penalty) at that
// point, but the caller still owns recording that outcome with whatever
// bookkeeping it's using (e.g. Evolution::FinishEpisode) and resetting for
// the next episode (ResetEpisode). This function has no dependency on
// Evolution at all, so it's safe to call from any thread as long as no two
// threads ever touch the same Genome concurrently (Activate lazily caches
// per-genome state that isn't synchronized).
bool SimulateStep(float deltaTime, const Genome &brain, Player &player,
                  std::vector<Bullet> &bullets, std::vector<Enemy> &enemies,
                  Episode &episode, int screenWidth, int screenHeight,
                  std::vector<Vector2> &killFlashes);

struct EpisodeOutcome
{
    float fitness;
    int score;
    float time;

    // Diagnostics, copied straight from the finished Episode — see Episode's
    // own fields for what each one means.
    float rewardSurvival;
    float rewardHits;
    float rewardKills;
    float rewardTouchPenalty;
    float rewardDeathPenalty;
    int shotsFired;
    int hits;
};

// Plays one full episode for `genome`, from a fresh spawn to death, entirely
// self-contained (its own Player/bullets/enemies/Episode state — nothing
// shared). Safe to call concurrently for different genomes on different
// threads. Used by the parameter sweep to evaluate a whole generation's
// population in parallel instead of one genome at a time.
EpisodeOutcome PlayEpisode(const Genome &genome, int screenWidth, int screenHeight);
