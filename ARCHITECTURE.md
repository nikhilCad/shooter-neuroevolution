# Architecture

## Stack

- C++17, raylib for window/input/drawing/shapes
- CMake, Makefile wrapper (`make dev`, `make sweep`)
- no ML libraries. NEAT (neuroevolution) — genome representation, mutation, crossover, speciation, forward pass — is all hand-rolled
- std::thread for the sweep, nothing else external

## ML approach

- neuroevolution via **NEAT** (NeuroEvolution of Augmenting Topologies), not gradient-based RL. no backprop, no loss function, no policy gradient.
- a genome is a variable-topology graph: node genes + connection genes (weight, enabled flag, innovation number), not a fixed-size weight vector. structure starts minimal (every input wired straight to every output, zero hidden nodes) and only grows via mutation.
- **structural mutation**: `MutateAddConnection` wires up two previously-unconnected nodes (rejecting anything that would create a cycle — the graph must stay feed-forward); `MutateAddNode` splits an existing connection with a new hidden node, whose outgoing weight starts at exactly `0.0` — a freshly grown node has zero effect on behavior the instant it appears, so it can never make a genome look better or worse until later weight mutations actually give it something to say.
- **innovation numbers**: every connection gene's historical origin is tracked (see `InnovationTracker` in [Genome.h](Genome.h)), so if two different genomes independently evolve "the same" structural change, they get the same innovation number. That's what makes `Crossover` meaningful — two genomes' genes are aligned by innovation number instead of by position.
- **speciation**: genomes are grouped into species by `GeneticDistance` (excess/disjoint gene counts + average weight difference). Each species mostly reproduces within itself, which is what protects a brand-new structural mutation long enough to have its weights tuned before it has to compete head-to-head with a fully-optimized genome from a different lineage. A species' champion is carried over unchanged once it has enough members; one all-time-best genome is preserved regardless of what happens to its species.
- fitness = reward from actually playing the episode (survival + hits + kills - death/touch penalties).
- mutation rate/strength — and how often structural mutations fire — all auto-ramp up when fitness stalls, back down once it improves.

## Network inputs and outputs

The brain is a NEAT genome evaluated as a feed-forward graph (topological evaluation with memoization — see `Activate` in [Genome.cpp](Genome.cpp)), not a fixed dense layer. Hidden nodes only exist if mutation has grown them; a brand new genome has none. Sizes are computed from `PLAYER_AGENT_*` constants in [PlayerAgent.h](PlayerAgent.h), so they always match what `GetPlayerState`/`DecidePlayerAction` in [PlayerAgent.cpp](PlayerAgent.cpp) actually build/decode.

**Inputs — 40 total** (`PLAYER_AGENT_INPUT_SIZE`): 10 player features, 7 features for each of the 4 nearest enemies (closest first), 2 aggregate features.

| # | Feature | Normalization |
|---|---|---|
| 0 | player x | `/ screenWidth` |
| 1 | player y | `/ screenHeight` |
| 2 | player health | `/ maxHealth` |
| 3 | player velocity x | `/ player.speed` |
| 4 | player velocity y | `/ player.speed` |
| 5 | facing cos | `GetAimDirection(player).x` |
| 6 | facing sin | `GetAimDirection(player).y` |
| 7 | nearest x-wall distance | `/ (screenWidth / 2)`, 0 = touching, 1 = center |
| 8 | nearest y-wall distance | `/ (screenHeight / 2)`, 0 = touching, 1 = center |
| 9 | touch-damage cooldown remaining | `episode.enemyTouchTimer / ENEMY_TOUCH_COOLDOWN`, clamped to [0,1] |
| 10–16 | nearest enemy: bodyX, bodyY, distance, health, bodyVX, bodyVY, closing speed | position/velocity rotated into the player's facing frame (see below), `/ screen diagonal`; health `/ maxHealth`; closing speed `/ enemy.speed` |
| 17–23 | 2nd-nearest enemy: same 7 features | same |
| 24–30 | 3rd-nearest enemy: same 7 features | same |
| 31–37 | 4th-nearest enemy: same 7 features | same |
| 38 | active enemy count | `min(count / 10, 1.0)` |
| 39 | nearby enemy count (within `PLAYER_AGENT_LOCAL_THREAT_RADIUS`) | `min(count / 5, 1.0)` |

Enemy position and velocity are rotated by `-player.rotation` before being fed in, so `bodyX` is "how far ahead of my gun" and `bodyY` is "how far to the side" — a direct aim-error signal that doesn't depend on which way the player happens to be facing. Closing speed is the component of the enemy's velocity aimed straight at the player (positive = approaching), which a fast-but-distant enemy can score higher on than a slow-but-close one — a distinction raw distance and body-frame velocity don't capture on their own. An empty enemy slot (fewer than 4 enemies alive) zeroes its 7 features except distance, which is set to `1.0` ("maximally far away") so the network can tell "no enemy here" apart from "an enemy is very close." The nearby-enemy-count aggregate is a localized "am I currently surrounded" signal, distinct from the total-active-count aggregate which counts everything on screen regardless of proximity.

**Outputs — 5 total** (`PLAYER_AGENT_OUTPUT_SIZE`), decoded in `DecidePlayerAction`:

| # | Meaning | Activation |
|---|---|---|
| 0 | move.x | `tanh` |
| 1 | move.y | `tanh` |
| 2 | aim.x | `tanh` |
| 3 | aim.y | `tanh` |
| 4 | shoot | `sigmoid > 0.5` |

If both aim outputs are ~0, the player keeps its current facing rather than snapping to an undefined direction. Movement eases toward the commanded direction (capped acceleration) and aim turns toward the commanded direction (capped turn rate) instead of either snapping instantly — see `UpdatePlayer` in [Player.cpp](Player.cpp).

```mermaid
flowchart LR
    subgraph Inputs["Input layer — 40 nodes"]
        direction TB
        P["Player — 10<br/>x, y, health, vx, vy,<br/>facing cos/sin, wall dist x/y,<br/>touch-cooldown remaining"]
        E0["Nearest enemy #1 — 7<br/>bodyX, bodyY, dist, health,<br/>bodyVX, bodyVY, closing speed"]
        E1["Nearest enemy #2 — 7"]
        E2["Nearest enemy #3 — 7"]
        E3["Nearest enemy #4 — 7"]
        C["Active count + nearby count — 2"]
    end
    Inputs --> G["Hidden structure — grows from zero<br/>via MutateAddNode/MutateAddConnection"]
    G --> Outputs

    subgraph Outputs["Output layer — 5 nodes"]
        direction TB
        M["move.x, move.y — tanh"]
        A["aim.x, aim.y — tanh"]
        S["shoot — sigmoid > 0.5"]
    end
```

A minimal genome skips the hidden structure entirely — every input (plus an always-on bias node) connects straight to every output.

**Evolution loop**, one generation:

```mermaid
flowchart TD
    A[Genome i plays one episode via SimulateStep] --> B[FinishEpisode records fitness/score/time]
    B --> C{All genomes<br/>in population played?}
    C -- no --> A
    C -- yes --> D[EvolvePopulation]
    D --> E[SpeciatePopulation:<br/>group genomes by GeneticDistance]
    E --> F["Per species: champion copied unchanged<br/>(if species is large enough)"]
    E --> G["Per species: crossover or clone a parent,<br/>then mutate (weights + maybe structure)"]
    E --> H["All-time-best genome preserved<br/>+ a few brand-new random immigrants"]
    F --> I[Next generation]
    G --> I
    H --> I
    I --> A
```

Each species' offspring count is proportional to its total fitness-shared fitness (dividing by species size, so a small young species isn't swamped by one big one). A species stuck for 15+ generations without improving stops getting offspring — unless it's the species currently holding the best genome, which is never fully extinguished. Immigrant count, mutation rate/strength, and structural mutation chance all scale up the longer the whole population has gone without improving (see `stagnationBoost` in [Evolution.cpp](Evolution.cpp)).

## Files

**main.cpp** — entry point. picks `--sweep` or the interactive loop. owns the window, HUD, speed button, autosave timing. Hold Tab for the fitness/score/time graphs, hold N for the currently-playing genome's network diagram.

**Simulation.h/.cpp** — all the game constants (player/bullet/enemy/reward/evolution defaults) and `SimulateStep`, one game tick: movement, shooting, collisions, reward, episode end. Takes a genome directly and knows nothing about `Evolution` — it returns whether the episode just ended (the player died) and leaves recording that outcome and resetting for the next episode to the caller. `PlayEpisode` plays one genome through a full episode start-to-finish in its own self-contained state, safe to call from any thread as long as no two threads ever touch the same genome at once (used by the sweep to evaluate a whole generation in parallel).

**Player.h/.cpp, Enemy.h/.cpp** — plain structs + free functions, no classes. position, health, collision boxes, drawing. Enemies spawn at a random angle around the player's *current* position at a fixed radius, not at a random point along a fixed screen edge — a fixed-radius-from-player spawn means hiding in a corner doesn't increase the average spawn-to-player travel distance the way a fixed-screen-edge spawn did, which was making corner-camping an easy way to thin out how many enemies are in range at once.

**GenomeVisualizer.h/.cpp** — `DrawGenomeVisualization`, the node-link diagram of a genome's structure (inputs/bias left, outputs right, hidden nodes laid out by depth in between) shown by the N-key HUD overlay.

**Genome.h/.cpp** — the NEAT genome: node/connection genes, innovation tracking, mutation (weights, add-connection, add-node), crossover, `GeneticDistance`, and `Activate` (the feed-forward graph evaluator that replaces a fixed dense-layer forward pass).

**PlayerAgent.h/.cpp** — glue between game state and the network. builds the input vector (player + 4 nearest enemies, enemy features rotated into the player's facing frame), decodes outputs into move/aim/shoot.

**Evolution.h/.cpp** — population, speciation, per-species reproduction (crossover + mutation), stagnation-triggered mutation/structural-mutation boost. save/load to `save.dat` — versioned, deliberately refuses to load a file with the wrong shape (species aren't persisted — they're cheap to rebuild from scratch on the first generation after a resume).

**HistoryGraph.h/.cpp** — the fitness/score/time line graphs. same drawing code renders the in-game Tab overlay and the sweep's exported PNGs.

**ParameterSweep.h/.cpp** — `--sweep` mode. CLI flag parsing, writes `sweep_results.txt` + `RESULTS.md` + `sweep_images/`. Two levels of parallelism, sized to never oversubscribe the machine: outer worker threads each run one whole `(config, repeat)` training run to completion (`RunConfigsInParallel`), and within each run, every genome in a generation is independent until fitness is aggregated, so `RunSweepConfig` fans a whole generation's `PlayEpisode` calls out across inner threads too. The inner thread budget is `hardware_concurrency() / outer thread count`, so a sweep with enough `(config, repeat)` tasks to already saturate every core gets an inner budget of 1 (pure outer parallelism, unchanged from before), while a sweep with only one or two tasks — which used to leave most cores idle — puts the rest of the machine to work inside that single run instead.

**RandomUtil.h** — thread-local RNG. exists because the sweep runs configs in parallel and raylib's `GetRandomValue` isn't thread-safe.

## Why it's split this way

- `SimulateStep` doesn't know or care if anyone's watching — same function runs live in the window at 1x-49152x speed, or headless on a sweep thread.
- `SimulateStep` also doesn't know or care about `Evolution` — it takes a genome directly and just reports back when an episode ends, instead of reaching into an `Evolution` to find "the current genome" and advance it. That's what lets the sweep play many genomes' episodes on many threads at once (`PlayEpisode`): each thread's game state and genome are its own, with no shared `Evolution` object being mutated concurrently.
- game logic never calls raylib drawing functions, only `CheckCollisionRecs` (pure math, safe off the main thread).
- sweep training runs fully parallel with no window at all. the window only opens afterward, on the main thread, to export PNGs — raylib's GL calls aren't thread-safe so that part has to stay single-threaded.
- `Genome`'s algorithms (mutation, crossover, distance, evaluation) know nothing about the game — `PlayerAgent` is the only file that translates between "player + enemies" and "a vector of floats," so the ML core is reusable for a completely different game with zero changes.
