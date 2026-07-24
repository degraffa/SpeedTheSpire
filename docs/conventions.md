# Conventions (binding on every task)

The **single authoritative copy** of the rules that govern work in this repo:
statuses, git discipline, the canonical reference reading order, the
precedence chain, hygiene, and the build commands. It supersedes any
paraphrase of these rules found elsewhere — [stage-b-tasks.md](stage-b-tasks.md),
[stage-b-orchestrator-prompt.md](stage-b-orchestrator-prompt.md) and
[../CLAUDE.md](../CLAUDE.md) point here and carry only their own local
specifics. If one of them ever disagrees with this file, **this file wins and
the other gets fixed in the same change**.

Frozen documents ([stage-a-design.md](stage-a-design.md),
[stage-b-design.md](stage-b-design.md)) are NOT superseded by this file — see
the precedence chain below.

---

## 1. Statuses

`[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.

A task is **done only when its Acceptance block passes** — run the commands,
don't infer. Tests land in the same change as the code they verify. Registry
YAML is code: entries land with their tier-2 tests in one commit.

Respect `Deps:` exactly. Tasks marked ∥ with satisfied deps and disjoint
deliverables may run in parallel — but never two agents whose deliverables
touch the same files, and never more parallel work than can be verified and
committed serially: **parallel execution, serial integration.**

**Gates** (G1–G7) are stop-the-line: nothing past a gate starts until the gate
task is `[x]`. Phase B3/B4 content work additionally requires **both** G4 and
G5 (design §3.2: bridge first, then registry migration, then mass content).

---

## 2. Git discipline

- Integration happens on `master` (local repo, no remote; if a remote appears,
  push tags too). Execution agents work in their own git worktree under
  `D:\STS_BG_Mod\_wt\<task>` on a task branch; the orchestrator verifies, then
  lands the work on `master`.
- **One task = one commit**, made at task completion. Gates always get their
  own commit. The ledger's checkbox + Log update goes **in the same commit** as
  the task's code — history and ledger never disagree.
- Commit message: subject `<id>: <what changed>` (e.g. `B1.2: oracle state
  block`); body lists the acceptance evidence (test target names that ran
  green, incl. asan; for bridge tasks, the Windows-host command that ran and
  its recorded result) and cites provenance for any behavior derived from the
  game. Claude-authored commits carry the `Co-Authored-By: Claude` trailer.
- Commit only from a green tree — **debug and asan presets pass; release too
  at gates**. Never `--no-verify`, never amend or rebase committed work; fix
  forward.
- Never `git add -A`; stage an explicit path list so nothing unrelated rides
  along.
- Tag gates: `g4-bridge-live` (= M2), `g5-registry-live`, `g6-s1-content`
  (= M3), `g7-s1-verified` (= M4).
- **Never commit:** decompiled Java or anything from `sts-classes.jar` /
  `desktop-1.0.jar` (license hygiene — the repo re-expresses, never copies),
  built jars (incl. the fork jar), campaign artifacts (raw JSONL or translated
  traces — they live under the §7.3 data root), scratch files, `build/`.
  Exceptions, all small and reviewed: golden vectors (`tests/golden/`, as in
  Stage A), the curated CI oracle corpus (compressed, `tests/golden/
  oracle_corpus/`), promoted regression reproducers, generated verification
  reports (`docs/verification/`). The vendored fork *source* is committed
  (upstream is MIT — verified at B0.1) with its license file intact.
- **Session-start ritual** for any agent: `git status` + `git log --oneline
  -5`. A dirty tree, or a `[~]` task whose Log is empty, is an **incident** —
  investigate and resolve it before any new work. **Never dispatch on a dirty
  tree.**

---

## 3. Canonical references — reading order when picking up a task

1. [stage-b-tasks.md](stage-b-tasks.md) — the task's Deps / Deliverables /
   Acceptance / **Inherited**, plus the **Deferred obligations** table.
2. [stage-b-design.md](stage-b-design.md), the cited §§ — the Stage B frozen
   spec.
3. [stage-a-design.md](stage-a-design.md) — frozen mechanics (RNG, state,
   queue, damage, schema) that Stage B builds on.
4. The decompiled Java at `D:\STS_BG_Mod\SlayTheSpireDecompiled` — all
   `File.java:line` citations resolve here. **Read the cited method in full
   before implementing it**, even when a design doc paraphrases it: the design
   docs paraphrase, the Java is the spec.
5. The live game via the oracle bridge (G4 is `[x]`) — the runtime oracle.
6. [../InitialPlan.md](../InitialPlan.md) — intent and rationale only, never
   mechanics.
7. Toolchains and installs: JDK 8 (`C:\Program Files\Java\jdk1.8.0_171`)
   builds the fork; the game's bundled JRE 8 (`<game>\jre\bin\java.exe`) runs
   ModTheSpire; Java 25 runs golden capture (stage-a A0.1); Python 3 runs the
   driver and codegen; the game is at
   `D:\SteamLibrary\steamapps\common\SlayTheSpire` with CommunicationMod at
   workshop item `2131373661` (design §1.2).

[stage-b-log.md](stage-b-log.md) is **not** in the reading order — it is the
archive of completed task Logs, read only when you need the provenance of
something already landed.

### Provenance

Any behavior taken from the game cites `ClassName.method (File.java:line)`
into `D:\STS_BG_Mod\SlayTheSpireDecompiled` (stage-a §1). If implementation
contradicts the design docs' reading of the Java: **stop, re-read the cited
source, and report back — do not code around it.**

---

## 4. Precedence on conflict

> **reproduced live-game observation > decompiled Java > design docs > ledger
> > InitialPlan** (stage-b-design §1.3)

Live-game observation only outranks the decompiled Java once G4 is `[x]`
(it is), and only over a real evidence bar:

- a `(seed, action-prefix)` reproducer,
- a **second** reproduction, and
- a **strip-patch audit** (design §1.3) proving the fork's rendering-strip
  patches are not what produced the observation.

Every such override is recorded in the design doc's change log.

**Discovering any conflict between documents is stop-the-line.** Fix the
losing document in the same change, and record it in the task Log **and** in
whichever change log owns the losing text (stage-a §12 / stage-b §11 / the
ledger's change log). If the fix is not mechanical, or would change a frozen
mechanic, propose the change-log entry and surface it — do not silently apply
it.

---

## 5. Hygiene

- **No rule without its test** — the test lands in the same commit as the
  behavior. For registry entries the test is the tier-2 table test; for
  bridge/campaign code the test is the recorded acceptance run.
- Golden mismatch, fixture divergence, or **campaign divergence** =
  stop-the-line. Debugging step 1 is re-reading the cited Java; step 2 is
  auditing the fork's strip patches (design §1.3); root cause goes in the task
  Log, reproducers get promoted to regression fixtures.
- Registry ids and opcode numbers are **append-only** (design §4.4). Anything
  that would renumber an existing id or opcode is stop-the-line: surface it
  first, and if it is sanctioned it needs a design-doc change-log entry **and**
  a schema-version bump.
- Stage A fixtures are never hand-edited. If a struct edit invalidates them,
  regenerate via the checked-in independent generator
  (`tools/fixture_gen/`) and prove zero-diff-in-meaning; a bump of
  `SCHEMA_VERSION` outside the places the ledger plans for it is
  stop-the-line.
- No new third-party dependencies beyond those granted in design §2.6/§4.3
  (nlohmann/json tools-only; PyYAML codegen-only) without a design-doc
  change-log entry.
- A completed task's forward-looking obligations go into the ledger's
  **Deferred obligations** table **and** onto the owning task's
  `**Inherited:**` line — never only into the archived Log, which nobody
  reads during execution.
- At each gate: update `CLAUDE.md`'s **Current state** block so a fresh
  session orients correctly without reading history.
- **Stop and surface to the user** (do not improvise) when: a divergence
  survives triage and the fix would change a frozen mechanic; the bridge
  throughput floor (≥ 5 actions/sec, design §2.2) becomes unreachable; an id /
  opcode renumber or an unplanned `SCHEMA_VERSION` bump is needed; a task's
  real scope turns out materially larger than its ledger entry (propose the
  split as a ledger change-log entry first); the B0.2 profile-unlock audit
  fails or the game/mod environment drifts from the frozen 11-30-2020 build;
  or two documents conflict and the fix isn't mechanical. Otherwise proceed
  autonomously — reversible, in-scope work never waits for permission.

---

## 6. Build and test

Engine work builds and tests in **WSL Ubuntu-2404** (default distro, on `E:`;
GCC 13 / Clang 18 / CMake 3.28 / Ninja; user `alex`, passwordless sudo).
Presets: `debug`, `asan`, `release`.

```bash
cmake --preset debug        # or asan, release
cmake --build --preset debug
ctest --preset debug
```

`release` has **no test preset** — it builds the tests into `build/release`;
run them with `ctest --test-dir build/release`.

### Calling WSL from the Windows host (hard-won — read before scripting it)

The harness's Git-Bash layer mangles `$VAR` and bare `/mnt/...` arguments
forwarded to `wsl`. **Run multi-line WSL work from a script file:**

```bash
MSYS_NO_PATHCONV=1 wsl -d Ubuntu-2404 -- bash /mnt/c/.../script.sh
```

For a one-shot build/test:

```bash
MSYS_NO_PATHCONV=1 wsl -d Ubuntu-2404 -- bash -lc 'cd /mnt/d/STS_BG_Mod/SpeedTheSpire \
  && cmake --preset debug && cmake --build --preset debug && ctest --preset debug'
```

A cold WSL start can fail with `0x800705aa` under memory pressure — retry
after freeing RAM.

### Bridge-side components

The fork jar (JDK 8, `tools/oracle_bridge/build_fork.ps1`) and the Python
driver build and run on the **Windows host** and are excluded from WSL CI.
Their acceptance commands say so explicitly and record the Windows-host
command plus its result in the task Log. Nothing in CI builds or runs the
game.

---

## Change note — 2026-07-24 (creation)

Created by consolidating three drifted copies of these rules
(`stage-b-tasks.md` "Working agreements", `stage-b-orchestrator-prompt.md`
§2-§4, and `CLAUDE.md`). Where the wordings differed, the **strictest and most
complete** reading was adopted:

1. **Reading order** — kept the ledger's 7 entries (the prompt dropped the
   toolchain entry) and folded in the prompt's concrete paths (game dir,
   workshop id, JDK 8 dir).
2. **Live-override bar** — adopted the prompt's more specific
   `(seed, action-prefix)` reproducer over the ledger's bare "a reproducer".
3. **Document-conflict recording** — union of both: fix the loser in the same
   change **and** record it in the task Log **and** the owning change log.
4. **Green-tree bar** — union: debug + asan on every commit (ledger), release
   additionally at gates (prompt §5).
5. **Branch model** — the ledger said "work on `master`"; CLAUDE.md described
   per-task worktrees under `_wt\<task>` with serial integration. Reconciled to
   the practice actually in use: worktree per task, integrate to `master`, one
   task = one commit.
6. **Dependency grant** — kept the ledger's version, which names the sanctioned
   escape hatch (a design-doc change-log entry); the prompt stated the ban with
   no escape.
7. **Session-start ritual** — adopted the prompt's stricter form ("never
   dispatch on a dirty tree") on top of the ledger's checks.
8. **Provenance** — union: read the cited method **in full** before
   implementing (prompt) *and* stop + re-read + report rather than coding
   around a contradiction (both, merged).
9. **Append-only ids/opcodes** — union: the ledger's "change-log entry +
   schema-version bump" requirement *plus* the prompt's stop-and-surface
   trigger.
10. **Build commands** — the ledger and prompt gave only the WSL-native
    triple; CLAUDE.md alone carried the `release`-has-no-test-preset caveat and
    the `MSYS_NO_PATHCONV` WSL-call gotcha. Both are kept here.
11. **Commit body** — kept the ledger's fuller requirement (named test targets
    incl. asan, and the Windows-host command + result for bridge tasks) over
    the prompt's "acceptance evidence and provenance".
12. **`git add`** — the explicit-path-list rule was practice but written down
    nowhere; recorded here.

No rule was weakened or dropped in the consolidation.
