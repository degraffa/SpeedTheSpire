// SHOP-tier relic pickup bodies -- the out-of-combat overrides declared by
// `pickup:` on the RelicTier.SHOP rows of registry/relics.yaml. See
// relics/relic_pickup.hpp for the three surfaces and the generated dispatch.
//
// No SHOP relic overrides canSpawn: none of the seventeen files defines one, so
// every shop row takes AbstractRelic's `return true` default. That absence is
// itself RNG-relevant -- a spurious gate would change the relicRng draw order --
// so it is recorded here rather than left to be inferred from an empty file.

#include "relic_pickup.hpp"

#include "sts/engine/combat_rewards.hpp"  // RewardScreen assembly doors
#include "sts/engine/potions.hpp"         // get_random_potion (flat draw)
#include "sts/engine/run_deck.hpp"        // add_card_copy_to_master_deck

namespace sts::engine {

// --- onEquip -----------------------------------------------------------------

void relic_on_equip_lees_waffle(RunState& rs, RngStream& /*misc_rng*/,
                                RelicSlot& /*slot*/) noexcept {
    // Waffle.onEquip (Waffle.java:28-31):
    //     player.increaseMaxHp(7, false);
    //     player.heal(player.maxHealth);
    // The `false` does NOT suppress the heal. AbstractCreature.increaseMaxHp(int
    // amount, boolean showEffect) (AbstractCreature.java:199-209) is the only
    // definition of the method in the tree -- AbstractPlayer does not override it
    // -- and its body NEVER READS showEffect: the `this.heal(amount, true)` at
    // :206 is unconditional, as is the TextAboveCreatureEffect at :205. So
    // `increaseMaxHp(7, false)` raises max HP by 7 AND heals 7; the
    // heal(maxHealth) on the next line then tops off whatever remains.
    //
    // The sim's `max_hp += 7; hp = max_hp` reaches that same end state in one
    // step, which is why there is no observable divergence. DO NOT delete the
    // `hp = rs.max_hp` on the strength of the `false` -- the flag is inert, both
    // Java steps heal, and dropping the assignment would leave Waffle granting
    // max HP without the HP.
    //
    // (An earlier version of this comment justified the two separate steps as
    // surviving "a future partial-heal modifier". That rationale was inverted:
    // under such a modifier the Java's increaseMaxHp heal would itself be scaled,
    // while the sim does one raw assignment. The heal here is out-of-combat, so
    // no Magic Flower multiplier applies either way -- MagicFlower.onPlayerHeal
    // only fires while the room phase is COMBAT, MagicFlower.java:32.)
    rs.max_hp = static_cast<int16_t>(rs.max_hp + 7);
    rs.hp = rs.max_hp;
}

// --- onEquip: the equip-SCREEN trio (`pickup: on_equip_screen`) ---------------
//
// Orrery, Dolly's Mirror and Cauldron are the three SHOP relics whose onEquip
// OPENS A SCREEN at the purchase site, which is why they sit on the
// on_equip_screen surface (RelicEquipContext, include/sts/engine/relic_pools.hpp)
// rather than the plain one. Landing them discharges the "Egg trio onEquip
// reward-screen preview pass (the equip-plumbing decision)" ledger row's last
// two named owners, and it changed NO dispatch surface -- the same answer that
// row already recorded for the eggs themselves.
//
// THE ROOM IS THE MERCHANT'S, AND THAT IS RNG-VISIBLE. Every reward card these
// two roll goes through AbstractDungeon.rollRarity, which asks
// `getCurrRoom().getCardRarity(roll)` (AbstractDungeon.java:1597-1603) -- and
// ShopRoom both raises the rare chance to 9 (ShopRoom.java:35-36) and forwards
// `useAlternation = false` (:52-55), so the offers roll against 9/37 with NO
// alterCardRarityProbabilities pass. `ctx.reward_room` is RoomType::Shop at the
// purchase site and reward_card_rarity_with_relics carries both halves.
//
// WHERE THE SCREEN GOES. Neither body opens anything itself: they fill
// ctx.rewards / raise a grid request and the acquisition site presents it
// (apply_claim_equip_request, run_advance.cpp). At a shop that means
// RunPhase::COMBAT_REWARD over the merchant, which is literally what the game
// does -- combatRewardScreen.open sets `AbstractDungeon.screen =
// COMBAT_REWARD` inside the still-mounted ShopRoom (CombatRewardScreen.java:
// 241-261), and ProceedButton's COMBAT_REWARD arm then opens the MAP for a
// non-boss non-event room (ProceedButton.java:122-158), never the merchant
// again.

void relic_on_equip_screen_orrery(RunState& rs, RngStream& /*misc_rng*/,
                                  RelicSlot& /*slot*/,
                                  RelicEquipContext& ctx) noexcept {
    // Orrery.onEquip (Orrery.java:27-33), read in full:
    //
    //     for (int i = 0; i < 4; ++i) getCurrRoom().addCardToRewards();
    //     combatRewardScreen.open(DESCRIPTIONS[1]);
    //     getCurrRoom().rewardPopOutTimer = 0.0f;
    //
    // THE PLAYER SEES FIVE CARD ROWS, NOT FOUR. The loop is four
    // (AbstractRoom.addCardToRewards, AbstractRoom.java:573-578, each one
    // `new RewardItem()` == a full getRewardCards() roll), and open() ->
    // setupItemReward appends ONE MORE unconditional card row on top
    // (CombatRewardScreen.java:72-96): its skip gate is `event with
    // noCardsInRewards || TreasureRoom || RestRoom`, and a ShopRoom is none of
    // those. Nothing removes that row here -- that is Cauldron's line, not
    // Orrery's -- so the fifth offer is real, claimable, and the reason the
    // relic reads "3 cards" five times over.
    //
    // The Prayer Wheel arm at :89-94 is gated on `getCurrRoom() instanceof
    // MonsterRoom`, so it cannot fire in a shop even with the relic owned.
    //
    // RNG: 5 x getRewardCards = 5 x (3 rollRarity + 3 pool picks + 3 non-RARE
    // upgrade randomBoolean -- the upgrade draw is taken even at Act 1's 0.0f
    // chance, trap 2) = 45 cardRng draws minimum, plus one extra pool draw per
    // no-dupe rejection, plus the cardBlizzRandomizer pity step per roll. All
    // of that is roll_card_reward_item's, which is why this body is five calls
    // and no arithmetic.
    ctx.rewards = RewardScreen{};
    ctx.rewards.open_card_item = kNoOpenCardReward;
    for (int i = 0; i < 4; ++i) {
        roll_setup_item_card_reward(rs, ctx.reward_room, ctx.rewards);
    }
    // combatRewardScreen.open -> setupItemReward's own unconditional row.
    roll_setup_item_card_reward(rs, ctx.reward_room, ctx.rewards);
    ctx.screen = RelicEquipScreen::ITEM_REWARD;
}

void relic_on_equip_screen_cauldron(RunState& rs, RngStream& /*misc_rng*/,
                                    RelicSlot& /*slot*/,
                                    RelicEquipContext& ctx) noexcept {
    // Cauldron.onEquip (Cauldron.java:30-45), read in full:
    //
    //     for (int i = 0; i < 5; ++i)
    //         getCurrRoom().addPotionToRewards(PotionHelper.getRandomPotion());
    //     combatRewardScreen.open(DESCRIPTIONS[1]);
    //     getCurrRoom().rewardPopOutTimer = 0.0f;
    //     <remove the FIRST RewardType.CARD row from combatRewardScreen.rewards>
    //
    // 1. The five brews are PotionHelper.getRandomPotion() -- the no-argument
    //    overload, i.e. `potions.get(potionRng.random(size - 1))`
    //    (PotionHelper.java:169-172): the FLAT uniform draw off potionRng, ONE
    //    draw each, with none of returnRandomPotion's d100 tier gate or
    //    rejection sampling (trap 14). +5 on potionRng, exactly.
    //
    // 2. THE HIDDEN CARD ROLL. open() calls setupItemReward
    //    (CombatRewardScreen.java:254), which appends the unconditional
    //    `new RewardItem()` card row for any room that is not a TreasureRoom /
    //    RestRoom / noCardsInRewards event (:72-96) -- a ShopRoom qualifies --
    //    and THEN Cauldron deletes that row again (:36-44). The cards are never
    //    shown; the DRAWS ARE STILL SPENT. That is +9 cardRng for a clean roll
    //    (3 rollRarity + 3 pool picks + 3 upgrade randomBooleans) plus any
    //    no-dupe redraws, and the cardBlizzRandomizer pity moves with it. This
    //    burn was measured live on seed STS430130 (S2.43 triage) before it was
    //    ever written down, and skipping it desyncs cardRng for the rest of the
    //    run. Structurally identical to Neow's three-potion blessing, which
    //    rolls and deletes the same row (NeowReward.java:268-283).
    //
    // 3. The removal is the FIRST CARD row, and the five potions were queued
    //    ahead of it, so it is always the row setupItemReward just added.
    ctx.rewards = RewardScreen{};
    ctx.rewards.open_card_item = kNoOpenCardReward;
    for (int i = 0; i < 5; ++i) {
        RunRewardItem& potion = ctx.rewards.items[ctx.rewards.count++];
        potion = RunRewardItem{};
        potion.kind = static_cast<uint8_t>(RewardItemKind::POTION);
        potion.id = static_cast<uint16_t>(get_random_potion(rs.potion_rng));
    }
    roll_setup_item_card_reward(rs, ctx.reward_room, ctx.rewards);
    (void)remove_first_card_reward_item(ctx.rewards);
    ctx.screen = RelicEquipScreen::ITEM_REWARD;
}

void relic_on_equip_screen_dollys_mirror(RunState& rs, RngStream& /*misc_rng*/,
                                         RelicSlot& /*slot*/,
                                         RelicEquipContext& ctx) noexcept {
    // DollysMirror.onEquip (DollysMirror.java:33-42): park the room at
    // RoomPhase.INCOMPLETE and open
    //
    //     gridSelectScreen.open(player.masterDeck, 1, DESCRIPTIONS[1],
    //                           false, false, false, false);
    //
    // -- the RAW master deck (no getPurgeableCards, no
    // getGroupWithoutBottledCards: every other grid in scope filters, this one
    // does not), ONE pick, canCancel FALSE (the sixth argument), so the pick is
    // mandatory and there is no confirm step. GridCardSelectScreen.open stores
    // the group BY REFERENCE (`targetGroup = group`, :437-438), so the rows are
    // in master-deck order. ZERO RNG on every path -- the duplicate identity is
    // the picked card's, not a draw.
    //
    // The screen-up bookkeeping at :35-39 (hide the banner and the cancel
    // button, remember previousScreen) is presentation: it is what returns the
    // player to the SHOP screen after the pick, which is exactly what the
    // pending-deck-pick overlay does by leaving RunPhase::SHOP in place.
    //
    // The empty-deck guard is DEFENSIVE, not the Java's: the game would open a
    // grid with no rows and never see selectedCards.size() == 1. No run can
    // reach a zero-card master deck (a purge cannot take the last card), so the
    // arm is unreachable; requesting no screen is the non-hanging rendering.
    if (rs.master_deck_count == 0) {
        return;
    }
    ctx.screen = RelicEquipScreen::GRID_DUPLICATE;
    ctx.grid_picks = 1;
}

bool dollys_mirror_pick_legal(const RunState& rs,
                              uint16_t deck_index) noexcept {
    // Every master-deck row, unfiltered (DollysMirror.java:41 passes the group
    // itself). Curses, statuses, Ascender's Bane and already-bottled cards are
    // all offerable.
    return deck_index < rs.master_deck_count;
}

void relic_dollys_mirror_duplicate(RunState& rs,
                                   uint16_t deck_index) noexcept {
    // DollysMirror.update (DollysMirror.java:45-58) once the single pick lands:
    //
    //     AbstractCard c = gridSelectScreen.selectedCards.get(0)
    //                          .makeStatEquivalentCopy();
    //     c.inBottleFlame = false; c.inBottleLightning = false;
    //     c.inBottleTornado = false;
    //     effectList.add(new ShowCardAndObtainEffect(c, ...));
    //     getCurrRoom().phase = RoomPhase.COMPLETE;
    //
    // makeStatEquivalentCopy (AbstractCard.java:825-849) carries timesUpgraded,
    // cost/costForTurn and misc; the three explicit `false`s then strip the
    // bottle flags FROM THE COPY ONLY, so bottling a card and mirroring it
    // yields one bottled original and one free duplicate.
    // ShowCardAndObtainEffect is the ordinary obtain door -- constructor-time
    // Omamori gate (a mirrored CURSE can be eaten by a charge), append,
    // onObtainCard in acquisition order, onMasterDeckChange -- which is
    // add_card_copy_to_master_deck exactly.
    if (!dollys_mirror_pick_legal(rs, deck_index)) {
        return;
    }
    (void)add_card_copy_to_master_deck(rs, rs.master_deck[deck_index]);
}

}  // namespace sts::engine
