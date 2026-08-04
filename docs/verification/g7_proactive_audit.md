# G7 proactive regression audit

Generated deterministically by `tools/verify_report/check_g7_proactive_coverage.py`.

Verdict: **PASS**.

## observable-private-state

Campaigns repeatedly found correct mechanics represented through the wrong oracle-visible field.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `RelicHooks.LanternGrantsEnergyOnFirstTurnOnly` | Lantern's Java-private firstTurn latch must not leak through AbstractRelic.counter. | yes | yes |
| `RelicHooks.RedSkullPrivateLatchNeverChangesOracleCounter` | Red Skull's private isActive state remains separate from its observable counter. | yes | yes |
| `RelicRaresShop.GamblingChipQueuesTheOptionalDiscardScreenOnceOnly` | Gambling Chip's private activated latch fires once without mutating its observable counter. | yes | yes |

## lifecycle-and-queue-order

Several deep diffs were ordering defects at combat entry, hook construction, turn expiry, or terminal settlement.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `RunCombatBattleStart.StoneCalendarCounterSequenceMatchesTheJavaOrder` | Battle-start initialization precedes the first turn-start counter increment. | yes | yes |
| `RelicBossSpecial.GremlinMaskWeakensThePlayerAtBattleStart` | Gremlin Visage's source flag and end-of-round expiry match the Java lifecycle. | yes | yes |
| `CardRaresCorruption.CostWalkRunsWhenTheApplyActionIsConstructed` | Corruption walks all piles at action construction time, not delayed resolution time. | yes | yes |
| `CardRaresReaper.HealsTheDamageThatActuallyLanded` | Reaper retains terminal-hit healing through damage resolution and combat settlement. | yes | yes |

## rng-cardinality-boundaries

Uniform campaigns exposed missed RNG work at zero/one-element and conditionally empty boundaries.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `CardRaresFiendFire.RandomExhaustsBillTheFinalSingletonToo` | A Java random(0) draw still occurs for the final singleton candidate. | yes | yes |
| `PilesDraw.OverdrawWithOnlyDrawCardsStillRunsTheEmptyShuffleAction` | The trailing empty-discard shuffle action still executes its hook boundary. | yes | yes |
| `RunEmeraldElite.EntryConsumesOneMapRngDrawAndAppliesStrength` | The burning-elite entry consumes its singleton mapRng draw before applying the rolled buff. | yes | yes |

## filtered-command-identity

Harness gaps clustered where visible ordinals differed from fixed simulator slots or identities.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `ReplayCommandMap.CombatPlayZeroNamesTheTenthHandSlot` | CommunicationMod's play-zero alias maps to the tenth hand slot. | yes | yes |
| `ReplayCommandMap.CombatDrawGridMapsFilteredOrdinalToSourcePileSlot` | A filtered capture ordinal maps back to the simulator's source-pile identity. | yes | yes |
| `ReplayCommandMap.CombatRewardOrdinalsElideTheEmeraldKeyRow` | Reward ordinals remain stable when the oracle-only Emerald Key row is present. | yes | yes |
| `ReplayCommandMap.SingingBowlOrdinalAfterCardRowsMapsToTheNamedAction` | The capture's post-card Singing Bowl ordinal maps to the named max-HP action without stealing a real fourth card. | yes | yes |

## nested-screen-transitions

Choice-bearing events and onEquip bodies failed at nested acknowledgement and return boundaries.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `WheelOfChange.SpinIsOneMiscRngDrawAndGoldPaysBeforeAcknowledgement` | The spin, result acknowledgement, payout, and leave states remain distinct. | yes | yes |
| `NeowPayout.BossSwapOntoPandorasBoxWaitsForConfirmation` | Pandora's Box preserves its confirmation boundary before applying replacements. | yes | yes |
| `NeowPayout.BossSwapOntoTinyHouseOpensItsRewardScreenAndClaims` | Tiny House returns from its nested card screen to the still-live item reward. | yes | yes |
| `NeowPayout.BossSwapOntoCallingBellOffersThreeRelicsAndTheCurse` | Calling Bell confirmation precedes the mandatory curse/relic reward sequence. | yes | yes |

## persistent-state-and-exclusions

Late findings crossed transient/combat/run state or used broad filters with one source-level exclusion.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `CardUncommonSkillsInfernalBlade.GeneratedBloodForBloodKeepsZeroButRevealsReducedBaseNextTurn` | A generated Blood for Blood keeps its turn override while updating the saved combat base. | yes | yes |
| `RunPotion.BloodPotionHealsPersistentHpOnACombatRewardScreen` | Out-of-combat potion use updates persistent run HP at the reward boundary. | yes | yes |
| `FountainOfCleansing.DrinkRemovesEveryCurseButAscendersBane` | The special-curse exclusion survives the broad curse-removal path. | yes | yes |
