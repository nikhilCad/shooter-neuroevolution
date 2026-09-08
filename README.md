# Space Shooter ML

A twin-stick space shooter where the player is controlled entirely by a hand-rolled NEAT
(NeuroEvolution of Augmenting Topologies) agent — no ML libraries, no gradient descent. The
network's structure and weights are both evolved purely by playing the game.

## Usage

```
make dev    # build and play the game interactively (watch the agent train live)
make sweep  # run a population-size parameter sweep (override with ARGS="--populations=20,40,60,80 --generations=4000 --repeats=4")
```

`make sweep` (re)writes this README's results further down and regenerates `sweep_images/` —
the documentation links below are preserved across that regeneration.

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) — current design: the NEAT genome representation, mutation/crossover/speciation, network inputs and outputs, and how the pieces fit together (with diagrams).

## Archive

Earlier version of this project used a fixed-topology network with a plain (μ+λ) evolution
strategy instead of NEAT. Its architecture, sweep results, and result graphs are preserved for
reference in [archive/v1-fixed-topology/](archive/v1-fixed-topology/):

- [archive/v1-fixed-topology/ARCHITECTURE.md](archive/v1-fixed-topology/ARCHITECTURE.md) — the old architecture doc.
- [archive/v1-fixed-topology/README.md](archive/v1-fixed-topology/README.md) — the old sweep-results report (population × hidden-layer-size grid).
- [archive/v1-fixed-topology/sweep_images/](archive/v1-fixed-topology/sweep_images/) and [archive/v1-fixed-topology/sweep_results.txt](archive/v1-fixed-topology/sweep_results.txt) — the graphs and raw data behind that report.
