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
- **Calling WSL from the Windows host — use `tools/wsl_run.sh`, never hand-roll
  the `wsl` line:**
  ```bash
  tools/wsl_run.sh debug asan release   # configure + build + test each, one summary
  tools/wsl_run.sh --script tools/bench_ab.sh A B   # run a script inside WSL
  ```
  (`tools\wsl_run.cmd` is the same entry point for cmd/PowerShell callers; both
  work unchanged from inside WSL.) **Why it exists:** the harness's Git-Bash
  layer mangles `$VAR` and bare `/mnt/...` arguments forwarded to `wsl` — `wsl
  -d Ubuntu-2404 -- bash -c 'printf "[%s]" "$1"' _ x` prints `[]`, because the
  boundary substitutes `$1` before WSL's bash sees it. A hand-rolled invocation
  therefore loses shell variables silently; an orchestrator loop over the three
  presets returned three empty results exactly that way. The helper forwards a
  fixed argv, keeps every line of shell code in a file on the WSL side, and
  refuses an argument containing `$` instead of running a mangled command. Full
  write-up in
  [conventions §6](docs/conventions.md#calling-wsl-from-the-windows-host--use-toolswsl_runsh).
  A cold WSL start can fail with `0x800705aa` under memory pressure — retry
  after freeing RAM.
- **Two more WSL/`D:` traps that have each cost an agent real time** — full
  symptom/cause/rule write-ups in
  [conventions §6](docs/conventions.md#calling-wsl-from-the-windows-host--use-toolswsl_runsh):
  1. `git` run **inside** WSL against a Windows-created worktree fails with
     `fatal: not a git repository: …/.git/worktrees/<name>` — the worktree's
     `.git` file holds a `D:/…` path WSL cannot resolve. Run git from Windows;
     send only builds/tests through WSL.
  2. `D:` is a DrvFs mount where **every file reports mode 777**, so `find
     -executable` / `-perm` predicates match *everything* (one agent wiped its
     own build dir's CTest files this way). Never use permission predicates
     under `/mnt/d`.

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
