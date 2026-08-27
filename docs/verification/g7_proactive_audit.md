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

## s2-replay-copy-shared-instance-state

The S2.V2 depth wave's first divergence: per-instance card state that the Java keys by uuid across every pile, which a fresh pool row silently snapshots instead of sharing.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `DoubleTap.ReplayedRampageReadsTheGrownMisc` | STS100009: a double-tapped Rampage dealt 8 then 13, because ModifyDamageAction writes every in-battle instance sharing the uuid and makeSameInstanceOf copies the uuid. | yes | yes |
| `DoubleTap.ReplayedRampageGrowthPersistsOnTheOriginalInstance` | The write-back half: the replay copy's own growth lands on the original, which is in the discard by then. | yes | yes |
| `Necronomicon.ReplayedRampageReadsTheGrownMisc` | Necronomicon's replay is the same op_play_card copy call and shared the defect. | yes | yes |
| `DoubleTap.ReplayedRitualDaggerKillGrowsTheOriginalInstance` | A kill scored by a same-uuid replay copy grows the master-deck row, not a transient copy. | yes | yes |
| `DoubleTap.ReplayedNonAccumulatingAttacksAreUnlinkedAndDoNotGrow` | The negative control: Strike and a Searing Blow+2 replay carry no misc link and grow nothing. | yes | yes |

## s2-spawn-prepass-group-visibility

A defect visible only through the run-layer entry point: the two spawn paths published monster_count differently, so group-reading openers decided against an empty group.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `MonsterFramework.SpawnTraceMatchesSpawnGroupWhenTheMaskKeepsEverything` | The run layer's only spawn path and the path every test uses must agree by construction. | yes | yes |
| `MonsterFramework.SpawnTracePublishesTheKeptCountBeforeAnyInitRuns` | MonsterGroup builds every member before init() runs any of them, so a slot-k member must see the whole group. | yes | yes |
| `CityNormalsII.CenturionAtSlotZeroOpensOnProtectThroughTheSpawnTrace` | STS108173: a slot-0 Centurion read aliveCount == 1, took the alone-arm, and spent the monster turn on FURY instead of the 20-block roll. | yes | yes |

## s2-combat-terminal-adjudication

Deep captures kept disagreeing about WHICH terminal a combat ended on and what still resolves behind the killing hit -- the one place a wrong answer flips a run's outcome.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `BeyondNormalsI.SpikerThornsKillsThePlayerAndTheQueuedHealCannotUndoIt` | STS103364: the death is latched inside the hit that lands it; DeathScreen's update arm never calls room.update, so the action queue freezes on the killing item. | yes | yes |
| `BeyondNormalsI.MutualKillByThornsIsADefeatNotAVictory` | The victory arm's survivors drain one at a time with a live player_hp check between them. | yes | yes |
| `GuardianSharpHide.DefeatTerminalResolvesNothingBehindTheKillingHit` | The DEFEAT terminal resolves nothing; the survivor allowlist belongs to the victory terminal only. | yes | yes |
| `GuardianSharpHide.RetaliationQueuedBehindTheLethalBlowStillLands` | The four-arm clearPostCombatActions allowlist, derived per opcode from the Java classes. | yes | yes |
| `GuardianSharpHide.DyingOwnersNormalHitStillCancelsAtTheVictoryTerminal` | S2.49's dying-owner cancel survives the widened survivor set. | yes | yes |
| `RelicHooks.BurningBloodDoesNotHealAPlayerAtZero` | BurningBlood.onVictory heals behind a currentHealth > 0 guard the engine body omitted. | yes | yes |
| `BeyondNormalsI.SurvivableSpikerThornsStillLandsAndTheHealStillResolves` | The negative control, green on both sides: at a VICTORY terminal the retaliation lands AND the heal resolves. | yes | yes |

## s2-card-cost-state-lifecycle

A whole family reached the depth wave undetected because no acceptance surface compares in-combat card COSTS against the capture -- the engine wrote cost state where the game does not, and did not where it does.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `PilesCostReset.MummifiedHandCostZeroIsLostOnTheReshuffle` | STS101166: Soul.update's DRAW_PILE and DISCARD_PILE arms call clearPowers -> resetAttributes on every move, mid-turn included. | yes | yes |
| `PilesCostReset.MidTurnDiscardOfThePlayedCardResetsItsThisTurnCost` | The capture shows Bash+(cost 0) at seq 330 and Bash+(cost 2) at seq 331, with no turn boundary between them. | yes | yes |
| `CardSkillsWarcry.PutBackOnTheDrawPileResetsTheThisTurnCost` | moveToDeck is Soul-routed too; moveToHand is not, and keeps its this-turn cost. | yes | yes |
| `CardUpgradeInCombat.UpgradeBaseCostCarriesTheThisTurnDifferenceAcross` | STS128113: AbstractCard.upgrade reaches the cost only through upgradeBaseCost, so a Snecko-rolled 2 survives an upgrade that keeps the cost. | yes | yes |
| `CardUpgradeInCombat.PerInstanceRuntimeFlagBitsSurviveTheUpgrade` | The blind registry re-seed also wiped freeToPlayOnce, purgeOnUse and exhaustOnUseOnce. | yes | yes |
| `PilesCostReset.CombatPersistentCostReductionSurvivesTheSameReshuffle` | The negative control: Corruption/Blood for Blood/Confusion move `cost` itself and must be untouched by the reset. | yes | yes |

## s2-reward-offer-preview

Run state was right and the OFFER was wrong -- a class of defect invisible to any check that only compares what the player ended up owning.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `RewardOfferPreview.AHeldEggUpgradesItsTypeInTheOFFERAndSpendsNoRng` | STS193303: the game previews a card reward through every owned relic's onPreviewObtainCard at ASSEMBLY, at two sites the engine had neither of. | yes | yes |
| `RewardOfferPreview.ColorlessOffersTakeTheEggPreviewToo` | The RewardItem(CardColor) constructor is the only preview the colorless flavour gets. | yes | yes |
| `RewardOfferPreview.CeramicFishAndPeriaptDoNotPreviewTheOffer` | The negative control: the pass is egg-gated, not generic over onObtainCard. | yes | yes |

## s2-liveness-senses

Three different senses of 'dead' (isDying, dead-or-escaped, basically-dead) that the Java keeps apart and one predicate cannot express.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `CardLimbo.HalfDeadTargetPassesCanUseAndStillRunsTheFanOut` | STS105835: cardPlayable reads the bare isDying field, and a halfDead Darkling is at 0 hp with isDying false, so the whole onPlayCard fan-out runs before the dead-target block notices. | yes | yes |
| `CardLimbo.NecronomiconReplayIntoItsOwnHalfDeathStillCountsThePlay` | The witnessed shape end to end: Velvet Choker counted the replay live and did not in the sim. | yes | yes |
| `S2EventContent.RitualDaggerPaysNothingForHalfDeadOrMinionKills` | The other side of the same distinction: a halfDead kill is not a kill for reward purposes. | yes | yes |

## s2-double-boss-handoff

The last un-modelled screen seam hid a real engine gap behind it -- nothing downstream of an unmodelled crossing can be compared at all, so the defect was invisible rather than red.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `BossVictory.TheA20DoubleBossHandoffMovesTheActsBossIdToTheSecondBoss` | goToDoubleBoss's first line is bossKey = bossList.get(0); nothing mirrored it into boss_ids, so both deep captures diverged from the crossing to the terminal. | yes | yes |
| `BossVictory.AnActTwoBossVictoryLeavesTheActsBossIdWhereItWas` | The negative control: an ordinary act victory must not move the act's boss id. | yes | yes |
| `ReplayCommandMap.TheDoubleBossHandoffProceedIsElidedBecauseTheEngineCrossedAlready` | The capture holds the first Act-3 boss room's bare proceed button while the engine ran the crossing inline off the kill. | yes | yes |
| `ReplayCommandMap.TheDoubleBossHandoffGateNeedsEveryTermOfProceedButtonsPair` | The structural gate takes every term of ProceedButton's pair; a looser gate would silently skip real records. | yes | yes |
| `ReplayCommandMap.TheFinishedActThreeVictoryProceedIsTheRunTerminal` | The same COMPLETE label ends the run when Act 3 is finished. | yes | yes |
| `ReplayCommandMap.ADeathParkedAtRunOverIsNotACompleteScreenTerminal` | A death parked at run-over must not be read as the victory terminal. | yes | yes |
| `ReplayCommandMap.ACompleteScreenCommandOtherThanProceedIsNotModelled` | The arm stays narrow: only proceed is modelled on a COMPLETE screen. | yes | yes |

## s2-unmodelled-clock-inputs

An input the simulator deliberately does not model did not merely hide one event -- it shortened a draw list and changed which event EVERY Act-3 question-mark room returned.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `S243SecretPortalPlaytimeGate.BothHalvesOfTheJavaTestAreLive` | getShrine's gate is playtime >= 800.0f AND id.equals(TheBeyond), at the inclusive boundary. | yes | yes |
| `S243SecretPortalPlaytimeGate.WitnessSTS108107DrawsTheWomanInBlue` | STS108107 at 924.35 s: the game drew index 13 of 14 where the engine drew index 5 of 13. | yes | yes |
| `S243SecretPortalPlaytimeGate.WitnessSTS153269DrawsFountainOfCleansing` | STS153269 at 960.92 s: index 10 of 15 against index 8 of 14. | yes | yes |
| `S243SecretPortalPlaytimeGate.ControlSTS111111BelowThresholdIsUnchanged` | The sub-threshold control at 710.14 s is unchanged; an act-only approximation breaks it. | yes | yes |
| `S243SecretPortalPlaytimeGate.AnActOnlyPinWouldBreakTheSubThresholdControl` | The refuted alternative, pinned as a test rather than left as an argument. | yes | yes |
| `S243SecretPortalPlaytimeGate.TheEngineDefaultIsStillTheTrapFivePin` | Every in-engine caller passes 0.0f, so no sim trajectory moves. | yes | yes |
| `S213DrawGates.SecretPortalIsPinnedFalseInEveryActIncludingTheBeyond` | Trap 5's original pin, unmodified by the playtime input. | yes | yes |

## s2-cross-act-capture-derivation

The first captures to cross an act boundary showed that a per-record derivation which is exact within one act is not exact across a run.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `ReplayFiredAccum.ActOneMonotoneSequenceIsAByteExactNoOp` | The fold must be a no-op for every landed Act-1 verification, the committed 50-seed corpus included. | yes | yes |
| `ReplayFiredAccum.AnActTwoRecordRegainsTheActOneFires` | dungeonTransitionSetup clears the event and shrine lists, so an Act-2 dump cannot witness an Act-1 fire. | yes | yes |
| `ReplayFiredAccum.AShrineRefiredAcrossActsFoldsToOneBit` | The shrine-bit aliasing case that ruled out a masking recognizer. | yes | yes |
| `ReplayFiredAccum.AFireTheCaptureNeverAttestedStaysMissing` | No false green: substitution only adds bits the capture itself attested earlier. | yes | yes |
| `ReplayFiredAccum.FoldSubstitutesTheUnionNotTheRecord` | The fold's direction, pinned. | yes | yes |
| `ReplayFiredAccum.BossIdSlotCarriesAcrossTheActCrossing` | The translator fills only the record's own act's slot from act_boss, so slot 0 collapsed after every crossing. | yes | yes |
| `ReplayFiredAccum.BossIdFoldNeverOverwritesAnAttestedSlot` | A capture attesting a different boss keeps its own value, so real disagreements still diff. | yes | yes |

## s2-live-run-state-projection

Run-level state that combat mutates mid-action: every one of these was invisible while a run finished normally and wrong the moment a capture read the run state between two steps.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `RunCombatGold.RitualDaggerKillFoldsItsGrownMiscToTheMasterDeck` | STS432354: the game writes masterDeck INSIDE RitualDaggerAction, so a run that dies pre-fold carries the grown row. | yes | yes |
| `RunCombatGold.RitualDaggerGrowthReachesTheMasterDeckAtTheNextBoundary` | The live-purse discipline extended to persistent card misc. | yes | yes |
| `RunPotion.AConsumedFairyBurnsItsSlotAtFoldBack` | A spent Fairy leaves the belt at its own step boundary, not at the next fold. | yes | yes |
| `RunPotion.EntropicBrewInCombatKeepsAFairyItRolls` | STS432580: the burn ate a fresh potion because the armed-fairy mirror went stale on an in-combat obtain. | yes | yes |
| `RunPotion.AFairyObtainedMidCombatStillRevives` | Arming per PLACED Fairy also closed the downstream belt-conditional miscRng gold tail. | yes | yes |
| `RunStolenGoldOrdering.SyncChargesSameStepStealsBeforeSameStepGreed` | S2.48's live purse, the ordering the same family rests on. | yes | yes |
| `RunStolenGoldOrdering.SyncBanksEarlierGreedBeforeALaterStealBoundary` | The banked amount is charged once; the replay harness's own projection double-credited it before the campaign found it. | yes | yes |

## s2-deferred-hook-boundaries

Effects the engine resolved inline that the game queues (or queues to the wrong end): only a live capture with the right screen open can tell the two apart.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `RelicHooksUnceasingTop.DrawsOneWhenAPlayEmptiesTheHandMidTurn` | The deferred onRefreshHand row's first live witness: the hook belongs at the pump idle boundary. | yes | yes |
| `RelicHooksUnceasingTop.RefiresOnALaterEmptyButNeverChains` | It refires on a later empty hand and never chains within one resolution. | yes | yes |
| `Potions.BloodPotionQueuesItsHealAndResolvesOnThePump` | STS432663: BloodPotion queues a HEAL item; the inline heal was observable behind ColorlessPotion's open DISCOVERY screen. | yes | yes |
| `Potions.BloodPotionHealWaitsBehindAnOpenDiscoveryScreen` | The screen that made the difference observable. | yes | yes |
| `CardLimbo.DropkickReshuffleExcludesThePlayedCard` | STS432630: DropkickAction addToTops all three follow-ups, and the played card stays in LIMBO across its own empty-deck reshuffle -- 18 cards, not 19. | yes | yes |
| `CardUncommonSkillsSecondWind.BlockGainsPrecedeTheFilingUseCard` | The same-class ordering rider, observable through Juggernaut's per-gain random-target draws. | yes | yes |

## s2-positional-monster-identity

A positional replay target is only as good as the monster array's order, and a missing construction-time offset silently sent every rally to the wrong minion.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `CityElites.RallySummonInsertsAfterADeadWizardRecordInTheSameSlot` | STS431071: the Gremlin Wizard ctor's x - 35.0f offset is the smart-position key whose absence rotated the monster array after a dead Wizard. | yes | yes |
| `CityElites.ALiveWizardStillClaimsItsSummonSlot` | All four capture rallies' insertion indices reproduce. | yes | yes |

## s2-screen-arm-shape-and-ownership

Screens whose click count, ownership or hidden RNG work differed from the game's -- each one stopped a replay somewhere that looked like a mapping bug and was not.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `Addict.LeaveIsStateNeutralAndTheEventHasNoA15Branch` | The Addict's default arm openMap()s at the press, so LEAVE is one click and not two. | yes | yes |
| `ShopFlow.CauldronBrewsFiveFlatPotionsAndBurnsTheDeletedCardRow` | STS430130: combatRewardScreen.open()'s setupItemReward constructs a full 3-card reward (+9 cardRng + blizz) that Cauldron then deletes from view. | yes | yes |
| `ShopFlow.CauldronsHiddenRowRollsTheMerchantRoomsRarityTable` | A reward roll made in the merchant's room uses the SHOP's 9/37 table with no alterCardRarityProbabilities pass. | yes | yes |
| `ShopFlow.OrreryOffersFiveCardRowsNotFour` | Orrery's four addCardToRewards plus setupItemReward's own row, which nothing deletes. | yes | yes |
| `ShopFlow.DollysMirrorGridIsUnfilteredAndDuplicatesThePick` | The equip-screen grid the deferred row left unmodelled. | yes | yes |
| `ReplayCommandMap.ADollysMirrorGridIsSeenOverAnyPhaseAndReadsDeckOrder` | The harness half: a deferred onEquip screen read as a mapping defect until the body landed. | yes | yes |

## s2-emitter-index-space-identity

A scripted line that desynchronises turns every later record into noise, and an emitter index space one permutation away from the simulator's is indistinguishable from an engine divergence.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `SimSearchScriptMatchAndKeep.FullBoardIndexIsScreenPositionNotBoardSlot` | STS100038: getOrderedCards sorts by the screen position stored at construction, and only six of the twelve slots are fixed points of that permutation. | yes | yes |
| `SimSearchScriptMatchAndKeep.TheShrinkingBoardIsTheSTS100038Stop` | The failure is silent while the numbers stay in range; it surfaced only once the live board shrank by a pair the sim never matched. | yes | yes |
| `SimSearchScriptLibrary.ReadPickEmitsCardIdentityNotAnOptionIndex` | STS100009: The Library's read is a GRID screen, so an option space of dynamically rolled cards carries a card identity, never a bare sim index. | yes | yes |
| `SimSearchScriptLibrary.TheOrdinalIsCountedInTheLiveReversedOrder` | addToBottom is a PREPEND, so the live grid runs in reverse roll order. | yes | yes |
| `SimSearchScript.ClaimOrdinalIsAnIdentityOrdinalNotAKindOrdinal` | STS100075: claiming POTION row 1 of three distinct ids asked for the second Strength Potion on a screen holding one. | yes | yes |
| `NeowPayout.STS100075ThreePotionTrioMatchesTheHandRunJava` | The engine was right and the emitter was wrong -- the literal trio and its pool indices, derived from the Java. | yes | yes |

## s2-act-two-three-replay-seams

Every Act-2 crosser in the breadth wave diverged at the same replay seam: a screen the engine models correctly but the harness never learned reads as an engine divergence, and nothing past it is compared at all.

| Required regression | Historical witness | Registered | Passing |
|---|---|---:|---:|
| `OracleCorpusReplay.ThreeActCorpusReplaysZeroDiff` | The committed five-run Acts 1-3 corpus: the boss-chest seam every one of the 19 Act-2 crossers diverged on, both boss-relic branches, the act-2->3 transition, an Act-3 kill and the A20 double-boss handoff, replayed out of real captures. | yes | yes |
| `OracleCorpusReplay.ThreeActInjectedSyntheticDivergenceFailsLoud` | The same path fails loud on a mutated state, so a green three-act corpus is an assertion rather than a no-op. | yes | yes |
| `ReplayCommandMap.ChoiceFreeBossRelicGridsAreRecognisedAsConfirmations` | The boss-chest arm's equip-grid confirmations, the shape the first Act-2 crossers needed. | yes | yes |
| `ReplayCommandMap.TheDeferredOnEquipSetMatchesTheUnionTree` | The deferred-onEquip stop names its owner instead of reading as a mapping defect; the list is empty today and the test is what keeps it honest. | yes | yes |
| `RunDifferBossChest.EveryMemberNamedSeparately` | S2.47's boss-relic offer storage is compared member by member, which is what makes a boss-relic pick a zero-diff assertion. | yes | yes |
