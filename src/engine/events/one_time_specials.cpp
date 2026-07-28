// The Act-1-REACHABLE one-time special events
// (AbstractDungeon.initializeSpecialOneTimeEventList, AbstractDungeon.java:
// 1340-1358), filtered by AbstractDungeon.getShrine's per-key gates
// (AbstractDungeon.java:1886-1936) against `id.equals("Exordium")`.
//
// REACHABLE IN ACT 1 -- implemented here (eight):
//   Accursed Blacksmith, Bonfire Elementals, FaceTrader, Fountain of Cleansing,
//   Lab, NoteForYourself, WeMeetAgain, The Woman in Blue.
// EXCLUDED BY THAT FILTER (six), each with the gate that excludes it:
//   Designer       TheCity/TheBeyond AND gold >= 75      (:1894-1898)
//   Duplicator     TheCity/TheBeyond                     (:1899-1903)
//   Knowing Skull  TheCity AND currentHealth > 12        (:1909-1913)
//   N'loth         TheCity AND relics.size() >= 2        (:1914-1918)
//   SecretPortal   playtime >= 800s AND TheBeyond        (:1929-1933)
//   The Joust      TheCity AND gold >= 50                (:1919-1923)
// `build_shrine_pool` (event_framework.cpp) is the single authority for that
// filter and is what the gate-evidence tests exercise.
//
// RNG ATTRIBUTION -- read per event from the Java:
//   * Accursed Blacksmith  no stream (the upgrade is a grid pick; Warped Tongs
//                          is a FIXED relic, not a pool draw).
//   * Bonfire Elementals   no stream on the payout itself; the curse payout's
//                          Spirit Poop / Circlet is fixed. acquire_relic still
//                          carries the onEquip miscRng contract.
//   * FaceTrader           miscRng.randomLong for the face shuffle
//                          (FaceTrader.java:116); no relic-pool draw at all --
//                          the five faces are a hand-written list.
//   * Fountain of Cleansing  no stream.
//   * Lab                  potionRng, one flat PotionHelper.getRandomPotion
//                          draw per potion (Lab.java:51-55).
//   * NoteForYourself      no stream (the granted card comes from a player
//                          PREFERENCE, not a pool -- see the pin below).
//   * WeMeetAgain          miscRng three times, CONDITIONALLY: randomLong for
//                          the potion pick, random(50, ...) for the gold offer,
//                          randomLong for the card pick -- each skipped when
//                          its offer is unavailable (WeMeetAgain.java:47-89).
//                          The payout then spends relicRng.
//   * The Woman in Blue    potionRng, one flat draw per purchased potion.
//
// Provenance (each read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * AccursedBlacksmith    AccursedBlacksmith.java:35-105
//   * Bonfire               Bonfire.java:38-152
//   * FaceTrader            FaceTrader.java:36-122
//   * FountainOfCurseRemoval FountainOfCurseRemoval.java:31-86
//   * Lab                   Lab.java:32-66
//   * NoteForYourself       NoteForYourself.java:36-106
//   * WeMeetAgain           WeMeetAgain.java:45-140
//   * WomanInBlue           WomanInBlue.java:41-111
//   * AbstractDungeon.getShrine        AbstractDungeon.java:1882-1942
//   * PotionHelper.getRandomPotion     PotionHelper.java:169-172
//   * AbstractPlayer.getRandomPotion   AbstractPlayer.java:2313-2324
//   * AbstractPlayer.loseGold          AbstractPlayer.java:697-712

#include "sts/engine/event_framework.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include "../relics/relic_pickup.hpp"
#include "event_common.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/registry/event_table.hpp"

namespace sts::engine {

namespace {

using events::acquire_event_relic;
using events::has_purgeable_card;
using events::has_upgradable_card;
using events::heal;
using events::increase_max_hp;
// lose_gold resolves to sts::engine::lose_gold (relic_pickup.hpp), the single
// lose-gold door -- an events:: duplicate was removed at integration.
using events::mathutils_ceil;
using events::one_proceed_menu;
using sts::registry::EventCardRarity;
using sts::registry::event_card_rarity;

constexpr int16_t kNoOffer = -1;

// PotionHelper.getRandomPotion(): a FLAT draw over the whole 33-entry Ironclad
// potion list -- NOT AbstractDungeon.returnRandomPotion's 65/25/10 rarity gate
// with its rejection sampling. One potionRng draw, always. Expressed by the
// public sts::engine::get_random_potion (potions.hpp) -- a byte-equivalent
// file-local duplicate briefly lived here (two concurrent branches each wrote
// one and the union was ambiguous); it was removed at integration in favor of
// the shared pool-index -> PotionId mapping.

// RewardItem(AbstractPotion) rows. The relic/gold doors already exist
// (combat_rewards.hpp); potions had no event-side row builder because no
// earlier event handed one out through a reward screen.
bool add_potion_reward(RewardScreen& s, PotionId id) noexcept {
    if (s.count >= kRewardItemCap) {
        return false;
    }
    RunRewardItem& item = s.items[s.count];
    item = RunRewardItem{};
    item.kind = static_cast<uint8_t>(RewardItemKind::POTION);
    item.id = static_cast<uint16_t>(id);
    ++s.count;
    return true;
}

// `rewards.clear()` + N potion rows + `combatRewardScreen.open()`, then the
// room phase goes COMPLETE so EventRoom.update stops ticking the event
// (EventRoom.java:33-41): the reward screen IS the exit, and proceeding from it
// lands on the map. Lab, The Woman in Blue and the Wheel's relic result all
// share this shape.
EventDialogStatus open_potion_reward_screen(RunController& rc,
                                            int count) noexcept {
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    for (int i = 0; i < count; ++i) {
        (void)add_potion_reward(rc.rewards, get_random_potion(rc.run.potion_rng));
    }
    rc.event = EventDialogState{};
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    return EventDialogStatus::TRANSITIONED;
}

// --- Accursed Blacksmith ----------------------------------------------------

void blacksmith_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void blacksmith_menu(const RunController& rc, const EventDialogState& es,
                     EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 0) {
        out.count = 3;
        // Forge is greyed out with no upgradable card (:37-41).
        out.enabled[0] = has_upgradable_card(rc.run);
        out.enabled[1] = true;
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus blacksmith_choose(RunController& rc, EventDialogState& es,
                                    uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        (void)event_grid_upgrade_card(rc.run, es, option);
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    es.screen = 1;
    if (option == 0) {
        if (has_upgradable_card(rc.run)) {
            open_event_grid(es, EventGridKind::UPGRADE);
        }
    } else if (option == 1) {
        // Rummage (:81-89): the Pain curse is obtained FIRST, then
        // spawnRelicAndObtain(new WarpedTongs()) -- a fixed relic, no pool
        // draw. Warped Tongs' own upgrade-a-random-card body remains deferred
        // (UpgradeRandomCardAction consumes shuffleRng and no opcode expresses
        // it); OWNING the relic is what this event grants, and that half is
        // live.
        (void)add_card_to_master_deck(rc.run, CardId::PAIN);
        (void)acquire_relic(rc.run, rc.combat.misc_rng, RelicId::WARPED_TONGS);
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kAccursedBlacksmith = {
    &blacksmith_enter,
    &blacksmith_menu,
    &blacksmith_choose,
};

// --- Bonfire Elementals -----------------------------------------------------
// Screens: 0 INTRO, 1 CHOOSE (offer a card), 2 COMPLETE.

void bonfire_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void bonfire_menu(const RunController& /*rc*/, const EventDialogState& es,
                  EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    one_proceed_menu(out);
}

// setReward (Bonfire.java:117-152), keyed on the OFFERED card's rarity. It runs
// BEFORE masterDeck.removeCard (:82-84), which matters for Parasite: the curse
// payout lands first and the -3 max HP follows.
void bonfire_pay_reward(RunController& rc, EventCardRarity rarity) noexcept {
    switch (rarity) {
        case EventCardRarity::CURSE:
            (void)acquire_relic(
                rc.run, rc.combat.misc_rng,
                run_has_relic(rc.run, RelicId::SPIRIT_POOP)
                    ? RelicId::CIRCLET
                    : RelicId::SPIRIT_POOP);
            break;
        case EventCardRarity::BASIC:
            break;  // "you offered garbage" -- no payout
        case EventCardRarity::COMMON:
        case EventCardRarity::SPECIAL:
            heal(rc.run, 5);
            break;
        case EventCardRarity::UNCOMMON:
            heal(rc.run, rc.run.max_hp);
            break;
        case EventCardRarity::RARE:
            // increaseMaxHp(10, false) still heals 10 (AbstractCreature.java:
            // 199-209), and the full heal follows it.
            increase_max_hp(rc.run, 10);
            heal(rc.run, rc.run.max_hp);
            break;
        case EventCardRarity::NONE:
        default:
            break;
    }
}

EventDialogStatus bonfire_choose(RunController& rc, EventDialogState& es,
                                 uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        if (!event_grid_card_legal(rc.run, es, option)) {
            return EventDialogStatus::CONTINUE;
        }
        const EventCardRarity rarity = event_card_rarity(
            static_cast<CardId>(rc.run.master_deck[option].card_id));
        bonfire_pay_reward(rc, rarity);
        (void)event_grid_remove_card(rc.run, es, option);
        es.screen = 2;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == 0) {
        // INTRO ignores which button was pressed (:94-98).
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 1) {
        return EventDialogStatus::FINISHED;
    }
    if (has_purgeable_card(rc.run)) {
        open_event_grid(es, EventGridKind::PURGE);
    } else {
        es.screen = 2;  // the "nothing to offer" page (:106-108)
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kBonfireElementals = {
    &bonfire_enter,
    &bonfire_menu,
    &bonfire_choose,
};

// --- FaceTrader -------------------------------------------------------------
// Screens: 0 INTRO, 1 MAIN (touch / trade / leave), 2 RESULT.
// scratch0 = the gold reward, scratch1 = the damage, both frozen by the
// constructor from the entry-time max HP (FaceTrader.java:36-44).

// getRandomFace (:96-118): a hand-written list in this exact order, filtered to
// un-owned faces, Circlet when all five are held, then ONE
// miscRng.randomLong-seeded JDK shuffle -- consumed even for a one-entry list,
// because the Random is constructed regardless.
constexpr std::array<RelicId, 5> kFaceRelics = {
    RelicId::CULTIST_MASK, RelicId::FACE_OF_CLERIC, RelicId::GREMLIN_MASK,
    RelicId::NLOTHS_MASK,  RelicId::SSSERPENT_HEAD,
};

void face_trader_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    es.scratch0 = static_cast<int16_t>(rc.run.ascension >= 15 ? 50 : 75);
    es.scratch1 = static_cast<int16_t>(
        std::max(1, static_cast<int>(rc.run.max_hp) / 10));
}

void face_trader_menu(const RunController& /*rc*/, const EventDialogState& es,
                      EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 1) {
        out.count = 3;
        out.enabled[0] = true;
        out.enabled[1] = true;
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus face_trader_choose(RunController& rc, EventDialogState& es,
                                     uint8_t option) {
    if (es.screen == 0) {
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 1) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // Touch (:63-70): gold BEFORE the damage, and DamageInfo(null, n) is a
        // null-owner NORMAL hit.
        gain_gold(rc.run, es.scratch0);
        (void)apply_event_damage(rc, es.scratch1,
                                 EventDamageOwner::NULL_SOURCE);
    } else if (option == 1) {
        std::array<RelicId, kFaceRelics.size()> ids{};
        int count = 0;
        for (RelicId id : kFaceRelics) {
            if (!run_has_relic(rc.run, id)) {
                ids[static_cast<std::size_t>(count++)] = id;
            }
        }
        if (count == 0) {
            ids[0] = RelicId::CIRCLET;
            count = 1;
        }
        JdkRandom jdk(random_long(rc.combat.misc_rng));
        jdk_shuffle(std::span<RelicId>(ids.data(),
                                       static_cast<std::size_t>(count)),
                    jdk);
        (void)acquire_relic(rc.run, rc.combat.misc_rng, ids[0]);
    }
    es.screen = 2;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kFaceTrader = {
    &face_trader_enter,
    &face_trader_menu,
    &face_trader_choose,
};

// --- Fountain of Cleansing --------------------------------------------------

void fountain_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void fountain_menu(const RunController& /*rc*/, const EventDialogState& es,
                   EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus fountain_choose(RunController& rc, EventDialogState& es,
                                  uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // The Java walks the master deck BACKWARDS (:54-59) so each removal
        // leaves the not-yet-visited prefix in place. The exclusion list is
        // AscendersBane / CurseOfTheBell / Necronomicurse plus inBottleFlame
        // and inBottleLightning (:55 -- and NOT inBottleTornado, a game
        // oddity). Bottling is live now (run_deck.hpp), but the two bottle
        // clauses stay unwritten because they are structurally dead: this
        // walk only visits CURSE-type cards, and no bottle can hold one (the
        // grids are ATTACK/SKILL/POWER type-filtered). rest_card_purgeable
        // therefore remains exactly this predicate.
        for (int i = static_cast<int>(rc.run.master_deck_count) - 1; i >= 0;
             --i) {
            const auto index = static_cast<uint16_t>(i);
            const CardDef* def =
                card_def(static_cast<CardId>(rc.run.master_deck[index].card_id));
            if (def == nullptr || def->type != CardType::CURSE ||
                !rest_card_purgeable(rc.run.master_deck[index])) {
                continue;
            }
            (void)remove_master_deck_card(rc.run, index);
        }
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kFountainOfCleansing = {
    &fountain_enter,
    &fountain_menu,
    &fountain_choose,
};

// --- Lab --------------------------------------------------------------------

void lab_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void lab_menu(const RunController& /*rc*/, const EventDialogState& es,
              EventDialogMenu& out) {
    (void)es;
    one_proceed_menu(out);
}

EventDialogStatus lab_choose(RunController& rc, EventDialogState& es,
                             uint8_t /*option*/) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    // Two potions at A15+, three below (Lab.java:51-55).
    return open_potion_reward_screen(rc, rc.run.ascension >= 15 ? 2 : 3);
}

constexpr EventDialogImpl kLab = {
    &lab_enter,
    &lab_menu,
    &lab_choose,
};

// --- NoteForYourself --------------------------------------------------------
// Screens: 0 INTRO, 1 CHOOSE (take / leave), 2 COMPLETE.
//
// PROFILE PIN. initializeObtainCard (:97-106) reads the PLAYER PROFILE
// preferences NOTE_CARD (default "Iron Wave") and NOTE_UPGRADE (default 0) --
// cross-RUN state written by a previous run's note. The engine already pins the
// frozen audited reference profile for the sibling read that decides whether
// this event exists at all (isNoteForYourselfAvailable's ASCENSION_LEVEL,
// event_framework.hpp:171-182); the same pin is taken here at the game's own
// documented defaults. It is the ONLY behaviour in this batch that a live
// capture could contradict without the Java being wrong, so the Deferred
// obligations table carries it for the oracle campaign.
constexpr CardId kNoteCard = CardId::IRON_WAVE;
constexpr uint8_t kNoteUpgrade = 0;

void note_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void note_menu(const RunController& /*rc*/, const EventDialogState& es,
               EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        return;
    }
    if (es.screen == 1) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus note_choose(RunController& rc, EventDialogState& es,
                              uint8_t option) {
    if (es.grid_kind != static_cast<uint8_t>(EventGridKind::NONE)) {
        (void)event_grid_remove_card(rc.run, es, option);
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == 0) {
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 1) {
        return EventDialogStatus::FINISHED;
    }
    es.screen = 2;
    if (option == 0) {
        // masterDeck.addToTop AFTER a hand-rolled onObtainCard pass and BEFORE
        // onMasterDeckChange (:57-63) -- not ShowCardAndObtainEffect, so no
        // Omamori door (and the granted card is never a curse). The give-away
        // grid then opens over the deck INCLUDING the card just taken (:65).
        (void)add_card_to_master_deck_top(rc.run, kNoteCard, kNoteUpgrade);
        if (has_purgeable_card(rc.run)) {
            open_event_grid(es, EventGridKind::PURGE);
        }
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kNoteForYourself = {
    &note_enter,
    &note_menu,
    &note_choose,
};

// --- WeMeetAgain ------------------------------------------------------------
// scratch0 = the offered potion's SLOT (kNoOffer when none is held),
// scratch1 = the gold offer (0 when the purse is under 50),
// scratch2 = the offered card's master-deck INDEX (kNoOffer when none).

[[nodiscard]] bool card_is_offerable(const CardInstance& card) noexcept {
    // rarity != BASIC && type != CURSE (WeMeetAgain.java:70-73).
    const CardDef* def = card_def(static_cast<CardId>(card.card_id));
    if (def == nullptr || def->type == CardType::CURSE) {
        return false;
    }
    return event_card_rarity(static_cast<CardId>(card.card_id)) !=
           EventCardRarity::BASIC;
}

void we_meet_again_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;

    // 1. getRandomPotion (AbstractPlayer.java:2313-2324): the held potions in
    //    SLOT order, one randomLong-seeded shuffle, first entry -- and NO draw
    //    at all when nothing is held.
    std::array<uint8_t, kPotionCap> slots{};
    int held = 0;
    for (uint8_t i = 0; i < kPotionCap; ++i) {
        if (rc.run.potions[i] != static_cast<uint16_t>(PotionId::NONE)) {
            slots[static_cast<std::size_t>(held++)] = i;
        }
    }
    if (held == 0) {
        es.scratch0 = kNoOffer;
    } else {
        JdkRandom jdk(random_long(rc.combat.misc_rng));
        jdk_shuffle(
            std::span<uint8_t>(slots.data(), static_cast<std::size_t>(held)),
            jdk);
        es.scratch0 = static_cast<int16_t>(slots[0]);
    }

    // 2. getGoldAmount (:81-89): 0 and NO draw under 50 gold; otherwise one
    //    inclusive random(50, min(gold, 150)) -- the >150 branch caps at 150.
    if (rc.run.gold < 50) {
        es.scratch1 = 0;
    } else {
        const int high = rc.run.gold > 150 ? 150 : rc.run.gold;
        es.scratch1 =
            static_cast<int16_t>(random(rc.combat.misc_rng, 50, high));
    }

    // 3. getRandomNonBasicCard (:68-79): same shuffle-and-take-first shape over
    //    the filtered master deck, again with no draw on an empty list.
    std::array<uint16_t, kMasterDeckCap> deck{};
    int offerable = 0;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        if (card_is_offerable(rc.run.master_deck[i])) {
            deck[static_cast<std::size_t>(offerable++)] = i;
        }
    }
    if (offerable == 0) {
        es.scratch2 = kNoOffer;
    } else {
        JdkRandom jdk(random_long(rc.combat.misc_rng));
        jdk_shuffle(std::span<uint16_t>(deck.data(),
                                        static_cast<std::size_t>(offerable)),
                    jdk);
        es.scratch2 = static_cast<int16_t>(deck[0]);
    }
}

void we_meet_again_menu(const RunController& /*rc*/, const EventDialogState& es,
                        EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 4;
        out.enabled[0] = es.scratch0 != kNoOffer;
        out.enabled[1] = es.scratch1 != 0;
        out.enabled[2] = es.scratch2 != kNoOffer;
        out.enabled[3] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus we_meet_again_choose(RunController& rc, EventDialogState& es,
                                       uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    bool pay = false;
    if (option == 0 && es.scratch0 != kNoOffer) {
        rc.run.potions[static_cast<uint8_t>(es.scratch0)] =
            static_cast<uint16_t>(PotionId::NONE);
        pay = true;
    } else if (option == 1 && es.scratch1 != 0) {
        lose_gold(rc.run, es.scratch1);
        pay = true;
    } else if (option == 2 && es.scratch2 != kNoOffer) {
        (void)remove_master_deck_card(rc.run,
                                      static_cast<uint16_t>(es.scratch2));
        pay = true;
    }
    if (pay) {
        // relicReward (:137-140): returnRandomScreenlessRelic(
        // returnRandomRelicTier()) -- relicRng, then spawnRelicAndObtain.
        acquire_event_relic(rc, /*screenless=*/true);
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kWeMeetAgain = {
    &we_meet_again_enter,
    &we_meet_again_menu,
    &we_meet_again_choose,
};

// --- The Woman in Blue ------------------------------------------------------

void woman_in_blue_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void woman_in_blue_menu(const RunController& /*rc*/, const EventDialogState& es,
                        EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        // All four buttons are always live: getShrine only offers this event at
        // >= 50 gold (AbstractDungeon.java:1924-1928), which already covers the
        // 40-gold top price, and nothing in the dialog can spend gold first.
        out.count = 4;
        for (int i = 0; i < 4; ++i) {
            out.enabled[i] = true;
        }
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus woman_in_blue_choose(RunController& rc, EventDialogState& es,
                                       uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option <= 2) {
        static constexpr int kCost[3] = {20, 30, 40};
        lose_gold(rc.run, kCost[option]);
        return open_potion_reward_screen(rc, option + 1);
    }
    if (rc.run.ascension >= 15) {
        // MathUtils.ceil(maxHealth * 0.05f), HP_LOSS with a null owner
        // (:96-98).
        (void)apply_event_damage(
            rc, mathutils_ceil(static_cast<float>(rc.run.max_hp) * 0.05f),
            EventDamageOwner::NULL_SOURCE);
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kTheWomanInBlue = {
    &woman_in_blue_enter,
    &woman_in_blue_menu,
    &woman_in_blue_choose,
};

}  // namespace

const EventDialogImpl* event_native_accursed_blacksmith() noexcept {
    return &kAccursedBlacksmith;
}

const EventDialogImpl* event_native_bonfire_elementals() noexcept {
    return &kBonfireElementals;
}

const EventDialogImpl* event_native_face_trader() noexcept {
    return &kFaceTrader;
}

const EventDialogImpl* event_native_fountain_of_cleansing() noexcept {
    return &kFountainOfCleansing;
}

const EventDialogImpl* event_native_lab() noexcept {
    return &kLab;
}

const EventDialogImpl* event_native_note_for_yourself() noexcept {
    return &kNoteForYourself;
}

const EventDialogImpl* event_native_we_meet_again() noexcept {
    return &kWeMeetAgain;
}

const EventDialogImpl* event_native_the_woman_in_blue() noexcept {
    return &kTheWomanInBlue;
}

}  // namespace sts::engine
