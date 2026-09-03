#!/usr/bin/env python3
"""Integrity-check, extract, and replay the committed oracle CI corpus."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import shutil
import struct
import subprocess
import tarfile
from pathlib import Path

FORMAT = "STS-ORACLE-CI-CORPUS v1"
THREE_ACT_FORMAT = "STS-ORACLE-CI-CORPUS v2"
KEYS_FORMAT = "STS-ORACLE-CI-CORPUS v3"
PROVENANCE_KEYS = {
    "schema_version", "driver_version", "pipeline_version", "fork_jar_sha256",
}


class CorpusError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def check_trace_header(label: str, trace: bytes, entry: dict) -> None:
    if len(trace) < 24:
        raise CorpusError(f"{label}: translated trace header is short")
    magic, schema, state_size, records, seed_long = struct.unpack(
        "<4sIIIq", trace[:24])
    if (magic, schema, state_size, records, seed_long) != (
            b"STS0", entry["trace_schema"], entry["trace_state_size"],
            entry["trace_records"], entry["seed_long"]):
        raise CorpusError(f"{label}: translated trace header drift")


def extract(archive_path: Path, expected: set[str]) -> dict[str, bytes]:
    members: dict[str, bytes] = {}
    with tarfile.open(archive_path, mode="r:gz") as archive:
        for item in archive:
            if not item.isfile() or item.name not in expected or \
                    item.name.startswith("/") or ".." in Path(item.name).parts:
                raise CorpusError(f"unexpected or unsafe archive member {item.name!r}")
            extracted = archive.extractfile(item)
            if extracted is None:
                raise CorpusError(f"cannot read archive member {item.name}")
            members[item.name] = extracted.read()
    if set(members) != expected:
        raise CorpusError("archive member set differs from the manifest")
    return members


def validate_v1(manifest: dict, archive_path: Path,
                expect_entries: int) -> dict[str, bytes]:
    provenance = manifest.get("provenance")
    if not isinstance(provenance, dict) or set(provenance) != PROVENANCE_KEYS \
            or any(value in (None, "") for value in provenance.values()):
        raise CorpusError("corpus manifest provenance is incomplete")
    entries = manifest.get("entries")
    if not isinstance(entries, list) or len(entries) != expect_entries or \
            manifest.get("entry_count") != expect_entries:
        raise CorpusError(
            f"corpus must contain exactly {expect_entries} manifest entries")
    expected = {
        f"raw/{entry['seed']}.jsonl" for entry in entries
    } | {
        f"traces/{entry['seed']}.trace" for entry in entries
    }
    members = extract(archive_path, expected)
    if len({entry["seed"] for entry in entries}) != expect_entries:
        raise CorpusError("corpus seed identities are not distinct")
    for entry in entries:
        seed = entry["seed"]
        raw = members[f"raw/{seed}.jsonl"]
        trace = members[f"traces/{seed}.trace"]
        if sha256(raw) != entry["source_artifact_sha256"]:
            raise CorpusError(f"{seed}: source artifact SHA-256 mismatch")
        if sha256(trace) != entry["translated_trace_sha256"]:
            raise CorpusError(f"{seed}: translated trace SHA-256 mismatch")
        check_trace_header(seed, trace, entry)
    return members


def validate_v3(manifest: dict, archive_path: Path,
                expect_entries: int) -> dict[str, bytes]:
    """The S3.23 curated KEY corpus.

    Everything the v2 walk checks about members, hashes and provenance -- but
    the act-3 requirement is dropped (a key run need not be deep) and the three
    contract assertions are about KEYS instead: an EMERALD_KEY claim whose run
    then crosses into the next act, BOTH sapphire branches, and a same-seed
    keyed/control pair whose burning-elite marks differ. A corpus that
    silently lost its control half would still replay clean and would stop
    being evidence for AbstractDungeon.java:543.
    """
    members = validate_v2(manifest, archive_path, expect_entries,
                          require_act3=False, require_double_boss=False)
    entries = manifest["entries"]
    keys = [entry.get("keys") or {} for entry in entries]
    if not any(fact.get("emerald_claim_crossed_act") for fact in keys):
        raise CorpusError(
            "keys corpus carries no EMERALD_KEY claim that crosses an act")
    branches = {fact.get("sapphire_branch") for fact in keys}
    if not {"key", "relic"} <= branches:
        raise CorpusError(
            f"keys corpus carries only the "
            f"{sorted(b for b in branches if b)} sapphire branch")
    if not any(fact.get("all_three_keys") for fact in keys):
        raise CorpusError("keys corpus carries no all-three-keys run")
    by_seed: dict[str, list[dict]] = {}
    for entry, fact in zip(entries, keys):
        by_seed.setdefault(str(entry.get("seed")), []).append(fact)
    paired = any(
        any(f.get("emerald_claim_act") is not None for f in group)
        and any(f.get("emerald_claim_act") is None for f in group)
        and len({tuple(f.get("emerald_marked_acts") or []) for f in group}) > 1
        for group in by_seed.values())
    if not paired:
        raise CorpusError(
            "keys corpus carries no same-seed keyed/control pair whose "
            "burning-elite marks differ")
    return members


def validate_v2(manifest: dict, archive_path: Path, expect_entries: int,
                require_act3: bool = True,
                require_double_boss: bool = True) -> dict[str, bytes]:
    """The S2.46 curated Acts 1-3 corpus.

    Everything v1 checks, plus the three things this corpus exists to freeze:
    every member really is an act-3 capture, at least one of them is a
    COMPLETED A20 double-boss run, and both boss-relic policy axes are
    present. Those are contract assertions, not statistics -- a curated corpus
    that silently lost its double-boss run would still replay clean and would
    no longer be the evidence the gate cites.
    """
    entries = manifest.get("entries")
    if not isinstance(entries, list) or len(entries) != expect_entries or \
            manifest.get("entry_count") != expect_entries:
        raise CorpusError(
            f"corpus must contain exactly {expect_entries} manifest entries")
    expected: set[str] = set()
    for entry in entries:
        label = f"{entry.get('cohort')}/{entry.get('seed')}"
        provenance = entry.get("provenance")
        if not isinstance(provenance, dict) or \
                set(provenance) != PROVENANCE_KEYS:
            raise CorpusError(f"{label}: entry provenance is incomplete")
        for key in ("schema_version", "driver_version", "fork_jar_sha256"):
            if provenance[key] in (None, ""):
                raise CorpusError(f"{label}: entry provenance has no {key}")
        # `pipeline_version` is the one pin a driver-stopped campaign cannot
        # have: it is written by the postprocess that never ran. Null is legal
        # there and nowhere else.
        if provenance["pipeline_version"] in (None, "") and \
                entry.get("campaign_status") == "complete":
            raise CorpusError(f"{label}: a complete campaign has no pipeline_version")
        if require_act3 and entry.get("max_act") != 3:
            raise CorpusError(f"{label}: not an act-3 capture")
        if entry.get("trace_member") is None and \
                not str(entry.get("trace_absent_reason") or "").strip():
            raise CorpusError(f"{label}: a missing trace needs a stated reason")
        expected.add(str(entry["member"]))
        if entry.get("trace_member") is not None:
            expected.add(str(entry["trace_member"]))
    if len(expected) != sum(
            1 + (entry.get("trace_member") is not None) for entry in entries):
        raise CorpusError("corpus member names are not distinct")
    members = extract(archive_path, expected)
    for entry in entries:
        label = f"{entry.get('cohort')}/{entry.get('seed')}"
        raw = members[str(entry["member"])]
        if sha256(raw) != entry["source_artifact_sha256"]:
            raise CorpusError(f"{label}: source artifact SHA-256 mismatch")
        if entry.get("trace_member") is not None:
            trace = members[str(entry["trace_member"])]
            if sha256(trace) != entry["translated_trace_sha256"]:
                raise CorpusError(f"{label}: translated trace SHA-256 mismatch")
            check_trace_header(label, trace, entry)
    if require_double_boss and not any(
            entry.get("completed_double_boss") for entry in entries):
        raise CorpusError("corpus carries no completed A20 double-boss run")
    axes = {entry.get("policy_axis") for entry in entries}
    if require_double_boss and not {"take", "skip"} <= axes:
        raise CorpusError(
            f"corpus carries only the {sorted(a for a in axes if a)} "
            f"boss-relic axis")
    return members


def validate_archive(archive_path: Path, manifest_path: Path,
                     expect_entries: int = 50) -> tuple[dict, dict[str, bytes]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    archive_bytes = archive_path.read_bytes()
    if sha256(archive_bytes) != manifest.get("archive_sha256"):
        raise CorpusError("corpus archive SHA-256 mismatch")
    fmt = manifest.get("format")
    if fmt == FORMAT:
        return manifest, validate_v1(manifest, archive_path, expect_entries)
    if fmt == THREE_ACT_FORMAT:
        return manifest, validate_v2(manifest, archive_path, expect_entries)
    if fmt == KEYS_FORMAT:
        return manifest, validate_v3(manifest, archive_path, expect_entries)
    raise CorpusError("unsupported corpus manifest format")


def raw_member_name(manifest: dict, entry: dict) -> str:
    if manifest.get("format") in (THREE_ACT_FORMAT, KEYS_FORMAT):
        return str(entry["member"])
    return f"raw/{entry['seed']}.jsonl"


def inject_divergence(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        record = json.loads(line)
        if record.get("record_kind") != "action":
            continue
        game = record["state_json"]["game_state"]
        game["current_hp"] = int(game["current_hp"]) + 1
        lines[index] = json.dumps(record, separators=(",", ":"), sort_keys=True)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return
    raise CorpusError("cannot inject divergence: no action record")


def run(archive: Path, manifest_path: Path, replay_bin: Path,
        scratch: Path, inject: bool, expect_entries: int = 50) -> int:
    manifest, members = validate_archive(archive, manifest_path, expect_entries)
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    raw_paths = []
    entries = manifest["entries"][:1] if inject else manifest["entries"]
    for entry in entries:
        member = raw_member_name(manifest, entry)
        path = scratch / Path(member).name
        path.write_bytes(members[member])
        raw_paths.append(path)
    if inject:
        inject_divergence(raw_paths[0])
    def replay_one(path: Path) -> subprocess.CompletedProcess:
        return subprocess.run(
            [str(replay_bin), str(path), "--replay", "--stop-on-diff"],
            check=False, capture_output=True, text=True)

    # Whole-run replay is independent per seed. Four bounded workers keep the
    # sanitizer smoke in the design's seconds-scale budget without exploding
    # process count inside ctest's own parallel matrix.
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(replay_one, raw_paths))
    failed = [result for result in results if result.returncode != 0]
    for result in failed:
        print(result.stdout, end="")
        print(result.stderr, end="")
    return 0 if not failed else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--replay-bin", type=Path, required=True)
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--inject-divergence", action="store_true")
    parser.add_argument(
        "--expect-entries", type=int, default=50,
        help="the corpus's committed entry count, asserted rather than read "
             "off the manifest it is checking")
    args = parser.parse_args()
    try:
        return run(args.archive, args.manifest, args.replay_bin, args.scratch,
                   args.inject_divergence, args.expect_entries)
    except (CorpusError, OSError, json.JSONDecodeError, tarfile.TarError) as exc:
        print(f"oracle corpus error: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
