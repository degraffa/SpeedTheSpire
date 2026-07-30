#!/usr/bin/env python3
"""Build the deterministic compressed 50-seed oracle replay corpus."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import struct
import tarfile
from pathlib import Path
from typing import Any

from generate_report import (
    ReportError, capture_race_record_count, read_json, sha256_file,
    write_text_lf,
)

FORMAT = "STS-ORACLE-CI-CORPUS v1"
# The committed smoke corpus is a frozen B5.4 artifact, not a moving sample of
# whichever campaigns the latest dashboard aggregates. Keep its no-argument
# regeneration inputs pinned independently from generate_report's G7 defaults.
DEFAULT_CORPUS_CAMPAIGNS = (
    "b52_accept_locked_20260729_71000_71049",
    "b52_accept_20260729_70000_70049",
    "b53_full_act1_20260729",
)


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


def tar_bytes(entries: list[dict[str, Any]]) -> bytes:
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.PAX_FORMAT) as archive:
        members: list[tuple[str, Path]] = []
        for entry in entries:
            members.append((f"raw/{entry['seed']}.jsonl", entry["raw_path"]))
            members.append((f"traces/{entry['seed']}.trace", entry["trace_path"]))
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
    payload = tar_bytes(entries)
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-root", type=Path,
                        default=Path(r"D:\STS_BG_Mod\_oracle_data\campaigns"))
    parser.add_argument("--campaign", action="append", dest="campaigns")
    parser.add_argument("--count", type=int, default=50)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
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
