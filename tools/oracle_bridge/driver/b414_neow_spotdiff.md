# B4.14 oracle spot-diff runbook — the Neow blessing

The B4.14 acceptance's oracle leg: **the Neow screen across >= 10 seeds
zero-diff — options AND post-choice state**. The tier-2 leg (the four-option
roll against a draw-by-draw hand derivation, the category tables, every
payout's stream attribution, the drawbacks and grids) is `neow_test` and runs
in CI; this leg needs the live game, which is **launched manually by a human
operator** (`CLAUDE.md`, oracle-bridge section) — an agent cannot start it.
Everything up to the capture is prepared below; the capture and the read-out
are the remaining human steps.

This runbook is the sibling of
[b45_reward_spotdiff.md](b45_reward_spotdiff.md) and deliberately does **not**
restate its §1 environment decision or its §2 preflight: those are the same
gates, unchanged, and duplicating them is how one of them goes stale. Do §1
and §2 of that file first, with a **new campaign tag**, then come back here.

## 1. Why floor 0 makes this the cheapest capture in the set

Neow is on screen before any other content, so the capture is short: launch,
read the four options, take one, read the state again, quit. Ten seeds is ten
of those. Nothing downstream has to be modelled for the diff to be meaningful,
which is exactly why the acceptance asks for ten seeds rather than three.

Two properties make the comparison unusually sharp:

- **`neowRng` is a fresh `Random(Settings.seed)`** (NeowEvent.java:363, trap
  17) whose counter is **0** before the blessing and **5** after it — one draw
  per category plus category 2's drawback. `GameStateConverter` dumps
  `neowRng` as `{counter, s0, s1}` and reports it as **null** before the
  blessing screen (PROTOCOL.md §"Event-scoped"), so the capture itself tells
  you whether you are looking at the intro screen or the blessing screen.
- The sim's `RunPhase::NEOW` **is** the blessing screen: `run_begin` rolls the
  four options at run start, because NeowEvent's intro screens (screenNum 0/1)
  consume only MathUtils flavour draws. So `run_begin(seed, 20).run.neow_rng`
  must equal the capture's `neowRng` at the blessing screen, counter and raw
  state both.

## 2. Capture (operator, Windows host)

Put **ten or more** base-35 seed strings, one per line, in
`D:/STS_BG_Mod/_oracle_data/campaigns/b414_seeds.txt`. There is no seed to
avoid: floor 0 has no combat, no elite and no chest, so none of the B4.5
exclusions apply. Prefer a spread that reaches every category-2 drawback — the
drawback is `neowRng` draw 3, so it is uniform over the four; ten seeds will
usually cover all four, and the read-out below says what to do if one is
missing.

Derive a never-before-used id from the successful preflight tag of
`b45_reward_spotdiff.md` §2:

```bat
set B414_NEOW_ID=b414_neow_oracle_%B45_TAG%

C:\Python39\python.exe orchestrator.py ^
    --campaign-id %B414_NEOW_ID% ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/b414_seeds.txt ^
    --policy random-legal ^
    --fresh
```

`random-legal` takes one of the four blessings on every seed and then walks on,
so each run's JSONL carries the blessing screen, the choice, any payout
sub-screen (card pick / master-deck grid / the potion reward screen) and the
state after it. The same id / `--fresh` / symlink rules as B4.5 apply verbatim.

Then require the oracle gate before translating:

```bat
C:\Python39\python.exe validate_artifacts.py --require-oracle ^
    --campaign D:/STS_BG_Mod/_oracle_data/campaigns/%B414_NEOW_ID%
```

## 3. Translate

```bash
tools/wsl_run.sh debug          # builds translate_cli among everything else
build/debug/tools/oracle_bridge/translator/translate_cli \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/$B414_NEOW_ID/run_*.jsonl
```

Expected: `OK` per file, **zero unknown-field errors**. The Neow screen arrives
as `screen_type: EVENT` with `event_id: "Neow Event"`; B4.14 taught the
translator that sentinel, so a failure here is real drift (a renamed id, a new
option field), not the known gap.

## 4. The read-out — what must match

Two comparisons per seed. Both are required; the first alone would pass on a
correct option list with a wrong payout, and the second alone would pass on a
right payout reached from a wrong menu.

### 4a. THE OPTIONS, at the blessing screen

The capture's `screen_state.options[]` carries four localized labels. The sim
has `RunController::neow.option_type[0..3]` and `option_drawback[2]`. The join
is by MEANING, not by string: map each label through the table below (the
strings come from `NeowReward`'s `TEXT[]` indices, cited per row in
`neow.hpp`'s category tables) and compare the resulting four-tuple.

| Category | Sim field | What the label says |
|---|---|---|
| 0 | `option_type[0]` | one of THREE_CARDS / ONE_RANDOM_RARE_CARD / REMOVE_CARD / UPGRADE_CARD / TRANSFORM_CARD / RANDOM_COLORLESS |
| 1 | `option_type[1]` | one of THREE_SMALL_POTIONS / RANDOM_COMMON_RELIC / TEN_PERCENT_HP_BONUS / THREE_ENEMY_KILL / HUNDRED_GOLD |
| 2 | `option_drawback[2]` then `option_type[2]` | the label is the DRAWBACK text followed by the reward text — read it in that order, it is the order it was rolled in |
| 3 | `option_type[3]` | always the boss-relic swap |

Also compare, at this screen:

| Field | Sim source |
|---|---|
| `neowRng` `{counter, s0, s1}` | `run_begin(seed, 20).run.neow_rng`; counter must be **5** |
| every other stream's counter | unchanged from `run_begin` — the blessing roll must move nothing else |
| `max_hp` / `current_hp` | 75 / 68 at ascension 20 (the run-setup order; see the A20 ledger row) |

A category-2 label whose drawback text appears AFTER the reward text is the
single most likely sign of a wrong reading, not of a divergence — check the
label order before filing anything.

### 4b. THE POST-CHOICE STATE, after the payout has fully resolved

Take the last record after the payout's own screen closed (after the card was
picked or skipped, after both grid selections, after the reward screen's
Proceed) and diff its translated `RunState` against the sim stepped through the
same choices from `run_begin(seed, 20)`:

| Field | Why it moves |
|---|---|
| `gold` | HUNDRED_GOLD / TWO_FIFTY_GOLD, and the NO_GOLD drawback emptying the purse |
| `max_hp`, `hp` | the two max-HP gains (which also heal), the 10 % max-HP loss (which clamps), the PERCENT_DAMAGE drawback |
| `master_deck[]` | card offers taken, removals, transforms, upgrades, and the CURSE drawback's appended curse |
| `relics[]` | the common/rare relic, Neow's Lament, and the boss swap (which **removes** Burning Blood) |
| `relic_pools[]` + `relic_pool_count[]` | pool front-pops, including a rejected `canSpawn` pop being consumed rather than returned |
| `potions[]` | claims off the three-potion reward screen (A20 has two slots, so at most two of three) |
| `card_blizz_randomizer` | **moves on the three-potion blessing** — see the trap below |
| `neowRng`, `cardRng`, `potionRng`, `relicRng` counters | the stream attribution this whole task is about |

Drive the sim side by hand for now (`run_begin`, then the `CHOOSE` sequence the
artifact's `action_command` records), exactly as B4.5 §5 describes — the
generalized replay adapter is still the undischarged B1.6 obligation.
`diff_run_states` (`tools/diff_harness`, `sts/diff/differ.hpp`) prints the
field-by-field diff.

### The three traps the read-out exists to catch

1. **A colorless blessing moves `cardRng`, not only `neowRng`.**
   `getColorlessCardFromPool` indexes through `CardGroup.getRandomCard(true,
   rarity)`, whose `true` is `AbstractDungeon.cardRng` (CardGroup.java:509-524)
   — three draws, one per offered card — while the three rarity rolls stay on
   `neowRng`. A capture of a colorless option is the cheapest possible proof of
   this split; try to get at least one in the ten. The CURSE drawback's card is
   also a `cardRng` draw, taken **after** the payout's.
2. **The three-potion blessing moves `cardRng` AND the card pity.** Opening the
   combat reward screen from a NeowRoom makes `setupItemReward` roll a full
   `getRewardCards()` row (CombatRewardScreen.java:72-96) that `NeowReward`
   then deletes (NeowReward.java:273-283). If a capture of that option shows
   `cardBlizzRandomizer` unchanged, the reading of `setupItemReward` is wrong
   and this is a real divergence.
3. **The boss swap never returns Black Blood.** `loseRelic(relics.get(0))` runs
   before the BOSS-pool draw (NeowReward.java:243-247) and BlackBlood.canSpawn
   is `hasRelic("Burning Blood")`, so the front pop is rejected and consumed.
   A capture whose boss swap yields Black Blood contradicts the model.

### The one known-benign mismatch

The RED reward pools (`kIroncladCommonPool` / `...UncommonPool` /
`...RarePool`) are still emitted in **registry-id order** — the interim
library-order deviation documented in B4.5 §6. A THREE_CARDS /
THREE_RARE_CARDS / ONE_RANDOM_RARE_CARD / TRANSFORM offer whose *shape* agrees
(right count, right rarity, right stream counters) but whose *card ids* differ
is that deviation, not a Neow bug. **The COLORLESS offers are exempt**: their
pools are sorted by cardID before indexing (CardGroup.java:509-524), so
`kColorlessUncommonPool` / `kColorlessRarePool` are order-exact and a colorless
id mismatch IS a divergence.

Neow's card offers are, incidentally, extra evidence for pinning the RED
library order (B4.5 §6): each offer names three ids drawn at known `neowRng`
indices.

## 5. Recording the result

Zero-diff on >= 10 seeds discharges the acceptance: tick B4.14's checkbox, drop
the "code landed; blocked on the manual oracle capture" header from its ledger
block, and record the campaign id plus the seed list in its Log. Anything else
is a divergence: follow conventions §5 — re-read the cited Java first, audit
the fork's strip patches second, promote the reproducer to a regression
fixture, and do not tick the box.

## 6. What actually ran — 2026-07-27

**No `b414_neow_oracle_*` campaign was ever launched, and §2 was not run.** By
the time the read-out came due, three strict-validated campaigns taken for
other tasks were already on disk, and **every run in all three passes through
Neow** — floor 0 is unavoidable, which is exactly §1's point turned around.
Forty-one A20 Ironclad runs, all `oracle_block_enabled: true` on the frozen
stack, all translating `OK`:

| Campaign | Runs |
|---|---|
| `b45_rewards_oracle_20260727T204809Z_claude01` | STS00042-46 |
| `b45_rewards_oracle2_20260727T204809Z_claude01` | STS00047-52 |
| `b47_treasure_oracle_20260727T204809Z_claude01` | STS00053-82 |

**The read-out is a committed mode, not a hand-drive.** §4b says to drive the
sim side by hand; it is now `replay_run_diff --neow`, and the comparison it
performs is §4a plus §4b plus one checkpoint this runbook did not name:

```bash
build/debug/tools/oracle_bridge/replay/replay_run_diff --neow \
    /mnt/d/STS_BG_Mod/_oracle_data/campaigns/<campaign>/run_*.jsonl
```

The extra checkpoint is **ACTIVATION** — the record immediately after the
option is pressed, before any payout sub-screen resolves. §4 asked for two
comparisons and both are still made, but a boss swap whose relic has a deferred
`onEquip` body can only reach the first of them, and the acquisition it *does*
prove (which relic, off which pool pop, with `relicRng` untouched) is the part
that belongs to this task. Splitting it out is what lets those seeds be
reported precisely instead of just excluded.

**Result: 35 of 41 seeds zero-diff on all three checkpoints.** Six exclusions,
each named: STS00045/46 (Empty Cage) and STS00052/54 (Astrolabe) stop after
ACQUISITION because the capture opens a grid the blessing did not; STS00076
discards a potion out of combat, which the run layer has no verb for; STS00068
diverges on one field, `relics[1].counter`, which is a Centennial Puzzle
registry defect rather than a Neow one (obligations table).

**On the three traps.** All three were reached without asking for them. Two
colorless blessings confirmed the `cardRng`/`neowRng` split (trap 1); two
three-potion blessings moved `cardBlizzRandomizer`, confirming the rolled-then-
deleted reward row (trap 2); and twelve boss swaps returned no Black Blood
(trap 3).

**On the known-benign mismatch.** §4's RED-pool caveat is obsolete — B4.5
pinned the CardLibrary order, and no card-offer identity differed anywhere in
the 41 runs. What the capture *did* find is the neighbouring order question the
caveat did not cover: `transformCard`'s list reads `commonCardPool` forwards but
`srcUncommonCardPool` and `srcRareCardPool` BACKWARDS, because the `src*` copies
are built with the prepending `addToBottom`. That was a real divergence, fixed
in `transform_card` and frozen by `NeowCapture.TransformTwoReproducesThe-
CapturedIdentities`.
