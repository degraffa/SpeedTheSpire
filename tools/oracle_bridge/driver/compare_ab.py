#!/usr/bin/env python3
"""B1.3 strip-equivalence comparator (SpeedTheSpire task B1.3).

Byte-compares the oracle state dumps of two campaigns that ran the SAME fixed
per-seed scripts -- one with the rendering-strip patches ON, one with them OFF --
and asserts they are byte-identical after the single sanctioned normalization
(drop per-instance `uuid` fields; PROTOCOL.md / B0.2, the only nondeterministic
field). This is the design §2.2 semantic guard: a strip patch may remove time and
pixels, never order or state.

For each seed present in both campaigns it aligns the `action` records by seq and,
per record, checks (a) the injected command matches and (b) the normalized
state_json (which carries the full §2.5 `oracle` block when oracleBlock is on) is
identical. Any mismatch is a semantic leak and is reported with its location; the
run must NOT be "fixed" by normalizing the difference away.

Exit 0 iff every compared record is byte-identical. Stdlib only.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re


def strip_uuid(obj):
    """Recursively drop 'uuid' keys (the only known nondeterministic field)."""
    if isinstance(obj, dict):
        return {k: strip_uuid(v) for k, v in obj.items() if k != "uuid"}
    if isinstance(obj, list):
        return [strip_uuid(v) for v in obj]
    return obj


def canon(state) -> str:
    return json.dumps(strip_uuid(state), sort_keys=True, ensure_ascii=False,
                      separators=(",", ":"))


def load_run(path: str):
    header = None
    actions = []  # list of (seq, action_command, state_json)
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            k = rec.get("record_kind")
            if k == "header":
                header = rec
            elif k == "action":
                actions.append((rec.get("seq"), rec.get("action_command"),
                                rec.get("state_json")))
    return header, actions


def seeds_in(campaign_dir: str) -> dict:
    out = {}
    for path in glob.glob(os.path.join(campaign_dir,
                                       "run_*_a20_ironclad.jsonl")):
        m = re.search(r"run_(.+?)_a20_ironclad\.jsonl$", os.path.basename(path))
        if m:
            out[m.group(1)] = path
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="B1.3 strip-equivalence comparator")
    ap.add_argument("--off", required=True, help="strip-OFF campaign dir (A)")
    ap.add_argument("--on", required=True, help="strip-ON campaign dir (B)")
    ap.add_argument("--max-report", type=int, default=5,
                    help="max per-seed diff details to print")
    args = ap.parse_args(argv)

    off = seeds_in(args.off)
    on = seeds_in(args.on)
    common = sorted(set(off) & set(on))
    only_off = sorted(set(off) - set(on))
    only_on = sorted(set(on) - set(off))
    if only_off:
        print(f"WARN seeds only in OFF: {only_off}")
    if only_on:
        print(f"WARN seeds only in ON:  {only_on}")
    if not common:
        print("FAIL: no common seeds to compare")
        return 2

    tot_records = 0
    tot_bytes = 0
    tot_state_diffs = 0
    tot_cmd_diffs = 0
    tot_len_diffs = 0
    per_seed = []

    for seed in common:
        h_off, a_off = load_run(off[seed])
        h_on, a_on = load_run(on[seed])
        n = min(len(a_off), len(a_on))
        len_diff = len(a_off) != len(a_on)
        if len_diff:
            tot_len_diffs += 1
        state_diffs = 0
        cmd_diffs = 0
        details = []
        # oracle-block presence must match (both should be on for B1.3)
        ob_off = (h_off or {}).get("oracle_block_enabled")
        ob_on = (h_on or {}).get("oracle_block_enabled")
        for i in range(n):
            seq_o, cmd_o, st_o = a_off[i]
            seq_n, cmd_n, st_n = a_on[i]
            if cmd_o != cmd_n:
                cmd_diffs += 1
                if len(details) < args.max_report:
                    details.append(f"  seq {i}: CMD off={cmd_o!r} on={cmd_n!r}")
                continue
            c_o = canon(st_o)
            c_n = canon(st_n)
            tot_records += 1
            tot_bytes += len(c_o)
            if c_o != c_n:
                state_diffs += 1
                if len(details) < args.max_report:
                    # find first differing field for a readable pointer
                    details.append(_first_diff(seq_o, cmd_o, st_o, st_n))
        tot_state_diffs += state_diffs
        tot_cmd_diffs += cmd_diffs
        status = "OK" if (state_diffs == 0 and cmd_diffs == 0
                          and not len_diff and ob_off == ob_on) else "DIFF"
        per_seed.append((seed, status, n, state_diffs, cmd_diffs, len_diff,
                         ob_off, ob_on, details))

    print("=" * 70)
    print(f"B1.3 strip-equivalence: OFF={args.off}")
    print(f"                         ON={args.on}")
    print(f"common seeds: {len(common)}")
    print("-" * 70)
    for (seed, status, n, sd, cd, ld, ob_o, ob_n, details) in per_seed:
        flags = []
        if ld:
            flags.append("LEN")
        if ob_o != ob_n:
            flags.append(f"oracleblk off={ob_o} on={ob_n}")
        print(f"[{status}] {seed}: {n} records compared, state_diffs={sd}, "
              f"cmd_diffs={cd}"
              + (("  " + " ".join(flags)) if flags else ""))
        for d in details:
            print(d)
    print("-" * 70)
    print(f"records compared: {tot_records}")
    print(f"bytes compared (normalized OFF state, sum): {tot_bytes}")
    print(f"state diffs: {tot_state_diffs}  cmd diffs: {tot_cmd_diffs}  "
          f"length diffs: {tot_len_diffs}")
    ok = (tot_state_diffs == 0 and tot_cmd_diffs == 0 and tot_len_diffs == 0)
    print("RESULT:", "PASS -- byte-identical" if ok else "FAIL -- semantic leak")
    return 0 if ok else 1


def _first_diff(seq, cmd, st_o, st_n) -> str:
    so = strip_uuid(st_o)
    sn = strip_uuid(st_n)
    path = _walk(so, sn, "")
    return f"  seq {seq} cmd={cmd!r}: STATE diff at {path or '<root>'}"


def _walk(a, b, p):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                return f"{p}.{k} (missing in OFF)"
            if k not in b:
                return f"{p}.{k} (missing in ON)"
            r = _walk(a[k], b[k], f"{p}.{k}")
            if r:
                return r
        return ""
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return f"{p} (list len {len(a)} vs {len(b)})"
        for i, (x, y) in enumerate(zip(a, b)):
            r = _walk(x, y, f"{p}[{i}]")
            if r:
                return r
        return ""
    if a != b:
        return f"{p} (off={a!r} on={b!r})"
    return ""


if __name__ == "__main__":
    raise SystemExit(main())
