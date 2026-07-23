#!/usr/bin/env python3
"""B1.3 throughput measurement (SpeedTheSpire task B1.3).

Reads the per-run timing sidecars (run_<SEED>_a20_ironclad.timing.jsonl) a
campaign's driver wrote and reports sustained injected-actions/sec. The design
§2.2 floor is 5 actions/sec sustained (working target ~20/sec).

Because the driver is strictly lock-step -- exactly one command per fully
resolved ready-for-command state -- the wall time between two consecutive marks
is the time the game took to resolve one injected action. Per run,

    sustained rate = (#marks - 1) / (t_last_mono - t_first_mono)

which measures steady-state throughput (the initial `start`/menu settle is not
marked, so it is excluded). The campaign figure reported is the pooled rate
(total intervals / total marked wall time) plus the per-run median, and the
worst (slowest) run, so a single slow seed cannot hide under a good average.

Stdlib only.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import statistics


def load_marks(path: str) -> list:
    marks = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("record_kind") == "mark":
                marks.append(rec)
    return marks


def run_rate(marks: list):
    """(rate, n_actions, span_seconds) using the monotonic clock."""
    if len(marks) < 2:
        return None
    t = [m["t_mono"] for m in marks]
    span = t[-1] - t[0]
    if span <= 0:
        return None
    return (len(marks) - 1) / span, len(marks), span


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="B1.3 throughput measurement")
    ap.add_argument("--campaign-dir", required=True)
    ap.add_argument("--floor", type=float, default=5.0,
                    help="required sustained actions/sec floor (design §2.2)")
    args = ap.parse_args(argv)

    paths = sorted(glob.glob(os.path.join(args.campaign_dir,
                                          "run_*_a20_ironclad.timing.jsonl")))
    if not paths:
        print(f"no timing sidecars under {args.campaign_dir}")
        return 1

    rows = []
    pooled_intervals = 0.0
    pooled_actions = 0
    label = None
    for path in paths:
        m = re.search(r"run_(.+?)_a20_ironclad\.timing\.jsonl$",
                      os.path.basename(path))
        seed = m.group(1) if m else os.path.basename(path)
        marks = load_marks(path)
        # capture the run label from the header if present
        if label is None:
            with open(path, "r", encoding="utf-8") as fh:
                for line in fh:
                    r = json.loads(line)
                    if r.get("record_kind") == "timing_header":
                        label = r.get("run_label")
                    break
        r = run_rate(marks)
        if r is None:
            rows.append((seed, None, len(marks), None))
            continue
        rate, n, span = r
        rows.append((seed, rate, n, span))
        pooled_actions += (n - 1)
        pooled_intervals += span

    print("=" * 64)
    print(f"B1.3 throughput  campaign={os.path.basename(args.campaign_dir)}"
          f"  label={label}")
    print("-" * 64)
    print(f"{'seed':<12}{'actions':>9}{'span_s':>10}{'act/sec':>10}")
    valid = []
    for seed, rate, n, span in rows:
        if rate is None:
            print(f"{seed:<12}{n:>9}{'--':>10}{'--':>10}")
            continue
        valid.append(rate)
        print(f"{seed:<12}{n:>9}{span:>10.2f}{rate:>10.2f}")
    print("-" * 64)
    if not valid:
        print("no measurable runs")
        return 1
    pooled = pooled_actions / pooled_intervals if pooled_intervals else 0.0
    median = statistics.median(valid)
    worst = min(valid)
    best = max(valid)
    print(f"pooled sustained (total intervals / total wall): {pooled:.2f} act/s")
    print(f"per-run median: {median:.2f}   worst: {worst:.2f}   "
          f"best: {best:.2f}   runs: {len(valid)}")
    print(f"floor (design §2.2): {args.floor:.1f} act/s")
    ok = worst >= args.floor
    print("RESULT:", f"PASS -- every run >= floor" if ok else
          f"BELOW FLOOR on {sum(1 for r in valid if r < args.floor)} run(s)")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
