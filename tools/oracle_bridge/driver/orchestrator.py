#!/usr/bin/env python3
"""SpeedTheSpire oracle-campaign orchestrator (Stage B task B1.4).

The campaign driver (campaign_driver.py) is spawned *by the game* (via the
CommunicationMod config.properties `command`), so it cannot own the game's
process lifecycle -- that is this script's job. The orchestrator runs on the
Windows host (started by the operator), and:

  1. writes config.properties so the next game launch spawns the campaign driver
     with the right args (design/README 1.2 launch recipe);
  2. launches ModTheSpire under the game's BUNDLED JRE 8 (never system Java --
     a system-Java upgrade killed a B1.1 launch, ledger B1.1 Log);
  3. watches the campaign progress + heartbeat files the driver maintains;
  4. relaunches the game after a crash, a hang, or a driver-requested restart
     (boss-reward runs end mid-dungeon; the protocol cannot walk back to the
     menu, so the seed after such a run resumes on a fresh launch) -- until the
     driver marks the campaign complete;
  5. optionally induces one deliberate mid-campaign game kill (`--kill-after-
     seeds`) to exercise crash-resume (the B1.4 acceptance bar).

The driver owns per-run protocol + artifacts + the resume file; a crashed game
costs one run, not the campaign (design 7.1(2)). Resume granularity is one seed.

Stdlib-only. Run it from anywhere on the Windows host with Python 3.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import re
import secrets
import subprocess
import sys
import time
from datetime import datetime, timezone

from campaign_paths import (
    ORACLE_LAUNCH_TOKEN_ENV,
    campaign_dir_under_root,
    campaign_file_under_root,
    validate_campaign_id,
    validate_seed_list,
)

FATAL_PROGRESS_STATUS = "fatal_environment_drift"
EXIT_FATAL_ENVIRONMENT = 3
EXIT_CAMPAIGN_INVALID = 4
SCHEMA_VERSION = 1

# g6_campaign_spotdiff.md §9: a single unreadable/missing heartbeat sample
# must never look identical to "stale since launch" -- that confusion killed
# two healthy games mid-combat in the G6 campaign. An unreadable sample only
# counts toward a kill once it has recurred this many consecutive polls (each
# poll is STALL_POLL_INTERVAL_S apart); any readable sample resets the streak
# to 0 immediately. A heartbeat that IS readable but genuinely old is a real
# stall and is judged the instant it is seen, at the same stall_timeout
# threshold as before -- that path is unchanged.
STALL_UNREADABLE_STREAK = 3
STALL_POLL_INTERVAL_S = 3.0
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
_ACTIVE_PROCESSES = []
_TRACK_LAUNCHES = False


class _IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in (
        "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
        "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]


class _BasicLimitInformation(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_longlong),
        ("PerJobUserTimeLimit", ctypes.c_longlong),
        ("LimitFlags", ctypes.c_ulong),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", ctypes.c_ulong),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", ctypes.c_ulong),
        ("SchedulingClass", ctypes.c_ulong),
    ]


class _ExtendedLimitInformation(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", _BasicLimitInformation),
        ("IoInfo", _IoCounters),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


class _WindowsKillJob:
    """Best-effort kill-on-close container for one launched JVM tree."""

    def __init__(self, proc: subprocess.Popen):
        self.handle = None
        if os.name != "nt":
            return
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateJobObjectW.restype = ctypes.c_void_p
        kernel32.SetInformationJobObject.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_ulong]
        kernel32.SetInformationJobObject.restype = ctypes.c_int
        kernel32.AssignProcessToJobObject.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p]
        kernel32.AssignProcessToJobObject.restype = ctypes.c_int
        kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        kernel32.CloseHandle.restype = ctypes.c_int
        handle = kernel32.CreateJobObjectW(None, None)
        if not handle:
            raise OSError(ctypes.get_last_error(), "CreateJobObjectW failed")
        info = _ExtendedLimitInformation()
        info.BasicLimitInformation.LimitFlags = (
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)
        if not kernel32.SetInformationJobObject(
                handle, 9, ctypes.byref(info), ctypes.sizeof(info)):
            error = ctypes.get_last_error()
            if not kernel32.CloseHandle(handle):
                raise OSError(
                    ctypes.get_last_error(),
                    "CloseHandle failed after job configuration error")
            raise OSError(error, "SetInformationJobObject failed")
        if not kernel32.AssignProcessToJobObject(
                handle, ctypes.c_void_p(proc._handle)):
            error = ctypes.get_last_error()
            if not kernel32.CloseHandle(handle):
                raise OSError(
                    ctypes.get_last_error(),
                    "CloseHandle failed after job assignment error")
            raise OSError(error, "AssignProcessToJobObject failed")
        self.handle = handle

    def close(self) -> None:
        if self.handle is not None:
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            kernel32.CloseHandle.restype = ctypes.c_int
            handle = self.handle
            self.handle = None
            if not kernel32.CloseHandle(handle):
                raise OSError(ctypes.get_last_error(), "CloseHandle failed")


def utc() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


def log(msg: str) -> None:
    line = f"[orchestrator {utc()}] {msg}"
    print(line, flush=True)


def esc_prop(value: str) -> str:
    """Escape a java .properties value: forward slashes only (backslash is an
    escape char), colons escaped (mirrors SpireConfig's own output)."""
    return value.replace("\\", "/").replace(":", r"\:")


def write_config(config_path: str, command: str, oracle_block: bool,
                 strip_draw: bool, strip_anim: bool, strip_cadence: bool) -> None:
    os.makedirs(os.path.dirname(config_path), exist_ok=True)
    body = (
        "#SpeedTheSpire campaign orchestrator (B1.3/B1.4) -- generated\n"
        f"#{time.ctime()}\n"
        "verbose=false\n"
        "maxInitializationTimeout=10\n"
        f"oracleBlock={'true' if oracle_block else 'false'}\n"
        # B1.3 rendering-strip family flags (each individually toggleable). With
        # all three false the fork is byte-identical to its pre-B1.3 behaviour --
        # the strip-equivalence baseline.
        f"stripDrawSuppression={'true' if strip_draw else 'false'}\n"
        f"stripAnimationCollapse={'true' if strip_anim else 'false'}\n"
        f"stripFastCadence={'true' if strip_cadence else 'false'}\n"
        "runAtGameStart=true\n"
        f"command={esc_prop(command)}\n"
    )
    with open(config_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(body)


def build_driver_command(args) -> str:
    effective_fork_jar = getattr(args, "effective_fork_jar", args.fork_jar)
    parts = [
        args.python.replace("\\", "/"),
        os.path.join(args.driver_dir, "campaign_driver.py").replace("\\", "/"),
        "--data-root", args.data_root.replace("\\", "/"),
        "--campaign-id", args.campaign_id,
        "--seeds", args.seeds_arg,
        "--policy", args.policy,
        "--fork-jar", effective_fork_jar.replace("\\", "/"),
        "--launch-log", args.launch_log,
        "--launch-token", args.launch_token,
        "--policy-seed", str(args.policy_seed),
        "--timeout", str(args.command_timeout),
        "--max-actions", str(args.max_actions),
        "--run-label",
        f"strip-{args.strip_draw[0]}{args.strip_anim[0]}{args.strip_cadence[0]}",
        "--max-settle", str(args.max_settle),
        "--settle-sleep", str(args.settle_sleep),
    ]
    if args.policy == "script":
        if args.script_dir:
            parts += ["--script-dir", args.script_dir.replace("\\", "/")]
        else:
            parts += ["--script", args.script.replace("\\", "/")]
    if args.policy == "greedy" and args.card_table:
        parts += ["--card-table", args.card_table.replace("\\", "/")]
    return " ".join(parts)


def progress_path(args) -> str:
    return campaign_file_under_root(
        args.data_root, args.campaign_id, "campaign_progress.json")


def heartbeat_path(args) -> str:
    return campaign_file_under_root(
        args.data_root, args.campaign_id, "campaign_heartbeat.json")


def restart_requested_for_launch(prog, launch_token: str) -> bool:
    """Whether `prog` asks us to retire the launch bound to `launch_token`.

    A request from the prior launch can coexist briefly with a newly spawned
    game before its driver resumes and clears the field.  The one-use token
    digest makes that old request harmless instead of creating a relaunch loop.
    """
    request = prog.get("restart_requested") if isinstance(prog, dict) else None
    if not isinstance(request, dict):
        return False
    observed = request.get("launch_token_sha256")
    if not isinstance(observed, str):
        return False
    expected = hashlib.sha256(launch_token.encode("utf-8")).hexdigest()
    return secrets.compare_digest(observed, expected)


def read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, json.JSONDecodeError):
        return None


def fatal_progress(prog) -> bool:
    return bool(prog and prog.get("status") == FATAL_PROGRESS_STATUS)


def log_fatal_progress(prog) -> None:
    fatal = prog.get("fatal") or {}
    detail = fatal.get("message") or "driver reported environment drift"
    log(f"FATAL ENVIRONMENT DRIFT -- {detail}")


def resolve_seeds(spec: str) -> list:
    if os.path.exists(spec):
        with open(spec, "r", encoding="utf-8") as fh:
            seeds = [line.strip().upper() for line in fh
                     if line.strip() and not line.startswith("#")]
    else:
        seeds = [seed.strip().upper() for seed in spec.split(",")
                 if seed.strip()]
    return validate_seed_list(seeds)


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def progress_identity_error(prog, args):
    if not prog:
        return None
    expected = {
        "campaign_id": args.campaign_id,
        "seed_list": args.seed_list,
        "policy": args.policy,
        "fork_jar_sha256": args.fork_hash,
        "schema_version": SCHEMA_VERSION,
    }
    mismatches = [
        f"{key}={prog.get(key)!r} (expected {value!r})"
        for key, value in expected.items()
        if type(prog.get(key)) is not type(value) or prog.get(key) != value
    ]
    return "; ".join(mismatches) if mismatches else None


def completion_error(prog, expected_seeds):
    if prog.get("status") != "complete":
        return (f"campaign status must be 'complete', got "
                f"{prog.get('status')!r}")
    failed = prog.get("seeds_failed")
    done = prog.get("seeds_done")
    if not isinstance(failed, list) or not isinstance(done, list):
        return "completion ledger lacks seeds_done/seeds_failed lists"
    if failed:
        return f"campaign has {len(failed)} failed seed(s)"
    done_seeds = [row.get("seed") for row in done if isinstance(row, dict)]
    if len(done_seeds) != len(done) or done_seeds != expected_seeds:
        return (f"completed seed ledger {done_seeds!r} does not match "
                f"requested seed list {expected_seeds!r}")
    return None


def clear_fresh_campaign_files(data_root: str, campaign_id: str,
                               seed_list: list) -> list:
    """Remove only artifacts this exact invocation owns.

    Unexpected seed artifacts are deliberately preserved; strict validation
    will report them as stale instead of silently deleting unrelated evidence.
    """
    campaign_id = validate_campaign_id(campaign_id)
    seed_list = validate_seed_list(seed_list)
    campaign_dir = campaign_dir_under_root(data_root, campaign_id)
    names = {
        "campaign_progress.json", "campaign_progress.json.tmp",
        "campaign_heartbeat.json", "campaign_manifest.json",
        "orchestrator_timeline.json",
    }
    for seed in seed_list:
        names.add(f"run_{seed}_a20_ironclad.jsonl")
        names.add(f"run_{seed}_a20_ironclad.timing.jsonl")
    try:
        entries = os.listdir(campaign_dir)
    except FileNotFoundError:
        entries = []
    names.update(name for name in entries
                 if re.fullmatch(r"mts_launch[0-9]+\.log", name))
    removed = []
    for name in sorted(names):
        path = campaign_file_under_root(data_root, campaign_id, name)
        if os.path.isfile(path):
            os.remove(path)
            removed.append(name)
    return removed


def highest_launch_index(data_root: str, campaign_id: str) -> int:
    """Highest existing append-only launch-log index, with redirect checks."""
    campaign_dir = campaign_dir_under_root(data_root, campaign_id)
    try:
        entries = os.listdir(campaign_dir)
    except FileNotFoundError:
        return 0
    highest = 0
    for name in entries:
        match = re.fullmatch(r"mts_launch([1-9][0-9]*)\.log", name)
        if match is None:
            continue
        # A redirected owned-looking log must stop resume before either the
        # driver reads it as evidence or a later launch allocates around it.
        campaign_file_under_root(data_root, campaign_id, name)
        highest = max(highest, int(match.group(1)))
    return highest


def kill_tree(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    try:
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)],
                       capture_output=True)
    except OSError:
        proc.kill()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()


def cleanup_process(proc: subprocess.Popen) -> None:
    """Retire the JVM tree and close its kill-on-close job handle."""
    try:
        kill_tree(proc)
    finally:
        job = getattr(proc, "_oracle_kill_job", None)
        if job is not None:
            job.close()
            proc._oracle_kill_job = None
        try:
            _ACTIVE_PROCESSES.remove(proc)
        except ValueError:
            pass


def launch_game(args, launch_idx: int) -> subprocess.Popen:
    expected_log = f"mts_launch{launch_idx}.log"
    if args.launch_log != expected_log:
        raise ValueError(
            f"launch #{launch_idx} must bind {expected_log!r}, got "
            f"{args.launch_log!r}")
    java = os.path.join(args.game_dir, "jre", "bin", "java.exe")
    private_runtime = getattr(args, "private_runtime", False)
    effective_workdir = getattr(args, "effective_workdir", args.game_dir)
    cmd = [java]
    if private_runtime:
        cmd.extend([
            f"-Xms{args.java_xms_mib}m",
            f"-Xmx{args.java_xmx_mib}m",
            f"-Djava.io.tmpdir={args.runtime_temp_dir}",
        ])
    cmd.extend(["-jar", args.mts_jar,
                "--skip-launcher", "--mods",
                "basemod,CommunicationMod-oracle"])
    out = open(campaign_file_under_root(
        args.data_root, args.campaign_id, args.launch_log), "x",
               encoding="utf-8", newline="\n")
    env = os.environ.copy()
    env[ORACLE_LAUNCH_TOKEN_ENV] = args.launch_token
    if private_runtime:
        env.update({
            "LOCALAPPDATA": args.runtime_local_app_data,
            "APPDATA": args.runtime_app_data,
            "TEMP": args.runtime_temp_dir,
            "TMP": args.runtime_temp_dir,
        })
    log(f"launch #{launch_idx}: {java} -jar {args.mts_jar} --skip-launcher "
        "--mods basemod,CommunicationMod-oracle  "
        f"(cwd={effective_workdir})")
    try:
        proc = subprocess.Popen(cmd, cwd=effective_workdir, stdout=out,
                                stderr=subprocess.STDOUT, env=env)
        try:
            kill_job = _WindowsKillJob(proc)
            setattr(proc, "_oracle_kill_job", kill_job)
        except (OSError, AttributeError) as exc:
            if private_runtime:
                kill_tree(proc)
                raise RuntimeError(
                    "private-runtime JVM could not be placed in its "
                    f"kill-on-close job: {exc}") from exc
            log(f"warning: JVM job-object containment unavailable: {exc}")
        if _TRACK_LAUNCHES:
            _ACTIVE_PROCESSES.append(proc)
        return proc
    finally:
        out.close()


def _main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Oracle campaign orchestrator (B1.4)")
    ap.add_argument("--data-root", default=r"D:\STS_BG_Mod\_oracle_data\campaigns")
    ap.add_argument("--campaign-id", required=True)
    ap.add_argument("--seeds", required=True,
                    help="comma-separated seeds or a path to a seed-list file")
    ap.add_argument("--policy", choices=["random-legal", "greedy", "script"],
                    default="random-legal",
                    help="passed straight through to campaign_driver.py; "
                         "`greedy` is the depth-seeking live policy "
                         "(greedy_policy.py)")
    ap.add_argument("--card-table", default=None,
                    help="override the greedy policy's card side table "
                         "(default: cards_sidetable.json beside the driver)")
    ap.add_argument("--script", help="single command script for --policy script "
                    "(applied to every seed)")
    ap.add_argument("--script-dir", help="directory of per-seed scripts "
                    "script_<SEED>.txt for --policy script (B1.3 A/B replay)")
    ap.add_argument("--policy-seed", type=int, default=1234)
    # B1.3 rendering-strip toggles. Default matches the fork default (on). For an
    # A/B equivalence pass: one campaign with all three true (strip on), one with
    # all three false (strip off); byte-compare the normalized dumps.
    ap.add_argument("--strip-draw", choices=["true", "false"], default="true")
    ap.add_argument("--strip-anim", choices=["true", "false"], default="true")
    ap.add_argument("--strip-cadence", choices=["true", "false"], default="true")
    ap.add_argument("--max-settle", type=int, default=60)
    ap.add_argument("--settle-sleep", type=float, default=0.0)
    ap.add_argument("--game-dir",
                    default=r"D:\SteamLibrary\steamapps\common\SlayTheSpire")
    ap.add_argument("--mts-jar",
                    default=r"D:\SteamLibrary\steamapps\workshop\content\646570\1605060445\ModTheSpire.jar")
    ap.add_argument("--python", default=r"C:\Python39\python.exe")
    ap.add_argument("--driver-dir",
                    default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--fork-jar",
                    default=r"D:\SteamLibrary\steamapps\common\SlayTheSpire\mods\CommunicationMod-oracle.jar")
    ap.add_argument("--runtime-workdir",
                    help="isolated game working directory for this worker")
    ap.add_argument("--runtime-local-app-data",
                    help="isolated LOCALAPPDATA root for this worker")
    ap.add_argument("--runtime-app-data",
                    help="isolated APPDATA root for this worker")
    ap.add_argument("--runtime-temp-dir",
                    help="isolated TEMP/TMP root for this worker")
    ap.add_argument("--runtime-fork-jar",
                    help="private runtime copy of CommunicationMod fork")
    ap.add_argument("--java-xms-mib", type=int, default=256)
    ap.add_argument("--java-xmx-mib", type=int, default=1024)
    ap.add_argument("--seeds-per-launch", type=int, default=50,
                    help="recycle a JVM after this many newly completed seeds")
    ap.add_argument("--command-timeout", type=float, default=90.0,
                    help="driver per-command watchdog (s)")
    ap.add_argument("--stall-timeout", type=float, default=240.0,
                    help="relaunch if the heartbeat goes stale this long while "
                         "the game is still alive (> command-timeout)")
    ap.add_argument("--max-actions", type=int, default=3000)
    ap.add_argument("--campaign-timeout", type=float, default=7200.0,
                    help="overall wall-clock guard (s)")
    ap.add_argument("--kill-after-seeds", type=int, default=None,
                    help="induce ONE deliberate game kill once this many seeds "
                         "are done (acceptance: crash-resume)")
    ap.add_argument("--fresh", action="store_true",
                    help="wipe any existing progress for this campaign first")
    args = ap.parse_args(argv)

    runtime_values = (
        args.runtime_workdir, args.runtime_local_app_data,
        args.runtime_app_data, args.runtime_temp_dir, args.runtime_fork_jar)
    if any(runtime_values) and not all(runtime_values):
        ap.error("the five --runtime-* arguments must be supplied together")
    if (args.java_xms_mib < 1 or args.java_xmx_mib < 1
            or args.java_xms_mib > args.java_xmx_mib):
        ap.error("Java heap sizes must be positive and Xms must not exceed Xmx")
    if args.seeds_per_launch < 1:
        ap.error("--seeds-per-launch must be positive")
    args.private_runtime = all(runtime_values)
    args.effective_workdir = (
        args.runtime_workdir if args.private_runtime else args.game_dir)
    args.effective_fork_jar = (
        args.runtime_fork_jar if args.private_runtime else args.fork_jar)
    config_local_app_data = (
        args.runtime_local_app_data if args.private_runtime
        else os.environ.get("LOCALAPPDATA"))

    args.seeds_arg = args.seeds  # passed through to the driver verbatim
    try:
        validate_campaign_id(args.campaign_id)
        args.seed_list = resolve_seeds(args.seeds)
        args.fork_hash = sha256_file(args.effective_fork_jar)
        camp_dir = campaign_dir_under_root(args.data_root, args.campaign_id)
    except (OSError, ValueError) as exc:
        log(f"INVALID CAMPAIGN INPUT -- {exc}")
        return EXIT_CAMPAIGN_INVALID
    os.makedirs(camp_dir, exist_ok=True)
    try:
        # Re-resolve after mkdir so an existing/redirection race fails closed.
        camp_dir = campaign_dir_under_root(args.data_root, args.campaign_id)
    except ValueError as exc:
        log(f"INVALID CAMPAIGN PATH -- {exc}")
        return EXIT_CAMPAIGN_INVALID
    if args.fresh:
        try:
            removed = clear_fresh_campaign_files(
                args.data_root, args.campaign_id, args.seed_list)
        except ValueError as exc:
            log(f"REFUSING --fresh CLEANUP -- {exc}")
            return EXIT_CAMPAIGN_INVALID
        log(f"--fresh: cleared {len(removed)} owned prior file(s) in "
            f"{camp_dir}")

    start = time.time()
    try:
        launch_idx = highest_launch_index(args.data_root, args.campaign_id)
    except ValueError as exc:
        log(f"INVALID EXISTING LAUNCH LOG -- {exc}")
        return EXIT_CAMPAIGN_INVALID
    if not config_local_app_data:
        ap.error("LOCALAPPDATA is unavailable")
    config_path = os.path.join(
        config_local_app_data, "ModTheSpire", "CommunicationMod",
        "config.properties")
    kill_done = False
    timeline = []

    while True:
        if time.time() - start > args.campaign_timeout:
            log("CAMPAIGN TIMEOUT -- giving up")
            return 2
        prog = read_json(progress_path(args))
        identity_error = progress_identity_error(prog, args)
        if identity_error:
            log(f"CAMPAIGN IDENTITY MISMATCH -- {identity_error}; use a new "
                "campaign id or explicitly restart with --fresh")
            return EXIT_CAMPAIGN_INVALID
        if fatal_progress(prog):
            log_fatal_progress(prog)
            timeline.append({"event": "fatal_environment_drift", "utc": utc()})
            _summary(args, timeline)
            return EXIT_FATAL_ENVIRONMENT
        if prog and prog.get("status") in ("complete",
                                          "complete_with_failures"):
            error = completion_error(prog, args.seed_list)
            if error:
                log(f"CAMPAIGN NOT ACCEPTABLE -- {error}")
                _summary(args, timeline)
                return EXIT_CAMPAIGN_INVALID
            log("campaign already complete")
            break

        launch_idx += 1
        args.launch_log = f"mts_launch{launch_idx}.log"
        # Binding nonce, not a credential: it only proves the driver inherited
        # this exact game process. Keep it out of diagnostics anyway.
        args.launch_token = secrets.token_hex(32)
        driver_cmd = build_driver_command(args)
        write_config(config_path, driver_cmd,
                     oracle_block=True,
                     strip_draw=(args.strip_draw == "true"),
                     strip_anim=(args.strip_anim == "true"),
                     strip_cadence=(args.strip_cadence == "true"))
        log(f"wrote {config_path} for append-only {args.launch_log}")
        log(f"strip: draw={args.strip_draw} anim={args.strip_anim} "
            f"cadence={args.strip_cadence}")
        log("driver command prepared with one-use launch binding "
            "(token omitted)")
        done_rows = prog.get("seeds_done", []) if isinstance(prog, dict) else []
        launch_done_start = len(done_rows) if isinstance(done_rows, list) else 0
        proc = launch_game(args, launch_idx)
        timeline.append({"event": "launch", "idx": launch_idx, "utc": utc(),
                         "done_at_launch": launch_done_start})
        launch_started = time.time()
        hb_unreadable_streak = 0

        # monitor this launch
        while True:
            time.sleep(STALL_POLL_INTERVAL_S)
            now = time.time()
            if now - start > args.campaign_timeout:
                log("CAMPAIGN TIMEOUT during launch -- killing game")
                cleanup_process(proc)
                return 2

            prog = read_json(progress_path(args))
            done = len(prog["seeds_done"]) if prog else 0
            failed = len(prog["seeds_failed"]) if prog else 0
            identity_error = progress_identity_error(prog, args)
            if identity_error:
                log(f"CAMPAIGN IDENTITY MISMATCH -- {identity_error}")
                cleanup_process(proc)
                timeline.append({"event": "campaign_identity_mismatch",
                                 "utc": utc()})
                _summary(args, timeline)
                return EXIT_CAMPAIGN_INVALID

            if fatal_progress(prog):
                log_fatal_progress(prog)
                cleanup_process(proc)
                timeline.append({
                    "event": "fatal_environment_drift",
                    "utc": utc(),
                    "done": done,
                    "failed": failed,
                })
                _summary(args, timeline)
                return EXIT_FATAL_ENVIRONMENT

            if prog and prog.get("status") in ("complete",
                                              "complete_with_failures"):
                error = completion_error(prog, args.seed_list)
                if error:
                    log(f"CAMPAIGN NOT ACCEPTABLE -- {error}")
                    cleanup_process(proc)
                    timeline.append({"event": "campaign_failed",
                                     "utc": utc(), "done": done,
                                     "failed": failed})
                    _summary(args, timeline)
                    return EXIT_CAMPAIGN_INVALID
                log(f"campaign COMPLETE ({done} done, {failed} failed) -- "
                    f"stopping game")
                cleanup_process(proc)
                timeline.append({"event": "complete", "utc": utc(),
                                 "done": done, "failed": failed})
                _summary(args, timeline)
                return 0

            if restart_requested_for_launch(prog, args.launch_token):
                request = prog.get("restart_requested") or {}
                reason = request.get("reason") or "driver request"
                log(f"driver requested restart ({reason}); {done} done -- "
                    "killing + relaunching")
                cleanup_process(proc)
                timeline.append({"event": "driver_restart", "utc": utc(),
                                 "reason": reason, "done": done})
                break

            newly_completed = done - launch_done_start
            if newly_completed >= args.seeds_per_launch:
                log(f"capacity recycle: {newly_completed} seed(s) completed "
                    f"on launch #{launch_idx}; killing + relaunching")
                cleanup_process(proc)
                timeline.append({
                    "event": "capacity_recycle", "utc": utc(),
                    "done": done, "done_at_launch": launch_done_start,
                    "completed_this_launch": newly_completed,
                    "limit": args.seeds_per_launch,
                })
                break

            # induced kill for acceptance (once)
            if (args.kill_after_seeds is not None and not kill_done
                    and done >= args.kill_after_seeds):
                log(f"INDUCED KILL: {done} seeds done >= {args.kill_after_seeds}"
                    f"; killing game to exercise crash-resume")
                cleanup_process(proc)
                kill_done = True
                timeline.append({"event": "induced_kill", "utc": utc(),
                                 "after_seeds": done})
                break  # relaunch

            # game process died on its own (crash / driver-triggered game exit)
            if proc.poll() is not None:
                log(f"game process exited (code {proc.returncode}); "
                    f"{done} done, {failed} failed -- relaunching if incomplete")
                timeline.append({"event": "game_exit", "utc": utc(),
                                 "code": proc.returncode, "done": done})
                cleanup_process(proc)
                break

            # heartbeat stall while the game is still alive: driver gone
            # (EXIT_NEED_RESTART / EXIT_GAME_GONE) or a true hang -> relaunch.
            # A missing heartbeat file (the driver died at startup before writing
            # one) is treated the same once the launch has had stall_timeout to
            # produce one, so a driver-startup death can't hang the campaign.
            #
            # An unreadable/missing SAMPLE is not proof of that: read_json
            # (below) swallows OSError/JSONDecodeError and returns None for
            # any transient hiccup (e.g. a read racing the heartbeat's own
            # write), and treating one such sample as "stale since launch"
            # is exactly what g6_campaign_spotdiff.md §9 found killing a
            # healthy mid-combat game -- `hb_age` fell back to
            # `now - launch_started`, which trivially exceeds stall_timeout
            # on every poll after the first, so ONE bad read was sufficient.
            # A readable sample immediately resets the streak; only
            # STALL_UNREADABLE_STREAK consecutive unreadable samples count.
            # A heartbeat that reads back fine but carries a genuinely old
            # `t` is a real stall and still fires on the spot, same threshold
            # as before -- that path has no streak requirement.
            hb = read_json(heartbeat_path(args))
            if isinstance(hb, dict) and "t" in hb:
                hb_unreadable_streak = 0
                hb_age = now - hb["t"]
                stalled = (hb_age > args.stall_timeout
                           and (now - launch_started) > args.stall_timeout)
                reason = f"heartbeat stale {hb_age:.0f}s"
            else:
                hb_unreadable_streak += 1
                hb_age = now - launch_started
                stalled = (
                    hb_unreadable_streak >= STALL_UNREADABLE_STREAK
                    and hb_age > args.stall_timeout
                    and (now - launch_started) > args.stall_timeout)
                reason = (f"heartbeat unreadable/absent for "
                          f"{hb_unreadable_streak} consecutive sample(s), "
                          f"{hb_age:.0f}s since launch")
            if stalled:
                log(f"{reason} (> {args.stall_timeout:.0f}); game still up -- "
                    f"killing + relaunching")
                cleanup_process(proc)
                timeline.append({"event": "stall_relaunch", "utc": utc(),
                                 "age": round(hb_age, 1), "done": done})
                break

    _summary(args, timeline)
    return 0


def _summary(args, timeline) -> None:
    prog = read_json(progress_path(args)) or {}
    with open(campaign_file_under_root(
            args.data_root, args.campaign_id,
            "orchestrator_timeline.json"), "w",
              encoding="utf-8", newline="\n") as fh:
        json.dump({"timeline": timeline, "final_status": prog.get("status")},
                  fh, indent=2)
    log("=== campaign summary ===")
    log(f"status: {prog.get('status')}  launches: {prog.get('launches')}")
    for s in prog.get("seeds_done", []):
        log(f"  DONE  {s['seed']}  outcome={s['outcome']}  "
            f"floor={s['floor']}  actions={s['actions']}  attempts={s['attempts']}")
    for s in prog.get("seeds_failed", []):
        log(f"  FAIL  {s['seed']}  reason={s['reason']}  attempts={s['attempts']}")
    kills = [t for t in timeline if t["event"] == "induced_kill"]
    relaunch = [t for t in timeline if t["event"] in
                ("game_exit", "stall_relaunch", "induced_kill",
                 "driver_restart", "capacity_recycle")]
    log(f"induced kills: {len(kills)}  relaunch events: {len(relaunch)}  "
        f"total launches: {len([t for t in timeline if t['event']=='launch'])}")


def main(argv=None) -> int:
    """Run one campaign and never leave a successfully launched JVM behind."""
    global _TRACK_LAUNCHES
    _TRACK_LAUNCHES = True
    try:
        return _main(argv)
    finally:
        for proc in list(_ACTIVE_PROCESSES):
            try:
                cleanup_process(proc)
            except Exception as exc:  # noqa: BLE001 - final safety boundary
                log(f"warning: final JVM cleanup failed: {exc}")
        _TRACK_LAUNCHES = False


if __name__ == "__main__":
    sys.exit(main())
