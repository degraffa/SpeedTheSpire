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

**Stage B in progress** — registry track complete through gate **G5**
(tag `g5-registry-live`), tracked in
[docs/stage-b-tasks.md](docs/stage-b-tasks.md) against the frozen
[docs/stage-b-design.md](docs/stage-b-design.md). Landed so far:

- **B0.1** — CommunicationMod v1.2.1 vendored source-only (MIT) under
  `tools/oracle_bridge/communicationmod-oracle/`, with
  `tools/oracle_bridge/PROTOCOL.md` cataloguing every `GameStateConverter`
  field. (Bridge groundwork; the rest of the bridge track — B0.2, B1.x, gate
  **G4** — is not yet done and **needs the live game** running on the Windows
  host, so it can't run headless/unattended.)
- **B2.1 + B2.2 + G5** — the registry is live and is now the single source of
  truth for content ids/tables. `registry/*.yaml` (8 domains, currently the
  5 skeleton cards + 3 powers + Jaw Worm; other 5 domains valid-but-empty) is
  compiled by `tools/registry_gen/gen.py` (Python 3 + PyYAML) into constexpr
  headers under the build tree (`<build>/generated/sts/registry/*.hpp`, never
  committed). `types.hpp`/`cards.hpp`/`monster_jaw_worm.*` now **re-export**
  the generated enums/tables into `sts::engine` (no hand tables left — no dual
  system); ids are append-only and `static_assert`-pinned. CI installs
  `python3-yaml` and runs the generator.

All **140** gtest cases green in `debug`, `asan`, and `release` (Stage A's 131
+ 9 registry-gen cases); `SCHEMA_VERSION`=1 and `sizeof(CombatState)`=3504
unchanged across the migration. **Next:** the oracle bridge track (B0.2 →
B1.1–B1.6 → **G4**), which requires the live Slay the Spire game (JDK-8 fork
build + interactive seeded runs); Phases B3/B4 (S1 content) are gated on
**both** G4 and G5.

---

**Stage A / milestone M1** (tag `m1-walking-skeleton`; gate `G3`
passed in [docs/stage-a-tasks.md](docs/stage-a-tasks.md), tracked against
the frozen [docs/stage-a-design.md](docs/stage-a-design.md)) — the walking
skeleton: a fully playable, bit-exact Ironclad-vs-Jaw-Worm combat:

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

The oracle bridge track (Stage B Phase B0/B1 → gate **G4**). B0.1 (source pin
+ protocol survey) is done; **B0.2** is next: stock-jar bridge bring-up +
environment audit. This and the rest of the bridge track (B1.1–B1.6, the
JDK-8 fork build, campaign driver, translator) **require the live Slay the
Spire game** running under ModTheSpire on the Windows host — they are not
headless/WSL-CI work. See `docs/stage-b-tasks.md` Phase B0/B1 and
`docs/stage-b-design.md` §2. Phases B3/B4 (S1 combat + run content) stay
gated on **both** G4 and G5; G5 is done, so G4 is the blocker.
