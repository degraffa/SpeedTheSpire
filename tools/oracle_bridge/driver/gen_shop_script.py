#!/usr/bin/env python3
"""Cut a per-seed `--policy script` command list from a round-A artifact,
diverging at one merchant (SpeedTheSpire wave2-capture, Track C).

The two capture rows this serves both need a purchase the live policy refuses:
greedy never opens a merchant (its SHOP_ROOM score is 'leave without walking
in'), so a Courier restock -- or a shelf purchase of any kind -- can only be
captured by a script. A full-run script cannot be written blind (combat
commands depend on the exact hand), but it does not have to be: the game is
deterministic given (seed, command prefix), so a round-A artifact's own
ordered commands, replayed verbatim, walk the run into the SAME merchant with
the SAME shelf, and the script only diverges from there.

    prefix = every round-A action_command STRICTLY BEFORE the first command
             issued against the SHOP_ROOM screen of the target floor
             (that command was greedy's `proceed`; the room is already
             entered and the merchant already built by then -- stock is
             rolled at ROOM ENTRY, not at screen open)
    + `choose shop`                    (the SHOP_ROOM screen's one row)
    + `choose <name>` per purchase     (the shop choice list is the
             AFFORDABLE unsold rows by LOWERCASED display name --
             b48_shop_spotdiff.md section 6; `choose` matches the exact
             choice string first, PROTOCOL.md section 2)
    + trailing `state` no-ops so every post-purchase dump lands in an
      ordinary action record rather than only in the terminal marker.

The script then simply runs out (`script_exhausted`), which is a valid
terminal: the capture's subject is the merchant, and everything after the
last purchase is already recorded.

Stdlib only, like the sibling extract_scripts.py.
"""
from __future__ import annotations

import argparse
import json

MARKER = "__terminal_observed__"


def load_actions(path: str) -> list:
    recs = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("record_kind") != "action":
                continue
            if rec.get("action_command") == MARKER:
                continue
            recs.append(rec)
    return recs


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="Cut a shop-diverging script from a round-A artifact")
    ap.add_argument("--artifact", required=True,
                    help="round-A run_<SEED>_a20_ironclad.jsonl")
    ap.add_argument("--shop-floor", type=int, required=True,
                    help="floor of the SHOP_ROOM screen to diverge at")
    ap.add_argument("--buy", required=True,
                    help="comma-separated choice names, sent in order after "
                         "`choose shop` (lowercased display names)")
    ap.add_argument("--out", required=True, help="script file to write")
    ap.add_argument("--trailing-states", type=int, default=2,
                    help="`state` no-ops appended after the last purchase")
    args = ap.parse_args(argv)

    recs = load_actions(args.artifact)
    cut = None
    for i, rec in enumerate(recs):
        gs = (rec.get("state_json") or {}).get("game_state") or {}
        if gs.get("screen_type") == "SHOP_ROOM" and \
                gs.get("floor") == args.shop_floor:
            cut = i
            break
    if cut is None:
        print(f"no SHOP_ROOM screen at floor {args.shop_floor} in "
              f"{args.artifact}")
        return 1

    cmds = [rec["action_command"] for rec in recs[:cut]]
    cmds.append("choose shop")
    for name in args.buy.split(","):
        name = name.strip()
        if name:
            cmds.append(f"choose {name}")
    cmds.extend(["state"] * args.trailing_states)

    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(f"# wave2cap round-B script: {args.artifact} prefix "
                 f"(first {cut} commands), diverging at the floor-"
                 f"{args.shop_floor} merchant\n")
        fh.write(f"# purchases: {args.buy}\n")
        for c in cmds:
            fh.write(c + "\n")
    print(f"wrote {len(cmds)} commands to {args.out} (prefix {cut})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
