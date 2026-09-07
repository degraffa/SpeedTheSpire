# Windows build entrypoint verification

Completed 2026-09-07 on base `67ffcda214f74913a094a9f1df7f288c0d13ba11`.
The orchestrator's bare `cmake --build --preset win-release` triggered an
implicit reconfigure after integration and reproduced the documented missing
vcvars/LLVM failure. The resulting cache had empty `CMAKE_CXX_FLAGS`, losing
`/EHsc`. Its build directory was preserved by the orchestrator as
`build/win-release-poisoned-20260907` before fresh configuration.

`tools/win_build.cmd` now delegates to a small PowerShell helper that validates
one Windows preset, rejects a pre-existing cache without `/EHsc`, initializes
vcvars64 and LLVM, then configures and builds. Remaining arguments pass to
`cmake --build`, including `--target` and `--parallel`. Optional
`STS_VCVARS64` and `STS_LLVM_BIN` overrides support other installations.
A nonblocking `FileShare.None` lock on `build/.win_build.lock` covers configure
and build, allowing only one helper per worktree; `finally` releases it.
It reports the selected worktree and toolchain, never runs ctest, and never
removes or renames build directories. [Conventions](../conventions.md#a-failed-win--configure-poisons-the-cache-ehsc-silently-vanishes)
now prescribes the helper even for an already configured tree.

Actual invocations in the task's isolated worktree:

| Invocation | Observed result |
|---|---|
| `tools/win_build.cmd --help` | Usage printed, exit 0. |
| `tools/win_build.cmd release` | Unsupported preset rejected, exit 2. |
| `tools/win_build.cmd win-debug --preset win-asan` | Build-argument preset override rejected before configuring, exit 2. |
| `tools/win_build.cmd win-debug --target replay_run_diff --parallel 4` | Fresh configure and target build succeeded, exit 0. |
| Same target with `--parallel 2` and both toolchain overrides set | Reconfigure succeeded; Ninja reported no work, exit 0. |
| Two real debug helper invocations overlapped | Owner configured/built successfully (exit 0); contender named the active helper and refused immediately (exit 2). A subsequent invocation succeeded after lock release. |
| `tools/win_build.cmd win-asan --target replay_run_diff` with copied poisoned cache metadata | Refused before toolchain setup or CMake, exit 2; named the exact directory to move aside and explained the fresh-configure remedy. Cache SHA256 unchanged. |

For the refusal witness, only the quarantined `CMakeCache.txt` was copied
read-only from the main checkout into this task's ignored `build/win-asan`.
The helper was never run in the main checkout. The successful debug cache
retains `/DWIN32 /D_WINDOWS /EHsc`. No unit tests were written or run; this
task changes the build entrypoint and documentation only.

Raw logs remain at
`D:/STS_BG_Mod/_oracle_data/build_entrypoint_20260907/`.
Repository-wide Markdown-link and stale-count checks, plus `git diff --check`,
pass.
