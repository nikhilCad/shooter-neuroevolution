#pragma once
#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include "NeuralNetwork.h"
#include <vector>

// How many of the closest enemies the network gets per-enemy detail on (dx,
// dy, distance, health each) — beyond this count, enemies are invisible to
// the network except through the aggregate active-enemy-count input.
const int PLAYER_AGENT_NEAREST_ENEMY_COUNT = 4;
const int PLAYER_AGENT_FEATURES_PER_ENEMY = 4;
const int PLAYER_AGENT_INPUT_SIZE = 3 + PLAYER_AGENT_NEAREST_ENEMY_COUNT * PLAYER_AGENT_FEATURES_PER_ENEMY + 1;
const int PLAYER_AGENT_OUTPUT_SIZE = 5;

struct PlayerAction
{
    Vector2 move; // desired movement direction, components roughly in [-1, 1]
    Vector2 aim;  // desired aim direction (not necessarily normalized)
    bool shoot;
};

// Encodes the game state (player + the nearest PLAYER_AGENT_NEAREST_ENEMY_COUNT
// enemies, closest first) into the network's input vector.
std::vector<float> GetPlayerState(const Player &player, const std::vector<Enemy> &enemies,
                                   int screenWidth, int screenHeight);

// Runs the brain on the current state and decodes its outputs into an action.
PlayerAction DecidePlayerAction(const NeuralNetwork &brain, const Player &player,
                                 const std::vector<Enemy> &enemies,
                                 int screenWidth, int screenHeight);
