#!/usr/bin/env python3
"""B5.2 one-command oracle campaign pipeline.

Windows owns the game process. Post-processing crosses into WSL only through
tools/wsl_run.cmd and produces translated traces, replay diffs, a raw
encounter-list oracle result, a triage queue, and generated JSON/Markdown
reports under the same immutable campaign directory.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
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
PARALLEL_GROUP_FORMAT = "STS-ORACLE-PARALLEL-GROUP v1"
PARALLEL_REPORT_FORMAT = "STS-ORACLE-PARALLEL-REPORT v1"
PARALLEL_ORCHESTRATION_VERSION = "parallel-oracle-v1"
CAMPAIGN_ROOT = r"D:\STS_BG_Mod\_oracle_data\campaigns"
GAME_LOCK_PATH = r"D:\STS_BG_Mod\_oracle_data\oracle_game.lock"
SCHEDULE_ROOT = r"D:\STS_BG_Mod\_oracle_data\schedules"
RUNTIME_ROOT = r"D:\STS_BG_Mod\_oracle_data\runtimes"
SOURCE_GAME_DIR = r"D:\SteamLibrary\steamapps\common\SlayTheSpire"
SOURCE_FORK_JAR = (
    r"D:\SteamLibrary\steamapps\common\SlayTheSpire\mods"
    r"\CommunicationMod-oracle.jar")
EXIT_DIVERGENCES = 10
EXIT_GAME_BUSY = 11
DEFAULT_JAVA_XMS_MIB = 256
DEFAULT_JAVA_XMX_MIB = 1024
DEFAULT_SEEDS_PER_LAUNCH = 50
AUTO_NATIVE_HEADROOM_MIB = 768
AUTO_MEMORY_RESERVE_MIB = 4096
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
_PIPELINE_LIFETIME_JOB = None
_RUN_RE = re.compile(r"run_([0-9A-Z]+)_a20_ironclad\.jsonl")
_FIRST_DIFF_RE = re.compile(
    r"first divergence: seq=(\d+) floor=(\d+) screen=([^ ]+) "
    r"\((\d+) field")
_CAPTURE_RACE_RE = re.compile(
    r"\b(\d+)\s+([a-z][a-z0-9-]*-race)\b")


class GameResourceBusy(RuntimeError):
    """The machine-wide oracle campaign coordinator is already owned."""


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


class OrchestratorProcessJob:
    """Outer kill-on-close job containing every worker orchestrator.

    When the pipeline dies, Windows closes this process-owned handle and kills
    the orchestrators.  Closing their handles in turn triggers their inner
    kill-on-close jobs, retiring the JVM trees as well.
    """

    def __init__(self, required: bool):
        self.required = required
        self.handle = None
        if os.name != "nt":
            return
        try:
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CreateJobObjectW.restype = ctypes.c_void_p
            kernel32.SetInformationJobObject.argtypes = [
                ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                ctypes.c_ulong]
            kernel32.SetInformationJobObject.restype = ctypes.c_int
            kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            kernel32.CloseHandle.restype = ctypes.c_int
            handle = kernel32.CreateJobObjectW(None, None)
            if not handle:
                raise OSError(
                    ctypes.get_last_error(), "CreateJobObjectW failed")
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
            self.handle = handle
        except OSError:
            if required:
                raise
            print(
                "warning: legacy orchestrator job containment unavailable",
                file=sys.stderr)

    def assign_handle(self, process_handle) -> None:
        if os.name != "nt":
            return
        if self.handle is None:
            if self.required:
                raise OSError("required orchestrator job is unavailable")
            return
        if process_handle is None:
            raise OSError("orchestrator process has no Windows handle")
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.AssignProcessToJobObject.argtypes = [
            ctypes.c_void_p, ctypes.c_void_p]
        kernel32.AssignProcessToJobObject.restype = ctypes.c_int
        if not kernel32.AssignProcessToJobObject(
                self.handle, ctypes.c_void_p(process_handle)):
            raise OSError(
                ctypes.get_last_error(),
                "AssignProcessToJobObject failed for orchestrator")

    def assign(self, proc: subprocess.Popen) -> None:
        self.assign_handle(getattr(proc, "_handle", None))

    def close(self) -> None:
        if self.handle is not None:
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
            kernel32.CloseHandle.restype = ctypes.c_int
            handle = self.handle
            self.handle = None
            if not kernel32.CloseHandle(handle):
                raise OSError(ctypes.get_last_error(), "CloseHandle failed")


def _install_pipeline_lifetime_job() -> None:
    """Put this pipeline in a job before it can spawn an orchestrator.

    Children inherit membership atomically at process creation, eliminating
    the Popen-to-Assign race.  The handle intentionally remains open for the
    rest of this process; kernel teardown closes it and kills any descendants
    which outlive the pipeline.
    """
    global _PIPELINE_LIFETIME_JOB
    if os.name != "nt" or _PIPELINE_LIFETIME_JOB is not None:
        return
    job = OrchestratorProcessJob(required=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetCurrentProcess.restype = ctypes.c_void_p
    try:
        job.assign_handle(kernel32.GetCurrentProcess())
    except Exception:
        job.close()
        raise
    _PIPELINE_LIFETIME_JOB = job


class GameResourceLock:
    """Cross-process coordinator lock released by the OS after a crash.

    The path and on-disk metadata shape are intentionally unchanged from
    B5.2's singleton game lock.  One owner may now launch several isolated
    games, but two independent top-level schedulers must still not
    oversubscribe the machine or race the shared post-processing build tree.
    """

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
                "oracle campaign coordinator is busy; "
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


def parse_instance_spec(value: str):
    """Argparse type for ``--instances``: ``auto`` or a positive integer."""
    text = str(value).strip().lower()
    if text == "auto":
        return text
    try:
        count = int(text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "instances must be 'auto' or a positive integer") from exc
    if count < 1:
        raise argparse.ArgumentTypeError(
            "instances must be 'auto' or a positive integer")
    return count


def _available_memory_mib() -> Optional[int]:
    """Best-effort available physical memory without a third-party package."""
    if os.name == "nt":
        class MemoryStatusEx(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong),
                ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]
        status = MemoryStatusEx()
        status.dwLength = ctypes.sizeof(status)
        try:
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(
                    ctypes.byref(status)):
                return int(status.ullAvailPhys // (1024 * 1024))
        except (AttributeError, OSError):
            return None
        return None
    try:
        page_size = os.sysconf("SC_PAGE_SIZE")
        available_pages = os.sysconf("SC_AVPHYS_PAGES")
        return int(page_size * available_pages // (1024 * 1024))
    except (AttributeError, OSError, ValueError):
        return None


def _physical_core_budget() -> int:
    """A conservative CPU-bound worker budget.

    The stripped game consumes roughly one busy core.  Python has no portable
    physical-core query, so use one worker per SMT pair and allow an explicit
    ``--instances`` value when the operator has better machine knowledge.
    """
    logical = os.cpu_count() or 1
    return max(1, logical // 2)


def resolve_instance_count(instance_spec, seed_count: int,
                           java_xmx_mib: int = DEFAULT_JAVA_XMX_MIB) -> int:
    """Resolve and bound the local worker topology before artifacts are made."""
    if seed_count < 1:
        raise ValueError("cannot schedule an empty seed selection")
    if isinstance(instance_spec, int):
        if instance_spec < 1:
            raise ValueError("instances must be positive")
        return min(instance_spec, seed_count)
    if instance_spec != "auto":
        raise ValueError("instances must be 'auto' or a positive integer")
    cpu_slots = _physical_core_budget()
    available = _available_memory_mib()
    if available is None:
        memory_slots = cpu_slots
    else:
        usable = max(0, available - AUTO_MEMORY_RESERVE_MIB)
        per_worker = java_xmx_mib + AUTO_NATIVE_HEADROOM_MIB
        memory_slots = max(1, usable // per_worker)
    return max(1, min(seed_count, cpu_slots, memory_slots))


def worker_campaign_id(group_id: str, count: int, index: int) -> str:
    validate_campaign_id(group_id)
    if count < 1 or index < 0 or index >= count:
        raise ValueError("worker index must be in [0, worker count)")
    return validate_campaign_id(
        f"{group_id}.worker-{index + 1:03d}-of-{count:03d}")


def _group_paths(group_id: str) -> tuple:
    group_dir = campaign_dir_under_root(CAMPAIGN_ROOT, group_id)
    return group_dir, {
        "manifest": campaign_file_under_root(
            CAMPAIGN_ROOT, group_id, "parallel_group.json"),
        "report_json": campaign_file_under_root(
            CAMPAIGN_ROOT, group_id, "parallel_report.json"),
        "report_md": campaign_file_under_root(
            CAMPAIGN_ROOT, group_id, "parallel_report.md"),
    }


def _parallel_identity(base_id: str, group_id: str, all_seeds: list,
                       selected: list, outer_shard_count: int,
                       outer_shard_index: int, policy: str, policy_seed: int,
                       script: Optional[dict]) -> dict:
    identity = {
        "base_campaign_id": base_id,
        "group_campaign_id": group_id,
        "artifact_root": CAMPAIGN_ROOT,
        "outer_shard_count": outer_shard_count,
        "outer_shard_index": outer_shard_index,
        "source_seed_count": len(all_seeds),
        "selected_seed_list": selected,
        "policy": policy,
        "policy_seed": policy_seed,
    }
    if script is not None:
        identity["script"] = script
    return identity


def _prepare_exact_worker(group: dict, worker: dict) -> dict:
    """Create/validate one ordinary campaign directory for a local worker."""
    campaign_id = worker["campaign_id"]
    campaign_dir, paths = _campaign_paths(campaign_id)
    os.makedirs(campaign_dir, exist_ok=True)
    campaign_dir, paths = _campaign_paths(campaign_id)
    config = {
        "pipeline_version": PIPELINE_VERSION,
        "campaign_id": campaign_id,
        "base_campaign_id": group["base_campaign_id"],
        "artifact_root": CAMPAIGN_ROOT,
        "policy": group["policy"],
        "policy_seed": group["policy_seed"],
        "shard_count": group["outer_shard_count"],
        "shard_index": group["outer_shard_index"],
        "source_seed_count": group["source_seed_count"],
        "seed_list": worker["seed_list"],
        "parallel_group_id": group["group_campaign_id"],
        "parallel_worker_count": group["resolved_instances"],
        "parallel_worker_index": worker["index"],
    }
    if "script" in group:
        config["script"] = group["script"]
    if os.path.exists(paths["config"]):
        prior = _read_json(paths["config"])
        if prior != config:
            raise ValueError(
                f"worker {campaign_id} campaign identity mismatch; "
                "use a new campaign id")
    else:
        _write_json(paths["config"], config)
    with open(paths["seeds"], "w", encoding="utf-8", newline="\n") as fh:
        for seed in worker["seed_list"]:
            fh.write(seed + "\n")
    return {"campaign_dir": campaign_dir, "paths": paths, **worker}


def prepare_parallel_group(base_id: str, seeds_spec: Optional[str],
                           outer_shard_count: int,
                           outer_shard_index: int, policy: str,
                           policy_seed: int, instance_spec,
                           script: Optional[dict] = None,
                           java_xmx_mib: int = DEFAULT_JAVA_XMX_MIB,
                           java_xms_mib: int = DEFAULT_JAVA_XMS_MIB,
                           seeds_per_launch: int =
                           DEFAULT_SEEDS_PER_LAUNCH,
                           resume_existing: bool = False) -> tuple:
    """Persist a deterministic host-shard then local-worker topology."""
    if (not isinstance(java_xms_mib, int) or java_xms_mib < 1
            or not isinstance(java_xmx_mib, int) or java_xmx_mib < 1
            or java_xms_mib > java_xmx_mib):
        raise ValueError(
            "Java heap sizes must be positive and Xms must not exceed Xmx")
    if not isinstance(seeds_per_launch, int) or seeds_per_launch < 1:
        raise ValueError("seeds_per_launch must be positive")
    group_id = shard_campaign_id(
        base_id, outer_shard_count, outer_shard_index)
    group_dir, paths = _group_paths(group_id)
    if resume_existing:
        if not os.path.exists(paths["manifest"]):
            raise ValueError(
                "--resume-existing requires an existing parallel group")
        group = _read_json(paths["manifest"])
        if group.get("format") != PARALLEL_GROUP_FORMAT:
            raise ValueError("parallel group manifest has an unknown format")
        selected = validate_seed_list(group.get("selected_seed_list"))
        if group.get("selected_seed_list") != selected:
            raise ValueError(
                "parallel group selected seed list is not canonical")
        if not isinstance(group.get("source_seed_count"), int) or \
                group["source_seed_count"] < len(selected):
            raise ValueError("parallel group has an invalid source seed count")
        resume_identity = {
            "base_campaign_id": base_id,
            "group_campaign_id": group_id,
            "artifact_root": CAMPAIGN_ROOT,
            "outer_shard_count": outer_shard_count,
            "outer_shard_index": outer_shard_index,
            "policy": policy,
            "policy_seed": policy_seed,
        }
        prior_identity = {
            key: group.get(key) for key in resume_identity
        }
        if prior_identity != resume_identity or group.get("script") != script:
            raise ValueError(
                "parallel campaign identity mismatch; use a new campaign id")
    else:
        if seeds_spec is None:
            raise ValueError("parallel campaign requires a seed list")
        all_seeds = _read_seeds(seeds_spec)
        selected = shard_seeds(
            all_seeds, outer_shard_count, outer_shard_index)
        identity = _parallel_identity(
            base_id, group_id, all_seeds, selected,
            outer_shard_count, outer_shard_index, policy, policy_seed, script)
        os.makedirs(group_dir, exist_ok=True)
        group_dir, paths = _group_paths(group_id)
        if os.path.exists(paths["manifest"]):
            group = _read_json(paths["manifest"])
            if group.get("format") != PARALLEL_GROUP_FORMAT:
                raise ValueError("parallel group manifest has an unknown format")
            prior_identity = {
                key: group.get(key) for key in identity
            }
            if prior_identity != identity:
                raise ValueError(
                    "parallel campaign identity mismatch; use a new campaign id")
        else:
            count = resolve_instance_count(
                instance_spec, len(selected), java_xmx_mib)
            workers = [
                {
                    "index": index,
                    "campaign_id": worker_campaign_id(group_id, count, index),
                    "seed_list": selected[index::count],
                }
                for index in range(count)
            ]
            group = {
                "format": PARALLEL_GROUP_FORMAT,
                "orchestration_version": PARALLEL_ORCHESTRATION_VERSION,
                **identity,
                "requested_instances": instance_spec,
                "resolved_instances": count,
                "java_xms_mib": java_xms_mib,
                "java_xmx_mib": java_xmx_mib,
                "seeds_per_launch": seeds_per_launch,
                "workers": workers,
            }
            _write_json(paths["manifest"], group)

    if resume_existing or os.path.exists(paths["manifest"]):
        count = group.get("resolved_instances")
        if not isinstance(count, int) or count < 1:
            raise ValueError("parallel group has an invalid instance count")
        requested_count = (
            min(instance_spec, len(selected))
            if isinstance(instance_spec, int) else count)
        if requested_count != count:
            raise ValueError(
                f"parallel topology is already {count} instance(s), not "
                f"the requested {instance_spec}; use a new campaign id")
        for key, value in (
                ("java_xms_mib", java_xms_mib),
                ("java_xmx_mib", java_xmx_mib),
                ("seeds_per_launch", seeds_per_launch)):
            if group.get(key) != value:
                raise ValueError(
                    f"parallel topology {key} is already {group.get(key)!r}, "
                    f"not {value!r}; use a new campaign id")
    workers = group.get("workers")
    if not isinstance(workers, list) or len(workers) != count:
        raise ValueError("parallel group worker topology is malformed")
    expected_ids = [worker_campaign_id(group_id, count, i)
                    for i in range(count)]
    observed_ids = [row.get("campaign_id") for row in workers]
    observed_seeds = [seed for row in workers
                      for seed in row.get("seed_list", [])]
    if observed_ids != expected_ids or sorted(observed_seeds) != \
            sorted(selected) or len(observed_seeds) != len(set(observed_seeds)):
        raise ValueError(
            "parallel group workers do not form the exact selected seed set")
    prepared = [_prepare_exact_worker(group, row) for row in workers]
    return group, {**paths, "group_dir": group_dir, "workers": prepared}


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


def _terminate_process_tree(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    if os.name == "nt":
        try:
            subprocess.run(
                ["taskkill", "/T", "/F", "/PID", str(proc.pid)],
                capture_output=True, check=False)
        except OSError:
            proc.kill()
    else:
        proc.kill()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()


def run_orchestrator(args, campaign_id: str, seed_path: str,
                     runtime=None,
                     process_job: Optional[OrchestratorProcessJob] = None
                     ) -> int:
    command = [
        sys.executable, os.path.join(_driver_dir(), "orchestrator.py"),
        "--campaign-id", campaign_id,
        "--seeds", seed_path,
        "--game-dir", args.game_dir,
        "--fork-jar", args.fork_jar,
        "--policy", args.policy,
        "--policy-seed", str(args.policy_seed),
        "--campaign-timeout", str(args.campaign_timeout),
        "--max-actions", str(args.max_actions),
        "--seeds-per-launch", str(args.seeds_per_launch),
    ]
    if runtime is not None:
        command.extend([
            "--runtime-workdir", os.fspath(runtime.game_workdir),
            "--runtime-local-app-data", os.fspath(runtime.local_app_data),
            "--runtime-app-data", os.fspath(runtime.app_data),
            "--runtime-temp-dir", os.fspath(runtime.temp_dir),
            "--runtime-fork-jar", os.fspath(runtime.fork_jar),
            "--java-xms-mib", str(args.java_xms_mib),
            "--java-xmx-mib", str(args.java_xmx_mib),
        ])
    if args.fresh:
        command.append("--fresh")
    if args.policy == "script":
        command.extend(["--script", os.path.abspath(args.script)])
    proc = subprocess.Popen(command)
    try:
        if process_job is not None:
            process_job.assign(proc)
    except Exception:
        _terminate_process_tree(proc)
        raise
    return proc.wait()


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


def _legacy_campaign_exists(base_id: str, shard_count: int,
                            shard_index: int) -> bool:
    """Whether this id already belongs to the pre-parallel B5.2 layout."""
    campaign_id = shard_campaign_id(base_id, shard_count, shard_index)
    _campaign_dir, paths = _campaign_paths(campaign_id)
    _group_dir, group_paths = _group_paths(campaign_id)
    if os.path.exists(group_paths["manifest"]):
        return False
    return any(os.path.exists(path) for path in (
        paths["config"],
        campaign_file_under_root(
            CAMPAIGN_ROOT, campaign_id, "campaign_progress.json"),
        campaign_file_under_root(
            CAMPAIGN_ROOT, campaign_id, "campaign_manifest.json"),
    ))


def _run_legacy_pipeline(args, base: str) -> int:
    """The exact single shared-cwd path used by in-progress B5.2 campaigns."""
    try:
        owner_id = shard_campaign_id(
            base, args.shard_count, args.shard_index)
    except ValueError as exc:
        print(f"invalid pipeline input: {exc}", file=sys.stderr)
        return 2
    lock = GameResourceLock(owner_id)
    try:
        lock.acquire()
    except GameResourceBusy as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_GAME_BUSY
    process_job = None
    try:
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
        process_job = OrchestratorProcessJob(required=False)
        rc = run_orchestrator(
            args, campaign_id, paths["seeds"], process_job=process_job)
        if rc != 0:
            print(f"orchestrator failed with exit {rc}", file=sys.stderr)
            return rc
        return postprocess_and_report(campaign_id)
    finally:
        try:
            if process_job is not None:
                process_job.close()
        finally:
            lock.release()


def _runtime_row(worker: dict, runtime) -> dict:
    manifest_path = os.fspath(runtime.manifest_path)
    return {
        "index": worker["index"],
        "campaign_id": worker["campaign_id"],
        "runtime_format": runtime.runtime_format,
        "runtime_manifest": manifest_path.replace("\\", "/"),
        "runtime_manifest_sha256": _sha256_file(manifest_path),
        "desktop_sha256": runtime.desktop_sha256,
        "fork_sha256": runtime.fork_sha256,
        "profile_template_sha256": runtime.profile_template_sha256,
    }


def generate_parallel_report(group: dict, paths: dict,
                             capture_results: dict,
                             postprocess_results: dict,
                             capture_wall_seconds: float) -> dict:
    worker_rows = []
    totals = {
        "captured_actions": 0,
        "replay_clean_actions": 0,
        "strict_zero_diff_actions": 0,
        "untriaged_count": 0,
    }
    completed_seeds = []
    diff_counts = {}
    for worker in group["workers"]:
        campaign_id = worker["campaign_id"]
        _campaign_dir, worker_paths = _campaign_paths(campaign_id)
        child = None
        current_derivation_succeeded = (
            capture_results.get(campaign_id) == 0
            and postprocess_results.get(campaign_id) in (
                0, EXIT_DIVERGENCES))
        if current_derivation_succeeded and os.path.exists(
                worker_paths["report_json"]):
            child = _read_json(worker_paths["report_json"])
            for key in totals:
                totals[key] += int(child.get(key, 0))
            child_seeds = [
                row.get("seed") for row in child.get("runs", [])
                if isinstance(row, dict) and isinstance(row.get("seed"), str)
            ]
            if (sorted(child_seeds) != sorted(worker["seed_list"])
                    or len(child_seeds) != len(set(child_seeds))):
                raise ValueError(
                    f"parallel child report {campaign_id} does not form its "
                    "exact worker seed set")
            completed_seeds.extend(child_seeds)
            for key, count in child.get("diff_counts", {}).items():
                diff_counts[key] = diff_counts.get(key, 0) + int(count)
        worker_rows.append({
            "index": worker["index"],
            "campaign_id": campaign_id,
            "seeds_requested": len(worker["seed_list"]),
            "capture_exit": capture_results.get(campaign_id),
            "postprocess_exit": postprocess_results.get(campaign_id),
            "report": (
                os.path.relpath(worker_paths["report_json"], paths["group_dir"])
                .replace("\\", "/")
                if child is not None else None),
        })
    requested = group["selected_seed_list"]
    if len(completed_seeds) != len(set(completed_seeds)):
        raise ValueError(
            "parallel child reports contain a seed assigned to two workers")
    all_derivations_succeeded = all(
        capture_results.get(worker["campaign_id"]) == 0
        and postprocess_results.get(worker["campaign_id"]) in (
            0, EXIT_DIVERGENCES)
        for worker in group["workers"])
    if all_derivations_succeeded and sorted(completed_seeds) != \
            sorted(requested):
        raise ValueError(
            "successful parallel child reports do not form the exact "
            "requested seed set")
    aggregate_rate = (
        totals["captured_actions"] / capture_wall_seconds
        if capture_wall_seconds > 0 else None)
    report = {
        "report_format": PARALLEL_REPORT_FORMAT,
        "orchestration_version": PARALLEL_ORCHESTRATION_VERSION,
        "group_campaign_id": group["group_campaign_id"],
        "resolved_instances": group["resolved_instances"],
        "seeds_requested": len(requested),
        "seeds_reported": len(completed_seeds),
        "capture_wall_seconds": capture_wall_seconds,
        "aggregate_actions_per_second": aggregate_rate,
        **totals,
        "diff_counts": diff_counts,
        "workers": worker_rows,
    }
    _write_json(paths["report_json"], report)
    rate = (f"{aggregate_rate:.3f}" if aggregate_rate is not None
            else "unavailable")
    lines = [
        f"# Parallel oracle campaign report — {group['group_campaign_id']}",
        "",
        f"- Instances: {group['resolved_instances']}",
        f"- Seeds reported: {len(completed_seeds)} / {len(requested)}",
        f"- Captured actions: {totals['captured_actions']}",
        f"- Capture wall time: {capture_wall_seconds:.3f} s; aggregate: "
        f"{rate} actions/s",
        f"- Pending triage: {totals['untriaged_count']}",
        "",
        "| Worker | Seeds | Capture | Post-process |",
        "|---|---:|---:|---:|",
    ]
    for row in worker_rows:
        lines.append(
            f"| {row['campaign_id']} | {row['seeds_requested']} | "
            f"{row['capture_exit']} | {row['postprocess_exit']} |")
    with open(paths["report_md"], "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    return report


def _run_parallel_pipeline_locked(args, group: dict, paths: dict) -> int:
    """Run capture and shared-build postprocessing under the coordinator."""
    capture_results = {}
    runtime_by_campaign = {}
    capture_started = datetime.now(timezone.utc).timestamp()
    process_job = None
    executor = None
    try:
        # This job is deliberately outside the worker threads.  A hard death
        # of this pipeline closes the only owning handle and kills every
        # orchestrator; each orchestrator's nested job then kills its JVM.
        process_job = OrchestratorProcessJob(required=True)
        # Import lazily so report/postprocess-only use remains independent of
        # the Windows runtime staging implementation.
        from instance_runtime import prepare_instance_runtime

        for worker in paths["workers"]:
            runtime = prepare_instance_runtime(
                campaign_id=worker["campaign_id"],
                source_game_dir=args.game_dir,
                source_fork_jar=args.fork_jar,
                runtime_root=RUNTIME_ROOT,
                fresh=args.fresh,
            )
            runtime_by_campaign[worker["campaign_id"]] = runtime

        runtime_rows = [
            _runtime_row(worker, runtime_by_campaign[worker["campaign_id"]])
            for worker in paths["workers"]
        ]
        if "runtimes" in group and group["runtimes"] != runtime_rows:
            raise ValueError(
                "parallel runtime provenance changed under an existing "
                "campaign id")
        if "runtimes" not in group:
            group["runtimes"] = runtime_rows
            _write_json(paths["manifest"], group)

        executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=group["resolved_instances"],
            thread_name_prefix="oracle-worker")
        futures = {
            executor.submit(
                run_orchestrator, args, worker["campaign_id"],
                worker["paths"]["seeds"],
                runtime_by_campaign[worker["campaign_id"]],
                process_job
            ): worker["campaign_id"]
            for worker in paths["workers"]
        }
        for future in concurrent.futures.as_completed(futures):
            campaign_id = futures[future]
            try:
                capture_results[campaign_id] = int(future.result())
            except Exception as exc:  # noqa: BLE001 - worker boundary
                print(
                    f"worker {campaign_id} raised: {exc}",
                    file=sys.stderr)
                capture_results[campaign_id] = 2
    except (OSError, ValueError) as exc:
        print(f"parallel runtime preparation failed: {exc}", file=sys.stderr)
        return 2
    finally:
        # Close containment before waiting during exceptional unwind: this is
        # what makes KeyboardInterrupt promptly retire running workers rather
        # than ThreadPoolExecutor waiting forever for orphan orchestrators.
        try:
            if process_job is not None:
                process_job.close()
        finally:
            if executor is not None:
                executor.shutdown(wait=True, cancel_futures=True)
    capture_wall_seconds = max(
        0.0, datetime.now(timezone.utc).timestamp() - capture_started)

    postprocess_results = {}
    for worker in paths["workers"]:
        campaign_id = worker["campaign_id"]
        if capture_results.get(campaign_id) == 0:
            try:
                postprocess_results[campaign_id] = postprocess_and_report(
                    campaign_id)
            except Exception as exc:  # noqa: BLE001 - worker boundary
                print(
                    f"worker {campaign_id} postprocess raised: {exc}",
                    file=sys.stderr)
                postprocess_results[campaign_id] = 2

    try:
        generate_parallel_report(
            group, paths, capture_results, postprocess_results,
            capture_wall_seconds)
    except (OSError, ValueError) as exc:
        print(f"parallel report failed: {exc}", file=sys.stderr)
        return 2

    for worker in group["workers"]:
        rc = capture_results.get(worker["campaign_id"], 2)
        if rc != 0:
            return rc
    for worker in group["workers"]:
        rc = postprocess_results.get(worker["campaign_id"], 2)
        if rc not in (0, EXIT_DIVERGENCES):
            return rc
    if any(postprocess_results.get(worker["campaign_id"]) ==
           EXIT_DIVERGENCES for worker in group["workers"]):
        return EXIT_DIVERGENCES
    return 0


def run_parallel_pipeline(args, base: str) -> int:
    try:
        owner_id = shard_campaign_id(
            base, args.shard_count, args.shard_index)
    except ValueError as exc:
        print(f"invalid parallel pipeline input: {exc}", file=sys.stderr)
        return 2
    lock = GameResourceLock(owner_id)
    try:
        lock.acquire()
    except GameResourceBusy as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_GAME_BUSY
    try:
        try:
            group, paths = prepare_parallel_group(
                base, args.seeds, args.shard_count, args.shard_index,
                args.policy, args.policy_seed, args.instances,
                script_identity(args), args.java_xmx_mib, args.java_xms_mib,
                args.seeds_per_launch, args.resume_existing)
        except (OSError, ValueError) as exc:
            print(f"invalid parallel pipeline input: {exc}", file=sys.stderr)
            return 2
        print(
            f"campaign-group={group['group_campaign_id']} "
            f"root={CAMPAIGN_ROOT} "
            f"seeds={len(group['selected_seed_list'])} "
            f"instances={group['resolved_instances']} outer-shard="
            f"{args.shard_index}/{args.shard_count}", flush=True)
        return _run_parallel_pipeline_locked(args, group, paths)
    finally:
        lock.release()


def run_pipeline(args, campaign_base: Optional[str] = None) -> int:
    base = campaign_base or args.campaign_id
    if _legacy_campaign_exists(
            base, args.shard_count, args.shard_index):
        if args.resume_existing:
            print(
                "--resume-existing applies only to parallel campaign groups; "
                "pass the original --seeds for this legacy campaign",
                file=sys.stderr)
            return 2
        print(
            "existing pre-parallel campaign detected; resuming the legacy "
            "single shared-runtime topology", flush=True)
        return _run_legacy_pipeline(args, base)
    return run_parallel_pipeline(args, base)


def run_locked_artifact_operation(campaign_id: str, operation) -> int:
    """Serialize standalone report/postprocess writes with active campaigns."""
    campaign_id = validate_campaign_id(campaign_id)
    lock = GameResourceLock(campaign_id)
    try:
        lock.acquire()
    except GameResourceBusy as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_GAME_BUSY
    try:
        return int(operation(campaign_id))
    finally:
        lock.release()


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
        "--instances", str(args.instances),
        "--java-xms-mib", str(args.java_xms_mib),
        "--java-xmx-mib", str(args.java_xmx_mib),
        "--seeds-per-launch", str(args.seeds_per_launch),
        "--game-dir", args.game_dir,
        "--fork-jar", args.fork_jar,
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
    seed_source = parser.add_mutually_exclusive_group(required=True)
    seed_source.add_argument(
        "--seeds", help="comma-separated seeds or seed-list file")
    seed_source.add_argument(
        "--resume-existing", action="store_true",
        help="resume an existing parallel group from its persisted seed set")
    parser.add_argument("--policy", choices=["random-legal", "greedy", "script"],
                        default="random-legal")
    parser.add_argument("--script",
                        help="one-command-per-line script for script policy")
    parser.add_argument("--policy-seed", type=int, default=1234)
    parser.add_argument("--shard-count", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0,
                        help="zero-based shard index")
    parser.add_argument(
        "--instances", type=parse_instance_spec, default="auto",
        help="local isolated game instances: positive integer or auto")
    parser.add_argument("--java-xms-mib", type=int,
                        default=DEFAULT_JAVA_XMS_MIB)
    parser.add_argument("--java-xmx-mib", type=int,
                        default=DEFAULT_JAVA_XMX_MIB)
    parser.add_argument("--seeds-per-launch", type=int,
                        default=DEFAULT_SEEDS_PER_LAUNCH,
                        help="recycle each JVM after this many completed seeds")
    parser.add_argument("--game-dir", default=SOURCE_GAME_DIR)
    parser.add_argument("--fork-jar", default=SOURCE_FORK_JAR)
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
    schedule.add_argument(
        "--instances", type=parse_instance_spec, default="auto")
    schedule.add_argument("--java-xms-mib", type=int,
                          default=DEFAULT_JAVA_XMS_MIB)
    schedule.add_argument("--java-xmx-mib", type=int,
                          default=DEFAULT_JAVA_XMX_MIB)
    schedule.add_argument("--seeds-per-launch", type=int,
                          default=DEFAULT_SEEDS_PER_LAUNCH)
    schedule.add_argument("--game-dir", default=SOURCE_GAME_DIR)
    schedule.add_argument("--fork-jar", default=SOURCE_FORK_JAR)
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
        return run_locked_artifact_operation(
            args.campaign_id, postprocess_and_report)
    if args.command == "report":
        def report_operation(campaign_id):
            result = generate_report(campaign_id)
            return EXIT_DIVERGENCES if result["untriaged_count"] else 0
        return run_locked_artifact_operation(
            args.campaign_id, report_operation)
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


def script_entrypoint(argv=None) -> int:
    """Process entrypoint with inheritance-safe Windows containment."""
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments and arguments[0] in (
            "run", "nightly", "scheduled", "postprocess"):
        _install_pipeline_lifetime_job()
    return main(arguments)


if __name__ == "__main__":
    raise SystemExit(script_entrypoint())
