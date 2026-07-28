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
import math
import os
import re
import sys

from campaign_paths import (
    exact_path_without_redirect,
    validate_campaign_id,
    validate_seed_list,
)

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
STRICT_DONE_KEYS = {
    "seed", "outcome", "floor", "actions", "attempts", "artifact",
}
STRICT_SEED_KEYS = {"string", "long", "long_getLong", "crosscheck_ok"}
STRICT_TERMINAL_KEYS = {
    "record_kind", "seq", "outcome", "floor", "actions",
}
TIMING_HEADER_KEYS = {
    "record_kind", "campaign_id", "policy", "seed", "attempt",
    "schema_version", "driver_version", "fork_jar_sha256",
}
TIMING_MARK_KEYS = {
    "record_kind", "seq", "t_mono", "t_wall", "cmd", "floor", "screen",
}
TERMINAL_MARKER = "__terminal_observed__"
_RUN_NAME_RE = re.compile(r"run_([0-9A-Z]+)_a20_ironclad\.jsonl")
_SEED_CHARS = "0123456789ABCDEFGHIJKLMNPQRSTUVWXYZ"


def _fail(errs, path, lineno, msg):
    errs.append(f"{os.path.basename(path)}:{lineno}: {msg}")


def _is_int(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_number(value) -> bool:
    return isinstance(value, (int, float)) and \
        not isinstance(value, bool) and math.isfinite(value)


def seed_to_long(seed_str: str) -> int:
    total = 0
    for ch in seed_str.upper().replace("O", "0"):
        idx = _SEED_CHARS.find(ch)
        total = (total * 35 + (idx if idx >= 0 else 0)) & 0xFFFFFFFFFFFFFFFF
    return total - (1 << 64) if total >= (1 << 63) else total


def _filename_seed(path: str):
    match = _RUN_NAME_RE.fullmatch(os.path.basename(path))
    return match.group(1) if match else None


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
    if require_oracle:
        try:
            path = exact_path_without_redirect(path)
        except ValueError as exc:
            _fail(errs, path, 0, str(exc))
            return errs, 0
    try:
        with open(path, "r", encoding="utf-8") as fh:
            for lineno, line in enumerate(fh, 1):
                line = line.strip()
                if not line:
                    continue
                try:
                    records.append((lineno, json.loads(line)))
                except json.JSONDecodeError as e:
                    _fail(errs, path, lineno, f"record not JSON: {e}")
    except OSError as exc:
        _fail(errs, path, 0, f"cannot read artifact: {exc}")
        return errs, 0
    if not records:
        _fail(errs, path, 0, "empty file")
        return errs, 0

    ln0, header = records[0]
    if not isinstance(header, dict):
        _fail(errs, path, ln0, "header record is not an object")
        return errs, 0
    if header.get("record_kind") != "header":
        _fail(errs, path, ln0, "first record is not a header")
    missing = HEADER_KEYS - header.keys()
    if missing:
        _fail(errs, path, ln0, f"header missing keys: {sorted(missing)}")
    seed_obj = header.get("seed")
    if not isinstance(seed_obj, dict):
        _fail(errs, path, ln0, "header.seed missing/not an object")
        seed_obj = {}
    if SEED_KEYS - seed_obj.keys():
        _fail(errs, path, ln0, "header.seed lacks string/long")
    fh_hash = header.get("fork_jar_sha256", "")
    if not isinstance(fh_hash, str) or len(fh_hash) != 64:
        _fail(errs, path, ln0, f"fork_jar_sha256 not a sha256: {fh_hash!r}")
    oracle_on = header.get("oracle_block_enabled", False)
    if require_oracle and oracle_on is not True:
        _fail(errs, path, ln0,
              "--require-oracle requires oracle_block_enabled: true")

    expected_seed = None
    expected_long = None
    if require_oracle:
        expected_seed = _filename_seed(path)
        if expected_seed is None:
            _fail(errs, path, ln0,
                  "strict artifact filename must be "
                  "run_<SEED>_a20_ironclad.jsonl")
        missing_seed = STRICT_SEED_KEYS - seed_obj.keys()
        if missing_seed:
            _fail(errs, path, ln0,
                  f"strict header.seed missing {sorted(missing_seed)}")
        header_seed = seed_obj.get("string")
        if expected_seed is not None and header_seed != expected_seed:
            _fail(errs, path, ln0,
                  f"header seed string {header_seed!r} does not match "
                  f"filename {expected_seed!r}")
        if isinstance(header_seed, str):
            try:
                validate_seed_list([header_seed])
                computed_long = seed_to_long(header_seed)
                expected_long = computed_long
                if not _is_int(seed_obj.get("long_getLong")) or \
                        seed_obj.get("long_getLong") != computed_long:
                    _fail(errs, path, ln0,
                          f"header seed.long_getLong "
                          f"{seed_obj.get('long_getLong')!r} does not match "
                          f"computed {computed_long!r}")
                if not _is_int(seed_obj.get("long")) or \
                        seed_obj.get("long") != computed_long:
                    _fail(errs, path, ln0,
                          f"header seed.long {seed_obj.get('long')!r} does "
                          f"not match computed {computed_long!r}")
            except ValueError as exc:
                _fail(errs, path, ln0, f"invalid header seed: {exc}")
        else:
            _fail(errs, path, ln0, "header seed.string must be a string")
        if seed_obj.get("crosscheck_ok") is not True:
            _fail(errs, path, ln0,
                  "strict header seed.crosscheck_ok must be true")
        for key in ("campaign_id", "attempt"):
            if key not in header:
                _fail(errs, path, ln0, f"strict header missing {key}")
        try:
            validate_campaign_id(header.get("campaign_id"))
        except ValueError as exc:
            _fail(errs, path, ln0, f"invalid header campaign_id: {exc}")
        if not _is_int(header.get("attempt")) or header.get("attempt") < 1:
            _fail(errs, path, ln0,
                  "strict header attempt must be a positive integer")
        if not _is_int(header.get("schema_version")):
            _fail(errs, path, ln0,
                  "strict header schema_version must be an integer")
        if not isinstance(header.get("driver_version"), str) or \
                not header.get("driver_version"):
            _fail(errs, path, ln0,
                  "strict header driver_version must be a non-empty string")
        if not isinstance(fh_hash, str) or \
                re.fullmatch(r"[0-9A-Fa-f]{64}", fh_hash) is None:
            _fail(errs, path, ln0,
                  "strict fork_jar_sha256 must be 64 hexadecimal digits")

    action_count = 0
    injected_count = 0
    in_game_oracle_actions = 0
    saw_terminal = False
    terminal_count = 0
    terminal = None
    action_records = []
    marker_positions = []
    for record_index, (lineno, rec) in enumerate(records[1:], 1):
        if not isinstance(rec, dict):
            _fail(errs, path, lineno, "top-level record is not an object")
            continue
        kind = rec.get("record_kind")
        if kind == "terminal":
            saw_terminal = True
            terminal_count += 1
            terminal = rec
            if "outcome" not in rec:
                _fail(errs, path, lineno, "terminal record lacks outcome")
            if require_oracle:
                if terminal_count > 1:
                    _fail(errs, path, lineno,
                          "strict artifact has more than one terminal")
                if record_index != len(records) - 1:
                    _fail(errs, path, lineno,
                          "strict terminal must be the final record")
                missing_terminal = STRICT_TERMINAL_KEYS - rec.keys()
                if missing_terminal:
                    _fail(errs, path, lineno,
                          "strict terminal missing "
                          f"{sorted(missing_terminal)}")
                if not isinstance(rec.get("outcome"), str) or \
                        not rec.get("outcome"):
                    _fail(errs, path, lineno,
                          "strict terminal outcome must be a non-empty string")
                if not _is_int(rec.get("floor")):
                    _fail(errs, path, lineno,
                          "strict terminal floor must be an integer")
                if not _is_int(rec.get("actions")) or \
                        rec.get("actions", -1) < 0:
                    _fail(errs, path, lineno,
                          "strict terminal actions must be a non-negative integer")
                if not _is_int(rec.get("seq")) or rec.get("seq", -1) < 0:
                    _fail(errs, path, lineno,
                          "strict terminal seq must be a non-negative integer")
            continue
        if kind != "action":
            _fail(errs, path, lineno, f"unexpected record_kind {kind!r}")
            continue
        if require_oracle and saw_terminal:
            _fail(errs, path, lineno,
                  "strict action appears after terminal")
        if require_oracle:
            expected_seq = len(action_records)
            if not _is_int(rec.get("seq")) or rec.get("seq") != expected_seq:
                _fail(errs, path, lineno,
                      f"strict action seq must be contiguous: got "
                      f"{rec.get('seq')!r}, expected {expected_seq}")
            action_records.append(rec)
        action_count += 1
        if "action_command" not in rec:
            _fail(errs, path, lineno, "action record lacks action_command")
        if "sim_action_bits" not in rec:
            _fail(errs, path, lineno, "action record lacks sim_action_bits key")
        if rec.get("action_command") == TERMINAL_MARKER:
            marker_positions.append(len(action_records) - 1)
        else:
            injected_count += 1
        st = rec.get("state_json")
        if not isinstance(st, dict):
            _fail(errs, path, lineno, "state_json missing/not an object")
            continue
        miss = STATUS_KEYS - st.keys()
        if miss:
            _fail(errs, path, lineno, f"state_json missing status keys {sorted(miss)}")
        if require_oracle:
            for key in ("ready_for_command", "in_game"):
                if not isinstance(st.get(key), bool):
                    _fail(errs, path, lineno,
                          f"strict state_json.{key} must be boolean")
        if not st.get("in_game"):
            continue  # menu / terminal-observed states have no game_state
        gs = st.get("game_state")
        if not isinstance(gs, dict):
            _fail(errs, path, lineno, "in_game state lacks game_state object")
            continue
        gmiss = GS_ANCHORS - gs.keys()
        if gmiss:
            _fail(errs, path, lineno, f"game_state missing anchors {sorted(gmiss)}")
        if require_oracle and expected_long is not None and (
                not _is_int(gs.get("seed")) or
                gs.get("seed") != expected_long):
            _fail(errs, path, lineno,
                  f"game_state.seed {gs.get('seed')!r} does not match "
                  f"header/requested seed {expected_long!r}")
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
                    if expected_long is not None and (
                            not _is_int(oc.get("seed")) or
                            oc.get("seed") != expected_long):
                        _fail(errs, path, lineno,
                              f"oracle.seed {oc.get('seed')!r} does not match "
                              f"header/requested seed {expected_long!r}")
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
    if require_oracle:
        if terminal_count != 1:
            _fail(errs, path, 0,
                  f"strict artifact requires exactly one terminal, got "
                  f"{terminal_count}")
        if len(marker_positions) > 1:
            _fail(errs, path, 0,
                  "strict artifact has duplicate terminal-observed markers")
        if marker_positions and marker_positions[0] != len(action_records) - 1:
            _fail(errs, path, 0,
                  "terminal-observed marker must be the last action")
        if isinstance(terminal, dict):
            if terminal.get("seq") != len(action_records):
                _fail(errs, path, 0,
                      f"terminal seq {terminal.get('seq')!r} does not match "
                      f"{len(action_records)} action records")
            if terminal.get("actions") != injected_count:
                _fail(errs, path, 0,
                      f"terminal actions {terminal.get('actions')!r} does not "
                      f"match {injected_count} injected action records")
    if require_oracle and in_game_oracle_actions == 0:
        _fail(errs, path, 0,
              "--require-oracle requires at least one in-game action "
              "with a valid oracle block")
    return errs, injected_count if require_oracle else action_count


def _read_json(path, errs, label):
    try:
        with open(exact_path_without_redirect(path), "r",
                  encoding="utf-8") as fh:
            value = json.load(fh)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        errs.append(f"{label}: cannot read valid JSON: {exc}")
        return None
    if not isinstance(value, dict):
        errs.append(f"{label}: expected a JSON object")
        return None
    return value


def _artifact_identity(path, errs):
    records = []
    try:
        with open(exact_path_without_redirect(path), "r",
                  encoding="utf-8") as fh:
            for lineno, line in enumerate(fh, 1):
                if line.strip():
                    records.append((lineno, json.loads(line)))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        errs.append(f"{os.path.basename(path)}: cannot inspect identity: {exc}")
        return None, None, []
    if not records:
        return None, None, []
    header = records[0][1] if isinstance(records[0][1], dict) else None
    terminals = [
        rec for _lineno, rec in records
        if isinstance(rec, dict) and rec.get("record_kind") == "terminal"
    ]
    actions = [
        rec for _lineno, rec in records
        if isinstance(rec, dict) and rec.get("record_kind") == "action" and
        rec.get("action_command") != TERMINAL_MARKER
    ]
    return header, terminals[-1] if terminals else None, actions


def _timing_records(path, errs):
    records = []
    try:
        with open(exact_path_without_redirect(path), "r",
                  encoding="utf-8") as fh:
            for lineno, line in enumerate(fh, 1):
                if line.strip():
                    try:
                        value = json.loads(line)
                    except json.JSONDecodeError as exc:
                        errs.append(
                            f"{os.path.basename(path)}:{lineno}: timing record "
                            f"not JSON: {exc}")
                        continue
                    records.append((lineno, value))
    except (OSError, ValueError) as exc:
        errs.append(f"{os.path.basename(path)}: cannot read timing artifact: "
                    f"{exc}")
        return None, []
    if not records:
        errs.append(f"{os.path.basename(path)}: empty timing artifact")
        return None, []

    header = records[0][1]
    if not isinstance(header, dict) or \
            header.get("record_kind") != "timing_header":
        errs.append(f"{os.path.basename(path)}:{records[0][0]}: first record "
                    "is not a timing_header")
        header = None
    elif TIMING_HEADER_KEYS - header.keys():
        errs.append(f"{os.path.basename(path)}:{records[0][0]}: timing_header "
                    f"missing {sorted(TIMING_HEADER_KEYS - header.keys())}")

    marks = []
    prior_mono = None
    prior_wall = None
    for expected_seq, (lineno, value) in enumerate(records[1:]):
        if not isinstance(value, dict):
            errs.append(f"{os.path.basename(path)}:{lineno}: timing record is "
                        "not an object")
            continue
        kind = value.get("record_kind")
        if kind != "mark":
            errs.append(f"{os.path.basename(path)}:{lineno}: unexpected timing "
                        f"record_kind {kind!r}")
            continue
        missing = TIMING_MARK_KEYS - value.keys()
        if missing:
            errs.append(f"{os.path.basename(path)}:{lineno}: timing mark "
                        f"missing {sorted(missing)}")
        if not _is_int(value.get("seq")) or \
                value.get("seq") != expected_seq:
            errs.append(f"{os.path.basename(path)}:{lineno}: timing mark seq "
                        f"{value.get('seq')!r} is not contiguous expected "
                        f"{expected_seq}")
        if not isinstance(value.get("cmd"), str) or not value.get("cmd"):
            errs.append(f"{os.path.basename(path)}:{lineno}: timing mark cmd "
                        "must be a non-empty string")
        mono = value.get("t_mono")
        wall = value.get("t_wall")
        if not _is_number(mono):
            errs.append(f"{os.path.basename(path)}:{lineno}: t_mono must be "
                        "finite numeric")
        elif prior_mono is not None and mono <= prior_mono:
            errs.append(f"{os.path.basename(path)}:{lineno}: t_mono must be "
                        "strictly increasing")
        if not _is_number(wall):
            errs.append(f"{os.path.basename(path)}:{lineno}: t_wall must be "
                        "finite numeric")
        elif prior_wall is not None and wall < prior_wall:
            errs.append(f"{os.path.basename(path)}:{lineno}: t_wall must be "
                        "non-decreasing")
        if _is_number(mono):
            prior_mono = mono
        if _is_number(wall):
            prior_wall = wall
        marks.append(value)
    if not marks:
        errs.append(f"{os.path.basename(path)}: strict timing artifact has "
                    "no action marks")
    return header, marks


def validate_campaign(campaign_dir: str, require_oracle: bool = False):
    """Validate one campaign directory.

    The historical/default mode remains the original glob-and-validate
    behavior. Strict oracle mode additionally treats progress + manifest as the
    campaign authority and proves a bijection between their successful seed
    ledger and both artifact families.
    """
    if require_oracle:
        try:
            campaign_dir = exact_path_without_redirect(campaign_dir)
        except ValueError as exc:
            return [], [f"campaign directory: {exc}"]
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
    try:
        validate_campaign_id(directory_id)
        validate_campaign_id(campaign_id)
    except ValueError as exc:
        errs.append(f"campaign identity: {exc}")
    if campaign_id != directory_id:
        errs.append("campaign_progress.json: campaign_id does not match "
                    f"directory name ({campaign_id!r} != {directory_id!r})")
    if progress.get("status") != "complete":
        errs.append("campaign_progress.json: strict campaign status must be "
                    f"'complete', got {progress.get('status')!r}")
    current_attempt = progress.get("current_seed_attempt")
    if progress.get("current_seed") is not None or \
            not _is_int(current_attempt) or current_attempt != 0:
        errs.append("campaign_progress.json: completed campaign retains a "
                    "current seed/attempt")

    seed_list = progress.get("seed_list")
    done = progress.get("seeds_done")
    failed = progress.get("seeds_failed")
    try:
        seed_list = validate_seed_list(seed_list)
    except ValueError as exc:
        errs.append(f"campaign_progress.json: invalid seed_list: {exc}")
        seed_list = []
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
    # policy_seed (design 7.5 reproducibility) is additive, NOT in
    # STRICT_MANIFEST_KEYS/STRICT_PROGRESS_KEYS, so an old campaign missing it
    # entirely from both files is unaffected -- this only fires once both
    # sides carry the key and disagree.
    if "policy_seed" in progress and "policy_seed" in manifest and \
            manifest["policy_seed"] != progress["policy_seed"]:
        errs.append("campaign_manifest.json: policy_seed does not match "
                    "campaign_progress.json")

    expected_runs = set()
    expected_timings = set()
    rows_by_seed = {}
    for row in done:
        if not isinstance(row, dict):
            continue
        missing_done = STRICT_DONE_KEYS - row.keys()
        if missing_done:
            errs.append("campaign_progress.json: seeds_done row missing "
                        f"{sorted(missing_done)}")
        seed = row.get("seed")
        if not isinstance(row.get("outcome"), str) or not row.get("outcome"):
            errs.append(f"campaign_progress.json: seed {seed!r} outcome must "
                        "be a non-empty string")
        if not _is_int(row.get("floor")):
            errs.append(f"campaign_progress.json: seed {seed!r} floor must "
                        "be an integer")
        if not _is_int(row.get("actions")) or row.get("actions", -1) < 0:
            errs.append(f"campaign_progress.json: seed {seed!r} actions must "
                        "be a non-negative integer")
        if not _is_int(row.get("attempts")) or row.get("attempts", 0) < 1:
            errs.append(f"campaign_progress.json: seed {seed!r} attempts must "
                        "be a positive integer")
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

    artifact_actions = {}
    for path in files:
        name = os.path.basename(path)
        if name not in expected_runs:
            continue
        seed = name[len("run_"):-len("_a20_ironclad.jsonl")]
        row = rows_by_seed.get(seed, {})
        file_errs, _action_count = validate_file(
            path, require_oracle=True)
        errs.extend(file_errs)
        header, terminal, actions = _artifact_identity(path, errs)
        artifact_actions[seed] = actions
        if not isinstance(header, dict):
            continue
        expected_header = {
            "campaign_id": campaign_id,
            "policy": progress.get("policy"),
            "fork_jar_sha256": progress.get("fork_jar_sha256"),
            "schema_version": progress.get("schema_version"),
            "driver_version": progress.get("driver_version"),
            "attempt": row.get("attempts"),
        }
        # Additive: only asserted when the progress ledger actually carries
        # policy_seed, so a campaign captured before this field existed
        # (progress lacks the key entirely) is not newly failed.
        if "policy_seed" in progress:
            expected_header["policy_seed"] = progress.get("policy_seed")
        for key, expected in expected_header.items():
            if header.get(key) != expected:
                errs.append(f"{name}: header {key} {header.get(key)!r} "
                            f"does not match campaign {expected!r}")
        header_seed = header.get("seed")
        if not isinstance(header_seed, dict) or \
                header_seed.get("string") != seed:
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
        timing, marks = _timing_records(path, errs)
        if timing is None:
            continue
        for key, expected in {
                "campaign_id": campaign_id,
                "policy": progress.get("policy"),
                "seed": seed,
                "attempt": row.get("attempts"),
                "schema_version": progress.get("schema_version"),
                "driver_version": progress.get("driver_version"),
                "fork_jar_sha256": progress.get("fork_jar_sha256"),
        }.items():
            if timing.get(key) != expected:
                errs.append(f"{name}: timing_header {key} "
                            f"{timing.get(key)!r} does not match campaign "
                            f"{expected!r}")
        actions = artifact_actions.get(seed, [])
        if len(marks) != len(actions):
            errs.append(f"{name}: {len(marks)} timing marks do not match "
                        f"{len(actions)} injected artifact actions")
        for index, (mark, action) in enumerate(zip(marks, actions)):
            if mark.get("seq") != action.get("seq"):
                errs.append(f"{name}: timing mark #{index} seq does not match "
                            "artifact action")
            if mark.get("cmd") != action.get("action_command"):
                errs.append(f"{name}: timing mark #{index} cmd "
                            f"{mark.get('cmd')!r} does not match artifact "
                            f"{action.get('action_command')!r}")
            state = action.get("state_json")
            game_state = state.get("game_state") \
                if isinstance(state, dict) else None
            if isinstance(game_state, dict):
                if mark.get("floor") != game_state.get("floor"):
                    errs.append(f"{name}: timing mark #{index} floor does not "
                                "match artifact action state")
                if mark.get("screen") != game_state.get("screen_type"):
                    errs.append(f"{name}: timing mark #{index} screen does not "
                                "match artifact action state")

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
    strict_campaign_files = set()
    if args.campaign:
        campaign_files, campaign_errs = validate_campaign(
            args.campaign, require_oracle=args.require_oracle)
        files += campaign_files
        if args.require_oracle:
            strict_campaign_files = {
                os.path.abspath(path) for path in campaign_files
            }
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
        if os.path.abspath(path) not in strict_campaign_files:
            total_errs += len(errs)
    print(f"\n{len(files)} file(s), {total_errs} error(s)")
    return 0 if total_errs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
