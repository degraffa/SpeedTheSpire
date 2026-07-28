#!/usr/bin/env bash
# run_readout.sh -- run the built `replay_run_diff` inside WSL from anywhere.
#
# WHY IT EXISTS. `replay_run_diff` is an ELF binary built by the WSL toolchain,
# so a Windows-host caller cannot exec it: Git-Bash answers
# `cannot execute binary file: Exec format error`, and the sanctioned way across
# the boundary is `tools/wsl_run.sh --script <path> [args...]` (conventions §6),
# which needs a script FILE to hand its arguments to. This is that file and
# nothing more -- it changes to the repo root and execs the debug binary with
# whatever it was given.
#
#   tools/wsl_run.sh --script tools/run_readout.sh --treasure /mnt/d/.../run_*.jsonl
#   tools/wsl_run.sh --script tools/run_readout.sh --event    /mnt/d/.../run_*.jsonl
#
# Inside WSL, call the binary directly instead; this adds nothing there.
#
# Artifact paths must be `/mnt/<drive>/...`: the argument list is forwarded
# untouched, and `wsl_run.sh` sets MSYS_NO_PATHCONV itself so such an argument
# survives the boundary intact.
set -u
cd "$(dirname -- "$0")/.."
bin=build/debug/tools/oracle_bridge/replay/replay_run_diff
if [ ! -f "$bin" ]; then
    echo "run_readout.sh: $bin is not built -- run tools/wsl_run.sh debug first" >&2
    exit 2
fi
exec "$bin" "$@"
