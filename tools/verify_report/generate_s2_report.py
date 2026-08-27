#!/usr/bin/env python3
"""Generate the deterministic S2.43 verification dashboard (S2-G2 items 1-4).

The B5.4 dashboard (`generate_report.py`) aggregated one flat list of campaign
reports against the G7 bar.  This is its S2 sibling: it reopens every artifact
of the S2.43 oracle campaigns, applies the retest classification log the triage
waves regenerated, joins the registry, and reports design `docs/s2-design.md`
section 6 items 1-4 with **literal shortfalls** -- the numbers as the inputs
read today, never a bar inferred to be met.

Four rules the S2 evidence forced, each a deliberate departure from B5.4:

* **The classification input is layered.**  A worker `report.json` carries the
  classification the capture-time postprocess wrote.  When a triage wave fixes
  the engine, the retest instrument is a fresh `--replay` sweep whose log is
  the *current* classification of the runs it covers.  A run covered by the
  retest log takes the retest verdict; every other run keeps its report
  verdict.  A classification string outside the known vocabulary is fatal --
  there is no "other" bucket.
* **Provenance is per cohort group, not one aggregate.**  The escape-window
  recaptures were taken under later fork pins *on purpose*; collapsing them
  into one required pin would hide exactly the fact that matters.
* **A non-gameplay terminal is reported, not fatal.**  `generate_report.py`
  raises on such a run; a dashboard whose whole job is to print shortfalls
  must instead exclude it from the full-run count, list it, and keep going.
* **A stale disposition is reported, not fatal.**  This dashboard regenerates
  over changing classification inputs: when a sibling task makes a
  dispositioned run replay clean, the right output is a rendered "no longer
  exercised" row, not a tool that refuses to run.  An *untriaged* finding is
  still counted against the bar, which is the half that protects the gate.

Runtime: the artifact pass reads and hashes every consumed capture (roughly
4 GB for the current cohorts; well under a minute warm).
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(REPO / "tools" / "registry_gen"))
from stsgen.loader import load_registry  # noqa: E402

from generate_report import (  # noqa: E402
    ReportError,
    capture_race_record_count,
    read_json,
    sha256_file,
    write_text_lf,
)

REPORT_FORMAT = "STS-S2-VERIFICATION-REPORT v1"
DISPOSITIONS_FORMAT = "STS-S2-DIVERGENCE-DISPOSITIONS v1"

# The cohorts S2.43's breadth wave produced.  These are the *current* named
# cohorts, in the `generate_report.py` tradition of a no-argument default that
# names today's evidence; `--breadth`/`--recapture` select a different set, and
# the depth cohorts join here when S2.V2's scan output schedules them.
DEFAULT_BREADTH_GROUPS = (
    "s243_breadth_rand",
    "s243_breadth_take",
    "s243_breadth_skip",
)
DEFAULT_RECAPTURE_GROUPS = (
    "s243_recap_take",
    "s243_recap_skip",
    "s243_recap2_take",
    "s243_recap2_skip",
)
DEFAULT_ARTIFACT_ROOT = Path(r"D:\STS_BG_Mod\_oracle_data\campaigns")
DEFAULT_RETEST_LOG = Path(r"D:\STS_BG_Mod\_oracle_data\s243_resweep5.log")
DEFAULT_PREP_CENSUS = Path(
    r"D:\STS_BG_Mod\_oracle_data\s243_prep\scan_s243_prep.txt")

# A run that wedged, hit the action cap or lost the command channel is an
# attempt, but it is not a *full-run* attempt -- design section 6 item 1's own
# word, and the reading `generate_report.py` already encodes for G7.  Such runs
# stay in the evidence inventory and are reported separately; they are never
# silently counted toward the 2,000.
GAMEPLAY_TERMINAL_OUTCOMES = frozenset({
    "death", "victory", "act1_boss_reward",
})

# The whole classification vocabulary.  Anything else is fatal: this tool may
# not invent a bucket for an artifact it cannot classify.
KNOWN_CLASSIFICATIONS = frozenset({"clean", "state_divergence", "part"})
CLEAN_CLASSIFICATION = "clean"

# Dispositions.  `open-*` is reviewed but never acceptance (the B5.4 rule).
RUN_DISPOSITION_STATUSES = frozenset({
    "superseded-by-recapture",
    "standing-deviation",
    "resolved",
    "open-product-divergence",
    "open-harness-gap",
})
# Design section 6 item 4's two sanctioned per-row alternatives to a sighting.
EVENT_DISPOSITION_STATUSES = frozenset({
    "directed-capture-scheduled",
    "reachability-argument",
})

# The only `event_id` strings a capture may carry that are not events.yaml
# rows.  Explicit data, never a pattern: an unknown id is fatal.
NON_REGISTRY_EVENT_IDS = {
    "Neow Event": "the Neow blessing screen -- run-layer content, not an "
                  "events.yaml row (design B section 4.2)",
}

# The scripted boss-relic cohort configs, by the SHA-pinned config file the
# capture header names.  Named data, so a cohort's take/skip identity comes
# from the pin the run actually used and not from its directory name.
BOSS_RELIC_POLICY_CONFIGS = {
    "policy_bossrelic_take.json": "take",
    "policy_bossrelic_skip.json": "skip",
}

ARTIFACT_NAME_RE = re.compile(
    r"^run_(?P<seed>[A-Za-z0-9_]+)_a20_ironclad\.jsonl$")
ACT_RE = re.compile(rb'"act":\s*(\d+)')
ACT_BOSS_RE = re.compile(rb'"act_boss":\s*"([^"]*)"')
EVENT_ID_RE = re.compile(rb'"event_id":\s*"([^"]*)"')
RETEST_RE = re.compile(r"^(CLEAN|PART)\s+(\S+?):\s*(.*)$")
FIRST_DIVERGENCE_RE = re.compile(r"^\s*first divergence:\s*(.*)$")
CENSUS_HEAD_RE = re.compile(
    r"^rows=(\d+)\s+seeds=(\d+)\s+actions=(\d+)\s+max_floor=(\d+)\s+"
    r"failures=(\d+)\s*$")
CENSUS_EVENT_RE = re.compile(r"^ {2}(\S.*?):\s+(\d+)\s+\(")
CENSUS_DEPTH_RE = re.compile(
    r"^\s+act boss (FIGHT|KILL):\s+a1=(\d+)[^a]*a2=(\d+)[^a]*a3=(\d+)")


# --------------------------------------------------------------------------
# artifact pass
# --------------------------------------------------------------------------

def decode_json_string(raw: bytes, path: Path) -> str:
    """Decode one captured JSON string body (the bytes between the quotes)."""
    try:
        return json.loads(b'"' + raw + b'"')
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        raise ReportError(f"{path}: undecodable JSON string {raw!r}") from exc


def scan_artifact(path: Path) -> dict[str, Any]:
    """Read one capture once: hash it, take its header, and observe it.

    The observation is line-local, which is exact here because the capture is
    one JSON record per line and `game_state.act`, `game_state.act_boss` and
    `screen_state.event_id` all live in the same record.  The `act` key appears
    twice per record (`game_state.act` plus the fork's `oracle.act` mirror);
    the two disagreeing would be a capture defect, so it is fatal rather than
    resolved by silently picking one.
    """
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise ReportError(f"cannot read artifact {path}: {exc}") from exc

    digest = hashlib.sha256(data).hexdigest()
    lines = data.split(b"\n")
    if not lines or not lines[0].strip():
        raise ReportError(f"{path}: artifact has no header record")
    try:
        header = json.loads(lines[0])
    except json.JSONDecodeError as exc:
        raise ReportError(f"{path}: unreadable artifact header") from exc
    if not isinstance(header, dict):
        raise ReportError(f"{path}: artifact header is not an object")

    sightings: Counter = Counter()
    act_bosses: dict[int, list[str]] = {}
    max_act = 0
    for line in lines[1:]:
        if not line.strip():
            continue
        boss_match = ACT_BOSS_RE.search(line)
        events = EVENT_ID_RE.findall(line)
        if boss_match is None and not events:
            continue
        acts = {int(value) for value in ACT_RE.findall(line)}
        if len(acts) != 1:
            raise ReportError(
                f"{path}: a record carries {len(acts)} distinct `act` values "
                f"{sorted(acts)}; the capture's act is ambiguous")
        act = next(iter(acts))
        max_act = max(max_act, act)
        if boss_match is not None:
            boss = decode_json_string(boss_match.group(1), path)
            seen = act_bosses.setdefault(act, [])
            if boss not in seen:
                seen.append(boss)
        for raw in events:
            sightings[(act, decode_json_string(raw, path))] += 1
    return {
        "sha256": digest,
        "bytes": len(data),
        "header": header,
        "sightings": sightings,
        "act_bosses": act_bosses,
        "artifact_max_act": max_act,
    }


def validate_header(header: dict[str, Any], campaign_id: str, seed: str,
                    report: dict[str, Any]) -> None:
    """Require the artifact to identify itself as this run's A20 Ironclad capture."""
    seed_value = header.get("seed")
    seed_string = (
        seed_value.get("string") if isinstance(seed_value, dict) else seed_value
    )
    expected = {
        "record_kind": "header",
        "campaign_id": campaign_id,
        "ascension": 20,
        "character": "IRONCLAD",
        "oracle_block_enabled": True,
        "fork_jar_sha256": report.get("fork_jar_sha256"),
        "driver_version": report.get("driver_version"),
        "policy": report.get("policy"),
    }
    mismatches = {
        key: (header.get(key), want)
        for key, want in expected.items() if header.get(key) != want
    }
    if seed_string != seed:
        mismatches["seed.string"] = (seed_string, seed)
    if mismatches:
        raise ReportError(
            f"{campaign_id}/{seed}: artifact header does not identify this "
            f"run as an A20 Ironclad capture of this campaign: {mismatches}")


def header_policy_pins(header: dict[str, Any]) -> dict[str, Any]:
    """The version/pin fields a reader needs to reproduce the capture."""
    external = header.get("external_policy")
    pins = {
        "policy": header.get("policy"),
        "policy_seed": header.get("policy_seed"),
        "policy_cmd_sha256": None,
        "policy_config": None,
        "policy_config_sha256": None,
    }
    if isinstance(external, dict):
        pins["policy_cmd_sha256"] = external.get("cmd_sha256")
        config_path = external.get("config_path")
        pins["policy_config"] = (
            Path(str(config_path).replace("\\", "/")).name
            if config_path else None)
        pins["policy_config_sha256"] = external.get("config_sha256")
    return pins


# --------------------------------------------------------------------------
# inputs: cohort groups, retest log, dispositions, prep census
# --------------------------------------------------------------------------

def group_workers(artifact_root: Path, group: str) -> list[str]:
    manifest = read_json(artifact_root / group / "parallel_group.json")
    if manifest.get("format") != "STS-ORACLE-PARALLEL-GROUP v1":
        raise ReportError(f"{group}: unsupported parallel-group format")
    if manifest.get("group_campaign_id") != group:
        raise ReportError(f"{group}: parallel group names a different cohort")
    workers = manifest.get("workers")
    if not isinstance(workers, list) or not workers:
        raise ReportError(f"{group}: parallel group lists no workers")
    ids = []
    for worker in workers:
        if not isinstance(worker, dict) or not worker.get("campaign_id"):
            raise ReportError(f"{group}: malformed worker entry {worker!r}")
        ids.append(str(worker["campaign_id"]))
    if len(set(ids)) != len(ids):
        raise ReportError(f"{group}: duplicate worker campaign ids")
    return sorted(ids)


def load_retest_log(path: Path) -> dict:
    """Parse a `--replay` sweep log into (campaign_id, seed) -> verdict.

    The sweep prints one `CLEAN <path>: <summary>` or `PART <path>: <summary>`
    line per artifact, optionally followed by an indented `first divergence:`
    line.  Paths carry whatever host ran the sweep (the sweeps run under WSL),
    so only the last two components are used.
    """
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise ReportError(f"cannot read retest log {path}: {exc}") from exc
    entries: dict = {}
    lines = text.splitlines()
    for index, line in enumerate(lines):
        match = RETEST_RE.match(line)
        if match is None:
            continue
        result, raw_path, summary = match.groups()
        parts = raw_path.replace("\\", "/").split("/")
        if len(parts) < 2:
            raise ReportError(
                f"{path}: retest path is not campaign-qualified: {raw_path}")
        campaign_id = parts[-2]
        name_match = ARTIFACT_NAME_RE.match(parts[-1])
        if name_match is None:
            raise ReportError(
                f"{path}: retest names an artifact this tool cannot identify: "
                f"{parts[-1]}")
        key = (campaign_id, name_match.group("seed"))
        first_divergence = None
        if index + 1 < len(lines):
            follow = FIRST_DIVERGENCE_RE.match(lines[index + 1])
            if follow is not None:
                first_divergence = follow.group(1).strip()
        record = {
            "classification": (
                CLEAN_CLASSIFICATION if result == "CLEAN" else "part"),
            "summary": summary.strip(),
            "first_divergence": first_divergence,
        }
        if key in entries and entries[key] != record:
            raise ReportError(
                f"{path}: conflicting retest verdicts for {key[0]}/{key[1]}")
        entries[key] = record
    if not entries:
        raise ReportError(f"{path}: no retest verdicts found")
    return entries


def load_dispositions(path: Path) -> tuple:
    """Read the exact, wildcard-free disposition table.

    Two lists, because the two things being dispositioned are different: a run
    finding is keyed (campaign_id, seed, classification), while design section
    6 item 4's per-row alternative to a sighting is keyed by a registry event
    row's `game_id`.
    """
    value = read_json(path)
    if value.get("format") != DISPOSITIONS_FORMAT:
        raise ReportError(f"{path}: unsupported dispositions format")
    items = value.get("items")
    event_rows = value.get("event_rows")
    if not isinstance(items, list) or not isinstance(event_rows, list):
        raise ReportError(f"{path}: items and event_rows must both be lists")

    runs: dict = {}
    for item in items:
        if not isinstance(item, dict):
            raise ReportError(f"{path}: disposition item must be an object")
        key = tuple(str(item.get(field, "")) for field in
                    ("campaign_id", "seed", "classification"))
        if not all(key):
            raise ReportError(f"{path}: incomplete disposition identity {item!r}")
        if key in runs:
            raise ReportError(f"{path}: duplicate disposition {key}")
        status = item.get("status")
        if status not in RUN_DISPOSITION_STATUSES:
            raise ReportError(f"{path}: invalid status for {key}: {status!r}")
        if not str(item.get("reference", "")).strip() or \
                not str(item.get("note", "")).strip():
            raise ReportError(f"{path}: {key} needs a reference and a note")
        if status == "superseded-by-recapture":
            superseded = item.get("superseded_by")
            if not isinstance(superseded, dict) or \
                    not superseded.get("campaign_id") or \
                    not superseded.get("seed"):
                raise ReportError(
                    f"{path}: {key} must name the recapture that supersedes "
                    f"it as superseded_by {{campaign_id, seed}}")
        runs[key] = item

    events: dict = {}
    for item in event_rows:
        if not isinstance(item, dict):
            raise ReportError(f"{path}: event disposition must be an object")
        game_id = str(item.get("game_id", ""))
        if not game_id:
            raise ReportError(f"{path}: event disposition needs a game_id")
        if game_id in events:
            raise ReportError(f"{path}: duplicate event disposition {game_id}")
        if item.get("status") not in EVENT_DISPOSITION_STATUSES:
            raise ReportError(
                f"{path}: invalid event status for {game_id}: "
                f"{item.get('status')!r}")
        if not str(item.get("reference", "")).strip() or \
                not str(item.get("note", "")).strip():
            raise ReportError(
                f"{path}: event row {game_id} needs a reference and a note")
        events[game_id] = item
    return runs, events


def load_prep_census(path: Path) -> dict[str, Any]:
    """Parse the sim-side `seed_scan` summary the S2.43 prep wave produced."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise ReportError(f"cannot read prep census {path}: {exc}") from exc
    head = None
    events: dict[str, int] = {}
    depth: dict[str, dict[str, int]] = {}
    in_events = False
    in_all_policies = False
    for line in text.splitlines():
        if head is None:
            match = CENSUS_HEAD_RE.match(line)
            if match is not None:
                head = {
                    "rows": int(match.group(1)), "seeds": int(match.group(2)),
                    "actions": int(match.group(3)),
                    "max_floor": int(match.group(4)),
                    "failures": int(match.group(5)),
                }
                continue
        if line.startswith("depth ["):
            in_all_policies = line.startswith("depth [all policies]")
            continue
        if in_all_policies:
            match = CENSUS_DEPTH_RE.match(line)
            if match is not None:
                depth[match.group(1).lower()] = {
                    "act1": int(match.group(2)), "act2": int(match.group(3)),
                    "act3": int(match.group(4)),
                }
                continue
        if line.startswith("events fired"):
            in_events = True
            continue
        if in_events:
            match = CENSUS_EVENT_RE.match(line)
            if match is None:
                in_events = False
                continue
            events[match.group(1)] = int(match.group(2))
    if head is None:
        raise ReportError(f"{path}: prep census has no summary head line")
    if not events:
        raise ReportError(f"{path}: prep census has no events-fired block")
    return {"head": head, "depth": depth, "events_fired_rows": events}


# --------------------------------------------------------------------------
# aggregation
# --------------------------------------------------------------------------

def collect_runs(artifact_root: Path, groups, role: str) -> tuple:
    """Reopen every worker report and every artifact of the named groups."""
    cohorts: list[dict[str, Any]] = []
    runs: list[dict[str, Any]] = []
    for group in groups:
        worker_ids = group_workers(artifact_root, group)
        pins: list[dict[str, Any]] = []
        provenance = None
        group_runs = 0
        group_bytes = 0
        finished: list[str] = []
        worker_hashes: list[dict[str, str]] = []
        for campaign_id in worker_ids:
            directory = artifact_root / campaign_id
            report = read_json(directory / "report.json")
            if report.get("report_format") != "STS-ORACLE-CAMPAIGN-REPORT v1":
                raise ReportError(
                    f"{campaign_id}: unsupported campaign report format")
            if report.get("campaign_id") != campaign_id:
                raise ReportError(
                    f"{campaign_id}: report names a different campaign")
            if report.get("campaign_status") != "complete":
                raise ReportError(f"{campaign_id}: campaign is not complete")
            current = {
                "schema_version": report.get("schema_version"),
                "driver_version": report.get("driver_version"),
                "pipeline_version": report.get("pipeline_version"),
                "fork_jar_sha256": report.get("fork_jar_sha256"),
                "policy": report.get("policy"),
                "policy_seed": report.get("policy_seed"),
            }
            if provenance is None:
                provenance = current
            elif current != provenance:
                raise ReportError(
                    f"{campaign_id}: worker provenance differs from the rest "
                    f"of cohort {group}: {current} != {provenance}")
            finished.append(str(report.get("finished_utc") or ""))
            worker_hashes.append({
                "campaign_id": campaign_id,
                "report_sha256": sha256_file(directory / "report.json"),
            })
            for run in report.get("runs", []):
                seed = str(run.get("seed", ""))
                if not seed:
                    raise ReportError(f"{campaign_id}: a run row has no seed")
                classification = str(run.get("classification", ""))
                if classification not in KNOWN_CLASSIFICATIONS:
                    raise ReportError(
                        f"{campaign_id}/{seed}: unclassifiable run -- the "
                        f"report says {classification!r}, outside the known "
                        f"vocabulary {sorted(KNOWN_CLASSIFICATIONS)}")
                artifact = directory / str(run.get("source_artifact", ""))
                scan = scan_artifact(artifact)
                if scan["sha256"] != str(run.get("source_artifact_sha256", "")):
                    raise ReportError(
                        f"{campaign_id}/{seed}: source artifact hash drift")
                validate_header(scan["header"], campaign_id, seed, report)
                pin = header_policy_pins(scan["header"])
                pins.append(pin)
                config = pin["policy_config"]
                group_bytes += scan["bytes"]
                group_runs += 1
                runs.append({
                    "role": role,
                    "group": group,
                    "campaign_id": campaign_id,
                    "seed": seed,
                    "outcome": str(run.get("outcome", "")),
                    "actions": int(run.get("actions", 0)),
                    "max_act": int(run.get("max_act") or 0),
                    "artifact_max_act": scan["artifact_max_act"],
                    "victory": bool(run.get("victory")),
                    "boss_fight_acts": [int(a) for a in
                                        (run.get("boss_fight_acts") or [])],
                    "boss_kill_acts": [int(a) for a in
                                       (run.get("boss_kill_acts") or [])],
                    "boss_relic_acts": [int(a) for a in
                                        (run.get("boss_relic_acts") or [])],
                    "act_bosses": {str(k): v for k, v in
                                   sorted(scan["act_bosses"].items())},
                    "sightings": scan["sightings"],
                    "capture_classification": classification,
                    "artifact": artifact.name,
                    "artifact_sha256": scan["sha256"],
                    "artifact_bytes": scan["bytes"],
                    "policy": str(report.get("policy", "")),
                    "policy_config": config,
                    "boss_relic_policy": BOSS_RELIC_POLICY_CONFIGS.get(config),
                    "capture_race_records": capture_race_record_count(
                        run, f"{campaign_id}/{seed}"),
                })
        distinct = sorted({json.dumps(pin, sort_keys=True) for pin in pins})
        cohorts.append({
            "group": group,
            "role": role,
            "workers": len(worker_ids),
            "worker_reports": worker_hashes,
            "runs": group_runs,
            "artifact_bytes": group_bytes,
            "provenance": provenance or {},
            "capture_pins": [json.loads(pin) for pin in distinct],
            "newest_finished_utc": max(finished) if finished else None,
        })
    return cohorts, runs


def apply_classifications(runs, retest) -> None:
    for run in runs:
        entry = retest.get((run["campaign_id"], run["seed"]))
        if entry is None:
            run["final_classification"] = run["capture_classification"]
            run["classification_source"] = "capture postprocess"
            run["retest_summary"] = None
            run["retest_first_divergence"] = None
        else:
            run["final_classification"] = entry["classification"]
            run["classification_source"] = "retest sweep"
            run["retest_summary"] = entry["summary"]
            run["retest_first_divergence"] = entry["first_divergence"]


def act_boss_rows(domains, act: int) -> list[str]:
    return sorted(
        str(row["game_id"]) for row in domains["encounters"]
        if row.get("act") == act and row.get("pool") == "BOSS")


def depth_table(runs, act: int, boss_rows) -> dict[str, Any]:
    """Per-BOSS-row Act-N depth accounting, over clean runs only.

    A boss-reward claim is the BOSS_REWARD screen the driver latched for that
    act; the boss chest is `TreasureRoomBoss` in that act; the take/skip axis
    comes from the SHA-pinned boss-relic policy config the run's own header
    names.  Act 3 has no boss chest, so its kill witness is the run's victory
    flag -- exactly how the driver's own reach accounting reads it.
    """
    per_row = {
        boss: {
            "boss_fight_runs": 0,
            "boss_reward_claim_runs": 0,
            "boss_chest_runs": 0,
            "kill_runs": 0,
            "take_cohort_runs": 0,
            "skip_cohort_runs": 0,
            "onward_transition_runs": 0,
        } for boss in boss_rows
    }
    unknown: Counter = Counter()
    unattributed: Counter = Counter()
    for run in runs:
        if run["final_classification"] != CLEAN_CLASSIFICATION:
            continue
        for boss in run["act_bosses"].get(str(act)) or []:
            if boss not in per_row:
                unknown[boss] += 1
                continue
            cell = per_row[boss]
            if act in run["boss_fight_acts"]:
                cell["boss_fight_runs"] += 1
            if act in run["boss_relic_acts"]:
                cell["boss_reward_claim_runs"] += 1
                if run["boss_relic_policy"] == "take":
                    cell["take_cohort_runs"] += 1
                elif run["boss_relic_policy"] == "skip":
                    cell["skip_cohort_runs"] += 1
                else:
                    unattributed[str(run["policy_config"])] += 1
            if act in run["boss_kill_acts"]:
                cell["boss_chest_runs"] += 1
            killed = (run["victory"] if act == 3
                      else act in run["boss_kill_acts"])
            if killed:
                cell["kill_runs"] += 1
            if run["max_act"] > act:
                cell["onward_transition_runs"] += 1
    return {
        "rows": per_row,
        "unknown_boss_identities": dict(sorted(unknown.items())),
        "unattributed_boss_relic_policies": dict(sorted(unattributed.items())),
    }


def double_boss_runs(runs, final_act: int) -> list[dict[str, Any]]:
    """Runs whose own records witnessed two distinct final-act boss identities.

    Detection is artifact-side on purpose: the campaign report's
    `boss_kill_acts` is a SET of act numbers and structurally cannot express
    two kills in one act (`campaign_driver.py` `_reach_fields`).  With no
    Act-3 run in the consumed evidence this detector is unexercised by live
    data; its behaviour is pinned by the synthetic fixtures in
    `test_s2_report.py` rather than asserted from a live column here.
    """
    hits = []
    for run in runs:
        if run["final_classification"] != CLEAN_CLASSIFICATION:
            continue
        bosses = run["act_bosses"].get(str(final_act)) or []
        if len(bosses) >= 2:
            hits.append({
                "campaign_id": run["campaign_id"],
                "seed": run["seed"],
                "bosses": list(bosses),
                "victory": run["victory"],
            })
    return sorted(hits, key=lambda hit: (hit["campaign_id"], hit["seed"]))


def event_coverage(domains, runs, event_dispositions, census
                   ) -> list[dict[str, Any]]:
    """The section 7.4 coverage join behind design section 6 item 4."""
    by_game_id = {str(row["game_id"]): row for row in domains["events"]}
    deep: Counter = Counter()
    any_act: Counter = Counter()
    witnesses: dict = {}
    for run in runs:
        clean = run["final_classification"] == CLEAN_CLASSIFICATION
        for (act, event_id), count in sorted(run["sightings"].items()):
            if event_id not in by_game_id:
                if event_id not in NON_REGISTRY_EVENT_IDS:
                    raise ReportError(
                        f"{run['campaign_id']}/{run['seed']}: capture carries "
                        f"event_id {event_id!r}, which is neither an "
                        f"events.yaml row nor an allowlisted non-registry id")
                continue
            if not clean:
                continue
            any_act[event_id] += count
            if act >= 2:
                deep[event_id] += count
                witnesses.setdefault(
                    event_id, (run["campaign_id"], run["seed"]))
    census_events = census["events_fired_rows"]
    unknown_census = sorted(
        name for name in census_events if name not in by_game_id)
    if unknown_census:
        raise ReportError(
            f"the sim-side census names events that are not registry rows: "
            f"{unknown_census}")
    rows = []
    for row in domains["events"]:
        acts = list((row.get("conditions") or {}).get("acts") or [])
        if not any(act >= 2 for act in acts):
            continue
        game_id = str(row["game_id"])
        disposition = event_dispositions.get(game_id)
        sightings = deep.get(game_id, 0)
        if sightings > 0:
            status = "sighted-zero-diff"
        elif disposition is not None:
            status = "disposition-on-record"
        else:
            status = "OWED"
        witness = witnesses.get(game_id)
        rows.append({
            "id": int(row["id"]),
            "name": str(row["name"]),
            "game_id": game_id,
            "pool": str((row.get("conditions") or {}).get("pool", "")),
            "acts": acts,
            "act2_3_sightings_clean": sightings,
            "any_act_sightings_clean": any_act.get(game_id, 0),
            "sim_prep_census_rows": census_events.get(game_id, 0),
            "witness": ({"campaign_id": witness[0], "seed": witness[1]}
                        if witness else None),
            "disposition": disposition,
            "status": status,
        })
    return rows


def aggregate(artifact_root: Path, breadth_groups, recapture_groups,
              retest_log: Path, dispositions_path: Path, registry_dir: Path,
              prep_census_path: Path) -> dict[str, Any]:
    domains = load_registry(registry_dir)
    retest = load_retest_log(retest_log)
    run_dispositions, event_dispositions = load_dispositions(dispositions_path)
    census = load_prep_census(prep_census_path)

    breadth_cohorts, breadth_runs = collect_runs(
        artifact_root, breadth_groups, "breadth")
    recapture_cohorts, recapture_runs = collect_runs(
        artifact_root, recapture_groups, "recapture")
    cohorts = breadth_cohorts + recapture_cohorts
    runs = breadth_runs + recapture_runs
    index = {(run["campaign_id"], run["seed"]): run for run in runs}
    if len(index) != len(runs):
        raise ReportError("two consumed runs share a (campaign_id, seed)")
    fork_by_group = {
        cohort["group"]: cohort["provenance"].get("fork_jar_sha256")
        for cohort in cohorts
    }

    apply_classifications(runs, retest)
    unmatched = sorted(key for key in retest if key not in index)
    if unmatched:
        raise ReportError(
            f"the retest log classifies runs outside the consumed cohorts: "
            f"{unmatched[:5]}{'...' if len(unmatched) > 5 else ''}")

    # ---- findings and dispositions ---------------------------------------
    findings: list[dict[str, Any]] = []
    for run in sorted(runs, key=lambda r: (r["campaign_id"], r["seed"])):
        if run["final_classification"] == CLEAN_CLASSIFICATION:
            continue
        key = (run["campaign_id"], run["seed"], run["final_classification"])
        disposition = run_dispositions.get(key)
        named = None
        if disposition is not None and \
                disposition["status"] == "superseded-by-recapture":
            target = disposition["superseded_by"]
            target_key = (str(target["campaign_id"]), str(target["seed"]))
            if target_key[1] != run["seed"]:
                raise ReportError(
                    f"{key}: superseded_by names a different seed "
                    f"{target_key[1]}")
            replacement = index.get(target_key)
            if replacement is None:
                raise ReportError(
                    f"{key}: superseded_by names {target_key}, which is not in "
                    f"the consumed evidence")
            if replacement["final_classification"] != CLEAN_CLASSIFICATION:
                raise ReportError(
                    f"{key}: superseded_by names {target_key}, whose own "
                    f"current classification is "
                    f"{replacement['final_classification']!r}, not clean")
            named = {
                "campaign_id": target_key[0],
                "seed": target_key[1],
                "artifact": replacement["artifact"],
                "artifact_sha256": replacement["artifact_sha256"],
                "fork_jar_sha256": fork_by_group.get(replacement["group"]),
                "capture_race_records": replacement["capture_race_records"],
                "outcome": replacement["outcome"],
            }
        findings.append({
            "campaign_id": run["campaign_id"],
            "seed": run["seed"],
            "group": run["group"],
            "role": run["role"],
            "classification": run["final_classification"],
            "capture_classification": run["capture_classification"],
            "classification_source": run["classification_source"],
            "retest_first_divergence": run["retest_first_divergence"],
            "artifact_sha256": run["artifact_sha256"],
            "disposition": disposition,
            "superseding_recapture": named,
        })

    events = event_coverage(domains, runs, event_dispositions, census)
    consumed_campaigns = {campaign_id for campaign_id, _seed in index}
    finding_keys = {
        (f["campaign_id"], f["seed"], f["classification"]) for f in findings
    }
    # A disposition is stale when the thing it dispositions no longer needs
    # one.  Only dispositions naming a campaign this invocation actually read
    # are judged, so selecting a cohort subset does not slander the rest.
    stale_run = sorted(
        key for key in set(run_dispositions) - finding_keys
        if key[0] in consumed_campaigns)
    still_needed = {
        row["game_id"] for row in events if row["status"] != "sighted-zero-diff"
    }
    stale_event = sorted(set(event_dispositions) - still_needed)
    untriaged = [f for f in findings if f["disposition"] is None]
    open_findings = [
        f for f in findings
        if f["disposition"] is not None
        and str(f["disposition"]["status"]).startswith("open-")
    ]

    # ---- item 1: breadth --------------------------------------------------
    breadth_seeds = {run["seed"] for run in breadth_runs}
    full_run_keys = {
        (run["campaign_id"], run["seed"]) for run in breadth_runs
        if run["outcome"] in GAMEPLAY_TERMINAL_OUTCOMES
    }
    non_terminal = sorted(
        ({"campaign_id": run["campaign_id"], "seed": run["seed"],
          "outcome": run["outcome"]}
         for run in breadth_runs
         if (run["campaign_id"], run["seed"]) not in full_run_keys),
        key=lambda entry: (entry["campaign_id"], entry["seed"]))
    policies = sorted({run["policy"] for run in breadth_runs})
    policy_identities = sorted({
        run["policy"] if not run["policy_config"]
        else f"{run['policy']}:{run['policy_config']}"
        for run in breadth_runs
    })
    capture_counts = Counter(
        run["capture_classification"] for run in breadth_runs)
    breadth_final = Counter(run["final_classification"] for run in breadth_runs)
    reclassified = sum(
        1 for run in breadth_runs
        if run["capture_classification"] != run["final_classification"])

    item1 = {
        "distinct_breadth_seeds": len(breadth_seeds),
        "breadth_runs": len(breadth_runs),
        "seed_shortfall_to_2000": max(0, 2000 - len(breadth_seeds)),
        "full_run_attempts": len(full_run_keys),
        "full_run_shortfall_to_2000": max(0, 2000 - len(full_run_keys)),
        "non_gameplay_terminal_runs": non_terminal,
        "gameplay_terminal_outcomes": sorted(GAMEPLAY_TERMINAL_OUTCOMES),
        "policies": policies,
        "policy_identities": policy_identities,
        "mixed_policies": len(policies) >= 2,
        "classification_as_captured": dict(sorted(capture_counts.items())),
        "classification_final": dict(sorted(breadth_final.items())),
        "reclassified_by_retest": reclassified,
        "capture_race_records": sum(
            run["capture_race_records"] for run in breadth_runs),
        "capture_race_runs": sum(
            1 for run in breadth_runs if run["capture_race_records"]),
        "captured_actions": sum(run["actions"] for run in breadth_runs),
        "untriaged_findings": len(untriaged),
        "zero_untriaged": not untriaged,
        "open_findings": len(open_findings),
        "zero_open": not open_findings,
    }
    item1["met"] = bool(
        len(breadth_seeds) >= 2000 and len(full_run_keys) >= 2000
        and item1["mixed_policies"] and not untriaged and not open_findings)

    # ---- items 2 and 3: depth ---------------------------------------------
    act2_rows = act_boss_rows(domains, 2)
    act3_rows = act_boss_rows(domains, 3)
    act2 = depth_table(runs, 2, act2_rows)
    act3 = depth_table(runs, 3, act3_rows)
    doubles = double_boss_runs(runs, 3)
    first_bosses = sorted({hit["bosses"][0] for hit in doubles})

    def rows_where(table, field):
        return sorted(boss for boss, cell in table["rows"].items()
                      if cell[field] > 0)

    item2 = {
        "registry_boss_rows": act2_rows,
        "per_row": act2["rows"],
        "unknown_boss_identities": act2["unknown_boss_identities"],
        "unattributed_boss_relic_policies":
            act2["unattributed_boss_relic_policies"],
        "rows_with_zero_diff_boss_reward_claim":
            rows_where(act2, "boss_reward_claim_runs"),
        "rows_with_zero_diff_boss_chest": rows_where(act2, "boss_chest_runs"),
        "rows_with_take_witness": rows_where(act2, "take_cohort_runs"),
        "rows_with_skip_witness": rows_where(act2, "skip_cohort_runs"),
        "rows_with_onward_transition":
            rows_where(act2, "onward_transition_runs"),
        "act2_entering_clean_runs": sum(
            1 for run in runs
            if run["final_classification"] == CLEAN_CLASSIFICATION
            and run["max_act"] >= 2),
        "act2_boss_fight_clean_runs": sum(
            1 for run in runs
            if run["final_classification"] == CLEAN_CLASSIFICATION
            and 2 in run["boss_fight_acts"]),
    }
    item2["met"] = bool(
        act2_rows
        and all(cell["boss_reward_claim_runs"] > 0
                and cell["boss_chest_runs"] > 0
                and cell["onward_transition_runs"] > 0
                for cell in act2["rows"].values())
        and item2["rows_with_take_witness"]
        and item2["rows_with_skip_witness"])

    item3 = {
        "registry_boss_rows": act3_rows,
        "per_row": act3["rows"],
        "unknown_boss_identities": act3["unknown_boss_identities"],
        "rows_witnessed_killed": rows_where(act3, "kill_runs"),
        "double_boss_runs": doubles,
        "double_boss_run_count": len(doubles),
        "double_boss_shortfall_to_3": max(0, 3 - len(doubles)),
        "double_boss_first_boss_identities": first_bosses,
        "double_boss_identity_shortfall_to_2": max(0, 2 - len(first_bosses)),
        "act3_entering_clean_runs": sum(
            1 for run in runs
            if run["final_classification"] == CLEAN_CLASSIFICATION
            and run["max_act"] >= 3),
        "detector_exercised_by_live_evidence": bool(doubles),
    }
    item3["met"] = bool(
        act3_rows
        and all(cell["kill_runs"] > 0 for cell in act3["rows"].values())
        and len(doubles) >= 3 and len(first_bosses) >= 2)

    # ---- item 4: event depth ----------------------------------------------
    owed = [row for row in events if row["status"] == "OWED"]
    item4 = {
        "act2_3_registry_event_rows": len(events),
        "sighted_zero_diff": sum(
            1 for row in events if row["status"] == "sighted-zero-diff"),
        "disposition_on_record": sum(
            1 for row in events if row["status"] == "disposition-on-record"),
        "owed": len(owed),
        "owed_rows": [row["game_id"] for row in owed],
        "rows": events,
    }
    item4["met"] = not owed

    # ---- inputs and determinism -------------------------------------------
    manifest = sorted(
        ({
            "role": run["role"], "group": run["group"],
            "campaign_id": run["campaign_id"], "seed": run["seed"],
            "artifact": run["artifact"],
            "artifact_bytes": run["artifact_bytes"],
            "sha256": run["artifact_sha256"],
            "outcome": run["outcome"],
            "capture_classification": run["capture_classification"],
            "final_classification": run["final_classification"],
            "classification_source": run["classification_source"],
            "max_act": run["max_act"],
        } for run in runs),
        key=lambda entry: (entry["campaign_id"], entry["seed"]))
    roll = hashlib.sha256()
    for entry in manifest:
        roll.update(f"{entry['campaign_id']}\t{entry['seed']}\t"
                    f"{entry['sha256']}\n".encode("utf-8"))
    newest = max((cohort["newest_finished_utc"] or "") for cohort in cohorts)

    return {
        "format": REPORT_FORMAT,
        "inputs": {
            "artifact_root": str(artifact_root),
            "cohorts": cohorts,
            "artifacts_consumed": len(manifest),
            "artifact_bytes_consumed": sum(
                entry["artifact_bytes"] for entry in manifest),
            "artifact_roll_up_sha256": roll.hexdigest(),
            "retest_log": {
                "path": retest_log.name,
                "sha256": sha256_file(retest_log),
                "verdicts": len(retest),
            },
            "dispositions": {
                "path": dispositions_path.name,
                "sha256": sha256_file(dispositions_path),
                "run_items": len(run_dispositions),
                "event_row_items": len(event_dispositions),
            },
            "prep_census": {
                "path": prep_census_path.name,
                "sha256": sha256_file(prep_census_path),
                **census["head"],
                "depth_all_policies": census["depth"],
            },
            "newest_capture_finished_utc": newest,
        },
        "evidence_totals": {
            "runs_consumed": len(runs),
            "classification_final": dict(sorted(
                Counter(run["final_classification"]
                        for run in runs).items())),
            "findings": len(findings),
        },
        "item1_breadth": item1,
        "item2_act2_depth": item2,
        "item3_act3_depth": item3,
        "item4_event_depth": item4,
        "divergence_inventory": findings,
        "stale_dispositions": {
            "runs": [list(key) for key in stale_run],
            "event_rows": stale_event,
        },
        "artifact_manifest": manifest,
    }


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------

def verdict(flag: bool) -> str:
    return "MET" if flag else "UNMET"


def missing(expected, witnessed) -> str:
    gap = sorted(set(expected) - set(witnessed))
    return ", ".join(gap) if gap else "none"


def short_hash(value, width: int = 16) -> str:
    """Render a pin for a table cell; a missing pin says so, without an ellipsis."""
    if not value:
        return "n/a"
    return f"`{str(value)[:width]}…`"


def counts(mapping) -> str:
    return "; ".join(f"{name} {value:,}"
                     for name, value in sorted(mapping.items())) or "none"


def markdown(report: dict[str, Any]) -> str:
    inputs = report["inputs"]
    item1 = report["item1_breadth"]
    item2 = report["item2_act2_depth"]
    item3 = report["item3_act3_depth"]
    item4 = report["item4_event_depth"]
    act2_total = len(item2["registry_boss_rows"])
    act3_total = len(item3["registry_boss_rows"])
    lines = [
        "# S2.43 verification dashboard — S2-G2 items 1–4",
        "",
        "Generated deterministically by "
        "`tools/verify_report/generate_s2_report.py` — no arguments, the "
        "defaults name the cohorts below. Regenerate from the repository root "
        "with:",
        "",
        "```bat",
        "C:\\Python39\\python.exe tools\\verify_report\\generate_s2_report.py",
        "```",
        "",
        "Re-running it over unchanged inputs rewrites these files byte for "
        "byte. The raw captures stay uncommitted by design (stage-B design "
        "section 7.3); what is committed is this report, the per-artifact hash "
        "manifest that pins them, and the tool that reopens them. Newest "
        f"capture consumed: **{inputs['newest_capture_finished_utc']}**. The "
        "tool README ([../../tools/verify_report/README.md]"
        "(../../tools/verify_report/README.md)) carries the argument forms and "
        "the rules this dashboard applies.",
        "",
        "This is **evidence accounting, not a gate inference**. Every bar "
        "below is the number the inputs carry today; a bar with no evidence is "
        "printed as a literal shortfall, never as a pending success. Items 5–7 "
        "of the design section-6 bar belong to S2.46, S2.44 and S2.45 and are "
        "outside this report's scope.",
        "",
        "Denominator: [../s2-design.md](../s2-design.md) section 6, S2-G2 "
        "items 1–4. Ledger row: [../s2-tasks.md](../s2-tasks.md) S2.43.",
        "",
        "## Verdicts at a glance",
        "",
        "| Bar | Verdict | Headline |",
        "|---|---|---|",
        f"| Item 1 — breadth | **{verdict(item1['met'])}** | "
        f"{item1['distinct_breadth_seeds']:,} distinct seeds, "
        f"{item1['full_run_attempts']:,} full-run attempts, "
        f"{item1['untriaged_findings']} untriaged, "
        f"{item1['open_findings']} open |",
        f"| Item 2 — Act-2 depth | **{verdict(item2['met'])}** | "
        f"{len(item2['rows_with_zero_diff_boss_reward_claim'])} of "
        f"{act2_total} Act-2 BOSS rows carry a zero-diff boss-reward claim |",
        f"| Item 3 — Act-3 depth | **{verdict(item3['met'])}** | "
        f"{len(item3['rows_witnessed_killed'])} of {act3_total} Act-3 BOSS "
        f"rows witnessed killed, {item3['double_boss_run_count']} double-boss "
        f"runs |",
        f"| Item 4 — event depth | **{verdict(item4['met'])}** | "
        f"{item4['sighted_zero_diff']} sighted, "
        f"{item4['disposition_on_record']} dispositioned, "
        f"**{item4['owed']} OWED** of "
        f"{item4['act2_3_registry_event_rows']} Act-2/3 event rows |",
        "",
        "## Inputs, pins and determinism",
        "",
        f"- Artifacts reopened and hashed: **{inputs['artifacts_consumed']:,}** "
        f"({inputs['artifact_bytes_consumed'] / 1e9:.2f} GB). Roll-up over the "
        f"sorted (campaign, seed, sha256) triples: "
        f"`{inputs['artifact_roll_up_sha256'][:16]}…`. Every per-artifact hash "
        "is committed beside this file as `s243-artifact-manifest.csv`.",
        f"- Retest classification log `{inputs['retest_log']['path']}` "
        f"(`{inputs['retest_log']['sha256'][:16]}…`), "
        f"{inputs['retest_log']['verdicts']} verdicts.",
        f"- Dispositions `{inputs['dispositions']['path']}` "
        f"(`{inputs['dispositions']['sha256'][:16]}…`): "
        f"{inputs['dispositions']['run_items']} exact run items and "
        f"{inputs['dispositions']['event_row_items']} event-row items, zero "
        "wildcards.",
        f"- Sim-side census `{inputs['prep_census']['path']}` "
        f"(`{inputs['prep_census']['sha256'][:16]}…`): "
        f"{inputs['prep_census']['rows']:,} scanned rows over "
        f"{inputs['prep_census']['seeds']:,} seeds, deepest floor "
        f"{inputs['prep_census']['max_floor']}.",
        "",
        "| Cohort | Role | Workers | Runs | Policy | Policy seed | Fork pin | "
        "Driver | Pipeline | Schema |",
        "|---|---|---:|---:|---|---:|---|---|---|---:|",
    ]
    for cohort in inputs["cohorts"]:
        prov = cohort["provenance"]
        lines.append(
            f"| {cohort['group']} | {cohort['role']} | {cohort['workers']} | "
            f"{cohort['runs']} | {prov.get('policy')} | "
            f"{prov.get('policy_seed')} | "
            f"`{str(prov.get('fork_jar_sha256'))[:8]}…` | "
            f"{prov.get('driver_version')} | {prov.get('pipeline_version')} | "
            f"{prov.get('schema_version')} |")
    lines += [
        "",
        "The fork pins deliberately differ across cohorts: the escape-window "
        "recaptures were taken under the two successive holds that closed that "
        "class, so collapsing them into one required aggregate pin would hide "
        "the very fact the recapture evidence exists to record.",
        "",
        "### Scripted-policy pins, as the capture headers carry them",
        "",
        "| Cohort | Policy | Policy cmd SHA-256 | Config | Config SHA-256 |",
        "|---|---|---|---|---|",
    ]
    for cohort in inputs["cohorts"]:
        for pin in cohort["capture_pins"]:
            lines.append(
                f"| {cohort['group']} | {pin['policy']} | "
                f"{short_hash(pin['policy_cmd_sha256'])} | "
                f"{pin['policy_config'] or 'n/a'} | "
                f"{short_hash(pin['policy_config_sha256'])} |")

    lines += [
        "",
        "## Item 1 — breadth",
        "",
        "> ≥ 2,000 distinct full-run A20 Ironclad oracle attempts under mixed "
        "policies (random-legal + survival/scripted external policies), zero "
        "untriaged findings, zero open dispositions.",
        "",
        f"- Distinct breadth seeds, counted from the artifacts themselves: "
        f"**{item1['distinct_breadth_seeds']:,}**; shortfall to 2,000: "
        f"**{item1['seed_shortfall_to_2000']}**.",
        f"- Full-run attempts — terminal outcome one of "
        f"`{'`, `'.join(item1['gameplay_terminal_outcomes'])}`: "
        f"**{item1['full_run_attempts']:,}**; shortfall to 2,000: "
        f"**{item1['full_run_shortfall_to_2000']}**. Attempts that reached no "
        f"gameplay terminal: **{len(item1['non_gameplay_terminal_runs'])}** — "
        "listed below, kept in the evidence inventory, never counted toward "
        "the 2,000.",
        f"- Policies: **{', '.join(item1['policies'])}**; distinct pinned "
        f"policy identities: **{', '.join(item1['policy_identities'])}**; "
        f"mixed-policy requirement met: "
        f"**{'YES' if item1['mixed_policies'] else 'NO'}**.",
        f"- Classification as captured: "
        f"**{counts(item1['classification_as_captured'])}**.",
        f"- Classification as read today, the retest sweep applied to "
        f"{item1['reclassified_by_retest']} runs: "
        f"**{counts(item1['classification_final'])}**.",
        f"- Captured actions across the breadth cohorts: "
        f"**{item1['captured_actions']:,}** (a visible diagnostic; there is no "
        "action quota in the S2-G2 bar). Replay-recognized capture-race "
        f"records: **{item1['capture_race_records']}** across "
        f"**{item1['capture_race_runs']}** runs.",
        f"- Untriaged findings: **{item1['untriaged_findings']}**. A finding "
        "is triaged only when an exact (campaign, seed, classification) "
        "disposition exists — there is no wildcard and no `other` bucket, and "
        "an artifact this tool cannot classify aborts the report.",
        f"- Open dispositions: **{item1['open_findings']}**. An `open-*` "
        "disposition is reviewed, but it is never acceptance.",
        f"- **Item 1: {verdict(item1['met'])}**",
        "",
    ]
    if item1["non_gameplay_terminal_runs"]:
        lines += [
            "| Campaign | Seed | Terminal outcome |",
            "|---|---|---|",
        ]
        for entry in item1["non_gameplay_terminal_runs"]:
            lines.append(f"| {entry['campaign_id']} | {entry['seed']} | "
                         f"`{entry['outcome']}` |")
        lines.append("")

    lines += [
        "### Divergence inventory — every consumed run whose current "
        "classification is not clean",
        "",
        "| Campaign | Seed | As captured | Today | Disposition | "
        "Named exactly | Recapture fork | Recapture races |",
        "|---|---|---|---|---|---|---|---:|",
    ]
    for finding in report["divergence_inventory"]:
        disposition = finding["disposition"]
        status = disposition["status"] if disposition else "**UNTRIAGED**"
        recapture = finding["superseding_recapture"]
        if recapture:
            named = (f"{recapture['campaign_id']} / {recapture['seed']} "
                     f"(`{recapture['artifact_sha256'][:12]}…`)")
            fork = f"`{str(recapture['fork_jar_sha256'])[:8]}…`"
            races = str(recapture["capture_race_records"])
        else:
            named = disposition["reference"] if disposition else ""
            fork = ""
            races = ""
        lines.append(
            f"| {finding['campaign_id']} | {finding['seed']} | "
            f"{finding['capture_classification']} | "
            f"{finding['classification']} | {status} | {named} | {fork} | "
            f"{races} |")
    lines += [
        "",
        "Every superseding recapture named above was itself reopened, hashed "
        "and re-read from this report's own evidence set, and its own current "
        "classification is clean — the tool refuses to render a supersession "
        "it cannot verify, and refuses one that names a different seed. The "
        "last column is that recapture's own count of replay-recognized "
        "capture-race records, printed rather than assumed: the class was "
        "closed in two rounds, and only the round taken under the endBattle "
        "settle-lag hold reaches zero.",
        "",
    ]
    stale = report["stale_dispositions"]
    if stale["runs"] or stale["event_rows"]:
        lines += [
            "**Dispositions no longer exercised** — the finding each one names "
            "is no longer non-clean (or the event row it covers is now "
            "sighted). Remove them on the next triage pass; they are reported "
            "rather than fatal so this dashboard still regenerates over "
            "improved inputs.",
            "",
        ]
        for key in stale["runs"]:
            lines.append(f"- run `{key[0]}` / `{key[1]}` / `{key[2]}`")
        for game_id in stale["event_rows"]:
            lines.append(f"- event row `{game_id}`")
        lines.append("")

    lines += [
        "## Item 2 — Act-2 depth",
        "",
        "> ≥ 1 zero-diff boss-reward claim **and boss-chest boss-relic pick** "
        "for every Act-2 registry BOSS row (both a take and at least one skip "
        "witnessed across the cohort), each followed by a zero-diff act-2→3 "
        "transition into a playable Act-3 floor.",
        "",
        f"- Clean runs that entered Act 2: "
        f"**{item2['act2_entering_clean_runs']}**; clean runs that fought an "
        f"Act-2 boss: **{item2['act2_boss_fight_clean_runs']}**.",
        "",
        "| Act-2 BOSS row | Boss fights | Boss-reward claims | Boss chests | "
        "Take cohort | Skip cohort | Act-2→3 transitions |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for boss in item2["registry_boss_rows"]:
        cell = item2["per_row"][boss]
        lines.append(
            f"| {boss} | {cell['boss_fight_runs']} | "
            f"{cell['boss_reward_claim_runs']} | {cell['boss_chest_runs']} | "
            f"{cell['take_cohort_runs']} | {cell['skip_cohort_runs']} | "
            f"{cell['onward_transition_runs']} |")
    lines += [
        "",
        "**Literal shortfalls.**",
        "",
        f"- Act-2 BOSS rows with a zero-diff boss-reward claim: "
        f"**{len(item2['rows_with_zero_diff_boss_reward_claim'])} of "
        f"{act2_total}**; missing: "
        f"**{missing(item2['registry_boss_rows'], item2['rows_with_zero_diff_boss_reward_claim'])}**.",
        f"- Act-2 BOSS rows with a zero-diff boss chest: "
        f"**{len(item2['rows_with_zero_diff_boss_chest'])} of "
        f"{act2_total}**; missing: "
        f"**{missing(item2['registry_boss_rows'], item2['rows_with_zero_diff_boss_chest'])}**.",
        f"- Boss-relic **take** witnessed for: "
        f"**{', '.join(item2['rows_with_take_witness']) or 'no row'}**.",
        f"- Boss-relic **skip** witnessed for: "
        f"**{', '.join(item2['rows_with_skip_witness']) or 'no row'}**.",
        f"- Zero-diff act-2→3 transition witnessed for: "
        f"**{', '.join(item2['rows_with_onward_transition']) or 'no row'}**.",
        f"- **Item 2: {verdict(item2['met'])}**",
        "",
        "The depth cohorts this bar needs do not exist yet. S2.43's breadth "
        "wave measured zero Act-2 boss fights across its whole cohort — the "
        "escalation number that opened S2.V2 — so these rows fill from S2.V2's "
        "scan output, and the accounting above then changes only in its "
        "numbers.",
        "",
        "## Item 3 — Act-3 depth",
        "",
        "> every Act-3 registry BOSS row witnessed killed zero-diff, and ≥ 3 "
        "completed A20 **double-boss** runs (both bosses in one run, gold "
        "settlement zero-diff, covering ≥ 2 distinct first-boss identities).",
        "",
        f"- Clean runs that entered Act 3: "
        f"**{item3['act3_entering_clean_runs']}**.",
        "",
        "| Act-3 BOSS row | Boss fights | Kills witnessed |",
        "|---|---:|---:|",
    ]
    for boss in item3["registry_boss_rows"]:
        cell = item3["per_row"][boss]
        lines.append(f"| {boss} | {cell['boss_fight_runs']} | "
                     f"{cell['kill_runs']} |")
    lines += [
        "",
        "**Literal shortfalls.**",
        "",
        f"- Act-3 BOSS rows witnessed killed zero-diff: "
        f"**{len(item3['rows_witnessed_killed'])} of {act3_total}**; missing: "
        f"**{missing(item3['registry_boss_rows'], item3['rows_witnessed_killed'])}**.",
        f"- Completed double-boss runs: **{item3['double_boss_run_count']}**; "
        f"shortfall to 3: **{item3['double_boss_shortfall_to_3']}**.",
        f"- Distinct first-boss identities across those runs: "
        f"**{len(item3['double_boss_first_boss_identities'])}**; shortfall to "
        f"2: **{item3['double_boss_identity_shortfall_to_2']}**.",
        f"- **Item 3: {verdict(item3['met'])}**",
        "",
        "**Instrument note, stated rather than hidden.** The campaign report's "
        "`boss_kill_acts` is a *set* of act numbers and structurally cannot "
        "express two kills in one act, so double-boss detection here is "
        "artifact-side: a run counts when its own records witness two distinct "
        "Act-3 `act_boss` identities. No Act-3 run exists in the consumed "
        "evidence, so that detector is **unexercised by live data**; its "
        "behaviour is pinned by synthetic fixtures in the tool's unit tests "
        "instead of being asserted here, and the column above is a measured "
        "zero rather than a hard-wired false.",
        "",
        "## Item 4 — event depth (the section 7.4 coverage join)",
        "",
        "> every Act-2/3 event row sighted in ≥ 1 zero-diff oracle run *or* "
        "carrying an explicit per-row disposition (directed capture or a "
        "recorded reachability argument) — no wildcard dispositions.",
        "",
        f"- Act-2/3 registry event rows — every row whose `conditions.acts` "
        f"includes act 2 or act 3: "
        f"**{item4['act2_3_registry_event_rows']}**.",
        f"- Sighted **in act 2 or 3** in a run whose current classification is "
        f"clean: **{item4['sighted_zero_diff']}**.",
        f"- Carrying an exact per-row disposition: "
        f"**{item4['disposition_on_record']}**.",
        f"- **OWED — neither sighted nor dispositioned: {item4['owed']}** "
        f"({', '.join(item4['owed_rows']) or 'none'}).",
        f"- **Item 4: {verdict(item4['met'])}**",
        "",
        "A sighting counts only when it happens *in* act 2 or 3: an Act-1 draw "
        "of a shrine or of a cross-act special witnesses the Act-1 list, which "
        "is not what this bar is about — hence the separate any-act column. "
        "The sim-side census column is the rare-event context S2.43's prep "
        "scan measured; it is reach evidence for scheduling directed captures, "
        "never a substitute for an oracle sighting.",
        "",
        "| ID | Row | game_id | Pool | Acts | Act-2/3 sightings | Any-act "
        "sightings | Sim census rows | Status |",
        "|---:|---|---|---|---|---:|---:|---:|---|",
    ]
    for row in item4["rows"]:
        acts = ",".join(str(act) for act in row["acts"])
        lines.append(
            f"| {row['id']} | {row['name']} | {row['game_id']} | {row['pool']} "
            f"| {acts} | {row['act2_3_sightings_clean']:,} | "
            f"{row['any_act_sightings_clean']:,} | "
            f"{row['sim_prep_census_rows']:,} | {row['status']} |")
    lines += [
        "",
        "The full join, including the witnessing capture for every sighted "
        "row, is committed beside this file as `s243-event-coverage.csv`.",
        "",
    ]
    return "\n".join(lines) + "\n"


def event_csv(report: dict[str, Any]) -> str:
    output = io.StringIO(newline="")
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow([
        "id", "name", "game_id", "pool", "acts", "act2_3_sightings_clean",
        "any_act_sightings_clean", "sim_prep_census_rows",
        "witness_campaign_id", "witness_seed", "disposition_status", "status",
    ])
    for row in report["item4_event_depth"]["rows"]:
        witness = row["witness"] or {}
        disposition = row["disposition"] or {}
        writer.writerow([
            row["id"], row["name"], row["game_id"], row["pool"],
            " ".join(str(act) for act in row["acts"]),
            row["act2_3_sightings_clean"], row["any_act_sightings_clean"],
            row["sim_prep_census_rows"],
            witness.get("campaign_id", ""), witness.get("seed", ""),
            disposition.get("status", ""), row["status"],
        ])
    return output.getvalue()


def artifact_csv(manifest) -> str:
    output = io.StringIO(newline="")
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow([
        "role", "group", "campaign_id", "seed", "artifact", "artifact_bytes",
        "sha256", "outcome", "capture_classification", "final_classification",
        "classification_source", "max_act",
    ])
    for entry in manifest:
        writer.writerow([
            entry["role"], entry["group"], entry["campaign_id"], entry["seed"],
            entry["artifact"], entry["artifact_bytes"], entry["sha256"],
            entry["outcome"], entry["capture_classification"],
            entry["final_classification"], entry["classification_source"],
            entry["max_act"],
        ])
    return output.getvalue()


def write_report(report: dict[str, Any], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    document = dict(report)
    manifest = document.pop("artifact_manifest")
    write_text_lf(out_dir / "s243-dashboard.json",
                  json.dumps(document, indent=2, sort_keys=True) + "\n")
    write_text_lf(out_dir / "s243-dashboard.md", markdown(report))
    write_text_lf(out_dir / "s243-event-coverage.csv", event_csv(report))
    write_text_lf(out_dir / "s243-artifact-manifest.csv",
                  artifact_csv(manifest))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--artifact-root", type=Path,
                        default=DEFAULT_ARTIFACT_ROOT)
    parser.add_argument("--breadth", action="append", dest="breadth")
    parser.add_argument("--recapture", action="append", dest="recapture")
    parser.add_argument("--retest-log", type=Path, default=DEFAULT_RETEST_LOG)
    parser.add_argument("--dispositions", type=Path,
                        default=Path(__file__).with_name(
                            "s243_dispositions.json"))
    parser.add_argument("--prep-census", type=Path,
                        default=DEFAULT_PREP_CENSUS)
    parser.add_argument("--registry", type=Path, default=REPO / "registry")
    parser.add_argument("--out-dir", type=Path,
                        default=REPO / "docs" / "verification")
    args = parser.parse_args(argv)
    try:
        report = aggregate(
            args.artifact_root,
            args.breadth or list(DEFAULT_BREADTH_GROUPS),
            args.recapture or list(DEFAULT_RECAPTURE_GROUPS),
            args.retest_log,
            args.dispositions,
            args.registry,
            args.prep_census,
        )
        write_report(report, args.out_dir)
    except ReportError as exc:
        print(f"s2 verification report error: {exc}", file=sys.stderr)
        return 2
    print(args.out_dir / "s243-dashboard.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
