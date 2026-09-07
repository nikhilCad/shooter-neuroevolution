#include "HistoryGraph.h"
#include <algorithm>

float GetHistoryPanelsTotalHeight()
{
    return HISTORY_TITLE_HEIGHT + HISTORY_PANEL_HEIGHT * 3 + HISTORY_PANEL_GAP * 2 + HISTORY_PANEL_MARGIN;
}

void DrawHistoryGraph(Rectangle area, const std::vector<float> &history, const char *label,
                      Color color, Color maxColor)
{
    DrawRectangleRec(area, Fade(BLACK, 0.05f));
    DrawRectangleLinesEx(area, 1, DARKGRAY);
    DrawText(label, (int)area.x + 6, (int)area.y + 4, 16, BLACK);
    DrawText("value", (int)area.x + area.width - 90, (int)area.y + 4, 14, color);
    DrawText("best so far", (int)area.x + area.width - 40, (int)area.y + 4, 14, maxColor);

    if (history.size() < 2)
    {
        DrawText("Not enough generations yet", (int)area.x + 6, (int)(area.y + area.height / 2), 14, GRAY);
        return;
    }

    std::vector<float> runningMax(history.size());
    runningMax[0] = history[0];
    for (size_t i = 1; i < history.size(); i++)
        runningMax[i] = std::max(runningMax[i - 1], history[i]);

    float minV = history[0];
    float maxV = history[0];
    for (float v : history)
    {
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }
    if (maxV - minV < 0.001f)
        maxV = minV + 1.0f; // avoid a flat divide-by-zero range

    float graphTop = area.y + 24.0f;
    float graphHeight = area.height - 30.0f;

    auto toPoints = [&](const std::vector<float> &values)
    {
        std::vector<Vector2> points(values.size());
        for (size_t i = 0; i < values.size(); i++)
        {
            float t = (float)i / (float)(values.size() - 1);
            float normalized = (values[i] - minV) / (maxV - minV);
            points[i].x = area.x + t * area.width;
            points[i].y = graphTop + graphHeight - normalized * graphHeight;
        }
        return points;
    };

    std::vector<Vector2> maxPoints = toPoints(runningMax);
    DrawLineStrip(maxPoints.data(), (int)maxPoints.size(), maxColor);

    std::vector<Vector2> valuePoints = toPoints(history);
    DrawLineStrip(valuePoints.data(), (int)valuePoints.size(), color);

    DrawText(TextFormat("max %.1f", maxV), (int)area.x + 6, (int)graphTop - 2, 12, GRAY);
    DrawText(TextFormat("min %.1f", minV), (int)area.x + 6, (int)(graphTop + graphHeight - 10), 12, GRAY);
}

void DrawGenerationHistoryPanels(int canvasWidth, const Evolution &evolution, const char *title)
{
    float panelWidth = canvasWidth - HISTORY_PANEL_MARGIN * 2;

    DrawText(title, (int)HISTORY_PANEL_MARGIN, 10, 20, BLACK);

    Rectangle fitnessArea = {HISTORY_PANEL_MARGIN, HISTORY_TITLE_HEIGHT, panelWidth, HISTORY_PANEL_HEIGHT};
    Rectangle scoreArea = {HISTORY_PANEL_MARGIN, fitnessArea.y + HISTORY_PANEL_HEIGHT + HISTORY_PANEL_GAP, panelWidth, HISTORY_PANEL_HEIGHT};
    Rectangle timeArea = {HISTORY_PANEL_MARGIN, scoreArea.y + HISTORY_PANEL_HEIGHT + HISTORY_PANEL_GAP, panelWidth, HISTORY_PANEL_HEIGHT};

    DrawHistoryGraph(fitnessArea, evolution.fitnessHistory, "Fitness per generation", RED, GOLD);
    DrawHistoryGraph(scoreArea, evolution.scoreHistory, "Score per generation", BLUE, GOLD);
    DrawHistoryGraph(timeArea, evolution.timeHistory, "Time survived per generation", DARKGREEN, GOLD);
}

bool ExportGenerationHistoryImage(const Evolution &evolution, const char *title, const char *imagePath)
{
    int canvasWidth = 820;
    int canvasHeight = (int)GetHistoryPanelsTotalHeight() + 20;

    RenderTexture2D target = LoadRenderTexture(canvasWidth, canvasHeight);
    BeginTextureMode(target);
    ClearBackground(RAYWHITE);
    DrawGenerationHistoryPanels(canvasWidth, evolution, title);
    EndTextureMode();

    Image image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image); // render textures are stored bottom-up in OpenGL
    bool ok = ExportImage(image, imagePath);
    UnloadImage(image);
    UnloadRenderTexture(target);
    return ok;
}
