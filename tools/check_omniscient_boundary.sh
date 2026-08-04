#!/usr/bin/env bash
# Fail when training-facing code reaches the OMNISCIENT (full-state) reads.
#
# WHY THIS EXISTS (task T0.7; docs/training-plan.md §1, §2.1). The engine has
# two observation surfaces and only one of them respects the information
# contract:
#   * PublicView (include/sts/engine/public_view.hpp) -- what a perfect-memory
#     player could know, mask included, hashed by public_hash().
#   * the omniscient observation (include/sts/engine/omniscient_observation.hpp)
#     -- a raw CombatState read, true intents and all. Legitimate for a debug
#     dump, a diff harness, or an omniscient baseline; a silent information leak
#     if an actor or a training pipeline reads it.
# Nothing in the type system separates the two: both take a state and return
# bytes. So the second surface is spelled with a token nothing else in the tree
# uses -- `omniscient` -- and this script is the grep that makes the spelling
# enforceable rather than decorative (conventions.md §7's preference order:
# automate the check, do not re-document the rule).
#
# Usage:
#   tools/check_omniscient_boundary.sh              # the repo's checked set
#   tools/check_omniscient_boundary.sh --scan DIR   # scan DIR as if it were
#                                                   #   training-facing (repeatable)
# Exit: 0 clean, 1 violations found, 2 usage/environment error.
#
# Run it from Git-Bash on the Windows host, or in CI. The default mode is a
# git-side check, so it must NOT go through WSL: WSL's git cannot read a linked
# worktree's `gitdir: D:/...` (conventions §6), and `bash` on the PowerShell
# PATH is WSL's bash. `--scan DIR` uses no git at all, which is what lets
# tests/omniscient_boundary_test.cpp run it against a fixture directory on
# either host.
#
# WHAT IS SCANNED in the default mode -- deliberately conservative, and both
# halves are listed in full in the script body below (kTrainingPaths,
# kDenylist):
#   1. TRAINING-FACING TREES, whole: include/sts/training, src/training,
#      tools/training, tests/training. None exist yet -- the training program is
#      a separate repo (CLAUDE.md), and T1.1 is what creates its in-repo
#      surface. They are declared now so the guard is already in place on the
#      day the first one appears, rather than being remembered afterwards. A
#      path that does not exist is skipped, not an error.
#   2. A DENYLIST of individual files that must stay on the public side of the
#      boundary whatever tree they live in: the public-view encoder and the
#      belief sampler. Those two ARE the information contract -- if either ever
#      reached the omniscient surface, every twin test downstream would still
#      pass while the leak sat in the observation.
# Both are scanned as tracked-or-untracked-but-not-ignored files, so a violation
# is caught before it is staged.
#
# WHICH FILES, in either mode: code and build files only -- .h/.hpp/.ipp, .c/
# .cc/.cpp/.cxx, .py, .cmake and CMakeLists.txt. Markdown and other prose is not
# scanned: a document is allowed to talk about the boundary (this script's own
# header does), and a doc reference reads no state.
#
# ESCAPE HATCH: a line containing `omniscient-boundary-ok` is skipped. It exists
# for a comment that names the other surface to contrast with it -- public_view
# .cpp cites the omniscient encoder for Runic Dome parity, which is the reason
# the parity is correct. It is not for code: a hatched line that actually reads
# the omniscient surface is exactly the leak this script exists to catch, and a
# reviewer sees the token.
set -euo pipefail

case ${1:-} in -h|--help)
    awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"; exit 0 ;;
esac

# The forbidden spelling. Case-insensitive, so `Omniscient*` type names and
# `omniscient_*` function/member names are one pattern.
readonly kPattern='omniscient'
readonly kHatch='omniscient-boundary-ok'

# Code/build files only (see the header). Anchored at the end of the path.
readonly kCodeRe='(\.(h|hpp|ipp|c|cc|cpp|cxx|py|cmake)|(^|/)CMakeLists\.txt)$'

# The training-facing trees (whole-tree scan), and the individual files that
# must stay public-side wherever they live.
readonly kTrainingPaths=(include/sts/training src/training tools/training tests/training)
readonly kDenylist=(
    include/sts/engine/public_view.hpp
    src/engine/public_view.cpp
    src/engine/public_hash.cpp
    include/sts/engine/resample.hpp
    src/engine/resample.cpp
)

scan_dirs=()
while [ $# -gt 0 ]; do
    case $1 in
        --scan)
            [ $# -ge 2 ] || { echo "check_omniscient_boundary: --scan needs a directory" >&2; exit 2; }
            [ -d "$2" ] || { echo "check_omniscient_boundary: not a directory: $2" >&2; exit 2; }
            scan_dirs+=("$2"); shift 2 ;;
        *)
            echo "check_omniscient_boundary: unrecognised argument: $1" >&2
            echo "Usage: $0 [--scan DIR]...   (--help for the full description)" >&2
            exit 2 ;;
    esac
done

files=()
if [ ${#scan_dirs[@]} -gt 0 ]; then
    # Fixture / ad-hoc mode: no git, no repo assumptions. Select by name only --
    # never by a permission predicate, which matches everything under a DrvFs
    # /mnt/d mount (conventions §6).
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        files+=("$f")
    done < <(find "${scan_dirs[@]}" -type f | grep -E "$kCodeRe" || true)
else
    git rev-parse --show-toplevel >/dev/null 2>&1 || {
        echo "check_omniscient_boundary: git does not see a repository here." >&2
        [ "$(uname -s)" = Linux ] && [ -f .git ] && echo \
            "  In a linked worktree under WSL this is the §6 gitdir trap -- run it from Windows." >&2
        exit 2
    }
    cd "$(git rev-parse --show-toplevel)"

    paths=()
    for p in "${kTrainingPaths[@]}"; do
        [ -d "$p" ] && paths+=("$p")
    done
    for p in "${kDenylist[@]}"; do
        [ -f "$p" ] || {
            echo "check_omniscient_boundary: denylisted file is missing: $p" >&2
            echo "  It was renamed or deleted -- update kDenylist in this script so the" >&2
            echo "  boundary keeps covering whatever replaced it." >&2
            exit 2
        }
        paths+=("$p")
    done
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        files+=("$f")
    done < <(git ls-files --cached --others --exclude-standard -- "${paths[@]}" \
             | grep -E "$kCodeRe" || true)
fi

if [ ${#files[@]} -eq 0 ]; then
    echo "check_omniscient_boundary: clean (no files in the checked set)"
    exit 0
fi

hits=$(grep -nHiE "$kPattern" "${files[@]}" | grep -v -- "$kHatch" || true)

if [ -n "$hits" ]; then
    echo "check_omniscient_boundary: training-facing code references the omniscient" >&2
    echo "(full-state) observation surface. Read PublicView instead -- see" >&2
    echo "include/sts/engine/omniscient_observation.hpp's header for the rule." >&2
    echo >&2
    printf '%s\n' "$hits" >&2
    exit 1
fi
echo "check_omniscient_boundary: clean (${#files[@]} files checked)"
