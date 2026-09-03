#!/usr/bin/env python3
"""Build the deterministic compressed oracle replay corpora.

Two corpora, two formats, one builder:

* `--act1` (the default, B5.4): 50 distinct classification-clean race-free
  Act-1 runs selected by a mechanical rule, `STS-ORACLE-CI-CORPUS v1`.
* `--three-act` (S2.46): a CURATED handful of Acts 1-3 captures,
  `STS-ORACLE-CI-CORPUS v2`.  The picks are named one by one below with the
  reason each is in the corpus, because there is no mechanical "first N" rule
  that would produce this set -- the point is to carry the *shapes* the S2
  depth wave exercised (the A20 double-boss handoff under both first-boss
  identities, an Act-3 boss kill, the Mind Bloom Act-1-boss re-fight, and the
  boss-relic skip axis), not a sample.

Three facts the S2 depth evidence forces on the v2 format, each of them a
thing the v1 format asserts and this corpus cannot:

1. **Provenance is per entry.**  The depth cohorts deliberately ran under two
   different fork pins (the escape-window settle-lag hold, then the
   SecretPortal playtime pin), and collapsing them into one aggregate pin
   would hide exactly the fact the evidence exists to record.
2. **A translated trace is optional.**  Two of the picks have none: one
   campaign's translation aborted on an Act-4 `Spire Heart` event id (out of
   S2 scope) and one wave was stopped mid-campaign by the driver before any
   postprocess ran.  Both captures still replay clean, which is what the CI
   corpus asserts, so the trace member is carried when it exists and its
   absence is recorded with a reason rather than quietly papered over.
3. **The campaign-time classification is not the current one.**  Two picks
   were classified non-clean by the postprocess that ran the day they were
   captured, and are clean on the landed engine -- they are the captures whose
   divergences drove the fixes.  So the v2 builder does not trust a stored
   verdict at all: it RE-REPLAYS every pick with a real `replay_run_diff`
   binary and refuses any capture that is not zero-diff to its run terminal
   with no capture-race records.  That is the same assertion CI then makes on
   every preset, which is the only verdict worth freezing.

* `--keys` (S3.23): the curated KEY corpus, `STS-ORACLE-CI-CORPUS v3`.  Same
  per-entry shape as v2 with two differences the keys evidence forces:

  1. **`key-race` records are allowed, and only that family.**  A key CLAIM
     does not write `Settings.has*Key`: `RewardItem.claimReward` queues an
     `ObtainKeyEffect` (RewardItem.java:317-333) that sets the flag 0.33
     wall-clock seconds later (ObtainKeyEffect.java:40-41), so every capture
     that claims a key necessarily contains records where the sim holds a bit
     the dump does not.  A key corpus that refused capture-race records could
     therefore contain no key claim at all.  Every OTHER family stays
     forbidden, and the key family's own classifier is narrow (main.cpp).
  2. **The corpus contract is about keys, not about act 3.**  It asserts that
     the emerald key is CLAIMED and the run then crosses into the next act,
     that both sapphire branches are present (the key taken, and the relic
     taken on the same chest), and that a key-not-taken CONTROL on the same
     seed is carried beside its keyed line -- which is what makes the
     burning-elite gate (`!Settings.hasEmeraldKey`, AbstractDungeon.java:543)
     a measured pair and not a matching coincidence.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import re
import struct
import subprocess
import tarfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from generate_report import (
    ReportError, capture_race_record_count, read_json, sha256_file,
    write_text_lf,
)

FORMAT = "STS-ORACLE-CI-CORPUS v1"
THREE_ACT_FORMAT = "STS-ORACLE-CI-CORPUS v2"
KEYS_FORMAT = "STS-ORACLE-CI-CORPUS v3"
# The committed smoke corpus is a frozen B5.4 artifact, not a moving sample of
# whichever campaigns the latest dashboard aggregates. Keep its no-argument
# regeneration inputs pinned independently from generate_report's G7 defaults.
DEFAULT_CORPUS_CAMPAIGNS = (
    "b52_accept_locked_20260729_71000_71049",
    "b52_accept_20260729_70000_70049",
    "b53_full_act1_20260729",
)


@dataclass(frozen=True)
class ThreeActPick:
    """One curated Acts 1-3 capture, with the reason it is in the corpus."""

    campaign_id: str
    seed: str
    role: str
    why: str


# Pinned exactly like DEFAULT_CORPUS_CAMPAIGNS above and for the same reason: a
# frozen CI corpus must not silently become a different set of runs because a
# later campaign wave landed. Adding a run here is a deliberate, reviewed edit.
DEFAULT_THREE_ACT_PICKS = (
    ThreeActPick(
        "s2v2_dbv_103509a.worker-001-of-001", "STS103509",
        "double-boss-victory",
        "A completed A20 double-boss run -- Donu and Deca, then Time Eater -- "
        "carrying the whole design section 6 item 3 shape end to end: the "
        "Act-2 boss chest, the act-2->3 transition, both Act-3 boss fights, "
        "the COMPLETE-screen handoff and the victory terminal."),
    ThreeActPick(
        "s2v2_db47_b.worker-001-of-001", "STS128113",
        "double-boss-victory",
        "The second first-boss identity the item-3 bar asks for: Time Eater "
        "first, then Donu and Deca, also a victory."),
    ThreeActPick(
        "s2v2_awk_105835.worker-001-of-001", "STS105835",
        "act3-boss-kill",
        "The Awakened One killed zero-diff, then a death to the second boss "
        "-- the item-3 kill witness and the LOSING double-boss shape, which "
        "no victory can exercise."),
    ThreeActPick(
        "s2v2_mb_102529.worker-001-of-001", "STS102529",
        "directed-event",
        "The Mind Bloom Act-1-boss re-fight (Guardian) the S2.33 deferred row "
        "asked for, played to the fixed 25/50 gold reward claim."),
    ThreeActPick(
        "s2v2_skip_b.worker-001-of-001", "STS111111",
        "boss-relic-skip",
        "The boss-relic SKIP policy axis into Act 3; every other pick runs "
        "the take config, and a corpus carrying only one side of that axis "
        "would freeze half the bar."),
)

@dataclass(frozen=True)
class KeysPick:
    """One curated key capture (S3.23), with the reason it is in the corpus."""

    campaign_id: str
    seed: str
    role: str
    why: str


# Pinned for the same reason as DEFAULT_THREE_ACT_PICKS above. Four captures
# from the S3.23 keys wave, chosen to carry every shape the S3.11 evidence
# bar names and nothing decorative.
DEFAULT_KEYS_PICKS = (
    KeysPick(
        "s323_STS507768_keys.worker-001-of-001", "STS507768",
        "emerald-pair-keys",
        "The emerald pair's KEYS half: EMERALD_KEY claimed at the Act-1 "
        "burning elite (floor 8), SAPPHIRE_KEY claimed at the floor-9 chest, "
        "a Recall campfire, and the run crossing into Act 2 -- whose map "
        "carries NO burning elite, because the key is held "
        "(AbstractDungeon.java:543)."),
    KeysPick(
        "s323_STS507768_ctrl.worker-001-of-001", "STS507768",
        "emerald-pair-control",
        "The SAME SEED with no key taken: the same floor-9 chest claims the "
        "RELIC instead (the sapphire relic branch, whose linked key row must "
        "die with it), and its Act-2 map DOES carry the burning-elite mark. "
        "The pair is trap 1's whole live content; a single keyed run that "
        "happened to match proves nothing."),
    KeysPick(
        "s323_STS506383_keys.worker-001-of-001", "STS506383",
        "three-act-all-keys",
        "The deepest keyed line: all three keys, the Act-2 boss chest, the "
        "act-2->3 transition, an Act-3 boss killed and the A20 double-boss "
        "COMPLETE handoff -- the only committed capture that carries the key "
        "state through a whole three-act run."),
    KeysPick(
        "s323_STS508459_keys.worker-001-of-001", "STS508459",
        "act2-emerald-claim",
        "The other side of the gate's timing: this line claims the emerald "
        "key in ACT 2 (floor 31), so its Act-2 map was generated with the key "
        "UNHELD and does carry a burning elite. Without it the corpus would "
        "only ever witness an Act-1 claim, and 380 of 380 of S3.22's emitted "
        "Black-Star lines were exactly that."),
)

# The take/skip axis is read from the policy config each capture's own campaign
# names -- never from a cohort directory name (the s243 dashboard's rule).
POLICY_AXIS_BY_CONFIG = {
    "policy_bossrelic_take.json": "take",
    "policy_bossrelic_skip.json": "skip",
    "follower_sim_search.json": "take",
    "follower_sim_search_skip.json": "skip",
    # S3.23's two follower configs: both drive a scripted line that TAKES the
    # boss relic; they differ only in which sim policy's script they replay.
    "follower_keys.json": "take",
    "follower_ctrl.json": "take",
}

REPLAY_CLEAN_RE = re.compile(
    r"^CLEAN\s.*?:\s(?P<records>\d+) records compared "
    r"\((?P<reward>\d+) on reward screens\), "
    r"(?P<library>\d+) library-order-only, "
    r"(?P<obtain>\d+) obtain-race, "
    r"(?P<escape>\d+) escape-race, "
    r"(?P<preview>\d+) preview-race, "
    r"(?P<key>\d+) key-race; stop: (?P<stop>.*)$")


def trace_header(path: Path) -> dict[str, int]:
    data = path.read_bytes()[:24]
    if len(data) != 24:
        raise ReportError(f"{path}: translated trace has a short header")
    magic, schema, state_size, records, seed_long = struct.unpack("<4sIIIq", data)
    if magic != b"STS0" or schema != 1 or state_size == 0 or records == 0:
        raise ReportError(f"{path}: invalid translated trace header")
    return {
        "trace_schema": schema,
        "trace_state_size": state_size,
        "trace_records": records,
        "seed_long": seed_long,
    }


def select(campaign_root: Path, campaign_ids: list[str],
           count: int) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    seen: set[str] = set()
    provenance: dict[str, Any] | None = None
    for campaign_id in campaign_ids:
        campaign_dir = campaign_root / campaign_id
        report = read_json(campaign_dir / "report.json")
        if report.get("report_format") != "STS-ORACLE-CAMPAIGN-REPORT v1":
            raise ReportError(f"{campaign_id}: unsupported campaign report format")
        if report.get("campaign_id") != campaign_id:
            raise ReportError(f"{campaign_id}: report campaign_id mismatch")
        if report.get("campaign_status") != "complete":
            raise ReportError(f"{campaign_id}: campaign is not complete")
        current = {
            "schema_version": report.get("schema_version"),
            "driver_version": report.get("driver_version"),
            "pipeline_version": report.get("pipeline_version"),
            "fork_jar_sha256": report.get("fork_jar_sha256"),
        }
        if provenance is None:
            provenance = current
        elif current != provenance:
            raise ReportError(f"{campaign_id}: corpus provenance mismatch")
        for run in report.get("runs", []):
            if len(entries) == count:
                break
            seed = str(run.get("seed", ""))
            if seed in seen or run.get("classification") != "clean" or \
                    capture_race_record_count(
                        run, f"{campaign_id}/{seed}") != 0:
                continue
            raw = campaign_dir / str(run["source_artifact"])
            trace = campaign_dir / str(run["trace"])
            if sha256_file(raw) != run.get("source_artifact_sha256"):
                raise ReportError(f"{campaign_id}/{seed}: raw artifact hash drift")
            if (campaign_dir / "diffs" / f"{seed}.status").read_text().strip() != "0":
                raise ReportError(f"{campaign_id}/{seed}: replay status is not clean")
            if not (campaign_dir / "diffs" / f"{seed}.log").read_text(
                    encoding="utf-8").lstrip().startswith("==="):
                raise ReportError(f"{campaign_id}/{seed}: malformed replay report")
            header = trace_header(trace)
            entries.append({
                "campaign_id": campaign_id,
                "seed": seed,
                "actions": int(run["actions"]),
                "source_artifact_sha256": sha256_file(raw),
                "translated_trace_sha256": sha256_file(trace),
                "raw_path": raw,
                "trace_path": trace,
                **header,
            })
            seen.add(seed)
    if len(entries) != count:
        raise ReportError(f"only {len(entries)} eligible clean seeds; need {count}")
    assert provenance is not None
    return entries, provenance


def tar_bytes(members: list[tuple[str, Path]]) -> bytes:
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for name, source in sorted(members):
            data = source.read_bytes()
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            info.mtime = 0
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            archive.addfile(info, io.BytesIO(data))
    compressed = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=compressed,
                       mtime=0, compresslevel=9) as stream:
        stream.write(raw.getvalue())
    return compressed.getvalue()


def build(campaign_root: Path, campaign_ids: list[str], count: int,
          archive_path: Path, manifest_path: Path) -> dict[str, Any]:
    entries, provenance = select(campaign_root, campaign_ids, count)
    members: list[tuple[str, Path]] = []
    for entry in entries:
        members.append((f"raw/{entry['seed']}.jsonl", entry["raw_path"]))
        members.append((f"traces/{entry['seed']}.trace", entry["trace_path"]))
    payload = tar_bytes(members)
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    archive_path.write_bytes(payload)
    manifest_entries = []
    for entry in entries:
        manifest_entries.append({
            key: entry[key] for key in (
                "campaign_id", "seed", "actions", "source_artifact_sha256",
                "translated_trace_sha256", "trace_schema", "trace_state_size",
                "trace_records", "seed_long")
        })
    manifest = {
        "format": FORMAT,
        "archive": archive_path.name,
        "archive_sha256": hashlib.sha256(payload).hexdigest(),
        "entry_count": len(entries),
        "provenance": provenance,
        "selection_policy": (
            "campaign CLI order, then B5.2 report run order; first distinct "
            "classification=clean run with zero known-race records"),
        "campaigns": campaign_ids,
        "entries": manifest_entries,
    }
    write_text_lf(
        manifest_path, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


# --------------------------------------------------------------------------
# S2.46: the curated three-act corpus (STS-ORACLE-CI-CORPUS v2)
# --------------------------------------------------------------------------

def campaign_provenance(source: dict[str, Any], label: str) -> dict[str, Any]:
    """The four pins, per entry. `pipeline_version` is absent by construction
    on a campaign the driver stopped before postprocess, so it is carried as
    null rather than invented; every other pin is required."""
    provenance = {
        "schema_version": source.get("schema_version"),
        "driver_version": source.get("driver_version"),
        "pipeline_version": source.get("pipeline_version"),
        "fork_jar_sha256": source.get("fork_jar_sha256"),
    }
    for key in ("schema_version", "driver_version", "fork_jar_sha256"):
        if provenance[key] in (None, ""):
            raise ReportError(f"{label}: campaign provenance has no {key}")
    return provenance


def policy_axis(source: dict[str, Any], label: str) -> tuple[str, Any, Any]:
    policy = str(source.get("policy", ""))
    external = source.get("external_policy") or {}
    config_path = str(external.get("config_path", ""))
    config = config_path.replace("\\", "/").rsplit("/", 1)[-1]
    if policy == "random-legal" and not config:
        return "random-legal", None, None
    axis = POLICY_AXIS_BY_CONFIG.get(config)
    if axis is None:
        raise ReportError(
            f"{label}: policy config {config!r} names no known boss-relic "
            f"take/skip axis")
    return axis, config, external.get("config_sha256")


def replay_verdict(replay_bin: Path, artifact: Path, label: str,
                   allow_key_race: bool = False) -> dict[str, Any]:
    """Re-replay one capture and require zero-diff to its run terminal.

    This is the v2 corpus's selection gate. A stored `classification` is the
    verdict of the postprocess that ran on capture day; two of the curated
    picks were non-clean then and are clean on the landed engine, so the only
    verdict worth freezing is the one a real binary produces now -- which is
    also exactly what `ci_corpus_smoke.py` asserts in CI afterwards.
    """
    process = subprocess.run(
        [str(replay_bin), str(artifact), "--replay", "--stop-on-diff"],
        check=False, capture_output=True, text=True)
    if process.returncode != 0:
        raise ReportError(
            f"{label}: replay_run_diff exited {process.returncode}; a corpus "
            f"member must replay zero-diff\n{process.stdout}{process.stderr}")
    matches = [REPLAY_CLEAN_RE.match(line.strip())
               for line in process.stdout.splitlines()]
    clean = [match for match in matches if match is not None]
    if len(clean) != 1:
        raise ReportError(
            f"{label}: expected exactly one CLEAN replay summary line, "
            f"found {len(clean)}")
    fields = clean[0].groupdict()
    kinds = ("obtain", "escape", "preview")
    races = {kind: int(fields[kind]) for kind in kinds}
    if any(races.values()):
        raise ReportError(
            f"{label}: replay recognised capture-race records {races}; the "
            f"corpus takes race-free captures only")
    key_race = int(fields["key"])
    # The keys corpus is the ONE place a capture-race family is admitted, and
    # only this one: an ObtainKeyEffect record is the unavoidable shadow of the
    # very claim the corpus exists to freeze (module docstring).
    if key_race and not allow_key_race:
        raise ReportError(
            f"{label}: replay recognised {key_race} key-race record(s); only "
            f"the keys corpus admits that family")
    stop = fields["stop"].strip()
    if stop != "run terminal":
        raise ReportError(
            f"{label}: replay stopped at {stop!r}, not the run terminal")
    return {
        "records_compared": int(fields["records"]),
        "reward_screen_records": int(fields["reward"]),
        "library_order_only_records": int(fields["library"]),
        "key_race_records": key_race,
        "stop": stop,
        "verdict": "CLEAN",
    }


def key_facts(raw: Path) -> dict[str, Any]:
    """The key-shaped facts a v3 entry freezes, read off the capture itself.

    Everything here comes from the fork's own oracle block and screen dumps,
    never from a policy name: `oracle.has*Key` (S3.21 (a)), the reward-row list
    the command at that record answered, and each act's map nodes with the
    `has_emerald_key` mark (S3.21 (3)). A claim is recognised as "the record
    whose reward list holds the row AND whose command chooses that row's
    index", which is what makes `sapphire_branch` say KEY vs RELIC rather than
    guessing from the outcome.
    """
    keys_final = {"ruby": False, "emerald": False, "sapphire": False}
    emerald_claim_act: int | None = None
    emerald_claim_floor: int | None = None
    sapphire_branch: str | None = None
    marked_acts: set[int] = set()
    cross_acts: set[int] = set()
    with raw.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("record_kind") != "action":
                continue
            state = record.get("state_json") or {}
            game = state.get("game_state") or {}
            # The fork's oracle block lives INSIDE `game_state`; the top-level
            # fallback keeps this readable against an older dump shape.
            oracle = game.get("oracle") or state.get("oracle") or {}
            for name, field in (("ruby", "hasRubyKey"),
                                ("emerald", "hasEmeraldKey"),
                                ("sapphire", "hasSapphireKey")):
                if oracle.get(field):
                    keys_final[name] = True
            act = game.get("act")
            if isinstance(act, int):
                cross_acts.add(act)
                for node in game.get("map") or []:
                    if node.get("has_emerald_key"):
                        marked_acts.add(act)
            rows = ((game.get("screen_state") or {}).get("rewards")) or []
            command = str(record.get("action_command") or "").split()
            if len(command) != 2 or command[0] != "choose":
                continue
            try:
                index = int(command[1])
            except ValueError:
                continue
            if not 0 <= index < len(rows):
                continue
            chosen = str((rows[index] or {}).get("reward_type") or "")
            offered = {str((row or {}).get("reward_type") or "") for row in rows}
            if chosen == "EMERALD_KEY" and emerald_claim_act is None:
                emerald_claim_act = act
                emerald_claim_floor = game.get("floor")
            if "SAPPHIRE_KEY" in offered and sapphire_branch is None:
                if chosen == "SAPPHIRE_KEY":
                    sapphire_branch = "key"
                elif chosen == "RELIC":
                    sapphire_branch = "relic"
    return {
        "keys_final": keys_final,
        "all_three_keys": all(keys_final.values()),
        "emerald_claim_act": emerald_claim_act,
        "emerald_claim_floor": emerald_claim_floor,
        "emerald_claim_crossed_act": (
            emerald_claim_act is not None
            and max(cross_acts) > emerald_claim_act),
        "sapphire_branch": sapphire_branch,
        "emerald_marked_acts": sorted(marked_acts),
    }


def three_act_entry(campaign_root: Path, pick: ThreeActPick,
                    replay_bin: Path, *, require_act3: bool = True,
                    allow_key_race: bool = False) -> dict[str, Any]:
    from generate_s2_report import scan_artifact  # noqa: PLC0415

    label = f"{pick.campaign_id}/{pick.seed}"
    campaign_dir = campaign_root / pick.campaign_id
    # The worker's progress file is the one input EVERY campaign has, complete
    # or driver-stopped, and it is where the SHA-pinned external-policy
    # identity lives -- `report.json` records only the policy's name.
    progress = read_json(campaign_dir / "campaign_progress.json")
    if progress.get("campaign_id") != pick.campaign_id:
        raise ReportError(f"{label}: progress campaign_id mismatch")
    report_path = campaign_dir / "report.json"
    report = read_json(report_path) if report_path.is_file() else None
    if report is not None:
        if report.get("report_format") != "STS-ORACLE-CAMPAIGN-REPORT v1":
            raise ReportError(f"{label}: unsupported campaign report format")
        if report.get("campaign_id") != pick.campaign_id:
            raise ReportError(f"{label}: report campaign_id mismatch")
        source = report
        rows = report.get("runs", [])
        row_source = "campaign report"
    else:
        source = progress
        rows = source.get("seeds_done", [])
        row_source = "campaign progress"
    if (source.get("policy"), source.get("policy_seed")) != (
            progress.get("policy"), progress.get("policy_seed")):
        raise ReportError(f"{label}: policy identity disagrees across inputs")
    matches = [row for row in rows if str(row.get("seed")) == pick.seed]
    if len(matches) != 1:
        raise ReportError(
            f"{label}: {len(matches)} run rows name this seed; need exactly 1")
    run = matches[0]

    artifact_name = str(run.get("source_artifact") or run.get("artifact") or "")
    if not artifact_name:
        raise ReportError(f"{label}: run row names no capture artifact")
    raw = campaign_dir / artifact_name
    if not raw.is_file():
        raise ReportError(f"{label}: capture {artifact_name} is missing")
    declared = run.get("source_artifact_sha256")
    digest = sha256_file(raw)
    if declared is not None and declared != digest:
        raise ReportError(f"{label}: raw artifact hash drift")
    if capture_race_record_count(run, label) != 0:
        raise ReportError(f"{label}: campaign recorded capture-race records")

    scan = scan_artifact(raw)
    if scan["sha256"] != digest:
        raise ReportError(f"{label}: artifact hash disagrees with its scan")
    if require_act3 and scan["artifact_max_act"] != 3:
        raise ReportError(
            f"{label}: capture reaches act {scan['artifact_max_act']}; the "
            f"three-act corpus takes act-3 captures only")
    act3 = list(scan["act_bosses"].get(3) or [])
    victory = bool(run.get("victory"))

    stem = f"{pick.campaign_id.split('.worker-')[0]}__{pick.seed}"
    trace_member: str | None = None
    trace_path: Path | None = None
    trace_absent: str | None = None
    trace_fields: dict[str, Any] = {
        "translated_trace_sha256": None, "trace_schema": None,
        "trace_state_size": None, "trace_records": None, "seed_long": None,
    }
    if report is None:
        trace_absent = (
            f"the driver stopped this campaign mid-seed "
            f"(status {source.get('status')!r}), so no postprocess ran and no "
            f"trace was translated")
    elif int(run.get("translation_exit", 0)) != 0:
        trace_absent = (
            f"the campaign's translation exited "
            f"{int(run['translation_exit'])}; the capture carries a record "
            f"the translator refuses (its campaign diffs log names it) while "
            f"the capture itself replays zero-diff")
    else:
        trace_path = campaign_dir / str(run["trace"])
        if not trace_path.is_file():
            raise ReportError(f"{label}: translated trace is missing")
        trace_member = f"traces/{stem}.trace"
        trace_fields = {
            "translated_trace_sha256": sha256_file(trace_path),
            **trace_header(trace_path),
        }

    axis, config, config_sha = policy_axis(progress, label)
    return {
        "campaign_id": pick.campaign_id,
        "cohort": pick.campaign_id.split(".worker-")[0],
        "seed": pick.seed,
        "role": pick.role,
        "why": pick.why,
        "member": f"raw/{stem}.jsonl",
        "trace_member": trace_member,
        "trace_absent_reason": trace_absent,
        "actions": int(run["actions"]),
        "floor": int(run["floor"]),
        "outcome": str(run["outcome"]),
        "victory": victory,
        "max_act": scan["artifact_max_act"],
        "act3_boss_identities": act3,
        "double_boss": len(act3) >= 2,
        "completed_double_boss": len(act3) >= 2 and victory,
        "source_artifact_sha256": digest,
        "campaign_classification": run.get("classification"),
        "campaign_status": str(source.get("campaign_status")
                               or source.get("status")),
        "run_row_source": row_source,
        "provenance": campaign_provenance(source, label),
        "policy": str(source.get("policy", "")),
        "policy_seed": source.get("policy_seed"),
        "policy_axis": axis,
        "policy_config": config,
        "policy_config_sha256": config_sha,
        "replay": replay_verdict(replay_bin, raw, label, allow_key_race),
        "keys": key_facts(raw),
        "raw_path": raw,
        "trace_path": trace_path,
        **trace_fields,
    }


def build_three_act(campaign_root: Path, picks: list[ThreeActPick],
                    replay_bin: Path, archive_path: Path,
                    manifest_path: Path) -> dict[str, Any]:
    entries = [three_act_entry(campaign_root, pick, replay_bin)
               for pick in picks]
    members: list[tuple[str, Path]] = []
    for entry in entries:
        members.append((entry["member"], entry["raw_path"]))
        if entry["trace_member"] is not None:
            members.append((entry["trace_member"], entry["trace_path"]))
    if len({name for name, _ in members}) != len(members):
        raise ReportError("three-act corpus members are not distinct")
    if not any(entry["completed_double_boss"] for entry in entries):
        raise ReportError(
            "the three-act corpus must carry at least one COMPLETED A20 "
            "double-boss run")
    axes = {entry["policy_axis"] for entry in entries}
    if not {"take", "skip"} <= axes:
        raise ReportError(
            f"the three-act corpus must carry both boss-relic axes; it has "
            f"{sorted(axes)}")
    payload = tar_bytes(members)
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    archive_path.write_bytes(payload)
    manifest = {
        "format": THREE_ACT_FORMAT,
        "archive": archive_path.name,
        "archive_sha256": hashlib.sha256(payload).hexdigest(),
        "entry_count": len(entries),
        "selection_policy": (
            "curated: the pinned DEFAULT_THREE_ACT_PICKS list in "
            "tools/verify_report/build_ci_corpus.py, in that order, each "
            "carrying its own reason; every pick is re-replayed at build time "
            "and must be zero-diff to its run terminal with zero capture-race "
            "records"),
        "replay_verification": "replay_run_diff <capture> --replay --stop-on-diff",
        "entries": [
            {key: value for key, value in entry.items()
             if key not in ("raw_path", "trace_path")}
            for entry in entries
        ],
    }
    write_text_lf(
        manifest_path, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


def build_keys(campaign_root: Path, picks: list[KeysPick], replay_bin: Path,
               archive_path: Path, manifest_path: Path) -> dict[str, Any]:
    """The S3.23 curated KEY corpus (v3). Same members as v2, key contract."""
    entries = [
        three_act_entry(campaign_root,
                        ThreeActPick(pick.campaign_id, pick.seed, pick.role,
                                     pick.why),
                        replay_bin, require_act3=False, allow_key_race=True)
        for pick in picks
    ]
    members: list[tuple[str, Path]] = []
    for entry in entries:
        members.append((entry["member"], entry["raw_path"]))
        if entry["trace_member"] is not None:
            members.append((entry["trace_member"], entry["trace_path"]))
    if len({name for name, _ in members}) != len(members):
        raise ReportError("keys corpus members are not distinct")
    # The contract, asserted here and again in CI (ci_corpus_smoke.py).
    if not any(entry["keys"]["emerald_claim_crossed_act"] for entry in entries):
        raise ReportError(
            "the keys corpus must carry an EMERALD_KEY claim whose run then "
            "crosses into the next act -- the claim record alone does not "
            "witness AbstractDungeon.java:543")
    branches = {entry["keys"]["sapphire_branch"] for entry in entries}
    if not {"key", "relic"} <= branches:
        raise ReportError(
            f"the keys corpus must carry BOTH sapphire branches; it has "
            f"{sorted(b for b in branches if b)}")
    if not any(entry["keys"]["all_three_keys"] for entry in entries):
        raise ReportError("the keys corpus carries no all-three-keys run")
    # The PAIR: one seed, a keyed line and a key-not-taken control, whose
    # burning-elite marks differ. Without it the gate is matched, not measured.
    by_seed: dict[str, list[dict[str, Any]]] = {}
    for entry in entries:
        by_seed.setdefault(str(entry["seed"]), []).append(entry)
    paired = [
        seed for seed, group in by_seed.items()
        if any(e["keys"]["emerald_claim_act"] is not None for e in group)
        and any(e["keys"]["emerald_claim_act"] is None for e in group)
        and len({tuple(e["keys"]["emerald_marked_acts"]) for e in group}) > 1
    ]
    if not paired:
        raise ReportError(
            "the keys corpus must carry a same-seed keyed/control PAIR whose "
            "burning-elite marks differ")
    payload = tar_bytes(members)
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    archive_path.write_bytes(payload)
    manifest = {
        "format": KEYS_FORMAT,
        "archive": archive_path.name,
        "archive_sha256": hashlib.sha256(payload).hexdigest(),
        "entry_count": len(entries),
        "emerald_pair_seeds": sorted(paired),
        "selection_policy": (
            "curated: the pinned DEFAULT_KEYS_PICKS list in "
            "tools/verify_report/build_ci_corpus.py, in that order, each "
            "carrying its own reason; every pick is re-replayed at build time "
            "and must be zero-diff to its run terminal, with no capture-race "
            "records other than the ObtainKeyEffect key-race family"),
        "replay_verification": "replay_run_diff <capture> --replay --stop-on-diff",
        "entries": [
            {key: value for key, value in entry.items()
             if key not in ("raw_path", "trace_path")}
            for entry in entries
        ],
    }
    write_text_lf(
        manifest_path, json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", type=Path,
                        default=Path(r"D:\STS_BG_Mod\_oracle_data\campaigns"))
    parser.add_argument("--campaign", action="append", dest="campaigns")
    parser.add_argument("--count", type=int, default=50)
    parser.add_argument("--three-act", action="store_true",
                        help="build the S2.46 curated Acts 1-3 corpus (v2)")
    parser.add_argument("--keys", action="store_true",
                        help="build the S3.23 curated key corpus (v3)")
    parser.add_argument("--replay-bin", type=Path,
                        help="replay_run_diff, required by --three-act/--keys")
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.three_act and args.keys:
            raise ReportError("--three-act and --keys are exclusive")
        if args.keys:
            if args.replay_bin is None:
                raise ReportError("--keys needs --replay-bin")
            manifest = build_keys(
                args.artifact_root, list(DEFAULT_KEYS_PICKS),
                args.replay_bin, args.archive, args.manifest)
        elif args.three_act:
            if args.replay_bin is None:
                raise ReportError("--three-act needs --replay-bin")
            manifest = build_three_act(
                args.artifact_root, list(DEFAULT_THREE_ACT_PICKS),
                args.replay_bin, args.archive, args.manifest)
        else:
            manifest = build(args.artifact_root,
                             args.campaigns or list(DEFAULT_CORPUS_CAMPAIGNS),
                             args.count, args.archive, args.manifest)
    except ReportError as exc:
        print(f"corpus build error: {exc}")
        return 2
    print(f"{args.archive}: {manifest['entry_count']} clean seeds, "
          f"sha256={manifest['archive_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
