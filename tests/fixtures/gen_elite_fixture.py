#!/usr/bin/env python3
"""Independent A20 fixtures for the two Act-1 elites, Gremlin Nob and Sentry.

This is deliberately a SECOND implementation of the decompiled decision trees,
written from the Java and not shared with the C++ monster modules. It emits one
TSV per variant for the 32 fixed seed-battery seeds x 20 turns. Regenerate from
the repository root with:

    python3 tests/fixtures/gen_elite_fixture.py

Neither elite's A20 move selection READS the rollMove draw -- the Nob takes the
ascension >= 18 branch, which is a pure move-history tree, and the Sentry never
looks at `num` at any ascension. The draw still happens on every roll
(AbstractMonster.rollMove, AbstractMonster.java:465-467), so these fixtures pin
exactly that: the move sequence derived independently, and the aiRng state after
each turn proving the stream advanced by one draw and no more. The Sentry gets
two files because its OPENING move is decided by its slot parity, not by RNG
(Sentry.java:136-143), so slot 0 and slot 1 are genuinely different sequences.

Provenance read in full before authoring:
  GremlinNob.java:56-84 (ctor: setHp A8 (85,90) else (82,86); damage[0]=rushDmg,
    damage[1]=bashDmg), :86-113 (takeTurn -> unconditional RollMoveAction),
    :126-170 (getMove: forced first Bellow, then the A18 history tree);
  Sentry.java:59-77 (ctor: setHp A8 (39,45) else (38,42)), :84-113 (takeTurn ->
    unconditional RollMoveAction), :134-150 (getMove: slot-parity first move,
    then strict alternation);
  AbstractMonster.java:431-491 (moveHistory + lastMove/lastMoveBefore/
    lastTwoMoves), :465-467 (rollMove), :712-715 (init), :765-775 (setHp);
  Random.java:53-86 and RandomXS128's nextLong/nextInt.
"""

import os

MASK64 = (1 << 64) - 1
NUM_SEEDS = 32
NUM_TURNS = 20

# Move ids (GremlinNob.java:49-51 / Sentry.java:51-52).
BULL_RUSH = 1
SKULL_BASH = 2
BELLOW = 3
BOLT = 3
BEAM = 4


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


def last_move(history, move):
    return bool(history) and history[-1] == move


def last_move_before(history, move):
    return len(history) >= 2 and history[-2] == move


def last_two(history, move):
    return len(history) >= 2 and history[-1] == move and history[-2] == move


def decide_gremlin_nob(ai, history, _slot):
    # rollMove always draws; the A18 branch never reads the value.
    ai.random_int(99)
    if not history:
        history.append(BELLOW)  # usedBellow (GremlinNob.java:128-132)
        return
    if not last_move(history, SKULL_BASH) and \
            not last_move_before(history, SKULL_BASH):
        history.append(SKULL_BASH)  # GremlinNob.java:134-141
        return
    history.append(SKULL_BASH if last_two(history, BULL_RUSH) else BULL_RUSH)


def decide_sentry(ai, history, slot):
    ai.random_int(99)  # drawn, never read (Sentry.java:134-150)
    if not history:
        history.append(BOLT if slot % 2 == 0 else BEAM)
        return
    history.append(BOLT if last_move(history, BEAM) else BEAM)


VARIANTS = {
    # name: (decider, hp_lo, hp_hi, slot)
    "gremlin_nob": (decide_gremlin_nob, 85, 90, 0),
    "sentry_slot0": (decide_sentry, 39, 45, 0),
    "sentry_slot1": (decide_sentry, 39, 45, 1),
}


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


def simulate(seed, variant):
    decide, hp_lo, hp_hi, slot = VARIANTS[variant]
    hp_rng = Rng(seed)
    ai = Rng(seed)
    hp = hp_rng.random_range(hp_lo, hp_hi)  # the ctor's single setHp draw
    history = []
    decide(ai, history, slot)               # init() -> rollMove
    turns = []
    for _ in range(NUM_TURNS):
        executed = history[-1]
        decide(ai, history, slot)           # takeTurn's trailing RollMoveAction
        turns.append((executed, ai.s0, ai.s1, ai.counter))
    return hp, hp_rng, turns


def write_variant(here, seeds, variant):
    lines = [
        f"# {variant} A20 independent fixture (Act-1 elites).",
        "# seed <label> <seed> <hp> <hp_s0> <hp_s1> <hp_counter>",
        "# turn <K> <executed_move> <ai_s0> <ai_s1> <ai_counter>",
    ]
    for label, seed in seeds:
        hp, hp_rng, turns = simulate(seed, variant)
        lines.append(
            f"seed\t{label}\t{seed}\t{hp}\t{hp_rng.s0}\t{hp_rng.s1}\t{hp_rng.counter}"
        )
        for k, (move, s0, s1, counter) in enumerate(turns, start=1):
            lines.append(f"turn\t{k}\t{move}\t{s0}\t{s1}\t{counter}")
    out = os.path.join(here, f"{variant}_fixture.tsv")
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(seeds)} seeds x {NUM_TURNS} turns)")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", ".."))
    seeds = load_seeds(os.path.join(root, "tests", "golden", "seed_battery.txt"))
    for variant in VARIANTS:
        write_variant(here, seeds, variant)


if __name__ == "__main__":
    main()
