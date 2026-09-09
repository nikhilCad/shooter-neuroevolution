# Input changelog

A running log of every change to the network's input vector (`GetPlayerState` /
`PLAYER_AGENT_*` constants in [PlayerAgent.h](PlayerAgent.h)) and the reasoning behind it.

**This file is append-only.** When the input vector changes again, add a new dated
entry below the existing ones — don't rewrite or remove past entries, even if a later
change supersedes an earlier one (note the supersession in the new entry instead).
Each entry that changes the input *count* also bumps `SAVE_VERSION` in
[Evolution.cpp](Evolution.cpp), so a save file's version log doubles as a rough
timeline for these changes too.

---

## Baseline (28 inputs)

3 player features (x, y, health) + 6 features × 4 nearest enemies (dx, dy, distance,
health, velocity x/y, all in world/screen space) + 1 aggregate (active enemy count).

**Why:** the original input set — enough for the network to see the player's own state
and the immediate threats around it, with enemy velocity already included so aiming
could in principle account for target movement (whether it actually learned to use it
well is a separate question from whether it could).

## Body-frame rotation + player state additions (28 → 34 inputs)

Added: player velocity (x, y), player facing (cos, sin of rotation), and two wall-
distance features (nearest x-wall, nearest y-wall). Also rotated every enemy's
position and velocity into the player's own facing frame (`bodyX`/`bodyY`/`bodyVX`/
`bodyVY` instead of raw world dx/dy/vx/vy) — same feature count per enemy (6), just
re-expressed.

**Why:**
- Movement gained inertia (acceleration-based easing instead of instant velocity) as
  part of a physics-glitch fix, so the network needed to see its own current velocity
  to know it wasn't instantly free to change direction.
- Aim gained a turn-rate cap (gradual rotation instead of snapping) for the same
  reason, so the network needed to know its current facing to judge how much turning
  error remained.
- Wall distance gives an explicit "am I cornered" signal instead of forcing the
  network to infer it from raw x/y position.
- Body-frame enemy features make "is this enemy ahead of my gun" a direct feature
  instead of something the network has to reconstruct by combining raw enemy position
  with its own absolute rotation — the same aim-error signal, regardless of which way
  the player happens to be facing.

## NEAT rewrite (34 inputs, unchanged)

Switching from a fixed-topology dense network to NEAT (variable-topology genomes,
speciation, crossover) did not change the input vector at all — same 34 features,
same meanings. Only the network *representation* changed (graph of node/connection
genes instead of a flat weight vector for a fixed dense layer).

## Touch-cooldown, closing speed, local threat density (34 → 40 inputs)

Added: touch-damage cooldown remaining (1 player feature — `episode.enemyTouchTimer /
ENEMY_TOUCH_COOLDOWN`, clamped to [0,1]), per-enemy closing speed (1 more feature per
enemy slot, so 6 → 7 per enemy), and nearby-enemy count within a fixed radius (a 2nd
aggregate feature, alongside the existing total active-enemy count).

**Why:**
- Touch-cooldown remaining: the network could otherwise only ever see the *consequence*
  of a touch (health dropping) with no way to know it currently has a brief
  post-touch grace window — could unlock more aggressive plays right after taking a hit.
- Closing speed (component of an enemy's velocity aimed straight at the player,
  positive = approaching): raw distance and body-frame velocity alone don't
  distinguish "a fast enemy far away" from "a slow enemy nearby" — closing speed
  captures the urgency directly.
- Nearby-enemy count: the existing active-enemy-count aggregate counts everything on
  screen regardless of proximity; a localized "am I currently surrounded" signal is a
  much sharper input for a "am I in danger right now" decision than a global count.

## Dropped active-enemy count, added forward-cone enemy count (40 → 40 inputs, same count)

Removed: total active-enemy count (the aggregate added in the baseline set). Added:
forward-cone enemy count — how many active enemies (checked against all of them, not
just the 4 detailed slots) sit within `PLAYER_AGENT_FORWARD_CONE_COS` (cos of 20°) of
the player's current aim direction, i.e. roughly "would committing to this aim
direction actually line up a shot."

**Why:**
- A parameter sweep (populations=[40,80,160], 6000 generations, 4 repeats —
  see `RESULTS.md`'s "Input usage" section from that run) measured average `|weight|`
  of enabled connections from every input, aggregated across every genome in every
  config/repeat's final population. `nearbyEnemyCount` (added in the entry above) came
  out as the single most relied-on input in the entire 40-input set by a wide margin
  (avg |weight| 10.8, ~2x the next-highest). Its sibling `activeEnemyCount` — the
  original, non-localized version of essentially the same idea — came out near the
  *bottom* of the same ranking (avg |weight| 0.58, ~19x lower than `nearbyEnemyCount`).
  That direct sibling comparison was the clearest, best-evidenced removal signal seen
  across any sweep so far, so `activeEnemyCount` was cut rather than kept as dead
  weight in every genome's input layer.
- Forward-cone count was added in its place rather than just shrinking the input
  vector, motivated by a separate behavioral observation: the agent visibly hesitates
  ("dances") around clusters of enemies instead of confidently engaging them. Bullets
  are single-target (no piercing), so a cluster's only real value is positional — kill
  them one at a time from a good stand-off distance instead of getting surrounded —
  but nothing in the input set told the network "if I keep aiming this way, how many
  enemies are roughly in that line," which requires comparing multiple enemies'
  positions against each other, a nontrivial reconstruction for the fairly shallow
  genomes observed so far. This is a direct, cheap-to-compute version of that signal.
- Deliberately not hardcoded as a behavior (e.g. "always aim at the biggest cluster")
  — that would defeat the point of an evolved policy and could easily be wrong in
  some situations (a cluster can also mean "retreat," not "commit"). The input just
  makes the *option* to reason about lined-up shots directly available; what to do
  with it is left to evolution.

## Added reverse-move-cone enemy count (40 → 41 inputs)

Added: reverse-move-cone enemy count — how many active enemies sit within the same
`PLAYER_AGENT_FORWARD_CONE_COS` cone width as the existing forward-cone feature, but
centered on the direction directly opposite the player's *current movement*, not its
aim/facing. Zero while near-stationary (no movement direction to be "opposite" of).

**Why:** watching a trained agent (gen 50000 of a long run), it would consistently
retreat from a frontal threat, then circle around to *face* a trailing enemy before
shooting it, instead of just snap-aiming backward and shooting while continuing to
flee — even though `move` and `aim` are fully independent outputs, so that behavior
costs nothing movement-wise. The existing forward-cone input is relative to aim/
facing, not travel direction, so there was no signal at all for "something's
positioned such that I could shoot it right now without changing course." This adds
that signal directly rather than leaving the network to reconstruct it (comparing
enemy position against velocity/movement, not just facing) on its own.
