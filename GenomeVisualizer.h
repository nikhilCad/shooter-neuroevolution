#pragma once
#include "raylib.h"
#include "Genome.h"

// Draws a node-link diagram of a genome's current structure inside `area`:
// inputs (plus the bias node) on the left, outputs on the right (labeled
// with what each one means), hidden nodes laid out left-to-right by depth
// in between. Connection color encodes weight sign (green positive, red
// negative), thickness encodes magnitude; disabled connections are drawn as
// thin gray lines. Meant for a live HUD overlay of whichever genome is
// currently playing — layout is recomputed from scratch every call (cheap
// for these tiny genomes) so it always reflects the genome's current shape,
// including nodes/connections a mutation just added this generation.
void DrawGenomeVisualization(const Genome &genome, Rectangle area);
