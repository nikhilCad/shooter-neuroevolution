#pragma once
#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include "Genome.h"
#include <vector>

// Player features: x, y, health, velocity x/y, facing cos/sin, nearest
// x-wall distance, nearest y-wall distance.
const int PLAYER_AGENT_PLAYER_FEATURE_COUNT = 9;
// How many of the closest enemies the network gets per-enemy detail on (dx,
// dy, distance, health, velocity x/y each, all expressed in the player's own
// facing frame — see GetPlayerState) — beyond this count, enemies are
// invisible to the network except through the aggregate active-enemy-count
// input. Velocity is included so the network can learn to lead moving
// targets instead of only ever aiming at their current position.
const int PLAYER_AGENT_NEAREST_ENEMY_COUNT = 4;
const int PLAYER_AGENT_FEATURES_PER_ENEMY = 6;
const int PLAYER_AGENT_INPUT_SIZE = PLAYER_AGENT_PLAYER_FEATURE_COUNT +
                                    PLAYER_AGENT_NEAREST_ENEMY_COUNT * PLAYER_AGENT_FEATURES_PER_ENEMY + 1;
const int PLAYER_AGENT_OUTPUT_SIZE = 5;

struct PlayerAction
{
    Vector2 move; // desired movement direction, components roughly in [-1, 1]
    Vector2 aim;  // desired aim direction (not necessarily normalized)
    bool shoot;
};

// Encodes the game state (player + the nearest PLAYER_AGENT_NEAREST_ENEMY_COUNT
// enemies, closest first) into the network's input vector. Enemy position and
// velocity are rotated into the player's own facing frame (forward/lateral
// instead of world x/y), so "is this enemy ahead of my gun" is a direct
// feature instead of something the network has to reconstruct from the
// player's absolute rotation.
std::vector<float> GetPlayerState(const Player &player, const std::vector<Enemy> &enemies,
                                   int screenWidth, int screenHeight);

// Runs the brain on the current state and decodes its outputs into an action.
PlayerAction DecidePlayerAction(const Genome &brain, const Player &player,
                                 const std::vector<Enemy> &enemies,
                                 int screenWidth, int screenHeight);
