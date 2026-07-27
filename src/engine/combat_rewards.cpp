// Post-combat reward assembly + claim. All provenance (every cited
// method read in full) is on the declarations in combat_rewards.hpp; comments
// here carry only the local line-level facts.

#include "sts/engine/combat_rewards.hpp"

#include <cassert>

#include "relics/relic_pickup.hpp"     // gain_gold (the one run-layer gold door)
#include "sts/engine/cards.hpp"        // the three generated reward pools
#include "sts/engine/interp.hpp"       // mathutils_round
#include "sts/engine/potions.hpp"      // return_random_potion (trap 14)
#include "sts/engine/relic_pools.hpp"  // tier roll / pool pops / acquire_relic
#include "sts/engine/run_deck.hpp"     // add_card_to_master_deck (the door)

namespace sts::engine {

namespace {

void remove_item(RewardScreen& s, uint8_t index) noexcept {
    assert(index < s.count);
    for (uint8_t i = static_cast<uint8_t>(index + 1); i < s.count; ++i) {
        s.items[i - 1] = s.items[i];
    }
    --s.count;
    s.items[s.count] = RunRewardItem{};
}

RunRewardItem& push_item(RewardScreen& s) noexcept {
    assert(s.count < kRewardItemCap);
    RunRewardItem& item = s.items[s.count++];
    item = RunRewardItem{};
    return item;
}

// AbstractRoom.addGoldToRewards + RewardItem.applyGoldBonus (RewardItem.java:
// 110-129): Golden Idol adds MathUtils.round(amount * 0.25f) as bonusGold.
// Ownership is read at ASSEMBLY (RewardItem construction), not at claim.
void add_gold_item(const RunState& rs, RewardScreen& s, int gold) noexcept {
    RunRewardItem& item = push_item(s);
    item.kind = static_cast<uint8_t>(RewardItemKind::GOLD);
    item.gold = gold;
    item.bonus_gold =
        run_has_relic(rs, RelicId::GOLDEN_IDOL)
            ? mathutils_round(static_cast<float>(gold) * 0.25f)
            : 0;
}

// AbstractDungeon.returnRandomNonCampfireRelic (AbstractDungeon.java:690-697):
// re-pop while the drawn relic is Peace Pipe / Shovel / Girya. Each rejected
// pop is CONSUMED from the pool (returnRandomRelicKey removes before the
// caller ever sees the id); nothing is put back. Terminates because every
// iteration shrinks a pool and the empty-pool ladder bottoms out at Circlet.
RelicId return_random_non_campfire_relic_key(RunState& rs, RelicTier tier,
                                             const RelicSpawnContext& ctx) noexcept {
    RelicId id = return_random_relic_key(rs, tier, ctx);
    while (id == RelicId::PEACE_PIPE || id == RelicId::SHOVEL ||
           id == RelicId::GIRYA) {
        id = return_random_relic_key(rs, tier, ctx);
    }
    return id;
}

// One pool-indexed draw: getCard(rarity) -> CardGroup.getRandomCard(...) ->
// group.get(rng.random(size - 1)) (CardGroup.java:498-507). The STREAM is the
// caller's: combat rewards index these lists with cardRng
// (getRandomCard(true)), Neow's card blessings with NeowEvent.rng
// (getRandomCard(rng)). Published as draw_card_from_pool below.
CardId draw_from_pool(RngStream& card_rng, RewardCardRarity rarity) noexcept {
    static_assert(kIroncladCommonPoolCount == 20 &&
                      kIroncladUncommonPoolCount == 36 &&
                      kIroncladRarePoolCount == 16,
                  "the 72 red non-basics split 20/36/16 (design §5.1); a new "
                  "red card changes every reward-pool index and needs the "
                  "library-order question re-answered");
    switch (rarity) {
        case RewardCardRarity::RARE: {
            const int32_t i = random(card_rng, kIroncladRarePoolCount - 1);
            return kIroncladRarePool[static_cast<unsigned>(i)];
        }
        case RewardCardRarity::UNCOMMON: {
            const int32_t i = random(card_rng, kIroncladUncommonPoolCount - 1);
            return kIroncladUncommonPool[static_cast<unsigned>(i)];
        }
        case RewardCardRarity::COMMON:
        default: {
            const int32_t i = random(card_rng, kIroncladCommonPoolCount - 1);
            return kIroncladCommonPool[static_cast<unsigned>(i)];
        }
    }
}

// AbstractDungeon.getRewardCards (AbstractDungeon.java:1423-1479), one CARD
// reward item. Adds nothing when the relic-modified count is <= 0
// (addCardToRewards only adds a non-empty RewardItem, AbstractRoom.java:573-578).
void roll_card_reward_item(RunState& rs, RoomType room,
                           RewardScreen& s) noexcept {
    // numCards = 3, then every relic's changeNumberOfCardsInReward in
    // acquisition order (:1425-1428). S1 overrides: Question Card +1
    // (QuestionCard.java:34-36), Busted Crown -2 (BustedCrown.java:56-59).
    int num = kCardRewardBaseCount;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        const auto id = static_cast<RelicId>(rs.relics[i].relic_id);
        if (id == RelicId::QUESTION_CARD) {
            num += 1;
        } else if (id == RelicId::BUSTED_CROWN) {
            num -= 2;
        }
    }
    if (num <= 0) {
        return;
    }
    // Question Card is unique (one pool slot; ordinary relics never duplicate,
    // only Circlet stacks), so 3 + 1 == kRewardCardCap is the S1 maximum. The
    // clamp is defence for a hand-assembled / translator-imported relic list
    // the invariant does not cover; it is unreachable from a played run.
    assert(num <= kRewardCardCap);
    if (num > kRewardCardCap) {
        num = kRewardCardCap;
    }

    RunRewardItem item{};
    item.kind = static_cast<uint8_t>(RewardItemKind::CARDS);
    RewardCardRarity rarities[kRewardCardCap] = {};

    for (int i = 0; i < num; ++i) {
        // rollRarity: cardRng.random(99) + cardBlizzRandomizer
        // (AbstractDungeon.java:1597-1603; trap 13: ALWAYS cardRng), against
        // the ROOM's thresholds.
        const int roll = static_cast<int>(random(rs.card_rng, 99)) +
                         static_cast<int>(rs.card_blizz_randomizer);
        const RewardCardRarity rarity = reward_card_rarity(roll, room);

        // The pity switch (AbstractDungeon.java:1435-1451): RARE resets to +5,
        // COMMON steps -1 with a -40 floor, UNCOMMON leaves it. Note the boss
        // room resets pity on every card -- its getCardRarity returns RARE
        // unconditionally, and the switch runs on the RETURNED rarity.
        switch (rarity) {
            case RewardCardRarity::RARE:
                rs.card_blizz_randomizer =
                    static_cast<int16_t>(kCardBlizzStartOffset);
                break;
            case RewardCardRarity::COMMON: {
                int v = rs.card_blizz_randomizer - kCardBlizzGrowth;
                if (v <= kCardBlizzMaxOffset) {
                    v = kCardBlizzMaxOffset;
                }
                rs.card_blizz_randomizer = static_cast<int16_t>(v);
                break;
            }
            case RewardCardRarity::UNCOMMON:
                break;
        }

        // The no-dupe re-roll loop (AbstractDungeon.java:1452-1461): one
        // cardRng draw per attempt, compared by card id against everything
        // already picked. The pools are untouched -- getRandomCard is a pure
        // indexed read, so there is nothing to book-keep (the once-mooted
        // "removal bookkeeping" storage is unnecessary by construction).
        CardId id;
        bool dupe;
        do {
            id = draw_from_pool(rs.card_rng, rarity);
            dupe = false;
            for (int j = 0; j < i; ++j) {
                dupe = dupe || item.card_ids[j] == static_cast<uint16_t>(id);
            }
        } while (dupe);
        item.card_ids[i] = static_cast<uint16_t>(id);
        rarities[i] = rarity;
    }

    // The upgrade pass (AbstractDungeon.java:1469-1477): `c.rarity != RARE &&
    // cardRng.randomBoolean(cardUpgradedChance) && c.canUpgrade()`. Act 1's
    // chance is 0.0f (Exordium.java:107) so the branch never fires, but the
    // randomBoolean draw itself is real for every non-RARE card -- ONE cardRng
    // advance each -- and only `rarity != RARE` short-circuits it. canUpgrade()
    // is vacuously true here: the red reward pools hold no STATUS/CURSE row and
    // every offer is at upgrade 0.
    for (int i = 0; i < num; ++i) {
        if (rarities[i] != RewardCardRarity::RARE &&
            random_boolean(rs.card_rng, kExordiumCardUpgradedChance)) {
            item.card_upgrades[i] = 1;
        }
    }

    item.card_count = static_cast<uint8_t>(num);
    push_item(s) = item;
}

}  // namespace

CardId draw_card_from_pool(RngStream& rng, RewardCardRarity rarity) noexcept {
    return draw_from_pool(rng, rarity);
}

CardId draw_colorless_card_from_pool(RngStream& card_rng,
                                     RewardCardRarity rarity) noexcept {
    // The two views are pre-sorted by the generator, so the draw is a single
    // indexed read (AbstractDungeon.java:1579-1595 -> CardGroup.java:509-524).
    if (rarity == RewardCardRarity::RARE) {
        const int32_t i = random(card_rng, kColorlessRarePoolCount - 1);
        return kColorlessRarePool[static_cast<std::size_t>(i)];
    }
    const int32_t i = random(card_rng, kColorlessUncommonPoolCount - 1);
    return kColorlessUncommonPool[static_cast<std::size_t>(i)];
}

void roll_setup_item_card_reward(RunState& rs, RoomType room,
                                 RewardScreen& s) noexcept {
    roll_card_reward_item(rs, room, s);
}

bool remove_first_card_reward_item(RewardScreen& s) noexcept {
    for (uint8_t i = 0; i < s.count; ++i) {
        if (s.items[i].kind == static_cast<uint8_t>(RewardItemKind::CARDS)) {
            if (s.open_card_item == i) {
                s.open_card_item = kNoOpenCardReward;
            } else if (s.open_card_item != kNoOpenCardReward &&
                       s.open_card_item > i) {
                --s.open_card_item;
            }
            remove_item(s, i);
            return true;
        }
    }
    return false;
}

bool run_has_relic(const RunState& rs, RelicId id) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

int roll_boss_gold(RngStream& misc_rng, int ascension) noexcept {
    const int tmp = 100 + static_cast<int>(random(misc_rng, -5, 5));
    if (ascension >= 13) {
        return mathutils_round(static_cast<float>(tmp) * 0.75f);
    }
    return tmp;
}

int roll_elite_gold(RngStream& treasure_rng) noexcept {
    return static_cast<int>(random(treasure_rng, 25, 35));
}

int roll_normal_gold(RngStream& treasure_rng) noexcept {
    return static_cast<int>(random(treasure_rng, 10, 20));
}

void assemble_combat_rewards(RunState& rs, RngStream& misc_rng, RoomType room,
                             RewardOutcome outcome, RewardScreen& out,
                             int32_t stolen_gold_return,
                             bool preserve_existing) noexcept {
    if (!preserve_existing) {
        out = RewardScreen{};
    }
    out.open_card_item = kNoOpenCardReward;

    // The two named gates the outcome controls (see RewardOutcome's contract):
    // haveMonstersEscaped for the gold/potion suppression, and the smoked
    // screen's items-or-nothing. The seam is these two booleans, not the
    // branches below.
    const bool monsters_escaped = outcome == RewardOutcome::MONSTERS_ESCAPED;
    const bool screen_offers_items = outcome != RewardOutcome::PLAYER_ESCAPED;

    // (0) A killed thief's returned gold. Looter.die() ran DURING combat
    // (addStolenGoldToRewards, Looter.java:170-172 -> AbstractRoom.java:
    // 619-626), so its item PRECEDES every battle-over item and counts toward
    // the >= 4 potion-suppression threshold in (3). No Golden Idol bonus:
    // applyGoldBonus's theft branch skips it (RewardItem.java:104-129).
    if (stolen_gold_return > 0) {
        RunRewardItem& item = push_item(out);
        item.kind = static_cast<uint8_t>(RewardItemKind::STOLEN_GOLD);
        item.gold = stolen_gold_return;
    }

    // (1) Gold (AbstractRoom.java:286-326). The plain-monster branch is gated
    // on !haveMonstersEscaped() in the Java (boss and elite are not).
    switch (room) {
        case RoomType::Boss:
            add_gold_item(rs, out, roll_boss_gold(misc_rng, rs.ascension));
            break;
        case RoomType::Elite:
            add_gold_item(rs, out, roll_elite_gold(rs.treasure_rng));
            break;
        case RoomType::Monster:
            if (!monsters_escaped) {
                add_gold_item(rs, out, roll_normal_gold(rs.treasure_rng));
            }
            break;
        default:
            // Event-room combats (combatEvent) give no gold: an EventRoom is
            // not a MonsterRoom (AbstractRoom.java:319), and only the event
            // framework can create one.
            break;
    }

    // (2) dropReward (AbstractRoom.java:329). Only MonsterRoomElite overrides
    // it outside mods (MonsterRoomElite.java:81-92): one relicRng tier roll
    // (<50 C / >82 R / else U == relic_tier_for_roll), one pool FRONT pop with
    // the canSpawn recheck AT ASSEMBLY, then Black Star's second tier roll +
    // non-campfire pop.
    if (room == RoomType::Elite) {
        RelicSpawnContext ctx{};
        ctx.floor = rs.floor;
        fill_deck_spawn_gates(rs, ctx);
        fill_campfire_relic_count(rs, ctx);
        fill_boss_spawn_gates(rs, ctx);

        const RelicTier tier = return_random_relic_tier(rs);
        RunRewardItem& relic_item = push_item(out);
        relic_item.kind = static_cast<uint8_t>(RewardItemKind::RELIC);
        relic_item.id =
            static_cast<uint16_t>(return_random_relic_key(rs, tier, ctx));

        if (run_has_relic(rs, RelicId::BLACK_STAR)) {
            const RelicTier tier2 = return_random_relic_tier(rs);
            RunRewardItem& second = push_item(out);
            second.kind = static_cast<uint8_t>(RewardItemKind::RELIC);
            second.id = static_cast<uint16_t>(
                return_random_non_campfire_relic_key(rs, tier2, ctx));
        }
        // The EMERALD_KEY item (MonsterRoomElite.addEmeraldKey:94-98) follows
        // the emerald-elite node flag, which is documented out of S1 scope
        // (map_rooms.hpp setEmeraldElite note) -- the mapRng draw is modelled,
        // the flag is not stored, so no key item is assembled.
    }

    // (3) addPotionToRewards (AbstractRoom.java:580-608). Elite and every
    // MonsterRoom subtype (plain AND boss) roll at 40 + blizzardPotionMod;
    // White Beast Statue forces 100 (:594-596); >= 4 items already assembled
    // forces 0 (:597-599 -- unreachable in S1 without the key item, kept
    // faithful). The roll is UNCONDITIONAL and the +/-10 ratchet moves either
    // way (:601-607).
    {
        int chance = 0;
        if (room == RoomType::Elite || room == RoomType::Event ||
            ((room == RoomType::Monster || room == RoomType::Boss) &&
             !monsters_escaped)) {
            // The escape clause guards only the `instanceof MonsterRoom`
            // branch (plain + boss), not the elite one (:582-589).
            chance = kBasePotionDropChance + rs.blizzard_potion_mod;
        }
        if (run_has_relic(rs, RelicId::WHITE_BEAST_STATUE)) {
            chance = 100;
        }
        if (out.count >= 4) {
            chance = 0;
        }
        if (static_cast<int>(random(rs.potion_rng, 0, 99)) < chance) {
            RunRewardItem& potion_item = push_item(out);
            potion_item.kind = static_cast<uint8_t>(RewardItemKind::POTION);
            potion_item.id = static_cast<uint16_t>(
                return_random_potion(rs.potion_rng, /*limited=*/false));
            rs.blizzard_potion_mod =
                static_cast<int16_t>(rs.blizzard_potion_mod -
                                     kBlizzardPotionModStep);
        } else {
            rs.blizzard_potion_mod =
                static_cast<int16_t>(rs.blizzard_potion_mod +
                                     kBlizzardPotionModStep);
        }
    }

    // (4) The card reward(s) -- rolled by CombatRewardScreen.setupItemReward
    // (CombatRewardScreen.java:72-100), which the smoked openCombat(label,
    // true) never calls (:280-288): a Smoke Bomb consumed every draw above but
    // rolls no card and offers NOTHING (the screen shows only Proceed). The
    // mugged screen KEEPS offering: setupItemReward runs for
    // openCombat(label, false) too (:280-285), so even an all-escaped combat
    // still rolls and shows its cards.
    if (!screen_offers_items) {
        out = RewardScreen{};  // items discarded unclaimed; stream movement stays.
        out.open_card_item = kNoOpenCardReward;
        return;
    }
    roll_card_reward_item(rs, room, out);
    // Prayer Wheel: a SECOND card reward for a PLAIN monster room only
    // (CombatRewardScreen.java:89-94: `instanceof MonsterRoom` and not Elite /
    // Boss), rolled after the first with the same procedure.
    if (room == RoomType::Monster && run_has_relic(rs, RelicId::PRAYER_WHEEL)) {
        roll_card_reward_item(rs, room, out);
    }
}

bool add_event_combat_gold_reward(const RunState& rs, RewardScreen& out,
                                  int32_t gold) noexcept {
    if (gold <= 0) {
        return false;
    }
    for (uint8_t i = 0; i < out.count; ++i) {
        RunRewardItem& item = out.items[i];
        if (static_cast<RewardItemKind>(item.kind) != RewardItemKind::GOLD) {
            continue;
        }
        item.gold += gold;
        item.bonus_gold =
            run_has_relic(rs, RelicId::GOLDEN_IDOL)
                ? mathutils_round(static_cast<float>(item.gold) * 0.25f)
                : 0;
        return true;
    }
    if (out.count >= kRewardItemCap) {
        return false;
    }
    add_gold_item(rs, out, gold);
    return true;
}

bool add_event_combat_relic_reward(RewardScreen& out, RelicId id) noexcept {
    if (id == RelicId::NONE || out.count >= kRewardItemCap) {
        return false;
    }
    RunRewardItem& item = push_item(out);
    item.kind = static_cast<uint8_t>(RewardItemKind::RELIC);
    item.id = static_cast<uint16_t>(id);
    return true;
}

bool open_rest_card_reward(RunState& rs, RewardScreen& out) noexcept {
    out = RewardScreen{};
    out.open_card_item = kNoOpenCardReward;
    roll_card_reward_item(rs, RoomType::Rest, out);
    if (out.count == 0) {
        return false;
    }
    out.open_card_item = 0;
    return true;
}

bool reward_claim_legal(const RunState& rs, const RewardScreen& s,
                        uint8_t index) noexcept {
    if (s.count > kRewardItemCap || index >= s.count ||
        index >= kRewardItemCap ||
        s.open_card_item != kNoOpenCardReward) {
        return false;
    }
    const RunRewardItem& item = s.items[index];
    switch (static_cast<RewardItemKind>(item.kind)) {
        case RewardItemKind::GOLD:
        case RewardItemKind::STOLEN_GOLD:
            return true;
        case RewardItemKind::CARDS:
            return item.card_count > 0 &&
                   item.card_count <= kRewardCardCap;
        case RewardItemKind::POTION:
            // Sozu claims-and-discards regardless of slots (RewardItem.java:
            // 276-279); otherwise obtainPotion needs a free slot below the
            // run's potionSlots or the claim bounces (RewardItem.java:280-288).
            if (run_has_relic(rs, RelicId::SOZU)) {
                return true;
            }
            for (uint8_t i = 0; i < rs.potion_slots && i < kPotionCap; ++i) {
                if (rs.potions[i] == static_cast<uint16_t>(PotionId::NONE)) {
                    return true;
                }
            }
            return false;
        case RewardItemKind::RELIC:
            return relic_acquire_legal(rs, static_cast<RelicId>(item.id));
        case RewardItemKind::NONE:
        default:
            return false;
    }
}

bool claim_reward(RunState& rs, RngStream& misc_rng, RewardScreen& s,
                  uint8_t index) noexcept {
    if (!reward_claim_legal(rs, s, index)) {
        return false;
    }
    RunRewardItem& item = s.items[index];
    switch (static_cast<RewardItemKind>(item.kind)) {
        case RewardItemKind::GOLD:
        case RewardItemKind::STOLEN_GOLD:
            // player.gainGold via the one run-layer door (Ectoplasm suppression
            // + the onGainGold fan-out live there, relics/relic_pickup.hpp).
            // The GOLD and STOLEN_GOLD claim cases are body-identical in the
            // game (RewardItem.claimReward, RewardItem.java:255-273);
            // bonus_gold is always 0 on a STOLEN_GOLD item.
            gain_gold(rs, item.gold + item.bonus_gold);
            remove_item(s, index);
            return true;
        case RewardItemKind::POTION: {
            if (!run_has_relic(rs, RelicId::SOZU)) {
                for (uint8_t i = 0; i < rs.potion_slots && i < kPotionCap; ++i) {
                    if (rs.potions[i] ==
                        static_cast<uint16_t>(PotionId::NONE)) {
                        rs.potions[i] = item.id;
                        break;
                    }
                }
            }  // Sozu: the potion is discarded, the item is still consumed.
            remove_item(s, index);
            return true;
        }
        case RewardItemKind::RELIC:
            // instantObtain: append in acquisition order + onEquip (which may
            // consume miscRng -- War Paint / Whetstone). The pool pop already
            // happened at assembly.
            {
                const RelicAcquireResult acquired =
                    acquire_relic(rs, misc_rng, static_cast<RelicId>(item.id));
                if (acquired != RelicAcquireResult::ACQUIRED &&
                    acquired != RelicAcquireResult::CIRCLET_STACKED) {
                    return false;
                }
            }
            remove_item(s, index);
            return true;
        case RewardItemKind::CARDS:
            // Opens the pick screen (RewardItem.claimReward case CARD returns
            // false -- the item is NOT removed until a card is taken or the
            // bowl is sung).
            s.open_card_item = index;
            return true;
        case RewardItemKind::NONE:
        default:
            return false;
    }
}

bool reward_card_item_open_legal(const RewardScreen& s) noexcept {
    if (s.count > kRewardItemCap ||
        s.open_card_item == kNoOpenCardReward ||
        s.open_card_item >= s.count ||
        s.open_card_item >= kRewardItemCap) {
        return false;
    }
    const RunRewardItem& item = s.items[s.open_card_item];
    return static_cast<RewardItemKind>(item.kind) == RewardItemKind::CARDS &&
           item.card_count > 0 &&
           item.card_count <= kRewardCardCap;
}

bool reward_take_card_legal(const RunState& rs, const RewardScreen& s,
                            uint8_t card_index) noexcept {
    if (!reward_card_item_open_legal(s) ||
        card_index >= kRewardCardCap ||
        rs.master_deck_count >= kMasterDeckCap) {
        return false;
    }
    const RunRewardItem& item = s.items[s.open_card_item];
    return static_cast<RewardItemKind>(item.kind) == RewardItemKind::CARDS &&
           card_index < item.card_count &&
           card_def(static_cast<CardId>(item.card_ids[card_index])) != nullptr;
}

bool reward_take_card(RunState& rs, RewardScreen& s,
                      uint8_t card_index) noexcept {
    if (!reward_take_card_legal(rs, s, card_index)) {
        return false;
    }
    const uint8_t item_index = s.open_card_item;
    const RunRewardItem& item = s.items[item_index];
    // THE door (run_deck.hpp): fires every owned relic's onObtainCard in
    // acquisition order -- Ceramic Fish (+9 gold), the eggs (upgrade on
    // obtain), Darkstone Periapt (+6 max HP) -- then onMasterDeckChange. A
    // direct rs.master_deck[] write here would silently disable all of them.
    // (The eggs also upgrade the game's on-screen OFFER via
    // onPreviewObtainCard, MoltenEgg2.java:41-48 -- which delegates to the same
    // guarded onObtainCard body, so obtaining through the door lands on the
    // identical post-claim deck; only the un-stored offer display differs.)
    if (!add_card_to_master_deck(rs,
                                 static_cast<CardId>(item.card_ids[card_index]),
                                 item.card_upgrades[card_index])) {
        return false;  // deck cap / unknown id: no mutation, item stays.
    }
    s.open_card_item = kNoOpenCardReward;
    remove_item(s, item_index);
    return true;
}

bool reward_sing(RunState& rs, RewardScreen& s) noexcept {
    if (!reward_card_item_open_legal(s) ||
        !run_has_relic(rs, RelicId::SINGING_BOWL)) {
        return false;
    }
    // SingingBowlButton.onClick (SingingBowlButton.java:82-87):
    // increaseMaxHp(2, true) -- maxHealth += 2 AND heal(2)
    // (AbstractCreature.java:199-209) -- then the reward item is removed.
    rs.max_hp = static_cast<int16_t>(rs.max_hp + 2);
    int hp = static_cast<int>(rs.hp) + 2;
    if (hp > rs.max_hp) {
        hp = rs.max_hp;
    }
    rs.hp = static_cast<int16_t>(hp);
    const uint8_t item_index = s.open_card_item;
    s.open_card_item = kNoOpenCardReward;
    remove_item(s, item_index);
    return true;
}

void reward_skip_card(RewardScreen& s) noexcept {
    // CardRewardScreen's skip: closeCurrentScreen without takeReward -- the
    // CARD item stays on the reward screen and can be re-opened.
    if (reward_card_item_open_legal(s)) {
        s.open_card_item = kNoOpenCardReward;
    }
}

}  // namespace sts::engine
