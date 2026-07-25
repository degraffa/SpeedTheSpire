#!/usr/bin/env python3
"""Independent A20 fixture for the Fungi Beast.

Deliberately a SECOND implementation of the decompiled behaviour, sharing no
code with the C++ monster module. One solo battery over the 32 fixed
seed-battery seeds x 20 turns: the HP roll (monsterHpRng) and the exact aiRng
draw sequence plus the move each turn executes.

takeTurn ends in a QUEUED RollMoveAction sitting AFTER the switch
(FungiBeast.java:97), so both move bodies reach it: one ai_rng.random(99) at
init() and one more per turn. getMove READS that value (its d60 threshold), so
the recorded move column is roll-driven rather than a constant.

usePreBattleAction's SporeCloudPower(this, 2) (:75-78) draws NO RNG, so it does
not appear in this fixture at all -- the C++ test asserts the power separately.

Regenerate from the repository root with:

    python3 tests/fixtures/gen_fungi_beast_fixture.py

Provenance read in full before authoring:
  FungiBeast.java:29-134; SporeCloudPower.java:14-42;
  AbstractMonster.java:431-491,705-715,765-775;
  Random.java:53-86 and RandomXS128's nextInt.
"""

import os

MASK64 = (1 << 64) - 1
NUM_SEEDS = 32
NUM_TURNS = 20

# A20 HP column: the A7 branch of the ctor (FungiBeast.java:57).
HP_MIN, HP_MAX = 24, 28

BITE, GROW = 1, 2  # FungiBeast.java:50-51


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


def get_move(h, num):
    """FungiBeast.getMove (FungiBeast.java:100-113)."""
    if num < 60:
        return GROW if h.last_two(BITE) else BITE
    return BITE if h.last(GROW) else GROW


def sequence(ai):
    h = History()
    h.push(get_move(h, ai.random_int(99)))  # AbstractMonster.init -> rollMove
    rows = []
    for _ in range(NUM_TURNS):
        executed = h.moves[-1]
        h.push(get_move(h, ai.random_int(99)))  # the trailing RollMoveAction (:97)
        rows.append((executed, ai.s0, ai.s1, ai.counter))
    return rows


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


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    seeds = load_seeds(os.path.join(root, "tests", "golden", "seed_battery.txt"))
    lines = [
        "# fungi_beast A20 solo independent fixture.",
        "# seed <label> <seed> <hp> <hp_s0> <hp_s1> <hp_counter>",
        "# turn <K> <executed_move> <ai_s0> <ai_s1> <ai_counter>",
    ]
    for label, seed in seeds:
        hp_rng = Rng(seed)
        ai = Rng(seed)
        hp = hp_rng.random_range(HP_MIN, HP_MAX)
        rows = sequence(ai)
        lines.append(
            f"seed\t{label}\t{seed}\t{hp}\t{hp_rng.s0}\t{hp_rng.s1}"
            f"\t{hp_rng.counter}")
        for k, (move, s0, s1, counter) in enumerate(rows, start=1):
            lines.append(f"turn\t{k}\t{move}\t{s0}\t{s1}\t{counter}")
    out = os.path.join(here, "fungi_beast_fixture.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(seeds)} seeds x {NUM_TURNS} turns)")


if __name__ == "__main__":
    main()
