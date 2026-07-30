#include "sts/engine/neow.hpp"

#include <cassert>
#include <cstddef>

#include "relics/relic_pickup.hpp"  // gain_gold / lose_gold (the run gold doors)
#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"  // rest_card_purgeable / rest_card_upgradeable
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

// --- Character-sheet edits ----------------------------------------------------

// AbstractCreature.increaseMaxHp(amount, true) (AbstractCreature.java:199-209):
// maxHealth += amount AND heal(amount). AbstractPlayer.heal's extra body is the
// onNotBloodied fan-out (AbstractPlayer.java:1545-1553), which no S1 relic can
// reach from here -- Red Skull is the only override and it acts on a combat
// power -- so the run-layer effect is the clamp alone.
void increase_max_hp(RunState& rs, int amount) noexcept {
    rs.max_hp = static_cast<int16_t>(rs.max_hp + amount);
    int hp = static_cast<int>(rs.hp) + amount;
    if (hp > rs.max_hp) {
        hp = rs.max_hp;
    }
    rs.hp = static_cast<int16_t>(hp);
}

// AbstractCreature.decreaseMaxHealth (AbstractCreature.java:211-223): subtract,
// floor max HP at 1, then clamp current HP down to the new max.
void decrease_max_health(RunState& rs, int amount) noexcept {
    int max_hp = static_cast<int>(rs.max_hp) - amount;
    if (max_hp <= 1) {
        max_hp = 1;
    }
    rs.max_hp = static_cast<int16_t>(max_hp);
    if (rs.hp > rs.max_hp) {
        rs.hp = rs.max_hp;
    }
}

// --- Relic spawn context ------------------------------------------------------

RelicSpawnContext neow_spawn_context(const RunState& rs) noexcept {
    RelicSpawnContext ctx{};
    ctx.floor = rs.floor;   // 0 at Neow
    ctx.act = rs.act;
    fill_deck_spawn_gates(rs, ctx);
    fill_campfire_relic_count(rs, ctx);
    // Read AFTER any loseRelic the payout has already done -- Black Blood's
    // canSpawn is hasRelic("Burning Blood") (BlackBlood.java:33-36), and the
    // boss swap removes Burning Blood BEFORE it draws (NeowReward.java:244-245),
    // so Black Blood is structurally unspawnable from that draw.
    fill_boss_spawn_gates(rs, ctx);
    return ctx;
}

void open_grid(NeowState& st, NeowGridMode mode, uint8_t picks) noexcept;

void spawn_relic_and_obtain(RunState& rs, NeowState& st, RewardScreen& rewards,
                            RngStream& misc_rng, RngStream& card_random_rng,
                            RelicId id) noexcept {
    // AbstractRoom.spawnRelicAndObtain (AbstractRoom.java:501-510): the Circlet
    // duplicate branch and obtain() are both what acquire_relic already models.
    // Every Neow relic payout goes through the screen-capable overload so that
    // a BOSS-swap relic with an on_equip_screen body (the five: Pandora's Box,
    // Tiny House, Astrolabe, Empty Cage, Calling Bell) runs it here rather
    // than being refused by the plain door -- and so no Neow path can ever be
    // the silent-inert one. The body's screen request is then translated onto
    // NeowState's own sub-screens; that translation living HERE (not in the
    // bodies) is what keeps the bodies re-homable to an Act-2 boss-chest call
    // site later (see RelicEquipContext, relic_pools.hpp).
    //
    // TIMING: the Java fires onEquip from the relic's fly-in animation, not
    // from obtain() (AbstractRelic.java:277-292 vs :339-348); nothing else in
    // any Neow payout runs in between, so this inline dispatch is observably
    // identical -- recorded at the bodies' header (relic_pickup_boss.cpp).
    RelicEquipContext ctx{card_random_rng, rewards, kNeowRewardScreenRoom};
    (void)acquire_relic(rs, misc_rng, id, ctx);
    switch (ctx.screen) {
        case RelicEquipScreen::NONE:
            break;  // synchronous body (or none): st.screen stays as set.
        case RelicEquipScreen::GRID_REMOVE:
            open_grid(st, NeowGridMode::REMOVE, ctx.grid_picks);
            break;
        case RelicEquipScreen::GRID_TRANSFORM_UPGRADE:
            open_grid(st, NeowGridMode::TRANSFORM_UPGRADE, ctx.grid_picks);
            break;
        case RelicEquipScreen::ITEM_REWARD:
            // The body assembled `rewards`; the claim/proceed flow is the same
            // NeowScreen::ITEM_REWARD branch the three-potion blessing uses.
            st.screen = static_cast<uint8_t>(NeowScreen::ITEM_REWARD);
            break;
        case RelicEquipScreen::GRID_BOTTLE:
            // Structurally unreachable at this site: the boss swap draws from
            // the BOSS pool and the Bottled trio is UNCOMMON. The bottle grid
            // belongs to the claim/purchase sites' pending-bottle overlay
            // (apply_claim_equip_request, run_advance.cpp); this function has
            // no RunController to park it on, which is fine because no Neow
            // payout can produce the request.
            assert(false && "a bottle grid request from the Neow boss swap");
            break;
        case RelicEquipScreen::GRID_CONFIRM_PANDORA:
            open_grid(st, NeowGridMode::CONFIRM_PANDORA, 0);
            break;
        case RelicEquipScreen::GRID_CONFIRM_CALLING_BELL:
            open_grid(st, NeowGridMode::CONFIRM_CALLING_BELL, 0);
            break;
    }
}

// --- Card offers ---------------------------------------------------------------

// NeowReward.rollRarity (NeowReward.java:370-375): ONE
// `NeowEvent.rng.randomBoolean(0.33f)` -> UNCOMMON, else COMMON. Note it is not
// AbstractDungeon.rollRarity: no d100, no cardBlizzRandomizer, no room table.
RewardCardRarity neow_roll_rarity(RngStream& neow_rng) noexcept {
    return random_boolean(neow_rng, 0.33f) ? RewardCardRarity::UNCOMMON
                                           : RewardCardRarity::COMMON;
}

// getColorlessCardFromPool(rarity) (AbstractDungeon.java:1579-1595) ->
// CardGroup.getRandomCard(true, rarity) (CardGroup.java:509-524). The draw
// itself now lives beside its RED sibling as draw_colorless_card_from_pool
// (combat_rewards.hpp) because the shop's two colourless slots are a second
// caller of exactly the same read; this stays as the local spelling Neow's
// payout code reads with.
CardId colorless_from_pool(RngStream& card_rng, RewardCardRarity rarity) noexcept {
    return draw_colorless_card_from_pool(card_rng, rarity);
}

bool offer_contains(const RunRewardItem& item, int upto, CardId id) noexcept {
    for (int j = 0; j < upto; ++j) {
        if (item.card_ids[j] == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

// NeowReward.getRewardCards (NeowReward.java:332-368): three cards. Per card ONE
// rollRarity draw off NeowEvent.rng (taken even when `rare_only` overrides the
// result, :336-339), then getCard(rarity) -> pool.getRandomCard(NeowEvent.rng),
// re-rolled while the id duplicates one already offered.
//
// It is NOT AbstractDungeon.getRewardCards: no relic count modifiers, no pity
// counter, and -- the easy miss -- NO upgrade pass, so none of the trailing
// randomBoolean(cardUpgradedChance) draws a combat reward takes happen here.
void neow_reward_cards(RunState& rs, bool rare_only, RunRewardItem& item) noexcept {
    for (int i = 0; i < kCardRewardBaseCount; ++i) {
        RewardCardRarity rarity = neow_roll_rarity(rs.neow_rng);
        if (rare_only) {
            rarity = RewardCardRarity::RARE;
        }
        CardId id;
        do {
            id = draw_card_from_pool(rs.neow_rng, rarity);
        } while (offer_contains(item, i, id));
        item.card_ids[i] = static_cast<uint16_t>(id);
    }
    item.card_count = kCardRewardBaseCount;
    item.kind = static_cast<uint8_t>(RewardItemKind::CARDS);
}

// NeowReward.getColorlessRewardCards (NeowReward.java:309-330). Same shape as
// above with two differences that both matter: a non-rare roll of COMMON is
// PROMOTED to UNCOMMON (:316-318), so a non-rare colorless offer is always
// uncommon while still paying its rollRarity draw; and the identity draws come
// off cardRng, not NeowEvent.rng.
void neow_colorless_cards(RunState& rs, bool rare_only,
                          RunRewardItem& item) noexcept {
    for (int i = 0; i < kCardRewardBaseCount; ++i) {
        RewardCardRarity rarity = neow_roll_rarity(rs.neow_rng);
        if (rare_only) {
            rarity = RewardCardRarity::RARE;
        } else if (rarity == RewardCardRarity::COMMON) {
            rarity = RewardCardRarity::UNCOMMON;
        }
        CardId id;
        do {
            id = colorless_from_pool(rs.card_rng, rarity);
        } while (offer_contains(item, i, id));
        item.card_ids[i] = static_cast<uint16_t>(id);
    }
    item.card_count = kCardRewardBaseCount;
    item.kind = static_cast<uint8_t>(RewardItemKind::CARDS);
}

// Put a freshly rolled three-card offer on the screen as the single, already
// OPEN card item -- CardRewardScreen.open with a null RewardItem, which is what
// every Neow card blessing calls (NeowReward.java:215, 219, 223, 265).
void open_neow_card_screen(RewardScreen& rewards, const RunRewardItem& item) noexcept {
    rewards = RewardScreen{};
    rewards.items[0] = item;
    rewards.count = 1;
    rewards.open_card_item = 0;
}

// --- Grid application ---------------------------------------------------------

void apply_grid(RunState& rs, NeowState& st, RngStream& misc_rng) noexcept {
    const auto mode = static_cast<NeowGridMode>(st.grid_mode);
    if (mode == NeowGridMode::TRANSFORM_UPGRADE) {
        // Astrolabe's grid (the only mode on MISC rng): the completed pick set
        // routes back through the relic's own transform application, in click
        // order (Astrolabe.update -> giveCards, Astrolabe.java:57-63, :65-79).
        relic_astrolabe_transform_cards(rs, misc_rng, st.grid_picked,
                                        st.grid_done);
        return;
    }
    if (mode == NeowGridMode::UPGRADE) {
        // NeowReward.update case UPGRADE_CARD (NeowReward.java:134-140).
        rs.master_deck[st.grid_picked[0]].upgrade = 1;
        return;
    }
    if (mode == NeowGridMode::REMOVE) {
        // NeowReward.update cases REMOVE_CARD / REMOVE_TWO (:141-156): the
        // removals are by OBJECT, so doing the higher master-deck index first
        // keeps the other index valid and produces the same deck either way.
        uint16_t a = st.grid_picked[0];
        uint16_t b = st.grid_picked[1];
        if (st.grid_done == 1) {
            (void)remove_master_deck_card(rs, a);
            return;
        }
        if (a < b) {
            const uint16_t t = a;
            a = b;
            b = t;
        }
        (void)remove_master_deck_card(rs, a);
        (void)remove_master_deck_card(rs, b);
        return;
    }

    // TRANSFORM. The list itself is `transform_card` (card_pools.hpp), the one
    // authority for AbstractDungeon.transformCard, shared with the event grids;
    // only the stream differs (NeowEvent.rng here, miscRng there). The single
    // and double cases DIFFER in order and both orders are encoded as written:
    //   TRANSFORM_CARD (:157-162):  transform draw, THEN removeCard, then obtain.
    //   TRANSFORM_TWO  (:163-173):  removeCard both, THEN transform+obtain each.
    if (st.grid_done == 1) {
        const auto src = static_cast<CardId>(rs.master_deck[st.grid_picked[0]].card_id);
        const CardId got = transform_card(rs.neow_rng, src);
        (void)remove_master_deck_card(rs, st.grid_picked[0]);
        (void)add_card_to_master_deck(rs, got);
        return;
    }

    const auto src0 = static_cast<CardId>(rs.master_deck[st.grid_picked[0]].card_id);
    const auto src1 = static_cast<CardId>(rs.master_deck[st.grid_picked[1]].card_id);
    uint16_t a = st.grid_picked[0];
    uint16_t b = st.grid_picked[1];
    if (a < b) {
        const uint16_t t = a;
        a = b;
        b = t;
    }
    (void)remove_master_deck_card(rs, a);
    (void)remove_master_deck_card(rs, b);
    const CardId got0 = transform_card(rs.neow_rng, src0);
    (void)add_card_to_master_deck(rs, got0);
    const CardId got1 = transform_card(rs.neow_rng, src1);
    (void)add_card_to_master_deck(rs, got1);
}

void open_grid(NeowState& st, NeowGridMode mode, uint8_t picks) noexcept {
    st.screen = static_cast<uint8_t>(NeowScreen::GRID);
    st.grid_mode = static_cast<uint8_t>(mode);
    st.grid_needed = picks;
    st.grid_done = 0;
    for (int i = 0; i < kNeowGridPickCap; ++i) {
        st.grid_picked[i] = 0;
    }
}

// --- Payout dispatch ----------------------------------------------------------

void apply_payout(RunState& rs, NeowState& st, RewardScreen& rewards,
                  RngStream& misc_rng, RngStream& card_random_rng,
                  NeowRewardType type) noexcept {
    st.screen = static_cast<uint8_t>(NeowScreen::DONE);
    switch (type) {
        case NeowRewardType::THREE_CARDS:
        case NeowRewardType::THREE_RARE_CARDS: {
            RunRewardItem item{};
            neow_reward_cards(rs, type == NeowRewardType::THREE_RARE_CARDS, item);
            open_neow_card_screen(rewards, item);
            st.screen = static_cast<uint8_t>(NeowScreen::CARD_REWARD);
            break;
        }
        case NeowRewardType::RANDOM_COLORLESS:
        case NeowRewardType::RANDOM_COLORLESS_2: {
            RunRewardItem item{};
            neow_colorless_cards(rs, type == NeowRewardType::RANDOM_COLORLESS_2,
                                 item);
            open_neow_card_screen(rewards, item);
            st.screen = static_cast<uint8_t>(NeowScreen::CARD_REWARD);
            break;
        }
        case NeowRewardType::ONE_RANDOM_RARE_CARD:
            // getCard(RARE, NeowEvent.rng) straight into a
            // ShowCardAndObtainEffect (NeowReward.java:232) -- the same obtain
            // door every card grant uses.
            (void)add_card_to_master_deck(
                rs, draw_card_from_pool(rs.neow_rng, RewardCardRarity::RARE));
            break;

        case NeowRewardType::REMOVE_CARD:
            open_grid(st, NeowGridMode::REMOVE, 1);
            break;
        case NeowRewardType::REMOVE_TWO:
            open_grid(st, NeowGridMode::REMOVE, 2);
            break;
        case NeowRewardType::TRANSFORM_CARD:
            open_grid(st, NeowGridMode::TRANSFORM, 1);
            break;
        case NeowRewardType::TRANSFORM_TWO_CARDS:
            open_grid(st, NeowGridMode::TRANSFORM, 2);
            break;
        case NeowRewardType::UPGRADE_CARD:
            open_grid(st, NeowGridMode::UPGRADE, 1);
            break;

        case NeowRewardType::THREE_SMALL_POTIONS: {
            // NeowReward.java:268-283. Three PotionHelper.getRandomPotion()
            // draws (one potionRng draw each -- NOT the tier-gated
            // returnRandomPotion), then combatRewardScreen.open(), whose
            // setupItemReward appends a full getRewardCards() row because
            // NeowRoom is neither a TreasureRoom nor a RestRoom and NeowEvent
            // leaves noCardsInRewards false -- so cardRng moves and the pity
            // counter moves -- and only THEN is that row deleted again.
            rewards = RewardScreen{};
            rewards.open_card_item = kNoOpenCardReward;
            for (int i = 0; i < 3; ++i) {
                RunRewardItem& item = rewards.items[rewards.count++];
                item = RunRewardItem{};
                item.kind = static_cast<uint8_t>(RewardItemKind::POTION);
                item.id = static_cast<uint16_t>(get_random_potion(rs.potion_rng));
            }
            roll_setup_item_card_reward(rs, kNeowRewardScreenRoom, rewards);
            (void)remove_first_card_reward_item(rewards);
            st.screen = static_cast<uint8_t>(NeowScreen::ITEM_REWARD);
            break;
        }

        case NeowRewardType::RANDOM_COMMON_RELIC:
            spawn_relic_and_obtain(
                rs, st, rewards, misc_rng, card_random_rng,
                return_random_relic_key(rs, RelicTier::COMMON,
                                        neow_spawn_context(rs)));
            break;
        case NeowRewardType::ONE_RARE_RELIC:
            spawn_relic_and_obtain(
                rs, st, rewards, misc_rng, card_random_rng,
                return_random_relic_key(rs, RelicTier::RARE,
                                        neow_spawn_context(rs)));
            break;
        case NeowRewardType::THREE_ENEMY_KILL:
            // Neow's Lament, a hand-built relic rather than a pool draw
            // (NeowReward.java:248-250).
            spawn_relic_and_obtain(rs, st, rewards, misc_rng, card_random_rng,
                                   RelicId::NEOWS_LAMENT);
            break;
        case NeowRewardType::BOSS_RELIC:
            // loseRelic(relics.get(0)) FIRST, then the BOSS-pool draw
            // (NeowReward.java:243-247). The order is observable: the removed
            // relic is the Ironclad's Burning Blood, and Black Blood's canSpawn
            // requires it, so the draw can never return Black Blood. The drawn
            // relic's onEquip -- including the five on_equip_screen bodies --
            // runs inside spawn_relic_and_obtain, which translates any screen
            // request onto st.screen (overriding the DONE set at the top of
            // this function).
            if (rs.relic_count > 0) {
                (void)lose_relic(rs, static_cast<RelicId>(rs.relics[0].relic_id));
            }
            spawn_relic_and_obtain(
                rs, st, rewards, misc_rng, card_random_rng,
                return_random_relic_key(rs, RelicTier::BOSS,
                                        neow_spawn_context(rs)));
            break;

        case NeowRewardType::TEN_PERCENT_HP_BONUS:
            increase_max_hp(rs, st.hp_bonus);
            break;
        case NeowRewardType::TWENTY_PERCENT_HP_BONUS:
            increase_max_hp(rs, st.hp_bonus * 2);
            break;
        case NeowRewardType::HUNDRED_GOLD:
            gain_gold(rs, kNeowGoldBonus);
            break;
        case NeowRewardType::TWO_FIFTY_GOLD:
            gain_gold(rs, kNeowLargeGoldBonus);
            break;

        case NeowRewardType::NONE:
            break;
    }
}

void apply_drawback(RunState& rs, const NeowState& st,
                    NeowDrawback drawback) noexcept {
    switch (drawback) {
        case NeowDrawback::NO_GOLD:
            // loseGold(player.gold) (NeowReward.java:198).
            lose_gold(rs, rs.gold);
            break;
        case NeowDrawback::TEN_PERCENT_HP_LOSS:
            decrease_max_health(rs, st.hp_bonus);
            break;
        case NeowDrawback::PERCENT_DAMAGE: {
            // damage(currentHealth / 10 * 3, HP_LOSS) (NeowReward.java:206).
            // Integer division first, so the loss is always strictly less than
            // current HP and this can never kill. HP_LOSS ignores block, and
            // there is no block outside combat; no S1 relic can intercept it
            // here (Tungsten Rod is BOSS tier and the boss swap is a different
            // option with no drawback).
            const int loss = static_cast<int>(rs.hp) / 10 * 3;
            int hp = static_cast<int>(rs.hp) - loss;
            if (hp < 0) {
                hp = 0;
            }
            rs.hp = static_cast<int16_t>(hp);
            break;
        }
        case NeowDrawback::CURSE:
        case NeowDrawback::NONE:
            // CURSE only arms a flag here; the card arrives after the payout.
            break;
    }
}

}  // namespace

// --- Category tables ----------------------------------------------------------

void neow_category_options(int cat, NeowDrawback drawback, NeowRewardType* out,
                           int& count) noexcept {
    count = 0;
    auto add = [&](NeowRewardType t) noexcept { out[count++] = t; };
    switch (cat) {
        case 0:  // NeowReward.java:88-96
            add(NeowRewardType::THREE_CARDS);
            add(NeowRewardType::ONE_RANDOM_RARE_CARD);
            add(NeowRewardType::REMOVE_CARD);
            add(NeowRewardType::UPGRADE_CARD);
            add(NeowRewardType::TRANSFORM_CARD);
            add(NeowRewardType::RANDOM_COLORLESS);
            break;
        case 1:  // NeowReward.java:97-104
            add(NeowRewardType::THREE_SMALL_POTIONS);
            add(NeowRewardType::RANDOM_COMMON_RELIC);
            add(NeowRewardType::TEN_PERCENT_HP_BONUS);
            add(NeowRewardType::THREE_ENEMY_KILL);
            add(NeowRewardType::HUNDRED_GOLD);
            break;
        case 2:
            // NeowReward.java:105-122. Three of the seven entries are gated on
            // the drawback that was ALREADY rolled: no second removal alongside
            // a curse, no gold alongside NO_GOLD, and the 20 % max-HP gain is
            // skipped by an early `break` when the drawback is the 10 % max-HP
            // LOSS. Every drawback therefore leaves exactly six options except
            // PERCENT_DAMAGE, which leaves all seven.
            add(NeowRewardType::RANDOM_COLORLESS_2);
            if (drawback != NeowDrawback::CURSE) {
                add(NeowRewardType::REMOVE_TWO);
            }
            add(NeowRewardType::ONE_RARE_RELIC);
            add(NeowRewardType::THREE_RARE_CARDS);
            if (drawback != NeowDrawback::NO_GOLD) {
                add(NeowRewardType::TWO_FIFTY_GOLD);
            }
            add(NeowRewardType::TRANSFORM_TWO_CARDS);
            if (drawback != NeowDrawback::TEN_PERCENT_HP_LOSS) {
                add(NeowRewardType::TWENTY_PERCENT_HP_BONUS);
            }
            break;
        case 3:  // NeowReward.java:123-125
            add(NeowRewardType::BOSS_RELIC);
            break;
        default:
            break;
    }
}

// --- Blessing roll ------------------------------------------------------------

void neow_roll_blessing(RunState& rs, NeowState& out) noexcept {
    out = NeowState{};
    out.screen = static_cast<uint8_t>(NeowScreen::BLESSING);
    out.chosen = kNeowNoChoice;
    // NeowReward.<init> line 1 for every category (NeowReward.java:59, :66):
    // (int)(maxHealth * 0.1f) -- a truncating cast, so an A20 Ironclad's 75 max
    // HP gives 7. Frozen here because every NeowReward instance computed it at
    // construction time and nothing changes max HP between then and activation.
    out.hp_bonus = static_cast<int16_t>(
        static_cast<int>(static_cast<float>(rs.max_hp) * 0.1f));

    NeowRewardType options[8];
    for (int cat = 0; cat < kNeowOptionCount; ++cat) {
        NeowDrawback drawback = NeowDrawback::NONE;
        if (cat == 2) {
            // THE DRAWBACK IS ROLLED FIRST (NeowReward.java:106-108, inside
            // getRewardOptions and therefore BEFORE the reward pick at :68),
            // and it is what decides how long the reward list is. Rolling the
            // reward first would both mis-order the stream and index a
            // differently-sized list.
            const int32_t d = random(rs.neow_rng, 0, kNeowDrawbackCount - 1);
            drawback = static_cast<NeowDrawback>(d + 1);
        }
        int count = 0;
        neow_category_options(cat, drawback, options, count);
        assert(count > 0);
        const int32_t pick = random(rs.neow_rng, 0, count - 1);
        out.option_type[cat] =
            static_cast<uint8_t>(options[static_cast<std::size_t>(pick)]);
        out.option_drawback[cat] = static_cast<uint8_t>(drawback);
    }
}

// --- Activation ---------------------------------------------------------------

void neow_activate(RunState& rs, NeowState& st, RewardScreen& rewards,
                   RngStream& misc_rng, RngStream& card_random_rng,
                   uint8_t index) noexcept {
    if (index >= kNeowOptionCount ||
        st.screen != static_cast<uint8_t>(NeowScreen::BLESSING)) {
        return;
    }
    const auto type = static_cast<NeowRewardType>(st.option_type[index]);
    const auto drawback = static_cast<NeowDrawback>(st.option_drawback[index]);
    st.chosen = index;

    // NeowReward.activate: the drawback switch (:192-212) runs first, then the
    // payout switch (:213-305).
    apply_drawback(rs, st, drawback);
    apply_payout(rs, st, rewards, misc_rng, card_random_rng, type);

    // The CURSE drawback's card is NOT part of activate(): it is obtained in the
    // next NeowReward.update tick (:183-186), which is after activate() returned
    // and before any grid/card selection the payout opened can resolve. That
    // makes the order observable on cardRng, which BOTH a colorless payout and
    // this curse draw share: the payout's colorless draws come first.
    //
    // getCardWithoutRng(CURSE) -> returnRandomCurse -> CardLibrary.getCurse():
    // one cardRng draw over the ten ordinary curses. The obtain runs through
    // the ShowCardAndObtainEffect door, so a held Omamori charge eats the curse
    // instead. Omamori is a COMMON relic (Omamori.java:18-19), so a played run
    // cannot be holding one HERE -- at floor 0 the only relic owned is the
    // starting one, and a single blessing cannot both grant the relic and roll
    // the drawback -- but the door is the door, and an imported RunState can.
    if (drawback == NeowDrawback::CURSE) {
        (void)add_card_to_master_deck(rs, return_random_curse(rs.card_rng));
    }
}

// --- Sub-screens --------------------------------------------------------------

bool neow_grid_card_legal(const RunState& rs, const NeowState& st,
                          uint16_t deck_index) noexcept {
    if (st.screen != static_cast<uint8_t>(NeowScreen::GRID) ||
        deck_index >= rs.master_deck_count) {
        return false;
    }
    const auto mode = static_cast<NeowGridMode>(st.grid_mode);
    if (mode == NeowGridMode::CONFIRM_PANDORA ||
        mode == NeowGridMode::CONFIRM_CALLING_BELL) {
        return false;
    }
    for (uint8_t i = 0; i < st.grid_done && i < kNeowGridPickCap; ++i) {
        if (st.grid_picked[i] == deck_index) {
            return false;  // gridSelectScreen never re-offers a selected card
        }
    }
    const CardInstance& card = rs.master_deck[deck_index];
    // The list the grid was opened over: getUpgradableCards for UPGRADE_CARD
    // (NeowReward.java:303), getPurgeableCards for every other grid payout
    // (:253, :257, :286, :290). Deliberately the PLAIN filters, with NO
    // getGroupWithoutBottledCards pass: neither Neow's grids nor the two
    // relic grids sharing this legality (Astrolabe.java:39/:55 via
    // getPurgeableCards, EmptyCage.java:45/:55) exclude bottled cards in the
    // Java -- do not "fix" this to match the event/shop/Toke grids, which DO
    // exclude (master_card_purgeable_unbottled and its site map,
    // relic_pools.hpp).
    return mode == NeowGridMode::UPGRADE
               ? rest_card_upgradeable(card)
               : rest_card_purgeable(card);
}

bool neow_grid_pick(RunState& rs, NeowState& st, RngStream& misc_rng,
                    uint16_t deck_index) noexcept {
    if (!neow_grid_card_legal(rs, st, deck_index)) {
        return false;
    }
    st.grid_picked[st.grid_done++] = deck_index;
    if (st.grid_done < st.grid_needed) {
        return true;  // the screen stays up until the set is complete
    }
    apply_grid(rs, st, misc_rng);
    st.grid_mode = static_cast<uint8_t>(NeowGridMode::NONE);
    st.screen = static_cast<uint8_t>(NeowScreen::DONE);
    return true;
}

void neow_skip_card(NeowState& st, RewardScreen& rewards) noexcept {
    if (st.screen != static_cast<uint8_t>(NeowScreen::CARD_REWARD)) {
        return;
    }
    rewards = RewardScreen{};
    rewards.open_card_item = kNoOpenCardReward;
    st.screen = static_cast<uint8_t>(NeowScreen::DONE);
}

void neow_finish_payout(NeowState& st) noexcept {
    st.screen = static_cast<uint8_t>(NeowScreen::DONE);
}

}  // namespace sts::engine
