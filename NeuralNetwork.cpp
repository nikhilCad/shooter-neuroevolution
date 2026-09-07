#include "NeuralNetwork.h"
#include <cmath>
#include <random>

namespace
{
    std::mt19937 &RandomEngine()
    {
        static std::mt19937 engine(std::random_device{}());
        return engine;
    }

    int WeightCount(int inputSize, int hiddenSize, int outputSize)
    {
        return inputSize * hiddenSize + hiddenSize + hiddenSize * outputSize + outputSize;
    }
}

NeuralNetwork CreateNeuralNetwork(int inputSize, int hiddenSize, int outputSize)
{
    NeuralNetwork net;
    net.inputSize = inputSize;
    net.hiddenSize = hiddenSize;
    net.outputSize = outputSize;

    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    net.weights.resize(WeightCount(inputSize, hiddenSize, outputSize));
    for (float &w : net.weights)
        w = dist(RandomEngine());

    return net;
}

std::vector<float> Forward(const NeuralNetwork &net, const std::vector<float> &input)
{
    const float *w = net.weights.data();

    // Input -> hidden (with bias), tanh activation
    std::vector<float> hidden(net.hiddenSize, 0.0f);
    for (int h = 0; h < net.hiddenSize; h++)
    {
        float sum = 0.0f;
        for (int i = 0; i < net.inputSize; i++)
            sum += input[i] * w[h * net.inputSize + i];
        hidden[h] = sum;
    }
    w += net.inputSize * net.hiddenSize;
    for (int h = 0; h < net.hiddenSize; h++)
        hidden[h] = tanhf(hidden[h] + w[h]);
    w += net.hiddenSize;

    // Hidden -> output (with bias), left un-activated; the caller applies
    // action-specific activations (tanh for directions, sigmoid for shoot).
    std::vector<float> output(net.outputSize, 0.0f);
    for (int o = 0; o < net.outputSize; o++)
    {
        float sum = 0.0f;
        for (int h = 0; h < net.hiddenSize; h++)
            sum += hidden[h] * w[o * net.hiddenSize + h];
        output[o] = sum;
    }
    w += net.hiddenSize * net.outputSize;
    for (int o = 0; o < net.outputSize; o++)
        output[o] += w[o];

    return output;
}

NeuralNetwork MutateNetwork(const NeuralNetwork &net, float mutationRate, float mutationStrength)
{
    NeuralNetwork mutated = net;
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::normal_distribution<float> noise(0.0f, mutationStrength);
    for (float &w : mutated.weights)
    {
        if (chance(RandomEngine()) < mutationRate)
            w += noise(RandomEngine());
    }
    return mutated;
}
