# B4.7 / B4.13 oracle spot-diff runbook — chests and ?-rooms

Two acceptance legs, two read-out modes, one runbook — because they share every
gate, every artifact and the same seed-from-one-record shape:

- **B4.7's oracle leg**, *"oracle spot-diff ≥ 2 treasure floors"* →
  `replay_run_diff --treasure`.
- **B4.13's** (and B4.10's) *arrival* leg — the ?-roll, the shrine/event split,
  the pool bookkeeping and the selected identity → `replay_run_diff --event`.
  Event **option flow** is deliberately NOT this mode's business; that stays
  `--replay`'s. The one exception is a body that deals in its **constructor**,
  because that happens at arrival rather than at a button press: Match and
  Keep's twelve-card board is compared here (§8c), and it is B4.13's remaining
  card-dealing leg.

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

The `--event` corpus is named once, in
[b413_event_sweep.sh](b413_event_sweep.sh), so a re-run selects exactly the same
files in the same order and is byte-comparable against §8b / §8c:

```bash
tools/wsl_run.sh --script tools/oracle_bridge/driver/b413_event_sweep.sh
tools/wsl_run.sh --script tools/oracle_bridge/driver/b413_event_sweep.sh --deal
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
| deal | the body's own `on_enter`, **only** for a body that spends RNG in its constructor | `miscRng` and `shuffleRng` (which live in `CombatState`, so `diff_run_states` never sees them); `cardRng` folds into the arrival diff below. Today that is Match and Keep alone — §8c |
| arrival | — | the **whole translated `RunState`**: the three pity floats, the three membership bitsets, `event_flags`, gold, every relic counter (the Tiny Chest one included) |
| options | `build_menu` | **advisory**: the entry page's button count. Reported, never folded into the verdict — a body's first page is content the selection layer does not own |
| board | `match_menu` / `match_choose`, driven by the capture's own picks | **not advisory** — the twelve dealt identities against every position the capture names, every attempt's match/miss, the cards the run kept, and a per-round walk. §8c states exactly what that does and does not reach |

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
  (`ShowCardAndObtainEffect.java:30-45,94-108`) while the removal is immediate, so
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

**Match and Keep is not in *these ten* campaigns**, which is why B4.13's
card-dealing spot-check stayed open after this sweep. It is closed by a
different campaign in §8c below; the coverage table above is deliberately left
describing *this* corpus, because a table that quietly absorbs a later
campaign's sightings stops being re-derivable. This sweep still does not touch
the NoteForYourself profile pin, which A20 gates out of the pool entirely — that
one needs a sub-A15 capture, which no campaign here is.

### 8c. `--event` — the Match and Keep! constructor deal

Campaign **`b4x_greedy_pilot_20260728T041406Z_claude01`** (10 runs, A20
Ironclad) holds **six complete Match and Keep interactions** — thirteen EVENT
records each: `Continue`, `Play`, five attempts of two grid picks, `Leave`.
Four of the six matched at least one pair and two matched nothing, so both
branches of `updateMatchGameLogic` are exercised.

```
  DEAL     OK   STS00212 floor=4: 5/12 screen position(s) named by the capture and identical, 5 attempt outcome(s) reproduced, 10 grid round(s) walked, kept {True Grit, Decay}
  DEAL     OK   STS00465 floor=2: 5/12 screen position(s) named by the capture and identical, 5 attempt outcome(s) reproduced, 10 grid round(s) walked, kept {Havoc, Writhe}
  DEAL     OK   STS00506 floor=3: 7/12 screen position(s) named by the capture and identical, 5 attempt outcome(s) reproduced, 10 grid round(s) walked, kept {nothing}
  DEAL     OK   STS00711 floor=2: 6/12 screen position(s) named by the capture and identical, 5 attempt outcome(s) reproduced, 10 grid round(s) walked, kept {Iron Wave}
  DEAL     OK   STS00783 floor=2: 5/12 screen position(s) named by the capture and identical, 5 attempt outcome(s) reproduced, 10 grid round(s) walked, kept {nothing}
  DEAL     OK   STS00947 floor=3: 2/12 screen position(s) named by the capture and identical, 5 attempt outcome(s) reproduced, 10 grid round(s) walked, kept {Bash, Pummel, Limit Break}
--- 10 file(s): 13 sighting(s), 13 zero-diff (+0 clean but for the known obtain race), 0 diverged; entry-page option count matched 13 of 13 advisory check(s) ---
--- constructor deals: 6 read out, 6 zero-diff; 30 screen position(s) named by a capture and compared, 30 attempt outcome(s) reproduced, 60 grid round(s) walked ---
```

Reproduce with
`tools/wsl_run.sh --script tools/oracle_bridge/driver/b413_event_sweep.sh --deal`;
the same script with no argument re-runs §8b's corpus, which this change leaves
**byte-identical**. The `--verbose` flag adds the twelve-card board line quoted
further down.

**Why the deal extended `--event` rather than becoming a mode of its own.**
Match and Keep is the only Act-1 body that spends RNG in its **constructor**
(`GremlinMatchGame.java:55-61`, run at `EventRoom.onPlayerEntry`), so three
streams have already moved by the time CommunicationMod dumps the "Continue"
page. The arrival step was therefore comparing a post-deal capture against a
pre-deal sim and reporting all six sightings as `cardRng +5` divergences that
were nothing of the kind. That is a defect in what the arrival step models, not
a missing mode: the deal happens *at arrival*, which is the moment this mode
already owns, and steps 0–3 of §4 are what identify the sighting as Match and
Keep in the first place. A separate mode would have had to duplicate the
seeding, the ?-roll, the throwaway selection and the identity join to reach the
same point.

**The three streams**, each read from the Java:

| Stream | What moves it | Where it is compared |
|---|---|---|
| `cardRng` | the three `AbstractDungeon.getCard(rarity)` pool draws (`:67-69`) and **both** `returnRandomCurse` calls (`:70-71`) — **+5** | §4's whole-`RunState` diff, once the sim is post-deal |
| `miscRng` | the board shuffle's one `randomLong` (`:58`) — **+1** | explicitly: it lives in `CombatState`, which `diff_run_states` does not reach |
| `shuffleRng` | `returnColorlessCard`'s `randomLong` (`AbstractDungeon.java:1101`) — **+0 here** | explicitly, and it is not a dead check — see below |

`shuffleRng` is untouched because these captures are **A20** and
`initializeCards` branches on `ascensionLevel >= 15` (`:66-71`), drawing a
second curse instead of a colorless uncommon. The `< 15` branch — the only one
that spends `shuffleRng`, and the only one that reaches
`draw_colorless_uncommon` — is therefore **not oracle-proved** and stays on its
tier-2 test. What the untouched stream *does* prove is the seeding: the five
floor streams are derived by the read-out as `floor_stream(seed, floor)` rather
than copied from the capture, so an unmoved `shuffleRng` that matches the
capture is that formula checked against the live game.

**What the capture actually exposes, and what it does not.** The artifact never
dumps `cards.group`. It dumps the fork's event option list, and three things
happen to it on the way out (`ChoiceScreenUtils.getEventScreenChoices`,
`patches/GremlinMatchGamePatch.java`):

1. **Cards are keyed by SCREEN POSITION, not by board slot.**
   `InitializeCardsPatch` stores `(i%4) + 4*(i%3)` per card — `placeCards`' own
   layout arithmetic (`:280-281`) read as a 4-wide grid index — which for
   `i = 0..11` is `[0, 5, 10, 3, 4, 9, 2, 7, 8, 1, 6, 11]`. A `card7` label
   names `cards.group[10]`, not slot 7. Six of the twelve slots move, so a
   read-out that skipped the hop would agree with the capture on the other six
   and look half-right.
2. **The list is compacted.** `getOrderedCards()` sorts by that position and
   keeps only cards still on the board **and** still face down, so the first
   pick of an attempt and both cards of a matched pair drop out of it. A
   `choose N` indexes *that* list, not the board.
3. **A revealed card carries its identity but no position.** `revealedCards` is
   permanent, so a mismatched pair keeps showing its `cardID` after flipping
   back down — and a **matched** pair never shows its name at all, because it
   leaves `cards.group` in the same frame it is revealed (`:221-222`).

The decode recovers the positions exactly anyway, because the list is sorted:
entry *j* is the *j*-th smallest still-offered position, and every hidden entry
re-states its own position in its label — so the reconstruction is **checked at
every entry of every record** rather than assumed. It fails loud on the first
disagreement. Note the consequence for reading an artifact by hand: the *first*
grid record is the least informative one (all twelve face down, no names), and
identities accumulate as the walk proceeds.

**What is therefore compared, stated precisely.** Not "twelve cards":

- every screen position the capture ever names, **position for position** — 2 to
  7 per sighting, **30 across the six**;
- every attempt as a **pair predicate** (`board[a] == board[b]` iff the capture
  matched) — **30 of 30**, which reaches positions the capture never names: the
  True Grit pair STS00212's first attempt took is pinned this way plus the deck
  delta, never by a label;
- the **multiset of cards the run kept**, which is the only witness for a
  matched pair's identity and the only thing that can settle the **fifth**
  attempt's outcome — no grid record follows it, the next screen is `Leave`;
- a **ten-round walk** per sighting driven through the engine's own
  `match_menu` / `match_choose`, whose still-face-down set must equal the next
  captured record's offered set — **60 rounds, all clean**. That re-derives the
  same facts through the state machine rather than through the board array, so
  a right board with wrong flip/remove bookkeeping is still caught.

Positions never flipped and never matched are constrained by the pairing
invariant alone, and the read-out counts them as neither.

Each of the six boards is structurally exactly what `initializeCards` produces —
six identities, each dealt twice, being one RARE, one UNCOMMON, one COMMON, two
curses and Bash. STS00212's, by screen position:

```
0:Decay | 1:Decay | 2:Shame | 3:Bash | 4:Fire Breathing | 5:Fire Breathing
6:Berserk | 7:Shame | 8:Bash | 9:True Grit | 10:True Grit | 11:Berserk
```

Berserk RARE, Fire Breathing UNCOMMON, True Grit COMMON, Decay + Shame the two
curses, Bash from `Ironclad.getStartCardForEvent`. That is corroboration, not a
compared field — the read-out asserts no rarities.

This campaign also carries the first captured **Mushrooms** (STS00711 floor 8)
and **The Woman in Blue** (STS00212 floor 10), both listed as unseen in §8b's
pool table; all thirteen of its sightings are zero-diff.

### 8d. Frozen in tests

The two shape decisions both modes depend on are
`tests/replay_readout_shapes_test.cpp` (`SapphireKeyRow.*`,
`RewardClaimMapping.*`, `EventJoin.*`) — the key row present / missing /
on-the-wrong-screen, both legitimate absences, the claim mapping in both
directions, and the fail-loud join. They run in every preset and need no
artifact.

The board alignment §8c depends on is `tests/replay_mk_board_test.cpp`
(`MatchBoardPositions.*`, `MatchBoardDecode.*`, `MatchDealCompare.*`), against
`tools/oracle_bridge/replay/src/mk_board.hpp`. It exists for the same reason:
every failure mode there is one that produces a **clean-looking** deal line
rather than an error. It pins the permutation against the fork patch's own
table and both directions of its inverse; the decode against the real STS00212
walk (revealed positions, attempt outcomes, offered sets) and against a
mislabelled position, a wrong-sized record, an out-of-range `choose`, an
identity that changes, an unclassifiable attempt and an unexplainable deck
delta; and the comparison against a swapped pair of named positions, a broken
pair predicate at positions the capture never named, a board that lost the
pairing invariant, and — the concrete regression the header exists for — the
**identity mapping**, i.e. comparing screen positions against board slots with
no permutation at all.
