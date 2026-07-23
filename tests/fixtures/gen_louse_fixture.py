#!/usr/bin/env python3
# Hand-derived fixture generator for B3.13 (Louse AI + monster turn + curl-up).
#
# An INDEPENDENT oracle re-deriving the game's RNG + the Louse decision tree from
# scratch (a SECOND implementation of the frozen spec, not a refactor of the
# C++), consumed by tests/louse_test.cpp. Emits BOTH variants -- LouseNormal and
# LouseDefensive share getMove exactly (identical move sequence + aiRng), but the
# monsterHpRng stream DIVERGES because their setHp ranges differ (the nextLong(n)
# rejection loop consumes different internal state), so bite + curl-up rolls land
# on different stream positions -> separate fixture files. One-time authoring
# step (not wired into CMake), mirroring gen_jaw_worm_fixture.py.
#
# Regen command (repo root, either environment):
#     python3 tests/fixtures/gen_louse_fixture.py
# Reads tests/golden/seed_battery.txt; writes louse_normal_fixture.tsv and
# louse_defensive_fixture.tsv next to this script. Deterministic.
#
# PROVENANCE (cross-checked vs D:\STS_BG_Mod\SlayTheSpireDecompiled):
#   RandomXS128 / Random wrapper: as in gen_jaw_worm_fixture.py (RNG unchanged).
#   ctor (LouseNormal.java:50-62 / LouseDefensive.java:53-65): setHp (A7 column at
#     A20: Normal (11,16), Defensive (12,18)) THEN biteDamage =
#     monsterHpRng.random(6,8) at A20 (>=2). Two monsterHpRng draws in the ctor.
#   usePreBattleAction (:64-73 / :67-76): CurlUpPower(monsterHpRng.random(9,12)) at
#     A20 (A17 branch). A THIRD monsterHpRng draw, in the pre-battle phase.
#   getMove (:128-153 / :131-156), A20 -> A17 branch: BITE=3, move4=4.
#     num<25 -> lastMove(4)? BITE : move4; elif lastTwoMoves(3) -> move4 else BITE.
#     One aiRng.random(99) draw, num USED, NO tiebreak booleans. No forced first
#     move: init's rollMove runs the real tree.
#
# DRAW-COUNTING: init = 2 monsterHpRng (HP, bite) + 1 aiRng.random(99) (decision
# #1, num USED). pre-battle = 1 monsterHpRng (curl-up). Each of the 20 turns = 1
# aiRng.random(99) (decision #(K+1)). aiRng.counter advances by exactly 1 per turn.

import os

MASK64 = (1 << 64) - 1
BITE, MOVE4 = 3, 4
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
    def __init__(self, seed):
        seed_bits = 0x8000000000000000 if seed == 0 else u64(seed)
        s0 = murmur3(seed_bits)
        self.s0 = s0
        self.s1 = murmur3(s0)
        self.counter = 0

    def _next_long_bits(self):
        s1 = self.s0
        s0 = self.s1
        self.s0 = s0
        s1 ^= u64(s1 << 23)
        self.s1 = u64(s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))
        return u64(self.s1 + s0)

    def _next_long_n(self, n):
        un = n
        while True:
            bits = self._next_long_bits() >> 1
            value = bits % un
            check = u64(bits - value + (un - 1))
            if to_i64(check) >= 0:
                return value

    def random_int(self, range_):
        v = self._next_long_n(range_ + 1)
        self.counter += 1
        return v

    def random_range(self, start, end):
        v = start + self._next_long_n(end - start + 1)
        self.counter += 1
        return v


def last_move(history, m):
    return len(history) >= 1 and history[-1] == m


def last_two_moves(history, m):
    return len(history) >= 2 and history[-1] == m and history[-2] == m


def decide(ai, history):
    """Louse.getMove A17 branch (A20): 1 random(99), num used, no tiebreaks."""
    num = ai.random_int(99)
    if num < 25:
        chosen = BITE if last_move(history, MOVE4) else MOVE4
    elif last_two_moves(history, BITE):
        chosen = MOVE4
    else:
        chosen = BITE
    history.append(chosen)
    return chosen


def load_seeds(battery_path):
    seeds = []
    with open(battery_path, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            label, value = line.split("\t")
            if label.startswith("r"):
                seeds.append((label, int(value)))
    seeds.sort(key=lambda lv: int(lv[0][1:]))
    assert len(seeds) == NUM_SEEDS, f"expected {NUM_SEEDS} r* seeds, got {len(seeds)}"
    return seeds


def simulate(seed, hp_lo, hp_hi):
    ai = Rng(seed)
    hp_rng = Rng(seed)

    hp = hp_rng.random_range(hp_lo, hp_hi)   # setHp: draw #1
    bite = hp_rng.random_range(6, 8)         # bite: draw #2 (in ctor)
    hp_after_ctor = (hp_rng.s0, hp_rng.s1, hp_rng.counter)
    curl = hp_rng.random_range(9, 12)        # curl-up: draw #3 (pre-battle)
    hp_after_curl = (hp_rng.s0, hp_rng.s1, hp_rng.counter)

    history = []
    decide(ai, history)                      # init rollMove: decision #1, num USED
    turns = []
    for _k in range(1, NUM_TURNS + 1):
        move_k = history[-1]
        decide(ai, history)                  # decision #(k+1)
        turns.append((move_k, ai.s0, ai.s1, ai.counter))

    return {
        "hp": hp, "bite": bite, "curl": curl,
        "hp_after_ctor": hp_after_ctor, "hp_after_curl": hp_after_curl,
        "turns": turns,
    }


def write_variant(here, seeds, out_name, title, hp_lo, hp_hi):
    lines = []
    lines.append(f"# {title} A20 hand-derived fixture (task B3.13).")
    lines.append("# Generated by tests/fixtures/gen_louse_fixture.py -- do not edit by hand.")
    lines.append("# Draw-counting: init = 2 monsterHpRng (HP, bite) + 1 aiRng.random(99)")
    lines.append("# (decision #1, num USED). pre-battle = 1 monsterHpRng (curl-up). Each of")
    lines.append("# the 20 turns = 1 aiRng.random(99); aiRng.counter advances by 1 per turn.")
    lines.append("# Move ids: BITE=3, STRENGTHEN/WEAKEN=4. s0/s1 are unsigned decimal uint64.")
    lines.append("# Rows:")
    lines.append("#   seed\t<label>\t<seed_int64>")
    lines.append("#   hp\t<hp>\t<bite>\t<hprng_s0>\t<hprng_s1>\t<hprng_counter>   (after 2 ctor draws)")
    lines.append("#   curl\t<curl_block>\t<hprng_s0>\t<hprng_s1>\t<hprng_counter>  (after 3rd draw)")
    lines.append("#   turn\t<K>\t<move_id>\t<airng_s0>\t<airng_s1>\t<airng_counter>")
    for label, seed in seeds:
        r = simulate(seed, hp_lo, hp_hi)
        c = r["hp_after_ctor"]
        u = r["hp_after_curl"]
        lines.append(f"seed\t{label}\t{seed}")
        lines.append(f"hp\t{r['hp']}\t{r['bite']}\t{c[0]}\t{c[1]}\t{c[2]}")
        lines.append(f"curl\t{r['curl']}\t{u[0]}\t{u[1]}\t{u[2]}")
        for k, (move, s0, s1, ctr) in enumerate(r["turns"], start=1):
            lines.append(f"turn\t{k}\t{move}\t{s0}\t{s1}\t{ctr}")
    out_path = os.path.join(here, out_name)
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out_path} ({len(seeds)} seeds x {NUM_TURNS} turns)")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(here, "..", ".."))
    battery = os.path.join(repo_root, "tests", "golden", "seed_battery.txt")
    seeds = load_seeds(battery)
    # A7 HP columns at A20 (LouseNormal.java:55-56 / LouseDefensive.java:59-60).
    write_variant(here, seeds, "louse_normal_fixture.tsv",
                  "LouseNormal", 11, 16)
    write_variant(here, seeds, "louse_defensive_fixture.tsv",
                  "LouseDefensive", 12, 18)


if __name__ == "__main__":
    main()
