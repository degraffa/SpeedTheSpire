# SpeedTheSpire

A headless, high-performance Slay the Spire simulator in C++. This repo is
the simulator **only** — no rendering, no UI, no training/agent code. The
training program (MCTS, imitation learning, the meta-value function) that
will eventually run on top of this simulator lives in a separate, not-yet-
created repo.

(Note: the folder on disk is still named `StockfishTheSpire` pending a
manual rename by the user — the code/namespace/build all already use the
new name.)

Full design rationale, RNG/state architecture, verification strategy, and
milestones: see [InitialPlan.md](InitialPlan.md). That document is the
source of truth for design decisions; this file is just the orientation
layer.

## Setup

- **Build system**: CMake ≥ 3.21, C++20. Dependencies (GoogleTest, Google
  Benchmark) are fetched via `FetchContent` — no external package manager.
- **Dev environment**: WSL2, Ubuntu 24.04 (distro `Ubuntu-2404`, installed on
  the `E:` drive — NVMe, not the system drive — set as the default WSL
  distro). GCC 13 / Clang 18 / CMake 3.28 / Ninja installed. Default WSL user
  is `alex` with passwordless sudo.
- **Build/test**:
  ```bash
  cmake --preset debug        # or release, asan
  cmake --build --preset debug
  ctest --preset debug
  ```
- **Layout**: `include/`+`src/engine/` (the simulator library), `tests/`
  (GoogleTest), `benchmarks/` (Google Benchmark), `registry/` (rules-as-data
  YAML/CSV, currently empty), `tools/oracle_bridge/` (CommunicationMod
  differential-testing harness, not yet built).

## Current state

**Stage A / milestone M1 complete** (tag `m1-walking-skeleton`; gate `G3`
passed in [docs/stage-a-tasks.md](docs/stage-a-tasks.md), tracked against
the frozen [docs/stage-a-design.md](docs/stage-a-design.md)). The walking
skeleton is a fully playable, bit-exact Ironclad-vs-Jaw-Worm combat:

- RNG trio bit-exact and golden-tested (gate `G1`, tag `g1-rng-green`):
  `rng_xs128.hpp`, `rng_jdk.hpp`, `rng_stream.hpp`, `seed_string.hpp`.
- `CombatState`/`RunState` (POD, memcpy-snapshot, xxh3-hashed) —
  `combat_state.hpp`, `run_state.hpp`, `state_hash.hpp`.
- Action-queue pump (`action_queue.hpp/.cpp`) implementing the game's
  `getNextAction` priority order, wired to an effect interpreter
  (`interp.hpp/.cpp`: DAMAGE/BLOCK/APPLY_POWER/etc, the float damage
  pipeline with Strength/Vulnerable/Weak) and pile ops (`piles.hpp/.cpp`:
  draw/discard/reshuffle, JDK-shuffle-exact).
- Jaw Worm AI (`monster_jaw_worm.hpp/.cpp`) — bit-exact move selection and
  real combat effects (Chomp/Bellow/Thrash).
- The five skeleton cards + card-play flow (`cards.hpp`, `card_play.hpp/.cpp`).
- The batch API (`advance.hpp/.cpp`): `advance()`, `legal_actions()`,
  `combat_begin()` — the only public entry point, per InitialPlan D0.1.
- Observation encoding (`observation.hpp`) and a diff/trace harness
  (`tools/diff_harness/`) verified against 20 independently-derived
  scripted-fight fixtures (`tools/fixture_gen/`,
  `tests/golden/combat_fixtures/`) with zero diffs.

131/131 gtest cases green in `debug`, `asan`, and `release` presets; all 11
design-doc §10 bit-exactness traps have a named passing test; CI
(`.github/workflows/ci.yml`) runs debug+asan as a matrix on every push/PR.

No run layer yet (map/events/shops/relics/potions) — that's Stage B, per
InitialPlan §B.1 (oracle bridge first).

## Immediate next step

Stage B planning: the CommunicationMod oracle bridge (InitialPlan §B.1) is
the first deliverable — it's what every later differential test rides on.
See InitialPlan.md Part 1 Stage B for the four verification tiers and
`docs/stage-a-design.md` §11 for what was deliberately deferred out of the
M1 skeleton (event/shop/relic mechanics, map generation, A20 modifier
table, potion identity).
