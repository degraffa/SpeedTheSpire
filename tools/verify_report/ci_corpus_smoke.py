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


class CorpusError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_archive(archive_path: Path, manifest_path: Path) -> tuple[
        dict, dict[str, bytes]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != FORMAT:
        raise CorpusError("unsupported corpus manifest format")
    provenance = manifest.get("provenance")
    provenance_keys = {
        "schema_version", "driver_version", "pipeline_version",
        "fork_jar_sha256",
    }
    if not isinstance(provenance, dict) or set(provenance) != provenance_keys \
            or any(value in (None, "") for value in provenance.values()):
        raise CorpusError("corpus manifest provenance is incomplete")
    archive_bytes = archive_path.read_bytes()
    if sha256(archive_bytes) != manifest.get("archive_sha256"):
        raise CorpusError("corpus archive SHA-256 mismatch")
    entries = manifest.get("entries")
    if not isinstance(entries, list) or len(entries) != 50 or \
            manifest.get("entry_count") != 50:
        raise CorpusError("corpus must contain exactly 50 manifest entries")
    expected = {
        f"raw/{entry['seed']}.jsonl" for entry in entries
    } | {
        f"traces/{entry['seed']}.trace" for entry in entries
    }
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
    if len({entry["seed"] for entry in entries}) != 50:
        raise CorpusError("corpus seed identities are not distinct")
    for entry in entries:
        seed = entry["seed"]
        raw = members[f"raw/{seed}.jsonl"]
        trace = members[f"traces/{seed}.trace"]
        if sha256(raw) != entry["source_artifact_sha256"]:
            raise CorpusError(f"{seed}: source artifact SHA-256 mismatch")
        if sha256(trace) != entry["translated_trace_sha256"]:
            raise CorpusError(f"{seed}: translated trace SHA-256 mismatch")
        if len(trace) < 24:
            raise CorpusError(f"{seed}: translated trace header is short")
        magic, schema, state_size, records, seed_long = struct.unpack(
            "<4sIIIq", trace[:24])
        if (magic, schema, state_size, records, seed_long) != (
                b"STS0", entry["trace_schema"], entry["trace_state_size"],
                entry["trace_records"], entry["seed_long"]):
            raise CorpusError(f"{seed}: translated trace header drift")
    return manifest, members


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
        scratch: Path, inject: bool) -> int:
    manifest, members = validate_archive(archive, manifest_path)
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    raw_paths = []
    entries = manifest["entries"][:1] if inject else manifest["entries"]
    for entry in entries:
        path = scratch / f"{entry['seed']}.jsonl"
        path.write_bytes(members[f"raw/{entry['seed']}.jsonl"])
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
    args = parser.parse_args()
    try:
        return run(args.archive, args.manifest, args.replay_bin, args.scratch,
                   args.inject_divergence)
    except (CorpusError, OSError, json.JSONDecodeError, tarfile.TarError) as exc:
        print(f"oracle corpus error: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
