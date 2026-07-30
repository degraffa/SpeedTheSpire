#!/usr/bin/env python3
"""B5.2 one-command oracle campaign pipeline.

Windows owns the game process. Post-processing crosses into WSL only through
tools/wsl_run.cmd and produces translated traces, replay diffs, a raw
encounter-list oracle result, a triage queue, and generated JSON/Markdown
reports under the same immutable campaign directory.
"""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from typing import Optional

from campaign_paths import (
    campaign_dir_under_root,
    campaign_file_under_root,
    validate_campaign_id,
    validate_seed_list,
)

PIPELINE_VERSION = "b5.2.0"
CAMPAIGN_ROOT = r"D:\STS_BG_Mod\_oracle_data\campaigns"
GAME_LOCK_PATH = r"D:\STS_BG_Mod\_oracle_data\oracle_game.lock"
SCHEDULE_ROOT = r"D:\STS_BG_Mod\_oracle_data\schedules"
EXIT_DIVERGENCES = 10
EXIT_GAME_BUSY = 11
_RUN_RE = re.compile(r"run_([0-9A-Z]+)_a20_ironclad\.jsonl")
_FIRST_DIFF_RE = re.compile(
    r"first divergence: seq=(\d+) floor=(\d+) screen=([^ ]+) "
    r"\((\d+) field")
_CAPTURE_RACE_RE = re.compile(
    r"\b(\d+)\s+([a-z][a-z0-9-]*-race)\b")


class GameResourceBusy(RuntimeError):
    """The single CommunicationMod game/config resource is already owned."""


class GameResourceLock:
    """Cross-process nonblocking lock released by the OS after a crash."""

    def __init__(self, campaign_id: str, path: str = GAME_LOCK_PATH):
        self.campaign_id = campaign_id
        self.path = path
        self._file = None

    def _owner(self) -> str:
        try:
            with open(self.path, "rb") as fh:
                fh.seek(1)
                owner = json.loads(fh.read().decode("utf-8"))
            campaign = owner.get("campaign_id", "unknown")
            pid = owner.get("pid", "unknown")
            return f"campaign={campaign} pid={pid}"
        except (OSError, UnicodeDecodeError, json.JSONDecodeError,
                AttributeError):
            return "campaign=unknown pid=unknown"

    def acquire(self) -> None:
        os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
        lock_file = open(self.path, "a+b")
        lock_file.seek(0, os.SEEK_END)
        if lock_file.tell() == 0:
            lock_file.write(b"\0")
            lock_file.flush()
        lock_file.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(
                    lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except (OSError, BlockingIOError):
            lock_file.close()
            raise GameResourceBusy(
                "oracle game/config resource is busy; "
                f"owner {self._owner()}")
        self._file = lock_file
        metadata = {
            "campaign_id": self.campaign_id,
            "pid": os.getpid(),
            "acquired_utc": datetime.now(timezone.utc).isoformat(),
        }
        lock_file.seek(1)
        lock_file.truncate()
        lock_file.write(json.dumps(metadata, sort_keys=True).encode("utf-8"))
        lock_file.flush()
        os.fsync(lock_file.fileno())

    def release(self) -> None:
        if self._file is None:
            return
        lock_file = self._file
        self._file = None
        try:
            if os.name == "nt":
                import msvcrt
                lock_file.seek(0)
                msvcrt.locking(lock_file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
        finally:
            lock_file.close()

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, _exc_type, _exc, _traceback):
        self.release()


def _driver_dir() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def _repo_root() -> str:
    return os.path.abspath(os.path.join(_driver_dir(), "..", "..", ".."))


def _read_json(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        value = json.load(fh)
    if not isinstance(value, dict):
        raise ValueError(f"{path} is not a JSON object")
    return value


def _write_json(path: str, value: object) -> None:
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(value, fh, indent=2, sort_keys=True)
        fh.write("\n")
    os.replace(tmp, path)


def _read_seeds(spec: str) -> list:
    if os.path.exists(spec):
        with open(spec, "r", encoding="utf-8") as fh:
            seeds = [
                line.strip().upper()
                for line in fh
                if line.strip() and not line.lstrip().startswith("#")
            ]
    else:
        seeds = [item.strip().upper() for item in spec.split(",")
                 if item.strip()]
    return validate_seed_list(seeds)


def shard_seeds(seeds: list, shard_count: int, shard_index: int) -> list:
    if shard_count < 1:
        raise ValueError("shard-count must be positive")
    if shard_index < 0 or shard_index >= shard_count:
        raise ValueError("shard-index must be in [0, shard-count)")
    selected = seeds[shard_index::shard_count]
    if not selected:
        raise ValueError("selected shard is empty")
    return selected


def shard_campaign_id(base: str, count: int, index: int) -> str:
    validate_campaign_id(base)
    if count == 1:
        return base
    return validate_campaign_id(
        f"{base}.shard-{index + 1:03d}-of-{count:03d}")


def script_identity(args) -> Optional[dict]:
    if args.policy != "script":
        return None
    if not args.script:
        raise ValueError("--policy script requires --script")
    path = os.path.abspath(args.script)
    with open(path, "rb") as fh:
        digest = hashlib.sha256(fh.read()).hexdigest()
    return {"path": path.replace("\\", "/"), "sha256": digest}


def _campaign_paths(campaign_id: str) -> tuple:
    campaign_dir = campaign_dir_under_root(CAMPAIGN_ROOT, campaign_id)
    return campaign_dir, {
        "config": campaign_file_under_root(
            CAMPAIGN_ROOT, campaign_id, "pipeline_config.json"),
        "seeds": campaign_file_under_root(
            CAMPAIGN_ROOT, campaign_id, "pipeline_seeds.txt"),
        "report_json": campaign_file_under_root(
            CAMPAIGN_ROOT, campaign_id, "report.json"),
        "report_md": campaign_file_under_root(
            CAMPAIGN_ROOT, campaign_id, "report.md"),
    }


def prepare_campaign(base_id: str, seeds_spec: str, shard_count: int,
                     shard_index: int, policy: str,
                     policy_seed: int,
                     script: Optional[dict] = None) -> tuple:
    all_seeds = _read_seeds(seeds_spec)
    selected = shard_seeds(all_seeds, shard_count, shard_index)
    campaign_id = shard_campaign_id(base_id, shard_count, shard_index)
    campaign_dir, paths = _campaign_paths(campaign_id)
    os.makedirs(campaign_dir, exist_ok=True)
    # Re-resolve after mkdir so a concurrently introduced junction fails.
    campaign_dir, paths = _campaign_paths(campaign_id)
    config = {
        "pipeline_version": PIPELINE_VERSION,
        "campaign_id": campaign_id,
        "base_campaign_id": base_id,
        "artifact_root": CAMPAIGN_ROOT,
        "policy": policy,
        "policy_seed": policy_seed,
        "shard_count": shard_count,
        "shard_index": shard_index,
        "source_seed_count": len(all_seeds),
        "seed_list": selected,
    }
    if script is not None:
        config["script"] = script
    if os.path.exists(paths["config"]):
        prior = _read_json(paths["config"])
        if prior != config:
            raise ValueError(
                "pipeline campaign identity mismatch; use a new campaign id")
    else:
        _write_json(paths["config"], config)
    with open(paths["seeds"], "w", encoding="utf-8", newline="\n") as fh:
        for seed in selected:
            fh.write(seed + "\n")
    return campaign_id, campaign_dir, paths, selected


def run_orchestrator(args, campaign_id: str, seed_path: str) -> int:
    command = [
        sys.executable, os.path.join(_driver_dir(), "orchestrator.py"),
        "--campaign-id", campaign_id,
        "--seeds", seed_path,
        "--policy", args.policy,
        "--policy-seed", str(args.policy_seed),
        "--campaign-timeout", str(args.campaign_timeout),
        "--max-actions", str(args.max_actions),
    ]
    if args.fresh:
        command.append("--fresh")
    if args.policy == "script":
        command.extend(["--script", os.path.abspath(args.script)])
    return subprocess.run(command, check=False).returncode


def run_postprocess(campaign_id: str) -> int:
    helper = os.path.join(_repo_root(), "tools", "wsl_run.cmd")
    # wsl_run.cmd's --script contract is a repo-relative path. Passing a
    # Windows absolute path through `%*` loses its backslashes before WSL bash
    # sees it (the exact boundary trap the helper exists to avoid).
    script = "tools/oracle_bridge/driver/postprocess_campaign.sh"
    # Batch files are command-interpreter programs on Windows. list2cmdline
    # gives cmd.exe one quoted command without shell interpolation.
    inner = subprocess.list2cmdline(
        [helper, "--script", script, campaign_id])
    return subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", inner], check=False).returncode


def _read_status(path: str) -> Optional[int]:
    try:
        with open(path, "r", encoding="ascii") as fh:
            return int(fh.read().strip())
    except (OSError, ValueError):
        return None


def _read_text(path: str) -> str:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError:
        return ""


def _sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            block = fh.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _timing_summary(campaign_dir: str, seed: str, actions: int) -> dict:
    path = os.path.join(
        campaign_dir, f"run_{seed}_a20_ironclad.timing.jsonl")
    marks = []
    try:
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                if line.strip():
                    value = json.loads(line)
                    if value.get("record_kind") == "mark":
                        marks.append(value)
    except (OSError, json.JSONDecodeError):
        return {"marks": 0, "active_seconds": None,
                "actions_per_second": None}
    elapsed = None
    rate = None
    if len(marks) >= 2:
        elapsed = marks[-1]["t_mono"] - marks[0]["t_mono"]
        if elapsed > 0:
            rate = actions / elapsed
    return {"marks": len(marks), "active_seconds": elapsed,
            "actions_per_second": rate}


def _capture_race_counts(diff_text: str) -> dict[str, int]:
    """Extract every named capture-race family from a replay summary.

    The replay executable currently reports obtain-race (card/potion obtain
    animation, including Entropic Brew), escape-race (Smoke Bomb settlement)
    and preview-race (Living Wall's wall-clock curse preview). Matching named
    ``*-race`` fields keeps strict accounting conservative when the replay
    classifier grows another narrowly reviewed capture-race family.
    """
    summaries = [
        line for line in diff_text.splitlines()
        if line.startswith("CLEAN ") or line.startswith("PART ")
    ]
    if not summaries:
        return {}
    if len(summaries) != 1:
        raise ValueError(
            f"replay report has {len(summaries)} verdict summaries")
    counts: dict[str, int] = {}
    for value, name in _CAPTURE_RACE_RE.findall(summaries[0]):
        count = int(value)
        if name in counts:
            raise ValueError(
                f"replay report repeats capture-race field {name!r}")
        counts[name] = count
    return counts


def _action_prefix(path: str, through_seq: Optional[int]) -> list:
    out = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("record_kind") != "action":
                continue
            seq = record.get("seq")
            if through_seq is not None and isinstance(seq, int) and \
                    seq > through_seq:
                break
            command = record.get("action_command")
            if command and command != "__terminal_observed__":
                out.append({"seq": seq, "command": command})
    return out


def _triage_item(campaign_dir: str, campaign_id: str, row: dict,
                 classification: str, detail: dict) -> dict:
    seed = row["seed"]
    run_name = f"run_{seed}_a20_ironclad.jsonl"
    run_path = os.path.join(campaign_dir, run_name)
    seq = detail.get("first_divergence_seq")
    prefix = _action_prefix(run_path, seq)
    triage_dir = os.path.join(campaign_dir, "triage", "pending")
    os.makedirs(triage_dir, exist_ok=True)
    repro_name = f"{seed}.reproducer.json"
    command_name = f"{seed}.commands.txt"
    with open(os.path.join(triage_dir, command_name), "w",
              encoding="utf-8", newline="\n") as fh:
        for action in prefix:
            fh.write(action["command"] + "\n")
    repro = {
        "format": "STS-ORACLE-REPRO v1",
        "campaign_id": campaign_id,
        "seed": seed,
        "ascension": 20,
        "character": "IRONCLAD",
        "classification": classification,
        "source_artifact": run_name,
        "first_divergence": detail,
        "action_prefix": prefix,
        "promotion_target": (
            "tests/golden/oracle_reproducers/<case-id>/"),
        "workflow": (
            "Reproduce twice; audit rendering-strip patches; fix or record "
            "the frozen-spec conflict; promote the minimal case and update "
            "docs/stage-b-tasks.md plus the owning change log."),
    }
    _write_json(os.path.join(triage_dir, repro_name), repro)
    return {
        "seed": seed,
        "classification": classification,
        "reproducer": f"triage/pending/{repro_name}",
        "commands": f"triage/pending/{command_name}",
        "detail": detail,
    }


def generate_report(campaign_id: str) -> dict:
    campaign_dir, paths = _campaign_paths(campaign_id)
    progress = _read_json(os.path.join(
        campaign_dir, "campaign_progress.json"))
    manifest = _read_json(os.path.join(
        campaign_dir, "campaign_manifest.json"))
    config = _read_json(paths["config"]) if os.path.exists(
        paths["config"]) else {}

    results = []
    triage = []
    total_active = 0.0
    active_known = True
    known_capture_race_records = 0
    known_capture_race_records_by_kind: dict[str, int] = {}
    for row in progress.get("seeds_done", []):
        seed = row["seed"]
        translation_rc = _read_status(os.path.join(
            campaign_dir, "translation", f"{seed}.status"))
        diff_rc = _read_status(os.path.join(
            campaign_dir, "diffs", f"{seed}.status"))
        lists_rc = _read_status(os.path.join(
            campaign_dir, "encounter_lists", f"{seed}.status"))
        diff_text = _read_text(os.path.join(
            campaign_dir, "diffs", f"{seed}.log"))
        race_counts = _capture_race_counts(diff_text)
        capture_race_records = sum(race_counts.values())
        obtain_race_records = race_counts.get("obtain-race", 0)
        escape_race_records = race_counts.get("escape-race", 0)
        known_capture_race_records += capture_race_records
        for name, count in race_counts.items():
            known_capture_race_records_by_kind[name] = (
                known_capture_race_records_by_kind.get(name, 0) + count)
        first = _FIRST_DIFF_RE.search(diff_text)
        detail = {}
        if first:
            detail = {
                "first_divergence_seq": int(first.group(1)),
                "floor": int(first.group(2)),
                "screen": first.group(3),
                "field_count": int(first.group(4)),
            }

        if translation_rc != 0:
            classification = "translation_drift"
        elif lists_rc != 0:
            classification = "encounter_list_divergence"
        elif diff_rc != 0:
            classification = (
                "state_divergence" if first else "replay_harness_error")
        elif "CLEAN " in diff_text:
            classification = "clean"
        else:
            classification = "replay_harness_error"

        timing = _timing_summary(
            campaign_dir, seed, int(row.get("actions", 0)))
        if timing["active_seconds"] is None:
            active_known = False
        else:
            total_active += timing["active_seconds"]
        result = {
            "seed": seed,
            "outcome": row.get("outcome"),
            "floor": row.get("floor"),
            "actions": row.get("actions"),
            "attempts": row.get("attempts"),
            "classification": classification,
            # Keep the original obtain-only field so existing report consumers
            # remain compatible. Strict evidence uses the all-family total.
            "known_obtain_race_records": obtain_race_records,
            "known_escape_race_records": escape_race_records,
            "known_capture_race_records": capture_race_records,
            "known_capture_race_records_by_kind": race_counts,
            "translation_exit": translation_rc,
            "replay_exit": diff_rc,
            "encounter_lists_exit": lists_rc,
            "source_artifact": f"run_{seed}_a20_ironclad.jsonl",
            "source_artifact_sha256": _sha256_file(os.path.join(
                campaign_dir, f"run_{seed}_a20_ironclad.jsonl")),
            "trace": f"traces/{seed}.trace",
            "diff_report": f"diffs/{seed}.log",
            "encounter_list_report": f"encounter_lists/{seed}.log",
            "timing": timing,
        }
        if detail:
            result["first_divergence"] = detail
        results.append(result)
        if classification != "clean":
            triage.append(_triage_item(
                campaign_dir, campaign_id, row, classification, detail))

    counts = {}
    for result in results:
        key = result["classification"]
        counts[key] = counts.get(key, 0) + 1
    actions = sum(int(row.get("actions", 0))
                  for row in progress.get("seeds_done", []))
    replay_clean_actions = sum(
        int(result.get("actions", 0))
        for result in results
        if result["classification"] == "clean")
    strict_zero_diff_actions = sum(
        int(result.get("actions", 0))
        for result in results
        if result["classification"] == "clean"
        and result["known_capture_race_records"] == 0)
    outcome_counts = {}
    floor_counts = {}
    for result in results:
        outcome = str(result.get("outcome"))
        outcome_counts[outcome] = outcome_counts.get(outcome, 0) + 1
        floor = str(result.get("floor"))
        floor_counts[floor] = floor_counts.get(floor, 0) + 1
    aggregate_rate = (
        actions / total_active
        if active_known and total_active > 0 else None)
    report = {
        "report_format": "STS-ORACLE-CAMPAIGN-REPORT v1",
        "pipeline_version": PIPELINE_VERSION,
        "campaign_id": campaign_id,
        "artifact_root": CAMPAIGN_ROOT,
        "campaign_status": progress.get("status"),
        "schema_version": manifest.get("schema_version"),
        "driver_version": manifest.get("driver_version"),
        "fork_jar_sha256": manifest.get("fork_jar_sha256"),
        "started_utc": progress.get("started_utc"),
        "finished_utc": manifest.get("finished_utc"),
        "policy": progress.get("policy"),
        "policy_seed": progress.get("policy_seed"),
        "shard": {
            "count": config.get("shard_count", 1),
            "index": config.get("shard_index", 0),
            "source_seed_count": config.get(
                "source_seed_count", len(progress.get("seed_list", []))),
        },
        "seeds_requested": len(progress.get("seed_list", [])),
        "seeds_completed": len(progress.get("seeds_done", [])),
        "seeds_failed": len(progress.get("seeds_failed", [])),
        "actions": actions,
        "captured_actions": actions,
        "replay_clean_actions": replay_clean_actions,
        "strict_zero_diff_actions": strict_zero_diff_actions,
        "active_seconds": total_active if active_known else None,
        "actions_per_second": aggregate_rate,
        "outcome_counts": outcome_counts,
        "floor_counts": floor_counts,
        "diff_counts": counts,
        "known_obtain_race_records":
            known_capture_race_records_by_kind.get("obtain-race", 0),
        "known_escape_race_records":
            known_capture_race_records_by_kind.get("escape-race", 0),
        "known_capture_race_records": known_capture_race_records,
        "known_capture_race_records_by_kind":
            known_capture_race_records_by_kind,
        "untriaged_count": len(triage),
        "runs": results,
        "triage_queue": triage,
    }
    _write_json(paths["report_json"], report)
    triage_index = os.path.join(
        campaign_dir, "triage", "pending", "index.json")
    os.makedirs(os.path.dirname(triage_index), exist_ok=True)
    _write_json(triage_index, {
        "campaign_id": campaign_id,
        "pending_count": len(triage),
        "items": triage,
    })

    rate_text = (
        f"{aggregate_rate:.3f}" if aggregate_rate is not None else "unavailable")
    lines = [
        f"# Oracle campaign report — {campaign_id}",
        "",
        f"- Status: `{progress.get('status')}`",
        f"- Seeds: {len(progress.get('seeds_done', []))} completed / "
        f"{len(progress.get('seed_list', []))} requested; "
        f"{len(progress.get('seeds_failed', []))} failed",
        f"- Captured actions: {actions}; replay-clean: "
        f"{replay_clean_actions}; strict zero-diff: "
        f"{strict_zero_diff_actions}; active throughput: "
        f"{rate_text} actions/s",
        f"- Diff classifications: `{json.dumps(counts, sort_keys=True)}`",
        f"- Known capture-race records: {known_capture_race_records} "
        f"(`{json.dumps(known_capture_race_records_by_kind, sort_keys=True)}`)",
        f"- Pending triage: {len(triage)}",
        "",
        "| Seed | Outcome | Floor | Actions | Classification |",
        "|---|---:|---:|---:|---|",
    ]
    for result in results:
        lines.append(
            f"| {result['seed']} | {result['outcome']} | {result['floor']} | "
            f"{result['actions']} | {result['classification']} |")
    with open(paths["report_md"], "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    return report


def postprocess_and_report(campaign_id: str) -> int:
    rc = run_postprocess(campaign_id)
    if rc != 0:
        return rc
    report = generate_report(campaign_id)
    print(os.path.join(
        campaign_dir_under_root(CAMPAIGN_ROOT, campaign_id), "report.md"))
    return EXIT_DIVERGENCES if report["untriaged_count"] else 0


def run_pipeline(args, campaign_base: Optional[str] = None) -> int:
    base = campaign_base or args.campaign_id
    try:
        campaign_id, _campaign_dir, paths, selected = prepare_campaign(
            base, args.seeds, args.shard_count, args.shard_index,
            args.policy, args.policy_seed, script_identity(args))
    except (OSError, ValueError) as exc:
        print(f"invalid pipeline input: {exc}", file=sys.stderr)
        return 2
    print(f"campaign={campaign_id} root={CAMPAIGN_ROOT} "
          f"seeds={len(selected)} shard={args.shard_index}/"
          f"{args.shard_count}", flush=True)
    lock = GameResourceLock(campaign_id)
    try:
        lock.acquire()
    except GameResourceBusy as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_GAME_BUSY
    try:
        rc = run_orchestrator(args, campaign_id, paths["seeds"])
    finally:
        lock.release()
    if rc != 0:
        print(f"orchestrator failed with exit {rc}", file=sys.stderr)
        return rc
    return postprocess_and_report(campaign_id)


def schedule_nightly(args) -> int:
    script = os.path.abspath(__file__)
    validate_campaign_id(args.task_name)
    nightly_args = [
        "nightly",
        "--campaign-prefix", args.campaign_prefix,
        "--seeds", os.path.abspath(args.seeds),
        "--policy", args.policy,
        "--policy-seed", str(args.policy_seed),
        "--shard-count", str(args.shard_count),
        "--shard-index", str(args.shard_index),
        "--campaign-timeout", str(args.campaign_timeout),
        "--max-actions", str(args.max_actions),
    ]
    if args.policy == "script":
        if not args.script:
            raise ValueError("--policy script requires --script")
        nightly_args.extend(["--script", os.path.abspath(args.script)])
    # schtasks.exe limits /TR to 262 characters. Keep its action short and put
    # the fully audited nightly argv in a fixed non-repo config instead.
    os.makedirs(SCHEDULE_ROOT, exist_ok=True)
    config_path = os.path.join(SCHEDULE_ROOT, args.task_name + ".json")
    _write_json(config_path, {
        "format": "STS-ORACLE-SCHEDULE v1",
        "arguments": nightly_args,
    })
    action = [
        sys.executable, script, "scheduled", "--config", config_path,
    ]
    task_command = subprocess.list2cmdline(action)
    command = [
        "schtasks.exe", "/Create", "/F", "/SC", "DAILY",
        "/ST", args.at, "/TN", args.task_name, "/TR", task_command,
    ]
    if args.print_only:
        print(subprocess.list2cmdline(command))
        return 0
    return subprocess.run(command, check=False).returncode


def run_scheduled(config_path: str) -> int:
    value = _read_json(os.path.abspath(config_path))
    if set(value) != {"format", "arguments"} or \
            value.get("format") != "STS-ORACLE-SCHEDULE v1":
        raise ValueError("scheduled config has an unknown format or key")
    arguments = value["arguments"]
    if not isinstance(arguments, list) or not all(
            isinstance(item, str) for item in arguments):
        raise ValueError("scheduled config arguments must be a string array")
    scheduled = parse_args(arguments)
    if scheduled.command != "nightly":
        raise ValueError("scheduled config must invoke nightly")
    day = datetime.now(timezone.utc).strftime("%Y%m%d")
    return run_pipeline(
        scheduled, f"{scheduled.campaign_prefix}_{day}")


def add_run_options(parser, campaign_required=True) -> None:
    if campaign_required:
        parser.add_argument("--campaign-id", required=True)
    parser.add_argument("--seeds", required=True,
                        help="comma-separated seeds or seed-list file")
    parser.add_argument("--policy", choices=["random-legal", "greedy", "script"],
                        default="random-legal")
    parser.add_argument("--script",
                        help="one-command-per-line script for script policy")
    parser.add_argument("--policy-seed", type=int, default=1234)
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0,
                        help="zero-based shard index")
    parser.add_argument("--campaign-timeout", type=float, default=57600.0,
                        help="unattended wall-clock guard in seconds")
    parser.add_argument("--max-actions", type=int, default=3000)
    parser.add_argument("--fresh", action="store_true")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description="B5.2 one-command oracle campaign pipeline")
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="capture, validate, translate, diff, report")
    add_run_options(run)
    nightly = sub.add_parser(
        "nightly", help="run/resume today's dated campaign")
    nightly.add_argument("--campaign-prefix", required=True)
    add_run_options(nightly, campaign_required=False)
    schedule = sub.add_parser(
        "schedule", help="install a Windows daily Task Scheduler entry")
    schedule.add_argument("--campaign-prefix", required=True)
    schedule.add_argument("--seeds", required=True)
    schedule.add_argument("--at", default="01:00", help="local HH:MM")
    schedule.add_argument("--task-name",
                          default="SpeedTheSpire-Oracle-Nightly")
    schedule.add_argument("--print-only", action="store_true")
    schedule.add_argument("--policy",
                          choices=["random-legal", "greedy", "script"],
                          default="random-legal")
    schedule.add_argument("--script")
    schedule.add_argument("--policy-seed", type=int, default=1234)
    schedule.add_argument("--shard-count", type=int, default=1)
    schedule.add_argument("--shard-index", type=int, default=0)
    schedule.add_argument("--campaign-timeout", type=float, default=57600.0)
    schedule.add_argument("--max-actions", type=int, default=3000)
    post = sub.add_parser(
        "postprocess", help="validate/translate/diff/report an existing campaign")
    post.add_argument("--campaign-id", required=True)
    report = sub.add_parser(
        "report", help="regenerate report/triage from derived outputs")
    report.add_argument("--campaign-id", required=True)
    scheduled = sub.add_parser(
        "scheduled", help=argparse.SUPPRESS)
    scheduled.add_argument("--config", required=True)
    seeds = sub.add_parser(
        "generate-seeds", help="write a deterministic sequential seed file")
    seeds.add_argument("--prefix", default="STS")
    seeds.add_argument("--start", type=int, required=True)
    seeds.add_argument("--count", type=int, required=True)
    seeds.add_argument("--width", type=int, default=5)
    seeds.add_argument("--out", required=True)
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    if args.command == "run":
        return run_pipeline(args)
    if args.command == "nightly":
        day = datetime.now(timezone.utc).strftime("%Y%m%d")
        return run_pipeline(args, f"{args.campaign_prefix}_{day}")
    if args.command == "schedule":
        return schedule_nightly(args)
    if args.command == "postprocess":
        validate_campaign_id(args.campaign_id)
        return postprocess_and_report(args.campaign_id)
    if args.command == "report":
        validate_campaign_id(args.campaign_id)
        result = generate_report(args.campaign_id)
        return EXIT_DIVERGENCES if result["untriaged_count"] else 0
    if args.command == "scheduled":
        return run_scheduled(args.config)
    if args.count < 1 or args.start < 0 or args.width < 1:
        print("count/width must be positive and start non-negative",
              file=sys.stderr)
        return 2
    generated = [
        f"{args.prefix}{value:0{args.width}d}"
        for value in range(args.start, args.start + args.count)
    ]
    validate_seed_list(generated)
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(generated) + "\n")
    print(f"wrote {len(generated)} seeds to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
