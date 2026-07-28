// B3.9 status and curse behavior. Expected values are hand-computed from the
// cited Java implementations in registry/cards.yaml, not read from the engine.

#include <array>
#include <cstdint>
#include <initializer_list>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/card_pools.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/run_deck.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

CombatState MakeState(std::initializer_list<CardId> hand, int16_t energy = 3) {
    CombatState s{};
    uint8_t i = 0;
    for (const CardId id : hand) {
        const CardDef* def = card_def(id);
        EXPECT_NE(def, nullptr);
        s.card_pool[i].card_id = static_cast<uint16_t>(id);
        s.card_pool[i].cost_now = card_cost(*def, 0);
        s.card_pool[i].flags = card_flags(*def, 0);
        s.hand[i] = i;
        ++i;
    }
    s.hand_count = i;
    s.player_hp = 100;
    s.player_max_hp = 100;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 50;
    s.monsters[0].max_hp = 50;
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

const PowerSlot* FindPlayerPower(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

bool HandContains(const CombatState& s, CardId id) {
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.card_pool[s.hand[i]].card_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

// Drain the end-of-turn sentinel and everything it queues, then STOP -- before
// the pump reaches the monster turn and the next turn's draw, which would
// reshuffle the very discard pile these tests are reading.
void PumpEndOfTurnStageOnly(CombatState& s) {
    for (int guard = 0; guard < 256; ++guard) {
        if (s.action_count == 0 && s.pre_turn_count == 0 &&
            s.card_queue_count == 0) {
            return;
        }
        if (pump_step(s, default_monster_turn).outcome ==
            PumpOutcome::WAITING_ON_USER) {
            return;
        }
    }
    ADD_FAILURE() << "end-of-turn stage did not quiesce";
}

TEST(StatusCurseRegistry, EveryInScopeEntryHasItsExactIdentityAndMetadata) {
    struct Expected {
        CardId id;
        CardType type;
        CardTrigger trigger;
        bool ethereal;
        bool exhaust;
        bool innate;
        bool poolable;
    };
    constexpr Expected kExpected[] = {
        {CardId::WOUND, CardType::STATUS, CardTrigger::ON_PLAY, false, false, false, false},
        {CardId::BURN, CardType::STATUS, CardTrigger::END_OF_TURN, false, false, false, false},
        {CardId::DAZED, CardType::STATUS, CardTrigger::ON_PLAY, true, false, false, false},
        {CardId::SLIMED, CardType::STATUS, CardTrigger::ON_PLAY, false, true, false, false},
        {CardId::VOID, CardType::STATUS, CardTrigger::ON_DRAW, true, false, false, false},
        {CardId::CLUMSY, CardType::CURSE, CardTrigger::ON_PLAY, true, false, false, true},
        {CardId::DECAY, CardType::CURSE, CardTrigger::END_OF_TURN, false, false, false, true},
        {CardId::DOUBT, CardType::CURSE, CardTrigger::END_OF_TURN, false, false, false, true},
        {CardId::INJURY, CardType::CURSE, CardTrigger::ON_PLAY, false, false, false, true},
        {CardId::NORMALITY, CardType::CURSE, CardTrigger::ON_PLAY, false, false, false, true},
        {CardId::PAIN, CardType::CURSE, CardTrigger::ON_OTHER_CARD_PLAYED, false, false, false, true},
        {CardId::PARASITE, CardType::CURSE, CardTrigger::ON_PLAY, false, false, false, true},
        {CardId::REGRET, CardType::CURSE, CardTrigger::END_OF_TURN, false, false, false, true},
        {CardId::SHAME, CardType::CURSE, CardTrigger::END_OF_TURN, false, false, false, true},
        {CardId::WRITHE, CardType::CURSE, CardTrigger::ON_PLAY, false, false, true, true},
        {CardId::ASCENDERS_BANE, CardType::CURSE, CardTrigger::ON_PLAY, true, false, false, false},
    };

    for (const Expected& e : kExpected) {
        const CardDef* def = card_def(e.id);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->type, e.type) << static_cast<int>(e.id);
        EXPECT_EQ(def->trigger, e.trigger) << static_cast<int>(e.id);
        EXPECT_EQ(has_card_flag(card_flags(*def, 0), CardFlag::ETHEREAL), e.ethereal);
        EXPECT_EQ(has_card_flag(card_flags(*def, 0), CardFlag::EXHAUST), e.exhaust);
        EXPECT_EQ(has_card_flag(card_flags(*def, 0), CardFlag::INNATE), e.innate);
        EXPECT_EQ(def->curse_pool, e.poolable);
        EXPECT_EQ(has_card_flag(card_flags(*def, 0), CardFlag::UNPLAYABLE),
                  e.id != CardId::SLIMED);
    }
    EXPECT_EQ(card_def(CardId::PARASITE)->on_remove_max_hp_loss, 3);
}

TEST(StatusCurses, SlimedExhaustsOnPlayAndPainHitsForEachOtherCard) {
    CombatState slimed = MakeState({CardId::SLIMED});
    ASSERT_TRUE(queue_card_play(slimed, 0, 0));
    pump(slimed, default_monster_turn);
    EXPECT_EQ(slimed.player_energy, 2);
    ASSERT_EQ(slimed.exhaust_count, 1);
    EXPECT_EQ(slimed.card_pool[slimed.exhaust[0]].card_id,
              static_cast<uint16_t>(CardId::SLIMED));

    CombatState pain = MakeState({CardId::PAIN, CardId::STRIKE});
    ASSERT_TRUE(queue_card_play(pain, 1, 0));
    pump(pain, default_monster_turn);
    EXPECT_EQ(pain.player_hp, 99);  // Pain.java: LoseHPAction(1), block bypassed.
    ASSERT_EQ(pain.hand_count, 1);
    EXPECT_TRUE(HandContains(pain, CardId::PAIN));
}

TEST(StatusCurses, VoidLosesOneEnergyWhenDrawn) {
    CombatState s = MakeState({});
    const CardDef* def = card_def(CardId::VOID);
    ASSERT_NE(def, nullptr);
    s.card_pool[0].card_id = static_cast<uint16_t>(CardId::VOID);
    s.card_pool[0].cost_now = card_cost(*def, 0);
    s.card_pool[0].flags = card_flags(*def, 0);
    s.draw[0] = 0;
    s.draw_count = 1;
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.amount = 1;
    add_to_bottom(s, draw);

    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.player_energy, 2);
}

TEST(StatusCurses, EndTurnEffectsPrecedeEtherealAndHandDiscard) {
    // Directed script. Each end-of-turn card plays ITSELF (the Java re-queues it
    // into the cardQueue with dontTriggerOnUseCard), so the resolution order is
    // [Regret's HP loss, Regret filed, Burn's damage, Burn filed] and only then
    // the DiscardAtEndOfTurnAction sweep, which exhausts Dazed.
    CombatState s = MakeState({CardId::REGRET, CardId::BURN, CardId::DAZED});
    s.player_block = 1;
    add_card_to_queue_bottom(s, make_end_turn_sentinel());

    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::END_TURN_SENTINEL);
    // The sentinel's dispatch already pulled Regret and Burn out of the hand
    // (AbstractPlayer.useCard:1373); Dazed is all that is left for the sweep.
    EXPECT_EQ(s.hand_count, 1);
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.player_hp, 97);  // Regret: hand size 3, LOCKED at trigger time.
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    ASSERT_EQ(s.discard_count, 1);  // Regret's own UseCardAction files it first.
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id, static_cast<uint16_t>(CardId::REGRET));
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.player_hp, 96);  // Burn 2 THORNS through one block.
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    ASSERT_EQ(s.discard_count, 2);
    EXPECT_EQ(s.card_pool[s.discard[1]].card_id, static_cast<uint16_t>(CardId::BURN));
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.hand_count, 0);
    ASSERT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.card_pool[s.exhaust[0]].card_id, static_cast<uint16_t>(CardId::DAZED));
    EXPECT_EQ(s.discard_count, 2);
    EXPECT_EQ(s.limbo_count, 0);
}

TEST(StatusCurses, DecayDoubtAndShameRunAtEndOfTurn) {
    CombatState s = MakeState({CardId::DECAY, CardId::DOUBT, CardId::SHAME});
    s.player_block = 1;
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::END_TURN_SENTINEL);
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(s.player_hp, 99);  // Decay 2 THORNS through one block.
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);  // file Decay
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    const PowerSlot* weak = FindPlayerPower(s, PowerId::WEAK);
    ASSERT_NE(weak, nullptr);
    EXPECT_EQ(weak->amount, 1);
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);  // file Doubt
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    const PowerSlot* frail = FindPlayerPower(s, PowerId::FRAIL);
    ASSERT_NE(frail, nullptr);
    EXPECT_EQ(frail->amount, 1);
}

// REGRESSION (oracle STS00048, floor 1: Spike Slime S + Acid Slime M). The
// end-of-turn curse is PLAYED out of the hand, so it reaches the discard pile
// BEFORE DiscardAtEndOfTurnAction files the rest of the hand from the top down.
// The sim used to leave it in the hand, which put it LAST instead of first; the
// resulting discard order is the input to the next reshuffle, so one
// transposition there re-deals the whole rest of the fight.
TEST(StatusCurses, EndOfTurnCurseIsDiscardedBeforeTheGetTopCardHandSweep) {
    // Hand order matches the capture at seq 9: Shame, an Ethereal curse, Strike.
    CombatState s = MakeState({CardId::SHAME, CardId::ASCENDERS_BANE, CardId::STRIKE});
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    PumpEndOfTurnStageOnly(s);

    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.limbo_count, 0);
    // Shame first (its own UseCardAction), then the sweep's getTopCard order --
    // Ascender's Bane exhausted, so Strike is the only card left to file.
    ASSERT_EQ(s.discard_count, 2);
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id, static_cast<uint16_t>(CardId::SHAME));
    EXPECT_EQ(s.card_pool[s.discard[1]].card_id, static_cast<uint16_t>(CardId::STRIKE));
    ASSERT_EQ(s.exhaust_count, 1);
    EXPECT_EQ(s.card_pool[s.exhaust[0]].card_id,
              static_cast<uint16_t>(CardId::ASCENDERS_BANE));
    // The play is a dontTriggerOnUseCard autoplay: it does NOT count as a card
    // played this turn (GameActionManager.java:220-249 skips the increment).
    EXPECT_EQ(s.cards_played_this_turn, 0);
    const PowerSlot* frail = FindPlayerPower(s, PowerId::FRAIL);
    ASSERT_NE(frail, nullptr);
    EXPECT_EQ(frail->amount, 1);
}

// Two triggering cards resolve strictly serially -- card k is filed before card
// k+1's own effect runs -- because the game only services the cardQueue with an
// empty action queue (GameActionManager.getNextAction:185-194).
TEST(StatusCurses, TwoEndOfTurnCursesFileInHandOrderAheadOfTheSweptHand) {
    CombatState s = MakeState({CardId::DEFEND, CardId::SHAME, CardId::DOUBT});
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    PumpEndOfTurnStageOnly(s);

    ASSERT_EQ(s.discard_count, 3);
    EXPECT_EQ(s.card_pool[s.discard[0]].card_id, static_cast<uint16_t>(CardId::SHAME));
    EXPECT_EQ(s.card_pool[s.discard[1]].card_id, static_cast<uint16_t>(CardId::DOUBT));
    EXPECT_EQ(s.card_pool[s.discard[2]].card_id, static_cast<uint16_t>(CardId::DEFEND));
}

// Regret's magicNumber is locked at trigger time with the hand still whole
// (Regret.java:35-39), NOT re-read when its LoseHPAction resolves -- by then the
// autoplay has removed Regret and every other triggering curse from the hand.
TEST(StatusCurses, RegretLocksTheHandSizeBeforeTheEndOfTurnCursesLeaveTheHand) {
    CombatState s = MakeState({CardId::REGRET, CardId::SHAME, CardId::DEFEND,
                               CardId::STRIKE});
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    PumpEndOfTurnStageOnly(s);
    EXPECT_EQ(s.player_hp, 96);  // 4 cards in hand at the lock, not 2.
}

TEST(StatusCurses, FrailModifiesOnlyCardBlockAndExpiresAfterSkipFirst) {
    CombatState s = MakeState({});
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = kActorPlayer;  // Shame is player-owned but constructs Frail(..., true).
    apply.tgt = kActorPlayer;
    apply.amount = 1;
    apply.flags = make_apply_power_flags(PowerId::FRAIL);
    execute_opcode(s, apply);

    ASSERT_NE(FindPlayerPower(s, PowerId::FRAIL), nullptr);
    // The justApplied latch is the slot's own `counter` (types.hpp), per
    // INSTANCE -- it replaced the player-only CombatState.flags bit precisely so
    // that a monster-owned Frail, and Vulnerable/Weak on any of six actors at
    // once, can each carry their own.
    EXPECT_EQ(FindPlayerPower(s, PowerId::FRAIL)->counter, 1);

    ActionQueueItem card_block{};
    card_block.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    card_block.tgt = kActorPlayer;
    card_block.amount = 5;
    execute_opcode(s, card_block);
    EXPECT_EQ(s.player_block, 3);  // floor(5 * 0.75) == 3.

    ActionQueueItem direct_block = card_block;
    direct_block.flags = kBlockNoPowers;
    execute_opcode(s, direct_block);
    EXPECT_EQ(s.player_block, 8);  // direct GainBlockAction adds the full 5.

    dispatch_at_end_of_round(s);
    ASSERT_NE(FindPlayerPower(s, PowerId::FRAIL), nullptr);
    EXPECT_EQ(FindPlayerPower(s, PowerId::FRAIL)->amount, 1);
    EXPECT_EQ(FindPlayerPower(s, PowerId::FRAIL)->counter, 0);

    // Stacking the existing object does not recreate/reset justApplied.
    execute_opcode(s, apply);
    ASSERT_EQ(FindPlayerPower(s, PowerId::FRAIL)->amount, 2);
    EXPECT_EQ(FindPlayerPower(s, PowerId::FRAIL)->counter, 0);
    dispatch_at_end_of_round(s);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    ASSERT_NE(FindPlayerPower(s, PowerId::FRAIL), nullptr);
    EXPECT_EQ(FindPlayerPower(s, PowerId::FRAIL)->amount, 1);
    dispatch_at_end_of_round(s);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(pump_step(s, default_monster_turn).outcome, PumpOutcome::RAN_ACTION);
    EXPECT_EQ(FindPlayerPower(s, PowerId::FRAIL), nullptr);
}

TEST(StatusCurses, NormalityVetoStartsAtTheFourthCard) {
    CombatState s = MakeState({CardId::NORMALITY, CardId::STRIKE});
    ActionMask mask{};
    s.cards_played_this_turn = 2;
    legal_actions(s, mask);
    EXPECT_TRUE(mask.can_play[1]);

    s.cards_played_this_turn = 3;
    legal_actions(s, mask);
    EXPECT_FALSE(mask.can_play[0]);
    EXPECT_FALSE(mask.can_play[1]);
    EXPECT_TRUE(mask.can_end_turn);
}

TEST(StatusCurses, PoolExcludesAscendersBaneAndParasiteRemovalReducesMaxHp) {
    ASSERT_EQ(kPoolableCurseCount, 10);
    for (const CardId id : kPoolableCurses) {
        EXPECT_NE(id, CardId::ASCENDERS_BANE);
        EXPECT_EQ(card_def(id)->type, CardType::CURSE);
    }
    RngStream card_rng = from_seed(12345);
    const int32_t before = card_rng.counter;
    const CardId rolled = return_random_curse(card_rng);
    EXPECT_EQ(card_rng.counter, before + 1);
    bool in_pool = false;
    for (const CardId id : kPoolableCurses) {
        in_pool = in_pool || id == rolled;
    }
    EXPECT_TRUE(in_pool);

    RunState run{};
    run.hp = 80;
    run.max_hp = 80;
    run.master_deck_count = 2;
    run.master_deck[0].card_id = static_cast<uint16_t>(CardId::PARASITE);
    run.master_deck[1].card_id = static_cast<uint16_t>(CardId::STRIKE);
    ASSERT_TRUE(remove_master_deck_card(run, 0));
    EXPECT_EQ(run.master_deck_count, 1);
    EXPECT_EQ(run.master_deck[0].card_id, static_cast<uint16_t>(CardId::STRIKE));
    EXPECT_EQ(run.max_hp, 77);
    EXPECT_EQ(run.hp, 77);
}

TEST(StatusCurses, WritheIsGuaranteedInTheOpeningHand) {
    constexpr std::array<CardId, 6> deck{{
        CardId::STRIKE, CardId::DEFEND, CardId::BASH,
        CardId::POMMEL_STRIKE, CardId::SHRUG_IT_OFF, CardId::WRITHE,
    }};
    const CombatState s = combat_begin(0xB39, 1, deck);
    EXPECT_TRUE(HandContains(s, CardId::WRITHE));
}

}  // namespace
}  // namespace sts::engine
