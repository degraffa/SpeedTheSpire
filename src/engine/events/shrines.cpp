// The six Exordium shrine-list bodies (Exordium.initializeShrineList,
// Exordium.java:238-246). Selection and the shared screen plumbing live in
// event_framework.cpp; this file owns only the shrine state machines.
//
// RNG ATTRIBUTION -- read per event from the Java, because it is NOT uniform:
//   * Transmorgrifier   AbstractDungeon.transformCard(c, false, miscRng)
//                       (Transmogrifier.java:49) -- miscRng, exactly as Living
//                       Wall's Change. One inclusive same-colour pool draw.
//   * Wheel of Change   miscRng.random(0, 5) for the spin
//                       (GremlinWheelGame.java:229); the relic result then
//                       spends relicRng through returnRandomRelicTier /
//                       returnRandomRelicKey. MathUtils.random(-10, 10) at
//                       :230 is libGDX's GLOBAL generator and drives only the
//                       wheel's stop angle -- not a game stream, not modelled.
//   * Match and Keep    THREE streams in one constructor: cardRng for the
//                       three getCard(rarity) pool draws and every
//                       returnRandomCurse (CardLibrary.getCurse,
//                       CardLibrary.java:1022-1029), shuffleRng for
//                       returnColorlessCard's randomLong
//                       (AbstractDungeon.java:1101), and miscRng for the board
//                       shuffle's randomLong (GremlinMatchGame.java:58).
//   * Golden Shrine / Purifier / Upgrade Shrine consume NO stream at all.
//
// Provenance (each read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * GremlinMatchGame  GremlinMatchGame.java:55-92, 179-285
//   * GoldShrine        GoldShrine.java:39-101
//   * Transmogrifier    Transmogrifier.java:32-84
//   * PurificationShrine PurificationShrine.java:31-81
//   * UpgradeShrine     UpgradeShrine.java:34-92
//   * GremlinWheelGame  GremlinWheelGame.java:84-313
//   * AbstractDungeon.getCard / transformCard / returnColorlessCard
//                       AbstractDungeon.java:1481-1498, 860-878, 1100-1113
//   * CardLibrary.getCurse  CardLibrary.java:1022-1029
//   * Ironclad.getStartCardForEvent  Ironclad.java:107-110

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include "../relics/relic_pickup.hpp"
#include "event_common.hpp"
#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"

namespace sts::engine {

namespace {

using events::draw_event_relic;
using events::has_purgeable_card;
using events::has_upgradable_card;
using events::heal;
using events::one_proceed_menu;
using sts::registry::EventCardRarity;
using sts::registry::event_card_rarity;

// AbstractDungeon.getCard(rarity) == pool.getRandomCard(true) ==
// group.get(cardRng.random(size - 1)) (CardGroup.java:502-506): ONE cardRng
// draw, a pure indexed read, nothing removed from the pool.
[[nodiscard]] CardId draw_pool_card(RngStream& card_rng,
                                    EventCardRarity rarity) noexcept {
    switch (rarity) {
        case EventCardRarity::RARE:
            return kIroncladRarePool[static_cast<std::size_t>(random(
                card_rng, static_cast<int32_t>(kIroncladRarePoolCount - 1)))];
        case EventCardRarity::UNCOMMON:
            return kIroncladUncommonPool[static_cast<std::size_t>(
                random(card_rng,
                       static_cast<int32_t>(kIroncladUncommonPoolCount - 1)))];
        case EventCardRarity::COMMON:
        default:
            return kIroncladCommonPool[static_cast<std::size_t>(random(
                card_rng, static_cast<int32_t>(kIroncladCommonPoolCount - 1)))];
    }
}

// AbstractDungeon.returnColorlessCard(UNCOMMON) (AbstractDungeon.java:
// 1100-1113): ONE shuffleRng.randomLong drives a JDK shuffle of
// colorlessCardPool, then the FIRST entry of the requested rarity wins;
// SwiftStrike is the never-taken fallback (the pool always holds uncommons).
//
// DELIBERATE, RECORDED DEVIATION: the game shuffles `colorlessCardPool.group`
// IN PLACE, so the new order persists into the next reader of that same list.
// This port shuffles a local copy. Nothing in Act 1 observes the persisted
// order -- transformCard's COLORLESS branch reads the untouched
// srcColorlessCardPool (AbstractDungeon.java:998-1014), and the shop's two
// colorless slots (which EXIST now, shop.cpp -- this comment used to say
// they did not) are NOT an observer either:
// getColorlessCardFromPool reaches CardGroup.getRandomCard(true, rarity)
// (CardGroup.java:509-524), which filters into a local tmp and
// Collections.sort()s it before indexing, discarding the source order on
// every read. The ledger's Deferred obligations row stays open only against
// a future reader of the UNSORTED whole-pool view.
//
// THE INPUT ORDER IS LOAD-BEARING and it is the LIVE pool's, not the `src*`
// twin's. `addColorlessCards` fills `colorlessCardPool` with `addToTop`, which
// is `group.add(c)` -- an APPEND (AbstractDungeon.java:1203-1210,
// CardGroup.java:455-457) -- so the live pool is plain CardLibrary library
// order, while the emitted `kColorlessPool` models `srcColorlessCardPool`,
// which `initializeCardPools` fills with the PREPENDING `addToBottom`
// (:1185-1187, CardGroup.java:459-461) and which therefore holds the SAME
// membership REVERSED. Reading it backwards here restores the live order, so
// one emitted array serves both readings -- the same discipline the RED pools
// use. It used to read a separately emitted `kEventTransformColorlessPool`
// whose order was a plain walk of `cards.yaml` rows, i.e. neither.
[[nodiscard]] CardId draw_colorless_uncommon(RngStream& shuffle_rng) noexcept {
    std::array<CardId, static_cast<std::size_t>(kColorlessPoolCount)> pool{};
    for (int i = 0; i < kColorlessPoolCount; ++i) {
        pool[static_cast<std::size_t>(i)] = kColorlessPool[
            static_cast<std::size_t>(kColorlessPoolCount - 1 - i)];
    }
    JdkRandom jdk(random_long(shuffle_rng));
    jdk_shuffle(std::span<CardId>(pool), jdk);
    for (CardId id : pool) {
        if (event_card_rarity(id) == EventCardRarity::UNCOMMON) {
            return id;
        }
    }
    return CardId::SWIFT_STRIKE;
}

// --- Match and Keep! --------------------------------------------------------
// GremlinMatchGame screens: 0 INTRO, 1 RULE_EXPLANATION, 2 PLAY (the twelve
// board slots ARE the options), 3 COMPLETE.
constexpr uint8_t kMatchIntro = 0;
constexpr uint8_t kMatchRules = 1;
constexpr uint8_t kMatchPlay = 2;
constexpr uint8_t kMatchDone = 3;

constexpr int kMatchAttempts = 5;  // GremlinMatchGame.java:46
constexpr int16_t kMatchNoneFlipped = -1;

void match_deal(RunController& rc, EventDialogState& es) noexcept {
    // initializeCards (GremlinMatchGame.java:63-92). Six identities in Java
    // order, then each is duplicated by makeStatEquivalentCopy, so the board
    // is [six originals][six copies] BEFORE the shuffle.
    CardId ids[6]{};
    ids[0] = draw_pool_card(rc.run.card_rng, EventCardRarity::RARE);
    ids[1] = draw_pool_card(rc.run.card_rng, EventCardRarity::UNCOMMON);
    ids[2] = draw_pool_card(rc.run.card_rng, EventCardRarity::COMMON);
    if (rc.run.ascension >= 15) {
        ids[3] = return_random_curse(rc.run.card_rng);
        ids[4] = return_random_curse(rc.run.card_rng);
    } else {
        ids[3] = draw_colorless_uncommon(rc.combat.shuffle_rng);
        ids[4] = return_random_curse(rc.run.card_rng);
    }
    // player.getStartCardForEvent() -- Bash for the Ironclad, no stream.
    ids[5] = CardId::BASH;

    // The relics' onPreviewObtainCard pass (:80-85) is the eggs' documented
    // deferral (registry/relics.yaml FrozenEgg2 row; relic_pickup_uncommon.cpp
    // :132-137). It changes only the upgrade shown ON THE BOARD: a matched card
    // is obtained through add_card_to_master_deck below, whose onObtainCard
    // pass upgrades exactly the same eggs' types, so the resulting MASTER DECK
    // is identical either way and no save-parity state diverges.
    for (int i = 0; i < 6; ++i) {
        es.board[i].card_id = static_cast<uint16_t>(ids[i]);
        es.board[i].upgrade = 0;
        es.board[i].taken = 0;
        es.board[i + 6] = es.board[i];
    }

    // Collections.shuffle(cards.group, new Random(miscRng.randomLong()))
    // (:58) -- one randomLong, then the exact JDK shuffle of all twelve.
    JdkRandom jdk(random_long(rc.combat.misc_rng));
    jdk_shuffle(std::span<EventBoardCard>(es.board, kEventBoardCap), jdk);

    es.scratch0 = static_cast<int16_t>(kMatchAttempts);
    es.scratch1 = kMatchNoneFlipped;
}

void match_enter(RunController& rc, EventDialogState& es) {
    // The deal happens in the CONSTRUCTOR (:55-61), i.e. at room entry, before
    // the player has pressed anything.
    match_deal(rc, es);
    es.screen = kMatchIntro;
}

void match_menu(const RunController& /*rc*/, const EventDialogState& es,
                EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen != kMatchPlay) {
        one_proceed_menu(out);
        return;
    }
    // updateMatchGameLogic (:179-214) accepts a click only on a card that is
    // still on the board AND still face down -- the already-flipped chosenCard
    // has isFlipped == false, so it cannot be picked twice.
    out.count = static_cast<uint8_t>(kEventBoardCap);
    for (int i = 0; i < kEventBoardCap; ++i) {
        out.enabled[i] = es.board[i].taken == 0 && es.scratch1 != i;
    }
}

EventDialogStatus match_choose(RunController& rc, EventDialogState& es,
                               uint8_t option) {
    if (es.screen == kMatchIntro) {
        es.screen = kMatchRules;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == kMatchRules) {
        es.screen = kMatchPlay;  // placeCards (:278-285)
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != kMatchPlay) {
        return EventDialogStatus::FINISHED;
    }
    if (option >= kEventBoardCap || es.board[option].taken != 0 ||
        es.scratch1 == option) {
        return EventDialogStatus::CONTINUE;
    }
    if (es.scratch1 == kMatchNoneFlipped) {
        es.scratch1 = static_cast<int16_t>(option);  // chosenCard (:191-194)
        return EventDialogStatus::CONTINUE;
    }

    const int first = es.scratch1;
    es.scratch1 = kMatchNoneFlipped;
    if (es.board[first].card_id == es.board[option].card_id) {
        // The pair leaves the board and the CHOSEN copy is obtained through
        // ShowCardAndObtainEffect (:221-224) -- the Omamori-aware door.
        es.board[first].taken = 1;
        es.board[option].taken = 1;
        (void)add_card_to_master_deck(
            rc.run, static_cast<CardId>(es.board[first].card_id),
            es.board[first].upgrade);
    }
    // attemptCount-- happens on EVERY resolved pair, match or miss (:235-239).
    es.scratch0 = static_cast<int16_t>(es.scratch0 - 1);
    if (es.scratch0 <= 0) {
        es.screen = kMatchDone;
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kMatchAndKeep = {
    &match_enter,
    &match_menu,
    &match_choose,
};

// --- Golden Shrine ----------------------------------------------------------

constexpr int gold_shrine_amount(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 50 : 100;  // GoldShrine.java:41
}

void gold_shrine_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void gold_shrine_menu(const RunController& /*rc*/, const EventDialogState& es,
                      EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 3;
        out.enabled[0] = true;
        out.enabled[1] = true;
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus gold_shrine_choose(RunController& rc, EventDialogState& es,
                                     uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        gain_gold(rc.run, gold_shrine_amount(rc.run));
    } else if (option == 1) {
        // Desecrate: gainGold(275) precedes the ShowCardAndObtainEffect
        // (GoldShrine.java:80-81) -- the OPPOSITE order to Liars Game, so the
        // two are written the way each Java body writes them. The curse still
        // goes through the Omamori-aware obtain door.
        gain_gold(rc.run, 275);
        (void)add_card_to_master_deck(rc.run, CardId::REGRET);
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kGoldenShrine = {
    &gold_shrine_enter,
    &gold_shrine_menu,
    &gold_shrine_choose,
};

// --- Transmorgrifier --------------------------------------------------------

void transmogrifier_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void transmogrifier_menu(const RunController& /*rc*/,
                         const EventDialogState& es, EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus transmogrifier_choose(RunController& rc,
                                        EventDialogState& es, uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        // Transmogrifier.update (:46-54): remove first, then
        // transformCard(c, false, miscRng) -- byte-identical to Living Wall's
        // Change, so it reuses the shared transform door.
        (void)event_grid_transform_card(rc.run, es, rc.combat.misc_rng, option);
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    es.screen = 1;
    if (option == 0 && has_purgeable_card(rc.run)) {
        // The Java opens the grid unconditionally; an empty group would leave
        // the screen with nothing selectable. Guarded exactly as Living Wall
        // guards its own grids (LivingWall.java:68-89 / exordium_events_ii).
        open_event_grid(es, EventGridKind::TRANSFORMABLE);
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kTransmorgrifier = {
    &transmogrifier_enter,
    &transmogrifier_menu,
    &transmogrifier_choose,
};

// --- Purifier ---------------------------------------------------------------

void purifier_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void purifier_menu(const RunController& /*rc*/, const EventDialogState& es,
                   EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus purifier_choose(RunController& rc, EventDialogState& es,
                                  uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        (void)event_grid_remove_card(rc.run, es, option);
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    es.screen = 1;
    if (option == 0 && has_purgeable_card(rc.run)) {
        open_event_grid(es, EventGridKind::PURGE);
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kPurifier = {
    &purifier_enter,
    &purifier_menu,
    &purifier_choose,
};

// --- Upgrade Shrine ---------------------------------------------------------

void upgrade_shrine_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void upgrade_shrine_menu(const RunController& rc, const EventDialogState& es,
                         EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        out.count = 2;
        // The constructor greys out option 0 when the deck has no upgradable
        // card (UpgradeShrine.java:36-40).
        out.enabled[0] = has_upgradable_card(rc.run);
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus upgrade_shrine_choose(RunController& rc,
                                        EventDialogState& es, uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        (void)event_grid_upgrade_card(rc.run, es, option);
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    es.screen = 1;
    if (option == 0 && has_upgradable_card(rc.run)) {
        open_event_grid(es, EventGridKind::UPGRADE);
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kUpgradeShrine = {
    &upgrade_shrine_enter,
    &upgrade_shrine_menu,
    &upgrade_shrine_choose,
};

// --- Wheel of Change --------------------------------------------------------
// Screens: 0 INTRO (spin), 1 COMPLETE (acknowledge -> applyResult), 2 LEAVE.
// scratch0 holds the spin result 0..5.
constexpr uint8_t kWheelIntro = 0;
constexpr uint8_t kWheelResult = 1;
constexpr uint8_t kWheelLeave = 2;

constexpr int wheel_gold_amount(const RunState& rs) noexcept {
    // setGold (GremlinWheelGame.java:100-108) keys off the dungeon id.
    return rs.act >= 3 ? 300 : rs.act == 2 ? 200 : 100;
}

constexpr float wheel_hp_loss_percent(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 0.15f : 0.1f;  // :81-92
}

void wheel_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kWheelIntro;
}

void wheel_menu(const RunController& /*rc*/, const EventDialogState& es,
                EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus wheel_choose(RunController& rc, EventDialogState& es,
                               uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        // The card-removal result's grid (:286-290); purgeLogic (:303-313)
        // removes the pick and leaves the LEAVE page up.
        (void)event_grid_remove_card(rc.run, es, option);
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == kWheelIntro) {
        // buttonEffect INTRO (:226-236): one miscRng.random(0, 5).
        es.scratch0 = static_cast<int16_t>(random(rc.combat.misc_rng, 0, 5));
        // preApplyResult (:186-221) is reached before the player acknowledges
        // the spin, and the GOLD result pays out THERE (:191-192), not in
        // applyResult -- which only logs it (:257-260).
        if (es.scratch0 == 0) {
            gain_gold(rc.run, wheel_gold_amount(rc.run));
        }
        es.screen = kWheelResult;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != kWheelResult) {
        return EventDialogStatus::FINISHED;
    }
    (void)option;

    // applyResult (:255-301).
    switch (es.scratch0) {
        case 0:
            break;  // gold already paid in preApplyResult
        case 1: {
            // rewards.clear(); addRelicToRewards(returnRandomScreenlessRelic(
            // returnRandomRelicTier())); room COMPLETE; combatRewardScreen
            // .open(). The room never returns to the dialog after that -- the
            // COMPLETE phase stops EventRoom.update from ticking the event
            // (EventRoom.java:33-41) -- so the reward screen IS the exit.
            const RelicId id = draw_event_relic(rc.run, /*screenless=*/true);
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            (void)add_event_combat_relic_reward(rc.rewards, id);
            rc.event = EventDialogState{};
            rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
            return EventDialogStatus::TRANSITIONED;
        }
        case 2:
            heal(rc.run, rc.run.max_hp);  // heal(maxHealth) == full heal
            break;
        case 3:
            (void)add_card_to_master_deck(rc.run, CardId::DECAY);
            break;
        case 4:
            if (has_purgeable_card(rc.run)) {
                open_event_grid(es, EventGridKind::PURGE);
            }
            break;
        default: {
            // DamageInfo(null, amount, HP_LOSS): a null owner skips every
            // onAttacked relic (AbstractPlayer.java:1405-1429) and HP_LOSS
            // changes only the unmodelled hpLossThisCombat counter, so the
            // NULL_SOURCE door is byte-exact here.
            const int damage = static_cast<int>(
                static_cast<float>(rc.run.max_hp) * wheel_hp_loss_percent(rc.run));
            (void)apply_event_damage(rc, damage, EventDamageOwner::NULL_SOURCE);
            break;
        }
    }
    es.screen = kWheelLeave;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kWheelOfChange = {
    &wheel_enter,
    &wheel_menu,
    &wheel_choose,
};

}  // namespace

const EventDialogImpl* event_native_match_and_keep() noexcept {
    return &kMatchAndKeep;
}

const EventDialogImpl* event_native_golden_shrine() noexcept {
    return &kGoldenShrine;
}

const EventDialogImpl* event_native_transmorgrifier() noexcept {
    return &kTransmorgrifier;
}

const EventDialogImpl* event_native_purifier() noexcept {
    return &kPurifier;
}

const EventDialogImpl* event_native_upgrade_shrine() noexcept {
    return &kUpgradeShrine;
}

const EventDialogImpl* event_native_wheel_of_change() noexcept {
    return &kWheelOfChange;
}

}  // namespace sts::engine
