#!/usr/bin/env python3
"""Extract per-seed fixed command scripts from a completed campaign's artifacts
(SpeedTheSpire task B1.3).

The B1.3 strip-equivalence A/B needs the *same fixed command list* forced on the
strip-on and strip-off runs, so any semantic divergence surfaces at a matching
seq (rather than the random-legal policy silently diverging into a different
command stream). This reads a baseline campaign's run_<SEED>_a20_ironclad.jsonl
files, pulls the ordered injected commands (the `action_command` of every
`action` record, dropping the synthetic `__terminal_observed__` marker), and
writes script_<SEED>.txt files a `--policy script --script-dir` campaign replays.

Dropping `state`/`proceed`/etc. driver recovery commands is NOT done: they were
actually sent and are part of the faithful trajectory (`state` is a pure no-op;
the others advanced real screens). Only the non-command marker is removed.

Stdlib only.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re

MARKER = "__terminal_observed__"


def extract_one(path: str) -> list:
    cmds = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("record_kind") != "action":
                continue
            cmd = rec.get("action_command")
            if not cmd or cmd == MARKER:
                continue
            cmds.append(cmd)
    return cmds


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Extract per-seed scripts (B1.3)")
    ap.add_argument("--campaign-dir", required=True,
                    help="a completed campaign's data dir (run_*.jsonl inside)")
    ap.add_argument("--out-dir", required=True,
                    help="where script_<SEED>.txt files are written")
    args = ap.parse_args(argv)

    os.makedirs(args.out_dir, exist_ok=True)
    runs = sorted(glob.glob(os.path.join(args.campaign_dir,
                                         "run_*_a20_ironclad.jsonl")))
    if not runs:
        print(f"no run artifacts under {args.campaign_dir}")
        return 1

    total = 0
    for path in runs:
        m = re.search(r"run_(.+?)_a20_ironclad\.jsonl$", os.path.basename(path))
        if not m:
            continue
        seed = m.group(1)
        cmds = extract_one(path)
        out = os.path.join(args.out_dir, f"script_{seed}.txt")
        with open(out, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(f"# B1.3 fixed replay script for seed {seed}\n")
            fh.write(f"# extracted from {os.path.basename(path)} "
                     f"({len(cmds)} commands)\n")
            for c in cmds:
                fh.write(c + "\n")
        total += 1
        print(f"{seed}: {len(cmds)} commands -> {out}")
    print(f"wrote {total} scripts to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
