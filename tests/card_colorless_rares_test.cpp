// B3.11 stage A: the four colorless RARE cards that stage owns -- Apotheosis,
// Master of Strategy, Sadistic Nature, Thinking Ahead -- plus the UPGRADE_ALL
// opcode. Per-card base/upgraded tier-2 behaviour, the Sadistic native-power
// Shackled exclusion fix, Thinking Ahead's queue-time hand.size() guard, and
// UPGRADE_ALL's pile order / canUpgrade gate.
//
// B3.11 stage B adds Secret Technique, Secret Weapon and Violence -- the
// draw-pile-sourced deck-to-hand verbs -- plus the DRAW_PILE_FETCH opcode and
// the PutOnDeckAction forced-path RNG fix that Warcry and Thinking Ahead share.
//
// Expected values are hand-computed from the cited decompiled Java (see the
// per-row provenance in registry/cards.yaml); the seeded permutation cases
// build an INDEPENDENT oracle out of the golden-tested JDK/xorshift primitives
// rather than re-running the engine's own code path.

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

CombatState MakeCombat(int16_t energy = 6, int16_t monster_hp = 100) {
    CombatState s{};
    s.player_hp = 70;
    s.player_max_hp = 80;
    s.player_energy = energy;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = monster_hp;
    s.monsters[0].max_hp = monster_hp;
    s.monster_attacks_queued = 1;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

CardPoolIndex AddCard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    uint8_t pi = 0;
    while (pi < kCardPoolCap &&
           s.card_pool[pi].card_id != static_cast<uint16_t>(CardId::NONE)) {
        ++pi;
    }
    const CardDef* d = card_def(id);
    EXPECT_NE(d, nullptr);
    s.card_pool[pi].card_id = static_cast<uint16_t>(id);
    s.card_pool[pi].upgrade = upgrade;
    s.card_pool[pi].cost_now = card_cost(*d, upgrade);
    s.card_pool[pi].flags = card_flags(*d, upgrade);
    return pi;
}

CardPoolIndex AddHand(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.hand[s.hand_count++] = pi;
    return pi;
}

CardPoolIndex AddDrawTop(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.draw[s.draw_count++] = pi;
    return pi;
}

CardPoolIndex AddDiscard(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.discard[s.discard_count++] = pi;
    return pi;
}

CardPoolIndex AddExhaust(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.exhaust[s.exhaust_count++] = pi;
    return pi;
}

void AddPlayerPower(CombatState& s, PowerId id, int16_t amount) {
    s.player_powers[s.player_power_count] =
        PowerSlot{static_cast<uint16_t>(id), amount};
    ++s.player_power_count;
}

const PowerSlot* FindPlayerPower(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

const PowerSlot* FindMonsterPower(const CombatState& s, uint8_t m, PowerId id) {
    for (uint8_t i = 0; i < s.monsters[m].power_count; ++i) {
        if (s.monsters[m].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[m].powers[i];
        }
    }
    return nullptr;
}

// CardEffectStep::op is sts::registry::Opcode -- the generated table's OWN
// mirror enum (cards.hpp banner: kept distinct from the engine's authoritative
// sts::engine::Opcode on purpose, cross-checked only by the drift static_assert
// in cards.hpp). Compare by the underlying value, the same pattern
// card_play.cpp uses (`static_cast<decltype(step.op)>(Opcode::X)`).
bool StepOpIs(sts::registry::Opcode actual, Opcode expected) {
    return static_cast<uint16_t>(actual) == static_cast<uint16_t>(expected);
}

bool PileHas(const CardPoolIndex* pile, uint8_t count, CardPoolIndex pi) {
    for (uint8_t i = 0; i < count; ++i) {
        if (pile[i] == pi) {
            return true;
        }
    }
    return false;
}

void Play(CombatState& s, uint8_t hand_slot = 0, uint8_t target = 0) {
    ASSERT_TRUE(queue_card_play(s, hand_slot, target));
    pump(s, default_monster_turn);
}

void Pump(CombatState& s) { pump(s, default_monster_turn); }

// A bare queued DRAW-source CHOOSE_CARD, for the paths Secret Technique's own
// canUse gate normally forbids (the k == 0 no-op) and for the full-hand redirect
// that a hand play cannot reach (playing the card frees a hand slot first).
void QueueDrawToHand(CombatState& s, CardType type, int amount = 1) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::CHOOSE_CARD);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    it.amount = amount;
    it.flags = make_choose_flags(ChoiceKind::DRAW_TO_HAND, /*random=*/false,
                                 /*copies=*/1, static_cast<uint8_t>(type));
    add_to_bottom(s, it);
}

ActionQueueItem DrawPileFetchItem(int amount, CardType type) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::DRAW_PILE_FETCH);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    it.amount = amount;
    it.flags = make_draw_pile_fetch_flags(static_cast<uint8_t>(type));
    return it;
}

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

// ===========================================================================
// Registry: the exact four-card roster this stage owns, in library order, with
// its interior gaps left reserved for later B3.11 stages.
// ===========================================================================

TEST(CardColorlessRaresRegistry, ExactRosterAndReservedGaps) {
    const std::array<std::pair<int, CardId>, 7> roster{{
        {112, CardId::APOTHEOSIS},
        {116, CardId::MASTER_OF_STRATEGY},
        {120, CardId::SADISTIC_NATURE},
        {121, CardId::SECRET_TECHNIQUE},
        {122, CardId::SECRET_WEAPON},
        {124, CardId::THINKING_AHEAD},
        {126, CardId::VIOLENCE},
    }};
    for (const auto& [id, card] : roster) {
        EXPECT_EQ(static_cast<int>(card), id);
        const CardDef* d = card_def(card);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->id, card);
    }
    // 113-115, 117-119, 123, 125: owned by later B3.11 stages. A reservation
    // nothing asserts is indistinguishable from an omission.
    for (const int reserved : {113, 114, 115, 117, 118, 119, 123, 125}) {
        EXPECT_EQ(card_def(static_cast<CardId>(reserved)), nullptr)
            << "id " << reserved << " is reserved and must have no row";
    }
}

TEST(CardColorlessRaresRegistry, CostsTypesFlagsAndTargeting) {
    struct Row {
        CardId id;
        uint8_t cost;
        uint8_t up_cost;
        CardType type;
        CardTargetKind target_kind;
    };
    const std::array<Row, 4> rows{{
        {CardId::APOTHEOSIS, 2, 1, CardType::SKILL, CardTargetKind::NONE},
        {CardId::MASTER_OF_STRATEGY, 0, 0, CardType::SKILL,
         CardTargetKind::NONE},
        {CardId::SADISTIC_NATURE, 0, 0, CardType::POWER, CardTargetKind::SELF},
        {CardId::THINKING_AHEAD, 0, 0, CardType::SKILL, CardTargetKind::NONE},
    }};
    for (const Row& r : rows) {
        const CardDef* d = card_def(r.id);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(card_cost(*d, 0), r.cost);
        EXPECT_EQ(card_cost(*d, 1), r.up_cost);
        EXPECT_EQ(d->type, r.type);
        EXPECT_EQ(d->target_kind, r.target_kind);
        EXPECT_FALSE(d->needs_target);
        EXPECT_FALSE(d->random_target);
    }

    // Apotheosis and Master of Strategy exhaust on BOTH rows.
    for (const CardId id : {CardId::APOTHEOSIS, CardId::MASTER_OF_STRATEGY}) {
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr);
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
    }
    // Thinking Ahead exhausts BASE only -- upgrade() clears it
    // (ThinkingAhead.java:46), the Limit Break / Brutality `upgraded_flags: []`
    // shape.
    {
        const CardDef* d = card_def(CardId::THINKING_AHEAD);
        ASSERT_NE(d, nullptr);
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_FALSE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
    }
    // Sadistic Nature is a POWER card and never exhausts.
    {
        const CardDef* d = card_def(CardId::SADISTIC_NATURE);
        ASSERT_NE(d, nullptr);
        EXPECT_FALSE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_FALSE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
    }
}

TEST(CardColorlessRaresRegistry, EffectProgramsBaseAndUpgraded) {
    // Apotheosis: one UPGRADE_ALL step, both rows.
    {
        const CardDef* d = card_def(CardId::APOTHEOSIS);
        ASSERT_NE(d, nullptr);
        for (uint8_t up = 0; up < 2; ++up) {
            const CardEffectView v = card_effect_steps(*d, up);
            ASSERT_EQ(v.count, 1);
            EXPECT_TRUE(StepOpIs(v.steps[0].op, Opcode::UPGRADE_ALL));
            EXPECT_EQ(v.steps[0].target, StepTarget::SELF);
        }
    }
    // Master of Strategy: DRAW 3 base / 4 upgraded.
    {
        const CardDef* d = card_def(CardId::MASTER_OF_STRATEGY);
        ASSERT_NE(d, nullptr);
        const CardEffectView base = card_effect_steps(*d, 0);
        const CardEffectView up = card_effect_steps(*d, 1);
        ASSERT_EQ(base.count, 1);
        EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::DRAW));
        EXPECT_EQ(base.steps[0].amount, 3);
        ASSERT_EQ(up.count, 1);
        EXPECT_TRUE(StepOpIs(up.steps[0].op, Opcode::DRAW));
        EXPECT_EQ(up.steps[0].amount, 4);
    }
    // Sadistic Nature: APPLY_POWER SADISTIC 5 base / 7 upgraded.
    {
        const CardDef* d = card_def(CardId::SADISTIC_NATURE);
        ASSERT_NE(d, nullptr);
        const CardEffectView base = card_effect_steps(*d, 0);
        const CardEffectView up = card_effect_steps(*d, 1);
        ASSERT_EQ(base.count, 1);
        EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::APPLY_POWER));
        EXPECT_EQ(base.steps[0].amount, 5);
        EXPECT_EQ(base.steps[0].extra, make_apply_power_flags(PowerId::SADISTIC));
        ASSERT_EQ(up.count, 1);
        EXPECT_EQ(up.steps[0].amount, 7);
    }
    // Thinking Ahead: DRAW 2 then a guarded put-on-draw-top CHOOSE_CARD, both
    // rows (the amount never moves -- it is a literal 2, not a magicNumber).
    {
        const CardDef* d = card_def(CardId::THINKING_AHEAD);
        ASSERT_NE(d, nullptr);
        for (uint8_t up = 0; up < 2; ++up) {
            const CardEffectView v = card_effect_steps(*d, up);
            ASSERT_EQ(v.count, 2);
            EXPECT_TRUE(StepOpIs(v.steps[0].op, Opcode::DRAW));
            EXPECT_EQ(v.steps[0].amount, 2);
            EXPECT_TRUE(StepOpIs(v.steps[1].op, Opcode::CHOOSE_CARD));
            EXPECT_EQ(v.steps[1].amount, 1);
            EXPECT_EQ(choose_kind_from_flags(v.steps[1].extra),
                      ChoiceKind::PUT_ON_DRAW_TOP);
            EXPECT_TRUE(choose_queue_guard_hand_nonempty(v.steps[1].extra));
        }
    }
}

// ===========================================================================
// Master of Strategy -- draw 3/4, exhausts
// ===========================================================================

TEST(CardColorlessRaresMasterOfStrategy, DrawsAndExhausts) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        for (int i = 0; i < 6; ++i) {
            AddDrawTop(s, CardId::STRIKE);
        }
        const CardPoolIndex mos = AddHand(s, CardId::MASTER_OF_STRATEGY, up);
        Play(s, 0);
        EXPECT_EQ(s.hand_count, up == 0 ? 3 : 4);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, mos));
    }
}

// ===========================================================================
// Sadistic Nature -- applies SADISTIC 5/7, and the Shackled exclusion fix
// ===========================================================================

TEST(CardColorlessRaresSadisticNature, AppliesSadisticStacks) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::SADISTIC_NATURE, up);
        Play(s, 0);
        const PowerSlot* sad = FindPlayerPower(s, PowerId::SADISTIC);
        ASSERT_NE(sad, nullptr);
        EXPECT_EQ(sad->amount, up == 0 ? 5 : 7);
    }
}

ActionQueueItem ApplyPowerItem(uint8_t src, uint8_t tgt, PowerId id,
                               int32_t stacks) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    it.src = src;
    it.tgt = tgt;
    it.amount = stacks;
    it.flags = make_apply_power_flags(id);
    return it;
}

TEST(CardColorlessRaresSadisticNature, FiresOnAnOrdinaryDebuff) {
    // Baseline: Sadistic still fires for an ordinary DEBUFF (WEAK), matching
    // the pre-existing PowerHooks.SadisticFiresWhenPlayerDebuffsUnprotectedTarget
    // coverage -- this file's Shackled test only makes sense read against this.
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::SADISTIC, 5);
    execute_opcode(s, ApplyPowerItem(kActorPlayer, 0, PowerId::WEAK, 2));
    ASSERT_NE(FindMonsterPower(s, 0, PowerId::WEAK), nullptr);
    ASSERT_EQ(s.action_count, 1);
    EXPECT_EQ(s.action_queue[s.action_head].opcode,
              static_cast<uint16_t>(Opcode::DAMAGE));
    EXPECT_EQ(s.action_queue[s.action_head].amount, 5);
}

TEST(CardColorlessRaresSadisticNature, ShackledDoesNotTriggerSadisticDamage) {
    // MANDATORY VERIFICATION finding: SadisticPower.onApplyPower
    // (SadisticPower.java:41-46) excludes `power.ID.equals("Shackled")` --
    // Dark Shackles' own debuff must never trigger the wielder's OWN Sadistic
    // stack. power_sadistic.cpp predates PowerId::SHACKLED (B3.10b) and was
    // missing this exclusion; this test is RED against the pre-fix native
    // body (it would see SHACKLED as an ordinary DEBUFF, applied to a
    // different, Artifact-less creature, and wrongly queue THORNS damage) and
    // GREEN after the fix added in this stage.
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::SADISTIC, 5);
    execute_opcode(s, ApplyPowerItem(kActorPlayer, 0, PowerId::SHACKLED, 9));
    const PowerSlot* shackled = FindMonsterPower(s, 0, PowerId::SHACKLED);
    ASSERT_NE(shackled, nullptr) << "Shackled itself still lands normally";
    EXPECT_EQ(shackled->amount, 9);
    EXPECT_EQ(s.action_count, 0)
        << "Sadistic must NOT queue THORNS damage for a Shackled application";
}

TEST(CardColorlessRaresSadisticNature, DarkShacklesCardDealsNoSadisticDamage) {
    // The same fact through the actual card interaction the brief names:
    // Dark Shackles applies Shackled + Strength(-amount) to the target; with
    // Sadistic up, neither queued APPLY_POWER may trigger Sadistic THORNS.
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/100);
    AddPlayerPower(s, PowerId::SADISTIC, 5);
    AddHand(s, CardId::DARK_SHACKLES);
    Play(s, 0, /*target=*/0);
    ASSERT_NE(FindMonsterPower(s, 0, PowerId::SHACKLED), nullptr);
    ASSERT_NE(FindMonsterPower(s, 0, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindMonsterPower(s, 0, PowerId::STRENGTH)->amount, -9);
    EXPECT_EQ(s.monsters[0].hp, 100)
        << "no Sadistic THORNS damage from either Dark Shackles debuff";
}

// ===========================================================================
// Thinking Ahead -- draw 2, queue-time-guarded put-back
// ===========================================================================

TEST(CardColorlessRaresThinkingAhead, NonEmptyHandOffersAPutBackChoice) {
    // Hand pre-play is [ThinkingAhead, Strike]: the guard reads hand_count
    // right after ThinkingAhead moves to limbo (Strike remains), so it is
    // non-empty and the CHOOSE_CARD step IS queued. After DRAW 2 the hand has
    // 3 candidates (Strike + 2 drawn), so 3 > need(1): a real prompt opens.
    CombatState s = MakeCombat();
    for (int i = 0; i < 4; ++i) {
        AddDrawTop(s, CardId::DEFEND);
    }
    AddHand(s, CardId::THINKING_AHEAD);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_FALSE(m.choice_from_discard);
    ASSERT_EQ(s.hand_count, 3);
    // Put the (still-unupgraded) Strike back on the draw top.
    uint8_t strike_slot = kHandCap;
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.hand[i] == strike) {
            strike_slot = i;
        }
    }
    ASSERT_LT(strike_slot, kHandCap);
    Step(s, make_action(ActionVerb::CHOOSE, strike_slot));

    EXPECT_EQ(s.hand_count, 2) << "2 drew in, 1 put back out";
    ASSERT_GT(s.draw_count, 0);
    EXPECT_EQ(s.draw[s.draw_count - 1], strike)
        << "the chosen card is on top of the draw pile";
}

TEST(CardColorlessRaresThinkingAhead,
     HandPlayAlwaysOffersTheChoiceEvenWithNoOtherCardsInHand) {
    // MANDATORY VERIFICATION correction against the original brief, which
    // claimed the played card is already out of hand at the guard's read: it
    // is not. AbstractPlayer.useCard (AbstractPlayer.java:1358-1384) runs
    // c.use() at :1369 and only removes the card from hand.group at :1374,
    // hand.removeCard(c) -- AFTER use() returns. So for an ORDINARY HAND PLAY
    // Thinking Ahead is STILL a hand.group member when its own use() reads
    // hand.size() (:34): hand.size() counts ITSELF, so the guard can never
    // see 0 there and the put-back is unconditionally offered -- even when
    // Thinking Ahead is the only card in hand (hand.size() == 1, not 0). Both
    // upgrade rows behave the same way; only the filing pile (exhaust vs
    // discard) differs.
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddDrawTop(s, CardId::DEFEND);
        AddDrawTop(s, CardId::DEFEND);
        const CardPoolIndex ta = AddHand(s, CardId::THINKING_AHEAD, up);

        Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

        ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER))
            << "the guard never blocks a hand play -- a real choice opens "
               "even though nothing else was in hand";
        ActionMask m{};
        legal_actions(s, m);
        ASSERT_TRUE(m.choice_pending);
        ASSERT_EQ(s.hand_count, 2) << "the 2 drawn Defends";

        Step(s, make_action(ActionVerb::CHOOSE, 0));

        EXPECT_EQ(s.hand_count, 1);
        ASSERT_GT(s.draw_count, 0);
        if (up == 0) {
            EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, ta))
                << "base row keeps exhaust";
        } else {
            EXPECT_TRUE(PileHas(s.discard, s.discard_count, ta))
                << "upgrade() clears exhaust (ThinkingAhead.java:46)";
        }
    }
}

TEST(CardColorlessRaresThinkingAhead,
     AutoplayOffTheDrawTopWithAnEmptyHandSkipsThePutBackEntirely) {
    // The guard's ONE reachable case today: Havoc (PLAY_TOP_DRAW,
    // PlayTopCardAction.java) autoplays the top of the draw pile WITHOUT ever
    // adding the card to hand.group -- op_play_top_draw's limbo_add puts the
    // instance straight into the limbo pile, matching that. If Thinking Ahead
    // is the card on top and the player's hand is otherwise empty (Havoc was
    // the only hand card, and it has already left hand by the time Thinking
    // Ahead's own effects are queued -- move_played_card_to_limbo runs before
    // Havoc's queued PLAY_TOP_DRAW action even executes), hand_count is
    // genuinely 0 at Thinking Ahead's own guard check: the put-back step is
    // never queued, and only the 2-card draw happens -- no CHOOSE prompt.
    CombatState s = MakeCombat();
    for (int i = 0; i < 4; ++i) {
        AddDrawTop(s, CardId::DEFEND);
    }
    const CardPoolIndex ta = AddDrawTop(s, CardId::THINKING_AHEAD);  // draw top
    AddHand(s, CardId::HAVOC);

    Play(s, 0, 0);

    EXPECT_EQ(s.hand_count, 2) << "Thinking Ahead's 2 Defends -- no put-back";
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, ta))
        << "Havoc's exhaustOnUseOnce forces the autoplayed card to exhaust "
           "regardless of its own (base) exhaust flag";
    // WAITING_ON_USER is the ordinary idle "your turn" phase (MakeCombat's own
    // starting value), not evidence of a pending choice by itself -- check
    // legal_actions' choice_pending instead.
    ActionMask m{};
    legal_actions(s, m);
    EXPECT_FALSE(m.choice_pending) << "no put-back choice was ever queued";
}

TEST(CardColorlessRaresThinkingAhead,
     EmptyHandAndEmptyDeckIsAlsoANoOpTheOtherWay) {
    // A SEPARATE, ordinary no-op: hand play, but the draw pile AND discard
    // are also empty, so DRAW itself pulls nothing and the put-back's
    // eligible count is 0 at EXECUTE time (the forced-with-0-eligible path,
    // unrelated to the queue-time guard -- the guard does not block a hand
    // play at all, see the test above). Still resolves as a single Play()
    // with no prompt because a forced (eligible <= need) CHOOSE_CARD never
    // requires one.
    CombatState s = MakeCombat();
    const CardPoolIndex ta = AddHand(s, CardId::THINKING_AHEAD);

    Play(s, 0);

    EXPECT_EQ(s.hand_count, 0);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, ta));
}

// ===========================================================================
// Apotheosis / UPGRADE_ALL -- hand, drawPile, discardPile, exhaustPile, in
// that order; canUpgrade gates each member; the played card is never touched.
// ===========================================================================

TEST(CardColorlessRaresApotheosis, UpgradesEveryEligibleCardInAllFourPiles) {
    CombatState s = MakeCombat();
    const CardPoolIndex apo = AddHand(s, CardId::APOTHEOSIS);
    // A second Apotheosis sitting in hand IS upgraded (it is not the played
    // instance -- only the one instance in limbo is exempt).
    const CardPoolIndex apo2 = AddHand(s, CardId::APOTHEOSIS);
    const CardPoolIndex searing = AddHand(s, CardId::SEARING_BLOW, /*upgrade=*/1);
    const CardPoolIndex draw_strike = AddDrawTop(s, CardId::STRIKE);
    const CardPoolIndex already_up = AddDiscard(s, CardId::DEFEND, /*upgrade=*/1);
    const CardPoolIndex status = AddDiscard(s, CardId::WOUND);
    const CardPoolIndex curse = AddDiscard(s, CardId::REGRET);
    const CardPoolIndex ex_bash = AddExhaust(s, CardId::BASH);

    Play(s, 0);

    // Statuses/curses skipped (canUpgrade: type == STATUS/CURSE -> false).
    EXPECT_EQ(s.card_pool[status].upgrade, 0);
    EXPECT_EQ(s.card_pool[curse].upgrade, 0);
    // Already-upgraded skipped (canUpgrade: otherwise !upgraded).
    EXPECT_EQ(s.card_pool[already_up].upgrade, 1);
    // Searing Blow upgraded even though already upgraded -- its canUpgrade
    // override is unconditional `true` (SearingBlow.java:58-60) -- so its
    // upgrade COUNT climbs by one more, to 2.
    EXPECT_EQ(s.card_pool[searing].upgrade, 2);
    // Ordinary eligible cards in each of the four piles get upgraded.
    EXPECT_EQ(s.card_pool[apo2].upgrade, 1) << "hand";
    EXPECT_EQ(s.card_pool[draw_strike].upgrade, 1) << "drawPile";
    EXPECT_EQ(s.card_pool[ex_bash].upgrade, 1) << "exhaustPile";
    // The played Apotheosis itself is in limbo throughout and is never
    // touched -- it now sits in the exhaust pile (base row carries exhaust),
    // still at upgrade 0.
    EXPECT_EQ(s.card_pool[apo].upgrade, 0);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, apo));
}

TEST(CardColorlessRaresApotheosis, UpgradedRowCostsOneAndHasTheSameProgram) {
    const CardDef* d = card_def(CardId::APOTHEOSIS);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->base_cost, 2);
    EXPECT_EQ(d->upgraded_cost, 1);
}

// ===========================================================================
// PutOnDeckAction's FORCED (no-screen) path spends cardRandomRng
// ===========================================================================
//
// PutOnDeckAction.update (PutOnDeckAction.java:33-54) clamps
// `amount = min(amount, hand.size())` (:37-39) and opens the hand-select screen
// only while `hand.group.size() > amount` (:45-49). The remaining branch --
// "you were asked for at least your whole hand", so nothing is actually chosen
// -- is nevertheless
//
//     for (i = 0; i < this.p.hand.size(); ++i)
//         this.p.hand.moveToDeck(this.p.hand.getRandomCard(cardRandomRng), false);
//
// and getRandomCard(rng) is group.get(rng.random(size - 1)) (CardGroup.java:
// 498-500): ONE cardRandomRng draw per moved card, spent even though the
// outcome is forced. The sim used to bill NOTHING there, which is an rng-stream
// divergence for every later draw in the combat. The three tests below pin the
// corrected accounting from both consumers' sides -- Warcry and Thinking Ahead
// -- and pin that the SCREEN path still bills nothing (:56-62 moves the
// selected cards with no rng at all).
//
// The billing is PER-CHOICE-KIND, not blanket: ArmamentsAction's forced path
// upgrades its single candidate with no rng, and every other kind is backed by
// its own action.

TEST(CardPutOnDeckForcedPath, WarcryWithOneOtherHandCardBillsExactlyOneDraw) {
    // hand.size() == 1 at the action (Warcry itself is in limbo by then), so
    // amount clamps to 1, 1 > 1 is false, and the forced loop runs exactly one
    // iteration: one getRandomCard(cardRandomRng) over a one-card hand -- a
    // draw whose result cannot change anything.
    CombatState s = MakeCombat();
    AddHand(s, CardId::WARCRY);
    const CardPoolIndex other = AddHand(s, CardId::STRIKE);
    const int32_t before = s.card_random_rng.counter;

    Play(s, 0);

    EXPECT_EQ(s.card_random_rng.counter - before, 1)
        << "the forced put-on-deck still spends one cardRandomRng draw";
    EXPECT_EQ(s.hand_count, 0);
    ASSERT_EQ(s.draw_count, 1);
    EXPECT_EQ(s.draw[s.draw_count - 1], other);
}

TEST(CardPutOnDeckForcedPath, EmptyHandBillsNothing) {
    // hand.size() == 0 -> the forced loop runs zero iterations. The Java's
    // "0 cards -> 0 draws" end of the same rule.
    CombatState s = MakeCombat();
    AddHand(s, CardId::WARCRY);
    const int32_t before = s.card_random_rng.counter;

    Play(s, 0);

    EXPECT_EQ(s.card_random_rng.counter, before);
    EXPECT_EQ(s.draw_count, 0);
}

TEST(CardPutOnDeckForcedPath, PromptedScreenPathBillsNothing) {
    // Two other hand cards -> hand.group.size() (2) > amount (1): the screen
    // opens (:45-49) and the action returns. Its second half (:56-62) then
    // moveToDeck's the player's selection with NO rng.
    CombatState s = MakeCombat();
    AddHand(s, CardId::WARCRY);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::DEFEND);
    const int32_t before = s.card_random_rng.counter;

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_EQ(s.card_random_rng.counter, before) << "opening the screen is free";

    Step(s, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(s.card_random_rng.counter, before)
        << "the user-selected path moves the card with no rng";
    EXPECT_EQ(s.draw_count, 1);
}

TEST(CardColorlessRaresThinkingAhead, ForcedPutBackBillsOneCardRandomDraw) {
    // The stage-A tests pinned Thinking Ahead's PILES but not its RNG, so the
    // divergence above was invisible from this card too. It is the same forced
    // PutOnDeckAction: draw 2 (only one card is there to draw), leaving a
    // one-card hand, then the put-back with amount 1 -- forced, one draw.
    CombatState s = MakeCombat();
    AddDrawTop(s, CardId::DEFEND);
    const CardPoolIndex ta = AddHand(s, CardId::THINKING_AHEAD);
    const int32_t before = s.card_random_rng.counter;

    Play(s, 0);

    EXPECT_EQ(s.card_random_rng.counter - before, 1);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.draw_count, 1) << "the drawn Defend went straight back";
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, ta));
}

// ===========================================================================
// Secret Technique (121) / Secret Weapon (122) -- the type-filtered
// draw-pile-to-hand choice
// ===========================================================================

TEST(CardColorlessRaresDeckToHand, RegistryRowsAndPrograms) {
    struct Row {
        CardId id;
        int raw_id;
        CardType filter;
    };
    const std::array<Row, 2> rows{{
        {CardId::SECRET_TECHNIQUE, 121, CardType::SKILL},
        {CardId::SECRET_WEAPON, 122, CardType::ATTACK},
    }};
    for (const Row& r : rows) {
        EXPECT_EQ(static_cast<int>(r.id), r.raw_id);
        const CardDef* d = card_def(r.id);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->base_cost, 0);
        EXPECT_EQ(d->upgraded_cost, 0);
        EXPECT_EQ(d->type, CardType::SKILL);
        EXPECT_EQ(d->target_kind, CardTargetKind::NONE);
        EXPECT_FALSE(d->needs_target);
        EXPECT_FALSE(d->random_target);
        // upgrade() sets exhaust = false and NOTHING else (:52-60), so the
        // upgraded flag row is empty and the two programs are identical.
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_FALSE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
        // canUse's draw-pile predicate rides the CardDef column, the same shape
        // Clash's requires_all_attacks uses.
        EXPECT_EQ(d->requires_draw_pile_type, static_cast<uint8_t>(r.filter));
        for (uint8_t up = 0; up < 2; ++up) {
            const CardEffectView v = card_effect_steps(*d, up);
            ASSERT_EQ(v.count, 1);
            EXPECT_TRUE(StepOpIs(v.steps[0].op, Opcode::CHOOSE_CARD));
            EXPECT_EQ(v.steps[0].amount, 1);
            EXPECT_EQ(choose_kind_from_flags(v.steps[0].extra),
                      ChoiceKind::DRAW_TO_HAND);
            EXPECT_EQ(choice_source(ChoiceKind::DRAW_TO_HAND),
                      ChoiceSource::DRAW);
            EXPECT_EQ(choose_type_filter_from_flags(v.steps[0].extra),
                      static_cast<uint8_t>(r.filter));
            EXPECT_FALSE(choose_is_random(v.steps[0].extra));
        }
    }
    // Every other card must keep the "no such predicate" sentinel -- a raw
    // CardType spelling would have made ATTACK (0) the default.
    EXPECT_EQ(card_def(CardId::VIOLENCE)->requires_draw_pile_type,
              kNoDrawPileType);
    EXPECT_EQ(card_def(CardId::STRIKE)->requires_draw_pile_type,
              kNoDrawPileType);
}

TEST(CardColorlessRaresDeckToHand, UnplayableWithoutAMatchingCardInTheDrawPile) {
    // SecretTechnique.canUse / SecretWeapon.canUse (:35-50) walk the draw pile
    // and refuse when nothing matches. The gate is the centralized
    // card_can_use_without_target, so it shows up in the public ActionMask.
    CombatState s = MakeCombat();
    AddDrawTop(s, CardId::STRIKE);  // an ATTACK, and nothing else
    AddHand(s, CardId::SECRET_TECHNIQUE);
    AddHand(s, CardId::SECRET_WEAPON);

    ActionMask m{};
    legal_actions(s, m);
    EXPECT_FALSE(m.can_play[0]) << "no SKILL in the draw pile";
    EXPECT_TRUE(m.can_play[1]) << "a Strike IS an ATTACK in the draw pile";

    // Adding a SKILL to the draw pile unlocks the first one.
    AddDrawTop(s, CardId::DEFEND);
    legal_actions(s, m);
    EXPECT_TRUE(m.can_play[0]);
    EXPECT_TRUE(m.can_play[1]);

    // A SKILL in HAND or DISCARD does not satisfy it -- the scan is the draw
    // pile only.
    CombatState t = MakeCombat();
    AddDrawTop(t, CardId::STRIKE);
    AddDiscard(t, CardId::DEFEND);
    AddHand(t, CardId::SECRET_TECHNIQUE);
    AddHand(t, CardId::DEFEND);
    ActionMask tm{};
    legal_actions(t, tm);
    EXPECT_FALSE(tm.can_play[0]);
}

TEST(CardColorlessRaresDeckToHand, TempBuildBillsExactlyKMinusOneDraws) {
    // CardGroup.addToRandomSpot (CardGroup.java:463-469): the FIRST insert lands
    // in an empty group and is a plain `group.add(c)` with NO rng; every later
    // insert is one `cardRandomRng.random(size - 1)`. So k matching cards cost
    // exactly k-1 draws, and the cost is paid whether or not a screen opens
    // (Skill/AttackFromDeckToHandAction build the list on their first update
    // tick, :34-39, before any of the size branches).
    //
    // k == 0: unreachable behind canUse from a hand play, but reachable when an
    // autoplay's revalidation races the pile -- queue the choice directly.
    {
        CombatState s = MakeCombat();
        AddDrawTop(s, CardId::STRIKE);
        AddDrawTop(s, CardId::STRIKE);
        const int32_t before = s.card_random_rng.counter;
        QueueDrawToHand(s, CardType::SKILL);
        Pump(s);
        EXPECT_EQ(s.card_random_rng.counter, before) << "k == 0 -> 0 draws";
        EXPECT_EQ(s.hand_count, 0) << "silent no-op (:40-43)";
        EXPECT_EQ(s.draw_count, 2) << "the draw pile is untouched";
    }
    // k == 1: still 0 draws, and the single match is auto-taken with NO screen.
    {
        CombatState s = MakeCombat();
        AddDrawTop(s, CardId::STRIKE);
        const CardPoolIndex only_skill = AddDrawTop(s, CardId::DEFEND);
        AddDrawTop(s, CardId::STRIKE);
        AddHand(s, CardId::SECRET_TECHNIQUE);
        const int32_t before = s.card_random_rng.counter;

        Play(s, 0);

        EXPECT_EQ(s.card_random_rng.counter, before) << "k == 1 -> k-1 == 0";
        ActionMask m{};
        legal_actions(s, m);
        EXPECT_FALSE(m.choice_pending) << "one match is taken with no screen";
        ASSERT_EQ(s.hand_count, 1);
        EXPECT_EQ(s.hand[0], only_skill);
        EXPECT_EQ(s.draw_count, 2);
    }
    // k == 3: 2 draws, and 3 > amount(1) opens a real prompt.
    {
        CombatState s = MakeCombat();
        AddDrawTop(s, CardId::DEFEND);
        AddDrawTop(s, CardId::STRIKE);
        AddDrawTop(s, CardId::SHRUG_IT_OFF);
        AddDrawTop(s, CardId::DEFEND);
        AddHand(s, CardId::SECRET_TECHNIQUE);
        const int32_t before = s.card_random_rng.counter;

        Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

        EXPECT_EQ(s.card_random_rng.counter - before, 2) << "k == 3 -> k-1 == 2";
        ActionMask m{};
        legal_actions(s, m);
        EXPECT_TRUE(m.choice_pending);
    }
}

TEST(CardColorlessRaresDeckToHand, TheTempBuildIsBilledExactlyOnceWhileBlocked) {
    // A blocked item stays at the queue head and is re-examined by every pump
    // step; the latch bit is what keeps the build's cost at k-1 rather than
    // k-1 per step.
    CombatState s = MakeCombat();
    for (int i = 0; i < 3; ++i) {
        AddDrawTop(s, CardId::DEFEND);
    }
    AddHand(s, CardId::SECRET_TECHNIQUE);
    const int32_t before = s.card_random_rng.counter;

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    ASSERT_EQ(s.card_random_rng.counter - before, 2);
    for (int i = 0; i < 5; ++i) {
        Pump(s);  // still blocked, must not re-bill
    }
    EXPECT_EQ(s.card_random_rng.counter - before, 2);
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    Step(s, make_action(ActionVerb::CHOOSE, 0));
    EXPECT_EQ(s.card_random_rng.counter - before, 2)
        << "resolving the selection costs nothing more";
}

TEST(CardColorlessRaresDeckToHand, MaskIsAMandatorySingleDrawPilePick) {
    // gridSelectScreen.open(tmp, amount, TEXT[0], false)
    // (SkillFromDeckToHandAction.java:65) reaches GridCardSelectScreen.open's
    // 7-arg form with anyNumber == false and, in COMBAT, no cancel button
    // (:446-448 shows it only for upgrade/transform/purge/shop screens). There
    // is therefore no "pick zero" and no "cancel" spelling at all: the mask
    // offers CHOOSE over the eligible slots and nothing else.
    CombatState s = MakeCombat();
    const CardPoolIndex bottom_skill = AddDrawTop(s, CardId::DEFEND);
    const CardPoolIndex attack = AddDrawTop(s, CardId::STRIKE);
    const CardPoolIndex top_skill = AddDrawTop(s, CardId::SHRUG_IT_OFF);
    AddHand(s, CardId::SECRET_TECHNIQUE);
    AddHand(s, CardId::STRIKE);  // a second hand card, to prove plays are off

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));

    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_TRUE(m.choice_from_draw);
    EXPECT_FALSE(m.choice_from_discard);
    EXPECT_FALSE(m.choice_from_exhaust);
    EXPECT_FALSE(m.choice_from_generated);
    EXPECT_FALSE(m.can_end_turn) << "no way to decline the pick";
    for (int i = 0; i < kHandCap; ++i) {
        EXPECT_FALSE(m.can_play[i]) << "slot " << i;
    }
    // arg0 indexes the DRAW pile, and only the SKILLs are eligible.
    ASSERT_EQ(s.draw_count, 3);
    EXPECT_TRUE(m.can_choose[0]) << "draw slot 0 == the Defend";
    EXPECT_FALSE(m.can_choose[1]) << "draw slot 1 == the Strike (wrong type)";
    EXPECT_TRUE(m.can_choose[2]) << "draw slot 2 == the Shrug It Off";
    EXPECT_FALSE(choice_slot_eligible(s, 1, ChoiceKind::DRAW_TO_HAND,
                                      static_cast<uint8_t>(CardType::SKILL)));
    // An illegal selection (the wrong-type slot) is a documented no-op that
    // leaves the prompt open -- the pick really is mandatory.
    Step(s, make_action(ActionVerb::CHOOSE, 1));
    ActionMask m2{};
    legal_actions(s, m2);
    EXPECT_TRUE(m2.choice_pending);
    EXPECT_EQ(s.hand_count, 1) << "nothing was taken";

    Step(s, make_action(ActionVerb::CHOOSE, 2));
    ActionMask m3{};
    legal_actions(s, m3);
    EXPECT_FALSE(m3.choice_pending);
    ASSERT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.hand[1], top_skill) << "hand.addToTop == append";
    // Only the chosen card left the draw pile; the rest keep their order --
    // the browse list the game randomizes is a throwaway copy.
    ASSERT_EQ(s.draw_count, 2);
    EXPECT_EQ(s.draw[0], bottom_skill);
    EXPECT_EQ(s.draw[1], attack);
}

TEST(CardColorlessRaresDeckToHand, SecretWeaponFiltersAttacksAndAutoTakesOne) {
    CombatState s = MakeCombat();
    AddDrawTop(s, CardId::DEFEND);
    const CardPoolIndex only_attack = AddDrawTop(s, CardId::STRIKE);
    AddDrawTop(s, CardId::DEFEND);
    const CardPoolIndex sw = AddHand(s, CardId::SECRET_WEAPON);

    Play(s, 0);

    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], only_attack);
    EXPECT_EQ(s.draw_count, 2);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, sw))
        << "the base row exhausts";
}

TEST(CardColorlessRaresDeckToHand, UpgradedCopyDoesNotExhaust) {
    for (const auto& [id, filler] :
         std::array<std::pair<CardId, CardId>, 2>{{
             {CardId::SECRET_TECHNIQUE, CardId::DEFEND},
             {CardId::SECRET_WEAPON, CardId::STRIKE},
         }}) {
        CombatState s = MakeCombat();
        AddDrawTop(s, filler);
        const CardPoolIndex pi = AddHand(s, id, /*upgrade=*/1);

        Play(s, 0);

        EXPECT_TRUE(PileHas(s.discard, s.discard_count, pi))
            << "upgrade() clears exhaust and nothing else";
        EXPECT_FALSE(PileHas(s.exhaust, s.exhaust_count, pi));
        EXPECT_EQ(s.hand_count, 1) << "the single match was still taken";
    }
}

TEST(CardColorlessRaresDeckToHand, FullHandSendsTheTakenCardToTheDiscard) {
    // SkillFromDeckToHandAction.java:46-48 (auto-take) and :72-74 (prompted):
    // at hand.size() == 10 the card is drawPile.moveToDiscardPile'd instead of
    // added to the hand. It is consumed either way. Queued directly, because a
    // hand PLAY of Secret Technique frees a hand slot before its own action
    // runs, so it can never see a full hand.
    CombatState s = MakeCombat();
    const CardPoolIndex skill = AddDrawTop(s, CardId::DEFEND);
    AddDrawTop(s, CardId::STRIKE);
    for (int i = 0; i < kHandCap; ++i) {
        AddHand(s, CardId::STRIKE);
    }
    ASSERT_EQ(s.hand_count, kHandCap);

    QueueDrawToHand(s, CardType::SKILL);
    Pump(s);

    EXPECT_EQ(s.hand_count, kHandCap) << "the hand did not grow";
    EXPECT_TRUE(PileHas(s.discard, s.discard_count, skill));
    EXPECT_EQ(s.draw_count, 1) << "it still LEFT the draw pile";
}

// ===========================================================================
// Violence (126) / DRAW_PILE_FETCH -- the dual-stream deck raid
// ===========================================================================

TEST(CardColorlessRaresViolence, RegistryRowAndProgram) {
    EXPECT_EQ(static_cast<int>(CardId::VIOLENCE), 126);
    const CardDef* d = card_def(CardId::VIOLENCE);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->base_cost, 0);
    EXPECT_EQ(d->upgraded_cost, 0);
    EXPECT_EQ(d->type, CardType::SKILL);
    EXPECT_EQ(d->target_kind, CardTargetKind::NONE);
    EXPECT_FALSE(d->needs_target);
    EXPECT_FALSE(d->random_target);
    // upgrade() is upgradeMagicNumber(1) only -- exhaust survives on BOTH rows.
    EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
    EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
    const CardEffectView base = card_effect_steps(*d, 0);
    const CardEffectView up = card_effect_steps(*d, 1);
    ASSERT_EQ(base.count, 1);
    EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::DRAW_PILE_FETCH));
    EXPECT_EQ(base.steps[0].amount, 3);
    EXPECT_EQ(draw_pile_fetch_type_from_flags(base.steps[0].extra),
              static_cast<uint8_t>(CardType::ATTACK));
    ASSERT_EQ(up.count, 1);
    EXPECT_TRUE(StepOpIs(up.steps[0].op, Opcode::DRAW_PILE_FETCH));
    EXPECT_EQ(up.steps[0].amount, 4) << "upgradeMagicNumber(1) -> 4";
    EXPECT_EQ(draw_pile_fetch_type_from_flags(up.steps[0].extra),
              static_cast<uint8_t>(CardType::ATTACK));
}

TEST(CardColorlessRaresViolence, DualStreamAccountingAcrossTheWhiffCases) {
    struct Case {
        const char* name;
        int attacks;
        int skills;
        int amount;
        int expect_card_random;  // temp build: k-1, or 0 when the build never ran
        int expect_shuffle;      // one per NON-EMPTY iteration
        int expect_taken;
    };
    // k == 0 with an EMPTY draw pile short-circuits at :33-36 BEFORE the build,
    // so it spends nothing; k == 0 with a NON-empty draw pile runs the build
    // (which for zero matches costs nothing) and stops at :42-45. The two zeros
    // have different causes, so both are pinned.
    const std::array<Case, 6> cases{{
        {"empty draw pile", 0, 0, 3, 0, 0, 0},
        {"no attacks at all", 0, 3, 3, 0, 0, 0},
        {"k == 1", 1, 2, 3, 0, 1, 1},
        {"k < N", 2, 1, 3, 1, 2, 2},
        {"k == N", 3, 0, 3, 2, 3, 3},
        {"k > N", 5, 1, 3, 4, 3, 3},
    }};
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        CombatState s = MakeCombat();
        for (int i = 0; i < c.attacks; ++i) {
            AddDrawTop(s, CardId::STRIKE);
        }
        for (int i = 0; i < c.skills; ++i) {
            AddDrawTop(s, CardId::DEFEND);
        }
        s.card_random_rng = from_seed(77);
        s.shuffle_rng = from_seed(88);
        const int32_t c_before = s.card_random_rng.counter;
        const int32_t s_before = s.shuffle_rng.counter;

        execute_opcode(s, DrawPileFetchItem(c.amount, CardType::ATTACK));

        EXPECT_EQ(s.card_random_rng.counter - c_before, c.expect_card_random)
            << "addToRandomSpot billing";
        EXPECT_EQ(s.shuffle_rng.counter - s_before, c.expect_shuffle)
            << "one CardGroup.shuffle() per non-empty iteration";
        EXPECT_EQ(s.hand_count, c.expect_taken);
        EXPECT_EQ(s.draw_count, c.attacks + c.skills - c.expect_taken);
        for (uint8_t i = 0; i < s.hand_count; ++i) {
            EXPECT_EQ(s.card_pool[s.hand[i]].card_id,
                      static_cast<uint16_t>(CardId::STRIKE))
                << "only ATTACKs are ever taken";
        }
    }
}

TEST(CardColorlessRaresViolence, SeededPickOrderMatchesAHandDerivedOracle) {
    // The one case where the temp list's ORDER is observable: Violence shuffles
    // it and takes the BOTTOM card, so the pick sequence depends on both the
    // addToRandomSpot insertion positions (cardRandomRng) and the per-iteration
    // Collections.shuffle (shuffleRng -> a JDK LCG).
    //
    // The oracle below is built from the golden-tested primitives ONLY, in the
    // order the Java performs them:
    //   1. walk drawPile.group front to back (== s.draw[0..n-1], index 0 being
    //      the bottom); for each ATTACK, addToRandomSpot into `tmp` -- the first
    //      is a free append, each later one is inserted at
    //      cardRandomRng.random(tmp.size() - 1) (an INCLUSIVE bound, so the
    //      index range is [0, size-1] and a card is never appended past the end
    //      once the group is non-empty);
    //   2. per iteration: seed a java.util.Random with one
    //      shuffleRng.randomLong(), Collections.shuffle `tmp` with it, take
    //      tmp.get(0) (getBottomCard) and remove it.
    CombatState s = MakeCombat();
    std::vector<CardPoolIndex> draw_order;
    for (const CardId c : {CardId::STRIKE, CardId::DEFEND, CardId::BASH,
                           CardId::SHRUG_IT_OFF, CardId::POMMEL_STRIKE,
                           CardId::STRIKE}) {
        draw_order.push_back(AddDrawTop(s, c));
    }
    const CardPoolIndex vio = AddHand(s, CardId::VIOLENCE);
    s.card_random_rng = from_seed(20260726);
    s.shuffle_rng = from_seed(31415);

    // -- the oracle --
    RngStream cprobe = from_seed(20260726);
    std::vector<CardPoolIndex> tmp;
    for (const CardPoolIndex pi : draw_order) {
        const CardDef* d = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (d == nullptr || d->type != CardType::ATTACK) {
            continue;
        }
        if (tmp.empty()) {
            tmp.push_back(pi);
            continue;
        }
        const int32_t pos =
            random(cprobe, static_cast<int32_t>(tmp.size()) - 1);
        tmp.insert(tmp.begin() + pos, pi);
    }
    ASSERT_EQ(tmp.size(), 4u) << "Strike, Bash, Pommel Strike, Strike";
    ASSERT_EQ(cprobe.counter, 3) << "k - 1 == 3 addToRandomSpot draws";
    RngStream sprobe = from_seed(31415);
    std::vector<CardPoolIndex> expect_taken;
    for (int i = 0; i < 3; ++i) {
        JdkRandom r(random_long(sprobe));
        jdk_shuffle(std::span<CardPoolIndex>(tmp), r);
        expect_taken.push_back(tmp.front());
        tmp.erase(tmp.begin());
    }

    Play(s, 0);

    EXPECT_EQ(s.card_random_rng.counter, 3);
    EXPECT_EQ(s.shuffle_rng.counter, 3);
    ASSERT_EQ(s.hand_count, 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s.hand[i], expect_taken[static_cast<size_t>(i)])
            << "pick " << i;
    }
    // The three unchosen cards keep their original relative draw-pile order:
    // only the throwaway browse list was ever shuffled.
    ASSERT_EQ(s.draw_count, 3);
    std::vector<CardPoolIndex> expect_draw;
    for (const CardPoolIndex pi : draw_order) {
        if (!PileHas(s.hand, s.hand_count, pi)) {
            expect_draw.push_back(pi);
        }
    }
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        EXPECT_EQ(s.draw[i], expect_draw[i]) << "draw slot " << int{i};
    }
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, vio));
}

TEST(CardColorlessRaresViolence, UpgradedFetchesFourAndWhiffsAreStillLegal) {
    // No canUse override on Violence (Violence.java has none), so it is legal
    // with a draw pile that cannot pay out -- and simply does nothing.
    {
        CombatState s = MakeCombat();
        for (int i = 0; i < 5; ++i) {
            AddDrawTop(s, CardId::STRIKE);
        }
        AddHand(s, CardId::VIOLENCE, /*upgrade=*/1);
        Play(s, 0);
        EXPECT_EQ(s.hand_count, 4) << "upgradeMagicNumber(1) -> 4";
        EXPECT_EQ(s.draw_count, 1);
    }
    {
        CombatState s = MakeCombat();
        AddDrawTop(s, CardId::DEFEND);  // no ATTACK anywhere
        const CardPoolIndex vio = AddHand(s, CardId::VIOLENCE);
        ActionMask m{};
        legal_actions(s, m);
        EXPECT_TRUE(m.can_play[0]) << "Violence has no canUse gate";
        const int32_t c_before = s.card_random_rng.counter;
        const int32_t s_before = s.shuffle_rng.counter;
        Play(s, 0);
        EXPECT_EQ(s.hand_count, 0);
        EXPECT_EQ(s.draw_count, 1);
        EXPECT_EQ(s.card_random_rng.counter, c_before);
        EXPECT_EQ(s.shuffle_rng.counter, s_before);
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, vio))
            << "a whiffed Violence still exhausts";
    }
}

TEST(CardColorlessRaresViolence, FullHandSendsEveryPickToTheDiscard) {
    // DrawPileToHandAction.java:51-55 -- at hand.size() == 10 each taken card is
    // moveToDiscardPile'd out of the draw pile and the loop CONTINUES, so the
    // shuffle draws are spent all the same.
    CombatState s = MakeCombat();
    for (int i = 0; i < 4; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    for (int i = 0; i < kHandCap; ++i) {
        AddHand(s, CardId::DEFEND);
    }
    s.card_random_rng = from_seed(5);
    s.shuffle_rng = from_seed(6);
    const int32_t c_before = s.card_random_rng.counter;
    const int32_t s_before = s.shuffle_rng.counter;

    execute_opcode(s, DrawPileFetchItem(3, CardType::ATTACK));

    EXPECT_EQ(s.card_random_rng.counter - c_before, 3) << "k == 4 -> k-1 == 3";
    EXPECT_EQ(s.shuffle_rng.counter - s_before, 3);
    EXPECT_EQ(s.hand_count, kHandCap) << "the hand did not grow";
    EXPECT_EQ(s.discard_count, 3);
    EXPECT_EQ(s.draw_count, 1) << "all three still left the draw pile";
}

// ===========================================================================
// Pool membership: the colorless combat pool grows by exactly these three
// ===========================================================================

TEST(CardColorlessRaresRegistry, AllSevenJoinTheColorlessCombatPool) {
    auto pool_has = [](CardId id) {
        for (const CardId c : kColorlessCombatPool) {
            if (c == id) {
                return true;
            }
        }
        return false;
    };
    for (const CardId id : {CardId::APOTHEOSIS, CardId::MASTER_OF_STRATEGY,
                            CardId::SADISTIC_NATURE, CardId::SECRET_TECHNIQUE,
                            CardId::SECRET_WEAPON, CardId::THINKING_AHEAD,
                            CardId::VIOLENCE}) {
        EXPECT_TRUE(pool_has(id)) << "COLORLESS + RARE + non-HEALING";
    }
    // Colour-gated pools stay RED-only.
    for (const CardId id : kIroncladCombatPool) {
        EXPECT_NE(id, CardId::VIOLENCE);
    }
}

}  // namespace
}  // namespace sts::engine
