#!/usr/bin/env python3
"""Independent A20 fixtures for the two Act-1 slavers.

Deliberately a SECOND implementation of the decompiled behaviour, sharing no
code with the C++ monster module. One battery per class, solo, over the 32 fixed
seed-battery seeds x 20 turns: the HP roll (monsterHpRng) and the exact aiRng
draw sequence plus the move each turn executes.

Both classes end takeTurn in a QUEUED RollMoveAction (SlaverBlue.java:89,
SlaverRed.java:110), so each draws one ai_rng.random(99) at init() and one more
per turn -- and, unlike the gremlins, BOTH getMove overrides READ that value.
The recorded move column is therefore roll-driven, not a constant, which is what
makes these fixtures a real check on the decision trees rather than on a count.

Regenerate from the repository root with:

    python3 tests/fixtures/gen_slaver_fixture.py

Provenance read in full before authoring:
  SlaverBlue.java:27-136; SlaverRed.java:32-173;
  AbstractMonster.java:431-491,705-715,765-775;
  Random.java:53-86 and RandomXS128's nextInt.
"""

import os

MASK64 = (1 << 64) - 1
NUM_SEEDS = 32
NUM_TURNS = 20
ASCENSION = 20


def u64(x):
    return x & MASK64


def to_i64(x):
    x &= MASK64
    return x - (1 << 64) if x >= (1 << 63) else x


def murmur3(x):
    x = u64(x)
    x ^= x >> 33
    x = u64(x * 0xFF51AFD7ED558CCD)
    x ^= x >> 33
    x = u64(x * 0xC4CEB9FE1A85EC53)
    x ^= x >> 33
    return x


class Rng:
    """Independent RandomXS128 + game Random wrapper counter."""

    def __init__(self, seed):
        seed_bits = 0x8000000000000000 if seed == 0 else u64(seed)
        self.s0 = murmur3(seed_bits)
        self.s1 = murmur3(self.s0)
        self.counter = 0

    def _next_long_bits(self):
        s1 = self.s0
        s0 = self.s1
        self.s0 = s0
        s1 ^= u64(s1 << 23)
        self.s1 = u64(s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))
        return u64(self.s1 + s0)

    def _next_long_n(self, n):
        while True:
            bits = self._next_long_bits() >> 1
            value = bits % n
            check = u64(bits - value + (n - 1))
            if to_i64(check) >= 0:
                return value

    def random_int(self, inclusive_range):
        value = self._next_long_n(inclusive_range + 1)
        self.counter += 1
        return value

    def random_range(self, start, end):
        value = start + self._next_long_n(end - start + 1)
        self.counter += 1
        return value


# Move ids, straight off the classes' byte constants.
BLUE_STAB, BLUE_RAKE = 1, 4                     # SlaverBlue.java:45-46
RED_STAB, RED_ENTANGLE, RED_SCRAPE = 1, 2, 3    # SlaverRed.java:50-52


class History:
    """AbstractMonster.moveHistory, newest last (lastMove/lastTwoMoves:469-491)."""

    def __init__(self):
        self.moves = []

    def push(self, move):
        self.moves.append(move)

    def last(self, move):
        return len(self.moves) >= 1 and self.moves[-1] == move

    def last_two(self, move):
        return len(self.moves) >= 2 and self.moves[-1] == move \
            and self.moves[-2] == move


def blue_get_move(h, num):
    """SlaverBlue.getMove (SlaverBlue.java:110-129)."""
    if num >= 40 and not h.last_two(BLUE_STAB):
        return BLUE_STAB
    if ASCENSION >= 17:
        return BLUE_RAKE if not h.last(BLUE_RAKE) else BLUE_STAB
    return BLUE_RAKE if not h.last_two(BLUE_RAKE) else BLUE_STAB


def red_get_move(h, num, used_entangle, first_turn):
    """SlaverRed.getMove (SlaverRed.java:138-166)."""
    if first_turn:
        return RED_STAB                                  # (:140-143)
    if num >= 75 and not used_entangle:
        return RED_ENTANGLE                              # (:145-147)
    if num >= 55 and used_entangle and not h.last_two(RED_STAB):
        return RED_STAB                                  # (:149-152)
    if ASCENSION >= 17:
        return RED_SCRAPE if not h.last(RED_SCRAPE) else RED_STAB
    return RED_SCRAPE if not h.last_two(RED_SCRAPE) else RED_STAB


def blue_sequence(ai):
    """init() roll + NUM_TURNS turns, alone in the group."""
    h = History()
    h.push(blue_get_move(h, ai.random_int(99)))  # AbstractMonster.init -> rollMove
    rows = []
    for _ in range(NUM_TURNS):
        executed = h.moves[-1]
        # takeTurn's trailing RollMoveAction (:89) -- queued, so it resolves
        # after the turn's damage/power actions, but nothing else draws aiRng in
        # between for a solo slaver.
        h.push(blue_get_move(h, ai.random_int(99)))
        rows.append((executed, ai.s0, ai.s1, ai.counter))
    return rows


def red_sequence(ai):
    h = History()
    ai.random_int(99)                 # drawn, then discarded by the firstTurn branch
    h.push(RED_STAB)
    used_entangle = False
    rows = []
    for _ in range(NUM_TURNS):
        executed = h.moves[-1]
        if executed == RED_ENTANGLE:
            used_entangle = True      # set synchronously in takeTurn (:90)
        h.push(red_get_move(h, ai.random_int(99), used_entangle, False))
        rows.append((executed, ai.s0, ai.s1, ai.counter))
    return rows


# A20 HP columns (the A7 branch of each ctor).
HP_RANGE = {
    "blue": (48, 52),  # SlaverBlue.java:51
    "red": (48, 52),   # SlaverRed.java:61
}
SEQUENCE = {"blue": blue_sequence, "red": red_sequence}


def load_seeds(path):
    seeds = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            label, value = line.split("\t")
            if label.startswith("r"):
                seeds.append((label, int(value)))
    seeds.sort(key=lambda item: int(item[0][1:]))
    assert len(seeds) == NUM_SEEDS
    return seeds


def write_solo(here, seeds, variant):
    lo, hi = HP_RANGE[variant]
    lines = [
        f"# slaver_{variant} A20 solo independent fixture.",
        "# seed <label> <seed> <hp> <hp_s0> <hp_s1> <hp_counter>",
        "# turn <K> <executed_move> <ai_s0> <ai_s1> <ai_counter>",
    ]
    for label, seed in seeds:
        hp_rng = Rng(seed)
        ai = Rng(seed)
        hp = hp_rng.random_range(lo, hi)
        rows = SEQUENCE[variant](ai)
        lines.append(
            f"seed\t{label}\t{seed}\t{hp}\t{hp_rng.s0}\t{hp_rng.s1}"
            f"\t{hp_rng.counter}")
        for k, (move, s0, s1, counter) in enumerate(rows, start=1):
            lines.append(f"turn\t{k}\t{move}\t{s0}\t{s1}\t{counter}")
    out = os.path.join(here, f"slaver_{variant}_fixture.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(seeds)} seeds x {NUM_TURNS} turns)")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    seeds = load_seeds(os.path.join(root, "tests", "golden", "seed_battery.txt"))
    for variant in ("blue", "red"):
        write_solo(here, seeds, variant)


if __name__ == "__main__":
    main()
