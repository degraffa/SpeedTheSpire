#!/usr/bin/env python3
"""Validate B1.4 campaign artifacts against the PROTOCOL.md schema (Stage B).

The B1.4 acceptance clause "artifacts validate against the PROTOCOL.md schema"
is enforced here. Extends echo_driver.py's `--verify` primitive to the design
2.7 record shapes:

  * the first line is a `header` record carrying the required provenance
    (fork-jar hash, seed in both encodings, versions);
  * every `action` record's `state_json` parses and carries the PROTOCOL.md 3.1
    status keys, the 3.2 game_state anchors, and (when oracle_block_enabled) the
    fork's 5-block `oracle` state (PROTOCOL.md 5) with the 14 RNG streams;
  * the file ends with a `terminal` record.

Runs standalone (no game), stdlib-only. Exit 0 iff every file is clean.

Usage:
  python validate_artifacts.py <run.jsonl> [more.jsonl ...]
  python validate_artifacts.py --campaign <data-root>/<campaign-id>
  python validate_artifacts.py --require-oracle --campaign <campaign-dir>
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import sys

HEADER_KEYS = {"record_kind", "schema_version", "driver_version", "created_utc",
               "game", "mods", "fork_jar_sha256", "oracle_block_enabled",
               "seed", "ascension", "character", "policy"}
SEED_KEYS = {"string", "long"}
STATUS_KEYS = {"available_commands", "ready_for_command", "in_game"}
GS_ANCHORS = {"seed", "floor", "act", "screen_type", "class", "current_hp",
              "max_hp"}
ORACLE_KEYS = {"seed", "floor", "act", "ascension", "streams",
               "cardBlizzRandomizer", "blizzardPotionMod", "eventPity",
               "purgeCost", "eventList", "shrineList",
               "specialOneTimeEventList", "relicPools"}
RUN_STREAMS = {"monsterRng", "eventRng", "merchantRng", "cardRng", "treasureRng",
               "relicRng", "potionRng", "monsterHpRng", "aiRng", "shuffleRng",
               "cardRandomRng", "miscRng", "mapRng"}
REWARD_ORACLE_FIELDS = {"cardBlizzRandomizer", "blizzardPotionMod"}
REWARD_STREAMS = {"cardRng", "treasureRng", "potionRng", "relicRng", "miscRng"}
STRICT_PROGRESS_KEYS = {
    "campaign_id", "schema_version", "driver_version", "fork_jar_sha256",
    "policy", "seed_list", "status", "seeds_done", "seeds_failed",
    "current_seed", "current_seed_attempt",
}
STRICT_MANIFEST_KEYS = {
    "campaign_id", "schema_version", "driver_version", "fork_jar_sha256",
    "policy", "seed_list", "status", "seeds_done", "seeds_failed",
}


def _fail(errs, path, lineno, msg):
    errs.append(f"{os.path.basename(path)}:{lineno}: {msg}")


def _check_stream_triples(errs, path, lineno, streams, names):
    missing = names - streams.keys()
    if missing:
        _fail(errs, path, lineno,
              f"oracle.streams missing {sorted(missing)}")
    for name in sorted(names & streams.keys()):
        sv = streams.get(name)
        if not isinstance(sv, dict) or \
                not {"counter", "s0", "s1"} <= sv.keys():
            _fail(errs, path, lineno,
                  f"stream {name} lacks counter/s0/s1")


def validate_file(path: str, require_oracle: bool = False):
    errs = []
    records = []
    with open(path, "r", encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            try:
                records.append((lineno, json.loads(line)))
            except json.JSONDecodeError as e:
                _fail(errs, path, lineno, f"record not JSON: {e}")
    if not records:
        _fail(errs, path, 0, "empty file")
        return errs, 0

    ln0, header = records[0]
    if header.get("record_kind") != "header":
        _fail(errs, path, ln0, "first record is not a header")
    missing = HEADER_KEYS - header.keys()
    if missing:
        _fail(errs, path, ln0, f"header missing keys: {sorted(missing)}")
    if SEED_KEYS - (header.get("seed") or {}).keys():
        _fail(errs, path, ln0, "header.seed lacks string/long")
    fh_hash = header.get("fork_jar_sha256", "")
    if len(fh_hash) != 64:
        _fail(errs, path, ln0, f"fork_jar_sha256 not a sha256: {fh_hash!r}")
    oracle_on = header.get("oracle_block_enabled", False)
    if require_oracle and oracle_on is not True:
        _fail(errs, path, ln0,
              "--require-oracle requires oracle_block_enabled: true")

    action_count = 0
    in_game_oracle_actions = 0
    saw_terminal = False
    for lineno, rec in records[1:]:
        kind = rec.get("record_kind")
        if kind == "terminal":
            saw_terminal = True
            if "outcome" not in rec:
                _fail(errs, path, lineno, "terminal record lacks outcome")
            continue
        if kind != "action":
            _fail(errs, path, lineno, f"unexpected record_kind {kind!r}")
            continue
        action_count += 1
        if "action_command" not in rec:
            _fail(errs, path, lineno, "action record lacks action_command")
        if "sim_action_bits" not in rec:
            _fail(errs, path, lineno, "action record lacks sim_action_bits key")
        st = rec.get("state_json")
        if not isinstance(st, dict):
            _fail(errs, path, lineno, "state_json missing/not an object")
            continue
        miss = STATUS_KEYS - st.keys()
        if miss:
            _fail(errs, path, lineno, f"state_json missing status keys {sorted(miss)}")
        if not st.get("in_game"):
            continue  # menu / terminal-observed states have no game_state
        gs = st.get("game_state")
        if not isinstance(gs, dict):
            _fail(errs, path, lineno, "in_game state lacks game_state object")
            continue
        gmiss = GS_ANCHORS - gs.keys()
        if gmiss:
            _fail(errs, path, lineno, f"game_state missing anchors {sorted(gmiss)}")
        if oracle_on or require_oracle:
            oc = gs.get("oracle")
            if not isinstance(oc, dict):
                reason = ("--require-oracle but no oracle block"
                          if require_oracle
                          else "oracle_block_enabled but no oracle block")
                _fail(errs, path, lineno, reason)
            else:
                if oracle_on:
                    omiss = ORACLE_KEYS - oc.keys()
                    if omiss:
                        _fail(errs, path, lineno,
                              f"oracle block missing {sorted(omiss)}")
                streams = oc.get("streams") or {}
                if not isinstance(streams, dict):
                    _fail(errs, path, lineno,
                          "oracle.streams missing/not an object")
                    streams = {}
                if oracle_on:
                    smiss = RUN_STREAMS - streams.keys()
                    if smiss:
                        _fail(errs, path, lineno,
                              f"oracle.streams missing {sorted(smiss)}")
                    # Preserve the default validator's historical contract:
                    # every emitted stream, including a future extra one, must
                    # have a complete state triple.
                    for name, sv in streams.items():
                        if not isinstance(sv, dict) or \
                                not {"counter", "s0", "s1"} <= sv.keys():
                            _fail(errs, path, lineno,
                                  f"stream {name} lacks counter/s0/s1")
                if require_oracle:
                    pity_missing = REWARD_ORACLE_FIELDS - oc.keys()
                    if pity_missing:
                        _fail(errs, path, lineno,
                              "reward oracle fields missing "
                              f"{sorted(pity_missing)}")
                    _check_stream_triples(
                        errs, path, lineno, streams, REWARD_STREAMS)
                    if not pity_missing and isinstance(streams, dict) and \
                            not (REWARD_STREAMS - streams.keys()) and all(
                                isinstance(streams.get(name), dict) and
                                {"counter", "s0", "s1"} <=
                                streams[name].keys()
                                for name in REWARD_STREAMS):
                        in_game_oracle_actions += 1

    if not saw_terminal:
        _fail(errs, path, records[-1][0], "no terminal record")
    if require_oracle and in_game_oracle_actions == 0:
        _fail(errs, path, 0,
              "--require-oracle requires at least one in-game action "
              "with a valid oracle block")
    return errs, action_count


def _read_json(path, errs, label):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            value = json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        errs.append(f"{label}: cannot read valid JSON: {exc}")
        return None
    if not isinstance(value, dict):
        errs.append(f"{label}: expected a JSON object")
        return None
    return value


def _artifact_identity(path, errs):
    records = []
    try:
        with open(path, "r", encoding="utf-8") as fh:
            for lineno, line in enumerate(fh, 1):
                if line.strip():
                    records.append((lineno, json.loads(line)))
    except (OSError, json.JSONDecodeError) as exc:
        errs.append(f"{os.path.basename(path)}: cannot inspect identity: {exc}")
        return None, None
    if not records:
        return None, None
    header = records[0][1] if isinstance(records[0][1], dict) else None
    terminals = [
        rec for _lineno, rec in records
        if isinstance(rec, dict) and rec.get("record_kind") == "terminal"
    ]
    return header, terminals[-1] if terminals else None


def _timing_identity(path, errs):
    try:
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                if line.strip():
                    value = json.loads(line)
                    if not isinstance(value, dict) or \
                            value.get("record_kind") != "timing_header":
                        errs.append(
                            f"{os.path.basename(path)}: first record is not "
                            "a timing_header")
                        return None
                    return value
    except (OSError, json.JSONDecodeError) as exc:
        errs.append(f"{os.path.basename(path)}: cannot inspect timing header: "
                    f"{exc}")
        return None
    errs.append(f"{os.path.basename(path)}: empty timing artifact")
    return None


def validate_campaign(campaign_dir: str, require_oracle: bool = False):
    """Validate one campaign directory.

    The historical/default mode remains the original glob-and-validate
    behavior. Strict oracle mode additionally treats progress + manifest as the
    campaign authority and proves a bijection between their successful seed
    ledger and both artifact families.
    """
    files = sorted(glob.glob(os.path.join(
        campaign_dir, "run_*_a20_ironclad.jsonl")))
    if not require_oracle:
        return files, []

    errs = []
    progress = _read_json(
        os.path.join(campaign_dir, "campaign_progress.json"),
        errs, "campaign_progress.json")
    manifest = _read_json(
        os.path.join(campaign_dir, "campaign_manifest.json"),
        errs, "campaign_manifest.json")
    if progress is None or manifest is None:
        return files, errs

    missing = STRICT_PROGRESS_KEYS - progress.keys()
    if missing:
        errs.append(f"campaign_progress.json: missing keys {sorted(missing)}")
    missing = STRICT_MANIFEST_KEYS - manifest.keys()
    if missing:
        errs.append(f"campaign_manifest.json: missing keys {sorted(missing)}")

    campaign_id = progress.get("campaign_id")
    directory_id = os.path.basename(os.path.normpath(campaign_dir))
    if campaign_id != directory_id:
        errs.append("campaign_progress.json: campaign_id does not match "
                    f"directory name ({campaign_id!r} != {directory_id!r})")
    if progress.get("status") != "complete":
        errs.append("campaign_progress.json: strict campaign status must be "
                    f"'complete', got {progress.get('status')!r}")
    if progress.get("current_seed") is not None or \
            progress.get("current_seed_attempt") != 0:
        errs.append("campaign_progress.json: completed campaign retains a "
                    "current seed/attempt")

    seed_list = progress.get("seed_list")
    done = progress.get("seeds_done")
    failed = progress.get("seeds_failed")
    if not isinstance(seed_list, list) or not seed_list or \
            any(not isinstance(seed, str) or not seed for seed in seed_list):
        errs.append("campaign_progress.json: seed_list must be a non-empty "
                    "list of seed strings")
        seed_list = []
    if len(seed_list) != len(set(seed_list)):
        errs.append("campaign_progress.json: seed_list contains duplicates")
    if not isinstance(done, list):
        errs.append("campaign_progress.json: seeds_done must be a list")
        done = []
    if not isinstance(failed, list):
        errs.append("campaign_progress.json: seeds_failed must be a list")
        failed = []
    if failed:
        errs.append("campaign_progress.json: strict campaign contains failed "
                    f"seeds {[row.get('seed') for row in failed if isinstance(row, dict)]}")

    done_seeds = [
        row.get("seed") for row in done if isinstance(row, dict)
    ]
    if len(done_seeds) != len(done):
        errs.append("campaign_progress.json: malformed seeds_done entry")
    if done_seeds != seed_list:
        errs.append("campaign_progress.json: seeds_done must match seed_list "
                    f"exactly and in order ({done_seeds!r} != {seed_list!r})")

    for key in STRICT_MANIFEST_KEYS:
        if key in progress and key in manifest and manifest[key] != progress[key]:
            errs.append(f"campaign_manifest.json: {key} does not match "
                        "campaign_progress.json")

    expected_runs = set()
    expected_timings = set()
    rows_by_seed = {}
    for row in done:
        if not isinstance(row, dict):
            continue
        seed = row.get("seed")
        artifact = row.get("artifact")
        conventional = f"run_{seed}_a20_ironclad.jsonl"
        if artifact != conventional:
            errs.append(f"campaign_progress.json: seed {seed!r} artifact "
                        f"must be {conventional!r}, got {artifact!r}")
        expected_runs.add(conventional)
        expected_timings.add(f"run_{seed}_a20_ironclad.timing.jsonl")
        if seed in rows_by_seed:
            errs.append(f"campaign_progress.json: duplicate done seed {seed!r}")
        rows_by_seed[seed] = row

    actual_runs = {os.path.basename(path) for path in files}
    timing_files = sorted(glob.glob(os.path.join(
        campaign_dir, "run_*_a20_ironclad.timing.jsonl")))
    actual_timings = {os.path.basename(path) for path in timing_files}
    for name in sorted(expected_runs - actual_runs):
        errs.append(f"campaign artifacts: missing {name}")
    for name in sorted(actual_runs - expected_runs):
        errs.append(f"campaign artifacts: unexpected/stale {name}")
    for name in sorted(expected_timings - actual_timings):
        errs.append(f"campaign timing artifacts: missing {name}")
    for name in sorted(actual_timings - expected_timings):
        errs.append(f"campaign timing artifacts: unexpected/stale {name}")

    for path in files:
        name = os.path.basename(path)
        if name not in expected_runs:
            continue
        seed = name[len("run_"):-len("_a20_ironclad.jsonl")]
        row = rows_by_seed.get(seed, {})
        header, terminal = _artifact_identity(path, errs)
        if not isinstance(header, dict):
            continue
        expected_header = {
            "campaign_id": campaign_id,
            "policy": progress.get("policy"),
            "fork_jar_sha256": progress.get("fork_jar_sha256"),
            "schema_version": progress.get("schema_version"),
            "attempt": row.get("attempts"),
        }
        for key, expected in expected_header.items():
            if header.get(key) != expected:
                errs.append(f"{name}: header {key} {header.get(key)!r} "
                            f"does not match campaign {expected!r}")
        if (header.get("seed") or {}).get("string") != seed:
            errs.append(f"{name}: header seed does not match filename/ledger")
        if isinstance(terminal, dict):
            for terminal_key, row_key in (
                    ("outcome", "outcome"), ("floor", "floor"),
                    ("actions", "actions")):
                if terminal.get(terminal_key) != row.get(row_key):
                    errs.append(f"{name}: terminal {terminal_key} does not "
                                "match seeds_done")

    for path in timing_files:
        name = os.path.basename(path)
        if name not in expected_timings:
            continue
        seed = name[len("run_"):-len("_a20_ironclad.timing.jsonl")]
        row = rows_by_seed.get(seed, {})
        timing = _timing_identity(path, errs)
        if timing is None:
            continue
        for key, expected in {
                "campaign_id": campaign_id,
                "policy": progress.get("policy"),
                "seed": seed,
                "attempt": row.get("attempts"),
        }.items():
            if timing.get(key) != expected:
                errs.append(f"{name}: timing_header {key} "
                            f"{timing.get(key)!r} does not match campaign "
                            f"{expected!r}")

    return files, errs


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Validate B1.4 campaign artifacts")
    ap.add_argument("paths", nargs="*", help="run JSONL files")
    ap.add_argument("--campaign", help="a <data-root>/<campaign-id> directory; "
                    "validates every run_*.jsonl within")
    ap.add_argument("--require-oracle", action="store_true",
                    help="reject artifacts without an enabled oracle block and "
                         "require B4.5 pity fields plus reward RNG triples")
    args = ap.parse_args(argv)

    files = list(args.paths)
    campaign_errs = []
    if args.campaign:
        campaign_files, campaign_errs = validate_campaign(
            args.campaign, require_oracle=args.require_oracle)
        files += campaign_files
    if not files:
        for error in campaign_errs:
            print(f"    {error}")
        print("no artifact files given")
        return 2

    total_errs = len(campaign_errs)
    for error in campaign_errs:
        print(f"    {error}")
    for path in files:
        errs, actions = validate_file(path, require_oracle=args.require_oracle)
        status = "OK" if not errs else f"{len(errs)} ERROR(S)"
        print(f"{os.path.basename(path)}: {actions} actions -- {status}")
        for e in errs[:25]:
            print(f"    {e}")
        total_errs += len(errs)
    print(f"\n{len(files)} file(s), {total_errs} error(s)")
    return 0 if total_errs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
