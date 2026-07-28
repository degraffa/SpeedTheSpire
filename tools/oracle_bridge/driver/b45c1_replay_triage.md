# `--replay` triage — b45 campaign 1 (STS00042-46)

Discharges the ledger's **"STS00042 replay stop at seq 32 — untriaged"** row.

> **SUPERSEDED IN PART — 2026-07-28.** Everything below is the read-out of a
> real `--replay` pass and is kept as written; it is history, not a live claim.
> But the cause it names for **STS00042 and STS00043** — the deferred
> `energyMaster` `+1` — **has since landed** (`energy_master`, derived at the
> recharge line in `action_queue.cpp`; the obligation row quoted in §2 is
> discharged, and the registry text quoted there was rewritten in the same
> change, so do not quote it from here). Those two runs must be **re-run**
> before anything is concluded from their rows: the class-(c) stops should
> reclassify, and whatever a re-run shows is a new finding rather than a
> confirmation of this one. The class-(c) boss-`onEquip` stops for STS00045/46
> (Empty Cage) are unaffected and still open.

The six-run frontier table in the ledger covers only the SECOND b45 campaign
(`b45_rewards_oracle2_…`, STS00047-52). This is the same read-out for the
FIRST, `b45_rewards_oracle_20260727T204809Z_claude01` (STS00042-46), which had
never had a full `--replay` pass. Both campaigns are strict-validated members of
the B4.5 capture recorded in [`b45_reward_spotdiff.md` §7](b45_reward_spotdiff.md).

Command (from the repo root, per [`b45_reward_spotdiff.md` §5](b45_reward_spotdiff.md)):

```bash
tools/wsl_run.sh debug
build/debug/tools/oracle_bridge/replay/replay_run_diff --replay \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/b45_rewards_oracle_20260727T204809Z_claude01/run_STS000{42,43,44,45,46}_a20_ironclad.jsonl
```

## The frontier table

| Run | Neow boss relic | Records compared | First divergence | Stop | Class |
|---|---|---|---|---|---|
| STS00042 | Philosopher's Stone | 33 | **seq 18** floor 1 `NONE`, 1 field (`hp: 66 -> 61`) | seq 32, `event command 'choose' arrived while the sim is in COMBAT, not an event dialog` | **(c)** deferred `energyMaster` |
| STS00043 | Fusion Hammer | 67 (to terminal) | **seq 15** floor 1 `COMBAT_REWARD`, 11 fields (reward assembly, all still at their run-begin values) | run terminal | **(c)** deferred `energyMaster` |
| STS00044 | — (kept Burning Blood) | 21 (to terminal) | **none — every compared record zero-diff** | run terminal | **CLEAN** |
| STS00045 | Empty Cage | 3 | none | seq 2, `the capture opens a master-deck grid the sim never opened (sim phase NEOW): the most recently acquired relic is Empty Cage, whose onEquip body is deferred` | **(c)** deferred boss `onEquip` |
| STS00046 | Empty Cage | 3 | none | seq 2, same as STS00045 (`choose 6`) | **(c)** deferred boss `onEquip` |

**No class (a) — no real engine divergence.** **No class (b) — no stop
attributable to the mapping table.** Campaign 1 therefore lands in the same
place campaign 2 did: every stop names a documented deferral.

Classes, as the brief defines them: **(a)** real sim divergence · **(b)**
harness/mapping gap · **(c)** named deferred body reached · **(d)** capture
artifact.

## STS00042 — the row's own question, answered

The row asked whether the seq-32 stop was "harness mapping vs a real
event/combat-boundary divergence". **It is neither, and seq 32 is not the
frontier.** The two sides first disagree fourteen records earlier, at seq 18,
and the cause is on the deferred-obligations table already.

### 1. The relic

Neow's blessing at seq 1 is `choose 3` — *"Lose your starting Relic Obtain a
random boss Relic"*. From seq 2 the capture's relic list reads
`[("Philosopher's Stone", -1)]` (Burning Blood at seq 0-1).

### 2. Half of that relic is live, half is deferred

`PhilosopherStone.atBattleStart` (`PhilosopherStone.java:41-48`) adds
`StrengthPower(m, 1)` to every monster. **The sim does this**: the capture's
floor-1 Cultist carries `('Strength', 1)` from its first record and the sim's
matches it — `--combat` prints no `monsters[0].powers[0]` row at seq 4.

`PhilosopherStone.onEquip` (`PhilosopherStone.java:55-58`) is
`++AbstractDungeon.player.energy.energyMaster`. **The sim does not do this**,
by design and on the record:

- `registry/relics.yaml`, the shared note above the BOSS rows — *"Ten boss
  relics do `++AbstractDungeon.player.energy.energyMaster` in onEquip (Fusion
  Hammer, Velvet Choker, Runic Dome, Cursed Key, Busted Crown, Ectoplasm, Sozu,
  Philosopher's Stone, Coffee Dripper, Mark of Pain) … The engine has NO
  energyMaster field: the per-turn recharge reads the `kIroncladBaseEnergy`
  constant (`EnergyManager.recharge`, `EnergyManager.java:25-40` →
  `action_queue.cpp start_of_turn`). … Each affected row's pool slot, canSpawn
  gate and relicRng draw are LIVE; only the +1 is inert."*
- The PHILOSOPHERS_STONE row's own provenance ends *"onEquip ++energyMaster
  (:55-58) is the shared deferral."*
- The obligation row: **"Ten `energyMaster` relics (… Philosopher's Stone …)"**,
  from B3.27, `UNASSIGNED — next action_queue.cpp owner`.

### 3. The fingerprint, at the very first combat record

`--replay --combat`, seq 4 (floor 1, turn 1, before any card is played):

```
ok   seq=4 floor=1 screen=NONE  sim_phase=COMBAT  cmd='play 4 0'
  combat seq=4: …
player.energy: 4 -> 3
```

Game 4, sim 3. Ironclad's base energy is 3, so the missing 1 *is* the missing
`energyMaster` increment. The same line appears on every turn-start record of
the run.

### 4. How one energy becomes a dead run

Capture, turn 1, hand `[AscendersBane, Strike_R, Bash, Strike_R, Strike_R]`:

| seq | cmd | game energy → | game Cultist |
|---|---|---|---|
| 4 | `play 4 0` (Strike) | 4 → 3 | 55 → 49 |
| 5 | `play 2 0` (Strike) | 3 → 2 | 49 → 43 |
| 6 | `play 2 0` (**Bash**, cost 2) | 2 → 0 | 43 → **35**, +Vulnerable 2 |

The sim reaches seq 6 with **1** energy, so the 2-cost Bash is illegal and the
play is a no-op. At seq 7 `--combat` reports `monsters[0].hp: 35 -> 43` and
`monsters[0].powers[…]: Vulnerable(2) -> NONE` — and no `player.block` row,
i.e. the sim gained neither damage nor block from that command, which is what an
unaffordable card looks like. (The card is confirmed as Bash on both sides:
seq 6's diff carries `hand[1]: 1 -> 10` with `card_pool[10].card_id: Strike_R ->
Bash`, so the sim's `play 2 0` also names its Bash.)

From there the sim is a card behind every turn. Its Cultist is at 29 HP when
the game's dies, so the sim's floor-1 fight **never ends**. The first
RunState-visible consequence is seq 18, `hp: 66 -> 61` — the sim's player eating
one extra Dark Strike because its fight is still running.

### 5. Why the stop lands on an event

From seq 20 the capture is on `COMBAT_REWARD` / `MAP` / `EVENT` screens while
`sim_phase=COMBAT`. Those commands map to `CHOOSE`es that a COMBAT-phase
controller ignores, so the replay keeps comparing records and keeps reporting
the same growing diff. seq 31-32 is floor 2's **FaceTrader** — an ordinary
`EventRoom`, correctly generated on both sides, with a fully implemented sim
body (`registry/events.yaml` FACE_TRADER, `src/engine/events/one_time_specials.cpp`
`face_trader_enter/menu/choose`). seq 31's page has ONE button (`Continue`) and
is elided as a map bounce; seq 32's has three (`Touch`, `Trade`, `Leave`), which
is the first multi-option event page of the run, and the mapping's desync guard
fires rather than hand a `CHOOSE` to a live combat.

**So the seq-32 message is where the harness first REFUSED, not where the run
diverged.**

Stated precisely, because the distinction matters for whoever picks this up:
the sim never left floor 1, so it never entered floor 2's room at all. This
artifact therefore says **nothing** about the event/combat boundary or the
`?`-room roll in either direction — it does not exercise them. What it does
establish is that the seq-32 message is not evidence for a defect there: the
controller was parked in a floor-1 combat fourteen records earlier, for a cause
that is fully accounted for. Re-triaging floor 2 needs a capture whose floor-1
fight the sim can finish, i.e. one whose Neow blessing is not an `energyMaster`
relic — or this same seed once that obligation lands.

## STS00043 — same root cause, different relic

Neow `choose 3` again; the relic is **Fusion Hammer**, whose `onEquip`
(`FusionHammer.java:47-49`) is the same `++energyMaster` and the same shared
deferral (its registry row: *"the energy half is the shared energyMaster
deferral above"*).

`player.energy: 4 -> 3` from seq 6, the first combat record. The chain:

- seq 8, `play 1 1` (Strike, cost 1). Game energy 1 → 0; sim energy **0**, so
  the play is illegal. At seq 9 `monsters[1].hp: 8 -> 14`: the sim's second
  Louse never took that Strike.
- Because it was never damaged, the sim's Louse keeps its **Curl Up 12**
  (`monsters[1].powers[0]: NONE -> Curl Up(20)` at seq 13, amount `0 -> 12`).
- seq 14, `play 3 1` (Bash, 8 damage) kills the game's 2-HP Louse and ends the
  fight. The sim's is at 14, drops to 6, and gains 12 block from Curl Up.

So the game is on `COMBAT_REWARD` at seq 15 while the sim is still fighting,
which is exactly the 11 fields the seq-15 diff reports: `card_blizz_randomizer`,
`blizzard_potion_mod` and the `cardRng` / `treasureRng` / `potionRng` triples,
all still at their run-begin values because the sim never assembled a reward.
Everything after seq 15 is downstream of the same energy deficit.

Unlike STS00042 this run reaches its terminal (67 records), because no command
in the remaining 52 records happened to trip a mapping guard. **A replay that
reaches the terminal is not a clean replay** — which is the second reason the
summary line now prints the first divergence separately from the stop.

## STS00044 — the control

Neow took a non-boss-swap blessing, so the run keeps Burning Blood and no
`energyMaster` relic is involved. `CLEAN`, 21 records, zero-diff to the run's
terminal. This is what makes the STS00042/43 attribution a controlled
observation rather than a story: same campaign, same policy, same ascension —
the two runs that drew an `energyMaster` boss relic diverge from their first
combat record, and the one that did not is zero-diff end to end.

## STS00045 / STS00046 — Empty Cage, twice

Both take Neow's boss swap and draw **Empty Cage**, whose `onEquip`
(`EmptyCage.java:28-52`) opens a two-card removal grid. That body is explicitly
deferred (`registry/relics.yaml` EMPTY_CAGE — *"DEFERRED: needs the run-layer
grid-select screen. No RNG is consumed either way. Explicit empty on_equip
body"*), and it is one of the five named on the obligation row *"Pandora's Box /
Tiny House / Astrolabe / Empty Cage / Calling Bell `onEquip`"*.

The harness already classifies this correctly and names the body. The
ACQUISITION is proved before the stop: seq 0-2 are zero-diff, so the relic is in
the list, its pool is popped and every stream matches. Nothing further is
knowable from these two artifacts — the capture spends its next command inside a
screen the engine does not open. Both are class (c), untriageable further until
that body lands, and neither is evidence of anything else.

## What was fixed here, and what was not

**Not fixed:** nothing in `src/engine/`. Every divergence found resolves to a
row already on the obligations table, so there is no engine change to propose
and no stop-the-line to raise.

**Fixed (class (b), reporting only — `tools/oracle_bridge/replay/`):**

1. **A stop reason names the phase, not its ordinal.** Both `UNMAPPED` reasons
   in `command_map.hpp` interpolated `rc.phase` as a bare enum ordinal. The cost
   is on the record: the obligation row this document discharges quoted *"the
   sim is in 3, not an event dialog"* and had to gloss the integer itself
   ("3 [COMBAT]") before it could state its question. `phase_name` already
   existed in `main.cpp` for the per-record `DIFF` line; it moved into
   `command_map.hpp` so both reasons share one spelling and `main.cpp` keeps
   one copy. Tests in `replay_command_map_test`:
   `AnEventDesyncStopNamesTheSimsPhaseRatherThanItsOrdinal`,
   `AnUnsimulatedGridStopAlsoNamesThePhaseRatherThanItsOrdinal`,
   `EveryRunPhaseHasAName` (a phase with no name would print `?` into a stop
   reason — the same unreadable outcome, arriving silently).

2. **The summary prints the FIRST DIVERGENCE beside the stop.** `replay_one`
   already tracked `diverged_at` and never printed it, so the only headline a
   reader got was `stop:` — which answers "why did the replay end", a different
   question from "where did the two sides first disagree". When they differ the
   stop is downstream, and this whole triage row exists because that was read as
   the frontier. The `PART`/`CLEAN` line is now followed by

   ```
         first divergence: seq=18 floor=1 screen=NONE (1 field) -- the stop above is DOWNSTREAM of this
   ```

   or, when there was none, `first divergence: none -- every compared record was
   zero-diff`. The `DOWNSTREAM` note is suppressed when the replay stopped by
   reaching the terminal or exhausting the artifact, since neither is a stop a
   reader would mistake for a frontier. Note what the two lines say together for
   STS00045/46: **stopped early, zero divergence** — a coverage limit, not a
   defect — versus STS00043's **ran to terminal, diverged at seq 15**.

**Regression check.** The campaign-2 six-run table is byte-for-byte unmoved by
both changes: STS00047/48/49/50 `CLEAN`, STS00051 reaches its terminal with 19
library-order-only records and the known floor-4 `kEventTransformRedPool`
downstream pair (`first divergence: seq=41 floor=4 screen=NONE (2 fields)`),
STS00052 stops at seq 2 on Astrolabe's deferred `onEquip`.

## Observation left deliberately unfixed

`command_map.hpp`'s EVENT branch no-ops a **one-button** page whenever the sim
is in neither `EVENT_DIALOG` nor `NEOW` (the documented `AbstractEvent.openMap`
bounce). When the sim is desynced into COMBAT, as at STS00042 seq 31, that
elision is not describing a bounce — it silently swallows a record. Tightening
it to `MAP_CHOICE` would be more precise, but it would also **truncate** a
desynced replay earlier and cost the per-record diffs that made this triage
possible (STS00043's 52 post-divergence records are the evidence that its drift
never widened beyond the one missing Strike). The first-divergence line removes
the reason the swallow mattered — the frontier is reported regardless — so this
is recorded rather than changed. Whoever owns the next mapping change should
decide it deliberately, not incidentally.
