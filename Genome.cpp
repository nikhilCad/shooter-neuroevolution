#include "Genome.h"
#include "RandomUtil.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

namespace
{
    uint64_t ConnectionKey(int inNode, int outNode)
    {
        return ((uint64_t)(uint32_t)inNode << 32) | (uint32_t)outNode;
    }

    int GetOrCreateConnectionInnovation(InnovationTracker &tracker, int inNode, int outNode)
    {
        uint64_t key = ConnectionKey(inNode, outNode);
        auto it = tracker.connectionInnovations.find(key);
        if (it != tracker.connectionInnovations.end())
            return it->second;
        int innovation = tracker.nextInnovationNumber++;
        tracker.connectionInnovations[key] = innovation;
        return innovation;
    }

    NodeSplitRecord GetOrCreateNodeSplit(InnovationTracker &tracker, int connectionInnovation, int inNode, int outNode)
    {
        auto it = tracker.nodeSplitByConnection.find(connectionInnovation);
        if (it != tracker.nodeSplitByConnection.end())
            return it->second;

        NodeSplitRecord record;
        record.newNodeId = tracker.nextNodeId++;
        record.inInnovation = GetOrCreateConnectionInnovation(tracker, inNode, record.newNodeId);
        record.outInnovation = GetOrCreateConnectionInnovation(tracker, record.newNodeId, outNode);
        tracker.nodeSplitByConnection[connectionInnovation] = record;
        return record;
    }

    // True if `to` is reachable from `from` by following existing enabled
    // connections — i.e. whether adding a from->to edge... no, callers use
    // this to check the reverse: whether outNode can already reach inNode,
    // which is exactly when adding inNode->outNode would close a cycle.
    bool CanReach(const Genome &genome, int from, int to)
    {
        if (from == to)
            return true;
        std::unordered_set<int> visited{from};
        std::vector<int> stack{from};
        while (!stack.empty())
        {
            int current = stack.back();
            stack.pop_back();
            for (const auto &c : genome.connections)
            {
                if (!c.enabled || c.inNode != current)
                    continue;
                if (c.outNode == to)
                    return true;
                if (visited.insert(c.outNode).second)
                    stack.push_back(c.outNode);
            }
        }
        return false;
    }
}

Genome CreateMinimalGenome(int inputCount, int outputCount, InnovationTracker &tracker)
{
    Genome genome;
    genome.inputCount = inputCount;
    genome.outputCount = outputCount;

    for (int i = 0; i < inputCount; i++)
        genome.nodes.push_back({i, NodeType::Input});
    int biasId = inputCount;
    genome.nodes.push_back({biasId, NodeType::Bias});
    int firstOutputId = biasId + 1;
    for (int i = 0; i < outputCount; i++)
        genome.nodes.push_back({firstOutputId + i, NodeType::Output});

    tracker.nextNodeId = std::max(tracker.nextNodeId, firstOutputId + outputCount);

    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int inNode = 0; inNode <= inputCount; inNode++) // 0..inputCount-1 real inputs, inputCount == biasId
    {
        for (int o = 0; o < outputCount; o++)
        {
            int outNode = firstOutputId + o;
            int innovation = GetOrCreateConnectionInnovation(tracker, inNode, outNode);
            genome.connections.push_back({inNode, outNode, dist(RandomEngine()), true, innovation});
        }
    }
    return genome;
}

namespace
{
    float EvaluateNode(int nodeId, const std::unordered_map<int, NodeType> &typeById,
                        const std::unordered_map<int, std::vector<const ConnectionGene *>> &incomingByNode,
                        const std::vector<float> &inputs,
                        std::unordered_map<int, float> &cache, std::unordered_set<int> &inProgress)
    {
        auto cached = cache.find(nodeId);
        if (cached != cache.end())
            return cached->second;
        // Should never trigger for a genuinely feed-forward genome — a cheap
        // safety net against a cycle sneaking in rather than infinite recursion.
        if (inProgress.count(nodeId))
            return 0.0f;

        NodeType type = typeById.at(nodeId);
        float value;
        if (type == NodeType::Input)
            value = inputs[nodeId];
        else if (type == NodeType::Bias)
            value = 1.0f;
        else
        {
            inProgress.insert(nodeId);
            float sum = 0.0f;
            auto it = incomingByNode.find(nodeId);
            if (it != incomingByNode.end())
                for (const ConnectionGene *conn : it->second)
                    sum += EvaluateNode(conn->inNode, typeById, incomingByNode, inputs, cache, inProgress) * conn->weight;
            inProgress.erase(nodeId);
            value = (type == NodeType::Output) ? sum : tanhf(sum);
        }
        cache[nodeId] = value;
        return value;
    }
}

std::vector<float> Activate(const Genome &genome, const std::vector<float> &inputs)
{
    std::unordered_map<int, NodeType> typeById;
    typeById.reserve(genome.nodes.size());
    for (const auto &n : genome.nodes)
        typeById[n.id] = n.type;

    std::unordered_map<int, std::vector<const ConnectionGene *>> incomingByNode;
    for (const auto &c : genome.connections)
        if (c.enabled)
            incomingByNode[c.outNode].push_back(&c);

    std::unordered_map<int, float> cache;
    std::unordered_set<int> inProgress;

    int biasId = genome.inputCount;
    int firstOutputId = biasId + 1;
    std::vector<float> outputs;
    outputs.reserve(genome.outputCount);
    for (int i = 0; i < genome.outputCount; i++)
        outputs.push_back(EvaluateNode(firstOutputId + i, typeById, incomingByNode, inputs, cache, inProgress));
    return outputs;
}

void MutateWeights(Genome &genome, float mutationRate, float mutationStrength)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::normal_distribution<float> noise(0.0f, mutationStrength);
    for (auto &c : genome.connections)
        if (chance(RandomEngine()) < mutationRate)
            c.weight += noise(RandomEngine());
}

void MutateAddConnection(Genome &genome, InnovationTracker &tracker)
{
    const int MAX_ATTEMPTS = 20;
    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++)
    {
        const NodeGene &a = genome.nodes[RandomInt(0, (int)genome.nodes.size() - 1)];
        const NodeGene &b = genome.nodes[RandomInt(0, (int)genome.nodes.size() - 1)];
        if (a.id == b.id || a.type == NodeType::Output || b.type == NodeType::Input || b.type == NodeType::Bias)
            continue;

        bool alreadyExists = false;
        for (const auto &c : genome.connections)
            if (c.inNode == a.id && c.outNode == b.id)
            {
                alreadyExists = true;
                break;
            }
        if (alreadyExists || CanReach(genome, b.id, a.id)) // b already reaching a means a->b would close a loop
            continue;

        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        int innovation = GetOrCreateConnectionInnovation(tracker, a.id, b.id);
        genome.connections.push_back({a.id, b.id, dist(RandomEngine()), true, innovation});
        return;
    }
    // No valid non-cyclic, non-duplicate pair found within the attempt budget — skip.
}

void MutateAddNode(Genome &genome, InnovationTracker &tracker)
{
    std::vector<int> enabledIndices;
    for (size_t i = 0; i < genome.connections.size(); i++)
        if (genome.connections[i].enabled)
            enabledIndices.push_back((int)i);
    if (enabledIndices.empty())
        return;

    int splitIndex = enabledIndices[RandomInt(0, (int)enabledIndices.size() - 1)];
    genome.connections[splitIndex].enabled = false;
    int splitInNode = genome.connections[splitIndex].inNode;
    int splitOutNode = genome.connections[splitIndex].outNode;
    int splitInnovation = genome.connections[splitIndex].innovation;

    NodeSplitRecord split = GetOrCreateNodeSplit(tracker, splitInnovation, splitInNode, splitOutNode);

    bool nodeAlreadyPresent = false;
    for (const auto &n : genome.nodes)
        if (n.id == split.newNodeId)
        {
            nodeAlreadyPresent = true;
            break;
        }
    if (!nodeAlreadyPresent)
        genome.nodes.push_back({split.newNodeId, NodeType::Hidden});

    bool inConnPresent = false, outConnPresent = false;
    for (const auto &c : genome.connections)
    {
        if (c.innovation == split.inInnovation)
            inConnPresent = true;
        if (c.innovation == split.outInnovation)
            outConnPresent = true;
    }
    if (!inConnPresent)
        genome.connections.push_back({splitInNode, split.newNodeId, 1.0f, true, split.inInnovation});
    if (!outConnPresent)
        genome.connections.push_back({split.newNodeId, splitOutNode, 0.0f, true, split.outInnovation});
}

Genome Crossover(const Genome &a, float fitnessA, const Genome &b, float fitnessB)
{
    const Genome &fitter = (fitnessA >= fitnessB) ? a : b;
    const Genome &other = (fitnessA >= fitnessB) ? b : a;

    Genome child;
    child.inputCount = fitter.inputCount;
    child.outputCount = fitter.outputCount;

    std::unordered_map<int, const ConnectionGene *> otherByInnovation;
    for (const auto &c : other.connections)
        otherByInnovation[c.innovation] = &c;

    std::unordered_map<int, bool> nodePresent;
    auto addNode = [&](int nodeId)
    {
        if (nodePresent[nodeId])
            return;
        for (const auto &n : fitter.nodes)
            if (n.id == nodeId)
            {
                child.nodes.push_back(n);
                nodePresent[nodeId] = true;
                return;
            }
        for (const auto &n : other.nodes)
            if (n.id == nodeId)
            {
                child.nodes.push_back(n);
                nodePresent[nodeId] = true;
                return;
            }
    };

    std::uniform_real_distribution<float> coin(0.0f, 1.0f);
    for (const auto &geneA : fitter.connections)
    {
        auto matchIt = otherByInnovation.find(geneA.innovation);
        ConnectionGene chosen = geneA;
        if (matchIt != otherByInnovation.end())
        {
            // Matching gene: inherit from either parent at random. If either
            // parent has it disabled, usually keep it disabled in the child —
            // it was switched off for a structural reason, so it shouldn't
            // just come back for free.
            if (coin(RandomEngine()) < 0.5f)
                chosen = *matchIt->second;
            if ((!geneA.enabled || !matchIt->second->enabled) && coin(RandomEngine()) < 0.75f)
                chosen.enabled = false;
        }
        // Disjoint/excess genes (no match in `other`) are inherited from the
        // fitter parent only, which iterating `fitter.connections` already ensures.
        child.connections.push_back(chosen);
        addNode(chosen.inNode);
        addNode(chosen.outNode);
    }

    // Every input/bias/output node must exist even if the fitter parent
    // never happened to wire one up — Activate expects the full node set.
    for (const auto &n : fitter.nodes)
        if (n.type != NodeType::Hidden)
            addNode(n.id);

    return child;
}

float GeneticDistance(const Genome &a, const Genome &b)
{
    std::unordered_map<int, const ConnectionGene *> bByInnovation;
    for (const auto &c : b.connections)
        bByInnovation[c.innovation] = &c;

    int maxInnovationA = 0;
    for (const auto &c : a.connections)
        maxInnovationA = std::max(maxInnovationA, c.innovation);
    int maxInnovationB = 0;
    for (const auto &c : b.connections)
        maxInnovationB = std::max(maxInnovationB, c.innovation);
    int lowerMaxInnovation = std::min(maxInnovationA, maxInnovationB);

    int disjoint = 0, excess = 0, matching = 0;
    float weightDiffSum = 0.0f;
    for (const auto &c : a.connections)
    {
        auto it = bByInnovation.find(c.innovation);
        if (it != bByInnovation.end())
        {
            matching++;
            weightDiffSum += fabsf(c.weight - it->second->weight);
            bByInnovation.erase(it); // consumed; whatever's left in b is only-in-b
        }
        else if (c.innovation > lowerMaxInnovation)
            excess++;
        else
            disjoint++;
    }
    for (const auto &kv : bByInnovation)
    {
        if (kv.second->innovation > lowerMaxInnovation)
            excess++;
        else
            disjoint++;
    }

    int largerGenomeSize = std::max((int)a.connections.size(), (int)b.connections.size());
    float n = (largerGenomeSize < 20) ? 1.0f : (float)largerGenomeSize;
    float avgWeightDiff = matching > 0 ? weightDiffSum / matching : 0.0f;

    const float C1_EXCESS = 1.0f, C2_DISJOINT = 1.0f, C3_WEIGHT = 0.4f;
    return (C1_EXCESS * excess) / n + (C2_DISJOINT * disjoint) / n + C3_WEIGHT * avgWeightDiff;
}
