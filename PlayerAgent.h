#pragma once
#include "raylib.h"
#include "Player.h"
#include "Enemy.h"
#include "NeuralNetwork.h"
#include <vector>

const int PLAYER_AGENT_INPUT_SIZE = 8;
const int PLAYER_AGENT_OUTPUT_SIZE = 5;

struct PlayerAction
{
    Vector2 move; // desired movement direction, components roughly in [-1, 1]
    Vector2 aim;  // desired aim direction (not necessarily normalized)
    bool shoot;
};

// Encodes the game state (player + nearest enemy) into the network's input vector.
std::vector<float> GetPlayerState(const Player &player, const std::vector<Enemy> &enemies,
                                   int screenWidth, int screenHeight);

// Runs the brain on the current state and decodes its outputs into an action.
PlayerAction DecidePlayerAction(const NeuralNetwork &brain, const Player &player,
                                 const std::vector<Enemy> &enemies,
                                 int screenWidth, int screenHeight);
