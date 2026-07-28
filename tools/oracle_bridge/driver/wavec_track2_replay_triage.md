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
