#pragma once
#include <cstdint>
#include <string>

// Replays one saved genome's episode (deterministically seeded, same
// SimulateStep the interactive game and PlayEpisode use) and records it to
// an animated GIF — for visually comparing "how good was the best genome at
// generation N" across a run's checkpoints (see Checkpoint.h). Captures
// frames to temporary PNGs under a render texture (the same offscreen
// pattern HistoryGraph/GenomeVisualizer use for their image exports) and
// shells out to ImageMagick's `magick` to assemble them, since there's no
// GIF encoder anywhere else in this codebase and vendoring one for a
// dev-tooling feature isn't worth it. Requires `magick` on PATH.
struct GifRecordOptions
{
    std::string genomePath;
    std::string outputGifPath;
    uint64_t seed = 1;
    // Longest an episode ever needs to run at fast-forward speed to prove
    // the point visually — good genomes can survive minutes, but a 4-5
    // minute-long gif is a poor way to look at one, so recording just stops
    // here even if the episode is still going.
    float maxSeconds = 45.0f;
    int captureFps = 20;
};

// Recognized flags: --record-gif=<genomeFile> --out=<outputGif>
//                    --seed=1 --max-seconds=45 --fps=20
GifRecordOptions ParseGifRecordOptions(int argc, char **argv);

// Requires an OpenGL context — call after InitWindow (a hidden one is fine;
// see main.cpp's --record-gif branch).
bool RecordGenomeGif(const GifRecordOptions &options);

// Batch form: every checkpointDir/gen_*.genome gets its own
// outDir/gen_*.gif, all under the same seed/maxSeconds/fps so the
// recordings are directly comparable across milestones. Also requires an
// OpenGL context (opens one itself, hidden, if none is active).
void RecordCheckpointGifs(const std::string &checkpointDir, const std::string &outDir,
                           uint64_t seed, float maxSeconds, int captureFps);
