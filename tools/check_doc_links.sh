#!/usr/bin/env bash
# Fail when a markdown link points at a file or an anchor that does not exist.
#
# WHY THIS EXISTS (conventions.md §7, §8): a section number or an anchor in
# prose is a value nothing re-derives, exactly like a committed test count. A
# task brief cited `§7`/`§8` of conventions.md at a revision where those
# sections did not exist, and CLAUDE.md's
# `#calling-wsl-from-the-windows-host--use-toolswsl_runsh` anchor was
# hand-maintained with nothing checking it. Renaming a heading silently breaks
# every link into it.
#
# Usage:  tools/check_doc_links.sh [path...]      (default: every markdown file)
# Exit:   0 clean, 1 broken links found, 2 usage/environment error.
#
# Run it from Git-Bash on the Windows host, or in CI. NOT through WSL: it asks
# git which files exist, and WSL's git cannot read a linked worktree's
# `gitdir: D:/...` (conventions §6). `bash` on the PowerShell PATH is WSL's bash.
#
# WHAT IS CHECKED, for every inline `[text](target)` link:
#   * the file path resolves, relative to the linking file, to a tracked (or
#     untracked-but-not-ignored) file or directory;
#   * the `#anchor`, if present, exists in the target markdown file -- either as
#     a heading, slugged the way GitHub slugs it (lowercase, drop everything
#     outside [a-z0-9 _-], spaces to hyphens, `-1`/`-2` for repeats), or as an
#     explicit `<a id="...">` / `<a name="...">`.
#   * `#anchor`-only links are resolved against the linking file itself.
# External targets (anything with a `scheme:` prefix) are skipped, as is
# anything inside a fenced code block or an inline `code span` -- prose *about*
# link syntax is not a link. Heading text keeps its code-span contents, though,
# because GitHub's slug does.
#
# WHAT IS NOT SCANNED AS A SOURCE, and why:
#   * tools/oracle_bridge/communicationmod-oracle/ - vendored upstream (MIT)
#     fork source. Its links are not ours to repair, so a failure there could
#     only be silenced, never fixed. It is still indexed as a *target*: links
#     INTO it are checked.
#   * build/ - build output.
#   * A link on a line containing `link-ok` is skipped. That hatch exists for
#     docs/stage-a-log.md and docs/stage-b-log.md, which are append-only
#     archives of past task Logs and may one day need to record a reference to
#     something that has since moved. They are deliberately NOT excluded
#     wholesale: both resolve completely today, and pre-excluding a file that
#     passes only guarantees nobody notices when it stops.
#
# KNOWN LIMITS, all currently unused in this repo (verified by grep, and each
# would fail closed -- a missed link is not a false alarm): reference-style
# links (`[a]: url`), links whose text itself contains a link, and link targets
# containing parentheses.
set -euo pipefail

case ${1:-} in -h|--help)
    awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"; exit 0 ;;
esac

git rev-parse --show-toplevel >/dev/null 2>&1 || {
    echo "check_doc_links: git does not see a repository here." >&2
    [ "$(uname -s)" = Linux ] && [ -f .git ] && echo \
        "  In a linked worktree under WSL this is the §6 gitdir trap -- run it from Windows." >&2
    exit 2
}
cd "$(git rev-parse --show-toplevel)"

ls_md() { git ls-files --cached --others --exclude-standard -- "$@" ':!build'; }

# Every markdown file is an anchor target; only some are link sources.
mapfile -t targets < <(ls_md '*.md' '*.markdown')
mapfile -t sources < <(ls_md "${@:-.}" \
    ':!tools/oracle_bridge/communicationmod-oracle/**' \
    | grep -E '\.(md|markdown)$' || true)
[ ${#sources[@]} -gt 0 ] || { echo "check_doc_links: no markdown files in scope" >&2; exit 2; }

index=$(mktemp); trap 'rm -f "$index"' EXIT
git ls-files --cached --others --exclude-standard >"$index"

hits=$(awk '
function dirname(p) { return sub(/\/[^\/]*$/, "", p) ? p : "" }

# Collapse "." and ".." so docs/../CLAUDE.md == CLAUDE.md.
function norm(p,   n, i, k, part, st, out) {
    n = split(p, part, "/"); k = 0
    for (i = 1; i <= n; i++) {
        if (part[i] == "" || part[i] == ".") continue
        if (part[i] == "..") { if (k > 0) k--; continue }
        st[++k] = part[i]
    }
    for (i = 1; i <= k; i++) out = out (i > 1 ? "/" : "") st[i]
    return (p ~ /\/$/) ? out "/" : out
}

# GitHub heading slug.
function slug(t) {
    sub(/[ \t]+#*[ \t]*$/, "", t)          # closing ATX hashes and trailing space
    t = tolower(t)
    gsub(/[^a-z0-9 _-]/, "", t)            # punctuation, and every non-ASCII byte
    gsub(/ /, "-", t)
    return t
}

function report(tgt, why) { printf "%s:%d: [%s]\n    -> %s\n", FILENAME, FNR, tgt, why }

function check(tgt,   i, p, a, res) {
    gsub(/^</, "", tgt); gsub(/>$/, "", tgt)
    sub(/[ \t].*$/, "", tgt)               # drop a link title
    if (tgt == "" || tgt ~ /^[a-zA-Z][a-zA-Z0-9+.-]*:/) return   # external scheme
    i = index(tgt, "#")
    if (i > 0) { a = substr(tgt, i + 1); p = substr(tgt, 1, i - 1) } else { a = ""; p = tgt }
    if (p == "") res = FILENAME
    else {
        res = norm(dirname(FILENAME) "/" p)
        if (!(res in have)) { report(tgt, "no such file or directory: " res); return }
    }
    if (a == "") return
    if (res !~ /\.(md|markdown)$/) { report(tgt, "anchor into a non-markdown file: " res); return }
    if (!((res SUBSEP a) in anchor)) report(tgt, "no anchor \"" a "\" in " res)
}

pass == 0 {                                # the repo file list
    have[$0] = 1
    d = $0
    while (sub(/\/[^\/]+$/, "", d)) { have[d] = 1; have[d "/"] = 1 }
    next
}
FNR == 1 { fence = 0 }
/^[ \t]*(```|~~~)/ { fence = !fence; next }
fence { next }

pass == 1 {                                # collect anchors
    if (match($0, /^#{1,6}[ \t]+/)) {
        a = slug(substr($0, RLENGTH + 1))
        if (a != "") {
            n = ++seen[FILENAME, a]
            anchor[FILENAME, (n == 1 ? a : a "-" (n - 1))] = 1
        }
    }
    s = $0
    while (match(s, /<a[ \t]+(id|name)[ \t]*=[ \t]*"[^"]*"/)) {
        m = substr(s, RSTART, RLENGTH); s = substr(s, RSTART + RLENGTH)
        sub(/^[^"]*"/, "", m); sub(/"$/, "", m)
        anchor[FILENAME, m] = 1
    }
    next
}

pass == 2 {                                # check links
    if (index($0, "link-ok")) next         # deliberate stale reference (see header)
    s = $0
    gsub(/`[^`]*`/, "", s)                 # inline code is prose about links, not a link
    while (match(s, /\]\([^()]*\)/)) {
        check(substr(s, RSTART + 2, RLENGTH - 3))
        s = substr(s, RSTART + RLENGTH)
    }
}
' pass=0 "$index" pass=1 "${targets[@]}" pass=2 "${sources[@]}")

if [ -n "$hits" ]; then
    echo "check_doc_links: broken markdown links." >&2
    echo "Fix the link -- but if the TARGET vanished rather than moved, the doc may" >&2
    echo "be what needs restoring; check before repointing." >&2
    echo >&2
    printf '%s\n' "$hits" >&2
    exit 1
fi
echo "check_doc_links: clean (${#sources[@]} files scanned, ${#targets[@]} indexed)"
