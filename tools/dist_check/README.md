# Tier-4 distribution checks

`dist_check` is the Stage-B design §3.4 campaign tool. It exercises simulator
code, rather than sampling a second copy of the probability formulas, and
compares the resulting aggregates with expectations derived from the cited
Java tables. It has no runtime dependency beyond the simulator.

## Pre-registered analytic family

The stochastic family is fixed at these 16 hypotheses:

1. `encounter.weak_first`
2. `encounter.first_strong_with_exclusions`
3. `encounter.elite_first`
4. `encounter.boss_first`
5. `composition.large_slime`
6. `composition.small_slimes`
7. `composition.two_louse`
8. `composition.three_louse`
9. `composition.lots_of_slimes`
10. `reward.card_rarity_pity`
11. `reward.potion_drop_ratchet`
12. `chest.joint_size_gold_relic`
13. `event.question_room_pity`
14. `tier.relic_reward`
15. `tier.relic_shop`
16. `tier.potion_65_25_10`

The family-wise alpha is 0.01. P-values are corrected with the
Holm-Bonferroni step-down procedure. Holm was chosen before the sweep because
it strongly controls family-wise error under arbitrary dependence (the pity
and ratchet cells are deliberately correlated) while rejecting fewer sound
hypotheses than plain Bonferroni. A p-value at or below its Holm threshold is a
stop-the-line flag, not a parameter-tuning signal.

Exact support checks and map quota/fixed-row checks are outside the stochastic
family: any failure flags the campaign directly. The map check covers the
`generateRoomTypes` requested quotas. It intentionally does not assert that
the final rule-constrained placement preserves every requested special room;
Java's `getNextRoomTypeAccordingToRules` can leave an unplaceable list entry
and `lastMinuteNodeChecker` fills that node with a monster.

The expectation sources are:

- encounter lists and exclusions: `Exordium.initializeLevelSpecificChances`,
  `generateMonsters`, `generateWeakEnemies`, `generateStrongEnemies`,
  `generateElites`, and `initializeBoss`;
- encounter compositions: `MonsterHelper.getEncounter`;
- reward rarity and pity: `AbstractDungeon.getRewardCards` and
  `AbstractRoom.getCardRarity`;
- potion drop ratchet: `AbstractRoom.addPotionToRewards`;
- chest joint table: `AbstractDungeon.getRandomChest`, `AbstractChest`, and
  the small/medium/large chest constructors;
- question-room pity: `EventHelper.roll`;
- tier rolls: `AbstractDungeon.returnRandomRelicTier`,
  `ShopScreen.rollRelicTier`, `PotionHelper.getRandomPotion`, and
  `AbstractDungeon.returnRandomPotion`;
- map quotas: `AbstractDungeon.generateRoomTypes`.

Run at least 10,000 seeds through the sanctioned WSL entry point:

```bash
tools/wsl_run.sh release
tools/wsl_run.sh --script tools/dist_check/run.sh release --seeds 20000
```

`--seeds` rejects values below 10,000.

## Pre-registered S2 act-2/3 family

The S2.44 family (s2-design §6, S2-G2 item 6) is a **separate family alongside
the one above, not an extension of it** — the B5.3 sixteen were registered,
corrected and reported as a closed set, and reopening them to add rows would
retroactively change every threshold that set was judged against. It is
`dist_check_s2`, and it is fixed at these **13 hypotheses**:

1. `s2.encounter.act2_weak_pair`
2. `s2.encounter.act2_first_strong_given_last_weak`
3. `s2.encounter.act2_elite_pair`
4. `s2.encounter.act3_weak_pair`
5. `s2.encounter.act3_first_strong_given_last_weak`
6. `s2.encounter.act3_elite_pair`
7. `s2.boss.act2_shuffle_pair`
8. `s2.boss.act3_double_boss_public_pair`
9. `s2.reward.card_upgraded_act2`
10. `s2.reward.card_upgraded_act3`
11. `s2.event.shrine_returns_after_act_crossing`
12. `s2.event.special_one_time_depletes_run_wide`
13. `s2.relic.boss_chest_can_spawn_front_scan`

The family-wise alpha is **0.01** and p-values are corrected with
**Holm-Bonferroni** — the same discipline, the same numbers and the same reason
as B5.3's: strong family-wise control under arbitrary dependence, which this
family needs even more than B5.3 did, because rows 1–3 (and 4–6, and 7) read one
`generate_monster_lists` call per seed and are therefore correlated by
construction. Nothing about the registration is adjusted after seeing a result.

### Replicate before flagging (the two-stage rule)

A row **retained** by Holm at stage one is final, and its replicate is **never
run**. A row **rejected** at stage one triggers exactly **one** confirmatory
replicate of the campaign on a pre-registered second seed block, judged at the
**same per-row Holm threshold**; the row is finally `FLAG`ged only if that
replicate rejects too, and otherwise reports `RETAINED-AFTER-REPLICATE` with
**both** p-values printed. The rule applies uniformly to all 13 rows **and to
the four negative controls** — a control must be rejected in *both* stages, so
the power claim is tested under the rule rather than beside it.

The replicate seed block is each sweep's own block **XOR `kReplicateSeedSalt`**
(`s2_main.cpp`), a constant fixed and documented before any replicate was ever
run and *derived* rather than picked: the ASCII bytes `'S' '2' '4' '4'` in the
high word. Every stage-one base is below 2³² and no sweep carries into bit 32,
so the two stages' blocks are disjoint by construction. The decision logic is
`confirm_by_replicate` in `stats.hpp`, pinned by `HolmReplicate.*` in
`tests/dist_check_test.cpp` — including that a stage-one retention does not so
much as consult the replicate.

**Why it was adopted, honestly.** The rule was **not** part of the original
registration. It was added on **2026-08-10** in response to this family's first
acceptance run, in which `s2.encounter.act3_weak_pair` rejected at
**p = 6.750359e-04** against its **7.692308e-04** threshold — an α-tail false
positive, not a divergence. The triage that established that, before the rule
existed and without re-seeding anything: the registered law is uniform 1/6 over
the six off-diagonal cells, and on the *same* seed base the χ² **shrinks** as n
grows — **21.42 → 7.71 → 5.67 → 3.61** (df 5) at 20k → 100k → 500k → 2,000,000
seeds, whereas a real bias grows linearly with n; the pool roll's band edges
(`0.33333334f` / `0.6666667f`) land on exact 24-bit boundaries so the roll is
uniform to one grid point in 2²⁴, and `populateMonsterList`'s rejection is a
re-roll, making the conditional exactly 1/2 — there is no mechanism that could
bias the second weak entry; and ten independent 20,000-seed blocks scored χ²
1.7–8.8, all retained. Under the rule the replicate retains that row at
**p = 7.098214e-01**.

**The family-wise consequence**, stated where the rule lives: under a true null
a row must land in its own α tail **twice, on independent seed blocks**, so the
false-flag rate falls from ~α to **~α² per row** — the price of a 13-row family
at α = 0.01 flagging roughly one clean run in a hundred is paid once. Power
against a real effect is **essentially unchanged**, because a true bias rejects
both stages; the four controls demonstrate exactly that, rejecting at p = 0 in
both. No other threshold, seed block, α, sample size or expectation moved.

Every hypothesis is a JOINT law wherever a joint one exists, because that is
what makes an exclusion an **exact support assertion** rather than a soft
frequency claim: an impossible cell has probability 0, and one observation in it
returns p = 0 outright. Rows 1/3/4/6 forbid the immediate repeat
(populateMonsterList, AbstractDungeon.java:1064-1095); row 2 forbids the four
TheCity exclusion pairs including **the game's only two-key exclusion**
(Chosen → Chosen and Byrds + Cultist and Chosen, TheCity.java:144-148); row 5
forbids TheBeyond's **self-exclusion** (3 Darklings, a key in both pools) while
requiring "Orb Walker"'s **inert** self-exclusion to remove nothing; rows 7/8
forbid a repeated boss.

What each row samples, and through which engine entry point:

- **1–7** `generate_monster_lists(act, …)` per seed, one call serving the weak
  pair, the first-strong-given-last-weak joint, the elite pair and the boss head
  pair. Acts 2 and 3 draw **two** weak entries, so index 1 is the entry
  `generateExclusions` keys on.
- **8** the A20 double boss read off the **public** surface:
  `encode_public_view` at `act == kFinalAct, boss_cursor == 1` publishes
  `boss_prefix[0]` and `second_boss_reserved`, which is s2-design §4.4's claim
  that the second fight is `bossList[1]` of the same shuffle rather than a
  re-draw. The Act-2 negative (no reserved second boss outside TheBeyond) is an
  exact check, not a frequency one.
- **9/10** `assemble_combat_rewards` at both ascension bands
  `card_upgraded_chance` keys on (A11 / A20). The bands are **exactly**
  equal-sized samples rather than random margins, because the upgrade
  `randomBoolean` is drawn for every non-RARE offer in every act and only its
  outcome changes (AbstractDungeon.java:1469-1477) — so the non-RARE count and
  the cardRng advance are identical across acts and ascensions for one seed.
  Act 1's 0.0f chance is an exact check on both facts, not a hypothesis.
- **11/12** the act-crossing asymmetry, `init_event_pools` →
  `generate_event` → `reinit_act_event_pools` → `generate_event`, over a context
  that pins every getShrine/getEvent gate so both acts' filtered pool sizes are
  constants (asserted exactly: 12 / 13 / 17 / 16). A **shrine** drawn in Act 1
  is drawable again in Act 2; a **special one-time** event is not, ever — that
  cell's probability is 0.
- **13** `roll_boss_chest` at the Act-2 chest, stratified over the two act-2
  canSpawn bodies: the fresh Ironclad (Ectoplasm gated by `actNum <= 1`; Black
  Blood *not* gated, because holding Burning Blood is what lets it spawn) and
  the Neow boss-swap line (both gated). Because BOTH the pop and the rejection
  reroute are `remove(0)` for BOSS tier, the pool consumption is a front scan
  and a rejection costs a permanent extra entry.

Expectation sources, all read in full from the decompiled tree:
`MonsterInfo.normalizeWeights`/`roll`; `AbstractDungeon.populateMonsterList` /
`populateFirstStrongEnemy`; `TheCity.generateMonsters`…`initializeBoss`
(:87-182) and `TheBeyond`'s counterparts (:84-176); `ProceedButton.update` /
`goToDoubleBoss` (:99-113, :210-220) with `MonsterRoomBoss.onPlayerEntry`
(:27-36); `AbstractDungeon.getRewardCards` upgrade pass (:1469-1477) with the
per-act `cardUpgradedChance` constants (Exordium.java:107, TheCity.java:84,
TheBeyond.java:81); `AbstractDungeon.dungeonTransitionSetup` (:2576-2577),
`generateEvent` (:1864-1880), `getShrine` (:1882-1942), `getEvent` (:1944-1990);
`AbstractDungeon.returnRandomRelicKey` / `returnEndRandomRelicKey` (:704-819)
with `BossChest.<init>` (:35-39).

The **analytic half is a library with its own tests** —
`include/sts/dist_check/s2_expect.hpp` and `DistCheckS2Expect.*` in
`tests/dist_check_test.cpp` — because a wrong expectation and an engine defect
are indistinguishable on a campaign report line. The laws are pinned there
against hand-derived numbers.

**Power is asserted, not assumed** (the T0.6 sampler-family precedent). Four
deliberately-wrong samplers run through the identical chi-square machinery on
every campaign run and must be rejected — **in both stages of the replicate
rule** — at the family's **strictest** Holm threshold (α/13); a survivor fails
the run:

- `mutant.first_strong_ignores_exclusions` — the first strong rolled without the
  rejection loop;
- `mutant.double_boss_repeats_first_boss` — the second Act-3 boss re-draws
  `bossList[0]`;
- `mutant.special_one_time_returns_next_act` — the act crossing rebuilds the
  one-time pool too (`init_event_pools` where the engine calls
  `reinit_act_event_pools`);
- `mutant.can_spawn_rejection_returns_relic` — a canSpawn rejection puts the
  relic back instead of consuming it.

Run it at the B5.3 scale through the sanctioned WSL entry point; `--seeds`
rejects values below 10,000, exactly as `run.sh` does:

```bash
tools/wsl_run.sh release
tools/wsl_run.sh --script tools/dist_check/s2_run.sh release --seeds 20000
```

## Pre-registered oracle spot family

`oracle_spot.py` consumes one completed, distinct campaign of at least 200
full Act-1 attempts and drives the simulator over the same seed longs with its
random-legal policy. It compares three end-to-end frequency aggregates:

1. encounter group signatures;
2. offered combat-reward card rarity;
3. question-room outcome mix (`EVENT`, `MONSTER`, `SHOP`, `TREASURE`).

The spot family also uses Holm-Bonferroni at family-wise alpha 0.01. Encounter
categories with fewer than 10 combined observations are pre-registered to
pool into `OTHER`, keeping the homogeneity test's expected cells meaningful.
Every report cell is ordered `[oracle, simulator]`, also recorded explicitly
by the report's `cell_order` field.
An empty encounter, a reward rarity outside common/uncommon/rare, or an
unregistered question-room outcome is an exact failure and is never pooled.
The capture validator requires a completed progress manifest, no failed seeds,
unique artifacts and seed longs, and a gameplay terminal (`death` or
`act1_boss_reward`) Act-1 record in every JSONL.
Timing sidecars and partial captures are never counted.

After building a test-enabled preset, run both the Python reader and the
WSL-built simulator through the sanctioned wrapper:

```bash
tools/wsl_run.sh --script tools/dist_check/oracle_spot.sh release \
  /mnt/d/STS_BG_Mod/_oracle_data/campaigns/<campaign-id> \
  /mnt/d/STS_BG_Mod/SpeedTheSpire/docs/verification/dist-check-oracle-spot.json
```

The report is nonzero/flagged on any corrected aggregate divergence. A flag
requires investigation of wiring or capture fidelity; the test definitions,
pooling threshold, alpha, and correction are not adjusted after seeing it.

## Pre-registered belief-sampler family (nightly)

The third family is the T0.6 distributional suite for `resample_hidden`, the
belief sampler (training-plan §2.4). It lives as a gtest binary,
`tests/sampler_dist_test.cpp`, because it needs no campaign, no capture and no
data root: every null is a closed-form conditional of the DECLARED knowledge
contract, enumerated in the test itself. The nine hypotheses, the family-wise
alpha, the sample sizes, the fixed sampler seeds and the family-wise accounting
are pre-registered in that file's header comment, which is the authority — this
section only says where the thing is and how to run it.

Two facts govern how it is read:

- It tests the **contract, not the mechanic** (training-plan §2.6d). Three rows
  of the sampler are deliberately coarser than the JDK (random insertion, relic
  membership under pop-time `canSpawn`, Match & Keep miss memory), so a
  seed-filtered comparison on those rows would fail by design. Exactly one
  hypothesis is seed-filtered: the encounter suffix, whose conditional law
  carries no coarsening, on a one-entry public prefix (~1/4 acceptance).
- Its **power is asserted, not assumed**. Three deliberately-biased,
  support-complete sampler mutants run through the identical machinery and must
  be rejected at the strictest Holm threshold. They run at full sample size in
  every mode, including the per-commit smoke run.

Like the analytic family above, the heavy version is out of band. The
per-commit `ctest` runs the same statistics in SMOKE mode (N/10, seed sweep
skipped, well under a second); the nightly job runs the pre-registered sizes:

```bash
tools/wsl_run.sh release
tools/wsl_run.sh --script tools/dist_check/sampler_dist.sh release
```

`sampler_dist.sh` is the entry point `.github/workflows/nightly.yml` runs on its
schedule; the only thing it does beyond locating the built binary is set
`STS_SAMPLER_DIST_MODE=nightly`. Alphas and sample sizes are deliberately not
settable from the command line — a job that can dial its own alpha is not a
pre-registered test — and, as above, nothing about the family is adjusted after
seeing a flag.
