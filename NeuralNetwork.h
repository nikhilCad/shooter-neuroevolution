#pragma once
#include <vector>

// A tiny feedforward network: input -> hidden (tanh) -> output (linear).
// The weight vector is the "genome" that evolution mutates between generations.
struct NeuralNetwork
{
    int inputSize;
    int hiddenSize;
    int outputSize;
    std::vector<float> weights;
};

NeuralNetwork CreateNeuralNetwork(int inputSize, int hiddenSize, int outputSize);
std::vector<float> Forward(const NeuralNetwork &net, const std::vector<float> &input);
NeuralNetwork MutateNetwork(const NeuralNetwork &net, float mutationRate, float mutationStrength);
