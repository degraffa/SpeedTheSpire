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
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone

FATAL_PROGRESS_STATUS = "fatal_environment_drift"
EXIT_FATAL_ENVIRONMENT = 3
EXIT_CAMPAIGN_INVALID = 4


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
    parts = [
        args.python.replace("\\", "/"),
        os.path.join(args.driver_dir, "campaign_driver.py").replace("\\", "/"),
        "--data-root", args.data_root.replace("\\", "/"),
        "--campaign-id", args.campaign_id,
        "--seeds", args.seeds_arg,
        "--policy", args.policy,
        "--fork-jar", args.fork_jar.replace("\\", "/"),
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
    return " ".join(parts)


def progress_path(args) -> str:
    return os.path.join(args.data_root, args.campaign_id, "campaign_progress.json")


def heartbeat_path(args) -> str:
    return os.path.join(args.data_root, args.campaign_id, "campaign_heartbeat.json")


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
    if not seeds or any(re.fullmatch(r"[0-9A-Z]+", seed) is None
                        for seed in seeds):
        raise ValueError("seeds must be non-empty base-35-style strings")
    if len(seeds) != len(set(seeds)):
        raise ValueError("seed list contains duplicates")
    return seeds


def progress_identity_error(prog, args):
    if not prog:
        return None
    expected = {
        "campaign_id": args.campaign_id,
        "seed_list": args.seed_list,
        "policy": args.policy,
    }
    mismatches = [
        f"{key}={prog.get(key)!r} (expected {value!r})"
        for key, value in expected.items() if prog.get(key) != value
    ]
    return "; ".join(mismatches) if mismatches else None


def completion_error(prog, expected_seeds):
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


def clear_fresh_campaign_files(campaign_dir: str, seed_list: list) -> list:
    """Remove only artifacts this exact invocation owns.

    Unexpected seed artifacts are deliberately preserved; strict validation
    will report them as stale instead of silently deleting unrelated evidence.
    """
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
        path = os.path.join(campaign_dir, name)
        if os.path.isfile(path):
            os.remove(path)
            removed.append(name)
    return removed


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


def launch_game(args, launch_idx: int) -> subprocess.Popen:
    java = os.path.join(args.game_dir, "jre", "bin", "java.exe")
    cmd = [java, "-jar", args.mts_jar,
           "--skip-launcher", "--mods", "basemod,CommunicationMod-oracle"]
    out = open(os.path.join(args.data_root, args.campaign_id,
                            f"mts_launch{launch_idx}.log"), "w",
               encoding="utf-8", newline="\n")
    log(f"launch #{launch_idx}: {java} -jar {args.mts_jar} --skip-launcher "
        f"--mods basemod,CommunicationMod-oracle  (cwd={args.game_dir})")
    return subprocess.Popen(cmd, cwd=args.game_dir, stdout=out,
                            stderr=subprocess.STDOUT)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Oracle campaign orchestrator (B1.4)")
    ap.add_argument("--data-root", default=r"D:\STS_BG_Mod\_oracle_data\campaigns")
    ap.add_argument("--campaign-id", required=True)
    ap.add_argument("--seeds", required=True,
                    help="comma-separated seeds or a path to a seed-list file")
    ap.add_argument("--policy", choices=["random-legal", "script"],
                    default="random-legal")
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

    args.seeds_arg = args.seeds  # passed through to the driver verbatim
    try:
        args.seed_list = resolve_seeds(args.seeds)
    except ValueError as exc:
        log(f"INVALID CAMPAIGN INPUT -- {exc}")
        return EXIT_CAMPAIGN_INVALID
    camp_dir = os.path.join(args.data_root, args.campaign_id)
    os.makedirs(camp_dir, exist_ok=True)
    if args.fresh:
        removed = clear_fresh_campaign_files(camp_dir, args.seed_list)
        log(f"--fresh: cleared {len(removed)} owned prior file(s) in "
            f"{camp_dir}")

    config_path = os.path.join(
        os.environ["LOCALAPPDATA"], "ModTheSpire", "CommunicationMod",
        "config.properties")
    driver_cmd = build_driver_command(args)
    write_config(config_path, driver_cmd,
                 oracle_block=True,
                 strip_draw=(args.strip_draw == "true"),
                 strip_anim=(args.strip_anim == "true"),
                 strip_cadence=(args.strip_cadence == "true"))
    log(f"wrote {config_path}")
    log(f"strip: draw={args.strip_draw} anim={args.strip_anim} "
        f"cadence={args.strip_cadence}")
    log(f"driver command: {driver_cmd}")

    start = time.time()
    launch_idx = 0
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
        proc = launch_game(args, launch_idx)
        timeline.append({"event": "launch", "idx": launch_idx, "utc": utc()})
        launch_started = time.time()

        # monitor this launch
        while True:
            time.sleep(3.0)
            now = time.time()
            if now - start > args.campaign_timeout:
                log("CAMPAIGN TIMEOUT during launch -- killing game")
                kill_tree(proc)
                return 2

            prog = read_json(progress_path(args))
            done = len(prog["seeds_done"]) if prog else 0
            failed = len(prog["seeds_failed"]) if prog else 0
            identity_error = progress_identity_error(prog, args)
            if identity_error:
                log(f"CAMPAIGN IDENTITY MISMATCH -- {identity_error}")
                kill_tree(proc)
                timeline.append({"event": "campaign_identity_mismatch",
                                 "utc": utc()})
                _summary(args, timeline)
                return EXIT_CAMPAIGN_INVALID

            if fatal_progress(prog):
                log_fatal_progress(prog)
                kill_tree(proc)
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
                    kill_tree(proc)
                    timeline.append({"event": "campaign_failed",
                                     "utc": utc(), "done": done,
                                     "failed": failed})
                    _summary(args, timeline)
                    return EXIT_CAMPAIGN_INVALID
                log(f"campaign COMPLETE ({done} done, {failed} failed) -- "
                    f"stopping game")
                kill_tree(proc)
                timeline.append({"event": "complete", "utc": utc(),
                                 "done": done, "failed": failed})
                _summary(args, timeline)
                return 0

            # induced kill for acceptance (once)
            if (args.kill_after_seeds is not None and not kill_done
                    and done >= args.kill_after_seeds):
                log(f"INDUCED KILL: {done} seeds done >= {args.kill_after_seeds}"
                    f"; killing game to exercise crash-resume")
                kill_tree(proc)
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
                break

            # heartbeat stall while the game is still alive: driver gone
            # (EXIT_NEED_RESTART / EXIT_GAME_GONE) or a true hang -> relaunch.
            # A missing heartbeat file (the driver died at startup before writing
            # one) is treated the same once the launch has had stall_timeout to
            # produce one, so a driver-startup death can't hang the campaign.
            hb = read_json(heartbeat_path(args))
            hb_age = (now - hb.get("t", now)) if hb else (now - launch_started)
            if hb_age > args.stall_timeout \
                    and (now - launch_started) > args.stall_timeout:
                log(f"heartbeat stale/absent {hb_age:.0f}s "
                    f"(> {args.stall_timeout:.0f}); game still up -- "
                    f"killing + relaunching")
                kill_tree(proc)
                timeline.append({"event": "stall_relaunch", "utc": utc(),
                                 "age": round(hb_age, 1), "done": done})
                break

    _summary(args, timeline)
    return 0


def _summary(args, timeline) -> None:
    prog = read_json(progress_path(args)) or {}
    man = read_json(os.path.join(args.data_root, args.campaign_id,
                                 "campaign_manifest.json"))
    with open(os.path.join(args.data_root, args.campaign_id,
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
                ("game_exit", "stall_relaunch", "induced_kill")]
    log(f"induced kills: {len(kills)}  relaunch events: {len(relaunch)}  "
        f"total launches: {len([t for t in timeline if t['event']=='launch'])}")


if __name__ == "__main__":
    sys.exit(main())
