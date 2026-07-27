#!/usr/bin/env bash
# Find (and optionally repair) object files a Ninja build tree will never
# rebuild because their recorded header dependencies are empty.
#
#   tools/check_ninja_deps.sh [--repair] [--quiet] <build-dir> [<build-dir>...]
#
# Exit 0 = every dependency record is well formed; 3 = at least one empty record
# was found (and deleted, with --repair); 2 = usage / unusable build directory.
#
# WHAT IT LOOKS FOR AND WHY THAT IS ALWAYS A BUG
#
# Ninja stores each compiled object's real header list in `.ninja_deps` and
# consults it to decide whether the object is out of date. `ninja -t deps`
# prints one record per object:
#
#   src/.../advance.cpp.o: #deps 82, deps mtime 1785181483242608400 (VALID)
#       ../../src/engine/advance.cpp
#       ../../include/sts/engine/combat_state.hpp
#       ...
#
# A well-formed record ALWAYS lists at least the translation unit's own source
# file, so `#deps 0` marked VALID is not a translation unit that happens to
# include nothing -- it is a truncated record. Ninja believes such an object
# depends on nothing at all, so no header edit will ever mark it dirty and it
# stays in the archive, at its old layout, for the life of the build tree.
#
# THE INCIDENT. `libsts_engine.a` in one build tree carried three monster
# translation units compiled a day earlier, against `CombatState` = 3928 bytes /
# `MonsterState` = 116 / `PowerSlot` = 4, while every other object in the SAME
# archive used 4696 / 212 / 8 -- the layout after instanced powers landed. A
# monster's init then wrote its fields at the old offsets, scribbling the live
# combat, and the run layer's legal-action mask came back EMPTY: a combat in
# which not even END_TURN was legal. The seed sweep reported it as a
# `no_legal_moves` dead end and the source was blamed for two hours; the sources
# were fine and a clean tree was green. Nothing in the build announced itself --
# `cmake --build` said everything was up to date, and it kept saying so.
#
# HOW A RECORD GETS TRUNCATED: two Ninja processes writing one build directory
# (conventions.md §6 -- since eliminated for `wsl_run.sh` by a per-build-tree
# lock), or a build killed mid-write. The lock stops NEW corruption; it cannot
# see damage already recorded, which is why this check exists and why
# `wsl_run.sh` runs it before every build.
#
# The repair is to delete the affected objects: a missing output is dirty, so
# Ninja recompiles it and writes a correct record in its place. `.ninja_deps` as
# a whole is deliberately left alone -- discarding it would rebuild the world.
set -euo pipefail

repair=0 quiet=0 dirs=()
for arg in "$@"; do
    case $arg in
        --repair) repair=1 ;;
        --quiet) quiet=1 ;;
        -h|--help)
            awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
            exit 0 ;;
        -*) echo "check_ninja_deps: unknown option '$arg'" >&2; exit 2 ;;
        *) dirs+=("$arg") ;;
    esac
done
[ ${#dirs[@]} -gt 0 ] || { echo "check_ninja_deps: need a build directory" >&2; exit 2; }

command -v ninja >/dev/null 2>&1 || {
    echo "check_ninja_deps: ninja is not on PATH" >&2; exit 2; }

found=0
for dir in "${dirs[@]}"; do
    # A tree that was never configured has nothing to check -- that is not an
    # error, it is the first-build case.
    [ -f "$dir/build.ninja" ] || continue

    # `-t deps` reads .ninja_deps only; it builds nothing.
    empty=$(ninja -C "$dir" -t deps 2>/dev/null |
            sed -n 's/^\(.*\): #deps 0, deps mtime .*$/\1/p') || true
    [ -n "$empty" ] || continue

    while IFS= read -r out; do
        [ -n "$out" ] || continue
        # Only a record whose output still exists can mislead a build; a record
        # for an already-deleted output is harmless and self-correcting.
        [ -f "$dir/$out" ] || continue
        found=$((found + 1))
        if [ "$repair" = 1 ]; then
            rm -f "$dir/$out"
            [ "$quiet" = 1 ] || echo "check_ninja_deps: removed $dir/$out (empty dependency record)"
        else
            [ "$quiet" = 1 ] || echo "check_ninja_deps: $dir/$out has an empty dependency record"
        fi
    done <<< "$empty"
done

if [ "$found" -gt 0 ]; then
    if [ "$repair" = 1 ]; then
        echo "check_ninja_deps: repaired $found object(s) that no header edit" \
             "could ever have marked dirty; they will be recompiled now" >&2
    else
        echo "check_ninja_deps: $found object(s) carry an empty dependency" \
             "record and will never be rebuilt -- re-run with --repair" >&2
    fi
    exit 3
fi
exit 0
