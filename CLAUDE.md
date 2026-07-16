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

Walking-skeleton build scaffold only: one library target, one smoke test,
one smoke benchmark. No game logic yet.

## Immediate next step

Stage A of InitialPlan.md: freeze the high-level design (RNG stream
architecture, state struct layout, action-queue semantics) and build the
walking skeleton — one enemy, five cards, full RNG plumbing, the batch
`advance(states[], actions[])` API, and the diff harness wired end to end.
This proves the architecture before mass card implementation begins.
