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
  YAML — 8 domains, the codegen source), `tools/registry_gen/` (the PyYAML
  codegen), `tools/oracle_bridge/` (CommunicationMod bridge: vendored fork
  source, `PROTOCOL.md`, and the `driver/` bring-up child).

## Current state

**Stage B in progress — milestone M2 reached.** Both Stage B gates so far are
`[x]`: the oracle-bridge track through gate **G4** (tag `g4-bridge-live`, = M2)
and the registry track through gate **G5** (tag `g5-registry-live`); bridge
groundwork (Phase B0) done. Tracked in
[docs/stage-b-tasks.md](docs/stage-b-tasks.md) against the frozen
[docs/stage-b-design.md](docs/stage-b-design.md). The full oracle bridge is
**live end-to-end**: deterministic fork jar (sha `04477E4E…`) → campaign driver
→ JSONL artifacts → C++ translator → v2 trace container + `diff_run_states` +
oracle adapter. Landed so far:

- **B0.1** — CommunicationMod v1.2.1 vendored source-only (MIT) under
  `tools/oracle_bridge/communicationmod-oracle/`, with
  `tools/oracle_bridge/PROTOCOL.md` cataloguing every `GameStateConverter`
  field.
- **B0.2** — stock-jar bridge bring-up. `tools/oracle_bridge/driver/
  echo_driver.py` is the minimal CommunicationMod child (logs each state JSON
  to JSONL; forwards operator commands from a side-channel file — the child's
  own stdio belongs to the game); `driver/README.md` documents the
  `config.properties` wiring + scriptable ModTheSpire launch. Verified against
  the **live game**: a seeded A20 Ironclad run (Neow → floors 1-3) captured and
  `--verify`-clean; `start` and the game's own seeded-run UI produce a
  byte-identical floor-0 state for the same seed (both share
  `SeedHelper.getLong`); stock throughput ~0.36 states/s; profile confirmed
  fully unlocked (complete card/relic pools). **The bridge runs only against
  the live game on the Windows host** (JDK-8 / ModTheSpire) — never in WSL/CI.
- **B2.1 + B2.2 + G5** — the registry is live and is now the single source of
  truth for content ids/tables. `registry/*.yaml` (8 domains, currently the
  5 skeleton cards + 3 powers + Jaw Worm; other 5 domains valid-but-empty) is
  compiled by `tools/registry_gen/gen.py` (Python 3 + PyYAML) into constexpr
  headers under the build tree (`<build>/generated/sts/registry/*.hpp`, never
  committed). `types.hpp`/`cards.hpp`/`monster_jaw_worm.*` now **re-export**
  the generated enums/tables into `sts::engine` (no hand tables left — no dual
  system); ids are append-only and `static_assert`-pinned. CI installs
  `python3-yaml` and runs the generator.
- **B1.1–B1.6 + G4** — the oracle fork track. The vendored fork
  (`CommunicationMod-oracle`) is built by `tools/oracle_bridge/build_fork.ps1`
  (JDK 8, no Maven, **deterministic jar** sha `04477E4E…`) and emits the §2.5
  `oracle` block (14 RNG streams as `{counter,s0,s1}` + pity + pools + move
  history) plus toggleable rendering-strip patches (≥32 act/s stripped). The
  `driver/` runs seeded A20 campaigns (crash-resume) → versioned JSONL; the C++
  `tools/oracle_bridge/translator/` (nlohmann, tools-only) turns JSONL into
  RunState/CombatState with a fail-loud disposition table, feeding the v2 trace
  container + `diff_run_states` + `CommunicationModOracleAdapter`. **G4 verified
  the bridge against the live game**: over a 20-seed A20 campaign (996 records)
  every dump translated with **zero unknown-FIELD errors** (unknown content ids
  are the expected pre-B3 registry gap, tallied via the translator's
  `--tolerate-unknown-ids` accounting mode); `floor_stream`/`map_stream`/
  `relicRng`-init cross-checked bit-for-bit against the engine's tier-1 RNG
  (`tools/oracle_bridge/translator/src/oracle_gate_check.cpp`). **The bridge runs
  only against the live game on the Windows host** (JDK-8 / ModTheSpire) and its
  campaign artifacts live under the §7.3 data root — never in WSL/CI, never
  committed.

All **166** gtest cases green in `debug`, `asan`, and `release` (Stage A's 131
+ 9 registry-gen + 26 bridge/translator/adapter cases); `SCHEMA_VERSION` bumped
to **2** at B1.6 (v1 fixtures compat-read), `sizeof(CombatState)`=3504 unchanged.
**Next:** Phases **B3/B4** (S1 = Ironclad / Act 1 / A20 content) — now unblocked
since **both** G4 and G5 are `[x]`; B3 registry rows land the content ids the
real A20 captures need (the id-drift guard currently reports them as unknown),
then B4 builds out the run layer (map/events/shops/relics/potions).

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

**Phase B3/B4 — S1 content** (Ironclad / Act 1 / A20), now **unblocked**: both
G4 (`g4-bridge-live`, M2) and G5 are `[x]`, the design §3.2 precondition for
content work. Start with **B3.1** (interpreter/card-mechanics extensions) per
`docs/stage-b-tasks.md`; the B3 registry waves add the ~126 card / 25 monster /
relic / potion / power rows the real A20 captures need — the id-drift guard
currently reports those as unknown content ids (see the G4 unknown-id tally: 94
distinct, e.g. `Burning Blood`, `AscendersBane`, `Cultist`). Once a batch's rows
land, re-run the translator over the §7.3 campaign corpus **without**
`--tolerate-unknown-ids` to watch the unknown-id set shrink. Most B3/B4 work is
headless WSL/CI (engine + registry + tests); the oracle bridge is only re-touched
to capture new directed scripts or spot-diff a batch (needs the live game on the
Windows host).

### Oracle bridge — operational state (read before touching the bridge)

- The bridge is wired and working against the **stock** CommunicationMod jar.
  Data root (uncommitted, design §7.3): **`D:\STS_BG_Mod\_oracle_data`** — holds
  the live `config.properties` targets, `run_capture*.jsonl` captures,
  `latest_state.json`, and bring-up helpers: `send.sh` (append one command +
  print a state summary), `autopilot.py` (throwaway legal-move stepper that
  drove the B0.2 capture — plays attacks, ends turns, takes rewards, walks the
  map), `compare_neow.py` (normalized floor-0 diff, seed-matched). These stay
  **uncommitted** on purpose; the committed driver is `echo_driver.py`.
- CommunicationMod reads
  `%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties` **only at
  game launch**; `command=` currently points at `echo_driver.py`. Exact wiring
  + the scriptable `--skip-launcher --mods basemod,CommunicationMod` launch are
  in `tools/oracle_bridge/driver/README.md`. The **game is launched manually**;
  the driver auto-attaches; drive it by appending commands to the command file,
  and only send state-changing commands while `ready_for_command: true`. The
  game runs on Windows Python (`C:/Python39/python.exe`), outside WSL.
- **WSL-call gotcha (bit us repeatedly):** the harness's Git-Bash layer mangles
  `$VAR` and bare `/mnt/...` args forwarded to `wsl`. Run multi-line WSL work
  from a script file: `MSYS_NO_PATHCONV=1 wsl -d Ubuntu-2404 -- bash
  /mnt/c/.../script.sh`. For engine builds/tests:
  `MSYS_NO_PATHCONV=1 wsl -d Ubuntu-2404 -- bash -lc 'cd
  /mnt/d/STS_BG_Mod/SpeedTheSpire && cmake --preset debug && cmake --build
  --preset debug && ctest --preset debug'` (also `asan`; the `release` preset
  has no test-preset — it builds tests into `build/release`, run them with
  `ctest --test-dir build/release`). A cold WSL start can fail with
  `0x800705aa` under memory pressure — retry after freeing RAM.
- Profile audit result (design §1.1): the save is **fully unlocked** at the
  pool gate (all 60 `UnlockTracker.refresh()` gated keys have `STSUnlocks`
  flag=2 → `lockedCards`/`lockedRelics` empty). The `IRONCLADUnlockLevel=3`
  meta-counter is cosmetic and does **not** gate run pools — do not "fix" it.

### How Stage B tasks have been executed (working pattern)

Orchestrator dispatches one sub-agent per task with a self-contained brief;
each works in its own git worktree under `D:\STS_BG_Mod\_wt\<task>`, runs the
acceptance itself, then the orchestrator re-verifies, cherry-picks/merges to
`master`, and updates the ledger — one task = one commit. Model choice:
**Fable** for larger/ambiguous tasks, **Opus** for established boilerplate.

### Codex/GPT delegation

This project exposes two Codex MCP servers through `.mcp.json`:

- `codex-sol` uses `gpt-5.6-sol` for difficult implementation, debugging,
  architecture, and review work.
- `codex-terra` uses `gpt-5.6-terra` for faster, routine work.

Use these tools when the user asks for GPT/Codex input or when a second model's
independent implementation or review would materially help. Give Codex a
self-contained task, including relevant paths and acceptance criteria. Codex
uses the current user's existing Codex authentication; never add credentials
to this repository.
