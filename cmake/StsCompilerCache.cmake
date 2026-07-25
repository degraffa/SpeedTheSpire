# Shared compiler cache (ccache / sccache), wired for the way this repo is
# actually worked: several agents at once, each in its own git worktree.
#
# THE WORKLOAD THIS IS FOR
#
# An agent typically edits 2-5 files. Every other translation unit in its
# worktree is byte-identical to what a sibling worktree compiled minutes ago,
# and gtest is identical in all of them -- yet each worktree x each preset
# rebuilt all of it from cold. That is the cost being removed.
#
# WHY base_dir IS THE WHOLE BALLGAME, NOT AN OPTIMISATION
#
# ccache's default cache directory (~/.cache/ccache) is already shared across
# worktrees, so it would be easy to conclude no configuration is needed. It
# would also be wrong. ccache hashes the compiler command line, and ours is full
# of absolute paths that differ per worktree:
#
#   -I/mnt/d/STS_BG_Mod/_wt/<task>/include
#   -DSTS_GOLDEN_DIR="/mnt/d/STS_BG_Mod/_wt/<task>/tests/golden"
#
# Different worktree => different command line => different hash => a 0% hit
# rate between worktrees, which is exactly the sharing this exists to provide.
# CCACHE_BASEDIR rewrites absolute paths under it to relative ones before
# hashing, which is what makes two worktrees of the same commit hit each other.
# Without it this file would be decoration.
#
# CCACHE_NOHASHDIR does the same job for the working directory that -g bakes
# into debug info. The tradeoff is real and small: a cached object may carry the
# path of whichever worktree first compiled it, so a debugger can need its
# source path remapped. That costs a debugger setting; hashing the directory
# costs the entire cross-worktree hit rate.
#
# WHY debug/asan/release CANNOT COLLIDE HERE
#
# conventions.md §6 records a real incident where a shared FETCHCONTENT_BASE_DIR
# made asan and release emit the SAME object path, so an ASan-instrumented
# libgtest.so landed exactly where the release link line read it. The caution is
# right; the mechanism does not transfer, and it is worth being precise about
# why rather than just asserting it is fine.
#
# That bug was a FILESYSTEM PATH collision: two builds computed the same output
# location. A compiler cache has no shared output location -- ccache writes the
# object exactly where the compiler was told to, inside the invoking build tree.
# The only thing shared is a content-addressed lookup, and the key includes the
# full command line: -fsanitize=address,undefined, -O0 vs -O3, -DNDEBUG and the
# rest all hash differently. An asan object therefore cannot be *found* by a
# release compile; there is no key under which it is reachable.
#
# Sloppiness is left at the default (none). A false cache hit in a bit-exact
# simulator is the worst possible failure -- a silently wrong binary that passes
# review -- so no correctness knob is traded for hit rate here. In particular
# the include_file_mtime/ctime relaxations are NOT set, even though the sources
# live on DrvFs where timestamps are eccentric: ccache falls back to hashing
# file contents, which is slower and correct.

option(STS_COMPILER_CACHE "Use ccache/sccache as the compiler launcher when available" ON)

function(sts_setup_compiler_cache)
    if(NOT STS_COMPILER_CACHE)
        message(STATUS "Compiler cache: disabled (-DSTS_COMPILER_CACHE=OFF)")
        return()
    endif()

    # A caller who has already chosen a launcher wins; never fight an explicit
    # -DCMAKE_CXX_COMPILER_LAUNCHER. The exception is OUR OWN shim, which is
    # present in the cache on every reconfigure of an existing build tree:
    # bailing out there would mean a deleted or stale shim never gets rewritten,
    # leaving a build tree whose compiler launcher points at nothing.
    set(_shim "${CMAKE_BINARY_DIR}/sts-ccache-launcher.sh")
    if(CMAKE_CXX_COMPILER_LAUNCHER AND
       NOT CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "${_shim}")
        message(STATUS "Compiler cache: honouring existing "
                       "CMAKE_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}")
        return()
    endif()

    find_program(STS_CCACHE_PROGRAM NAMES ccache sccache)
    if(NOT STS_CCACHE_PROGRAM)
        message(STATUS
            "Compiler cache: none found. Builds are correct but cold. "
            "Install one to share compilation across worktrees and presets: "
            "`sudo apt-get install ccache` in WSL.")
        return()
    endif()

    # The env vars above have to be in the environment of every compiler
    # invocation, and CMake has no portable way to set env for the build tool.
    # A generated launcher shim is the standard answer: it is regenerated on
    # every configure, lives in the build tree, and keeps the settings visible
    # in one readable file instead of spread across shell profiles that a fresh
    # agent session would not inherit.
    if(NOT UNIX)
        # The shim is a /bin/sh script. On a native Windows toolchain this needs
        # a .cmd equivalent; not written because no Windows build exists yet.
        message(STATUS
            "Compiler cache: found ${STS_CCACHE_PROGRAM} but the launcher shim "
            "is POSIX-only; skipping on this platform.")
        return()
    endif()

    file(WRITE "${_shim}"
"#!/bin/sh
# GENERATED by cmake/StsCompilerCache.cmake -- edits here are overwritten on the
# next configure. See that file for why each variable is set.
#
# CCACHE_BASEDIR makes two git worktrees of the same commit share cache entries
# by rewriting absolute paths under it to relative before hashing.
CCACHE_BASEDIR='${CMAKE_SOURCE_DIR}'
# Do not hash the working directory (which -g bakes into debug info); without
# this the cross-worktree hit rate is zero.
CCACHE_NOHASHDIR=1
export CCACHE_BASEDIR CCACHE_NOHASHDIR
exec '${STS_CCACHE_PROGRAM}' \"$@\"
")
    file(CHMOD "${_shim}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

    # Cache variables, and set before any add_subdirectory: the launcher is read
    # at target-creation time, so setting it here is what makes it reach the
    # FetchContent dependencies too. googletest is rebuilt per build tree per
    # worktree today and is one of the larger wins available.
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_shim}" CACHE STRING "" FORCE)
    set(CMAKE_C_COMPILER_LAUNCHER "${_shim}" CACHE STRING "" FORCE)

    message(STATUS "Compiler cache: ${STS_CCACHE_PROGRAM} "
                   "(shared across worktrees via CCACHE_BASEDIR=${CMAKE_SOURCE_DIR})")
endfunction()
