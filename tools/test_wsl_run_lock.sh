#!/usr/bin/env bash
# Regression for wsl_run.sh's per-worktree build lock.
#
# Run from Windows without starting a build:
#   tools\wsl_run.cmd --script tools/test_wsl_run_lock.sh
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(cd -- "$script_dir/.." && pwd)
lock=$root/build/.wsl_run.lock
mkdir -p "$root/build"

# A persistent lock file is harmless: kernel ownership, not file existence,
# gates the build. Prove an exited owner releases the lock.
(
    exec {released_fd}>"$lock"
    flock -n "$released_fd"
)
exec {owner_fd}>"$lock"
if ! flock -n "$owner_fd"; then
    echo "test_wsl_run_lock: FAIL: an exited owner leaked the lock" >&2
    exit 1
fi

# While this process owns the lock, a nested invocation must refuse before
# configure/build/test. The status and diagnostic are both part of the
# fail-loud contract.
set +e
output=$(bash "$script_dir/wsl_run.sh" debug 2>&1)
status=$?
set -e
if [ "$status" -ne 2 ]; then
    echo "test_wsl_run_lock: FAIL: nested wsl_run exited $status, expected 2" >&2
    printf '%s\n' "$output" >&2
    exit 1
fi
case $output in
    *"refusing concurrent use of this build tree"*)
        ;;
    *)
        echo "test_wsl_run_lock: FAIL: refusal diagnostic missing" >&2
        printf '%s\n' "$output" >&2
        exit 1
        ;;
esac

echo "test_wsl_run_lock: PASS"
