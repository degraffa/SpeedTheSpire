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

- Integration happens on `master`. The remote is `origin`
  (github.com/degraffa/SpeedTheSpire) — push `master` **and tags** after
  landing. Execution agents work in their own git worktree under
  `D:\STS_BG_Mod\_wt\<task>` on a task branch; the orchestrator verifies, then
  lands the work on `master`.
- **A worktree is deleted as part of landing, not "later".** Once a task's
  branch is merged and pushed, remove its worktree and delete its branch in the
  same step that lands it. A worktree is scaffolding for one task; it stops
  being useful the moment the work is on `master`.

  ```bash
  git worktree remove D:/STS_BG_Mod/_wt/<task>   # FIRST -- see order below
  git branch -d <task>                            # -d, never -D
  git worktree prune
  ```

  Three things that matter here:
  - **Order is load-bearing.** `git branch -d` fails while a worktree still
    references the branch (`cannot delete branch '<x>' used by worktree at …`).
    Remove the worktree first.
  - **Use `-d`, never `-D`.** `-d` refuses to delete a branch whose work is not
    contained in `master`, which is the last safety net against discarding
    unlanded work. If `-d` refuses, do **not** reach for `-D` — find out why.
  - **Deliberately kept worktrees are fine; forgotten ones are not.** A
    worktree holding uncommitted work for a still-open task is legitimate (it
    should be known and named). What this rule prevents is accumulation nobody
    is tracking.

  Why it is a rule: landed-task worktrees were once allowed to pile up to
  **16.3 GB across 11 directories**, and — worse than the disk — `git worktree
  list` became noise, so a worktree that genuinely still held uncommitted work
  was indistinguishable from ten that did not. Cleanup at landing keeps that
  list short enough to read.

  > **ELIMINATED 2026-07-25: `tools/task_worktree.sh` runs the whole lifecycle
  > in the order above, so the three commands are no longer typed by hand.**
  > Full write-up in [§8](#8-traps-already-hit-verification-discipline).
  >
  > ```bash
  > tools/task_worktree.sh create <task>   # refuses a dirty repo; prints the base sha
  > tools/task_worktree.sh list            # branch / in-master / dirty, per worktree
  > tools/task_worktree.sh land <task>     # remove -> branch -d -> prune, in that order
  > ```
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
  tree** — `tools/task_worktree.sh create` refuses to build a worktree while the
  repo is dirty, and prints the base commit sha for the brief to quote, so use
  it rather than `git worktree add`.

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
  fails or the game/mod environment drifts from the frozen build **as design
  §1.2 currently names it** (do not restate the versions here — that number
  went stale once already and nothing re-derived it; B4.5 / design §11 v0.1.7);
  or two documents conflict and the fix isn't mechanical. Otherwise proceed
  autonomously — reversible, in-scope work never waits for permission.

---

## 6. Build and test

### Where a new header goes

`include/sts/engine/` is the engine's **published surface** — it is the only
include directory `sts_engine` exposes (`src/engine/CMakeLists.txt`,
`target_include_directories(... PUBLIC ...)`). Put a header there only if
something outside the library includes it (a test, a tool, a future consumer).
That is why the per-monster headers live there: ten test files include them.

A header whose only consumers are other `.cpp` files in `sts_engine` is
**internal** and lives beside them, under `src/engine/<module>/` — as with
`interp/`, `powers/`, `relics/`. Putting internal helpers in the public
directory advertises them as supported API. If a test later needs one, move
that header up deliberately rather than pre-emptively.

Engine work builds and tests in **WSL Ubuntu-2404** (default distro, on `E:`;
GCC 13 / Clang 18 / CMake 3.28 / Ninja; user `alex`, passwordless sudo).
Presets: `debug`, `asan`, `release`.

```bash
cmake --preset debug        # or asan, release
cmake --build --preset debug
ctest --preset debug
```

All three have test presets, so `ctest --preset release` works. (It did not
until 2026-07-25; older notes telling you to use `ctest --test-dir
build/release` are stale, though that form still works.)

#### Two ways the test suite lies to you

**`cmake --build` before every `ctest`.** Test discovery is `PRE_TEST`, so a
test whose binary is missing or stale is reported as `<name>_NOT_BUILT (Not
Run)` — a *failure line that is not a real failure*. Running `ctest` against a
tree you have not just built produces a confusing red result that has nothing
to do with the code.

**`ctest` exits 0 on an empty test set.** A configuration that registers zero
tests reports success. All three presets now set `execution.noTestsAction:
error` so this fails loudly instead; do not remove it. This was found by
accident, not by design — assume any *new* test target is not running until you
have seen its name in `ctest -N`.

### Calling WSL from the Windows host — use `tools/wsl_run.sh`

**ELIMINATED 2026-07-25: `tools/wsl_run.sh` (+ `tools/wsl_run.cmd` for
cmd/PowerShell callers) is now the sanctioned form. It crosses the boundary
once, correctly, and refuses arguments it cannot carry; hand-rolled `wsl`
invocations are no longer the documented workaround.**

```bash
tools/wsl_run.sh debug                  # configure + build + test one preset
tools/wsl_run.sh debug asan release     # all three, one PASS/FAIL summary at the end
tools/wsl_run.sh release -DSTS_BUILD_BENCHMARKS=ON
tools/wsl_run.sh --script tools/bench_ab.sh A B   # run a script file inside WSL
```

The same command line works from Git-Bash, from cmd/PowerShell (via the
`.cmd`), and from inside WSL. Each worktree runs its own copy against its own
tree, so there is no path to edit.

**Why it exists — the trap it removes.** The harness's Git-Bash layer mangles
`$VAR` and bare `/mnt/...` arguments forwarded to `wsl`. The `$VAR` half is the
dangerous one, because it fails *silently*:

```bash
MSYS_NO_PATHCONV=1 wsl -d Ubuntu-2404 -- bash -c 'printf "[%s]" "$1"' _ x
# prints []  -- the boundary substituted $1 before WSL's bash ever saw the string
```

An orchestrator loop over `for p in debug asan release` lost its variable that
way and returned three empty results that looked like runs. The old workaround
("run multi-line work from a script file") was correct and was still hand-rolled
at every call site, which is why the trap kept firing — §7.

`wsl_run.sh` never interpolates across the boundary: it forwards a fixed argv,
re-executes itself inside WSL where all the shell code lives, sets
`MSYS_NO_PATHCONV=1` itself, derives its own `/mnt/<drive>/...` path from its
location, and **exits 2 on any argument containing `$`** rather than run a
command that has quietly lost a word. It also always builds before it tests, so
the `_NOT_BUILT` trap above cannot be reached through it.

A cold WSL start can fail with `0x800705aa` under memory pressure — retry
after freeing RAM.

#### `fatal: not a git repository: …/.git/worktrees/<name>` — git inside WSL

**Symptom:** any `git` command run *inside* WSL against a worktree created from
Windows dies immediately with

```
fatal: not a git repository: 'D:/STS_BG_Mod/SpeedTheSpire/.git/worktrees/<name>'
```

even though the directory exists and `git status` works fine from the Windows
side.

**Cause:** `git worktree add` writes the linked worktree's `.git` file as a
*Windows-style* path (`gitdir: D:/STS_BG_Mod/SpeedTheSpire/.git/worktrees/…`).
WSL's git cannot resolve `D:/…`; it needs `/mnt/d/…`. Nothing is corrupt.

**Rule:** **run every `git` command from the Windows side** — `git -C
D:/STS_BG_Mod/_wt/<name> …`. Send only builds and tests through WSL. Do not
"fix" the `.git` file to a `/mnt/` path: that breaks the Windows side, which is
where commits are made.

#### `D:` is DrvFs — every file reports mode 777

**Symptom:** `find /mnt/d/… -type f -executable` (or `-perm`, `test -x`, `[ -x
… ]`, `find … -executable -delete`) matches **every file** under `/mnt/d`, not
just executables. A cleanup one-liner built on that predicate looks correct in
review and silently destroys unrelated files.

**Cause:** `D:\` is mounted as DrvFs, which has no Unix permission bits and
reports mode 777 for everything, directories and text files alike.

**Real incident:** an agent on this effort used an `-executable` predicate to
prune stray binaries and silently wiped its own `build/debug` CTest files;
the failure surfaced later as a confusing "no tests found".

**Rule:** **never use an executable-bit or permission predicate on anything
under `/mnt/d`.** Select by path, name, or extension instead (`-name '*.o'`,
an explicit path list), and never combine a `-perm`/`-executable` predicate
with `-delete`.

#### FetchContent traps

**A shared `FETCHCONTENT_BASE_DIR` shares *build* trees, not just sources.**
`FetchContent_Declare` bakes both `SOURCE_DIR` and `BINARY_DIR` from the base
dir in effect, so pointing every preset at one base dir makes `asan` and
`release` emit the *same* object paths — an ASan-instrumented `libgtest.so`
landing exactly where the release link line reads. `STS_SHARED_DEP_CACHE`
therefore shares only the deps with **no sub-build** (xxHash, nlohmann). Do not
"simplify" it into a single base dir.

**`GIT_SHALLOW ON` is not a small clone.** CMake hard-codes `clone --depth 1
--no-single-branch`, so it still fetches every branch tip and tag — nlohmann
measured 278 MB → 186 MB, not the ~2 MB you would expect. Prefer a pinned
release asset (`URL` + `URL_HASH`); that took the same dependency to 1.9 MB.

#### Two build commands in one build tree corrupt each other's objects

**Symptom:** a link fails with `file format not recognized` for an object that
compiled cleanly before and compiles cleanly again when rebuilt alone.

**Cause:** two agents ran `tools/wsl_run.cmd debug` concurrently in the same
task worktree. Their Ninja processes wrote the same generated object at the
same time. The machine-wide build-token pool limits aggregate parallel jobs;
it does **not** serialize separate build invocations that share one build
directory.

**Rule:** one agent owns build/test execution for a worktree at a time. A
reviewer working in that same tree either waits for the owner's matrix or does
source-only review; an independently building reviewer needs a separate
worktree/build directory. If this symptom occurs, stop the competing build,
verify the exact failed object is under the intended preset's build directory,
remove only that object, and rebuild it. Do not respond with a broad build-tree
cleanup.

---

## 7. The rule of two — document once, eliminate on recurrence

**First time a trap is hit: write it down** (below, symptom first, with the
incident behind it).

**Second time the same trap is hit — by anyone, including a different agent, or
the orchestrator — the documentation has failed.** A trap that recurs after
being documented is evidence that reading is not a sufficient control. The
second occurrence is not another note; it is a **task**, owned by the
orchestrator, to make the trap structurally impossible or self-announcing.

Preference order for the elimination:

1. **Remove the footgun.** Delete the sharp edge, or replace the hand-rolled
   thing with a committed helper nobody has to get right twice
   (`tools/wsl_run.sh`, `tools/bench_ab.sh`).
2. **Fail loudly at the point of the mistake**, with a message that names the
   fix — an assert, a link error, a `noTestsAction: error`, a generated table
   whose missing entry will not link.
3. **Automate the check** — CI, a pre-commit hook, a test that greps for the
   forbidden shape.
4. **Only if none of the above apply**: strengthen the doc *and* put a pointer
   at the exact site where the mistake gets made.

An elimination is a real commit, not a paragraph. Record it in the trap's entry
below so the history stays visible: `ELIMINATED <date>: <how>`.

**Corollary — count honestly.** "I hit it, then an agent hit it" is two. The
point is not blame; it is that a trap surviving one documented encounter will
survive the next reader too.

---

## 8. Traps already hit (verification discipline)

**Re-derive numbers from the tree, never from a document.** Test counts in task
Logs are correct only on the day they are written. A stale count (454, when the
tree was at 526) was copied out of a Log into four separate task briefs before
anyone checked. `ctest -N | tail -1` is the source of truth.

> **ELIMINATED 2026-07-25: `tools/check_stale_counts.sh`, run by hand and by a
> `stale-numbers` CI job on every push.** It fails when a committed file asserts
> a pass count — an equal `<N>/<N>` ratio on a line that also talks about tests,
> an `<N>/<M> tests` count, or "all *N* tests pass" in prose. It reads tracked
> *and* untracked-but-not-ignored files, so it catches the number before you
> stage it; `--help` lists the patterns. Run it from **Git-Bash on the Windows
> host** (or in CI) — it is a git-side check, so the §6 gitdir trap applies and
> it cannot run through WSL; note that `bash` on the PowerShell `PATH` *is*
> WSL's bash. Deliberately **not** scanned:
> `docs/stage-*-log.md` (append-only archives of past runs), and the `[x]` /
> `[!]` lines of `docs/stage-*-tasks.md` (a landed entry recording what was
> green when it landed is history, and §2 requires it) — but `[ ]` and `[~]`
> lines *are* scanned, because a pending task brief quoting a count is exactly
> the incident above. A line carrying `stale-count-ok` is skipped; that hatch is
> for prose about a past incident, not for new claims. If it fires on a file you
> are editing, the number is already a liability: delete it and cite `ctest -N`.

**A section number or an anchor in prose is the same kind of value.** Nothing
re-derives it either. A task brief cited `§7`/`§8` of this file at a revision
where those sections did not exist; separately, an agent hand-updated
CLAUDE.md's `#calling-wsl-from-the-windows-host--use-toolswsl_runsh` anchor with
nothing checking it. Renaming a heading breaks every link into it **silently** —
GitHub renders a dead `#anchor` as an ordinary link that scrolls nowhere.

> **ELIMINATED 2026-07-25: `tools/check_doc_links.sh`, run by hand and as a
> second step of the `stale-numbers` CI job.** It resolves every inline
> `[text](target)` link in every markdown file — the path, relative to the
> linking file, against what git says exists; and the `#anchor`, against the
> target's actual headings (slugged the way GitHub slugs them, `-1`/`-2` for
> repeats) and its explicit `<a id="…">` anchors. Links inside fenced code
> blocks and external `scheme:` targets are skipped. It reads tracked *and*
> untracked-but-not-ignored files, so a broken link is caught before it is
> staged. Deliberately **not** scanned as a source:
> `tools/oracle_bridge/communicationmod-oracle/` (vendored upstream — its links
> are not ours to repair, so a failure there could only be silenced), though it
> is still indexed as a *target*. `docs/stage-a-log.md` and `docs/stage-b-log.md`
> **are** scanned: both resolve completely today, and pre-excluding a file that
> passes only guarantees nobody notices when it stops. A line carrying `link-ok`
> is skipped — that hatch is for an append-only Log that must record a reference
> to something since moved, not for a link you cannot be bothered to fix. **If
> it fires because the target genuinely vanished, the doc may be what needs
> restoring; do not repoint the link until you know which.**

**Benchmark A/B must be interleaved.** This box drifts by more than the effects
being measured. Two separate measurements this project has taken were wrong
until re-run as interleaved A/B/A/B pairs — one reported a spurious −1.4%, the
other a spurious 5.1s saving that a direct measurement showed to be noise. Build
both binaries, alternate them, and report the pair-wise delta with a noise
estimate. If load makes a number untrustworthy, **say it is unmeasured** rather
than publishing it.

> **ELIMINATED 2026-07-25: `tools/bench_ab.sh` — the only sanctioned way to
> compare two builds.** It takes two Google Benchmark binaries and *can only*
> measure interleaved (a discarded warm-up pair, then A B A B … for `-n`
> pairs), parses `items_per_second=<N>M/s`, and prints every pair's delta plus
> the mean, sd and standard error.
>
> ```bash
> tools/bench_ab.sh -n 5 /tmp/bench_A /tmp/bench_B          # inside WSL
> tools/wsl_run.sh --script tools/bench_ab.sh -n 5 A B      # from Windows
> ```
>
> When `|mean| <= 2 × sem` it prints `RESULT: UNMEASURED` and exits **3**
> instead of a headline — that is a valid answer meaning "the box is louder than
> the effect", and it is what you report. Exit 0 carries the delta with its 95%
> band. Calibration on this box, two copies of one binary: per-pair deltas
> ranged −2.8% … +2.4% and the verdict was UNMEASURED — that spread is the size
> of the effects both bad measurements above claimed to have found. The script
> never builds anything, so it cannot measure the wrong tree; comparing two
> commits means building each into its own tree and passing both binaries. A run
> that yields no result (crash, assert, empty filter) is an error, never half a
> comparison.

**`git branch --merged` and `git cherry` both lie here.** Integration edits the
ledger while cherry-picking, so a landed branch's patch-id no longer matches
anything on `master` — both tools report the work as unlanded. To check whether
a branch's work is in: match its commit subject's task id against `git log
master --grep`. Do not delete a branch on the strength of `--merged` alone. Also
remove a worktree *before* deleting its branch; the reverse order fails.

**A brief is only meaningful at a revision, and a worktree outlives its task.**
Two halves of one lifecycle, each of which failed after being written down.
*Dispatch:* a worktree was created for an agent while this file had an
uncommitted edit, so the brief cited a section that did not exist at the agent's
base commit and cost a round trip — §2 already said "never dispatch on a dirty
tree", and quoting `master` rather than a sha means the brief describes a moving
target. *Retirement:* landed worktrees piled up to 16.3 GB across 11 directories
until `git worktree list` was unreadable, and the `remove` → `branch -d` →
`prune` order kept being got wrong.

> **ELIMINATED 2026-07-25: `tools/task_worktree.sh` (+ `tools/task_worktree.cmd`
> for cmd/PowerShell callers) is now the sanctioned way to open and retire a
> task worktree.** `create <task>` **refuses while the repo is dirty**, printing
> what is dirty and why it matters, and on success prints the base commit sha to
> quote in the brief. `land <task>` does the removal in the load-bearing order
> and **refuses first** if the tree has uncommitted changes or if the branch is
> not contained in `master` — the same condition `git branch -d` refuses on,
> checked before anything is destroyed rather than halfway through. There is
> deliberately **no `-D` or `--force` path**: if it refuses, read the reason (the
> cherry-picked-landing case above is real, and its answer is `git log master
> --grep`, not `-D`). `list` prints every worktree with its branch, whether that
> branch is in `master`, and whether the tree is dirty — the "which of these can
> I delete" question that used to be answered by hand.
>
> Run it from the **Windows host**; it refuses under WSL, where git cannot read
> a linked worktree's `gitdir: D:/…` (§6). That is also why the `.cmd` shim
> resolves Git-for-Windows' own `bash.exe` rather than the `bash` on the
> PowerShell `PATH`, which is WSL's.

**A comment asserting "X does not exist yet" is a bug signal.** Two real defects
were found this way, not by a failing test: a spawn gate hard-coded `false`
under a comment reading "always false until POWER cards land" — and they had
landed; and a relic handler left empty for the same reason. When you meet a
comment that justifies inert code by a missing prerequisite, **check whether the
prerequisite arrived**. Prefer comments that describe the mechanism and cite the
Java, because those go stale visibly; a comment naming a task id goes stale
silently.

**`default:` in a dispatch switch turns a missing implementation into a silent
no-op.** Every registry-driven dispatch surface is now a generated table whose
entries are `extern` declarations, so an unimplemented row is a **link error**
instead of a shrug. When adding a new dispatch surface, reproduce that property
and *demonstrate* it (add a row with no body, capture the linker error, revert)
rather than assuming it holds.

**Hoisting a lookup out of a loop can be slower.** Moving a relic-array scan
above a hand loop measured ~22% slower on `bench_advance`: the relic mirror is a
region of `CombatState` the loop otherwise never touches, so the hoist dragged a
cold cache line into every call, while the original paid it almost never. Cache
behaviour beats instruction count here — measure before "optimising".

---

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
