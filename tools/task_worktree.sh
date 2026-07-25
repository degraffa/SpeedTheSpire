#!/usr/bin/env bash
# The sanctioned way to create and retire a task worktree.
#
# WHY THIS EXISTS (conventions.md §2, §7). Two rules that were written down and
# then skipped anyway:
#
#   * "Never dispatch on a dirty tree." A worktree branches from the *committed*
#     tree, so an uncommitted edit in the main repo is invisible to the agent
#     that gets dispatched. A brief citing a section that existed only in the
#     orchestrator's working copy sent an agent to a revision without it.
#   * "A worktree is deleted as part of landing." They reached 16.3 GB across 11
#     directories; the real damage was that `git worktree list` stopped being
#     readable, so the one worktree still holding uncommitted work looked
#     exactly like the ten that did not.
#
# Usage:
#   tools/task_worktree.sh create <name> [base]   refuse if dirty; print base sha
#   tools/task_worktree.sh land <name>            remove worktree, delete branch
#   tools/task_worktree.sh list                   what exists, what is safe to land
#
# Exit: 0 ok, 1 refused (dirty tree / unlanded branch), 2 usage or environment.
#
# Run it from the **Windows host** (Git-Bash, or tools\task_worktree.cmd from
# cmd/PowerShell). Not from WSL: WSL's git cannot resolve the `gitdir: D:/...`
# path that `git worktree add` writes, so every worktree looks like "not a git
# repository" (conventions §6). Note that `bash` on the PowerShell PATH is WSL's
# bash -- that is what the .cmd shim exists to avoid.
#
# There is deliberately no `-D` / `--force` path. `git branch -d` refusing is
# the last safety net against discarding unlanded work; if it refuses, read the
# reason (conventions §8: a cherry-picked landing is not an ancestor, so the
# branch must be confirmed by task id against `git log master --grep`).
#
# Env: STS_BASE_BRANCH (default master).
set -euo pipefail

base_branch=${STS_BASE_BRANCH:-master}
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)

usage() {  # print the header comment block, minus its leading '# '
    awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
    exit "${1:-2}"
}
[ $# -gt 0 ] || usage
case $1 in -h|--help) usage 0 ;; esac

if [ "$(uname -s)" = Linux ] && grep -qi microsoft /proc/version 2>/dev/null; then
    echo "task_worktree: run this from the Windows host, not WSL -- WSL's git cannot" >&2
    echo "  read a linked worktree's 'gitdir: D:/...' file (conventions §6)." >&2
    exit 2
fi

# The main worktree is always the first entry of `worktree list --porcelain`,
# so this works whether the script is invoked from the repo or from a worktree.
repo=$(git -C "$script_dir" worktree list --porcelain 2>/dev/null \
        | sed -n '1s/^worktree //p') || true
[ -n "${repo:-}" ] || { echo "task_worktree: no git repository at $script_dir" >&2; exit 2; }
wt_root=$(dirname -- "$repo")/_wt

# "<path>\t<branch>" per registered worktree, main worktree first.
worktrees() {
    git -C "$repo" worktree list --porcelain | awk '
        /^worktree /  { p = substr($0, 10); b = "(detached)" }
        /^branch /    { b = substr($0, 8); sub(/^refs\/heads\//, "", b) }
        /^$/          { if (p != "") print p "\t" b; p = "" }
        END           { if (p != "") print p "\t" b }'
}
contained() { git -C "$repo" merge-base --is-ancestor "$1" "$base_branch" 2>/dev/null; }
dirty()     { [ -n "$(git -C "$1" status --porcelain 2>/dev/null)" ]; }

cmd=$1; shift
case $cmd in
create)
    [ $# -ge 1 ] && [ $# -le 2 ] || usage
    name=$1 base=${2:-$base_branch} wt=$wt_root/$1
    if dirty "$repo"; then
        echo "task_worktree: refusing to create -- $repo is dirty:" >&2
        git -C "$repo" status --porcelain >&2
        echo >&2
        echo "A worktree branches from the COMMITTED tree, so none of the above is" >&2
        echo "visible to the agent you are about to dispatch: a brief quoting an" >&2
        echo "uncommitted edit sends it to a revision where that text does not exist." >&2
        echo "Commit or stash first (conventions §2, 'never dispatch on a dirty tree')." >&2
        exit 1
    fi
    git -C "$repo" rev-parse -q --verify "$base^{commit}" >/dev/null \
        || { echo "task_worktree: unknown base '$base'" >&2; exit 2; }
    git -C "$repo" worktree add "$wt" -b "$name" "$base"
    sha=$(git -C "$repo" rev-parse "$base")
    echo
    echo "task_worktree: created"
    echo "  worktree  $wt"
    echo "  branch    $name"
    echo "  base      $sha  ($(git -C "$repo" log -1 --format=%s "$sha"))"
    echo
    echo "Quote that sha in the brief, not '$base_branch': $base_branch moves while the agent works,"
    echo "and a section number is only meaningful at a revision."
    ;;

land)
    [ $# -eq 1 ] || usage
    name=$1 wt=$wt_root/$1
    git -C "$repo" rev-parse -q --verify "refs/heads/$name" >/dev/null \
        || { echo "task_worktree: no branch '$name'" >&2; exit 2; }
    registered=$(worktrees | awk -F'\t' -v n="$name" '$2 == n { print $1; exit }')

    if [ -n "$registered" ] && dirty "$registered"; then
        echo "task_worktree: refusing to land -- $registered has uncommitted changes:" >&2
        git -C "$registered" status --porcelain >&2
        exit 1
    fi
    if ! contained "$name"; then
        echo "task_worktree: refusing to land -- '$name' is not contained in $base_branch." >&2
        echo "  'git branch -d' would refuse too, and that refusal is the safety net." >&2
        echo "  Commits not on $base_branch:" >&2
        git -C "$repo" log --oneline "$base_branch..$name" >&2
        echo >&2
        echo "  Land it first. If it WAS landed by cherry-pick, this check cannot see" >&2
        echo "  that (conventions §8: integration edits the ledger, so the patch-id no" >&2
        echo "  longer matches) -- confirm by task id with 'git log $base_branch --grep'" >&2
        echo "  and then delete by hand. Do not reach for -D." >&2
        exit 1
    fi

    # Order is load-bearing: `git branch -d` fails while a worktree still
    # references the branch (conventions §2).
    if [ -n "$registered" ]; then
        echo "+ git worktree remove $registered"
        git -C "$repo" worktree remove "$registered"
    else
        echo "  (no worktree registered for '$name')"
    fi
    echo "+ git branch -d $name"
    git -C "$repo" branch -d "$name"
    echo "+ git worktree prune"
    git -C "$repo" worktree prune
    echo "task_worktree: landed '$name'"
    ;;

list)
    [ $# -eq 0 ] || usage
    printf '%-44s %-20s %-12s %s\n' PATH BRANCH "IN-$(echo "$base_branch" | tr 'a-z' 'A-Z')" DIRTY
    main=1
    while IFS=$'\t' read -r p b; do
        if [ "$main" = 1 ]; then in="(main)"; main=0
        elif [ "$b" = "(detached)" ]; then in="?"
        elif contained "$b"; then in="yes"
        else in="no"; fi
        if dirty "$p"; then d="YES"; else d="no"; fi
        printf '%-44s %-20s %-12s %s\n' "$p" "$b" "$in" "$d"
    done < <(worktrees)
    echo
    echo "in-$base_branch=yes and dirty=no  ->  tools/task_worktree.sh land <branch>"
    echo "anything else is either unlanded work or a tree someone is still using."
    ;;

*)  echo "task_worktree: unknown command '$cmd'" >&2; usage ;;
esac
