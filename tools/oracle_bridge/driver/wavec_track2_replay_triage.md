# `--replay` triage — Wave-C track 2 (the five boss `onEquip` bodies)

Offline validation of the landed `on_equip_screen` bodies against the existing
captures, per the stage's acceptance. Successor to
[`b45c1_replay_triage.md`](b45c1_replay_triage.md), whose STS00045/46/52 rows
describe the tree BEFORE this wave (each stopped at seq 2 on the then-deferred
body); this file records what `--replay` sees with the bodies live. No game was
launched and no capture pipeline ran — every command below is the offline
replay tool against artifacts already on disk.

Command (repo root; the tool from the same tree's `debug` build):

```bash
tools/wsl_run.sh debug
build/debug/tools/oracle_bridge/replay/replay_run_diff --replay \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/b45_rewards_oracle_20260727T204809Z_claude01/run_STS000{42,43,44,45,46}_a20_ironclad.jsonl \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/b45_rewards_oracle2_20260727T204809Z_claude01/run_STS000{47,48,49,50,51,52}_a20_ironclad.jsonl \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/b47_treasure_oracle_20260727T204809Z_claude01/run_STS00054_a20_ironclad.jsonl
```

## Verdicts

| Run | Neow boss relic | Before this wave | After | Class |
|---|---|---|---|---|
| STS00045 | Empty Cage | stop seq 2, 3 records, deferred body | **CLEAN to terminal**, 24 records, zero divergence | validated |
| STS00046 | Empty Cage | stop seq 2, 3 records, deferred body | **CLEAN to terminal**, 25 records, zero divergence | validated |
| STS00052 | Astrolabe | stop seq 2, 3 records, deferred body | **zero divergence over 43 records** (7 on reward screens, 0 library-order-only); stop seq 42: `screen 'SHOP_ROOM' is not modelled by the run layer` | validated to the new frontier |
| STS00054 | Astrolabe | (never `--replay`ed) | **zero divergence over 33 records**; stop seq 32, same `SHOP_ROOM` reason | validated to the new frontier |
| STS00042 | Philosopher's Stone | first div seq 18 (energyMaster) | unchanged (first div seq 18, 1 field) | pre-existing deferral, Deferred-obligations `energyMaster` row (track 1's) |
| STS00043 | Fusion Hammer | first div seq 15 (energyMaster) | unchanged (first div seq 15) | pre-existing deferral, same row |
| STS00044 / 47 / 48 / 49 / 50 / 51 | (no deferred body) | CLEAN | **still CLEAN**, zero divergence | no regression |

## The divergence the new coverage found — and its root cause (fixed)

The first run after the bodies landed, STS00052 showed 12 library-order-only
records: the three Astrolabe transform identities were each **one miscRng draw
behind the capture** (sim `Feel No Pain, Fiend Fire, Impervious` = draws 1–3;
game `Fiend Fire, Impervious, True Grit` = draws 2–4 of the same stream) with
every RNG counter equal. Root cause, from the decompiled Java, not from the
symptom: `Exordium.<init>` ends with `CardCrawlGame.music.changeBGM(id)`
(Exordium.java:58), and `MainMusic.getSong`'s Exordium arm draws
`AbstractDungeon.miscRng.random(1)` to pick the act's track
(MainMusic.java:56-66) — one real draw off the floor-0 misc stream
(`miscRng = new Random(Settings.seed)`, AbstractDungeon.java:411).
`nextRoomTransition`'s per-floor reseed (:1751) discards the offset, so floors
≥ 1 never see it — which is why every earlier capture replayed clean and the
gap stayed invisible until a floor-0 miscRng consumer (a boss-swap onEquip
body) existed. Fixed in `run_begin` (run_advance.cpp) with the citation at the
site and the pin moved into
`RunBegin.NeowHasGenerateSeedsFloorStreamsAtFloorZero`; STS00054 is the second
Astrolabe seed and reproduces the fix (zero divergence).

This was a divergence in the run layer's own start-of-run stream accounting —
Track 2 scope — not in the shared transform machinery: `transform_card`'s list
produced the game's exact identities once the stream was aligned.

## The new frontier

STS00052/54 both stop where the capture walks onto a **floor-2 shop**: the
`--replay` command mapping (`command_map.hpp`) has no `SHOP_ROOM` screen
branch (`screen '<X>' is not modelled by the run layer` is its generic
fallthrough). The run layer itself HAS shops (B4.8, `RunPhase::SHOP`) and the
`--shop` spot-diff mode drives them; what is missing is only the
screen-relative mapping entry for a shop reached mid-`--replay` — none of the
eleven b45 runs ever reached one, so the gap had no producer until Astrolabe's
runs got past floor 0. Left for the harness's next owner as a coverage
feature, deliberately not folded into this wave: it is disjoint from the five
bodies and the mapping deserves its own tests (stock indexing joins through
display names — see `StockRow`).

## Stage 3 re-run (the Bottled trio landing) — no regression

Same command set as above (offline, no game, no capture pipeline), re-run
after stage 3 landed the bottled master-deck marker, the pending-bottle
overlay and the `getGroupWithoutBottledCards` exclusions. All twelve
artifacts — STS00042–46 (`b45_rewards_oracle`), STS00047–52
(`b45_rewards_oracle2`), STS00054 (`b47_treasure_oracle`) — reproduce their
pre-stage-3 verdicts exactly:

| Run | Verdict | Same as baseline? |
|---|---|---|
| STS00044 / 45 / 46 / 47 / 48 / 49 / 50 / 51 | **CLEAN to terminal**, zero divergence | yes |
| STS00052 | zero divergence over 43 records; stop seq 42 `SHOP_ROOM` mapping frontier | yes |
| STS00054 | zero divergence over 33 records; stop seq 32, same frontier | yes |
| STS00042 | first div seq 18 (1 field) | yes — the pre-existing `energyMaster` deferral (track 1's row) |
| STS00043 | first div seq 15 | yes — same row |

No existing capture takes a bottle (the trio's `canSpawn` deck gates plus
these runs' actual draws), so this proves non-regression only; the positive
end-to-end proof needs the fork redeploy + a bottle-taking capture — the new
Deferred-obligations row (`docs/stage-b-tasks.md`) names the next
capture-campaign owner.

## Wave-C integration re-run (`wave-integrate` union of both tracks + master)

Same command set (offline, no game, no capture pipeline), re-run on the
`wave-integrate` union — `wave-runlayer` + `wave-combat` merged onto master
`7df27ab`, with the two cross-track coupling fixes (Snecko-Eye-derived innate
overflow threshold; composed rest-room entry order) applied. The acceptance
bar was: no artifact regresses vs the BETTER of its two per-track results,
and STS00042/43 may improve because track 1's `energyMaster` derivation and
track 2's stream fixes are on one tree for the first time.

| Run | Union verdict | vs better per-track result |
|---|---|---|
| STS00042 | zero divergence over 38 records; stop seq 37 `SHOP_ROOM` mapping frontier | equals track 1's post-`energyMaster` re-run (`b45c1_replay_triage.md` supersession note); the track-2 baseline's seq-18 divergence is GONE |
| STS00043 | **CLEAN to terminal**, 67 records, zero divergence | equals track 1's re-run; track-2 baseline's seq-15 divergence GONE |
| STS00044 / 47 / 48 / 49 / 50 / 51 | **CLEAN to terminal**, zero divergence | unchanged |
| STS00045 / 46 | **CLEAN to terminal** (24 / 25 records), zero divergence | equals track 2's result |
| STS00052 | zero divergence over 43 records; stop seq 42 `SHOP_ROOM` frontier | equals track 2's result |
| STS00054 | zero divergence over 33 records; stop seq 32 (`proceed`), same frontier | equals track 2's result |

12 files, 3 not clean — all three stops are the documented `SHOP_ROOM`
`--replay` mapping frontier (ledger row "replay generalized", which already
owns it; master's `badc58f` harness fixes did NOT add that arm, checked
against its read-out and the union source). **No new divergence appeared on
the union that neither track saw**, and no record anywhere compared
different. First divergence: none, on every file.

## `wave2-harness` stage 2 — the `SHOP_ROOM` frontier is gone

The mapping arm the "replay generalized" ledger row owned is landed:
`command_map.hpp` now has a `SHOP_ROOM` branch (the merchant click is a NOOP,
`proceed` is the menu's `kChooseProceed`, and a repeat after the sim has left is
a UI bounce) and a `SHOP_SCREEN` branch (`leave` is a NOOP, `choose i` resolves
through the capture's `choice_list` and then through IDENTITY to the sim's
unsold slot). Same offline command as above, `debug` build, no game and no
capture pipeline.

| Run | Before (union re-run) | After | Delta |
|---|---|---|---|
| STS00042 | zero-diff over 38, stop seq 37 `SHOP_ROOM` | **CLEAN to terminal**, 85 records | +47, frontier gone |
| STS00043 | CLEAN to terminal, 67 records | **CLEAN to terminal**, 67 records | unchanged |
| STS00044 / 45 / 46 / 47 / 48 / 49 / 50 / 51 | CLEAN to terminal | **CLEAN to terminal** | unchanged |
| STS00052 | zero-diff over 43, stop seq 42 `SHOP_ROOM` | zero-diff over 79, stop seq 78 **rest `Recall`** | +36, new frontier |
| STS00054 | zero-diff over 33, stop seq 32 `SHOP_ROOM` | zero-diff over 123, stop seq 122 **rest `Recall`** | +90, new frontier |

`--- 12 file(s), 2 not clean ---`, **first divergence: none, on every file** —
the two stops are class (c), with zero divergence up to them.

### What the new arm exposed, and how each was triaged

**The shop arm itself produced no divergence anywhere.** Everything below was
reached only because the replays now walk past a merchant.

1. **A second UI bounce, in the SAME shape as the shop's** — and this one was
   already broken. A rest room's `proceed` opens the map over the still-mounted
   campfire; a map `return` dismisses it and the capture presses `proceed` again
   (STS00052 seq 79/81/83, STS00054 seq 110/112/114). The REST branch mapped
   every one of them to `CHOOSE(kChooseProceed)`. Fixed the way the EVENT and
   SHOP branches discriminate: on the SIM's phase, never on the record.
2. **`RecallOption` is the new frontier, and it is class (c).**
   `build_rest_menu` (`rest_sites.cpp`) deliberately omits the Ruby Key button
   (`CampfireUI.java:94-96`) as "Act-4 concerns with no S1 representation". But
   the capture profile HAS the final act available, so **every captured rest
   site lists `["rest", "smith", "recall"]`** — which contradicts that comment's
   premise that the button is unreachable here. Pressing it was an illegal
   `CHOOSE` at the run layer, which is defined as a non-corrupting NO-OP: the
   sim stayed parked on the campfire while the capture walked on, and the first
   evidence was a `floor` field a dozen records later. The mapping now asks the
   legal mask and stops with the body NAMED. Whether to model the option is a
   run-layer question (`RunState.keys` exists) and is not this track's to
   answer — reported, not patched.

## `wave2-harness` stage 2 — the G6 main campaign, 30 runs

The `SHOP_ROOM` stop was 16 of the 30 stops in
[`g6_campaign2_spotdiff.md`](g6_campaign2_spotdiff.md) §6. Re-run whole with the
new arm (and with this wave's already-discharged potion / Duplication rows on
the tree, which is why two former translation aborts now replay):

```bash
build/debug/tools/oracle_bridge/replay/replay_run_diff --replay \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/g6_campaign2_20260728T153342Z_claude01/run_*[!g].jsonl
```

`--- 30 file(s), 12 not clean ---`, up from 4 clean. **No `SHOP_ROOM` stop
remains anywhere**, and no run regressed: every seed compares at least as many
records as its old row records, and none acquired an earlier first divergence.

Newly clean to terminal, having previously stopped at a merchant: STS00220,
STS00365, STS00425, STS00451, STS00577, STS00610, STS01857, STS01861, STS01906,
STS02041, STS02048, STS02110. **STS01221 — the boss-reward terminal seed the
gate cites — replays 200 records to its terminal**, where it previously aborted
in translation on `DuplicationPower`.

The twelve remaining, each attributed:

| First divergence | Runs | Class |
|---|---|---|
| `EVENT` page offset (seq 22 / 25 / 61) | STS01314, STS00221, STS02009 | **(d)** the §8.2 Wheel-of-Change capture artifact, unchanged |
| `gold` +20 in combat | STS00462, STS00683, **STS01372**, **STS01221** | **(c)** the "stolen-gold clamp vs in-combat gold ordering" ledger row (UNASSIGNED). The last two are newly reached and are the SAME shape, not new findings |
| `potions[i] NONE -> FairyPotion` | **STS00283** seq 85, **STS00856** seq 80 | **(c)** the capture's Fairy in a Bottle fired its auto-revive and the sim's did not. Newly reached; sim-side, so reported, not patched |
| `hp 14 -> 6`, then the sim dies while the capture fights on | **STS01789** seq 130 f10 | **(a)-candidate, NEW.** A combat damage divergence beyond every previous frontier. Sim-side — reported, not patched |
| 11 fields at a `COMBAT_REWARD`: `hp 50 -> 44` plus card / treasure / potion stream counters and `blizzard_potion_mod` | **STS00353** seq 97 f8 | **(a)-candidate, NEW.** The capture heals 6 leaving the fight (Burning Blood) and assembles a different reward set; the sim does neither. Sim-side — reported, not patched |
| none — zero-diff to the stop | STS01068 (unsimulated grid, seq 39) | unchanged |

The two (a)-candidates are this stage's honest headline. They are not
regressions and they are not the shop arm's doing — every record before them
compared zero-diff, including their merchants. They are simply the first
divergences that were ever *reachable*, and they belong to whoever owns
`src/engine`.

## `--replay` also learned `--event`'s obtain-race classification

`is_obtain_race` moved above the whole-run driver, and its parameters now name
the ROLE (`ahead` / `behind`) rather than the source, because the two modes see
the race from opposite sides: `--event` seeds from the pre-entry record, so the
CAPTURE is ahead; `--replay` diffs before applying record *k*, so the SIM is.
The narrowness is unchanged — every differing field must be `master_deck_count`
or a `master_deck[i]` at or past the shorter deck's end — and the verdict line
carries an `obtain-race` count beside the library-order one. The current corpus
produces none (0 across all 42 artifacts run here), which is exactly the state
the ledger row records; what changes is that such a record can no longer read as
a deck divergence.

## `wave2-integrate` — the four-track union (2026-07-28)

Same offline command set (`debug` build, no game, no capture pipeline), re-run
on the `wave2-integrate` union — `wave2-harness` + `wave2-engine` +
`wave2-prov` + `wave2-capture` merged onto master `e15ebad`, with the
integration commit (Orrery/Cauldron provenance re-derivation) applied. The
acceptance bar: no artifact regresses vs the best per-branch result anywhere.

b45+b47, all twelve artifacts — **identical to the stage-2 table above,
record for record**:

| Run | Union verdict | vs stage-2 baseline |
|---|---|---|
| STS00042 | **CLEAN to terminal**, 85 records | unchanged |
| STS00043 | **CLEAN to terminal**, 67 records | unchanged |
| STS00044 / 45 / 46 / 47 / 48 / 49 / 50 / 51 | **CLEAN to terminal** | unchanged |
| STS00052 | zero-diff over 79 records, stop seq 78 rest `Recall` | unchanged |
| STS00054 | zero-diff over 123 records, stop seq 122 rest `Recall` | unchanged |

`--- 12 file(s), 2 not clean ---`, first divergence: none, on every file.

G6 main campaign, all 30 runs — `--- 30 file(s), 12 not clean ---`, the SAME
twelve as the stage-2 table, each at its recorded first-divergence seq
(STS00221 s25 / STS01314 s22 / STS02009 s61 EVENT class (d); STS00462 s85,
STS00683 s79, STS01372 s91, STS01221 s145 gold class (c); STS00283 s85,
STS00856 s80 FairyPotion class (c); STS01789 s130 and STS00353 s97, the two
(a)-candidates, byte-identical shapes; STS01068 zero-diff to its seq-39 grid
stop). STS00509 — the Explosive-Potion retype pin — stays **CLEAN to
terminal, 171 records**. **No new divergence appeared on the union that no
branch saw**, and no run compares fewer records than any branch's own run of
it. The six wave2cap bottle captures' union verdicts (the payoff the
`SHOP_ROOM` arm + the bottle captures were jointly gated on) are recorded in
[`wave2cap_capture_runbook.md`](wave2cap_capture_runbook.md) §7.

## `wave3-followup` — the Elixir HAND_SELECT class is gone (2026-07-28)

The class had TWO stacked causes, root-caused offline from the STS05143
reproducer and separated by which artifact each fix cleaned. Both fixes are on
`wave3-followup`; same offline command set, `debug` build, no game.

**1. Harness: the HAND_SELECT `proceed` was not the confirm verb.** The
mapping sent `proceed` to `CHOOSE(kChooseProceed)` = CHOOSE(0xFF), which the
combat layer's OPTIONAL path reads as a toggle of hand slot 0xFF — an
illegal, silent no-op. Elixir's zero-to-99 exhaust screen (a blocking
CHOOSE_CARD at the queue head) therefore never closed: every later
`play`/`end` was illegal while the choice blocked, the sim froze at its
pre-Elixir hp and stayed parked in COMBAT to the artifact's end (the seq-58
stop reason "sim is in COMBAT" was the giveaway; the exhaust pile read empty
because neither the picks nor the end-of-turn ethereal exhaust ever ran). The
confirm button is the combat layer's own `ActionVerb::CONFIRM`. The branch
now also discriminates on the sim's phase and mask exactly as the REST/SHOP
branches do — a hand-select command outside COMBAT, a `choose` with no screen
open, or a slot the open screen does not offer are STOPS with the desync
named, never silent no-ops; the one legitimate no-screen `proceed` (a
MANDATORY selection resolves on its last pick, so the game's trailing confirm
press has no sim analogue) is elided as the UI bounce it is. The `choose N`
index space is proven IDENTITY over the unpicked prefix: the fork walks
`player.hand.group` (ChoiceScreenUtils.getHandSelectScreenChoices) — the hand
minus the already-selected cards — the engine keeps picked cards as the hand
TAIL in pick order, and the fork can only select, never unselect. Pinned by
the six `ReplayCommandMap.AHandSelect*` tests (RED-first: five failed on the
old mapping; the identity pin was the control that already passed).

**2. Engine: Toy Ornithopter's in-combat heal was inline instead of queued.**
With the screen finally confirming, STS03352 still diverged by 1 field for
exactly the two records its screen was open (seq 143-144, capture hp 58 / sim
63, reconverging at the confirm). `ToyOrnithopter.onUsePotion` in a
COMBAT-phase room is `addToBot(new HealAction(player, player, 5))`
(ToyOrnithopter.java:31-41), queued BEHIND the potion's own actions
(PotionPopUp.java:234-239 runs potion.use first) — so the game's +5 waits
behind Elixir's open ExhaustAction until the button. The engine's
`dispatch_run_relics_on_use_potion` healed inline at use time. It now queues
a HEAL item in combat (which also routes the heal through
`heal_player_with_relics`, i.e. Magic Flower's onPlayerHeal pass the inline
write skipped) and routes the out-of-combat branch through
`heal_out_of_combat` (the not-bloodied cross of AbstractPlayer.heal:404-408
the inline write also skipped — an Ornithopter heal past half now disarms an
active Red Skull). RED-first: `RunPotion.ToyOrnithopter*` — the blocked-heal,
Magic Flower and not-bloodied-cross tests failed on the inline write; the
immediate-heal and plain out-of-combat tests are the named controls that
passed throughout.

| Run | Before | After harness fix alone | After both | Class |
|---|---|---|---|---|
| STS05143 | stop seq 58, first div seq 51 (hp, then 12-24 fields/record) | **CLEAN to run terminal, 113 records** | CLEAN, 113 | fixed (harness) |
| STS03352 | first div seq 143, cascading | 248 records, first div seq 143-144 only (hp, 2 records, reconverging) | **CLEAN to run terminal, 248 records** | fixed (harness + engine) |

Standing corpus, re-run whole after both fixes: b45+b47 twelve — ten CLEAN,
STS00052/STS00054 still the two `Recall` stops at seq 78/122, zero divergence
anywhere, record-for-record identical to the wave-2 baseline. G6-main thirty —
`--- 30 file(s), 12 not clean ---`, the SAME twelve at the same first-diff
seqs. wave2cap bottle seven — STS05143 and STS03352 now CLEAN (above);
STS04888/STS03244 still CLEAN; STS00241 (seq-96 Smoke-Bomb race), STS04925
(s137 gold class (c)) and STS06578 (s81 gold class (c)) byte-identical to
their §7 runbook rows. **No run anywhere compares fewer records or acquires
an earlier first divergence.**

## `wave3-followup` — the two (a)-candidates were ONE engine defect (2026-07-28)

STS01789 seq 130 and STS00353 seq 97 — the stage-2 table's two sim-side
(a)-candidates — shared a root cause, found by reproducing STS01789 offline
with the `--replay --combat` triage print: the sim's floor-10 monsters carried
**different max-HP rolls** (Acid Slime (M) 33/Slaver 51 vs the game's 30/49;
ids, intents and every player-side number identical). The hand-computed game
deltas then pinned the mechanism: the game's Slaver-only turn dealt 13-5=8,
the sim's 21-5=16 — exactly one Corrosive Spit more, i.e. a slime the game
had killed was still alive sim-side, because its max HP was 3 higher and the
killing blow left it at 3.

**Root cause (engine): a PICK composition's discarded candidates still draw
their constructors' monsterHpRng rolls.** `bottomGetWeakWildlife` /
`bottomGetStrongHumanoid` / `bottomGetStrongWildlife`
(MonsterHelper.java:799-822) CONSTRUCT their whole candidate ArrayList — every
ctor runs setHp (one monsterHpRng draw; a Louse ctor draws biteDamage too,
LouseNormal.java:60) — and only then does `random(0, n-1)` keep one. The
engine's `resolve_composition` consumed the miscRng coins faithfully but
returned only the kept members, so `spawn_group` rolled HP for two monsters
where the game had rolled seven — every kept monster's HP came from the wrong
stream position. Both mixed encounters are affected ("Exordium Thugs",
"Exordium Wildlife") and no other S1 program over-constructs (BOOL/SEQ_BOOL/
POOL construct exactly what they keep).

**Fix**: `ResolvedGroup` now carries the CONSTRUCTION trace
(`constructed[]`/`kept_mask`), and the run layer spawns through
`spawn_group_trace` — kept members init at their construction-order positions,
discarded candidates burn their ctor draws (`burn_unspawned_ctor_rolls`,
ranges straight from the registry defs, CONSTRUCTOR_AFTER_HP rolls included,
no ai_rng and no PRE_BATTLE roll). RED-first:
`RunCombatSpawn.ExordiumThugs/ExordiumWildlifeDiscardedCandidatesBurnTheirCtorRolls`
(both failed on the kept-only spawn; hand-derived construction walks over
seeds 900-907), with `RunCombatSpawn.APlainCompositionBurnsNothing` ("2
Louse") as the named negative control (passed throughout).

**Why STS00353's two halves were one cause**: its floor-8 fight is an
Exordium Wildlife ({Fungi Beast, Spike Slime (M)}); the sim's mis-rolled
monster survived the capture's killing blow, so the sim never reached the
victory path — no Burning Blood +6 (the `hp 50 -> 44` field) — and never
assembled the reward screen (the card/treasure/potion stream-counter and
`blizzard_potion_mod` fields), then sat parked in COMBAT to the artifact's
end (its final stop read "sim is in COMBAT").

| Run | Before | After | Class |
|---|---|---|---|
| STS01789 | first div seq 130 (hp 14 vs 6), sim dead by seq 134, RUN_OVER while the capture fought on | **CLEAN to run terminal, 147 records** | fixed (engine) |
| STS00353 | first div seq 97 (11 fields at a COMBAT_REWARD), parked in COMBAT, stop seq 169 | **CLEAN to run terminal, 238 records** | fixed (engine, same defect) |

Standing corpus after this fix: G6-main `--- 30 file(s), 10 not clean ---` —
the remaining ten are the same rows at the same seqs (EVENT class (d) x3,
gold class (c) x4, FairyPotion class (c) x2, STS01068's grid stop); b45+b47
twelve and the bottle seven byte-identical to the section above. Full-output
diff between the two corpus runs shows exactly the two artifacts above
changed, both to CLEAN.

## `wave3-followup` — RecallOption modelled; the `Recall` stops are gone (2026-07-28)

The class-(c) frontier from stage 2 is closed. `RecallOption`
(RecallOption.java; CampfireUI.java:94-96) is now a real campfire button:

- **Presence**: appended AFTER the veto sweep whenever
  `Settings.isFinalActAvailable && !Settings.hasRubyKey`. isFinalActAvailable
  is PROFILE-derived (the AND of all three characters' _WIN prefs,
  Settings.java:642) and TRUE for the frozen capture profile, so the engine
  carries it as the profile constant `kFinalActAvailable` (rest_sites.hpp) on
  the same footing as the fixed A20; hasRubyKey is the run's `kKeyRuby` bit in
  the EXISTING `RunState.keys` bitfield (no new field, no schema question —
  the placeholder gained its first live writer and its bit constants).
- **Effect** (RecallOption.useOption -> CampfireRecallEffect.java:39-53 ->
  ObtainKeyEffect): the run records the RED key and the room COMPLETES — the
  campfire action is spent, no rest or smith at that site.
- **The ordering consequence nobody had modelled**: CampfireUI's cannotProceed
  auto-complete check (:97-104) runs AFTER the append, so a
  boss-relic-locked campfire does NOT auto-complete while the Ruby Key is
  still on offer; the auto-complete path is reachable again once the key is
  taken. Pinned by `RestSites.RecallSurvivesEveryVetoAndKeepsALockedCampfireOpen`
  (the existing auto-complete pins now take the key first, faithfully).
- **Harness**: the REST `choose` index space is the fork's USABLE-button list
  (`getValidRestRoomButtons` filters to `button.usable`), not the sim's full
  menu — every previously-replayed campfire was fully usable, so the identity
  held by luck. The mapping now walks the capture's N to the N-th usable
  ordinal and cross-checks the picked label ("rest"/"smith"/"lift"/"toke"/
  "dig"/"recall" — class SimpleName minus "Option") against the sim's option
  kind, stopping on any disagreement (`ReplayCommandMap.ARestChoose*` /
  `ARestLabelThatContradictsTheSimsMenuStops`). `RunState.keys` is
  differ-neutralized in `--replay` with the gap named: no capture surface
  exposes the game's key booleans, so the capture side is structurally 0 —
  the recall is validated by the sim walking past the spent campfire in
  lockstep instead.
- **Fuzz**: `MoveCat` 27 `RECALL` claimed (ledger namespace table updated;
  COUNT = 28), weight below rest/smith so soaks still mostly camp.

| Run | Before (stage-2 / union rows) | After | Delta |
|---|---|---|---|
| STS00052 | zero-diff over 79, stop seq 78 rest `Recall` | **CLEAN to run terminal, 97 records** | +18, frontier gone |
| STS00054 | zero-diff over 123, stop seq 122 rest `Recall` | **CLEAN to run terminal, 155 records** | +32, frontier gone |

**b45+b47 is now `--- 12 file(s), 0 not clean ---`** — the first time the
whole twelve-artifact corpus replays CLEAN to terminal. G6-main thirty and
the bottle seven byte-identical to the previous section (10 / 3 not clean,
same rows); full-output diff shows exactly the two artifacts above changed.

## `wave3-followup` — the Smoke-Bomb escape-settlement race is classified (2026-07-28)

STS00241 seq 96 — the wave-2 record's "documented benign" single-record
transient — now classifies as **RACE** instead of reading as a divergence.
The recognition is the escape-animation analogue of the obtain race and
equally narrow, split the same way: the FIELD-SET rule
(`is_escape_settlement_fields`, readout_shapes.hpp — every differing field
must be `hp` or a reward-assembly mover: `blizzard_potion_mod`,
`treasure_rng.*`, `potion_rng.*`; gold is deliberately OUT, assembled rewards
sit on the screen and never in the purse) is unit-tested JSON-free
(`EscapeSettlementRace.*`, with the outside-the-set negative control sweep),
and the WINDOW gates live at the one call site: capture screen `NONE` (still
in combat), sim phase `COMBAT_REWARD`, sim combat flagged PLAYER-ESCAPED. A
settlement computed WRONG rather than early still surfaces: the capture's own
settled records from the next seq on fail the window and diff for real.

| Run | Before | After |
|---|---|---|
| STS00241 | 218 records, ONE divergent record (seq 96, 8 fields) | **CLEAN to run terminal, 218 records, 1 escape-race** |

Whole-corpus check: exactly ONE escape-race record across all 49 artifacts
(STS00241's), every other verdict byte-identical — the recognition fired
nowhere else. The bottle seven is now `--- 7 file(s), 2 not clean ---` (the
two standing gold-class (c) rows, STS04925 s137 and STS06578 s81).

## `wave3-followup` — `boss_ids` compared for real (2026-07-28)

The act_boss row's re-owned mirror is discharged: `run_begin` records
`encounter_by_game_id(boss_list[0])->id` into `RunState.boss_ids[0]` — the
same EncounterId join and space the translator writes from the capture's
`act_boss` — and `--replay`'s `boss_ids` neutralization is REMOVED (it
existed only while the run layer had no writer). The whole 49-artifact corpus
re-ran with the field live: **zero `boss_ids` divergence anywhere**, all
verdicts byte-identical to the RecallOption section above (12/0, 30/10, 7/3).
Pinned by `RunBegin.BossIdsMirrorsTheRolledActBoss`.
