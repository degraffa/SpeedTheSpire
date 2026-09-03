# CI workflows

**2026-09-03 owner directive (docs/conventions.md §1): this project neither
writes nor runs unit tests. CI does not run `ctest`.** The existing gtest
suites stay in the tree but are not maintained, extended, or run as
acceptance. Read the directive in full in
[conventions.md](../../docs/conventions.md#1-statuses) before changing either
workflow below — it is what "green" means now, and it is binding on both
files.

## What "green" means now

A push is accepted when:

1. **The six CMake presets still build** (`debug`, `asan`, `release` on
   Linux; `win-debug`, `win-asan`, `win-release` on Windows — see
   [`CMakePresets.json`](../../CMakePresets.json)). `ci.yml` builds
   `debug`/`asan`/`release` on every push and `win-debug` on every push; the
   two Windows presets it does not build every push
   (`win-asan`/`win-release`) are built by hand before landing any task that
   touches the build (conventions §6) and are not, today, a per-push gate —
   see the note in `ci.yml`'s `windows` job for why `win-debug` was chosen.
2. **The committed oracle corpora replay zero-diff**
   (`tools/corpus_replay.sh`) — all three corpora
   (`tests/golden/oracle_corpus/act1_a20_50`, `three_act_a20_5`,
   `keys_a20_4`), all three `replay_run_diff` comparison modes (`--replay`,
   `--costs`, `--masks`), and every mode's injected-divergence negative
   control **fails loud** (the control's whole job is proving the comparison
   has teeth — a control that passes clean is the failure).
3. **The leak-gate and Stage-A fixture binaries pass, run directly** —
   `fixture_oracle_test`, `twin_test`, `tripwire_test` — invoked as whole
   processes rather than through `ctest`'s per-gtest-CASE entries, so each
   binary's own exit code is what fails the step.
4. **The `stale-numbers` job's three guards stay clean**
   (`tools/check_stale_counts.sh`, `tools/check_doc_links.sh`,
   `tools/check_omniscient_boundary.sh`) — unchanged by this directive; they
   were never test-shaped acceptance to begin with.

None of the above is "tests pass." A gtest binary failing under this scheme
is a real run producing a wrong answer — a replay divergence, a leaked hidden
byte, a fixture mismatch — not a red unit-test count. If you find yourself
writing a *new* gtest case as the acceptance for a change, that is the
directive telling you the acceptance surface is a real run instead: a
witness capture through `replay_run_diff --replay` (or `--combat`/`--vitals`
for a combat-internal drift), or an addition to the committed corpus under
`tests/golden/oracle_corpus/`.

## `ci.yml`

Two Linux jobs (`stale-numbers`, `build-and-test` — build-and-test is a
`[debug, asan, release]` matrix) and one Windows job (`windows`, `win-debug`
via a pinned clang-cl). Every job's steps are commented in place with why
each one exists; the summary above is only the "what does green mean" answer,
not a restatement of the file.

**The LLVM pin.** The Windows job installs LLVM **22.1.8** by exact tagged
release (`llvmorg-22.1.8`) rather than trusting whatever clang ships on the
`windows-latest` image, and fails loudly (`clang-cl --version` checked
against the pin) if it does not resolve. This exists because of a real,
already-hit defect: googletest 1.15.2 (and 1.17.0) fails to compile its own
headers under clang-cl's `/W4 /WX` once clang added `-Wcharacter-conversion`
(worked around in `tests/CMakeLists.txt` with a scoped `/WX-` on third-party
targets only, never on our own code) — see the long comment at the top of
`tests/CMakeLists.txt`. A newer clang could add the next such warning at any
time; an unpinned "latest LLVM" install would then turn a green Windows job
red for a dependency's problem, not an engine regression, on a schedule
nobody chose. 22.1.8 is the exact version already verified locally
(`docs/stage-b-tasks.md`'s build/toolchain effort row: "clang-cl 22.1.8,
presets win-debug / win-release / win-asan"), so the Windows job either
matches the verified toolchain or names the mismatch.

**What the Windows job cannot / does not do**, honestly scoped:

- It builds and verifies **`win-debug` only**, not all three `win-*`
  presets. `win-release` is LTO across roughly 40 executables — expensive on
  every push — and the byte-identical claim already on record (six presets,
  three hosts' worth of compilers, one hash) means a `win-debug` zero-diff
  result is not weaker evidence of engine correctness than a `win-release`
  one; it is only a faster one. `win-asan` is likewise not run per-push.
  Both remain a hand-run bar item before landing build/toolchain-adjacent
  work (conventions §6), not a CI gate today — extending the Windows matrix
  the way the Linux job is a matrix is future scope, not claimed by S3.66.
- It needs no WSL step. Every tool it runs (`cmake`/`ninja` via clang-cl,
  `tools/corpus_replay.sh`, the three real-run binaries) runs under
  Git-for-Windows' own `bash` — the same shell `tools/task_worktree.sh`
  already assumes exists on a Windows host — so none of the §6 WSL traps
  (the worktree `.git` gitdir path, `D:` DrvFs permission bits) apply to a
  `windows-latest` runner, which has no WSL and no `D:` drive at all.
- It does not run the nightly sampler-distributional suite
  (`tools/dist_check/sampler_dist.sh`) — that stays exactly where
  `nightly.yml` already puts it, unchanged by this task.

## `nightly.yml`

Unchanged by S3.66. It runs the pre-registered sampler distributional suite
on a schedule, which is deliberately **not** part of either push job — see
the file's own header comment for why.
