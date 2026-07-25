# A6.2 combat fixtures — derivation notes

Hand-arithmetic for the 20 scripted Jaw Worm fights whose expected per-action
state traces live beside this file (`*.trace`). These traces are produced by the
independent reference simulator `tools/fixture_gen/gen_combat_fixtures.cpp` and
checked, after **every** action, against the real engine (`combat_begin` +
`advance`) by `tests/fixture_oracle_test.cpp`. Acceptance: **zero diffs on all 20**
(ledger A6.2). This document lets a human re-verify the numbers against the cited
Java **without** re-deriving the RNG.

> **Every entry below has been re-derived against
> `gen_combat_fixtures --dump <name>`**, which prints the real turn-by-turn
> state and is the authority whenever this prose and a trace disagree. The
> narration once predated the end-of-turn hand discard and therefore named
> turn-2 cards out of a hand that no longer exists; that drift is now cleared
> across the whole corpus, and the per-fixture arithmetic and the coverage
> table below both match the checked-in traces. Keep it that way: a script
> change means re-reading its `--dump` output, not patching the numbers by
> hand.

## Model (re-derived from the spec, cited)

- **Player:** 80 HP (placeholder per design §11), 3 energy/turn, opening hand 5.
  Energy is **SET** to 3 at the start of every turn, never added (the Ironclad's
  `new EnergyManager(3)`, Ironclad.java:68, applied by `EnergyManager.recharge`
  → `EnergyPanel.setEnergy`, EnergyManager.java:25-41). A turn therefore buys at
  most 3 energy of cards. A card the player cannot pay for is refused outright
  by `AbstractCard.hasEnoughEnergy` (AbstractCard.java:888) — it is not played at
  a discount, and energy never goes negative (`EnergyPanel.useEnergy` clamps at
  0, EnergyPanel.java:71-74). **The generator enforces this at generation time**
  and aborts on a script that overspends; see the fixt13 note below.
- **A `Play` must name a slot that exists.** `queue_card_play` refuses an index
  past the hand and `advance()` no-ops the action, so an out-of-range slot is
  invisible to the zero-diff check — the reference simulator does nothing too,
  and the two agree perfectly about a fixture that stopped doing what its name
  says. The generator therefore **aborts generation** on one, naming the fixture,
  the action index, the slot and the hand size; see the fixt16 note below.
- **End of turn discards the hand.** `DiscardAtEndOfTurnAction` moves every
  ordinary (non-ethereal, non-Retain) hand card to the discard pile before the
  monster acts, so the hand does **not** persist across turns — each turn plays
  out of the 5 cards drawn at its start.
- **Cards** (`cards.hpp`; Strike_Red/Defend_Red/Bash/ShrugItOff/PommelStrike.java):
  - Strike 1E → DAMAGE 6
  - Defend 1E → BLOCK 5
  - Bash 2E → DAMAGE 8, **then** Vulnerable 2 (same target; damage lands BEFORE Vuln)
  - Shrug It Off 1E → BLOCK 8, then DRAW 1
  - Pommel Strike 1E → DAMAGE 9, then DRAW 1
- **Damage pipeline** (DamageInfo.applyPowers, design §5.5; float, floor once):
  - Player → monster: `floor(base × 1.5)` if the monster is Vulnerable, else `base`
    (the player carries no Strength/Weak in the skeleton). Then the monster's
    **block** absorbs, remainder hits HP (clamp ≥ 0). `6→9, 8→12, 9→13` under Vuln.
  - Monster → player: `base + monsterStrength` (the player carries no Vulnerable;
    the monster carries no Weak). Then the player's block absorbs, remainder to HP.
- **Jaw Worm A20** (JawWorm.java; `monster_jaw_worm.hpp`): HP roll ∈ [42,46] per
  seed. Chomp = 12 dmg. Bellow = +5 Strength then +9 block (0 damage). Thrash =
  7 dmg then +5 block. Strength/Vulnerable/block **never decay** in the skeleton
  (no power-decay hook; monster block persists — both established by `cards_test`).
- **Turn boundary** (`action_queue.cpp`): on END_TURN the monster acts FIRST, so
  *this* turn's player block absorbs the hit; **then** start-of-turn zeroes the
  player's block, refills energy to 3, `++turn`, and draws 5 (capped at hand 10,
  reshuffling the discard via `shuffle_rng` if the draw pile empties mid-draw).
  If the monster's hit kills the player, the pump halts at COMBAT_OVER: no
  start-of-turn runs. Every death fixture kills on a single-effect move
  (Chomp) / single-effect card (Strike) so no queued effect is left behind.
- **Seeds:** each fight's `run_seed = <rN fixture seed> − 1`, so
  `floor_stream(run_seed, 1) == from_seed(<rN seed>)` and the combat's
  ai_rng/monster_hp_rng land exactly on the A3.2 fixture row `rN`
  (`tests/fixtures/jaw_worm_fixture.tsv`) — the sanctioned monster oracle. The
  opening deck shuffle uses the G1-golden RNG primitives (independently verified,
  like `piles_test`). Move sequences below are read from that fixture.

Notation: `mon` = Jaw Worm HP; `blk` = block; `Str`/`Vuln` = monster powers.

---

## fixt01_r0_strike — Strike + Chomp (2 actions)
r0: mon HP 44; opening hand `Defend6 Strike3 Defend8 Bash9 Defend7`; moves C,B,…
- T1: **Strike** (hand[1]) → 6 dmg, mon 44→38. energy 3→2.
- **END**: monster T1 **Chomp** 12, player unblocked → HP 80→68. The 4-card hand is
  discarded (discard 5). Start T2: energy 3, block 0, draw 5 (draw 7→2) → **hand 5**.
  ai_rng → r0 turn 1.

## fixt02_r0_defend — Defend absorbs Chomp (2 actions)
r0: mon 44; hand as above.
- T1: **Defend** (hand[0]) → block +5. energy 3→2.
- **END**: **Chomp** 12 vs block 5 → HP 80→73 (12−5). Start T2: block 0.

## fixt03_r2_pommel — Pommel + Strike (3 actions)
r2: mon HP 46; hand `Pommel11 Strike0 Strike1 Strike2 Defend8`; moves C,B,T,…
- T1: **Pommel** (hand[0]) → 9 dmg, mon 46→37, draw 1. energy 3→2.
- **Strike** (hand[0]) → 6 dmg, mon 37→31. energy 2→1.
- **END**: **Chomp** 12 → HP 80→68.

## fixt04_r7_shrug — Shrug block + Chomp (2 actions)
r7: mon HP 46; hand `Shrug10 Defend6 Strike1 Pommel11 Strike3`; moves C,B,T,…
- T1: **Shrug** (hand[0]) → block +8, draw 1. energy 3→2.
- **END**: **Chomp** 12 vs block 8 → HP 80→76 (12−8).

## fixt05_r5_bash_vuln — Bash then Strike into Vulnerable (3 actions)
r5: mon HP 46; hand `Strike3 Bash9 Defend5 Strike2 Pommel11`; moves C,B,T,…
- T1: **Bash** (hand[1]) → 8 dmg (before Vuln), mon 46→38, apply **Vuln 2**. energy 3→1.
- **Strike** (hand[0]) → into Vuln `floor(6×1.5)=9`, mon 38→29. energy 1→0.
- **END**: **Chomp** 12 → HP 80→68. Monster keeps Vuln 2.

## fixt06_r3_shrug_pommel — two draws + block (3 actions)
r3: mon HP 42; hand `Strike4 Strike2 Pommel11 Defend5 Shrug10`; moves C,T,C,…
- T1: **Shrug** (hand[4]) → block +8, draw 1. **Pommel** (hand[2]) → 9 dmg, mon 42→33, draw 1. energy 3→1.
- **END**: **Chomp** 12 vs block 8 → HP 80→76.

## fixt07_r13_bash_strike — Bash + Strike into Vuln (3 actions)
r13: mon HP 44; hand `Strike3 Strike0 Strike2 Strike4 Bash9`; moves C,T,T,…
- T1: **Bash** (hand[4]) → 8 dmg, mon 44→36, **Vuln 2**. **Strike** (hand[0]) → `9`, mon 36→27. energy 3→0.
- **END**: **Chomp** 12 → HP 80→68.

## fixt08_r19_block_two_turns — 2 turns, reshuffle + Bellow (7 actions)
r19: mon HP 43; hand `Defend7 Defend6 Strike1 Strike0 Strike2`; moves C,B,C,…
- T1: **Defend, Defend, Strike** (hand[0] each time) → block 10, mon 43→37 (Strike 6). energy 0.
- **END**: **Chomp** 12 vs block 10 → HP 80→78. The 2-card hand is discarded (discard 5).
  Start T2: draw 5 (draw 7→2) → hand `Strike3 Defend5 Bash9 Defend8 Shrug10`.
- T2: **Strike** (hand[0]) → 6, mon 37→31. **Defend** (hand[0]) → block 5. energy 3→1.
- **END**: monster T2 = **Bellow** → +5 Str, +9 block, 0 damage. The 3-card hand is
  discarded → discard **10**. Start T3: draw 5 — the draw pile has 2, both are
  drawn, then it **empties** → **RESHUFFLE** the 10-card discard (`shuffle_rng`
  advances 1→2) → draw 3 more, leaving draw 7. HP stays 78. Monster now Str 5, block 9.

## fixt09_r6_bash_pommel_vuln — Bash + Pommel into Vuln (3 actions)
r6: mon HP 46; hand `Pommel11 Strike1 Defend6 Defend5 Bash9`; moves C,B,C,…
- T1: **Bash** (hand[4]) → 8, mon 46→38, **Vuln 2**. **Pommel** (hand[0]) → into Vuln `floor(9×1.5)=13`, mon 38→25, draw 1. energy 0.
- **END**: **Chomp** 12 → HP 80→68.

## fixt10_r8_block_stack — Shrug + Defend fully absorb Chomp (3 actions)
r8: mon HP 46; hand `Defend7 Shrug10 Strike3 Strike0 Strike4`; moves C,B,C,…
- T1: **Shrug** (hand[1]) → block +8, draw. **Defend** (hand[0]) → block +5 → 13. energy 3→1.
- **END**: **Chomp** 12 vs block 13 → **HP 80→80** (fully absorbed; 1 block remains, then decays).

## fixt11_r10_draws — Pommel + Shrug (2 draws) (3 actions)
r10: mon HP 45; hand `Shrug10 Pommel11 Strike3 Defend6 Defend7`; moves C,T,B,…
- T1: **Pommel** (hand[1]) → 9, mon 45→36, draw. **Shrug** (hand[0]) → block 8, draw. energy 3→1.
- **END**: **Chomp** 12 vs block 8 → HP 80→76.

## fixt12_r15_bash_pommel — Bash + Pommel, 2 turns (5 actions)
r15: mon HP 46; hand `Strike4 Defend8 Pommel11 Bash9 Shrug10`; moves C,B,T,…
- T1: **Bash** (hand[3]) → 8, mon 46→38, **Vuln 2**. **Pommel** (hand[2]) → `13`, mon 38→25, draw 1. energy 0.
- **END**: **Chomp** 12 → HP 80→68. The 4-card hand is discarded (discard 6). Start T2:
  draw 5 (draw 6→1) → hand `Defend6 Strike0 Defend5 Strike1 Strike2` — **no attack is
  played this turn**; the monster stays on 25.
- T2: **Defend** (hand[0]) → block 5. energy 3→2.
- **END**: monster T2 = **Bellow** → Str 5, block 9 (mon now Vuln 2 **and** Str 5). The
  4-card hand is discarded → discard 11. Start T3: the 1-card draw pile is drawn and
  **empties** → **RESHUFFLE** the 11-card discard (`shuffle_rng` 1→2) → draw 4 more.

## fixt13_r21_triple — three attacks into Vuln, across a turn boundary (5 actions)
r21: mon HP 42; hand `Strike4 Shrug10 Bash9 Defend7 Pommel11`; moves C,B,T,…
- T1: **Bash** (hand[2]) → 8 (before Vuln), mon 42→34, **Vuln 2**. energy 3→1.
  **Pommel** (hand[3]) → into Vuln `floor(9×1.5)=13`, mon 34→21, draw 1. energy 1→**0**.
- **END**: **Chomp** 12 → HP 80→68. Hand (4) discarded; start T2 draws 5 (draw 6→1).
  Monster keeps **Vuln 2** — nothing decays it in the skeleton.
- T2: **Strike** (hand[2]) → into the same Vuln `9`, mon 21→**12**. energy 3→2.
- **END**: monster T2 = **Bellow** → **Str 5**, +9 block. Start T3: the 1-card draw
  pile is exhausted mid-draw → **RESHUFFLE** the 11-card discard (`shuffle_rng`
  1→2) → draw 4 more. HP stays 68.

> **This fixture was corrected.** It previously scripted Bash + Strike + Pommel
> in **one** turn — 2 + 1 + 1 = **4 energy against a 3-energy budget** — and its
> recorded expectation contained `player_energy == -1`, a value the game cannot
> produce (`hasEnoughEnergy` refuses the third card; `useEnergy` clamps at 0).
> The generator had no affordability check and neither did the engine, so the
> two "independent" implementations agreed on an unreachable state and the
> cross-check passed. The `advance()` legality guard exposed it by refusing the
> unaffordable play. The rescript keeps all three attacks and all three damage
> figures (8 / 13 / 9) and lands the monster on the same **12 HP** the old
> script intended; only the turn boundary moved, between the second and third
> attack instead of after it. Both Vuln-boosted hits are preserved, and the
> split adds a turn boundary and a mid-draw reshuffle the old script did not
> cover.

## fixt14_r23_pommel_strikes — Pommel + 2 Strikes (4 actions)
r23: mon HP 44; hand `Defend5 Pommel11 Strike0 Strike3 Strike2`; moves C,B,C,…
- T1: **Pommel** (hand[1]) → 9, mon 44→35, draw. **Strike, Strike** (hand[1],[1]) → 6+6, mon 35→29→23. energy 0.
- **END**: **Chomp** 12 → HP 80→68.

## fixt15_r30_mixed_two_turns — Shrug/Strike then Pommel/Defend (6 actions)
r30: mon HP 42; hand `Strike2 Shrug10 Pommel11 Defend8 Strike0`; moves C,T,B,…
- T1: **Shrug** (hand[1]) → block 8, draw 1. **Strike** (hand[0]) → 6, mon 42→36. energy 3→1.
- **END**: **Chomp** 12 vs block 8 → HP 80→76. The 4-card hand is discarded (discard 6).
  Start T2: draw 5 (draw 6→1) → hand `Strike1 Defend7 Bash9 Strike4 Defend6`.
- T2: **Strike** (hand[0]) → 6, mon 36→30. **Defend** (hand[0]) → block 5. energy 3→1.
  (No Pommel — it went to the discard with turn 1's hand.)
- **END**: monster T2 = **Thrash** 7 (+0 Str) vs block 5 → HP 76→74 (7−5); +5 monster block.
  Start T3: the 1-card draw pile is drawn and **empties** → **RESHUFFLE** the 11-card
  discard (`shuffle_rng` 1→2) → draw 4 more.

## fixt16_r29_monster_death — MONSTER DEATH (6 actions)  ★ coverage
r29: mon HP 43; hand `Bash9 Strike1 Defend7 Strike4 Strike2`; moves C,T,…
- T1: **Bash** (hand[0]) → 8, mon 43→35, **Vuln 2**. **Strike** (hand[0]) → `9`, mon 35→26. energy 0.
- **END**: **Chomp** 12 → HP 80→68. The 3-card hand is discarded (discard 5). Start T2:
  draw 5 (draw 7→2) → hand `Shrug10 Defend8 Strike3 Defend6 Pommel11`. Monster still
  **Vuln 2**, block 0, on **26 HP**.
- T2: **Pommel** (hand[4]) → into Vuln `floor(9×1.5)=13`, mon 26→13, **draw 1 → Strike0**
  (the top of the 2-card draw pile) appended to the hand. energy 3→2.
  **Strike** (hand[2] = Strike3) → `9`, mon 13→4. energy 2→1.
  **Strike** (hand[3] = the drawn Strike0) → `9` into 4 HP → mon **0** → **COMBAT_OVER**.
  energy 1→0. Player survives at 68; monster ends 0/43 still holding Vuln 2.

Why this shape and not three Strikes: turn 2 is dealt out of the five cards drawn at
its start, and that hand holds exactly **one** Strike, so 3×9 = 27 is not on offer —
`Play` slots pointing at a fourth and fifth Strike are the turn-1 hand's, not this
one's. Pommel's `13` both covers the gap (13+9+9 = 31 ≥ 26 for 3 energy) and *draws*
the second Strike that makes the third play possible. The lethal card must stay a
**Strike**: it is the only single-DAMAGE attack, so when the pump halts at
COMBAT_OVER there is nothing left in the queue. A lethal Pommel would strand its
DRAW 1 and a lethal Bash its Vulnerable — the generator refuses both (`killed_note`).

## fixt17_r4_player_death — PLAYER DEATH on turn 9 (9 actions)  ★ coverage
r4: mon HP 44; moves C,B,C,B,C,B,T,B,C. Player never blocks (all END_TURN).
Strength after each Bellow: T2→5, T4→10, T6→15, T8→20. Player HP:
- T1 **Chomp** 12 → 68 · T2 **Bellow** 0 → 68 · T3 **Chomp** 12+5=17 → 51 ·
  T4 Bellow 0 → 51 · T5 **Chomp** 12+10=22 → 29 · T6 Bellow 0 → 29 ·
  T7 **Thrash** 7+15=22 → 7 · T8 Bellow 0 → 7 ·
  T9 **Chomp** 12+20=**32** → 7−32 < 0 → **HP 0, COMBAT_OVER**.
Lethal move is a single-DAMAGE Chomp (no trailing effect queued); start-of-turn 10
never runs. Monster ends at HP 44, block 41, Str 20.

## fixt18_r0_reshuffle_overlap — RESHUFFLE + Str/Vuln OVERLAP (9 actions)  ★ coverage
r0: mon HP 44; hand `Defend6 Strike3 Defend8 Bash9 Defend7`; moves C,B,T,…
- T1: **Bash** (hand[3]) → 8, mon 44→36, **Vuln 2**. **Defend** (hand[0]) → block 5. energy 0.
- **END**: **Chomp** 12 vs block 5 → HP 80→73. The 3-card hand is discarded (discard 5).
  Start T2: draw 5 (draw 7→2) → hand `Strike1 Defend5 Pommel11 Strike2 Strike0`.
- T2: **Strike** (hand[0]) → `9`, mon 36→27. **Defend** (hand[0]) → block 5.
  **Pommel** (hand[0]) → into Vuln `13`, mon 27→**14**, draw 1. energy 0. discard now 8.
- **END**: monster T2 = **Bellow** → **Str 5**, +9 block. Monster now holds **Vulnerable 2 AND
  Strength 5 simultaneously** (powers list `[Vulnerable(2), Strength(5)]` — Bash-first
  append order). The 3-card hand is discarded → discard **11**. Start T3: draw 5 — the
  1-card draw pile is drawn and **empties** → **RESHUFFLE** the 11-card discard
  (`shuffle_rng` 1→2) → draw 4 more. HP stays 73.
- T3: **Strike** (hand[0]) → into Vuln `9`, but monster **block 9** absorbs it all → mon 14, block 0.
- **END**: monster T3 = **Thrash** 7+Str5=12 vs unblocked player → HP 73→61; +5 monster block.
  Here the monster's Strength boosts *its* Thrash while its Vulnerable boosts *the
  player's* Strikes — both powers concurrently live, exercising the combined
  power-list bookkeeping (ledger A6.2 requirement).

## fixt19_r9_long_block — multi-turn, reshuffle (8 actions)
r9: mon HP 43; hand `Strike4 Defend8 Strike1 Defend5 Strike2`; moves C,T,B,…
- T1: **Defend** (hand[1]) → block 5. **Strike** (hand[0]) → 6, mon 43→37. energy 1.
- **END**: monster T1 **Chomp** 12 vs block 5 → HP 80→73. The 3-card hand is discarded
  (discard 5). Start T2: draw 5 (draw 7→2) → hand `Defend7 Bash9 Pommel11 Strike3 Shrug10`.
- T2: **Defend** (hand[0]) → block 5. **Bash** (hand[0]) → 8, mon 37→**29**, **Vuln 2**. energy 0.
- **END**: monster T2 = **Thrash** 7 vs block 5 → HP 73→71; +5 monster block. The 3-card
  hand is discarded → discard 10. Start T3: the 2-card draw pile is drawn and **empties**
  → **RESHUFFLE** the 10-card discard (`shuffle_rng` 1→2) → draw 3 more.
- T3: **Strike** (hand[0]) → into Vuln `floor(6×1.5)=9` vs monster **block 5** → 4 through,
  mon 29→**25**, monster block 0.
- **END**: monster T3 = **Bellow** → Str 5, +9 block — the monster now carries **Vuln 2 and
  Str 5 together**, as in fixt18.

## fixt20_r18_bash_two_turns — Bash Vuln then attack a Strengthened foe (6 actions)
r18: mon HP 45; hand `Strike0 Bash9 Defend8 Strike4 Shrug10`; moves C,T,C,…
- T1: **Bash** (hand[1]) → 8, mon 45→37, **Vuln 2**. **Strike** (hand[0]) → `9`, mon 37→28. energy 0.
- **END**: monster T1 **Chomp** 12 → HP 80→68. The 3-card hand is discarded (discard 5).
  Start T2: draw 5 (draw 7→2) → hand `Defend5 Strike3 Defend6 Strike1 Strike2`.
- T2: **Defend** (hand[0]) → block 5. **Strike** (hand[0]) → into Vuln `9`, mon 28→19. energy 3→1.
- **END**: monster T2 = **Thrash** 7 vs block 5 → HP 68→66; +5 monster block. Start T3: the
  2-card draw pile is drawn and **empties** → **RESHUFFLE** the 10-card discard
  (`shuffle_rng` 1→2) → draw 3 more. The monster never Bellows here (moves C,T,C), so it
  ends on Vuln 2 alone — **no Strength**.

---

## Coverage summary (ledger A6.2)

Every row below was re-checked against `--dumpall`; a fixture is listed only where
the card is actually **played** (or the event actually **occurs**) in its trace.

| requirement | fixtures |
|---|---|
| **Strike** | 01,03,05,07,08,13,14,15,16,18,19,20 |
| **Defend** | 02,08,10,12,15,18,19,20 |
| **Bash** | 05,07,09,12,13,16,18,19,20 |
| **Shrug It Off** | 04,06,10,11,15 |
| **Pommel Strike** | 03,06,09,11,12,13,14,16,18 |
| **Reshuffle** (`shuffle_rng` draws twice) | 08,12,13,15,18,19,20 |
| **Player death** (`player_hp ≤ 0`, phase `COMBAT_OVER`) | 17 |
| **Monster death** (`monsters[0].hp ≤ 0`, phase `COMBAT_OVER`) | 16 |
| **Bellow-Strength + Vulnerable concurrent** | 18 (also 12, 13, 19) |

The two death rows are the ones a stale table can hide most expensively, because a
fixture named for an outcome it never reaches still passes its zero-diff check —
both implementations simply agree that nothing happened, and the name goes on
asserting the coverage. Both are read out of the replayed terminal `CombatState`
(phase `COMBAT_OVER` plus which side is at 0 HP), never out of a file name.

All 20 traces replay through the real engine (`combat_begin` + `advance`) with a
zero `DiffReport` at every action, on the `debug`, `asan` and `release` presets.
