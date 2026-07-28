# B4.7 / B4.13 oracle spot-diff runbook — chests and ?-rooms

Two acceptance legs, two read-out modes, one runbook — because they share every
gate, every artifact and the same seed-from-one-record shape:

- **B4.7's oracle leg**, *"oracle spot-diff ≥ 2 treasure floors"* →
  `replay_run_diff --treasure`.
- **B4.13's** (and B4.10's) *arrival* leg — the ?-roll, the shrine/event split,
  the pool bookkeeping and the selected identity → `replay_run_diff --event`.
  Event **option flow** is deliberately NOT this mode's business; that stays
  `--replay`'s.

This runbook is the sibling of
[b45_reward_spotdiff.md](b45_reward_spotdiff.md),
[b414_neow_spotdiff.md](b414_neow_spotdiff.md) and
[b48_shop_spotdiff.md](b48_shop_spotdiff.md), and like the last of those it
deliberately does **not** restate the first one's §1 environment decision or §2
preflight: those are the same gates, unchanged, and duplicating them is how one
of them goes stale. Do §1 and §2 of that file first, with a **new campaign
tag**, then come back here.

## 1. Environment

Everything in `b45_reward_spotdiff.md` §1-§2 applies verbatim: the frozen game /
ModTheSpire / CommunicationMod build named in design §1.2, the fully-unlocked
audited profile, a human operator launching the game (an agent cannot), and a
`validate_artifacts.py --require-oracle` pass before anything is translated.

Two profile facts these two modes lean on specifically, both already audited:

- `Settings.isFinalActAvailable` is **true** (it reads the three per-class `_WIN`
  preferences, `Settings.java:642`). That is what puts a SAPPHIRE_KEY row on
  every chest open — see §3.
- `isNoteForYourselfAvailable` collapses to `ascension < 15` on this profile
  (`event_framework.hpp`, `AbstractDungeon.java:1360-1379`), which fixes the
  special-event pool's membership. At A20 the answer is **false**, so
  NoteForYourself is out of the Act-1 special list for every capture here.

Then translate exactly as `b48_shop_spotdiff.md` §3 does, and expect `OK` per
file with **zero unknown-field errors**. The CHEST slice is structurally
deferred (`chest_type` / `chest_open`, `translate.cpp`'s `parse_screen_state`)
and the EVENT slice is content-validated but storage-less — its `event_id` is
id-joined fail-loud and its option list shape-checked — so a failure here is
real drift, not the known gap.

## 2. Running the read-outs

Inside WSL:

```bash
tools/wsl_run.sh debug          # builds replay_run_diff among everything else
build/debug/tools/oracle_bridge/replay/replay_run_diff --treasure \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/<campaign>/run_*_a20_ironclad.jsonl
build/debug/tools/oracle_bridge/replay/replay_run_diff --event \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/<campaign>/run_*_a20_ironclad.jsonl
```

From the Windows host the binary is an ELF and cannot be exec'd directly
(`cannot execute binary file`), so go through the sanctioned boundary:

```bash
tools/wsl_run.sh --script tools/run_readout.sh --treasure /mnt/d/.../run_*.jsonl
```

Glob **`run_*_a20_ironclad.jsonl`**, not `run_*.jsonl`: the latter also matches
the `*.timing.jsonl` sidecars, which the translator correctly refuses with
`unknown record_kind "timing_header"` — twenty scary-looking `ERROR` lines that
mean only that the glob was too wide.

Both modes exit with the number of FILES that carried a divergence, so they
compose with `&&` in a campaign script.

## 3. What `--treasure` compares

A chest is a pure function of the state the room entry sees — `treasureRng`
only — so the mode seeds a `RunState` from the capture and never replays a
prefix. Per treasure ROOM (the first in-`TreasureRoom` record of a new floor):

### 3a. CONSTRUCTION, off the pre-entry record

`++floorNum` (`AbstractDungeon.java:1741`, trap 7 — before the floor-stream
reseed), then, **if the node symbol was `?`**, the ?-room entry (§4 trap 1),
then `roll_treasure_chest`.

| Field | Sim source / expectation |
|---|---|
| `screen_state.chest_type` | `ChestSize` from `treasure_chest_for_rolls` |
| `treasureRng` `{counter,s0,s1}` | **+2 exactly** — `getRandomChest`'s size roll then `randomizeReward`'s ONE shared contents roll (`AbstractDungeon.java:499-508`, `AbstractChest.java:54-60`) |
| every other run stream | **unchanged** — construction is treasureRng-only |
| all five relic pools | **unchanged** — the relic is a pool *front-pop* at OPEN time, so even `relicRng` stays put |

### 3b. OPEN, off the CHEST record

`open_treasure_chest`, then the **whole translated `RunState`** diffed against
the post-open record. That is what proves:

| Field | Why it moves |
|---|---|
| `treasureRng` | **+1 iff the chest rolled gold** — `Math.round(treasureRng.random(GOLD_AMT*0.9f, GOLD_AMT*1.1f))` (`AbstractChest.java:72`) and nothing else on the open path |
| `relic_pools[tier]` | the base relic is front-popped immediately, even though it is not yet *acquired* |
| `miscRng` | **not here** — first touched at claim time by `acquire_relic`'s `onEquip` bodies |

and the reward rows themselves, against `screen_state.rewards[]` with the key
row accounted for (§4 trap 2): row kinds in order, the gold amount, and the
relic's game id.

### 3c. The CLAIM walk

A `RunController` parked in `COMBAT_REWARD` with the captured post-open
`RunState` and the assembled screen, driven through the capture's own claim
commands, with the whole `RunState` diffed against **every** later in-room
record. A `proceed` at a chest or a reward screen is elided (it bounces the map
without leaving the room — the `main.cpp` header's lazy-leave note); a map
`choose` ends the visit.

## 4. What `--event` compares

Per captured `?` that stayed an event (the first in-`EventRoom` record of a new
floor, floor 0 excluded — Neow is `--neow`'s subject):

| Step | What runs | What is compared |
|---|---|---|
| node | — | the pre-entry MAP record's `next_nodes[i].symbol` must be `?` |
| entry | `dispatch_event_room_entry_relics` | Ssserpent Head's 50 gold / an unused Maw Bank's 12, which fire against the ORIGINAL EventRoom (`AbstractDungeon.java:1754-1779`) |
| roll | `event_room_roll` | must resolve to `EVENT`; **`eventRng` +1, byte-identical** — the one committed eventRng advance in the game, ever |
| selection | `generate_event` | `eventRng` must be **unmoved**: both selection draws are made on a throwaway duplicate and discarded (`AbstractDungeon.java:1865`, `EventRoom.java:28`) |
| identity | the registry join | `screen_state.event_id` → `EventId`, against the sim's selection |
| arrival | — | the **whole translated `RunState`**: the three pity floats, the three membership bitsets, `event_flags`, gold, every relic counter (the Tiny Chest one included) |
| options | `on_enter` + `build_menu` | **advisory**: the entry page's button count. Reported, never folded into the verdict — a body's first page is content the selection layer does not own |

**The identity join goes through `event_id`, not `event_name`.** The capture
carries both, and for six of the eighteen ids these campaigns show they differ:
`Liars Game`/"The Ssssserpent", `Golden Wing`/"Wing Statue",
`FaceTrader`/"Face Trader", `Fountain of Cleansing`/"The Divine Fountain",
`Bonfire Elementals`/"Bonfire Spirits", and `Transmorgrifier`/"Transmogrifier"
— the game's own misspelling is the *id*, and the UI spells it correctly.
`registry/events.yaml`'s `game_id` column is the id, so the name is carried
only to make a read-out line readable. `EventJoin.UnknownNameFailsLoud` pins
that a *name* passed to the join is refused rather than silently becoming
"no event".

## 5. The traps these read-outs exist to catch

1. **A chest or a shop can be a `?` room, and `room_type` will not say so.**
   `EventHelper.roll` replaces an EventRoom with a real TreasureRoom / ShopRoom /
   MonsterRoom (`AbstractDungeon.java:1763-1779`), and the capture reports the
   *resolved* type. The `?` path costs one committed `eventRng` float and runs
   the onEnterRoom fan-out first; a `T` node costs neither. The only capture-side
   evidence is the pre-entry MAP record's `next_nodes[i].symbol`, which is why
   both modes read it. **This trap fired during development**: STS00052's floor-5
   chest is a `?`, and a first pass that assumed a treasure node reported the
   missing `eventRng` draw as an engine divergence.
2. **Every Act-1 chest open carries an extra trailing `SAPPHIRE_KEY` row.**
   `isFinalActAvailable && !hasSapphireKey` holds (`AbstractChest.java:95-96`),
   so `AbstractRoom.addSapphireKey` (`:545-547`) appends a row LINKED in both
   directions to the base relic (`RewardItem.java:86-93`). It consumes no RNG
   and the engine models no key row, so the read-out elides it — but narrowly.
   A key row on a **non**-treasure screen, a key row that is not trailing or not
   linked, two key rows, or a **missing** key row is a divergence. See §6 for
   the two legitimate absences. All of it is
   `strip_sapphire_key_row` in `readout_shapes.hpp`, with named tests.
3. **Which row the capture claims decides whether the run keeps the relic.**
   Claiming the RELIC marks the key `isDone`/`ignoreReward`
   (`RewardItem.java:298-300`) and the run keeps the relic; claiming the **KEY**
   does the exact reverse (`:317-322`) and the base relic is **abandoned**. Both
   are legal captures — the `random-legal` policy takes whichever — so the
   read-out must map the claim rather than assume. A harness that "helpfully"
   claimed the relic when the capture claimed the key would gain a relic the run
   never had and diverge on every downstream floor.
4. **`++floorNum` happens before the roll.** It is not cosmetic for `--event`:
   `getEvent`'s filter gates Dead Adventurer and Mushrooms on `floorNum > 6`
   (`AbstractDungeon.java:1949-1982`), so an off-by-one floor silently changes
   the draw list the pool index addresses. (It also caught itself: seeded from
   the pre-entry record, the very first `--event` sweep reported `floor: N -> N-1`
   on every sighting.)
5. **`generate_event` must leave `eventRng` byte-identical.** The game's
   "duplicate" is a counter *replay*, not a copy, and the duplicate is discarded
   unassigned. A mode that saw `eventRng` move across selection would be looking
   at a draw-then-rewind port, which stage-a §3.2 forbids.
6. **A campaign with no `oracle` block translates to a stateless shell.**
   `b45_rewards` (the pre-oracle-gate capture that `b45_rewards_oracle_*`
   replaced) carries **zero** oracle blocks across all six runs, so every stream,
   pity float and membership bitset translates as value-init and `--event`
   reports a fabricated divergence on every sighting. This is exactly what §1's
   `validate_artifacts.py --require-oracle` gate is for; run it, and do not
   point either mode at a campaign that has not passed it.

## 6. The known-benign mismatches

- **The trailing `SAPPHIRE_KEY` row** (trap 2) — expected, elided, and reported
  on the `OPEN OK` line so its presence is visible rather than silent.
- **Two legitimate ABSENCES of that row.** (a) Once the run has claimed a key,
  `!Settings.hasSapphireKey` is false and no later chest appends one — the mode
  tracks the claim itself. (b) A **N'loth's Mask** open with a live charge:
  `removeOneRelicFromRewards` (`AbstractRoom.java:549-559`) deletes the first
  RELIC row **and** the row after it when that row is its `relicLink`, taking
  the key with the relic. A mask that left a RELIC row behind is *not* this case
  and is still flagged.
- **The obtain race.** `ShowCardAndObtainEffect` adds a transformed/obtained
  card to the master deck only when its animation completes
  (`ShowCardAndObtainEffect.java:30-45`) while the removal is immediate, so
  every capture dump in between is one card short and the card appears in the
  first dump after the effect finishes. `--event` recognises this **narrowly** —
  every differing field must be `master_deck_count` or a `master_deck[i]` at or
  past the seeded deck's end, so the shared prefix is identical and no stream,
  pool bit or `event_flags` may differ — prints a `RACE` line naming it, and
  counts it apart from the zero-diff total. It is B1.3's deferred
  capture-fidelity gap, carried as a B5.2 obligation row, not an engine defect.
- **The `kEventTransformRedPool` emission order** is a known open row owned
  elsewhere. An event *content* diff that traces to that pool order is that row,
  not a new finding. Nothing in either of these two modes touches it: selection
  and arrival never build a transform pool.

## 7. Recording the result

For **B4.7**: two treasure floors zero-diff discharges the acceptance's oracle
leg. Tick the leg, drop the "the task stays unchecked until its required
live-game spot-diff can run" line from its ledger block, and record the campaign
ids plus the two seeds in its Log.

For **B4.13**: this mode discharges the ARRIVAL half only. Match and Keep's deal
and NoteForYourself's profile pin are still outstanding, and neither event has
been captured — see §8.

Anything else is a divergence: follow conventions §5 — re-read the cited Java
first, audit the fork's strip patches second, promote the reproducer to a
regression fixture, and do not tick the box.

## 8. What actually ran — 2026-07-28

**No new campaign was launched.** As with B4.8, the existing corpus already
carried the sightings, and no capture in it has to be re-taken.

### 8a. `--treasure` — the two captured treasure floors

Every oracle-carrying campaign in `_oracle_data/campaigns` was swept (161 runs
across `b13_on20`, `b13_off20`, `b13_on20b`, `b13_offscript`, `b13_offscript2`,
`b14_accept`, `b14_accept2`, `b45_rewards_oracle_*`, `b45_rewards_oracle2_*`,
`b47_treasure_oracle_*`). It holds **exactly two** treasure rooms, and they are
the two the B4.7 brief names:

```
CHEST    OK   STS00052 floor=5 MediumChest tier=RARE gold=no treasureRng 2->4 entered via a ? node (eventRng +1 first)
WALK     OK   STS00052 floor=5 5 in-room records compared, chest skipped
CHEST    OK   STS00054 floor=9 SmallChest tier=COMMON gold=no treasureRng 3->5 entered via a 'T' node
OPEN     OK   STS00054 floor=9 seq=116 rows=[RELIC] treasureRng 5->5; capture carries the expected trailing SAPPHIRE_KEY row
CLAIM    KEY  STS00054 floor=9 seq=119: the capture claimed the SAPPHIRE_KEY row, abandoning the linked base relic (RewardItem.java:317-322)
WALK     OK   STS00054 floor=9 5 in-room records compared, chest opened
--- 2 file(s): 2 treasure room(s), construction clean 2, 1 opened (1 clean) / 1 skipped, in-room walks clean 2 (+0 partial), 0 divergence(s) ---
```

**Both zero-diff.** What each one is worth:

| Run | Floor | Node | Chest | What it proves |
|---|---|---|---|---|
| STS00052 | 5 | `?` → TREASURE | MediumChest, tier RARE, no gold | the ?-resolved chest path; the two construction draws; the **skip**, which must move nothing |
| STS00054 | 9 | `T` | SmallChest, tier COMMON, no gold | the direct chest path; the **open**, the gold branch NOT taken, the pool front-pop, the key row, and a key claim |

Together they cover both entry routes, two of the three sizes, two of the three
tiers, the open and the skip. Neither rolled gold, so
`Math.round(treasureRng.random(GOLD_AMT*0.9f, GOLD_AMT*1.1f))` is **not**
exercised by a live capture and stays on the tier-2 test — say so rather than
implying the whole open path is oracle-proved.

STS00054's open is the interesting one. The capture's `random-legal` policy
claimed the **key**, so the run walked away from a Bag of Preparation it had
already popped from the common pool: `common` 32 → 31 at `relicRng` **unmoved**,
and `relics` still just `[Astrolabe]` on the next floor. The read-out reproduces
exactly that — pool popped, relic not owned — which is trap 3 observed rather
than assumed.

### 8b. `--event` — every captured non-Neow sighting

The same ten campaigns hold **88** ?-room sightings that stayed events. **All 88
are zero-diff**; one of them is additionally flagged `RACE` for the §6 obtain
race, and every one of the 88 also matched the advisory entry-page option count.

| Corpus | Runs | Sightings | Zero-diff | Diverged |
|---|---|---|---|---|
| `b13_on20` + `b13_off20` + `b14_accept` + `b14_accept2` + `b45_rewards_oracle_*` + `b45_rewards_oracle2_*` + `b47_treasure_oracle_*` | 101 | 57 | 57 (1 obtain race) | 0 |
| `b13_on20b` + `b13_offscript` + `b13_offscript2` | 60 | 31 | 31 | 0 |

The b13 campaigns share one 20-seed set (they are the fork strip-patch A/B
sweeps) and `b14_accept*` re-runs its first ten, so the sightings repeat across
them under different policy runs — which is coverage of the *harness*, not of
new seeds. The genuinely distinct seeds are b13's 20, b45's 5 + 6 and b47's 30.

`b45_rewards`'s three sightings are **excluded, and it is the only exclusion**:
that campaign predates the oracle gate and carries no oracle block at all (trap
6). Its two unique-seed sightings (STS00041) are the only captured events not
read out.

**Eighteen distinct events**, spanning all three pools:

| Pool | Seen | Not seen |
|---|---|---|
| eventList (11) | Big Fish, The Cleric, Dead Adventurer, Golden Idol, Golden Wing, World of Goop, Liars Game, Living Wall, Scrap Ooze, Shining Light — **10 of 11** | Mushrooms |
| shrineList (6) | Transmorgrifier, Purifier, Upgrade Shrine, Wheel of Change — **4 of 6** | Match and Keep!, Golden Shrine |
| specialOneTimeEventList (14; 8 Act-1-reachable) | Bonfire Elementals, FaceTrader, Fountain of Cleansing, WeMeetAgain — **4 of 8** | Accursed Blacksmith, Lab, NoteForYourself (A20-gated out), The Woman in Blue |

Two of those are worth naming as gate evidence rather than as coverage:
**Fountain of Cleansing** (STS00048) only enters the pool when the player
`isCursed`, and **The Cleric** (STS00068 / STS00072 / STS00009) only when
`gold >= 35` — both drawn from a filtered list the read-out reproduced exactly,
which is the per-key gates of `getShrine` / `getEvent` observed live.
**Dead Adventurer** at STS00054 floor **12** is the `floorNum > 6` gate on the
right side of trap 4.

**Match and Keep is not in the corpus**, so B4.13's card-dealing spot-check is
still open and this read-out does not close it. Neither does it touch the
NoteForYourself profile pin, which A20 gates out of the pool entirely — that one
needs a sub-A15 capture, which no campaign here is.

### 8c. Frozen in tests

The two shape decisions both modes depend on are
`tests/replay_readout_shapes_test.cpp` (`SapphireKeyRow.*`,
`RewardClaimMapping.*`, `EventJoin.*`) — the key row present / missing /
on-the-wrong-screen, both legitimate absences, the claim mapping in both
directions, and the fail-loud join. They run in every preset and need no
artifact.
