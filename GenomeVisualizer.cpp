#include "GenomeVisualizer.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{
    // Longest path (in edges) from any input/bias node to `nodeId`, following
    // enabled connections — used only to lay hidden nodes out left-to-right
    // in roughly the order they feed into each other, not for evaluation.
    int NodeDepth(int nodeId, const std::unordered_map<int, NodeType> &typeById,
                  const std::unordered_map<int, std::vector<int>> &incomingFrom,
                  std::unordered_map<int, int> &cache)
    {
        auto cached = cache.find(nodeId);
        if (cached != cache.end())
            return cached->second;

        if (typeById.at(nodeId) == NodeType::Input || typeById.at(nodeId) == NodeType::Bias)
        {
            cache[nodeId] = 0;
            return 0;
        }

        cache[nodeId] = 0; // defensive cycle guard; a valid genome is acyclic
        int depth = 0;
        auto it = incomingFrom.find(nodeId);
        if (it != incomingFrom.end())
            for (int predecessor : it->second)
                depth = std::max(depth, NodeDepth(predecessor, typeById, incomingFrom, cache) + 1);

        cache[nodeId] = depth;
        return depth;
    }

    const char *OutputLabel(int outputIndex)
    {
        switch (outputIndex)
        {
        case 0:
            return "move.x";
        case 1:
            return "move.y";
        case 2:
            return "aim.x";
        case 3:
            return "aim.y";
        case 4:
            return "shoot";
        default:
            return "?";
        }
    }
}

void DrawGenomeVisualization(const Genome &genome, Rectangle area)
{
    DrawRectangleRec(area, Fade(BLACK, 0.05f));
    DrawRectangleLinesEx(area, 1, DARKGRAY);

    std::unordered_map<int, NodeType> typeById;
    typeById.reserve(genome.nodes.size());
    for (const auto &n : genome.nodes)
        typeById[n.id] = n.type;

    std::unordered_map<int, std::vector<int>> incomingFrom;
    for (const auto &c : genome.connections)
        if (c.enabled)
            incomingFrom[c.outNode].push_back(c.inNode);

    std::vector<int> inputIds, hiddenIds, outputIds;
    for (const auto &n : genome.nodes)
    {
        if (n.type == NodeType::Input || n.type == NodeType::Bias)
            inputIds.push_back(n.id);
        else if (n.type == NodeType::Hidden)
            hiddenIds.push_back(n.id);
        else
            outputIds.push_back(n.id);
    }
    std::sort(inputIds.begin(), inputIds.end());
    std::sort(hiddenIds.begin(), hiddenIds.end());
    std::sort(outputIds.begin(), outputIds.end()); // output ids are contiguous & assigned in output-index order

    std::unordered_map<int, int> depthCache;
    std::unordered_map<int, int> hiddenDepth;
    int maxHiddenDepth = 0;
    for (int id : hiddenIds)
    {
        int depth = NodeDepth(id, typeById, incomingFrom, depthCache);
        hiddenDepth[id] = depth;
        maxHiddenDepth = std::max(maxHiddenDepth, depth);
    }

    float margin = 30.0f;
    float inputX = area.x + margin;
    float outputX = area.x + area.width - margin;
    float hiddenSpan = outputX - inputX;
    float top = area.y + 40.0f;
    float bottom = area.y + area.height - 20.0f;

    std::unordered_map<int, Vector2> position;
    auto layoutColumn = [&](const std::vector<int> &ids, float x)
    {
        for (size_t i = 0; i < ids.size(); i++)
        {
            float t = ids.size() == 1 ? 0.5f : (float)i / (float)(ids.size() - 1);
            position[ids[i]] = {x, top + t * (bottom - top)};
        }
    };
    layoutColumn(inputIds, inputX);
    layoutColumn(outputIds, outputX);

    // Group hidden nodes by depth so nodes feed into each other roughly
    // left-to-right, then stack same-depth nodes vertically.
    std::unordered_map<int, std::vector<int>> hiddenByDepth;
    for (int id : hiddenIds)
        hiddenByDepth[hiddenDepth[id]].push_back(id);
    int depthColumns = std::max(maxHiddenDepth, 1) + 1;
    for (auto &kv : hiddenByDepth)
    {
        float x = inputX + ((float)std::max(kv.first, 1) / (float)depthColumns) * hiddenSpan;
        layoutColumn(kv.second, x);
    }

    // Connections first, so nodes draw on top.
    for (const auto &c : genome.connections)
    {
        auto fromIt = position.find(c.inNode);
        auto toIt = position.find(c.outNode);
        if (fromIt == position.end() || toIt == position.end())
            continue;

        if (!c.enabled)
        {
            DrawLineEx(fromIt->second, toIt->second, 1.0f, Fade(GRAY, 0.35f));
            continue;
        }
        float thickness = 1.0f + std::min(fabsf(c.weight), 3.0f);
        Color color = c.weight >= 0.0f ? Fade(GREEN, 0.7f) : Fade(RED, 0.7f);
        DrawLineEx(fromIt->second, toIt->second, thickness, color);
    }

    for (int id : inputIds)
        DrawCircleV(position[id], 5.0f, typeById[id] == NodeType::Bias ? GRAY : BLUE);
    for (int id : hiddenIds)
        DrawCircleV(position[id], 6.0f, ORANGE);
    for (size_t i = 0; i < outputIds.size(); i++)
    {
        Vector2 p = position[outputIds[i]];
        DrawCircleV(p, 6.0f, DARKGREEN);
        DrawText(OutputLabel((int)i), (int)(p.x + 10), (int)(p.y - 8), 14, DARKGREEN);
    }

    int enabledCount = (int)std::count_if(genome.connections.begin(), genome.connections.end(),
                                           [](const ConnectionGene &c)
                                           { return c.enabled; });
    DrawText(TextFormat("Genome: %d hidden nodes, %d connections (%d enabled)",
                         (int)hiddenIds.size(), (int)genome.connections.size(), enabledCount),
             (int)area.x + 10, (int)area.y + 10, 16, BLACK);
    DrawText("blue = input, gray = bias, orange = hidden, green = output  |  line green/red = weight +/-",
             (int)area.x + 10, (int)(area.y + area.height - 16), 12, GRAY);
}
