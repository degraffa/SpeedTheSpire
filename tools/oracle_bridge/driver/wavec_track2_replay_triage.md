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
