#!/usr/bin/env python3
"""Independent A20 fixture for the Looter.

Deliberately a SECOND implementation of the decompiled behaviour, sharing no
code with the C++ monster module: one battery, solo, over the 32 fixed
seed-battery seeds -- the HP roll (monsterHpRng), the exact aiRng draw sequence,
the move each turn executes, the slashCount/steal accounting, and the escape.

Unlike the slaver batteries this one is NOT 20 turns per seed: the Looter's
takeTurn machine ENDS -- Mug, Mug, then a 50/50 into [Smoke Bomb] or [Lunge,
Smoke Bomb], then Escape on turn 4 or 5 (Looter.java:88-135) -- and once the
queued EscapeAction resolves the monster has left the fight, so a solo combat
is over and there is no sixth game-reachable turn to record. Each seed's rows
therefore run exactly to its Escape turn (4 or 5, coin-driven), and the row
count itself pins the coin.

Draw accounting per seed (all aiRng draws, in order):
  * init() rollMove -- one random(99), DISCARDED by getMove (:176-179).
  * turn 1 (Mug, slashCount 0): one randomBoolean(0.6f) -- the talk gate
    (:92); Java's && short-circuits, so ONLY the first Mug draws it.
  * turn 2 (Mug, slashCount 1 -> 2): one randomBoolean(0.5f) (:101) -- true
    telegraphs Smoke Bomb, false telegraphs Lunge.
  * every other turn: NO draw (Lunge, Smoke Bomb and Escape roll nothing; no
    takeTurn body queues a RollMoveAction).
randomBoolean(float chance) is `nextFloat() < chance` (Random.java:83-86);
nextFloat is `(nextLong() >>> 40) * 2^-24` narrowed to float32
(RandomXS128.nextFloat), reproduced bit-exactly below.

Regenerate from the repository root with:

    python3 tests/fixtures/gen_looter_fixture.py

Provenance read in full before authoring:
  Looter.java:33-193; EscapeAction.java:13-29;
  AbstractMonster.java:705-715,765-775,894-906,915-919;
  Random.java:53-101 and RandomXS128's nextInt/nextLong/nextFloat.
"""

import os
import struct

MASK64 = (1 << 64) - 1
NUM_SEEDS = 32
ASCENSION = 20


def u64(x):
    return x & MASK64


def to_i64(x):
    x &= MASK64
    return x - (1 << 64) if x >= (1 << 63) else x


def f32(x):
    """Round a Python float to float32, as Java's float literals/narrowing do."""
    return struct.unpack("f", struct.pack("f", x))[0]


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

    def random_boolean(self, chance):
        # Random.randomBoolean(float) -> nextFloat() < chance (Random.java:
        # 83-86). nextFloat: (nextLong() >>> 40) * 2^-24, narrowed to float32
        # (exact: a 24-bit integer times 2^-24 fits a float32 significand), so
        # the double comparison below equals Java's float comparison.
        bits = self._next_long_bits() >> 40
        value = bits * (2.0 ** -24)
        self.counter += 1
        return value < f32(chance)


# Move ids, straight off the class's byte constants (Looter.java:48-51).
MUG, SMOKE_BOMB, ESCAPE, LUNGE = 1, 2, 3, 4

# goldAmt: 20 from A17, 15 below (Looter.java:63). A20 -> 20.
GOLD_AMT = 20 if ASCENSION >= 17 else 15


def looter_sequence(ai):
    """init() roll + turns up to and including the Escape turn, alone.

    Rows are (executed_move, slash_count_after, stolen_gold_after, escaped_after,
    ai_s0, ai_s1, ai_counter). stolen_gold is the engine's UNCLAMPED accrual
    (steal count x goldAmt) -- the run layer clamps at settlement.
    """
    ai.random_int(99)  # init rollMove; getMove discards it (:176-179)
    move = MUG         # the forced opener
    slash = 0
    escaped = False
    rows = []
    while not escaped:
        executed = move
        if executed == MUG:
            if slash == 0:
                ai.random_boolean(0.6)          # the talk gate (:92)
            slash += 1
            if slash == 2:
                move = SMOKE_BOMB if ai.random_boolean(0.5) else LUNGE  # (:101)
            else:
                move = MUG                      # (:108)
        elif executed == LUNGE:
            slash += 1
            move = SMOKE_BOMB                   # (:117), no draw
        elif executed == SMOKE_BOMB:
            move = ESCAPE                       # (:123), no draw
        else:  # ESCAPE
            escaped = True                      # (:128-131), no draw
            move = ESCAPE                       # the re-telegraph (:131)
        rows.append((executed, slash, slash * GOLD_AMT, int(escaped),
                     ai.s0, ai.s1, ai.counter))
    return rows


# A20 HP column (the A7 branch of the ctor, Looter.java:65).
HP_RANGE = (46, 50)


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
    lo, hi = HP_RANGE
    lines = [
        "# looter A20 solo independent fixture. Rows run to the Escape turn",
        "# (4 or 5, coin-driven), not a fixed 20 -- the machine ends there.",
        "# seed <label> <seed> <hp> <hp_s0> <hp_s1> <hp_counter>",
        "# turn <K> <executed_move> <slash_count_after> <stolen_gold_after>"
        " <escaped_after> <ai_s0> <ai_s1> <ai_counter>",
    ]
    smoke_path = lunge_path = 0
    for label, seed in seeds:
        hp_rng = Rng(seed)
        ai = Rng(seed)
        hp = hp_rng.random_range(lo, hi)
        rows = looter_sequence(ai)
        if len(rows) == 4:
            smoke_path += 1
        else:
            assert len(rows) == 5
            lunge_path += 1
        lines.append(
            f"seed\t{label}\t{seed}\t{hp}\t{hp_rng.s0}\t{hp_rng.s1}"
            f"\t{hp_rng.counter}")
        for k, (move, slash, stolen, esc, s0, s1, counter) in enumerate(
                rows, start=1):
            lines.append(
                f"turn\t{k}\t{move}\t{slash}\t{stolen}\t{esc}"
                f"\t{s0}\t{s1}\t{counter}")
    # The battery must exercise BOTH sides of the coin, or the fixture is
    # vacuous for the one aiRng-driven branch the Looter has.
    assert smoke_path > 0 and lunge_path > 0, (smoke_path, lunge_path)
    out = os.path.join(here, "looter_fixture.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(seeds)} seeds; {smoke_path} smoke-bomb paths, "
          f"{lunge_path} lunge paths)")


if __name__ == "__main__":
    main()
