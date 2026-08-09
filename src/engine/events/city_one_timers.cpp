// The five act-gated one-timer bodies S2.32 owns: Knowing Skull, The Joust,
// N'loth (specialOneTimeEventList, Act-2 gates) and Designer / Duplicator
// (Act-2/3 gates). Selection, the act gates and the pool bookkeeping are
// S2.13's (event_framework.cpp); this file owns only the dialog state
// machines.
//
// Provenance (each read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * KnowingSkull.java (182 lines)  -- ctor :60-74, buttonEffect :84-122,
//     obtainReward :124-172, setLeave :174-179
//   * TheJoust.java (143 lines)      -- ctor :45-48, buttonEffect :75-140
//   * Nloth.java (117 lines)         -- ctor :34-45, buttonEffect :55-114
//   * Designer.java (261 lines)      -- ctor :48-65, update :67-155,
//     buttonEffect :157-228, upgradeTwoRandomCards :230-258
//   * Duplicator.java (95 lines)     -- ctor :27-31, update :38-50,
//     buttonEffect :52-84, use :86-88
//   * PotionHelper.getRandomPotion   PotionHelper.java:169-172
//   * AbstractPlayer.obtainPotion    AbstractPlayer.java (first empty slot
//     below potionSlots or the potion is lost)
//   * AbstractDungeon.returnColorlessCard  AbstractDungeon.java:1100-1113
//   * AbstractDungeon.transformCard  AbstractDungeon.java:860-878

#include "sts/engine/event_framework.hpp"

#include <cstdint>
#include <span>

#include "../relics/relic_pickup.hpp"  // gain_gold / lose_gold doors
#include "event_common.hpp"
#include "sts/engine/card_pools.hpp"   // transform_card (the ONE list)
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"  // run_has_relic
#include "sts/engine/potions.hpp"      // get_random_potion / kPotionCap
#include "sts/engine/relic_pools.hpp"  // acquire_relic / lose_relic
#include "sts/engine/rest_sites.hpp"   // rest_card_upgradeable
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

using events::has_upgradable_card;
using events::one_proceed_menu;

// --- Knowing Skull -----------------------------------------------------------
// Screens: 0 INTRO_1, 1 ASK, 2 COMPLETE. scratch0/1/2 = the ramping
// potion/gold/card costs (each starts at 6, KnowingSkull.java:67-69, and only
// the purchased option's cost ++es -- AFTER the damage is paid, :124-166).
// The leave cost is the never-incremented 6 (:112-114).

constexpr uint8_t kSkullIntro = 0;
constexpr uint8_t kSkullAsk = 1;
constexpr uint8_t kSkullDone = 2;
constexpr int16_t kSkullBaseCost = 6;  // KnowingSkull.java:67
constexpr int32_t kSkullGoldReward = 90;  // GOLD_REWARD (:51)

void skull_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kSkullIntro;
    es.scratch0 = kSkullBaseCost;  // potionCost
    es.scratch1 = kSkullBaseCost;  // goldCost
    es.scratch2 = kSkullBaseCost;  // cardCost
}

void skull_menu(const RunController& /*rc*/, const EventDialogState& es,
                EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kSkullAsk) {
        // Four always-enabled buttons (:89-92, :167-171): the game never greys
        // one out -- a purchase can kill.
        out.count = 4;
        for (int i = 0; i < 4; ++i) {
            out.enabled[i] = true;
        }
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus skull_choose(RunController& rc, EventDialogState& es,
                               uint8_t option) {
    if (es.screen == kSkullIntro) {
        es.screen = kSkullAsk;  // :86-94
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != kSkullAsk) {
        return EventDialogStatus::FINISHED;
    }
    // Every arm pays DamageInfo(null, cost, HP_LOSS) FIRST (:127, :142, :153,
    // :112): null owner (no Torii), Tungsten Rod still applies, and the Fairy /
    // Lizard Tail revives live inside apply_event_damage. The game keeps
    // executing the grant after a lethal hit -- the reward lands on a dead run
    // -- so the grant below is applied regardless and only the RETURN differs.
    bool alive = true;
    switch (option) {
        case 0: {  // A POTION (:126-139)
            alive = apply_event_damage(rc, es.scratch0,
                                       EventDamageOwner::NULL_SOURCE);
            ++es.scratch0;  // ++potionCost, after the damage (:129)
            if (run_has_relic(rc.run, RelicId::SOZU)) {
                // Sozu short-circuits BEFORE getRandomPotion (:132-135): no
                // potionRng draw at all, nothing obtained.
                break;
            }
            const PotionId p = get_random_potion(rc.run.potion_rng);
            // obtainPotion: the first empty slot below potionSlots, or the
            // potion is lost (the draw is spent either way).
            const uint8_t slots =
                rc.run.potion_slots < static_cast<uint8_t>(kPotionCap)
                    ? rc.run.potion_slots
                    : static_cast<uint8_t>(kPotionCap);
            for (uint8_t i = 0; i < slots; ++i) {
                if (static_cast<PotionId>(rc.run.potions[i]) ==
                    PotionId::NONE) {
                    rc.run.potions[i] = static_cast<uint16_t>(p);
                    break;
                }
            }
            break;
        }
        case 1:  // GOLD (:141-150)
            alive = apply_event_damage(rc, es.scratch1,
                                       EventDamageOwner::NULL_SOURCE);
            ++es.scratch1;  // ++goldCost (:144)
            gain_gold(rc.run, kSkullGoldReward);
            break;
        case 2: {  // A COLORLESS CARD (:152-161)
            alive = apply_event_damage(rc, es.scratch2,
                                       EventDamageOwner::NULL_SOURCE);
            ++es.scratch2;  // ++cardCost (:155)
            // returnColorlessCard(UNCOMMON): one shuffleRng randomLong + the
            // persistent live-pool shuffle -- the second buy in one visit
            // reads the first buy's permutation (event_framework.hpp).
            (void)add_card_to_master_deck(rc.run,
                                          event_draw_colorless_uncommon(rc));
            break;
        }
        default:  // LEAVE pays the flat 6 (:112-114) and closes the dialog
            alive = apply_event_damage(rc, kSkullBaseCost,
                                       EventDamageOwner::NULL_SOURCE);
            es.screen = kSkullDone;
            break;
    }
    return alive ? EventDialogStatus::CONTINUE
                 : EventDialogStatus::TRANSITIONED;
}

constexpr EventDialogImpl kKnowingSkull = {
    &skull_enter,
    &skull_menu,
    &skull_choose,
};

// --- The Joust ---------------------------------------------------------------
// Screens: 0 HALT, 1 EXPLANATION, 2 PRE_JOUST, 3 JOUST, 4 COMPLETE.
// scratch0 = betFor (the player's displayed bet); scratch1 = ownerWins,
// rolled at the PRE_JOUST continue (:101) -- one screen BEFORE the reveal,
// which is why PublicView masks it (event_scratch_public_mask).

constexpr uint8_t kJoustHalt = 0;
constexpr uint8_t kJoustExplanation = 1;
constexpr uint8_t kJoustPreJoust = 2;
constexpr uint8_t kJoustJoust = 3;
constexpr uint8_t kJoustDone = 4;
constexpr int32_t kJoustBet = 50;          // BET_AMT (:40)
constexpr int32_t kJoustOwnerPay = 250;    // WIN_OWNER (:38)
constexpr int32_t kJoustMurdererPay = 100; // WIN_MURDERER (:39)

void joust_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kJoustHalt;
}

void joust_menu(const RunController& /*rc*/, const EventDialogState& es,
                EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kJoustExplanation) {
        // Two bets, neither ever greyed out (:79-80): the draw gate already
        // required gold >= 50 and no action between selection and bet can
        // spend any; loseGold clamps regardless.
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus joust_choose(RunController& rc, EventDialogState& es,
                               uint8_t option) {
    switch (es.screen) {
        case kJoustHalt:
            es.screen = kJoustExplanation;  // :77-82
            return EventDialogStatus::CONTINUE;
        case kJoustExplanation:
            // 0 = bet on the murderer (betFor false), 1 = bet on the owner
            // (betFor true); BOTH pay 50 up front (:84-96).
            es.scratch0 = option == 1 ? 1 : 0;
            lose_gold(rc.run, kJoustBet);
            es.screen = kJoustPreJoust;
            return EventDialogStatus::CONTINUE;
        case kJoustPreJoust:
            // ownerWins = miscRng.randomBoolean(0.3f) (:101): rolled HERE, at
            // the continue AFTER the bet, not at bet time.
            es.scratch1 = random_boolean(rc.combat.misc_rng, 0.3f) ? 1 : 0;
            es.screen = kJoustJoust;
            return EventDialogStatus::CONTINUE;
        case kJoustJoust:
            // The payout matrix (:106-134): a won bet on the owner pays 250, a
            // won bet on the murderer pays 100, a lost bet pays nothing (the
            // 50 is already gone).
            if (es.scratch1 != 0 && es.scratch0 != 0) {
                gain_gold(rc.run, kJoustOwnerPay);
            } else if (es.scratch1 == 0 && es.scratch0 == 0) {
                gain_gold(rc.run, kJoustMurdererPay);
            }
            es.screen = kJoustDone;
            return EventDialogStatus::CONTINUE;
        default:
            return EventDialogStatus::FINISHED;
    }
}

constexpr EventDialogImpl kTheJoust = {
    &joust_enter,
    &joust_menu,
    &joust_choose,
};

// --- N'loth ------------------------------------------------------------------
// Screens: 0 offer, 1 done. scratch0/1 = the two offered relic ids, named on
// the buttons: the ctor shuffles a COPY of the relic list with
// Collections.shuffle(new Random(miscRng.randomLong())) and offers
// shuffled[0] / shuffled[1] (:36-44). The draw gate required relics.size()
// >= 2 (AbstractDungeon.java:1914-1918).

void nloth_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    uint16_t ids[kRelicCap] = {};
    const uint8_t n = rc.run.relic_count;
    for (uint8_t i = 0; i < n; ++i) {
        ids[i] = rc.run.relics[i].relic_id;
    }
    JdkRandom jr(random_long(rc.combat.misc_rng));
    jdk_shuffle(std::span<uint16_t>(ids, n), jr);
    es.scratch0 = static_cast<int16_t>(n > 0 ? ids[0] : 0);
    es.scratch1 = static_cast<int16_t>(n > 1 ? ids[1] : 0);
}

void nloth_menu(const RunController& /*rc*/, const EventDialogState& es,
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

EventDialogStatus nloth_choose(RunController& rc, EventDialogState& es,
                               uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0 || option == 1) {
        const RelicId chosen = static_cast<RelicId>(
            option == 0 ? es.scratch0 : es.scratch1);
        if (run_has_relic(rc.run, RelicId::NLOTHS_GIFT)) {
            // The dead-in-practice re-entry arm (:61-64, :77-80), reproduced
            // faithfully: a Circlet is obtained and the chosen relic is NOT
            // lost. (The event is once per run and the gift has no second
            // source, so reaching it needs an imported state.)
            (void)acquire_relic(rc.run, rc.combat.misc_rng, RelicId::CIRCLET);
        } else {
            // loseRelic(choice.relicId) removes the FIRST instance with that
            // id (:67, :83), then spawnRelicAndObtain(Nloth's Gift).
            (void)lose_relic(rc.run, chosen);
            (void)acquire_relic(rc.run, rc.combat.misc_rng,
                                RelicId::NLOTHS_GIFT);
        }
    }
    es.screen = 1;  // option 2 and the trade arms all land on the same page
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kNloth = {
    &nloth_enter,
    &nloth_menu,
    &nloth_choose,
};

// --- Designer ----------------------------------------------------------------
// Screens: 0 INTRO, 1 MAIN, 2 DONE. scratch0 = adjustmentUpgradesOne,
// scratch1 = cleanUpRemovesCards -- TWO miscRng.randomBoolean() draws at the
// ctor (:52-53), which pick both the wording and the mechanic of options 0/1.
// scratch2 = which purge-grid flow is open (1 = Clean Up's remove, 2 = Full
// Service); scratch3 = the first pick of the two-card transform
// (TRANSFORM_PAIR_SECOND's exclusion, event_framework.hpp).

constexpr uint8_t kDesignerIntro = 0;
constexpr uint8_t kDesignerMain = 1;
constexpr uint8_t kDesignerDone = 2;
constexpr int16_t kDesignerFlowCleanUp = 1;
constexpr int16_t kDesignerFlowFullService = 2;

int designer_adjust_cost(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 50 : 40;  // :54-64
}
int designer_clean_up_cost(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 75 : 60;
}
int designer_full_service_cost(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 110 : 90;
}
int designer_punch_hp_loss(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 5 : 3;
}

// CardGroup.getGroupWithoutBottledCards(masterDeck).size() -- the WHOLE deck
// minus bottled cards, curses included. This is the Java's option gate
// (:169-173), which is deliberately LOOSER than the purge grids it opens
// (those filter to purgeable cards too): reproduced, not corrected.
[[nodiscard]] int unbottled_deck_count(const RunState& rs) noexcept {
    int n = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (!master_card_bottled(rs.master_deck[i])) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] bool designer_grid_has_card(const RunState& rs,
                                          EventGridKind kind,
                                          int16_t exclude) noexcept {
    EventDialogState probe{};
    open_event_grid(probe, kind);
    probe.scratch3 = exclude;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (event_grid_card_legal(rs, probe, i)) {
            return true;
        }
    }
    return false;
}

// Collections.shuffle(upgradableCards, new Random(miscRng.randomLong())) then
// upgrade the first `count` (upgradeTwoRandomCards :230-258 with count 2;
// Full Service's tail :90-105 with count 1). THE randomLong IS DRAWN EVEN
// WHEN THE LIST IS EMPTY -- both call sites shuffle before testing isEmpty.
void designer_upgrade_random(RunController& rc, int count) noexcept {
    uint16_t idx[kMasterDeckCap] = {};
    uint16_t n = 0;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (rest_card_upgradeable(rc.run.master_deck[i])) {
            idx[n++] = i;
        }
    }
    JdkRandom jr(random_long(rc.combat.misc_rng));
    jdk_shuffle(std::span<uint16_t>(idx, n), jr);
    const int upgrades = count < static_cast<int>(n) ? count
                                                     : static_cast<int>(n);
    for (int i = 0; i < upgrades; ++i) {
        ++rc.run.master_deck[idx[i]].upgrade;
    }
}

void designer_enter(RunController& rc, EventDialogState& es) {
    es.screen = kDesignerIntro;
    // The ctor's two draws, in source order (:52-53).
    es.scratch0 = random_boolean(rc.combat.misc_rng) ? 1 : 0;
    es.scratch1 = random_boolean(rc.combat.misc_rng) ? 1 : 0;
    es.scratch2 = 0;
    es.scratch3 = -1;
}

void designer_menu(const RunController& rc, const EventDialogState& es,
                   EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;  // grid picks ride can_choose_master_deck
    }
    if (es.screen == kDesignerMain) {
        // The four MAIN buttons with the Java's own disable predicates
        // (:163-174). Note the gates read the UNBOTTLED WHOLE-DECK count,
        // not the purgeable one -- see unbottled_deck_count.
        const RunState& rs = rc.run;
        out.count = 4;
        out.enabled[0] = rs.gold >= designer_adjust_cost(rs) &&
                         has_upgradable_card(rs);
        out.enabled[1] =
            rs.gold >= designer_clean_up_cost(rs) &&
            unbottled_deck_count(rs) >= (es.scratch1 != 0 ? 1 : 2);
        out.enabled[2] = rs.gold >= designer_full_service_cost(rs) &&
                         unbottled_deck_count(rs) >= 1;
        out.enabled[3] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus designer_choose(RunController& rc, EventDialogState& es,
                                  uint8_t option) {
    RunState& rs = rc.run;
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        switch (static_cast<EventGridKind>(es.grid_kind)) {
            case EventGridKind::UPGRADE:
                if (event_grid_upgrade_card(rs, es, option)) {
                    es.screen = kDesignerDone;
                }
                return EventDialogStatus::CONTINUE;
            case EventGridKind::PURGE:
                if (event_grid_remove_card(rs, es, option)) {
                    if (es.scratch2 == kDesignerFlowFullService) {
                        // Full Service's tail (:90-105): the upgradable list
                        // is built AFTER the removal, shuffled off one
                        // miscRng.randomLong, and its head upgraded (if any).
                        designer_upgrade_random(rc, 1);
                    }
                    es.scratch2 = 0;
                    es.screen = kDesignerDone;
                }
                return EventDialogStatus::CONTINUE;
            case EventGridKind::TRANSFORMABLE: {
                // First pick of the two-card transform: the game's grid wants
                // TWO cards and confirms with one only when the pool holds no
                // second (selectedCards.size() == 1, :130-138). Nothing is
                // removed until the confirm.
                if (!event_grid_card_legal(rs, es, option)) {
                    return EventDialogStatus::CONTINUE;
                }
                es.scratch3 = static_cast<int16_t>(option);
                if (designer_grid_has_card(rs, EventGridKind::TRANSFORM_PAIR_SECOND,
                                           es.scratch3)) {
                    open_event_grid(es, EventGridKind::TRANSFORM_PAIR_SECOND);
                    return EventDialogStatus::CONTINUE;
                }
                // The single-card confirm (:130-138): remove, one transform
                // roll, obtain.
                const CardId removed =
                    static_cast<CardId>(rs.master_deck[option].card_id);
                (void)remove_master_deck_card(rs, option);
                (void)add_card_to_master_deck(
                    rs, transform_card(rc.combat.misc_rng, removed));
                close_event_grid(es);
                es.scratch3 = -1;
                es.screen = kDesignerDone;
                return EventDialogStatus::CONTINUE;
            }
            case EventGridKind::TRANSFORM_PAIR_SECOND: {
                if (!event_grid_card_legal(rs, es, option)) {
                    return EventDialogStatus::CONTINUE;
                }
                // The two-card confirm (:113-129), in the Java's own order:
                // remove first pick, roll its transform, remove second pick,
                // roll its transform -- then the two ShowCardAndObtainEffects
                // resolve, appending the replacements in pick order AFTER
                // both removals (effectsQueue drains after buttonEffect).
                const uint16_t first = static_cast<uint16_t>(es.scratch3);
                const uint16_t second = option;
                const CardId removed_a =
                    static_cast<CardId>(rs.master_deck[first].card_id);
                (void)remove_master_deck_card(rs, first);
                const CardId t_a = transform_card(rc.combat.misc_rng, removed_a);
                const uint16_t second_adj =
                    second > first ? static_cast<uint16_t>(second - 1) : second;
                const CardId removed_b =
                    static_cast<CardId>(rs.master_deck[second_adj].card_id);
                (void)remove_master_deck_card(rs, second_adj);
                const CardId t_b = transform_card(rc.combat.misc_rng, removed_b);
                (void)add_card_to_master_deck(rs, t_a);
                (void)add_card_to_master_deck(rs, t_b);
                close_event_grid(es);
                es.scratch3 = -1;
                es.screen = kDesignerDone;
                return EventDialogStatus::CONTINUE;
            }
            case EventGridKind::NONE:
            case EventGridKind::DUPLICATE:
            default:
                return EventDialogStatus::CONTINUE;
        }
    }
    if (es.screen == kDesignerIntro) {
        es.screen = kDesignerMain;  // :160-176
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != kDesignerMain) {
        return EventDialogStatus::FINISHED;
    }
    switch (option) {
        case 0:  // ADJUSTMENTS (:180-190)
            lose_gold(rs, designer_adjust_cost(rs));
            if (es.scratch0 != 0) {
                // Upgrade-one: the 1-pick getUpgradableCards grid (:185).
                if (events::has_upgradable_card(rs)) {
                    open_event_grid(es, EventGridKind::UPGRADE);
                } else {
                    es.screen = kDesignerDone;  // gold gone regardless (:238)
                }
            } else {
                // Upgrade-two-random: one randomLong shuffle, first two
                // upgraded; the gold is lost even when nothing upgrades.
                designer_upgrade_random(rc, 2);
                es.screen = kDesignerDone;
            }
            return EventDialogStatus::CONTINUE;
        case 1:  // CLEAN UP (:191-202)
            lose_gold(rs, designer_clean_up_cost(rs));
            if (es.scratch1 != 0) {
                if (designer_grid_has_card(rs, EventGridKind::PURGE, -1)) {
                    es.scratch2 = kDesignerFlowCleanUp;
                    open_event_grid(es, EventGridKind::PURGE);
                } else {
                    // DEFENSIVE DEVIATION: the gate counts unbottled CURSES
                    // the purge grid then excludes, so the Java can open a
                    // mandatory EMPTY grid here and soft-lock. The gold is
                    // spent (it left at :193, before the open) and the dialog
                    // completes instead of hanging.
                    es.screen = kDesignerDone;
                }
            } else {
                if (designer_grid_has_card(rs, EventGridKind::TRANSFORMABLE,
                                           -1)) {
                    open_event_grid(es, EventGridKind::TRANSFORMABLE);
                } else {
                    es.screen = kDesignerDone;  // same defensive deviation
                }
            }
            return EventDialogStatus::CONTINUE;
        case 2:  // FULL SERVICE (:203-209)
            lose_gold(rs, designer_full_service_cost(rs));
            if (designer_grid_has_card(rs, EventGridKind::PURGE, -1)) {
                es.scratch2 = kDesignerFlowFullService;
                open_event_grid(es, EventGridKind::PURGE);
            } else {
                es.screen = kDesignerDone;  // same defensive deviation; the
                                            // random upgrade is unreachable in
                                            // the game's soft-locked twin too
            }
            return EventDialogStatus::CONTINUE;
        default: {  // PUNCH IT (:210-216)
            const bool alive = apply_event_damage(
                rc, designer_punch_hp_loss(rs), EventDamageOwner::NULL_SOURCE);
            es.screen = kDesignerDone;
            return alive ? EventDialogStatus::CONTINUE
                         : EventDialogStatus::TRANSITIONED;
        }
    }
}

constexpr EventDialogImpl kDesigner = {
    &designer_enter,
    &designer_menu,
    &designer_choose,
};

// --- Duplicator --------------------------------------------------------------
// Screens: 0 offer, 1 done. The copy grid opens over the WHOLE master deck
// (EventGridKind::DUPLICATE) and the pick is obtained as a
// makeStatEquivalentCopy with the bottle flags cleared (:42-47): same id, same
// upgrade count, never bottled. No RNG anywhere.

void duplicator_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void duplicator_menu(const RunController& /*rc*/, const EventDialogState& es,
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

EventDialogStatus duplicator_choose(RunController& rc, EventDialogState& es,
                                    uint8_t option) {
    RunState& rs = rc.run;
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        if (event_grid_card_legal(rs, es, option)) {
            const CardInstance& picked = rs.master_deck[option];
            // makeStatEquivalentCopy keeps the upgrade count (a x3-upgraded
            // Searing Blow copies at x3); the cleared inBottle flags are the
            // door's default (a fresh instance is never bottled). Obtaining a
            // copied CURSE runs the same Omamori gate the game's
            // ShowCardAndObtainEffect ctor spends.
            (void)add_card_to_master_deck(
                rs, static_cast<CardId>(picked.card_id), picked.upgrade);
            close_event_grid(es);
            es.screen = 1;
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        if (rs.master_deck_count > 0) {
            open_event_grid(es, EventGridKind::DUPLICATE);
        } else {
            es.screen = 1;  // defensive: an empty deck has nothing to copy
        }
        return EventDialogStatus::CONTINUE;
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kDuplicator = {
    &duplicator_enter,
    &duplicator_menu,
    &duplicator_choose,
};

}  // namespace

const EventDialogImpl* event_native_knowing_skull() noexcept {
    return &kKnowingSkull;
}

const EventDialogImpl* event_native_the_joust() noexcept {
    return &kTheJoust;
}

const EventDialogImpl* event_native_nloth() noexcept {
    return &kNloth;
}

const EventDialogImpl* event_native_designer() noexcept {
    return &kDesigner;
}

const EventDialogImpl* event_native_duplicator() noexcept {
    return &kDuplicator;
}

}  // namespace sts::engine
