#!/usr/bin/env bash
# Prove the engine is consumable with add_subdirectory() from a DIFFERENT
# top-level project.
#
# WHY THIS EXISTS (training-tasks.md deferred obligations): every repo-owned
# path in this build used to be spelled ${CMAKE_SOURCE_DIR}, which is the
# TOP-LEVEL source dir -- the consumer's root once the engine is embedded, not
# the engine's. The dangerous one was
#
#   target_include_directories(sts_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)
#
# a PUBLIC (exported) include dir: embedded, it handed every consumer its own
# <consumer>/include. That is a silently wrong include path, not a build error,
# so nothing in the engine's own six presets can catch it -- at the top level
# CMAKE_SOURCE_DIR and PROJECT_SOURCE_DIR are the same directory. Only a build
# driven from another top level tells the two apart, which is this script.
#
# The consumer it generates plants a DECOY at <consumer>/include/sts/engine/
# version.hpp whose body is `#error`. If the exported include dir ever regresses
# to the consumer's root, that header wins the lookup and the build fails loudly
# instead of quietly compiling against the wrong tree.
#
# Usage:
#   tools/check_embed_consumer.sh [--jobs N] [--keep]
#
#   STS_EMBED_WORKDIR=<dir>   reuse (and keep) a work dir instead of mktemp -d,
#                             so a long ninja build resumes across invocations.
#   STS_EMBED_CMAKE_ARGS=...  extra space-separated cache args for the configure.
#
# Exit: 0 pass, 1 a check failed, 2 usage/environment error.
#
# Runs from Git Bash on the Windows host (clang-cl, matching the win-* presets)
# and from inside WSL (the host default compiler, matching debug/asan/release);
# either host is authoritative. Not a ctest entry: it configures and builds a
# whole second tree, which is a minutes-scale campaign, not a unit test.
set -euo pipefail

readonly MIN_TESTS=100

die() { printf 'check_embed_consumer: %s\n' "$*" >&2; exit "${2:-1}"; }

jobs=""
keep=0
while [ $# -gt 0 ]; do
    case "$1" in
        --jobs) jobs="${2:-}"; shift 2 ;;
        --jobs=*) jobs="${1#--jobs=}"; shift ;;
        --keep) keep=1; shift ;;
        -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
        *) die "unrecognised argument: $1" 2 ;;
    esac
done

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
engine_root=$(cd -- "${script_dir}/.." && pwd)
[ -f "${engine_root}/CMakeLists.txt" ] || die "no CMakeLists.txt at ${engine_root}" 2
[ -f "${engine_root}/include/sts/engine/version.hpp" ] || \
    die "no include/sts/engine/version.hpp at ${engine_root}" 2

command -v cmake >/dev/null 2>&1 || die "cmake not on PATH" 2
command -v ctest >/dev/null 2>&1 || die "ctest not on PATH" 2

# Git Bash runs a WINDOWS cmake, which cannot resolve /tmp/... or /d/... paths.
# cygpath -m yields D:/foo (forward slashes), which is accepted by both cmake and
# the shell. Under WSL there is no cygpath and POSIX paths are already correct.
if command -v cygpath >/dev/null 2>&1; then
    to_native() { cygpath -m -- "$1"; }
    host="windows"
else
    to_native() { printf '%s' "$1"; }
    host="posix"
fi

if [ -z "${jobs}" ]; then
    jobs=$(nproc 2>/dev/null || echo 4)
fi

work="${STS_EMBED_WORKDIR:-}"
if [ -n "${work}" ]; then
    mkdir -p "${work}"
    work=$(cd -- "${work}" && pwd)
    keep=1
else
    work=$(mktemp -d) || die "mktemp -d failed" 2
fi
cleanup() { [ "${keep}" -eq 1 ] || rm -rf "${work}"; }
trap cleanup EXIT

src="${work}/consumer"
bld="${work}/build"
mkdir -p "${src}/include/sts/engine"

# The engine's own generated headers live under the ENGINE's binary dir, so a
# stale consumer tree is safe to reuse; only the sources below are rewritten.
cat > "${src}/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.21)
project(EmbedSmoke CXX)

# ctest reads CTestTestfile.cmake from the TOP-LEVEL build dir, and that file is
# only generated for a directory where testing is enabled. A consumer that wants
# the engine's suites as per-test ctest entries (the training repo does) must
# therefore call this itself, before add_subdirectory.
enable_testing()

add_subdirectory("$(to_native "${engine_root}")" engine)

add_executable(embed_smoke main.cpp)
target_link_libraries(embed_smoke PRIVATE sts_engine)
EOF

cat > "${src}/main.cpp" <<'EOF'
// Consumes the engine exactly as an embedding project would: public headers by
// their <sts/engine/...> spelling, resolved only through sts_engine's exported
// include directories, and one real symbol from the library so the link is real.
#include <cstdio>
#include <string_view>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/version.hpp"

int main() {
    const std::string_view v = sts::engine::VersionString();
    std::printf("embed_smoke: sts engine %d.%d.%d (%.*s), sizeof(RunController)=%zu\n",
                sts::engine::kVersion.major, sts::engine::kVersion.minor,
                sts::engine::kVersion.patch, static_cast<int>(v.size()), v.data(),
                sizeof(sts::engine::RunController));
    return 0;
}
EOF

# The decoy described in the header comment. Never included by a correct build.
cat > "${src}/include/sts/engine/version.hpp" <<'EOF'
#error "sts_engine exported the CONSUMER's include directory -- \
${CMAKE_SOURCE_DIR} regressed into a PUBLIC target_include_directories. \
See src/engine/CMakeLists.txt and tools/check_embed_consumer.sh."
EOF

configure_args=(-S "$(to_native "${src}")" -B "$(to_native "${bld}")"
                -G Ninja -DCMAKE_BUILD_TYPE=Debug)
if [ "${host}" = "windows" ]; then
    # Same toolchain the win-* presets pin (CMakePresets.json, "win-base").
    configure_args+=(-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl)
fi
if [ -n "${STS_EMBED_CMAKE_ARGS:-}" ]; then
    # shellcheck disable=SC2206
    configure_args+=(${STS_EMBED_CMAKE_ARGS})
fi

printf '==> consumer project: %s\n==> engine root:      %s\n==> host:             %s (-j %s)\n' \
    "${src}" "${engine_root}" "${host}" "${jobs}"

cmake "${configure_args[@]}" || die "configure of the embedding consumer failed"

# Everything, not just embed_smoke: gtest_discover_tests runs in PRE_TEST mode
# (cmake/StsAddTest.cmake), so `ctest -N` enumerates by executing each test
# binary -- they have to exist before the count below means anything.
cmake --build "$(to_native "${bld}")" -j "${jobs}" || die "build of the embedding consumer failed"

smoke=""
for cand in "${bld}/embed_smoke" "${bld}/embed_smoke.exe" \
            "${bld}/bin/embed_smoke" "${bld}/bin/embed_smoke.exe"; do
    [ -x "${cand}" ] && { smoke="${cand}"; break; }
done
[ -n "${smoke}" ] || die "embed_smoke was not produced under ${bld}"
"${smoke}" || die "embed_smoke exited non-zero"

listing="${work}/ctest-N.txt"
( cd "${bld}" && ctest -N ) > "${listing}" 2>&1 || {
    tail -20 "${listing}" >&2
    die "ctest -N failed in the consumer build tree"
}
total=$(tr -d '\r' < "${listing}" | sed -n 's/^Total Tests: \([0-9]\+\)$/\1/p' | tail -1)
[ -n "${total}" ] || { tail -20 "${listing}" >&2; die "no 'Total Tests:' line from ctest -N"; }

if [ "${total}" -le "${MIN_TESTS}" ]; then
    die "ctest -N in the consumer build listed ${total} tests, expected > ${MIN_TESTS}: \
the engine's suites are not reaching the consumer as per-test entries"
fi

printf 'check_embed_consumer: PASS -- embedded build clean, ctest -N sees %s tests (> %s)\n' \
    "${total}" "${MIN_TESTS}"
