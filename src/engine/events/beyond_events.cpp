// The seven TheBeyond eventList bodies (S2.33): Falling, Mind Bloom, The Moai
// Head, Mysterious Sphere, Sensory Stone, Tomb of Lord Red Mask, Winding
// Halls. Selection and dialog plumbing live in event_framework.cpp; this file
// owns only the event-specific state machines.
//
// Provenance (every class read in full from
// D:\STS_BG_Mod\SlayTheSpireDecompiled):
//   * Falling.java:22-133           (setCards :52-65, buttonEffect :67-132)
//   * MindBloom.java:33-144         (ctor :45-54, buttonEffect :56-143)
//   * MoaiHead.java:19-92           (ctor :33-43, buttonEffect :45-91)
//   * MysteriousSphere.java:22-95   (ctor :32-40, buttonEffect :50-94)
//   * SensoryStone.java:27-127      (getRandomMemory :108-116, reward :118-126)
//   * TombRedMask.java:19-74        (ctor :32-41, buttonEffect :43-73)
//   * WindingHalls.java:24-122      (ctor :45-56, buttonEffect :65-121)
//   * CardHelper.java:88-103        (hasCardWithType / returnCardOfType, both
//                                    over getGroupWithoutBottledCards)
//   * AbstractEvent.java:84-92      (enterCombat) / AbstractImageEvent's
//                                    enterCombatFromImage (same phase flip)
//   * AbstractRoom.java:541-543     (addRelicToRewards(tier) -> ONE immediate
//                                    returnRandomRelic pop, at press time)
//   * AbstractDungeon.java:1381-1421 (getColorlessRewardCards -- the Sensory
//                                    Stone reward roll, combat_rewards.cpp)
//   * ProceedButton.java:110-121    (the combat/reward-screen event list --
//                                    MindBloom and MysteriousSphere proceed
//                                    from their reward screen straight to the
//                                    map, the DeadAdventurer/Mushrooms shape)

#include "sts/engine/event_framework.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "../relics/relic_pickup.hpp"
#include "event_common.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_deck.hpp"

namespace sts::engine {

namespace {

using events::decrease_max_hp;
using events::event_relic_context;
using events::heal;
using events::one_proceed_menu;

// Falling ---------------------------------------------------------------------
// Falling.<init>/setCards/buttonEffect (Falling.java:39-132), read in full.
// Screens: 0 INTRO, 1 CHOICE, 2 RESULT.
//
// setCards runs in the CONSTRUCTOR (= on_enter): for each of ATTACK / SKILL /
// POWER -- in that statement order (:56-64) -- present in the master deck
// WITHOUT its bottled cards (CardHelper.java:88-103 both walk
// getGroupWithoutBottledCards), one miscRng.random(size - 1) picks the
// candidate. A type whose only member is bottled counts as ABSENT: no draw,
// disabled option. The CHOICE screen lists skill / power / attack in THAT
// order (:78-92), each disabled when absent; when all three are absent the
// screen is a single "nothing to lose" button (:74-76) and pressing it removes
// nothing (:101-104).

constexpr uint8_t kFallingIntro = 0;
constexpr uint8_t kFallingChoice = 1;
constexpr uint8_t kFallingResult = 2;

[[nodiscard]] int16_t falling_pick(RunState& rs, RngStream& misc_rng,
                                   CardType type) noexcept {
    int32_t count = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardInstance& card = rs.master_deck[i];
        const CardDef* def = card_def(static_cast<CardId>(card.card_id));
        if (def != nullptr && def->type == type &&
            !master_card_bottled(card)) {
            ++count;
        }
    }
    if (count == 0) {
        return -1;  // hasCardWithType false: no draw at all (:56-64)
    }
    // returnCardOfType: rng.random(cards.size() - 1), inclusive -- consumed
    // even for a single candidate (CardHelper.java:96-103).
    const int32_t pick = random(misc_rng, count - 1);
    int32_t seen = 0;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardInstance& card = rs.master_deck[i];
        const CardDef* def = card_def(static_cast<CardId>(card.card_id));
        if (def != nullptr && def->type == type &&
            !master_card_bottled(card)) {
            if (seen == pick) {
                return static_cast<int16_t>(i);
            }
            ++seen;
        }
    }
    return -1;  // unreachable: pick < count by construction
}

void falling_enter(RunController& rc, EventDialogState& es) {
    es.screen = kFallingIntro;
    // Java statement order: attack, then skill, then power (:56-64) -- the
    // draw ORDER is the contract; the display order below is different.
    es.scratch0 = falling_pick(rc.run, rc.combat.misc_rng, CardType::ATTACK);
    es.scratch1 = falling_pick(rc.run, rc.combat.misc_rng, CardType::SKILL);
    es.scratch2 = falling_pick(rc.run, rc.combat.misc_rng, CardType::POWER);
}

void falling_menu(const RunController& /*rc*/, const EventDialogState& es,
                  EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kFallingChoice) {
        if (es.scratch0 < 0 && es.scratch1 < 0 && es.scratch2 < 0) {
            out.count = 1;  // OPTIONS[8], the empty-deck shrug (:74-76)
            out.enabled[0] = true;
            return;
        }
        // Display order skill / power / attack (:78-92); an absent type's
        // option exists but is disabled (setDialogOption(..., true)).
        out.count = 3;
        out.enabled[0] = es.scratch1 >= 0;
        out.enabled[1] = es.scratch2 >= 0;
        out.enabled[2] = es.scratch0 >= 0;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus falling_choose(RunController& rc, EventDialogState& es,
                                 uint8_t option) {
    if (es.screen == kFallingIntro) {
        es.screen = kFallingChoice;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != kFallingChoice) {
        return EventDialogStatus::FINISHED;
    }
    es.screen = kFallingResult;
    if (es.scratch0 < 0 && es.scratch1 < 0 && es.scratch2 < 0) {
        return EventDialogStatus::CONTINUE;  // nothing to remove (:101-104)
    }
    // Buttons: 0 removes the skill, 1 the power, 2 the attack (:99-124).
    const int16_t index = option == 0   ? es.scratch1
                          : option == 1 ? es.scratch2
                                        : es.scratch0;
    if (index >= 0) {
        (void)remove_master_deck_card(rc.run, static_cast<uint16_t>(index));
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kFalling = {
    &falling_enter,
    &falling_menu,
    &falling_choose,
};

// Mind Bloom -------------------------------------------------------------------
// MindBloom.<init>/buttonEffect (MindBloom.java:45-143), read in full.
// Screens: 0 INTRO, 1 LEAVE. The FIGHT arm never returns to the dialog: the
// reward screen's Proceed goes straight to the map (ProceedButton.java:115).
//
// Menu (ctor :47-53): THREE options, all always enabled -- [0] "I am War"
// (the Act-1 boss re-fight), [1] "I am Awake" (upgrade every upgradable card +
// Mark of the Bloom), [2] a floor-keyed third: floorNum % 50 <= 40 offers 999
// gold + 2 Normality, else full heal + Doubt. Both the offer (:49-53) and the
// effect (:108-129) test `AbstractDungeon.floorNum % 50 <= 40` -- the same
// floor, so the two agree; in Act 3 (? floors 36..49) that is floors 36..40
// gold, 41..49 heal.

constexpr std::array<std::string_view, 3> kMindBloomBosses = {
    "The Guardian",  // MindBloom.java:67
    "Hexaghost",     // :68
    "Slime Boss",    // :69
};

[[nodiscard]] constexpr bool mind_bloom_gold_arm(int32_t floor) noexcept {
    return floor % 50 <= 40;  // MindBloom.java:49, :108
}

void mind_bloom_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void mind_bloom_menu(const RunController& /*rc*/, const EventDialogState& es,
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

EventDialogStatus mind_bloom_choose(RunController& rc, EventDialogState& es,
                                    uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // "I am War" (:61-81), in Java statement order:
        // (1) ONE miscRng.randomLong() seeds a JDK Random and
        //     Collections.shuffle picks among the three ACT-1 boss keys
        //     (:66-70) -- s2-design trap 6: floor-scoped stream, so any
        //     earlier miscRng consumer on this floor shifts the pick.
        std::array<std::string_view, 3> list = kMindBloomBosses;
        JdkRandom jr(random_long(rc.combat.misc_rng));
        jdk_shuffle(std::span<std::string_view>(list), jr);
        // (2) getEncounter(list.get(0)) constructs the boss (monsterHpRng)
        //     -- the engine performs the identical construction inside
        //     enter_event_combat below; nothing between reads that stream,
        //     so the deferral is stream-exact.
        // (3) rewards.clear(); fixed gold -- 25 at A13+ / 50 below, NO
        //     miscRng draw (:73-77; a20.yaml row 13's Mind Bloom clause);
        //     then addRelicToRewards(RelicTier.RARE), ONE immediate
        //     returnRandomRelic(RARE) pool pop (AbstractRoom.java:541-543).
        rc.rewards = RewardScreen{};
        rc.rewards.open_card_item = kNoOpenCardReward;
        (void)add_event_combat_gold_reward(
            rc.run, rc.rewards, rc.run.ascension >= 13 ? 25 : 50);
        (void)add_event_combat_relic_reward(
            rc.rewards, return_random_relic_key(rc.run, RelicTier::RARE,
                                                event_relic_context(rc.run)));
        // (4) enterCombatFromImage: an EventRoom combat -- eliteTrigger stays
        //     false (unlike Dead Adventurer) and the room kind stays Event, so
        //     the win pays EVENT-room card odds, not MonsterRoomBoss's
        //     all-RARE row, and no boss chest follows. Combat start runs the
        //     full stage-a 5.2a turn-1 sequence inside enter_event_combat.
        (void)enter_event_combat(rc, list[0]);
        rc.event = EventDialogState{};
        return EventDialogStatus::TRANSITIONED;
    }
    if (option == 1) {
        // "I am Awake" (:83-105): upgrade EVERY canUpgrade() master-deck card
        // in deck order -- Searing Blow keeps stacking (rest_card_upgradeable
        // mirrors canUpgrade) -- then spawnRelicAndObtain(Mark of the Bloom),
        // a fixed relic, no pool draw. The upgrades land BEFORE the relic, so
        // this event's own upgrades are never suppressed by anything; every
        // LATER heal is (events::heal routes the onPlayerHeal fan-out).
        for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
            if (rest_card_upgradeable(rc.run.master_deck[i])) {
                ++rc.run.master_deck[i].upgrade;
            }
        }
        (void)acquire_relic(rc.run, rc.combat.misc_rng,
                            RelicId::MARK_OF_THE_BLOOM);
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    // Option 2, keyed on the SAME floor test as the offer (:108-129).
    if (mind_bloom_gold_arm(rc.run.floor)) {
        // 999 gold FIRST (:116), then two Normality obtains (:117-118) --
        // each ShowCardAndObtainEffect runs its own Omamori gate.
        gain_gold(rc.run, 999);
        (void)add_card_to_master_deck(rc.run, CardId::NORMALITY);
        (void)add_card_to_master_deck(rc.run, CardId::NORMALITY);
    } else {
        // Full heal FIRST (:126), then the Doubt obtain (:127). The heal goes
        // through the onPlayerHeal door: with Mark of the Bloom already owned
        // (a second Mind Bloom this act) it heals zero.
        heal(rc.run, rc.run.max_hp);
        (void)add_card_to_master_deck(rc.run, CardId::DOUBT);
    }
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kMindBloom = {
    &mind_bloom_enter,
    &mind_bloom_menu,
    &mind_bloom_choose,
};

// The Moai Head ----------------------------------------------------------------
// MoaiHead.<init>/buttonEffect (MoaiHead.java:33-91), read in full.
// Screens: 0 INTRO, 1 RESULT. scratch0 = hpAmt, frozen by the ctor from the
// ENTRY-time max HP: MathUtils.round(maxHealth * (A15+ ? 0.18f : 0.125f))
// (:35).

void moai_head_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    const float percent = rc.run.ascension >= 15 ? 0.18f : 0.125f;
    es.scratch0 = static_cast<int16_t>(
        mathutils_round(static_cast<float>(rc.run.max_hp) * percent));
}

void moai_head_menu(const RunController& rc, const EventDialogState& es,
                    EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        out.count = 3;
        out.enabled[0] = true;
        // Option 1's disabled flag is literally !hasRelic("Golden Idol")
        // (:37-41): without the idol the trade shows greyed out.
        out.enabled[1] = run_has_relic(rc.run, RelicId::GOLDEN_IDOL);
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus moai_head_choose(RunController& rc, EventDialogState& es,
                                   uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    if (option == 0) {
        // (:54-61) maxHealth -= hpAmt; clamp hp; floor maxHealth at 1 (the
        // floor is unreachable -- round(0.18 * max) < max for every max >= 1);
        // then heal(maxHealth), a FULL heal through the onPlayerHeal door --
        // Mark of the Bloom zeroes it while the max-HP loss still lands.
        decrease_max_hp(rc.run, es.scratch0);
        heal(rc.run, rc.run.max_hp);
    } else if (option == 1 && run_has_relic(rc.run, RelicId::GOLDEN_IDOL)) {
        // (:68-77) loseRelic FIRST, then gainGold(333).
        (void)lose_relic(rc.run, RelicId::GOLDEN_IDOL);
        gain_gold(rc.run, 333);
    }
    // Option 2 (and a defensive idol-less option 1): leave (:80-84).
    es.screen = 1;
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kTheMoaiHead = {
    &moai_head_enter,
    &moai_head_menu,
    &moai_head_choose,
};

// Mysterious Sphere ------------------------------------------------------------
// MysteriousSphere.<init>/buttonEffect (MysteriousSphere.java:32-94), read in
// full. Screens: 0 INTRO, 1 PRE_COMBAT (one confirm button), 2 END.
//
// THE CONSTRUCTOR BUILDS THE ENCOUNTER (:39): getEncounter("2 Orb Walkers")
// runs at event ENTRY, so the game's monsterHpRng advances by the two ctor
// draw pairs (super-arg + setHp each, monster_orb_walker.hpp) whichever way
// the dialog ends. The engine defers the FIGHT path's construction to
// enter_event_combat -- stream-exact, since no other monsterHpRng consumer
// exists between entry and the fight -- and pays the LEAVE path's draws
// explicitly via burn_unspawned_ctor_rolls, the registry-driven ctor burn, so
// the post-event stream position matches the game on both exits. The one
// residue is transient: DURING the dialog the sim's counter trails the game's
// by four; nothing observable reads it there (the differ's combat comparison
// starts at the fight, and the leave path settles the burn before the event
// returns to the map).

constexpr uint8_t kSphereIntro = 0;
constexpr uint8_t kSpherePreCombat = 1;
constexpr uint8_t kSphereEnd = 2;

void sphere_burn_ctor_rolls(RunController& rc) noexcept {
    burn_unspawned_ctor_rolls(rc.combat, MonsterId::ORB_WALKER);
    burn_unspawned_ctor_rolls(rc.combat, MonsterId::ORB_WALKER);
}

void sphere_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = kSphereIntro;
}

void sphere_menu(const RunController& /*rc*/, const EventDialogState& es,
                 EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == kSphereIntro) {
        out.count = 2;
        out.enabled[0] = true;
        out.enabled[1] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus sphere_choose(RunController& rc, EventDialogState& es,
                                uint8_t option) {
    if (es.screen == kSphereIntro) {
        if (option == 0) {
            es.screen = kSpherePreCombat;  // "Open it" -> confirm page (:55-61)
        } else {
            sphere_burn_ctor_rolls(rc);  // the ctor draws the game already paid
            es.screen = kSphereEnd;      // (:63-69)
        }
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen == kSpherePreCombat) {
        // (:74-88), in Java order: gold miscRng.random(45, 55) (non-daily),
        // then addRelicToRewards(returnRandomScreenlessRelic(RARE)) -- the
        // SCREENLESS pop, unlike Mind Bloom's plain returnRandomRelic -- then
        // enterCombat.
        rc.rewards = RewardScreen{};
        rc.rewards.open_card_item = kNoOpenCardReward;
        (void)add_event_combat_gold_reward(
            rc.run, rc.rewards,
            static_cast<int32_t>(random(rc.combat.misc_rng, 45, 55)));
        (void)add_event_combat_relic_reward(
            rc.rewards,
            return_random_screenless_relic(rc.run, RelicTier::RARE,
                                           event_relic_context(rc.run)));
        (void)enter_event_combat(rc, "2 Orb Walkers");
        rc.event = EventDialogState{};
        return EventDialogStatus::TRANSITIONED;
    }
    return EventDialogStatus::FINISHED;
}

constexpr EventDialogImpl kMysteriousSphere = {
    &sphere_enter,
    &sphere_menu,
    &sphere_choose,
};

// Sensory Stone ----------------------------------------------------------------
// SensoryStone.<init>/buttonEffect/getRandomMemory/reward
// (SensoryStone.java:43-126), read in full.
// Screens: 0 INTRO, 1 INTRO_2 (one / two / three memories).
//
// The chosen arm runs, in Java order: getRandomMemory -- ONE
// miscRng.randomLong() seeding a cosmetic 4-string shuffle whose OUTPUT is
// display text; the DRAW is the state (:108-116) -- then reward(n):
// rewards.clear() + n RewardItem(COLORLESS) rows, each a full
// getColorlessRewardCards roll on cardRng AT CONSTRUCTION
// (roll_colorless_card_reward_item), room phase COMPLETE +
// combatRewardScreen.open(); then the HP_LOSS damage (5 / 10) for memories two
// and three (:78-93) -- AFTER the rolls, so a lethal second memory still moved
// cardRng. The reward screen is the exit (the COMPLETE phase stops the event
// ticking, the Lab/Wheel/Woman-in-Blue precedent); its Proceed goes to the
// map. The game's own ACCEPT screen is unreachable (reward() flips the screen
// to LEAVE before any further press), mirrored here by clearing the dialog.

void sensory_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void sensory_menu(const RunController& /*rc*/, const EventDialogState& es,
                  EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 1) {
        out.count = 3;
        out.enabled[0] = true;
        out.enabled[1] = true;  // no HP gate: 5 HP_LOSS can be lethal
        out.enabled[2] = true;
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus sensory_choose(RunController& rc, EventDialogState& es,
                                 uint8_t option) {
    if (es.screen == 0) {
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 1) {
        return EventDialogStatus::FINISHED;
    }
    // getRandomMemory: the shuffle's Random is constructed unconditionally,
    // so the miscRng.randomLong() draw happens for every memory choice.
    (void)random_long(rc.combat.misc_rng);
    // reward(choice): choice = buttonPressed + 1 rows (:73, :81, :90).
    rc.rewards = RewardScreen{};
    rc.rewards.open_card_item = kNoOpenCardReward;
    const int rows = static_cast<int>(option) + 1;
    for (int i = 0; i < rows; ++i) {
        roll_colorless_card_reward_item(rc.run, rc.rewards);
    }
    // The HP cost lands AFTER the reward construction (:83, :92):
    // DamageInfo(null, n, HP_LOSS) is the NULL_SOURCE door (the Wheel of
    // Change precedent -- no onAttacked relic sees a null owner).
    bool alive = true;
    if (option == 1) {
        alive = apply_event_damage(rc, 5, EventDamageOwner::NULL_SOURCE);
    } else if (option == 2) {
        alive = apply_event_damage(rc, 10, EventDamageOwner::NULL_SOURCE);
    }
    rc.event = EventDialogState{};
    if (!alive) {
        return EventDialogStatus::TRANSITIONED;  // RUN_OVER already installed
    }
    rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
    return EventDialogStatus::TRANSITIONED;
}

constexpr EventDialogImpl kSensoryStone = {
    &sensory_enter,
    &sensory_menu,
    &sensory_choose,
};

// Tomb of Lord Red Mask ----------------------------------------------------------
// TombRedMask.<init>/buttonEffect (TombRedMask.java:32-73), read in full.
// Screens: 0 INTRO, 1 RESULT.
//
// The menu SHAPE depends on Red Mask ownership (:34-40): WITH the mask the
// screen is [wear it (+222 gold), leave]; WITHOUT it it is [a DISABLED "wear"
// row, buy the mask for ALL gold, leave]. Buying at 0 gold is legal -- the
// Java has no gold gate, loseGold(0) is a no-op and the relic still lands.

void tomb_enter(RunController& /*rc*/, EventDialogState& es) {
    es.screen = 0;
}

void tomb_menu(const RunController& rc, const EventDialogState& es,
               EventDialogMenu& out) {
    out = EventDialogMenu{};
    if (es.screen == 0) {
        if (run_has_relic(rc.run, RelicId::RED_MASK)) {
            out.count = 2;
            out.enabled[0] = true;   // OPTIONS[0]: wear the mask
            out.enabled[1] = true;   // OPTIONS[4]: leave
        } else {
            out.count = 3;
            out.enabled[0] = false;  // OPTIONS[1], setDialogOption(..., true)
            out.enabled[1] = true;   // OPTIONS[2]: buy for all gold
            out.enabled[2] = true;   // OPTIONS[4]: leave
        }
        return;
    }
    one_proceed_menu(out);
}

EventDialogStatus tomb_choose(RunController& rc, EventDialogState& es,
                              uint8_t option) {
    if (es.screen != 0) {
        return EventDialogStatus::FINISHED;
    }
    const bool has_mask = run_has_relic(rc.run, RelicId::RED_MASK);
    if (option == 0 && has_mask) {
        gain_gold(rc.run, 222);  // (:47-51)
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (option == 1 && !has_mask) {
        // (:52-57) logMetric reads the pre-payment gold, loseGold(player.gold)
        // -- the onLoseGold door, a no-op purse write at 0 gold -- then
        // spawnRelicAndObtain(new RedMask()): a fixed relic, no pool draw.
        lose_gold(rc.run, rc.run.gold);
        (void)acquire_relic(rc.run, rc.combat.misc_rng, RelicId::RED_MASK);
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    // Leave (:58-63): openMap() fires immediately -- the RESULT page the Java
    // also installs is unreachable dressing, so this is FINISHED, not a page.
    return EventDialogStatus::FINISHED;
}

constexpr EventDialogImpl kTombOfLordRedMask = {
    &tomb_enter,
    &tomb_menu,
    &tomb_choose,
};

// Winding Halls ------------------------------------------------------------------
// WindingHalls.<init>/buttonEffect (WindingHalls.java:45-121), read in full.
// Screens: 0 INTRO, 1 CHOICE, 2 RESULT. scratch0/1/2 = hpAmt / healAmt /
// maxHPAmt, all frozen by the ctor from the ENTRY-time max HP (:47-54):
//   hpAmt    = round(maxHealth * (A15+ ? 0.18f : 0.125f))
//   healAmt  = round(maxHealth * (A15+ ? 0.2f  : 0.25f))   -- A15 heals LESS
//   maxHPAmt = round(maxHealth * 0.05f)                     -- tier-free

void winding_enter(RunController& rc, EventDialogState& es) {
    es.screen = 0;
    const bool a15 = rc.run.ascension >= 15;
    const auto max_hp = static_cast<float>(rc.run.max_hp);
    es.scratch0 = static_cast<int16_t>(
        mathutils_round(max_hp * (a15 ? 0.18f : 0.125f)));
    es.scratch1 = static_cast<int16_t>(
        mathutils_round(max_hp * (a15 ? 0.2f : 0.25f)));
    es.scratch2 = static_cast<int16_t>(mathutils_round(max_hp * 0.05f));
}

void winding_menu(const RunController& /*rc*/, const EventDialogState& es,
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

EventDialogStatus winding_choose(RunController& rc, EventDialogState& es,
                                 uint8_t option) {
    if (es.screen == 0) {
        es.screen = 1;
        return EventDialogStatus::CONTINUE;
    }
    if (es.screen != 1) {
        return EventDialogStatus::FINISHED;
    }
    es.screen = 2;
    if (option == 0) {
        // Embrace Madness (:78-91): DamageInfo(null, hpAmt) -- default NORMAL
        // type, null owner (the FaceTrader touch shape) -- THEN two Madness
        // obtains. The Java constructs the obtain effects after the damage
        // regardless of death; the master-deck writes are post-mortem
        // no-observables on a dead run, so the engine keeps the same order
        // and only then reports the transition.
        const bool alive = apply_event_damage(rc, es.scratch0,
                                              EventDamageOwner::NULL_SOURCE);
        (void)add_card_to_master_deck(rc.run, CardId::MADNESS);
        (void)add_card_to_master_deck(rc.run, CardId::MADNESS);
        if (!alive) {
            return EventDialogStatus::TRANSITIONED;
        }
    } else if (option == 1) {
        // Retrace steps (:93-102): heal FIRST -- the onPlayerHeal door; Mark
        // of the Bloom zeroes it -- then the Writhe obtain (Omamori-gated
        // inside the deck door).
        heal(rc.run, es.scratch1);
        (void)add_card_to_master_deck(rc.run, CardId::WRITHE);
    } else {
        // Press on (:104-112): decreaseMaxHealth(maxHPAmt).
        decrease_max_hp(rc.run, es.scratch2);
    }
    return EventDialogStatus::CONTINUE;
}

constexpr EventDialogImpl kWindingHalls = {
    &winding_enter,
    &winding_menu,
    &winding_choose,
};

}  // namespace

const EventDialogImpl* event_native_falling() noexcept {
    return &kFalling;
}

const EventDialogImpl* event_native_mind_bloom() noexcept {
    return &kMindBloom;
}

const EventDialogImpl* event_native_the_moai_head() noexcept {
    return &kTheMoaiHead;
}

const EventDialogImpl* event_native_mysterious_sphere() noexcept {
    return &kMysteriousSphere;
}

const EventDialogImpl* event_native_sensory_stone() noexcept {
    return &kSensoryStone;
}

const EventDialogImpl* event_native_tomb_of_lord_red_mask() noexcept {
    return &kTombOfLordRedMask;
}

const EventDialogImpl* event_native_winding_halls() noexcept {
    return &kWindingHalls;
}

}  // namespace sts::engine
