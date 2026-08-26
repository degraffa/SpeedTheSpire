// The eight NON-COMBAT Act-2 event bodies (TheCity.initializeEventList,
// TheCity.java:185-198): Addict, Back to Basics, Beggar, Cursed Tome, Drug
// Dealer, Forgotten Altar, Ghosts, Nest. The five remaining Act-2 rows
// (Colosseum, Masked Bandits, The Library, The Mausoleum, Vampires) are
// S2.32's; selection, the per-act pool rebuild and the dialog plumbing are
// already done (event_framework.cpp, S2.13) and NOTHING here re-touches
// build_event_pool / build_shrine_pool / the membership masks.
//
// A15 AUDIT, per event, from the ascensionLevel reads in each class. Only four
// of the eight have an A15 branch at all, and two of the four are easy to get
// wrong:
//   Addict           NONE. The 85-gold price is a compile-time constant
//                    (Addict.java:24) and no ascension read exists in the file.
//   Back to Basics   NONE. No ascension read in the file.
//   Beggar           NONE. GOLD_COST is 75 at every ascension
//                    (Beggar.java:27); the ascension-sensitive thing about
//                    this event is its DRAW gate, which is getEvent's, not the
//                    body's.
//   Cursed Tome      finalDmg 10 -> 15 (CursedTome.java:58). The three page
//                    damages (1/2/3) and the stop-reading 3 do NOT move.
//   Drug Dealer      NONE. No ascension read in the file.
//   Forgotten Altar  hpLoss percent 0.25 -> 0.35 (ForgottenAltar.java:50).
//                    The +5 max HP does not move.
//   Ghosts           THE COUNT, not the cost: becomeGhost's `amount = 5; if
//                    (ascensionLevel >= 15) amount -= 2` (Ghosts.java:86-89).
//                    The max-HP price stays ceil(maxHealth * 0.5f) at every
//                    ascension -- the ctor's ascension read (:37-41) only picks
//                    which OPTION STRING is shown, and reading it as a price
//                    change is the trap.
//   Nest             goldGain 99 -> 50 (Nest.java:35). The Ritual Dagger
//                    branch's 6 damage does not move.
//
// RNG ATTRIBUTION -- read per event from the Java:
//   * Addict           relicRng, via returnRandomScreenlessRelic(
//                      returnRandomRelicTier()) on BOTH payout options
//                      (Addict.java:46, :57) -- one tier roll plus the pool
//                      pops. No other stream.
//   * Back to Basics   NO SEEDED STREAM AT ALL. upgradeStrikeAndDefends' two
//                      MathUtils.random calls (:97) position a VFX sprite and
//                      come from libGDX's global unseeded Random, which is not
//                      part of any save or replay -- the same class of call as
//                      every other ShowCardBrieflyEffect in the game.
//   * Beggar           none.
//   * Cursed Tome      miscRng, ONE random(size-1) draw in randomBook (:156),
//                      and only on the obtain branch.
//   * Drug Dealer      miscRng, one transformCard draw PER selected card
//                      (:112), i.e. exactly two on the transform branch.
//   * Forgotten Altar  none on the idol swap (Bloody Idol is a FIXED relic);
//                      the already-have-Bloody-Idol Circlet payout takes the
//                      ordinary acquire door, whose miscRng contract is
//                      acquire_relic's.
//   * Ghosts           none.
//   * Nest             none.
//
// Provenance (each read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * Addict                   events/city/Addict.java:17-79
//   * BackToBasics             events/city/BackToBasics.java:26-101
//   * Beggar                   events/city/Beggar.java:19-95
//   * CursedTome               events/city/CursedTome.java:23-164
//   * DrugDealer               events/city/DrugDealer.java:27-135
//   * ForgottenAltar           events/city/ForgottenAltar.java:27-116
//   * Ghosts                   events/city/Ghosts.java:16-97
//   * Nest                     events/city/Nest.java:17-84
//   * AbstractRelic.instantObtain      AbstractRelic.java:219-249
//   * AbstractPlayer.damage            AbstractPlayer.java:1387-1502
//   * AbstractCard.canUpgrade          AbstractCard.java:672-680
//   * GridCardSelectScreen.open (7-arg) GridCardSelectScreen.java:437-458
//   * GenericEventDialog.setDialogOption overloads
//                                      GenericEventDialog.java:178-234
//   * BloodyIdol / GoldenIdol          BloodyIdol.java:14-39, GoldenIdol.java:12-30

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <cstdint>

#include "../relics/relic_pickup.hpp"
#include "event_common.hpp"
#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

using events::decrease_max_hp;
using events::draw_event_relic;
using events::has_purgeable_card;
using events::increase_max_hp;
using events::mathutils_ceil;
using events::one_proceed_menu;

constexpr int16_t kNoPick = -1;

// getGroupWithoutBottledCards(getPurgeableCards()) -- the grid every event in
// this batch opens EXCEPT Drug Dealer's.
[[nodiscard]] bool has_purgeable_unbottled(const RunState& rs) noexcept {
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (master_card_purgeable_unbottled(rs.master_deck[i])) {
            return true;
        }
    }
    return false;
}

// The RAW getPurgeableCards() count (CardGroup.java:978-985), which is what
// DrugDealer's option-1 gate reads (:41).
[[nodiscard]] int purgeable_count(const RunState& rs) noexcept {
    int n = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        if (rest_card_purgeable(rs.master_deck[i])) {
            ++n;
        }
    }
    return n;
}

// `openMap()` reached from somewhere run_advance's return-value path cannot
// see. run_advance discards `choose`'s status on a GRID pick (it only honours
// FINISHED on a MENU pick), so a body that ends the event from inside a grid --
// Beggar is the batch's one such body -- installs the transition itself and
// reports TRANSITIONED.
EventDialogStatus finish_to_map(RunController& rc) noexcept {
    rc.event = EventDialogState{};
    rc.phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
    return EventDialogStatus::TRANSITIONED;
}

// --- Addict -----------------------------------------------------------------
// Screens: 0 OFFER, 1 DONE.

void addict_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void addict_menu(const RunController& rc, const EventDialogState& es,
                 EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 3;
        // BOTH ctor arms pass the SAME isDisabled expression, `gold < 85`
        // (:30, :32); they differ only in the string. So this is one greyed-out
        // button, not a second option -- and buttonEffect's own `if (gold < 85)
        // break` (:45) is the dead defensive twin of it.
        out.enabled[0] = rc.run.gold >= 85;
        out.enabled[1] = true;
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus addict_choose(RunController& rc, EventDialogState& es,
                                uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option >= 2) {
        // The `default` arm (:65-69): unlike pay and rob, LEAVE calls
        // this.openMap() at the press itself, so the rewritten single-button
        // page (screenNum = 1, :71) is installed BEHIND an already-open map
        // and is never clicked -- the same unreachable dressing as Tomb of
        // Lord Red Mask's leave (beyond_events.cpp). One click, FINISHED.
        // Modelling it as a page put the sim one screen -- and then one
        // whole floor -- behind every capture that declined the Addict
        // (both Act-2 event divergences of the s243_breadth campaigns).
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // :46-49, in Java statement order: the relic is ROLLED, then 85 gold
        // is lost, then the relic is obtained. The gold loss sits between the
        // two, so an onEquip that reads gold (none today) would see the
        // reduced purse.
        const RelicId id = draw_event_relic(rc.run, /*screenless=*/true);
        lose_gold(rc.run, 85);
        (void)acquire_relic(rc.run, rc.combat.misc_rng, id);
    } else if (option == 1) {
        // :56-60. The roll comes FIRST (:57); the Shame's
        // ShowCardAndObtainEffect ctor (:59) spends an already-owned Omamori
        // charge synchronously; the relic is obtained (:60); the queued effect
        // appends the curse afterwards. Same three-way ordering as Big Fish's
        // box: a newly rolled Omamori cannot eat this Shame, while a newly
        // rolled Darkstone Periapt does see it.
        const RelicId id = draw_event_relic(rc.run, /*screenless=*/true);
        const bool blocked = omamori_blocks_card_obtain(rc.run, CardId::SHAME);
        (void)acquire_relic(rc.run, rc.combat.misc_rng, id);
        if (!blocked) {
            (void)add_card_to_master_deck_after_omamori(rc.run, CardId::SHAME);
        }
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kAddict = {
    &addict_enter,
    &addict_menu,
    &addict_choose,
};

// --- Back to Basics ---------------------------------------------------------
// Screens: 0 INTRO, 1 COMPLETE.

// CardTags.STARTER_STRIKE / STARTER_DEFEND (BackToBasics.java:93) are carried
// by exactly one card per class (Strike_Red.java:35-36, Defend_Red.java:33-34;
// the blue/green/purple twins are out of this engine's scope), so the tag test
// is the two Ironclad starter ids. canUpgrade() (AbstractCard.java:672-680) is
// rest_card_upgradeable's predicate.
[[nodiscard]] bool is_starter_strike_or_defend(const CardInstance& c) noexcept {
    const CardId id = static_cast<CardId>(c.card_id);
    return id == CardId::STRIKE || id == CardId::DEFEND;
}

void back_to_basics_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void back_to_basics_menu(const RunController& /*rc*/,
                         const EventDialogState& es, EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        // Neither ctor option takes an isDisabled argument (:41-42): both
        // buttons are always live, and the empty-deck case is handled INSIDE
        // option 0 by simply not opening the grid (:70-73).
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus back_to_basics_choose(RunController& rc,
                                        EventDialogState& es, uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        (void)event_grid_remove_card(rc.run, es, option);
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // Elegance (:69-75). `screen = COMPLETE` happens unconditionally
        // (:82), so a deck with nothing purgeable lands straight on the
        // proceed page with no removal -- the same silent-advance shape
        // Golden Wing's purge prompt has.
        if (has_purgeable_unbottled(rc.run)) {
            open_event_grid(es, EventGridKind::PURGE);
        }
    } else {
        // Simplicity (:91-100). Every eligible card is upgraded, in master-deck
        // order; bottledCardUpgradeCheck (:96) re-points the bottle at the same
        // instance, which in this engine's index-addressed deck is a no-op.
        for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
            CardInstance& card = rc.run.master_deck[i];
            if (is_starter_strike_or_defend(card) &&
                rest_card_upgradeable(card)) {
                ++card.upgrade;
            }
        }
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kBackToBasics = {
    &back_to_basics_enter,
    &back_to_basics_menu,
    &back_to_basics_choose,
};

// --- Beggar -----------------------------------------------------------------
// Screens: 0 INTRO, 1 GAVE_MONEY, 2 LEAVE. THREE screens, because paying does
// not open the grid -- it opens a page whose single button opens the grid
// (:79-85), and the grid pick then calls openMap() from update() (:46-57).

constexpr uint8_t kBeggarIntro = 0;
constexpr uint8_t kBeggarGaveMoney = 1;
constexpr uint8_t kBeggarLeave = 2;

void beggar_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kBeggarIntro;
}

void beggar_menu(const RunController& rc, const EventDialogState& es,
                 EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == kBeggarIntro) {
        out.count = 2;
        // isDisabled = gold < 75 on BOTH ctor arms (:36, :38). getEvent already
        // refuses to offer the event under 75 gold (AbstractDungeon.java:
        // 1970-1973) and nothing in the dialog can spend gold first, so the
        // grey-out is unreachable in play -- written anyway, because the gate
        // and the button are two different tests and only one of them is this
        // file's.
        out.enabled[0] = rc.run.gold >= 75;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus beggar_choose(RunController& rc, EventDialogState& es,
                                uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        // update() (:49-56) removes the card and calls openMap() in the same
        // tick: there is no post-purge page.
        if (event_grid_remove_card(rc.run, es, option)) {
            return finish_to_map(rc);
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == kBeggarIntro) {
        if (option == 0) {
            lose_gold(rc.run, 75);
            es.screen = kBeggarGaveMoney;
        } else {
            es.screen = kBeggarLeave;
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == kBeggarGaveMoney) {
        // The grid is opened UNCONDITIONALLY (:80) -- no emptiness test, unlike
        // Back to Basics. A deck with nothing removable therefore strands the
        // Java on a grid it cannot satisfy (canCancel is false, :80); this port
        // falls through to the leave page instead rather than emitting a phase
        // with no legal action. The state needs a deck of nothing but
        // Ascender's Bane / bottled cards and 75 spent gold, so it is a
        // documented defensive deviation on a barely-reachable state, not a
        // rewrite of the offer.
        if (has_purgeable_unbottled(rc.run)) {
            open_event_grid(es, EventGridKind::PURGE);
        }
        es.screen = kBeggarLeave;
        return EventDialogStatus::CONTINUE;
    }
    return EventDialogStatus::FINISHED;
}

constexpr EventDialogImpl kBeggar = {
    &beggar_enter,
    &beggar_menu,
    &beggar_choose,
};

// --- Cursed Tome ------------------------------------------------------------
// Screens: 0 INTRO, 1 PAGE_1, 2 PAGE_2, 3 PAGE_3, 4 LAST_PAGE, 5 END.
// The damage on each page is taken when LEAVING it, so PAGE_1's continue costs
// 1, PAGE_2's 2 and PAGE_3's 3 (:85, :95, :105).

constexpr uint8_t kTomeIntro = 0;
constexpr uint8_t kTomePage1 = 1;
constexpr uint8_t kTomePage2 = 2;
constexpr uint8_t kTomePage3 = 3;
constexpr uint8_t kTomeLastPage = 4;
constexpr uint8_t kTomeEnd = 5;

[[nodiscard]] int tome_final_damage(const RunState& rs) noexcept {
    return rs.ascension >= 15 ? 15 : 10;  // :58
}

// randomBook (:142-157): the three books in SOURCE ORDER, each added only when
// not already owned, Circlet when all three are, then ONE inclusive
// miscRng.random(size - 1). The Circlet fallback is why this event can pay a
// relic the player already effectively has -- flagged by S2.02 for this task
// and confirmed here against the Java.
[[nodiscard]] RelicId tome_random_book(RunController& rc) noexcept {
    RelicId books[3];
    int n = 0;
    if (!run_has_relic(rc.run, RelicId::NECRONOMICON)) {
        books[n++] = RelicId::NECRONOMICON;
    }
    if (!run_has_relic(rc.run, RelicId::ENCHIRIDION)) {
        books[n++] = RelicId::ENCHIRIDION;
    }
    if (!run_has_relic(rc.run, RelicId::NILRYS_CODEX)) {
        books[n++] = RelicId::NILRYS_CODEX;
    }
    if (n == 0) {
        return RelicId::CIRCLET;  // no draw: the list is built, then indexed
    }
    return books[random(rc.combat.misc_rng, n - 1)];
}

void tome_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kTomeIntro;
}

void tome_menu(const RunController& /*rc*/, const EventDialogState& es,
               EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kTomeIntro || es.screen == kTomeLastPage) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus tome_choose(RunController& rc, EventDialogState& es,
                              uint8_t option) {
    switch (es.screen) {
        case kTomeIntro:
            es.screen = option == 0 ? kTomePage1 : kTomeEnd;
            return EventDialogStatus::CONTINUE;
        case kTomePage1:
        case kTomePage2:
        case kTomePage3: {
            // 1 / 2 / 3, all DamageInfo(null, n, HP_LOSS): a NULL owner, so
            // Torii's onAttacked never runs (AbstractPlayer.java:1427-1434
            // gates the whole onAttacked block on `info.owner != null`), while
            // Tungsten Rod's owner-independent onLoseHpLast still does.
            const int damage = static_cast<int>(es.screen);
            const bool alive =
                apply_event_damage(rc, damage, EventDamageOwner::NULL_SOURCE);
            if (!alive) {
                return EventDialogStatus::TRANSITIONED;
            }
            es.screen = static_cast<uint8_t>(es.screen + 1);
            return EventDialogStatus::CONTINUE;
        }
        case kTomeLastPage: {
            if (option != 0) {
                const bool alive = apply_event_damage(
                    rc, 3, EventDamageOwner::NULL_SOURCE);
                if (!alive) {
                    return EventDialogStatus::TRANSITIONED;
                }
                es.screen = kTomeEnd;
                return EventDialogStatus::CONTINUE;
            }
            const bool alive = apply_event_damage(rc, tome_final_damage(rc.run),
                                                  EventDamageOwner::NULL_SOURCE);
            // The Java's damage() sets isDead and builds a DeathScreen but
            // RETURNS, so randomBook still runs and still spends its miscRng
            // draw -- the Scrap Ooze / Shining Light precedent. What cannot
            // follow a death is the reward SCREEN: the run is over.
            const RelicId book = tome_random_book(rc);
            if (!alive) {
                return EventDialogStatus::TRANSITIONED;
            }
            // rewards.clear(); addRelicToRewards(r); phase = COMPLETE;
            // combatRewardScreen.open() (:158-161). The screen IS the exit --
            // the same shape Lab and The Woman in Blue use for potions.
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            (void)add_event_combat_relic_reward(rc.rewards, book);
            rc.event = EventDialogState{};
            rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
            return EventDialogStatus::TRANSITIONED;
        }
        default:
            return EventDialogStatus::FINISHED;
    }
}

constexpr EventDialogImpl kCursedTome = {
    &tome_enter,
    &tome_menu,
    &tome_choose,
};

// --- Drug Dealer ------------------------------------------------------------
// Screens: 0 OFFER, 1 DONE. scratch0 holds the FIRST transform pick's
// master-deck index while the grid waits for the second.

void drug_dealer_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
    es.scratch0 = kNoPick;
}

void drug_dealer_menu(const RunController& rc, const EventDialogState& es,
                      EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        out.count = 3;
        out.enabled[0] = true;
        // `getPurgeableCards().size() >= 2` (:41) -- the RAW group, bottled
        // cards included, matching the grid this option opens. Under two, the
        // ctor installs a permanently disabled button instead (:44).
        out.enabled[1] = purgeable_count(rc.run) >= 2;
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus drug_dealer_choose(RunController& rc, EventDialogState& es,
                                     uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        if (!event_grid_card_legal(rc.run, es, option)) {
            return EventDialogStatus::CONTINUE;
        }
        if (es.scratch0 == kNoPick) {
            es.scratch0 = static_cast<int16_t>(option);
            return EventDialogStatus::CONTINUE;  // selectedCards.size() == 1
        }
        const uint16_t first = static_cast<uint16_t>(es.scratch0);
        if (option == first) {
            return EventDialogStatus::CONTINUE;  // the same card, not a pair
        }
        // update() (:104-122) fires only at size() == 2 and then walks
        // selectedCards IN SELECTION ORDER: removeCard, transformCard(miscRng),
        // and a QUEUED ShowCardAndObtainEffect per card -- so both removals and
        // both draws precede both appends. Written that way here; the
        // interleaved spelling would give the same deck (appends are always at
        // the end) but this order is the one the Java can be read off.
        const CardId a =
            static_cast<CardId>(rc.run.master_deck[first].card_id);
        const CardId b = static_cast<CardId>(rc.run.master_deck[option].card_id);
        const uint16_t hi = std::max(first, static_cast<uint16_t>(option));
        const uint16_t lo = std::min(first, static_cast<uint16_t>(option));
        (void)remove_master_deck_card(rc.run, hi);
        (void)remove_master_deck_card(rc.run, lo);
        const CardId new_a = transform_card(rc.combat.misc_rng, a);
        const CardId new_b = transform_card(rc.combat.misc_rng, b);
        (void)add_card_to_master_deck(rc.run, new_a);
        (void)add_card_to_master_deck(rc.run, new_b);
        close_event_grid(es);
        es.scratch0 = kNoPick;
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        (void)add_card_to_master_deck(rc.run, CardId::JAX);
    } else if (option == 1) {
        open_event_grid(es, EventGridKind::TRANSFORMABLE_ANY);
        return EventDialogStatus::CONTINUE;  // screen advances at the 2nd pick
    } else if (option == 2) {
        // :73-79. hasRelic("MutagenicStrength") -- the one-word id, not the
        // display name -- redirects the payout to a Circlet.
        (void)acquire_relic(
            rc.run, rc.combat.misc_rng,
            run_has_relic(rc.run, RelicId::MUTAGENIC_STRENGTH)
                ? RelicId::CIRCLET
                : RelicId::MUTAGENIC_STRENGTH);
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kDrugDealer = {
    &drug_dealer_enter,
    &drug_dealer_menu,
    &drug_dealer_choose,
};

// --- Forgotten Altar --------------------------------------------------------
// Screens: 0 OFFER, 1 DONE. scratch0 is the ctor-frozen hpLoss (:50), computed
// from the ENTRY-time max HP -- which is why option 1's +5 does not enlarge it.

void forgotten_altar_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    const float percent = rc.run.ascension >= 15 ? 0.35f : 0.25f;
    es.scratch0 = static_cast<int16_t>(
        mathutils_round(static_cast<float>(rc.run.max_hp) * percent));
}

void forgotten_altar_menu(const RunController& rc, const EventDialogState& es,
                          EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 3;
        // isDisabled = !hasRelic("Golden Idol") on BOTH ctor arms (:46, :48).
        out.enabled[0] = run_has_relic(rc.run, RelicId::GOLDEN_IDOL);
        out.enabled[1] = true;
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus forgotten_altar_choose(RunController& rc,
                                         EventDialogState& es,
                                         uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // gainChalice (:99-115). TWO different payouts, and the Bloody Idol one
        // is not the "already have it" case one would guess: holding a Bloody
        // Idol pays a plain Circlet through the ordinary obtain door AND LEAVES
        // THE GOLDEN IDOL IN PLACE (:106-109) -- the swap only happens on the
        // else arm. There the Golden Idol's onUnequip runs (empty, GoldenIdol
        // has no override) and the Bloody Idol is seated IN ITS SLOT with
        // callOnEquip false, so relic ORDER is preserved (trap 8).
        if (run_has_relic(rc.run, RelicId::BLOODY_IDOL)) {
            (void)acquire_relic(rc.run, rc.combat.misc_rng, RelicId::CIRCLET);
        } else {
            (void)swap_relic_in_place(rc.run, RelicId::GOLDEN_IDOL,
                                      RelicId::BLOODY_IDOL);
        }
    } else if (option == 1) {
        // :74-75, in this order: the max HP (and its free 5 heal) lands FIRST,
        // then the damage. DamageInfo(null, hpLoss) is a NORMAL null-owner hit,
        // not HP_LOSS.
        increase_max_hp(rc.run, 5);
        const bool alive = apply_event_damage(rc, static_cast<int>(es.scratch0),
                                              EventDamageOwner::NULL_SOURCE);
        if (!alive) {
            return EventDialogStatus::TRANSITIONED;
        }
    } else if (option == 2) {
        (void)add_card_to_master_deck(rc.run, CardId::DECAY);
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kForgottenAltar = {
    &forgotten_altar_enter,
    &forgotten_altar_menu,
    &forgotten_altar_choose,
};

// --- Ghosts -----------------------------------------------------------------
// Screens: 0 OFFER, 1/2 DONE (the Java keeps two distinct done-screens, :61 and
// :69, which behave identically -- both openMap). scratch0 is the ctor-frozen
// hpLoss (:33-36).

void ghosts_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    int loss = mathutils_ceil(static_cast<float>(rc.run.max_hp) * 0.5f);
    if (loss >= static_cast<int>(rc.run.max_hp)) {
        loss = static_cast<int>(rc.run.max_hp) - 1;  // :34-36
    }
    es.scratch0 = static_cast<int16_t>(loss);
}

void ghosts_menu(const RunController& /*rc*/, const EventDialogState& es,
                 EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        // Neither ctor option takes an isDisabled argument (:38-42): the deal
        // is offered at any HP, which is exactly why hpLoss is clamped to
        // maxHealth - 1 rather than gated.
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus ghosts_choose(RunController& rc, EventDialogState& es,
                                uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        decrease_max_hp(rc.run, static_cast<int>(es.scratch0));
        // becomeGhost (:84-95): the A15 branch moves the COUNT, 5 -> 3, and
        // nothing else. Each copy is an ordinary ShowCardAndObtainEffect
        // append; Apparition is not a curse, so no Omamori charge is at risk.
        const int count = rc.run.ascension >= 15 ? 3 : 5;
        for (int i = 0; i < count; ++i) {
            (void)add_card_to_master_deck(rc.run, CardId::APPARITION);
        }
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kGhosts = {
    &ghosts_enter,
    &ghosts_menu,
    &ghosts_choose,
};

// --- Nest -------------------------------------------------------------------
// Screens: 0 INTRO (one button), 1 OFFER (two), 2 DONE.

void nest_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void nest_menu(const RunController& /*rc*/, const EventDialogState& es,
               EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 1) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus nest_choose(RunController& rc, EventDialogState& es,
                              uint8_t option) {
    if (es.screen == 0) {
        // :42-46. The Ritual Dagger button is APPENDED (index 1) and index 0 is
        // then REWRITTEN into the gold offer, so the pair below is gold-first.
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 1) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        gain_gold(rc.run, rc.run.ascension >= 15 ? 50 : 99);  // :35, :56
    } else {
        // :62-66: damage FIRST (a NORMAL null-owner 6, so Torii cannot see it
        // and Tungsten Rod can), then the queued card append.
        const bool alive =
            apply_event_damage(rc, 6, EventDamageOwner::NULL_SOURCE);
        (void)add_card_to_master_deck(rc.run, CardId::RITUAL_DAGGER);
        if (!alive) {
            return EventDialogStatus::TRANSITIONED;
        }
    }
    es.screen = 2;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kNest = {
    &nest_enter,
    &nest_menu,
    &nest_choose,
};

}  // namespace

const EventDialogImpl* event_native_addict() noexcept {
    return &kAddict;
}

const EventDialogImpl* event_native_back_to_basics() noexcept {
    return &kBackToBasics;
}

const EventDialogImpl* event_native_beggar() noexcept {
    return &kBeggar;
}

const EventDialogImpl* event_native_cursed_tome() noexcept {
    return &kCursedTome;
}

const EventDialogImpl* event_native_drug_dealer() noexcept {
    return &kDrugDealer;
}

const EventDialogImpl* event_native_forgotten_altar() noexcept {
    return &kForgottenAltar;
}

const EventDialogImpl* event_native_ghosts() noexcept {
    return &kGhosts;
}

const EventDialogImpl* event_native_nest() noexcept {
    return &kNest;
}

}  // namespace sts::engine
