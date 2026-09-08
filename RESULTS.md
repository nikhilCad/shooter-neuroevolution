# Parameter Sweep Results

See [ARCHITECTURE.md](ARCHITECTURE.md) for the current NEAT design. An earlier fixed-topology approach's architecture, sweep results, and graphs are preserved in [archive/v1-fixed-topology/](archive/v1-fixed-topology/) for reference.

## Usage

```
make dev    # build and play the game interactively
make sweep  # run this parameter sweep (override with ARGS="--populations=20,40,60,80 --generations=4000 --repeats=4")
```

Each configuration below trained 4 times for 6000 generations each. populations=[80,160] mutationRate=0.15 mutationStrength=0.50

Trained in 2183.3s total (wall clock, running all configs/repeats in parallel across CPU threads).

Median is the primary ranking column — `bestFitness` is already a running-max over thousands of episodes within one run, so a single lucky episode can inflate it; the median across repeats resists that better than the mean does. Mean is shown alongside so an outlier-prone config (mean and median far apart) is visible rather than hidden.

| Population | Repeats | Generations | Median Fitness | Mean Fitness | Median Score | Mean Score | Median Time | Mean Time | Total Train Time (s) |
|---|---|---|---|---|---|---|---|---|---|
| 80 | 4 | 6000 | 2731.2 | 2595.9 | 11550 | 11025.0 | 147.2 | 140.5 | 2994.3 |
| 160 | 4 | 6000 | 3449.7 | 3189.3 | 14300 | 13300.0 | 181.3 | 169.6 | 6726.5 |

## pop80

![pop80](sweep_images/pop80.png)

## pop160

![pop160](sweep_images/pop160.png)

## Input usage

Average |weight| of enabled connections from each input, across every genome in every config/repeat's final population — a rough "how much does the evolved population actually rely on this input" signal. An input sitting near zero here across the whole sweep is a candidate to drop from `GetPlayerState` (fewer inputs means fewer connections for every genome to evaluate, speeding up every `Activate` call) — but check this holds up across more than one sweep before cutting anything, since a single run's population can converge on ignoring a genuinely useful input just by chance.

**Most relied on:** `nearbyEnemyCount` (avg |weight| 9.398). **Least relied on:** `player.y` (avg |weight| 0.488).

| Input | Avg \|weight\| | Samples |
|---|---|---|
| nearbyEnemyCount | 9.398 | 1175 |
| enemy#1.distance | 7.874 | 942 |
| enemy#1.bodyY | 6.622 | 1170 |
| enemy#1.health | 3.391 | 671 |
| enemy#3.closingSpeed | 2.337 | 562 |
| enemy#1.closingSpeed | 1.926 | 671 |
| player.touchCooldown | 1.848 | 634 |
| player.health | 1.810 | 889 |
| enemy#1.bodyVY | 1.544 | 749 |
| enemy#3.health | 1.106 | 713 |
| forwardConeEnemyCount | 1.067 | 651 |
| player.velocityY | 1.009 | 630 |
| player.wallDistX | 0.974 | 562 |
| player.x | 0.856 | 562 |
| enemy#4.closingSpeed | 0.748 | 626 |
| enemy#1.bodyVX | 0.735 | 571 |
| enemy#3.bodyVX | 0.668 | 584 |
| enemy#1.bodyX | 0.590 | 566 |
| enemy#4.bodyX | 0.584 | 528 |
| enemy#4.bodyY | 0.576 | 589 |
| enemy#3.distance | 0.572 | 626 |
| player.facingSin | 0.569 | 606 |
| enemy#2.distance | 0.568 | 520 |
| enemy#4.bodyVX | 0.565 | 559 |
| enemy#2.bodyVY | 0.557 | 527 |
| player.velocityX | 0.556 | 598 |
| enemy#3.bodyY | 0.552 | 515 |
| enemy#4.distance | 0.551 | 551 |
| enemy#2.closingSpeed | 0.528 | 521 |
| enemy#2.bodyX | 0.526 | 495 |
| player.facingCos | 0.524 | 655 |
| enemy#4.bodyVY | 0.515 | 564 |
| enemy#2.health | 0.512 | 573 |
| enemy#3.bodyX | 0.507 | 567 |
| enemy#2.bodyY | 0.504 | 569 |
| player.wallDistY | 0.504 | 498 |
| enemy#4.health | 0.502 | 486 |
| enemy#3.bodyVY | 0.494 | 495 |
| enemy#2.bodyVX | 0.489 | 513 |
| player.y | 0.488 | 516 |

## Fittest genome overall

`pop160` run 1 — fitness 4834.2, score 19600, time 249.1

![fittest genome](sweep_images/fittest_genome.png)

### Diagnostics for that run

**Reward breakdown** (of that fittest episode's 4834.2 total): survival 12.5, hits 1182.0, kills 3920.0, touch penalty -250.0, death penalty -30.0.

**Shot accuracy:** 591/1150 (51.4%).

**Species count:** 1 at the final generation (ranged 1-1 over the run).

**Population complexity at the final generation:** avg 23.1 hidden nodes, avg 33.0 enabled connections per genome.

**Deepest stagnation reached:** 11 generations without improving (mutation/structural-mutation boost peaked around 1.2x).

