#!/usr/bin/env bash
# Regression for check_ninja_deps.sh -- the guard that finds object files a
# Ninja tree will never rebuild because their recorded dependency list is empty.
#
# Run from Windows without starting a build:
#   tools\wsl_run.cmd --script tools/test_ninja_deps_guard.sh
#
# The fixture is a two-edge Ninja project with no compiler in it: one edge
# writes a TRUNCATED depfile (a target line with no prerequisites, which is what
# a half-written .ninja_deps record looks like from Ninja's side) and one writes
# a well-formed depfile. The guard must tell them apart, must not delete
# anything until asked, and the deletion it does perform must be the thing that
# makes Ninja rebuild.
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
guard=$script_dir/check_ninja_deps.sh

fail() {
    echo "test_ninja_deps_guard: FAIL: $1" >&2
    [ $# -lt 2 ] || printf '%s\n' "$2" >&2
    exit 1
}

command -v ninja >/dev/null 2>&1 || fail "ninja is not on PATH"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/build.ninja" <<'NINJA'
rule truncated
  command = printf '%s: \n' $out > $out.d && touch $out
  depfile = $out.d
  deps = gcc

rule healthy
  command = printf '%s: %s\n' $out $in > $out.d && touch $out
  depfile = $out.d
  deps = gcc

build stale.o: truncated stale.src
build good.o: healthy good.src
NINJA
: > "$work/stale.src"
: > "$work/good.src"
ninja -C "$work" >/dev/null

ninja -C "$work" -t deps | grep -q 'stale\.o: #deps 0,' \
    || fail "fixture did not produce an empty dependency record"

# An untouched build tree with a clean record set is silent and exits 0. Prove
# that first, so the exit-3 assertions below cannot pass vacuously.
status=0
bash "$guard" "$work/does-not-exist" >/dev/null 2>&1 || status=$?
[ "$status" = 0 ] || fail "an unconfigured directory should be exit 0, got $status"

# Detection: names the truncated object, leaves the well-formed one alone, and
# deletes NOTHING without --repair.
status=0
output=$(bash "$guard" "$work" 2>&1) || status=$?
[ "$status" = 3 ] || fail "detection should exit 3, got $status" "$output"
case $output in *stale.o*) ;; *) fail "truncated object not named" "$output" ;; esac
case $output in *good.o*) fail "well-formed object wrongly named" "$output" ;; esac
[ -f "$work/stale.o" ] || fail "detection must not delete anything"

# Repair: removes exactly the truncated object.
status=0
output=$(bash "$guard" --repair "$work" 2>&1) || status=$?
[ "$status" = 3 ] || fail "repair should exit 3, got $status" "$output"
[ ! -f "$work/stale.o" ] || fail "repair did not remove the truncated object"
[ -f "$work/good.o" ] || fail "repair removed a well-formed object"

# A record whose output is already gone cannot mislead a build, so a second pass
# is clean rather than a permanent complaint.
status=0
bash "$guard" "$work" >/dev/null 2>&1 || status=$?
[ "$status" = 0 ] || fail "second pass should be clean, got $status"

# The point of deleting the object: Ninja rebuilds it, which is what replaces
# the truncated record with a real one.
ninja -C "$work" >/dev/null
[ -f "$work/stale.o" ] || fail "ninja did not rebuild the removed object"

echo "test_ninja_deps_guard: PASS"
