// The five Act-2 eventList bodies S2.32 owns: The Library, The Mausoleum,
// Vampires, and the two combat embeds -- the Colosseum (the game's ONE
// two-fight sequence, and its ONE rewardAllowed=false / reopen() combat) and
// Masked Bandits. Selection and dialog plumbing live in event_framework.cpp.
//
// Provenance (each read in full from D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * TheLibrary.java (118 lines)    -- ctor :37-42, update :44-53,
//     buttonEffect :55-107, getBook :109-115 (unseeded flavour)
//   * TheMausoleum.java (99 lines)   -- ctor :37-41, buttonEffect :55-96
//   * Vampires.java (117 lines)      -- ctor :38-53, buttonEffect :55-99,
//     replaceAttacks :101-114
//   * Colosseum.java (113 lines)     -- ctor :30-33, buttonEffect :35-93,
//     reopen :99-110
//   * MaskedBandits.java (126 lines) -- ctor :37-44, buttonEffect :54-110,
//     stealGold :112-123
//   * AbstractRoom battle-over block AbstractRoom.java:277-357 (dropReward /
//     addPotionToRewards run even with rewardAllowed false; the screen open
//     does not), dropReward :454-455 (EMPTY base body -- an EventRoom drops
//     nothing), addPotionToRewards :580-608
//   * EventRoom.update              EventRoom.java:33-42 (the reopen() call)
//   * AbstractImageEvent.enterCombatFromImage / enterImageFromCombat
//                                    AbstractImageEvent.java:76-102
//   * AbstractPlayer.preBattlePrep   (drawPile.initializeDeck's ONE
//     shuffleRng.randomLong -- CardGroup.java:929-931 via shuffle(Random) --
//     then usePreBattleAction and applyPreCombatLogic)
//   * AbstractDungeon.resetPlayer    AbstractDungeon.java (transient only)
//
// MASKED BANDITS' CTOR-TIME MONSTERS -- A RECORDED, COUNTER-ONLY DEVIATION.
// The event's constructor installs getEncounter("Masked Bandits") on the room
// at EVENT ENTRY (MaskedBandits.java:43), so the game draws the trio's three
// monster_hp_rng ctor rolls the moment the dialog opens; this engine draws
// them inside enter_event_combat, at the FIGHT button. The VALUES are
// identical -- same stream, same counter start, and nothing else draws
// monster_hp_rng on an event floor -- so the fight path is byte-equal. The
// difference is visible only on the PAY path (the game's floor ends with the
// counter 3 higher than the sim's) and only to a counter comparator: the
// streams are floor-reseeded at the next transition, so no later draw moves.
// Recorded here rather than plumbed through an HP-override path; the S2.43
// capture campaign's differ is the one consumer that could ever see it.

#include "sts/engine/event_framework.hpp"

#include <cstdint>

#include "../relics/relic_pickup.hpp"  // gain_gold / lose_gold doors
#include "event_common.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/interp.hpp"       // mathutils_round
#include "sts/engine/relic_hooks.hpp"  // player_relics / at_pre_battle pass
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

using events::decrease_max_hp;
using events::event_relic_context;
using events::heal;
using events::mathutils_ceil;
using events::one_proceed_menu;

// --- The Library -------------------------------------------------------------
// Screens: 0 intro (Read / Sleep), 1 the twenty-card board (one option per
// slot), 2 done. scratch0 = the displayed Sleep heal amount, frozen at entry
// exactly as the ctor freezes healAmt (:39) -- a Read-path max-HP change could
// otherwise move it.

constexpr uint8_t kLibraryIntro = 0;
constexpr uint8_t kLibraryBoard = 1;
constexpr uint8_t kLibraryDone = 2;
constexpr int kLibraryBoardSize = 20;  // TheLibrary.java:67
static_assert(kLibraryBoardSize <= kEventBoardCap &&
                  kLibraryBoardSize <= kEventOptionCap,
              "the Library board is why both caps are 20");

void library_enter(RunController& rc, EventDialogState& es) {
    es.screen = kLibraryIntro;
    // healAmt = MathUtils.round(maxHealth * (A15+ ? 0.2f : 0.33f)) (:39).
    const float percent = rc.run.ascension >= 15 ? 0.2f : 0.33f;
    es.scratch0 = static_cast<int16_t>(
        mathutils_round(static_cast<float>(rc.run.max_hp) * percent));
}

void library_menu(const RunController& /*rc*/, const EventDialogState& es,
                  EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kLibraryIntro) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    if (es.screen == kLibraryBoard) {
        // The MANDATORY 1-pick grid (open(group, 1, ..., false), :91): one
        // option per rolled card, no cancel.
        out.count = kLibraryBoardSize;
        for (int i = 0; i < kLibraryBoardSize; ++i) {
            out.enabled[i] = true;
        }
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus library_choose(RunController& rc, EventDialogState& es,
                                 uint8_t option) {
    RunState& rs = rc.run;
    if (es.screen == kLibraryBoard) {
        if (option >= kLibraryBoardSize) {
            return EventDialogStatus::CONTINUE;
        }
        // The picked copy rides ShowCardAndObtainEffect (:48-50): makeCopy()
        // resets the preview, and the obtain door's onObtainCard pass is
        // where an egg upgrade would land -- the M&K precedent, so the board
        // deliberately stores upgrade 0 and skips onPreviewObtainCard.
        (void)add_card_to_master_deck(
            rs, static_cast<CardId>(es.board[option].card_id));
        es.screen = kLibraryDone;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != kLibraryIntro) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // READ (:60-92): twenty unique-by-cardID cards. Each ATTEMPT is one
        // rollRarity -- cardRng.random(99) + cardBlizzRandomizer against the
        // EventRoom's 3/37 thresholds WITH the alternation pass (Nloth's Gift
        // x3) and NO blizzard mutation (that lives only in getRewardCards) --
        // then one cardRng pool index; a duplicate cardID re-rolls BOTH
        // (:70-78). The while loop terminates because every pool is wider
        // than the 20 slots.
        for (int i = 0; i < kLibraryBoardSize; ++i) {
            CardId id;
            bool dupe;
            do {
                const int roll =
                    static_cast<int>(random(rs.card_rng, 99)) +
                    static_cast<int>(rs.card_blizz_randomizer);
                const RewardCardRarity rarity =
                    reward_card_rarity_with_relics(rs, roll, RoomType::Event);
                id = draw_card_from_pool(rs.card_rng, rarity);
                dupe = false;
                for (int j = 0; j < i; ++j) {
                    dupe = dupe ||
                           es.board[j].card_id == static_cast<uint16_t>(id);
                }
            } while (dupe);
            es.board[i].card_id = static_cast<uint16_t>(id);
            es.board[i].upgrade = 0;
            es.board[i].taken = 0;
        }
        // getBook (:109-115) picks flavour text with UNSEEDED MathUtils: no
        // stream, nothing modelled.
        es.screen = kLibraryBoard;
        return EventDialogStatus::CONTINUE;
    }
    // SLEEP (:95-97): heal the frozen amount.
    heal(rs, es.scratch0);
    es.screen = kLibraryDone;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kTheLibrary = {
    &library_enter,
    &library_menu,
    &library_choose,
};

// --- The Mausoleum -----------------------------------------------------------
// Screens: 0 offer, 1 done.

void mausoleum_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void mausoleum_menu(const RunController& /*rc*/, const EventDialogState& es,
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

EventDialogStatus mausoleum_choose(RunController& rc, EventDialogState& es,
                                   uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // OPEN (:60-80). The miscRng.randomBoolean() is drawn FIRST and drawn
        // ALWAYS -- at A15+ the result is then OVERWRITTEN true (:61-64), so
        // the counter moves identically on both tiers.
        bool cursed = random_boolean(rc.combat.misc_rng);
        if (rc.run.ascension >= 15) {
            cursed = true;
        }
        if (cursed) {
            // ShowCardAndObtainEffect(new Writhe()) is CONSTRUCTED before the
            // relic is obtained (:67 vs :73-74), and its ctor spends an
            // already-owned Omamori charge synchronously -- the Big Fish
            // precedent: a newly rolled Omamori (impossible from a screenless
            // relic draw, but the order is the spec) could not eat this
            // Writhe, while Darkstone Periapt does see it.
            const bool blocked =
                omamori_blocks_card_obtain(rc.run, CardId::WRITHE);
            events::acquire_event_relic(rc, /*screenless=*/true);
            if (!blocked) {
                (void)add_card_to_master_deck_after_omamori(rc.run,
                                                            CardId::WRITHE);
            }
        } else {
            events::acquire_event_relic(rc, /*screenless=*/true);
        }
    }
    // option 1 leaves with nothing (:82-85); both arms land on the same page.
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kTheMausoleum = {
    &mausoleum_enter,
    &mausoleum_menu,
    &mausoleum_choose,
};

// --- Vampires ------------------------------------------------------------------
// Screens: 0 offer, 1 done. scratch0 = the displayed max-HP cost, frozen at
// entry as the ctor freezes maxHpLoss (:41-44). The Blood Vial option exists
// only while the vial is held (:48-51); with it absent the buttons are
// [accept, refuse], with it present [accept, vial, refuse] -- the Java's
// `case 1: if (!hasVial) break;` fall-through to refuse is the same mapping.

void vampires_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    int loss = mathutils_ceil(static_cast<float>(rc.run.max_hp) * 0.3f);
    if (loss >= static_cast<int>(rc.run.max_hp)) {
        loss = static_cast<int>(rc.run.max_hp) - 1;  // :42-44
    }
    es.scratch0 = static_cast<int16_t>(loss);
}

void vampires_menu(const RunController& rc, const EventDialogState& es,
                   EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        const bool has_vial = run_has_relic(rc.run, RelicId::BLOOD_VIAL);
        out.count = has_vial ? 3 : 2;
        for (uint8_t i = 0; i < out.count; ++i) {
            out.enabled[i] = true;
        }
        return;
    }
    one_proceed_menu(out);
}

// replaceAttacks (:101-114): every STARTER_STRIKE-tagged master-deck card is
// removed BACK TO FRONT, then five Bites are obtained. In the Ironclad scope
// the STARTER_STRIKE holders are exactly the Strike_R instances at any
// upgrade (Strike_Red.java:35-36 is the only tags.add(STARTER_STRIKE) among
// registered cards; Pommel/Twin/Wild/Perfected Strike carry only STRIKE).
void vampires_replace_attacks(RunState& rs) noexcept {
    for (int i = static_cast<int>(rs.master_deck_count) - 1; i >= 0; --i) {
        if (static_cast<CardId>(rs.master_deck[i].card_id) == CardId::STRIKE) {
            (void)remove_master_deck_card(rs, static_cast<uint16_t>(i));
        }
    }
    for (int i = 0; i < 5; ++i) {
        // ShowCardAndObtainEffect per Bite (:109-113): the obtain door's
        // onObtainCard pass is the effect's fan-out (Bite is no curse, so
        // Omamori never bites back).
        (void)add_card_to_master_deck(rs, CardId::BITE);
    }
}

EventDialogStatus vampires_choose(RunController& rc, EventDialogState& es,
                                  uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    const bool has_vial = run_has_relic(rc.run, RelicId::BLOOD_VIAL);
    if (option == 0) {
        // ACCEPT (:60-69): decreaseMaxHealth then the deck edit. NO ascension
        // branch anywhere in the file.
        decrease_max_hp(rc.run, es.scratch0);
        vampires_replace_attacks(rc.run);
    } else if (option == 1 && has_vial) {
        // OFFER THE VIAL (:71-82): the relic pays instead of the max HP.
        (void)lose_relic(rc.run, RelicId::BLOOD_VIAL);
        vampires_replace_attacks(rc.run);
    }
    // REFUSE (the fall-through, :84-88) changes nothing.
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kVampires = {
    &vampires_enter,
    &vampires_menu,
    &vampires_choose,
};

// --- The Colosseum -------------------------------------------------------------
// Screens: 0 INTRO, 1 FIGHT, 2 POST_COMBAT, 3 LEAVE -- the Java's CurScreen in
// the same order (:28, :37-92). The two-fight flow:
//   FIGHT button   -> screen = POST_COMBAT (set BEFORE the fight, :51), the
//                     room's rewards cleared, rewardAllowed = FALSE (:55),
//                     enter "Colosseum Slavers". rc.event is deliberately
//                     KEPT: finish_combat_after_action's reopen edge keys on
//                     it (event_combat_reopens below).
//   any survivor   -> event_combat_reopen (the header contract): the unopened
//     outcome         battle-over draws, the reopen shuffle draw + atPreBattle
//                     pass, rewards cleared, back to EVENT_DIALOG here.
//   POST_COMBAT    -> option 0 flees to the map ("Fled From Nobs", :81-82);
//                     option 1 sets screen = LEAVE, pre-stocks the reward
//                     screen (RARE relic, UNCOMMON relic, 100 gold, :70-74),
//                     sets eliteTrigger (:75) and enters "Colosseum Nobs" --
//                     with rc.event CLEARED, because reopen() is a no-op at
//                     LEAVE (:101) and the ordinary event-combat reward flow
//                     (potion roll + card on top of the pre-stock) takes over.

constexpr uint8_t kColosseumIntro = 0;
constexpr uint8_t kColosseumFight = 1;
constexpr uint8_t kColosseumPostCombat = 2;
constexpr uint8_t kColosseumLeave = 3;

void colosseum_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kColosseumIntro;
}

void colosseum_menu(const RunController& /*rc*/, const EventDialogState& es,
                    EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kColosseumPostCombat) {
        out.count = 2;   // COWARDICE / VICTORY (:107-108)
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    // INTRO and FIGHT are single-continue pages (:32, :42); LEAVE never shows
    // a dialog (the Nobs fight ends on its reward screen).
    one_proceed_menu(out);
}

EventDialogStatus colosseum_choose(RunController& rc, EventDialogState& es,
                                   uint8_t option) {
    switch (es.screen) {
        case kColosseumIntro:
            es.screen = kColosseumFight;  // :38-46, text only
            return EventDialogStatus::CONTINUE;
        case kColosseumFight:
            // :48-62. The screen moves FIRST (:51), which is what the reopen
            // edge later reads; rewards.clear() (:54) + rewardAllowed = false
            // (:55) mean the Slavers pay NOTHING -- the resume path consumes
            // the battle-over draws without opening a screen.
            es.screen = kColosseumPostCombat;
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            (void)enter_event_combat(rc, "Colosseum Slavers");
            return EventDialogStatus::TRANSITIONED;
        case kColosseumPostCombat: {
            if (option != 1) {
                return EventDialogStatus::FINISHED;  // Fled From Nobs (:81-82)
            }
            es.screen = kColosseumLeave;
            rc.rewards = RewardScreen{};
            rc.rewards.open_card_item = kNoOpenCardReward;
            // addRelicToRewards(RARE) then (UNCOMMON) (:72-73): two
            // returnRandomRelic pool pops AT BUTTON TIME, both landing as
            // claimable screen items; then the flat 100 gold (:74). The
            // monster ctor draws land inside enter_event_combat -- a
            // different stream, so the order against the relic pops is
            // unobservable.
            const RelicSpawnContext ctx = event_relic_context(rc.run);
            (void)add_event_combat_relic_reward(
                rc.rewards,
                return_random_relic_key(rc.run, RelicTier::RARE, ctx));
            (void)add_event_combat_relic_reward(
                rc.rewards,
                return_random_relic_key(rc.run, RelicTier::UNCOMMON, ctx));
            (void)add_event_combat_gold_reward(rc.run, rc.rewards, 100);
            rc.event = EventDialogState{};
            // eliteTrigger = true (:75): Sling of Courage / Preserved Insect /
            // Slaver's Collar treat the Nobs as the elite fight it is -- the
            // Dead Adventurer precedent (kCombatFlagEliteRoom).
            (void)enter_event_combat(rc, "Colosseum Nobs",
                                     EventCombatVariant::NONE,
                                     /*elite_trigger=*/true);
            return EventDialogStatus::TRANSITIONED;
        }
        default:
            return EventDialogStatus::FINISHED;
    }
}

constexpr EventDialogImpl kColosseum = {
    &colosseum_enter,
    &colosseum_menu,
    &colosseum_choose,
};

// --- Masked Bandits ------------------------------------------------------------
// Screens: 0 INTRO (pay / fight), then the pay path's three continue pages
// 1/2/3 (PAID_1..PAID_3; the third press opens the map, :99-104). See the
// file-head note for the ctor-time monster construction deviation.

constexpr uint8_t kBanditsIntro = 0;
constexpr uint8_t kBanditsPaid3 = 3;

void bandits_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kBanditsIntro;
}

void bandits_menu(const RunController& /*rc*/, const EventDialogState& es,
                  EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kBanditsIntro) {
        out.count = 2;  // pay / fight -- neither is ever greyed out (:39-40)
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus bandits_choose(RunController& rc, EventDialogState& es,
                                 uint8_t option) {
    if (es.screen == kBanditsIntro) {
        if (option == 0) {
            // PAY (:59-67): stealGold's per-coin getRandomMonster() is
            // UNSEEDED MathUtils VFX (MonsterGroup.java:148-150, 212 --
            // no stream), then loseGold(ALL gold). loseGold's own amount > 0
            // guard makes the broke-player press a pure page turn, exactly
            // like stealGold's early return (:114-116).
            lose_gold(rc.run, rc.run.gold);
            es.screen = 1;
            return EventDialogStatus::CONTINUE;
        }
        // FIGHT (:68-82): the gold reward's miscRng draw, the Red Mask (a
        // Circlet when it is somehow already held -- the once-per-run pool
        // makes that an imported-state arm, reproduced anyway), then
        // enterCombat. rewardAllowed stays TRUE: the win screen adds the
        // potion roll + card reward on top of these two items.
        (void)add_event_combat_gold_reward(
            rc.run, rc.rewards, random(rc.combat.misc_rng, 25, 35));
        (void)add_event_combat_relic_reward(
            rc.rewards, run_has_relic(rc.run, RelicId::RED_MASK)
                            ? RelicId::CIRCLET
                            : RelicId::RED_MASK);
        (void)enter_event_combat(rc, "Masked Bandits");
        rc.event = EventDialogState{};
        return EventDialogStatus::TRANSITIONED;
    }
    if (es.screen < kBanditsPaid3) {
        ++es.screen;  // PAID_1 -> PAID_2 -> PAID_3 (:87-97)
        return EventDialogStatus::CONTINUE;
    }
    return EventDialogStatus::FINISHED;  // the PAID_3 press opens the map
}

constexpr EventDialogImpl kMaskedBandits = {
    &bandits_enter,
    &bandits_menu,
    &bandits_choose,
};

}  // namespace

// --- The Colosseum reopen seam (contract in event_framework.hpp) -------------

bool event_combat_reopens(const RunController& rc) noexcept {
    return rc.event.event_id ==
               static_cast<uint16_t>(EventId::COLOSSEUM) &&
           rc.event.screen == kColosseumPostCombat;
}

void event_combat_reopen(RunController& rc) noexcept {
    // (1) dropReward(): AbstractRoom's base body is empty (:454-455) and
    //     EventRoom does not override it -- nothing to do.
    // (2) addPotionToRewards() (:330-341): the unopened potion roll. The room
    //     reward list is the one fight-1's button cleared, so the >= 4
    //     suppression reads its live count.
    roll_event_potion_drop_unopened(rc.run, rc.rewards.count);
    // (3) reopen() (:99-110): resetPlayer is transient; preBattlePrep's one
    //     state-visible line is drawPile.initializeDeck's
    //     shuffleRng.randomLong (CardGroup.java:929-931; drawn
    //     UNCONDITIONALLY -- CardGroup.shuffle seeds its java.util.Random
    //     before Collections.shuffle ever looks at the list). The advanced
    //     stream is what the Nobs fight inherits through
    //     enter_event_combat's preserve_floor_streams.
    (void)random_long(rc.combat.shuffle_rng);
    //     monsters.usePreBattleAction() runs over the DEAD Slavers -- neither
    //     SlaverBlue nor SlaverRed declares the method (empty base body).
    //     applyPreCombatLogic then fires atPreBattle against the live relic
    //     list; the folded-back combat mirror is the engine's stand-in, and
    //     anything a body queues there dies with it (the header contract).
    {
        const RelicView rv = player_relics(rc.combat);
        dispatch_relics_at_pre_battle(rc.combat, rv.relics, rv.count);
    }
    //     enterImageFromCombat (:90-102): monsters cleared (the dead combat
    //     is simply left behind), rewards.clear() -- discarding the potion
    //     (2) may have rolled -- and the dialog is back up.
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
}

const EventDialogImpl* event_native_the_library() noexcept {
    return &kTheLibrary;
}

const EventDialogImpl* event_native_the_mausoleum() noexcept {
    return &kTheMausoleum;
}

const EventDialogImpl* event_native_vampires() noexcept {
    return &kVampires;
}

const EventDialogImpl* event_native_colosseum() noexcept {
    return &kColosseum;
}

const EventDialogImpl* event_native_masked_bandits() noexcept {
    return &kMaskedBandits;
}

}  // namespace sts::engine
