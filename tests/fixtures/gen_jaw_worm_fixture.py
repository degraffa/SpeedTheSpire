#!/usr/bin/env python3
# Hand-derived fixture generator for task A3.2 (Jaw Worm AI + monster turn).
#
# This is an INDEPENDENT oracle: it re-derives the game's RNG primitive and the
# Jaw Worm decision tree from scratch in Python, so a translation bug in the C++
# engine (include/sts/engine/monster_jaw_worm.*) has a real chance of being
# caught by a diff. It is NOT a refactor of the C++ code -- it is a second
# implementation of the same frozen spec in a different language. The generated
# fixture (jaw_worm_fixture.tsv) is consumed at runtime by tests/jaw_worm_test.cpp.
#
# Nothing here depends on anything outside the Python standard library, so it
# runs in either WSL or the Windows host. It is a ONE-TIME authoring step, not
# wired into CMake/CI (mirrors tools/golden_capture/README.md's spirit).
#
# Regen command (from the repo root, either environment):
#     python3 tests/fixtures/gen_jaw_worm_fixture.py
# It reads the 32 fixed-random seeds r0..r31 from tests/golden/seed_battery.txt
# (so the fixture stays tied to the rest of the suite's seed battery) and writes
# tests/fixtures/jaw_worm_fixture.tsv next to this script. Deterministic: a
# second run produces a byte-identical file.
#
# ---------------------------------------------------------------------------
# PROVENANCE / REVIEW (cross-checked against the decompiled Java one more time
# after writing this script -- see D:\STS_BG_Mod\SlayTheSpireDecompiled):
#
#   RandomXS128 (libGDX xorshift128+): com.badlogic.gdx.math.RandomXS128 --
#     setSeed (seed==0 -> Long.MIN_VALUE bits before murmur3; :94-97), murmur3
#     (:108-115), nextLong (:27-35), nextLong(n) rejection loop (:52-62),
#     nextFloat = (float)((double)(nextLong()>>>40) * NORM_FLOAT) (:69-72).
#     Design doc stage-a-design.md §3.1; C++ reference include/sts/engine/
#     rng_xs128.hpp (already golden-tested at gate G1).
#
#   Random wrapper: com.megacrit.cardcrawl.random.Random -- random(int range)
#     == nextInt(range+1) INCLUSIVE (:53-56), random(int,int) inclusive both
#     ends (:58-61), randomBoolean(float) == nextFloat() < chance (:83-86).
#     Design doc §3.2; C++ reference include/sts/engine/rng_stream.hpp.
#
#   HP roll: AbstractMonster.setHp(min,max) == monsterHpRng.random(min,max),
#     one inclusive draw, currentHealth = maxHealth (AbstractMonster.java:765-775).
#     JawWorm at ascension>=7 uses setHp(42,46) (JawWorm.java:81-82). A20 branch.
#
#   Move decision tree: JawWorm.getMove(int num) (JawWorm.java:148-184), rolled
#     by AbstractMonster.rollMove() == getMove(aiRng.random(99)) (:465-467).
#     firstMove forces CHOMP and RETURNS before using num, but the random(99)
#     draw is still consumed (rollMove already drew it). init() calls rollMove()
#     once before the first turn (:712-715). JawWorm.takeTurn() ends with an
#     unconditional RollMoveAction -> rollMove() for the NEXT turn (:145).
#     setMove appends nextMove to moveHistory at DECISION time (AbstractMonster
#     .setMove, :431-437). lastMove(m): history non-empty & most-recent==m
#     (:469-474). lastTwoMoves(m): size>=2 & the TWO most-recent both==m
#     (:486-491). Move ids CHOMP=1, BELLOW=2, THRASH=3 (JawWorm.java:65-67).
#     The randomBoolean tiebreak literals 0.5625f / 0.357f / 0.416f are the
#     verbatim source constants (:157,167,176), NOT the rounded percentages in
#     design doc §9's prose.
#
# DRAW-COUNTING CONVENTION (documented so the C++ driver and this script agree
# to the draw): init = decision #1 (forced Chomp: 1 aiRng.random(99) draw, value
# IGNORED). Each of the 20 take-turn steps executes turn K's move then rolls
# decision #(K+1): always 1 aiRng.random(99), PLUS on the tiebreak branches 1
# aiRng.randomBoolean draw. So aiRng.counter after turn K is VARIABLE (it is
# recorded per turn). monsterHpRng is drawn exactly once (at init), counter==1.
# Total aiRng.random(99) draws across the 20-turn window = 21 (decisions #1..#21;
# decision #21 rolls a turn-21 move that is never executed but is faithfully rolled
# by turn 20's RollMoveAction). ---------------------------------------------------

import os
import struct

MASK64 = (1 << 64) - 1
CHOMP, BELLOW, THRASH = 1, 2, 3
NUM_SEEDS = 32
NUM_TURNS = 20


def u64(x):
    return x & MASK64


def to_i64(x):
    x &= MASK64
    return x - (1 << 64) if x >= (1 << 63) else x


def f32(x):
    # Round a Python double to nearest IEEE-754 binary32, returned as a double
    # that exactly equals that float32 value (so comparisons match C++ float<float).
    return struct.unpack("<f", struct.pack("<f", x))[0]


def murmur3(x):
    x = u64(x)
    x ^= x >> 33
    x = u64(x * 0xFF51AFD7ED558CCD)
    x ^= x >> 33
    x = u64(x * 0xC4CEB9FE1A85EC53)
    x ^= x >> 33
    return x


class Rng:
    """RandomXS128 + the game's Random-wrapper draw counter, in one object."""

    def __init__(self, seed):
        seed_bits = 0x8000000000000000 if seed == 0 else u64(seed)
        s0 = murmur3(seed_bits)
        self.s0 = s0
        self.s1 = murmur3(s0)
        self.counter = 0

    # --- xorshift128+ primitive (raw 64-bit result) ---
    def _next_long_bits(self):
        s1 = self.s0
        s0 = self.s1
        self.s0 = s0
        s1 ^= u64(s1 << 23)
        self.s1 = u64(s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))
        return u64(self.s1 + s0)

    def _next_long_n(self, n):
        # nextLong(n) rejection loop (trap 4 -- do NOT reduce to a modulo).
        un = n
        while True:
            bits = self._next_long_bits() >> 1
            value = bits % un
            check = u64(bits - value + (un - 1))
            if to_i64(check) >= 0:
                return value

    def _next_float(self):
        bits = self._next_long_bits() >> 40
        product = float(bits) * 5.960464477539063e-8  # * 2^-24 (double), NORM_FLOAT
        return f32(product)

    # --- Random-wrapper methods (one logical draw each; counter += 1) ---
    def random_int(self, range_):
        # random(int range) == nextInt(range+1), INCLUSIVE upper bound.
        v = self._next_long_n(range_ + 1)
        self.counter += 1
        return v  # (int) narrowing is identity for these small non-negatives

    def random_range(self, start, end):
        # random(int start, int end), inclusive both ends.
        v = start + self._next_long_n(end - start + 1)
        self.counter += 1
        return v

    def random_boolean_p(self, chance):
        # randomBoolean(float chance) == nextFloat() < chance (both float32).
        v = self._next_float() < f32(chance)
        self.counter += 1
        return v


def last_move(history, m):
    return len(history) >= 1 and history[-1] == m


def last_two_moves(history, m):
    return len(history) >= 2 and history[-1] == m and history[-2] == m


def decide_next_move(ai, history):
    """JawWorm.getMove for a non-first move: 1 random(99) + maybe 1 tiebreak."""
    num = ai.random_int(99)  # 0..99
    if num < 25:
        if last_move(history, CHOMP):
            chosen = BELLOW if ai.random_boolean_p(0.5625) else THRASH
        else:
            chosen = CHOMP
    elif num < 55:
        if last_two_moves(history, THRASH):
            chosen = CHOMP if ai.random_boolean_p(0.357) else BELLOW
        else:
            chosen = THRASH
    else:
        if last_move(history, BELLOW):
            chosen = CHOMP if ai.random_boolean_p(0.416) else THRASH
        else:
            chosen = BELLOW
    history.append(chosen)  # setMove appends at decision time
    return chosen


def load_seeds(battery_path):
    """The 32 fixed-random seeds r0..r31 from tests/golden/seed_battery.txt."""
    seeds = []
    with open(battery_path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            label, value = line.split("\t")
            if label.startswith("r"):
                seeds.append((label, int(value)))
    seeds.sort(key=lambda lv: int(lv[0][1:]))  # r0, r1, ... r31 numeric order
    assert len(seeds) == NUM_SEEDS, f"expected {NUM_SEEDS} r* seeds, got {len(seeds)}"
    return seeds


def simulate(seed):
    ai = Rng(seed)
    hp_rng = Rng(seed)

    hp = hp_rng.random_range(42, 46)  # setHp(42,46): one inclusive draw

    history = []
    ai.random_int(99)          # init/rollMove decision #1: drawn but IGNORED
    history.append(CHOMP)      # firstMove forces CHOMP, appended to history

    turns = []
    for _k in range(1, NUM_TURNS + 1):
        move_k = history[-1]                 # turn k executes the current head
        decide_next_move(ai, history)        # roll decision #(k+1)
        turns.append((move_k, ai.s0, ai.s1, ai.counter))

    return {
        "hp": hp,
        "hp_s0": hp_rng.s0,
        "hp_s1": hp_rng.s1,
        "hp_counter": hp_rng.counter,
        "turns": turns,
    }


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(here, "..", ".."))
    battery = os.path.join(repo_root, "tests", "golden", "seed_battery.txt")
    out_path = os.path.join(here, "jaw_worm_fixture.tsv")

    seeds = load_seeds(battery)

    lines = []
    lines.append("# Jaw Worm A20 hand-derived fixture (task A3.2).")
    lines.append("# Generated by tests/fixtures/gen_jaw_worm_fixture.py -- do not edit by hand.")
    lines.append("# Draw-counting convention: init = decision #1 (forced Chomp, 1 aiRng.random(99)")
    lines.append("# draw IGNORED). Each of the 20 turns executes turn K then rolls decision #(K+1):")
    lines.append("# 1 aiRng.random(99) draw + (on tiebreak branches) 1 aiRng.randomBoolean draw, so")
    lines.append("# aiRng.counter after turn K is variable. monsterHpRng drawn once at init (counter 1).")
    lines.append("# Move ids: CHOMP=1, BELLOW=2, THRASH=3. s0/s1 are unsigned decimal uint64.")
    lines.append("# Rows:")
    lines.append("#   seed\t<label>\t<seed_int64>")
    lines.append("#   hp\t<hp>\t<hprng_s0>\t<hprng_s1>\t<hprng_counter>")
    lines.append("#   turn\t<K>\t<move_id>\t<airng_s0>\t<airng_s1>\t<airng_counter>")
    for label, seed in seeds:
        r = simulate(seed)
        lines.append(f"seed\t{label}\t{seed}")
        lines.append(f"hp\t{r['hp']}\t{r['hp_s0']}\t{r['hp_s1']}\t{r['hp_counter']}")
        for k, (move, s0, s1, ctr) in enumerate(r["turns"], start=1):
            lines.append(f"turn\t{k}\t{move}\t{s0}\t{s1}\t{ctr}")

    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")

    print(f"wrote {out_path} ({len(seeds)} seeds x {NUM_TURNS} turns)")


if __name__ == "__main__":
    main()
