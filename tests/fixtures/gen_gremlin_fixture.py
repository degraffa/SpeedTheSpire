#!/usr/bin/env python3
"""Independent A20 fixtures for the five Act-1 gremlins (B3.16).

Deliberately a SECOND implementation of the decompiled behaviour, sharing no
code with the C++ monster module. Two batteries, both over the 32 fixed
seed-battery seeds:

  * one per class, solo, 20 turns -- the HP roll (monsterHpRng) and the exact
    aiRng draw sequence. Four of the five classes draw aiRng ONLY at init()
    (their getMove overrides force a fixed move and their turns re-telegraph
    with a direct setMove or a queued SetMoveAction); GremlinFat additionally
    draws once per turn because its takeTurn ends in a real RollMoveAction.
    A solo Tsundere is the empty-validMonsters branch of
    GainBlockRandomMonsterAction: NO aiRng draw, and aliveCount == 1 flips it
    to Bash after the first Protect.

  * the Tsundere PROTECT battery -- a 4-gremlin gang (Warrior, Tsundere, Thief,
    Wizard, in spawn order) where the other three stay alive, so every Protect
    turn draws one aiRng pick over the three valid allies. The gang's four
    init() rolls precede the per-turn picks on the same stream.

Regenerate from the repository root with:

    python3 tests/fixtures/gen_gremlin_fixture.py

Provenance read in full before authoring:
  GremlinWarrior.java:45-141; GremlinThief.java:44-128; GremlinFat.java:48-141;
  GremlinTsundere.java:49-134; GremlinWizard.java:47-154;
  GainBlockRandomMonsterAction.java:20-42;
  AbstractMonster.java:431-491,705-715,765-775,908-913;
  Random.java:53-86 and RandomXS128's nextInt.
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


# --- the five solo decision trees -------------------------------------------
# Each returns the move executed this turn and pushes the next telegraph. The
# aiRng draws are taken exactly where the Java takes them.

SCRATCH, PUNCTURE, BLUNT, PROTECT, BASH, DOPE_MAGIC, CHARGE = 1, 1, 2, 1, 2, 1, 2


def solo_sequence(variant, ai):
    """init() roll + NUM_TURNS turns, alone in the group."""
    ai.random_int(99)  # AbstractMonster.init -> rollMove; every getMove here
                       # ignores num and forces the opening move.
    if variant == "warrior":
        move = SCRATCH
    elif variant == "thief":
        move = PUNCTURE
    elif variant == "fat":
        move = BLUNT
    elif variant == "tsundere":
        move = PROTECT
    else:
        move = CHARGE

    charge = 1  # GremlinWizard.currentCharge field initialiser (:43)
    rows = []
    for _ in range(NUM_TURNS):
        executed = move
        if variant in ("warrior", "thief"):
            move = executed  # queued SetMoveAction re-asserting the same move
        elif variant == "fat":
            ai.random_int(99)  # the trailing RollMoveAction (:80); num ignored
            move = BLUNT
        elif variant == "tsundere":
            if executed == PROTECT:
                # validMonsters is empty when alone, so GainBlockRandomMonster
                # takes the `target = this.source` branch with NO aiRng draw
                # (GainBlockRandomMonsterAction.java:35). aliveCount == 1, so
                # the > 1 test fails and it switches to Bash (:83-88).
                move = BASH
            else:
                move = BASH  # queued SetMoveAction re-asserting Bash (:97)
        else:  # wizard
            if executed == CHARGE:
                charge += 1
                move = DOPE_MAGIC if charge == 3 else CHARGE
            else:
                charge = 0
                move = DOPE_MAGIC  # A17+ re-telegraphs the blast (:92-94)
        rows.append((executed, ai.s0, ai.s1, ai.counter))
    return rows


# A20 HP columns (the A7 branch of each ctor).
HP_RANGE = {
    "warrior": (21, 25),   # GremlinWarrior.java:49
    "thief": (11, 15),     # GremlinThief.java:48
    "fat": (14, 18),       # GremlinFat.java:52
    "tsundere": (13, 17),  # GremlinTsundere.java:53,56
    "wizard": (22, 26),    # GremlinWizard.java:52
}

# The Protect battery's gang, in spawn order. The Tsundere sits at slot 1 and
# the other three neither draw aiRng on their turns nor die, so every aiRng
# draw after the four init() rolls is a block-target pick.
GANG = ["warrior", "tsundere", "thief", "wizard"]
TSUNDERE_SLOT = 1
VALID_SLOTS = [0, 2, 3]


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
        f"# gremlin_{variant} A20 solo independent fixture (B3.16).",
        "# seed <label> <seed> <hp> <hp_s0> <hp_s1> <hp_counter>",
        "# turn <K> <executed_move> <ai_s0> <ai_s1> <ai_counter>",
    ]
    for label, seed in seeds:
        hp_rng = Rng(seed)
        ai = Rng(seed)
        hp = hp_rng.random_range(lo, hi)
        rows = solo_sequence(variant, ai)
        lines.append(
            f"seed\t{label}\t{seed}\t{hp}\t{hp_rng.s0}\t{hp_rng.s1}"
            f"\t{hp_rng.counter}")
        for k, (move, s0, s1, counter) in enumerate(rows, start=1):
            lines.append(f"turn\t{k}\t{move}\t{s0}\t{s1}\t{counter}")
    out = os.path.join(here, f"gremlin_{variant}_fixture.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(seeds)} seeds x {NUM_TURNS} turns)")


def write_protect(here, seeds):
    lines = [
        "# Gremlin Tsundere Protect target battery, A20 (B3.16). Gang spawn "
        "order: " + ", ".join(GANG) + ".",
        "# seed <label> <seed> <hp0> <hp1> <hp2> <hp3> <hp_s0> <hp_s1> "
        "<hp_counter>",
        "# turn <K> <target_slot> <ai_s0> <ai_s1> <ai_counter>",
    ]
    for label, seed in seeds:
        hp_rng = Rng(seed)
        ai = Rng(seed)
        hps = []
        for variant in GANG:
            lo, hi = HP_RANGE[variant]
            hps.append(hp_rng.random_range(lo, hi))
        for _ in GANG:
            ai.random_int(99)  # spawn_group: one init() rollMove per member
        rows = []
        for _ in range(NUM_TURNS):
            # GainBlockRandomMonsterAction.update (:30-38): the source, any
            # escaping monster and any dying monster are excluded; the rest are
            # in list order and one aiRng.random(size - 1) picks among them.
            pick = ai.random_int(len(VALID_SLOTS) - 1)
            rows.append((VALID_SLOTS[pick], ai.s0, ai.s1, ai.counter))
        hp_txt = "\t".join(str(h) for h in hps)
        lines.append(
            f"seed\t{label}\t{seed}\t{hp_txt}\t{hp_rng.s0}\t{hp_rng.s1}"
            f"\t{hp_rng.counter}")
        for k, (slot, s0, s1, counter) in enumerate(rows, start=1):
            lines.append(f"turn\t{k}\t{slot}\t{s0}\t{s1}\t{counter}")
    out = os.path.join(here, "gremlin_protect_fixture.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(seeds)} seeds x {NUM_TURNS} turns)")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    seeds = load_seeds(os.path.join(root, "tests", "golden", "seed_battery.txt"))
    for variant in ("warrior", "thief", "fat", "tsundere", "wizard"):
        write_solo(here, seeds, variant)
    write_protect(here, seeds)


if __name__ == "__main__":
    main()
