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

Stage A in progress, tracked in [docs/stage-a-tasks.md](docs/stage-a-tasks.md)
against the frozen [docs/stage-a-design.md](docs/stage-a-design.md). **Gate G1
passed** (tag `g1-rng-green`): the tier-1 RNG trio is bit-exact and golden-
tested — `include/sts/engine/rng_xs128.hpp` (libGDX xorshift128+),
`rng_jdk.hpp` (JDK LCG + `Collections.shuffle`), `rng_stream.hpp` (the game's
`Random` wrapper: inclusive-bound draws, counter-restore semantics,
floor/act stream derivation), `seed_string.hpp` (base-35 seed codec). Golden
vectors captured via `tools/golden_capture/` (Windows-host JVM harness
against `sts-classes.jar`) live under `tests/golden/`. 18/18 gtest cases
green in both `debug` and `asan` presets; CI (`.github/workflows/ci.yml`)
runs both as a matrix on every push/PR.

No state structs, action queue, cards, or batch API yet — that's Phase 2+.

## Immediate next step

Phase 2 of docs/stage-a-tasks.md: `CombatState`/`RunState` structs (A2.1,
A2.2), building on the now-frozen RNG layer. Then Phase 3 (action-queue
pump + Jaw Worm AI), Phase 4 (effect interpreter + five skeleton cards),
Phase 5 (batch `advance()` + observation encoder), Phase 6 (diff harness +
M1 acceptance at gate G3). See docs/stage-a-tasks.md's parallelism map for
what can run concurrently.
