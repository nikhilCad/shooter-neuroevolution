#pragma once
#include "raylib.h"
#include "Evolution.h"
#include <vector>

// Shared layout for the fitness/score/time-over-generations graphs, used by
// both the in-game Tab overlay and the sweep's exported PNG summaries.
static const float HISTORY_PANEL_MARGIN = 20.0f;
static const float HISTORY_PANEL_HEIGHT = 150.0f;
static const float HISTORY_PANEL_GAP = 20.0f;
static const float HISTORY_TITLE_HEIGHT = 40.0f;

float GetHistoryPanelsTotalHeight();

// Draws one line graph of a per-generation metric inside `area`, auto-scaling
// the y-axis to the history's own min/max, plus a second line tracking the
// running best-so-far value in a different color.
void DrawHistoryGraph(Rectangle area, const std::vector<float> &history, const char *label,
                      Color color, Color maxColor);

// Draws the title plus the three stacked history graphs onto whatever the
// current draw target is (the screen, or an offscreen render texture).
void DrawGenerationHistoryPanels(int canvasWidth, const Evolution &evolution, const char *title);

// Renders the three history graphs to an offscreen texture and saves them as
// a PNG. Requires a live GL context (InitWindow must already have been
// called), even though nothing is shown on screen.
bool ExportGenerationHistoryImage(const Evolution &evolution, const char *title, const char *imagePath);
