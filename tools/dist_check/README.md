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
