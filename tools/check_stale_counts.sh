#!/usr/bin/env bash
# Fail when a committed file asserts a specific test-pass count.
#
# WHY THIS EXISTS (conventions.md §7, §8): a pass count is true only on the day
# it is written. A stale one (454, while the tree was at 526) was copied out of
# a task Log into four separate task briefs, and a `tests/CMakeLists.txt`
# comment asserting a suite total was stale again by the time anyone read it.
# `ctest -N | tail -1` is the source of truth; a number in a file is a claim
# nobody re-checks.
#
# Usage:  tools/check_stale_counts.sh [path...]      (default: the whole repo)
# Exit:   0 clean, 1 violations found, 2 usage/environment error.
#
# Run it from Git-Bash on the Windows host, or in CI. NOT through WSL: it is a
# git-side check, and WSL's git cannot read a linked worktree's `gitdir: D:/...`
# (conventions §6). Note that `bash` on the PowerShell PATH is WSL's bash.
#
# WHAT IS FLAGGED (a line must look like an assertion about a test run). The
# three examples below carry the `stale-count-ok` escape hatch, which is also
# what using it looks like -- this script does not exempt itself:
#   * <N>/<N>, both halves equal, N >= 10, on a line that also mentions
#     test / suite / ctest / gtest / pass / green:  "526/526 gtest cases"  stale-count-ok
#   * "<N> tests pass|passed|passing|green":        "all 570 tests pass"   stale-count-ok
#   * "<N>/<M> tests":                              "569/570 tests"        stale-count-ok
#
# WHAT IS NOT SCANNED, and why:
#   * docs/stage-*-log.md  - append-only archives of past runs. An old count is
#                            the point of the file.
#   * docs/stage-*-tasks.md, on `[x]` / `[!]` lines only - a landed task's index
#                            line records what was green when it landed, which
#                            conventions §2 requires. `[ ]` and `[~]` lines ARE
#                            scanned: a pending task brief quoting a count is
#                            precisely the incident above.
#   * tests/golden/, build/ - fixture data and build output.
#   * A line containing `stale-count-ok` is skipped. That escape hatch is for
#     prose describing a historical incident, not for new claims.
#
# Version numbers, dates, RNG values, Java line citations (`Foo.java:289/363`)
# and probability splits (`65/25/10`) do not match: the halves differ, or the
# line has no test vocabulary.
set -euo pipefail

case ${1:-} in -h|--help)
    awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"; exit 0 ;;
esac

# This is a git-side check, so run it from the Windows host (or in CI), never
# through WSL: WSL's git cannot resolve the `gitdir: D:/...` path that
# `git worktree add` writes, so it reports a linked worktree as "not a git
# repository" (conventions §6).
git rev-parse --show-toplevel >/dev/null 2>&1 || {
    echo "check_stale_counts: git does not see a repository here." >&2
    [ "$(uname -s)" = Linux ] && [ -f .git ] && echo \
        "  In a linked worktree under WSL this is the §6 gitdir trap -- run it from Windows." >&2
    exit 2
}
cd "$(git rev-parse --show-toplevel)"

# One broad candidate sweep; awk below decides. -I skips binary files.
# --untracked so a violation is caught before it is staged, not after CI runs;
# .gitignore'd paths (build/, scratch) stay out.
hits=$(git grep --untracked -nIiE \
        '[0-9]{2,5}/[0-9]{2,5}|[0-9]{2,5} +([a-z]+ +)?(tests?|cases?) +(are +)?(pass|green)' \
        -- "${@:-.}" \
        ':!build' ':!docs/stage-a-log.md' ':!docs/stage-b-log.md' ':!tests/golden' \
    | awk '
    {
        if (!match($0, /^[^:]+:[0-9]+:/)) next
        head = substr($0, 1, RLENGTH - 1)
        text = substr($0, RLENGTH + 1)
        path = substr(head, 1, index(head, ":") - 1)
        low  = tolower(text)

        if (index(low, "stale-count-ok")) next
        # A landed ledger entry is a historical record, not a live claim.
        if (path ~ /^docs\/stage-[ab]-tasks\.md$/ && low ~ /\[x\]|\[!\]/) next

        testish = (low ~ /test|suite|ctest|gtest|pass|green/)
        why = ""

        # R2 / R3: an explicit claim about tests, slash or no slash.
        if (low ~ /[0-9]{2,5} +([a-z]+ +)?(tests?|cases?) +(are +)?(pass|passes|passed|passing|green)/)
            why = "asserts a pass count in prose"
        else if (low ~ /[0-9]{2,5}\/[0-9]{2,5} *(tests?|cases?)/)
            why = "asserts an N/M test count"
        else if (testish) {
            # R1: an equal N/N ratio on a line that talks about tests.
            rest = text
            while (match(rest, /[0-9]+\/[0-9]+/)) {
                pair = substr(rest, RSTART, RLENGTH)
                rest = substr(rest, RSTART + RLENGTH)
                split(pair, p, "/")
                if (p[1] == p[2] && p[1] + 0 >= 10) { why = "asserts a " pair " pass count"; break }
            }
        }
        if (why != "") printf "%s\n    %s\n    -> %s\n", head, text, why
    }' || true)

if [ -n "$hits" ]; then
    echo "check_stale_counts: committed files assert a test-pass count." >&2
    echo "Re-derive it instead ('ctest -N | tail -1'), or drop the number." >&2
    echo >&2
    printf '%s\n' "$hits" >&2
    exit 1
fi
echo "check_stale_counts: clean"
