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

**Stage B in progress, past M3.** Tags: `g1-rng-green`, `m1-walking-skeleton`,
`g4-bridge-live` (= M2, oracle bridge live end-to-end), `g5-registry-live`
(the registry is the single source of truth for content ids/tables),
**`g6-s1-content` (= M3, S1 rules complete)** — every B3/B4 task `[x]`, 100 %
tier-2 registry coverage by scripted check, 1,000-seed random-policy soak
clean in debug + asan, and a 75-run oracle spot campaign spanning Neow
through a claimed Act-1 boss reward with zero un-triaged diffs through the
run-level differ. Open gates: **G7** (S1 verified = M4 — every B5 task `[x]`;
the closing oracle campaign is paused mid-volume, live state in
[docs/handoff-2026-07-30.md](docs/handoff-2026-07-30.md)) and **GT0** (Phase T
information layer). The training program has a binding spec and ledger —
[docs/training-plan.md](docs/training-plan.md) and
[docs/training-tasks.md](docs/training-tasks.md); read them before any
training-related work, and respect their capacity rule (G7 close → T0.x → S2
authoring).

Per-task state — what is `[x]`, what is next, every deferred obligation — lives
in [docs/stage-b-tasks.md](docs/stage-b-tasks.md); **do not restate it here.**

**The build is no longer WSL-only.** Six presets: `debug`/`asan`/`release`
(WSL, GCC) and `win-debug`/`win-asan`/`win-release` (Windows, clang-cl + Ninja).
They are byte-identical — the 20 fixture traces hash the same under GCC, Clang
and clang-cl, including the LTO release build — so either host is authoritative.
WSL is **optional, including for sanitizers**: ASan *and* UBSan both work under
clang-cl. `cl.exe` is not a substitute — it has no `-Wconversion` /
`-Wsign-conversion` at any level, and it silently ignores `/fsanitize=undefined`
(warning D9002, exit 0), producing a build that looks instrumented but is not.
Two things to know before touching the build: the floating-point contract is
pinned in `cmake/StsFloatingPoint.cmake`, and **adding `-march=native` or any
fast-math flag would change damage numbers** — baseline x86-64 having no FMA
instruction is the only reason contraction was never biting. And
`tools/wsl_run.sh` now draws its build `-j` from a machine-wide token pool, so
concurrent agents cannot oversubscribe the box. conventions §6 carries the
Windows traps and the compiler-cache setup.

**Combat start is not step 6** — [stage-a-design.md](docs/stage-a-design.md)
§5.2a. Three separate divergences were fixed from that one documentation gap,
none of them caught by a failing test. Read it before assuming turn 1 and turn N
run the same sequence.

The current test count is **not** restated here, or anywhere outside a landed
task's own Log — re-derive it with `ctest -N | tail -1`.
`tools/check_stale_counts.sh` fails the build if a committed file starts
asserting one again, and `tools/check_doc_links.sh` fails it if a markdown link
or `#anchor` stops resolving — a section number is a value nothing re-derives
either. Both run as steps of the `stale-numbers` CI job, and both are meant to
be run by hand first; conventions §8 has the scope of each.

Execution pattern: one sub-agent per task with a self-contained brief, each in
its own git worktree under `D:\STS_BG_Mod\_wt\<task>`, running its own
acceptance; the orchestrator re-verifies and lands it on `master` — one task =
one commit. Model choice: **Fable** for larger/ambiguous tasks, **Opus** for
established boilerplate.

**Open and retire those worktrees with `tools/task_worktree.sh`** (or
`tools\task_worktree.cmd` from PowerShell), not by hand:

```bash
tools/task_worktree.sh create <task>   # refuses a dirty repo; prints the base sha to quote
tools/task_worktree.sh list            # branch / in-master / dirty, per worktree
tools/task_worktree.sh land <task>     # remove -> branch -d -> prune, in that order
```

It exists because both halves of that lifecycle failed after being written
down: a brief was written against an uncommitted edit the dispatched agent
could not see, and landed worktrees piled up until `git worktree list` was
unreadable. Full write-up in
[conventions §8](docs/conventions.md#8-traps-already-hit-verification-discipline).
Run it from Windows — it refuses under WSL, for the gitdir reason above.

### Oracle bridge — operational state (read before touching the bridge)

- Data root (uncommitted, design §7.3): **`D:\STS_BG_Mod\_oracle_data`** — the
  private per-worker config/game roots, JSONL captures, campaign artifacts,
  and throwaway helpers (`send.sh`, `autopilot.py`, `compare_neow.py`). These
  stay uncommitted on purpose; the committed tools are
  `driver/campaign_pipeline.py`, `driver/campaign_driver.py`, and
  `driver/orchestrator.py`.
- Use `campaign_pipeline.py run --instances N` (or `auto`) for campaigns. The
  coordinator launches each ModTheSpire worker under the game's **bundled JRE
  8** with a private working directory, profile/save/run tree, temp directory
  and `LOCALAPPDATA`/`APPDATA` config namespace; capture runs concurrently and
  WSL post-processing runs serially. CommunicationMod still reads its
  `ModTheSpire\CommunicationMod\config.properties` only at JVM launch. Never
  launch lower-level orchestrators in parallel or point two games at the
  install directory. The manual `echo_driver.py` side channel remains only for
  protocol bring-up and must send state-changing commands solely while
  `ready_for_command: true`.
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
