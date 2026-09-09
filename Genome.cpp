#include "Genome.h"
#include "RandomUtil.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
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
    // Builds genome.compiled* from scratch: a topological ordering of every
    // node (DFS post-order — inputs/bias naturally end up first since they
    // have no incoming edges) plus, for each node in that order, its enabled
    // incoming connections resolved to dense indices. Evaluating dense
    // indices in increasing order then always sees every dependency's value
    // already computed, with no recursion or per-tick hashing needed.
    void BuildCompiledCache(const Genome &genome)
    {
        std::unordered_map<int, NodeType> typeById;
        typeById.reserve(genome.nodes.size());
        for (const auto &n : genome.nodes)
            typeById[n.id] = n.type;

        std::unordered_map<int, std::vector<std::pair<int, float>>> incomingById;
        for (const auto &c : genome.connections)
            if (c.enabled)
                incomingById[c.outNode].push_back({c.inNode, c.weight});

        std::vector<int> order;
        order.reserve(genome.nodes.size());
        std::unordered_set<int> visited;
        std::function<void(int)> visit = [&](int id)
        {
            if (!visited.insert(id).second)
                return;
            auto it = incomingById.find(id);
            if (it != incomingById.end())
                for (const auto &pr : it->second)
                    visit(pr.first);
            order.push_back(id);
        };
        for (const auto &n : genome.nodes)
            visit(n.id);

        std::unordered_map<int, int> denseIndexById;
        denseIndexById.reserve(order.size());
        for (size_t i = 0; i < order.size(); i++)
            denseIndexById[order[i]] = (int)i;

        genome.compiledNodeId = order;
        genome.compiledNodeType.resize(order.size());
        genome.compiledIncoming.assign(order.size(), {});
        for (size_t i = 0; i < order.size(); i++)
        {
            int id = order[i];
            genome.compiledNodeType[i] = typeById[id];
            auto it = incomingById.find(id);
            if (it != incomingById.end())
            {
                genome.compiledIncoming[i].reserve(it->second.size());
                for (const auto &pr : it->second)
                    genome.compiledIncoming[i].push_back({denseIndexById[pr.first], pr.second});
            }
        }

        int firstOutputId = genome.inputCount + 1;
        genome.compiledOutputDenseIndex.resize(genome.outputCount);
        for (int i = 0; i < genome.outputCount; i++)
            genome.compiledOutputDenseIndex[i] = denseIndexById[firstOutputId + i];

        genome.compiledValid = true;
    }
}

std::vector<float> Activate(const Genome &genome, const std::vector<float> &inputs)
{
    if (!genome.compiledValid)
        BuildCompiledCache(genome);

    size_t nodeCount = genome.compiledNodeId.size();
    std::vector<float> values(nodeCount, 0.0f);
    for (size_t i = 0; i < nodeCount; i++)
    {
        NodeType type = genome.compiledNodeType[i];
        if (type == NodeType::Input)
        {
            values[i] = inputs[genome.compiledNodeId[i]];
            continue;
        }
        if (type == NodeType::Bias)
        {
            values[i] = 1.0f;
            continue;
        }
        float sum = 0.0f;
        for (const auto &pr : genome.compiledIncoming[i])
            sum += values[pr.first] * pr.second; // pr.first < i always, guaranteed by topological order
        values[i] = (type == NodeType::Output) ? sum : tanhf(sum);
    }

    std::vector<float> outputs(genome.outputCount);
    for (int i = 0; i < genome.outputCount; i++)
        outputs[i] = values[genome.compiledOutputDenseIndex[i]];
    return outputs;
}

void MutateWeights(Genome &genome, float mutationRate, float mutationStrength)
{
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);
    std::normal_distribution<float> noise(0.0f, mutationStrength);
    for (auto &c : genome.connections)
        if (chance(RandomEngine()) < mutationRate)
            c.weight += noise(RandomEngine());
    // BuildCompiledCache snapshots each weight's VALUE into compiledIncoming
    // at cache-build time — Activate() reads that cached copy, never
    // genome.connections directly. So a genome whose cache was already built
    // (e.g. copied from a parent that already played an episode this
    // generation) would otherwise keep activating with its PRE-mutation
    // weights until some future structural mutation forces a rebuild —
    // silently discarding this mutation's effect on behavior even though the
    // mutated weight is still what gets saved/inherited from here on.
    genome.compiledValid = false;
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
        genome.compiledValid = false; // structure changed — Activate must rebuild its cache
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

    genome.compiledValid = false; // structure changed — Activate must rebuild its cache
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
    // Disabled connections are ignored entirely: they have zero effect on
    // Activate, so two genomes that behave identically shouldn't be pushed
    // apart just because one is carrying more disabled crossover leftovers
    // than the other. This also matters for `n` below — counting disabled
    // genes in genome size let crossover's gene bloat (see input.md/commit
    // history: genomes observed with 200+ total connections but only ~20-30
    // enabled) crush excess/disjoint toward zero once that raw count passed
    // the normalization cutoff, leaving avgWeightDiff as nearly the only
    // thing that could move the distance — which in practice never crossed
    // COMPATIBILITY_THRESHOLD, so the whole population collapsed to one
    // species and stayed there for an entire run.
    std::unordered_map<int, const ConnectionGene *> bByInnovation;
    int enabledCountB = 0;
    for (const auto &c : b.connections)
        if (c.enabled)
        {
            bByInnovation[c.innovation] = &c;
            enabledCountB++;
        }

    int maxInnovationA = 0;
    for (const auto &c : a.connections)
        if (c.enabled)
            maxInnovationA = std::max(maxInnovationA, c.innovation);
    int maxInnovationB = 0;
    for (const auto &c : b.connections)
        if (c.enabled)
            maxInnovationB = std::max(maxInnovationB, c.innovation);
    int lowerMaxInnovation = std::min(maxInnovationA, maxInnovationB);

    int disjoint = 0, excess = 0, matching = 0;
    float weightDiffSum = 0.0f;
    int enabledCountA = 0;
    for (const auto &c : a.connections)
    {
        if (!c.enabled)
            continue;
        enabledCountA++;
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

    int largerGenomeSize = std::max(enabledCountA, enabledCountB);
    float n = (largerGenomeSize < 20) ? 1.0f : (float)largerGenomeSize;
    float avgWeightDiff = matching > 0 ? weightDiffSum / matching : 0.0f;

    const float C1_EXCESS = 1.0f, C2_DISJOINT = 1.0f, C3_WEIGHT = 0.4f;
    return (C1_EXCESS * excess) / n + (C2_DISJOINT * disjoint) / n + C3_WEIGHT * avgWeightDiff;
}

namespace
{
    const uint32_t GENOME_FILE_MAGIC = 0x474E4F4D; // 'GNOM'
    const uint32_t GENOME_FILE_VERSION = 1;

    template <typename T>
    void WriteValue(FILE *file, const T &value) { fwrite(&value, sizeof(T), 1, file); }

    template <typename T>
    bool ReadValue(FILE *file, T &value) { return fread(&value, sizeof(T), 1, file) == 1; }
}

bool SaveGenomeToFile(const Genome &genome, const char *filePath)
{
    FILE *file = fopen(filePath, "wb");
    if (!file)
        return false;

    WriteValue(file, GENOME_FILE_MAGIC);
    WriteValue(file, GENOME_FILE_VERSION);
    WriteValue(file, (int32_t)genome.inputCount);
    WriteValue(file, (int32_t)genome.outputCount);
    WriteValue(file, (uint32_t)genome.nodes.size());
    for (const auto &n : genome.nodes)
    {
        WriteValue(file, (int32_t)n.id);
        WriteValue(file, (int32_t)n.type);
    }
    WriteValue(file, (uint32_t)genome.connections.size());
    for (const auto &c : genome.connections)
    {
        WriteValue(file, (int32_t)c.inNode);
        WriteValue(file, (int32_t)c.outNode);
        WriteValue(file, c.weight);
        WriteValue(file, (uint8_t)(c.enabled ? 1 : 0));
        WriteValue(file, (int32_t)c.innovation);
    }

    fclose(file);
    return true;
}

bool LoadGenomeFromFile(Genome &genome, const char *filePath)
{
    FILE *file = fopen(filePath, "rb");
    if (!file)
        return false;

    uint32_t magic = 0, version = 0;
    int32_t inputCount = 0, outputCount = 0;
    uint32_t nodeCount = 0, connectionCount = 0;
    bool ok = ReadValue(file, magic) && magic == GENOME_FILE_MAGIC &&
              ReadValue(file, version) && version == GENOME_FILE_VERSION &&
              ReadValue(file, inputCount) && ReadValue(file, outputCount) &&
              ReadValue(file, nodeCount);

    Genome loaded;
    if (ok)
    {
        loaded.inputCount = inputCount;
        loaded.outputCount = outputCount;
        loaded.nodes.resize(nodeCount);
        for (auto &n : loaded.nodes)
        {
            int32_t id = 0, type = 0;
            ok = ok && ReadValue(file, id) && ReadValue(file, type);
            n.id = id;
            n.type = (NodeType)type;
        }
    }

    ok = ok && ReadValue(file, connectionCount);
    if (ok)
    {
        loaded.connections.resize(connectionCount);
        for (auto &c : loaded.connections)
        {
            int32_t inNode = 0, outNode = 0, innovation = 0;
            float weight = 0.0f;
            uint8_t enabled = 0;
            ok = ok && ReadValue(file, inNode) && ReadValue(file, outNode) && ReadValue(file, weight) &&
                 ReadValue(file, enabled) && ReadValue(file, innovation);
            c.inNode = inNode;
            c.outNode = outNode;
            c.weight = weight;
            c.enabled = enabled != 0;
            c.innovation = innovation;
        }
    }

    fclose(file);
    if (!ok)
        return false;

    genome = std::move(loaded);
    return true;
}
