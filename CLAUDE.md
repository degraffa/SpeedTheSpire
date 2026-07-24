# SpeedTheSpire

A headless, high-performance Slay the Spire simulator in C++. This repo is
the simulator **only** — no rendering, no UI, no training/agent code. The
training program (MCTS, imitation learning, the meta-value function) that will
eventually run on top of this simulator lives in a separate, not-yet-created
repo.

This file is orientation. The binding documents are
[docs/conventions.md](docs/conventions.md) (**read first** — statuses, git
discipline, reading order, precedence chain, hygiene, build commands) and
[docs/stage-b-tasks.md](docs/stage-b-tasks.md) (the active ledger: open tasks,
deferred obligations, and a one-line index of what has landed; completed task
Logs are archived in [docs/stage-b-log.md](docs/stage-b-log.md), read only for
provenance). [docs/stage-a-design.md](docs/stage-a-design.md) and
[docs/stage-b-design.md](docs/stage-b-design.md) are **frozen** specs;
[InitialPlan.md](InitialPlan.md) is intent and rationale, never mechanics.

## Setup

- **Build system**: CMake ≥ 3.21, C++20. GoogleTest + Google Benchmark via
  `FetchContent` — no external package manager. Granted extras: nlohmann/json
  (tools-only), PyYAML (codegen-only).
- **Dev environment**: WSL2, Ubuntu 24.04 (distro `Ubuntu-2404`, installed on
  the `E:` drive — NVMe, not the system drive — set as the default WSL
  distro). GCC 13 / Clang 18 / CMake 3.28 / Ninja installed. Default WSL user
  is `alex` with passwordless sudo.
- **Build/test** (conventions §6 has the full form):
  ```bash
  cmake --preset debug        # or release, asan
  cmake --build --preset debug
  ctest --preset debug
  ```
- **Layout**: `include/` + `src/engine/` (the simulator library), `tests/`,
  `benchmarks/`, `registry/` (rules-as-data YAML, 8 domains — the codegen
  source), `tools/registry_gen/` (PyYAML codegen), `tools/oracle_bridge/`
  (vendored fork source, `PROTOCOL.md`, `driver/`, `translator/`),
  `tools/diff_harness/`, `tools/fixture_gen/`.
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

## Current state

**Stage A / M1 complete** (tag `m1-walking-skeleton`, gate G3): bit-exact RNG
trio, POD `CombatState`/`RunState` with memcpy snapshots, the action-queue pump
+ effect interpreter, JDK-exact pile ops, the batch `advance()` API, and 20
independently-generated combat fixtures replaying with zero diffs.

**Stage B in progress, past M2.** Tags: `g1-rng-green`, `m1-walking-skeleton`,
`g4-bridge-live` (= M2, oracle bridge live end-to-end), `g5-registry-live`
(the registry is the single source of truth for content ids/tables). Open
gates: **G6** (S1 rules complete = M3) and **G7** (S1 verified = M4). Current
work is Phase B3 (combat content) and Phase B4 (run layer).

Per-task state — what is `[x]`, what is next, every deferred obligation — lives
in [docs/stage-b-tasks.md](docs/stage-b-tasks.md); **do not restate it here.**
The newest task Log there also carries the current test count (526/526 gtest
cases, debug + asan, at `61e8e11`); the ledger is authoritative, any number in
this file is advisory.

Execution pattern: one sub-agent per task with a self-contained brief, each in
its own git worktree under `D:\STS_BG_Mod\_wt\<task>`, running its own
acceptance; the orchestrator re-verifies and lands it on `master` — one task =
one commit. Model choice: **Fable** for larger/ambiguous tasks, **Opus** for
established boilerplate.

### Oracle bridge — operational state (read before touching the bridge)

- Data root (uncommitted, design §7.3): **`D:\STS_BG_Mod\_oracle_data`** — the
  live `config.properties` targets, JSONL captures, `latest_state.json`,
  campaign artifacts, and throwaway helpers (`send.sh`, `autopilot.py`,
  `compare_neow.py`). These stay uncommitted on purpose; the committed tools
  are `driver/echo_driver.py`, `driver/campaign_driver.py`,
  `driver/orchestrator.py`.
- CommunicationMod reads
  `%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties` **only at
  game launch**. Wiring + the scriptable `--skip-launcher --mods
  basemod,CommunicationMod-oracle` launch are in
  `tools/oracle_bridge/driver/README.md`; run ModTheSpire under the game's
  **bundled JRE 8** (`<game>\jre\bin\java.exe`). The **game is launched
  manually**; the driver auto-attaches; drive it by appending commands to the
  command file, and only send state-changing commands while
  `ready_for_command: true`. The game runs on Windows Python
  (`C:/Python39/python.exe`), outside WSL.
- Profile audit result (design §1.1): the save is **fully unlocked** at the
  pool gate (all 60 `UnlockTracker.refresh()` gated keys have `STSUnlocks`
  flag=2 → `lockedCards`/`lockedRelics` empty). The `IRONCLADUnlockLevel=3`
  meta-counter is cosmetic and does **not** gate run pools — do not "fix" it.
- The bridge never runs in WSL/CI; campaign artifacts are never committed.

### Codex/GPT delegation

`.mcp.json` exposes two Codex MCP servers: `codex-sol` (`gpt-5.6-sol`) for
difficult implementation, debugging, architecture and review work, and
`codex-terra` (`gpt-5.6-terra`) for faster routine work. Use them when the user
asks for GPT/Codex input or when a second model's independent implementation or
review would materially help. Give Codex a self-contained task, including
relevant paths and acceptance criteria. Codex uses the current user's existing
Codex authentication; never add credentials to this repository.
