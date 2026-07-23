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


def _fail(errs, path, lineno, msg):
    errs.append(f"{os.path.basename(path)}:{lineno}: {msg}")


def validate_file(path: str):
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

    action_count = 0
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
        if oracle_on:
            oc = gs.get("oracle")
            if not isinstance(oc, dict):
                _fail(errs, path, lineno, "oracle_block_enabled but no oracle block")
            else:
                omiss = ORACLE_KEYS - oc.keys()
                if omiss:
                    _fail(errs, path, lineno, f"oracle block missing {sorted(omiss)}")
                streams = oc.get("streams") or {}
                smiss = RUN_STREAMS - streams.keys()
                if smiss:
                    _fail(errs, path, lineno,
                          f"oracle.streams missing {sorted(smiss)}")
                for name, sv in streams.items():
                    if not {"counter", "s0", "s1"} <= (sv or {}).keys():
                        _fail(errs, path, lineno,
                              f"stream {name} lacks counter/s0/s1")

    if not saw_terminal:
        _fail(errs, path, records[-1][0], "no terminal record")
    return errs, action_count


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Validate B1.4 campaign artifacts")
    ap.add_argument("paths", nargs="*", help="run JSONL files")
    ap.add_argument("--campaign", help="a <data-root>/<campaign-id> directory; "
                    "validates every run_*.jsonl within")
    args = ap.parse_args(argv)

    files = list(args.paths)
    if args.campaign:
        files += sorted(glob.glob(os.path.join(args.campaign,
                                               "run_*_a20_ironclad.jsonl")))
    if not files:
        print("no artifact files given")
        return 2

    total_errs = 0
    for path in files:
        errs, actions = validate_file(path)
        status = "OK" if not errs else f"{len(errs)} ERROR(S)"
        print(f"{os.path.basename(path)}: {actions} actions -- {status}")
        for e in errs[:25]:
            print(f"    {e}")
        total_errs += len(errs)
    print(f"\n{len(files)} file(s), {total_errs} error(s)")
    return 0 if total_errs == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
