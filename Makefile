.PHONY: dev sweep recordings

dev:
	cmake -S . -B build && cmake --build build && ./build/raylib_cpp

# Usage: make sweep ARGS="--populations=20,40 --generations=4000 --repeats=4 --seed=42"
sweep:
	cmake -S . -B build && cmake --build build && ./build/raylib_cpp --sweep $(ARGS)

# Trains one fresh population headlessly, snapshotting the all-time-best
# genome at each --checkpoints generation, then immediately replays and
# records a gif for every checkpoint. Checkpoints land in
# recordings/checkpoints/gen_<N>.genome, gifs in recordings/gifs/gen_<N>.gif
# (both gitignored — regenerate anytime by re-running this target).
# Requires ImageMagick (`brew install imagemagick`) for the `magick` CLI.
# Usage: make recordings ARGS="--checkpoints=1,100,1000,4000,10000,25000,50000 --seed=42 --max-seconds=45 --fps=20"
# ARGS is passed to both steps — a flag only one step recognizes (--checkpoints/
# --population for training, --max-seconds/--fps for recording) is simply
# ignored by the other. --seed applies to both but means something different
# in each (training seed vs. replay's enemy-spawn seed — see Checkpoint.h /
# GifRecorder.h); that's fine for the common case. Run
# --checkpoint-train/--record-checkpoint-gifs separately if you need distinct
# seeds or out-dirs for the two steps.
recordings:
	cmake -S . -B build && cmake --build build
	./build/raylib_cpp --checkpoint-train --out-dir=recordings/checkpoints $(ARGS)
	./build/raylib_cpp --record-checkpoint-gifs --checkpoint-dir=recordings/checkpoints --out-dir=recordings/gifs $(ARGS)
