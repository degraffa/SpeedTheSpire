#!/usr/bin/env python3
"""Independent A20 fixtures for B3.14's four small/medium slimes.

This is deliberately a second implementation of the decompiled decision trees,
not shared with the C++ monster module. It emits one TSV per class for 32 fixed
seed-battery seeds x 20 turns. Regenerate from the repository root with:

    python3 tests/fixtures/gen_slime_fixture.py

Provenance read in full before authoring:
  SpikeSlime_S.java:42-76; SpikeSlime_M.java:51-117;
  AcidSlime_S.java:45-95; AcidSlime_M.java:56-168;
  AbstractMonster.java:431-491,465-467,712-715,765-775;
  Random.java:53-86 and RandomXS128's nextInt/nextBoolean/nextFloat.
"""

import os

MASK64 = (1 << 64) - 1
NUM_SEEDS = 32
NUM_TURNS = 20


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

    def random_boolean(self):
        value = (self._next_long_bits() & 1) != 0
        self.counter += 1
        return value

    def random_boolean_chance(self, chance):
        # RandomXS128.nextFloat = (nextLong >>> 40) * 2^-24. The result is
        # exactly representable for the source's 0.5f/0.4f thresholds.
        value = (self._next_long_bits() >> 40) * (2.0 ** -24) < chance
        self.counter += 1
        return value


def last_move(history, move):
    return bool(history) and history[-1] == move


def last_two(history, move):
    return len(history) >= 2 and history[-1] == move and history[-2] == move


def decide_spike_small(ai, history):
    ai.random_int(99)  # num drawn, ignored
    history.append(1)


def decide_spike_medium(ai, history):
    num = ai.random_int(99)
    if num < 30:
        move = 4 if last_two(history, 1) else 1
    elif last_move(history, 4):
        move = 1
    else:
        move = 4
    history.append(move)


def decide_acid_small_initial(ai, history):
    ai.random_int(99)  # A17 branch ignores num
    history.append(1 if last_two(history, 1) else 2)


def decide_acid_medium(ai, history):
    num = ai.random_int(99)
    if num < 40:
        if last_two(history, 1):
            move = 2 if ai.random_boolean() else 4
        else:
            move = 1
    elif num < 80:
        if last_two(history, 2):
            move = 1 if ai.random_boolean_chance(0.5) else 4
        else:
            move = 2
    elif last_move(history, 4):
        move = 1 if ai.random_boolean_chance(0.4) else 2
    else:
        move = 4
    history.append(move)


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


def simulate(seed, hp_lo, hp_hi, variant):
    hp_rng = Rng(seed)
    ai = Rng(seed)
    hp = hp_rng.random_range(hp_lo, hp_hi)
    history = []
    if variant == "spike_small":
        decide_spike_small(ai, history)
    elif variant == "spike_medium":
        decide_spike_medium(ai, history)
    elif variant == "acid_small":
        decide_acid_small_initial(ai, history)
    else:
        decide_acid_medium(ai, history)

    turns = []
    for _ in range(NUM_TURNS):
        executed = history[-1]
        if variant == "spike_small":
            decide_spike_small(ai, history)
        elif variant == "spike_medium":
            decide_spike_medium(ai, history)
        elif variant == "acid_small":
            # takeTurn sets the next move directly: Tackle -> Lick, Lick -> Tackle.
            history.append(2 if executed == 1 else 1)
        else:
            decide_acid_medium(ai, history)
        turns.append((executed, ai.s0, ai.s1, ai.counter))
    return hp, hp_rng, turns


def write_variant(here, seeds, variant, hp_lo, hp_hi):
    lines = [
        f"# {variant} A20 independent fixture (B3.14).",
        "# seed <label> <seed> <hp> <hp_s0> <hp_s1> <hp_counter>",
        "# turn <K> <executed_move> <ai_s0> <ai_s1> <ai_counter>",
    ]
    for label, seed in seeds:
        hp, hp_rng, turns = simulate(seed, hp_lo, hp_hi, variant)
        lines.append(
            f"seed\t{label}\t{seed}\t{hp}\t{hp_rng.s0}\t{hp_rng.s1}\t{hp_rng.counter}"
        )
        for k, (move, s0, s1, counter) in enumerate(turns, start=1):
            lines.append(f"turn\t{k}\t{move}\t{s0}\t{s1}\t{counter}")
    out = os.path.join(here, f"slime_{variant}_fixture.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(seeds)} seeds x {NUM_TURNS} turns)")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    seeds = load_seeds(os.path.join(root, "tests", "golden", "seed_battery.txt"))
    write_variant(here, seeds, "spike_small", 11, 15)
    write_variant(here, seeds, "spike_medium", 29, 34)
    write_variant(here, seeds, "acid_small", 9, 13)
    write_variant(here, seeds, "acid_medium", 29, 34)


if __name__ == "__main__":
    main()
