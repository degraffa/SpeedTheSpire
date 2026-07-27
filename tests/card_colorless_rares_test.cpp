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
// B3.11 stage D adds Hand of Greed, Panache and The Bomb -- the last three
// colorless RARE rows -- plus the DAMAGE_GREED opcode, PowerIds PANACHE /
// THE_BOMB, and the schema 5 -> 6 machinery those two powers needed: the
// PowerSlot second number (`counter`), INSTANCED (non-merging) powers, and the
// CombatState combat-gold accumulator. The load-bearing cases there are
// Panache's two independently-moving numbers (one shared countdown, a summed
// damage), The Bomb's per-instance fuses ticking and detonating independently,
// and Hand of Greed banking gold only on a kill.
//
// B3.11 stage C adds the generated-card family -- Chrysalis, Magnetism, Mayhem,
// Metamorphosis, Transmutation -- plus the RANDOM_CARD_TO_DRAW opcode, the two
// new RANDOM_COLORLESS_TO_HAND flag bits and PowerIds MAYHEM / MAGNETISM. The
// load-bearing cases there are Chrysalis's stream ORDER (all N pool rolls, THEN
// all N random-spot inserts) and the permanent-vs-this-turn cost split between
// Chrysalis and Transmutation.
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
#include "sts/engine/power_hooks.hpp"  // dispatch_at_start_of_turn (Mayhem/Magnetism)
#include "sts/engine/powers.hpp"
#include "sts/engine/relics.hpp"       // RelicId (Chemical X)
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
        PowerSlot{static_cast<uint16_t>(id), amount, 0, 0};
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

// Resolve exactly what is on the action queue, with no turn machinery around
// it: the isolation the power-sweep cases need (the Combust precedent,
// card_uncommon_powers_test).
void DrainActions(CombatState& s) {
    ActionQueueItem it{};
    while (pop_action_front(s, it)) {
        execute_opcode(s, it);
    }
}

// A whole player end-of-turn through the pump (monster turn + the next
// start-of-turn), for the cases that must cross a real turn boundary.
void EndTurn(CombatState& s) {
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);
}

const PowerSlot* NthPlayerPower(const CombatState& s, PowerId id, int n) {
    int seen = 0;
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id != static_cast<uint16_t>(id)) continue;
        if (seen++ == n) return &s.player_powers[i];
    }
    return nullptr;
}

int CountPlayerPower(const CombatState& s, PowerId id) {
    int n = 0;
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) ++n;
    }
    return n;
}

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

// A bare queued RANDOM_CARD_TO_DRAW, for the accounting cases a hand play
// cannot isolate (the empty-draw-pile free append, the X-cost guard).
ActionQueueItem RandomCardToDrawItem(int amount, CardType type) {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::RANDOM_CARD_TO_DRAW);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    it.amount = amount;
    it.flags = make_random_card_to_draw_flags(static_cast<uint8_t>(type));
    return it;
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
// Registry: the exact twelve-card roster stages A, B and C own between them, in
// library order, with the interior gaps left reserved for the last B3.11 stage.
// ===========================================================================

TEST(CardColorlessRaresRegistry, ExactRosterAndReservedGaps) {
    const std::array<std::pair<int, CardId>, 15> roster{{
        {112, CardId::APOTHEOSIS},
        {113, CardId::CHRYSALIS},
        {114, CardId::HAND_OF_GREED},
        {115, CardId::MAGNETISM},
        {116, CardId::MASTER_OF_STRATEGY},
        {117, CardId::MAYHEM},
        {118, CardId::METAMORPHOSIS},
        {119, CardId::PANACHE},
        {120, CardId::SADISTIC_NATURE},
        {121, CardId::SECRET_TECHNIQUE},
        {122, CardId::SECRET_WEAPON},
        {123, CardId::THE_BOMB},
        {124, CardId::THINKING_AHEAD},
        {125, CardId::TRANSMUTATION},
        {126, CardId::VIOLENCE},
    }};
    for (const auto& [id, card] : roster) {
        EXPECT_EQ(static_cast<int>(card), id);
        const CardDef* d = card_def(card);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->id, card);
    }
    // Stage D FILLED the 114/119/123 reservations the earlier stages left, so
    // the colorless RARE block 112-126 is now CONTIGUOUS -- 15 rows, no gap.
    // The old "these three must have no row" assertion is replaced by the
    // roster above rather than deleted: a reservation nothing asserts is
    // indistinguishable from an omission, and so is a filled gap.
    for (int id = 112; id <= 126; ++id) {
        EXPECT_NE(card_def(static_cast<CardId>(id)), nullptr)
            << "id " << id << " is inside the colorless RARE block and must "
               "have a row";
    }
    // The PowerIds this family owns, pinned next to the CardIds they are
    // applied by. 85-86 are stage D's published RESERVE and stay unissued.
    EXPECT_EQ(static_cast<int>(PowerId::MAYHEM), 81);
    EXPECT_EQ(static_cast<int>(PowerId::MAGNETISM), 82);
    EXPECT_EQ(static_cast<int>(PowerId::PANACHE), 83);
    EXPECT_EQ(static_cast<int>(PowerId::THE_BOMB), 84);
    for (const int reserved : {85, 86}) {
        EXPECT_EQ(power_def(static_cast<PowerId>(reserved)), nullptr)
            << "PowerId " << reserved << " is reserved and must have no row";
    }
    // The opcode this stage owns, and its own published reserve.
    EXPECT_EQ(static_cast<int>(Opcode::DAMAGE_GREED), 57);
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
// Pool membership: the colorless combat pool grows by exactly these rows
// ===========================================================================

TEST(CardColorlessRaresRegistry, AllTwelveJoinTheColorlessCombatPool) {
    auto pool_has = [](CardId id) {
        for (const CardId c : kColorlessCombatPool) {
            if (c == id) {
                return true;
            }
        }
        return false;
    };
    for (const CardId id : {CardId::APOTHEOSIS, CardId::CHRYSALIS,
                            CardId::MAGNETISM, CardId::MASTER_OF_STRATEGY,
                            CardId::MAYHEM, CardId::METAMORPHOSIS,
                            CardId::SADISTIC_NATURE, CardId::SECRET_TECHNIQUE,
                            CardId::SECRET_WEAPON, CardId::THINKING_AHEAD,
                            CardId::TRANSMUTATION, CardId::VIOLENCE}) {
        EXPECT_TRUE(pool_has(id)) << "COLORLESS + RARE + non-HEALING";
    }
    // Colour-gated pools stay RED-only -- including the SKILL pool this stage
    // added: Chrysalis and Transmutation are COLORLESS SKILLs and must not be in
    // it, and Metamorphosis must not be in the ATTACK pool.
    for (const CardId id : kIroncladCombatPool) {
        EXPECT_NE(id, CardId::VIOLENCE);
    }
    for (const CardId id : kIroncladSkillPool) {
        EXPECT_NE(id, CardId::CHRYSALIS);
        EXPECT_NE(id, CardId::TRANSMUTATION);
    }
    for (const CardId id : kIroncladAttackPool) {
        EXPECT_NE(id, CardId::METAMORPHOSIS);
    }
}

// ===========================================================================
// STAGE C -- the generated-card family: Chrysalis, Magnetism, Mayhem,
// Metamorphosis, Transmutation, plus opcode RANDOM_CARD_TO_DRAW and the two
// new RANDOM_COLORLESS_TO_HAND flag bits.
// ===========================================================================

TEST(CardColorlessRaresStageC, CostsTypesFlagsAndTargeting) {
    // Chrysalis / Metamorphosis: cost 2 at BOTH levels (upgrade() only moves
    // the magicNumber) and exhaust at BOTH levels.
    for (const CardId id : {CardId::CHRYSALIS, CardId::METAMORPHOSIS}) {
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::SKILL);
        EXPECT_EQ(d->target_kind, CardTargetKind::NONE);
        EXPECT_FALSE(d->needs_target);
        EXPECT_EQ(card_cost(*d, 0), 2);
        EXPECT_EQ(card_cost(*d, 1), 2) << "upgrade() is upgradeMagicNumber only";
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
        EXPECT_FALSE(has_card_flag(card_flags(*d, 0), CardFlag::XCOST));
    }
    // Magnetism / Mayhem: POWER, SELF, cost 2 -> 1, never exhaust.
    for (const CardId id : {CardId::MAGNETISM, CardId::MAYHEM}) {
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::POWER);
        EXPECT_EQ(d->target_kind, CardTargetKind::SELF);
        EXPECT_FALSE(d->needs_target);
        EXPECT_EQ(card_cost(*d, 0), 2);
        EXPECT_EQ(card_cost(*d, 1), 1) << "upgrade() is upgradeBaseCost(1) only";
        EXPECT_FALSE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_FALSE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
    }
    // Transmutation: X-cost SKILL, SELF, and the exhaust flag SURVIVES the
    // upgrade (Transmutation.upgrade only rewrites the description).
    {
        const CardDef* d = card_def(CardId::TRANSMUTATION);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::SKILL);
        EXPECT_EQ(d->target_kind, CardTargetKind::SELF);
        EXPECT_FALSE(d->needs_target);
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::XCOST));
        EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::XCOST));
        EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
        EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST))
            << "upgrade() does NOT clear exhaust (Transmutation.java:43-50)";
    }
}

TEST(CardColorlessRaresStageC, EffectProgramsBaseAndUpgraded) {
    struct Gen {
        CardId id;
        CardType type;
    };
    for (const Gen& g : std::array<Gen, 2>{{{CardId::CHRYSALIS, CardType::SKILL},
                                            {CardId::METAMORPHOSIS,
                                             CardType::ATTACK}}}) {
        const CardDef* d = card_def(g.id);
        ASSERT_NE(d, nullptr);
        const CardEffectView base = card_effect_steps(*d, 0);
        const CardEffectView up = card_effect_steps(*d, 1);
        ASSERT_EQ(base.count, 1);
        ASSERT_EQ(up.count, 1);
        EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::RANDOM_CARD_TO_DRAW));
        EXPECT_TRUE(StepOpIs(up.steps[0].op, Opcode::RANDOM_CARD_TO_DRAW));
        EXPECT_EQ(base.steps[0].amount, 3);
        EXPECT_EQ(up.steps[0].amount, 5) << "upgradeMagicNumber(2)";
        EXPECT_EQ(random_card_to_draw_type_from_flags(base.steps[0].extra),
                  static_cast<uint8_t>(g.type));
        EXPECT_EQ(random_card_to_draw_type_from_flags(up.steps[0].extra),
                  static_cast<uint8_t>(g.type));
    }
    // Magnetism / Mayhem: one APPLY_POWER of 1 stack, identical on both rows.
    struct Pow {
        CardId card;
        PowerId power;
    };
    for (const Pow& p : std::array<Pow, 2>{{{CardId::MAGNETISM,
                                             PowerId::MAGNETISM},
                                            {CardId::MAYHEM, PowerId::MAYHEM}}}) {
        const CardDef* d = card_def(p.card);
        ASSERT_NE(d, nullptr);
        for (uint8_t up = 0; up < 2; ++up) {
            const CardEffectView v = card_effect_steps(*d, up);
            ASSERT_EQ(v.count, 1);
            EXPECT_TRUE(StepOpIs(v.steps[0].op, Opcode::APPLY_POWER));
            EXPECT_EQ(v.steps[0].amount, 1) << "no upgradeMagicNumber";
            EXPECT_EQ(v.steps[0].extra, make_apply_power_flags(p.power));
            EXPECT_EQ(v.steps[0].target, StepTarget::SELF);
        }
    }
    // Transmutation: ONE colorless-to-hand per X repetition, cost-zero-for-turn
    // on both rows and the upgraded-copy bit only on the upgraded one.
    {
        const CardDef* d = card_def(CardId::TRANSMUTATION);
        ASSERT_NE(d, nullptr);
        const CardEffectView base = card_effect_steps(*d, 0);
        const CardEffectView up = card_effect_steps(*d, 1);
        ASSERT_EQ(base.count, 1);
        ASSERT_EQ(up.count, 1);
        EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::RANDOM_COLORLESS_TO_HAND));
        EXPECT_EQ(base.steps[0].amount, 1) << "one per repetition (Whirlwind shape)";
        EXPECT_EQ(base.steps[0].extra, kColorlessToHandCostZeroForTurn);
        EXPECT_EQ(up.steps[0].amount, 1);
        EXPECT_EQ(up.steps[0].extra,
                  kColorlessToHandCostZeroForTurn | kColorlessToHandUpgradedCopy);
    }
}

// The two new opcode-52 flag bits default to 0, so the rows that predate them
// are byte-identical. Asserted, not assumed -- that is the whole justification
// for extending opcode 52 rather than spending reserve opcode 58.
TEST(CardColorlessRaresStageC, JackOfAllTradesExtraStaysZero) {
    const CardDef* d = card_def(CardId::JACK_OF_ALL_TRADES);
    ASSERT_NE(d, nullptr);
    for (uint8_t up = 0; up < 2; ++up) {
        const CardEffectView v = card_effect_steps(*d, up);
        ASSERT_EQ(v.count, 1);
        EXPECT_TRUE(StepOpIs(v.steps[0].op, Opcode::RANDOM_COLORLESS_TO_HAND));
        EXPECT_EQ(v.steps[0].extra, 0u)
            << "neither new bit may leak onto Jack of All Trades";
    }
}

// The emitted SKILL pool is the ATTACK pool's sibling: same RED + non-BASIC +
// non-HEALING membership rule, one CardType changed, and disjoint from it.
TEST(CardColorlessRaresStageC, IroncladSkillPoolShape) {
    EXPECT_GT(kIroncladSkillPoolCount, 0);
    for (const CardId id : kIroncladSkillPool) {
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr) << "every pool member is a registry row";
        EXPECT_EQ(d->type, CardType::SKILL);
        for (const CardId a : kIroncladAttackPool) {
            EXPECT_NE(a, id) << "the two type pools are disjoint";
        }
        bool in_combat_pool = false;
        for (const CardId c : kIroncladCombatPool) {
            in_combat_pool = in_combat_pool || c == id;
        }
        EXPECT_TRUE(in_combat_pool)
            << "the type pool is a subsequence of the full RED combat pool";
    }
    // Order-equivalence with the full RED combat pool: Java's type filter
    // preserves the concatenated pools' relative order, so the SKILL pool must
    // be exactly the SKILL SUBSEQUENCE of kIroncladCombatPool, in order.
    {
        int k = 0;
        for (const CardId c : kIroncladCombatPool) {
            const CardDef* d = card_def(c);
            if (d != nullptr && d->type == CardType::SKILL) {
                ASSERT_LT(k, kIroncladSkillPoolCount);
                EXPECT_EQ(kIroncladSkillPool[static_cast<unsigned>(k)], c);
                ++k;
            }
        }
        EXPECT_EQ(k, kIroncladSkillPoolCount);
    }
    // The BASIC rows never enter srcCommon/srcUncommon/srcRare.
    for (const CardId id : kIroncladSkillPool) {
        EXPECT_NE(id, CardId::DEFEND);
        EXPECT_NE(id, CardId::STRIKE);
    }
}

// ---------------------------------------------------------------------------
// Chrysalis / Metamorphosis -- opcode RANDOM_CARD_TO_DRAW
// ---------------------------------------------------------------------------

// THE pin of this stage. Chrysalis.use rolls ALL N cards before ANY of the N
// MakeTempCardInDrawPileActions resolves, so the cardRandomRng stream is
// [roll roll roll][insert insert insert]. The oracle below is built from the
// golden-tested primitives, not from the engine's own path, and it checks BOTH
// halves: which cards were generated AND where each landed.
TEST(CardColorlessRaresChrysalis, SeededStreamIsAllRollsThenAllInserts) {
    CombatState s = MakeCombat();
    // A two-card draw pile so the very first insert is a real random(size-1)
    // draw rather than the free empty-group append.
    const CardPoolIndex bottom = AddDrawTop(s, CardId::STRIKE);
    const CardPoolIndex top = AddDrawTop(s, CardId::DEFEND);
    AddHand(s, CardId::CHRYSALIS);
    s.card_random_rng = from_seed(31337);

    RngStream probe = from_seed(31337);
    // Half 1: the three pool rolls, back to back.
    CardId gen[3]{};
    for (int i = 0; i < 3; ++i) {
        const int32_t pick =
            random(probe, static_cast<int32_t>(kIroncladSkillPoolCount) - 1);
        gen[i] = kIroncladSkillPool[static_cast<unsigned>(pick)];
    }
    // Half 2: the three addToRandomSpot inserts, in queue order, into a pile
    // that grows as they go.
    std::vector<CardId> pile{CardId::STRIKE, CardId::DEFEND};
    for (int i = 0; i < 3; ++i) {
        const int32_t pos =
            random(probe, static_cast<int32_t>(pile.size()) - 1);
        pile.insert(pile.begin() + pos, gen[i]);
    }

    Play(s, 0);

    EXPECT_EQ(s.card_random_rng.counter, probe.counter)
        << "3 pool rolls + 3 insert draws == 6";
    EXPECT_EQ(s.card_random_rng.counter - from_seed(31337).counter, 6);
    ASSERT_EQ(s.draw_count, 5);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(s.card_pool[s.draw[i]].card_id,
                  static_cast<uint16_t>(pile[static_cast<std::size_t>(i)]))
            << "draw pile slot " << i;
    }
    // The two pre-existing instances are still the same pool rows.
    EXPECT_TRUE(PileHas(s.draw, s.draw_count, bottom));
    EXPECT_TRUE(PileHas(s.draw, s.draw_count, top));
}

// The interleaved encoding this opcode exists to avoid: rolling and inserting
// one card at a time consumes the SAME number of draws but in a different
// order, so it produces a different pile. Pinning the difference is what stops
// a future "simplification" from silently reintroducing it.
TEST(CardColorlessRaresChrysalis, InterleavedOrderWouldDifferFromTheJava) {
    RngStream a = from_seed(31337);
    RngStream b = from_seed(31337);
    CardId batched[3]{};
    for (int i = 0; i < 3; ++i) {
        batched[i] = kIroncladSkillPool[static_cast<unsigned>(
            random(a, static_cast<int32_t>(kIroncladSkillPoolCount) - 1))];
    }
    CardId interleaved[3]{};
    int size = 2;
    for (int i = 0; i < 3; ++i) {
        interleaved[i] = kIroncladSkillPool[static_cast<unsigned>(
            random(b, static_cast<int32_t>(kIroncladSkillPoolCount) - 1))];
        (void)random(b, size - 1);
        ++size;
    }
    bool same = true;
    for (int i = 0; i < 3; ++i) {
        same = same && batched[i] == interleaved[i];
    }
    EXPECT_FALSE(same)
        << "if these ever agree the pin above has stopped discriminating";
}

// The zeroing writes BOTH cost and costForTurn (Chrysalis.java:35-38), so it is
// PERMANENT for the combat: no COST_MODIFIED_FOR_TURN marker, and the
// end-of-turn reset sweep leaves it at 0. Contrast Transmutation below.
TEST(CardColorlessRaresChrysalis, GeneratedCopiesAreZeroCostPermanently) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::CHRYSALIS);
    // Seed 78, not 77: the three draw INDICES are seed-determined and unchanged,
    // but the SKILL pool they index is now in CardLibrary library order (the
    // B4.5 capture's recovered order), and 77's three indices happen to land on
    // three zero-cost skills there -- which would make the zero-cost assertion
    // below vacuous. 78 still lands on costed skills, so the pin keeps biting.
    s.card_random_rng = from_seed(78);
    Play(s, 0);
    ASSERT_EQ(s.draw_count, 3);

    int had_a_real_cost = 0;
    for (uint8_t i = 0; i < s.draw_count; ++i) {
        const CardInstance& c = s.card_pool[s.draw[i]];
        const CardDef* d = card_def(static_cast<CardId>(c.card_id));
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::SKILL) << "Chrysalis generates SKILLs only";
        EXPECT_EQ(c.upgrade, 0) << "makeCopy() -- a base library card";
        if (card_cost(*d, 0) > 0) {
            ++had_a_real_cost;
            EXPECT_EQ(c.cost_now, 0);
        } else {
            EXPECT_EQ(c.cost_now, card_cost(*d, 0)) << "the cost > 0 guard";
        }
        EXPECT_FALSE(has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN))
            << "permanent, not this-turn";
    }
    ASSERT_GT(had_a_real_cost, 0) << "this seed must generate a costed card";

    // End the turn: AbstractRoom.endTurn's costForTurn reset touches only
    // COST_MODIFIED_FOR_TURN rows, so these keep their zero.
    const std::array<CardPoolIndex, 3> made{s.draw[0], s.draw[1], s.draw[2]};
    Step(s, make_action(ActionVerb::END_TURN));
    for (const CardPoolIndex pi : made) {
        const CardInstance& c = s.card_pool[pi];
        const CardDef* d = card_def(static_cast<CardId>(c.card_id));
        ASSERT_NE(d, nullptr);
        if (card_cost(*d, 0) > 0) {
            EXPECT_EQ(c.cost_now, 0)
                << "the zero survives the end-of-turn sweep";
        }
    }
}

// `if (card.cost > 0)` -- an X-cost card (Java cost -1, CardFlag::XCOST here)
// fails the guard and stays an X-cost card. The ATTACK pool is where this is
// reachable: Whirlwind is its only X-cost member.
TEST(CardColorlessRaresMetamorphosis, GeneratedXCostCardKeepsItsXCost) {
    int64_t chosen = -1;
    for (int64_t seed = 1; seed <= 4000 && chosen < 0; ++seed) {
        RngStream probe = from_seed(seed);
        const int32_t pick =
            random(probe, static_cast<int32_t>(kIroncladAttackPoolCount) - 1);
        if (kIroncladAttackPool[static_cast<unsigned>(pick)] ==
            CardId::WHIRLWIND) {
            chosen = seed;
        }
    }
    ASSERT_GE(chosen, 0) << "no seed in range generates Whirlwind first";

    CombatState s = MakeCombat();
    s.card_random_rng = from_seed(chosen);
    execute_opcode(s, RandomCardToDrawItem(1, CardType::ATTACK));
    ASSERT_EQ(s.draw_count, 1);
    const CardInstance& c = s.card_pool[s.draw[0]];
    EXPECT_EQ(c.card_id, static_cast<uint16_t>(CardId::WHIRLWIND));
    EXPECT_TRUE(has_card_flag(c.flags, CardFlag::XCOST))
        << "an X-cost generated card is still an X-cost card";
    const CardDef* d = card_def(CardId::WHIRLWIND);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(c.cost_now, card_cost(*d, 0))
        << "the cost > 0 guard left the registry cost alone";
    EXPECT_FALSE(has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN));
}

TEST(CardColorlessRaresMetamorphosis, GeneratesAttacksAndUpgradedGeneratesFive) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        const CardPoolIndex meta = AddHand(s, CardId::METAMORPHOSIS, up);
        s.card_random_rng = from_seed(909 + up);
        Play(s, 0);
        EXPECT_EQ(s.draw_count, up == 0 ? 3 : 5);
        for (uint8_t i = 0; i < s.draw_count; ++i) {
            const CardDef* d =
                card_def(static_cast<CardId>(s.card_pool[s.draw[i]].card_id));
            ASSERT_NE(d, nullptr);
            EXPECT_EQ(d->type, CardType::ATTACK);
        }
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, meta))
            << "exhaust survives the upgrade";
    }
}

// CardGroup.addToRandomSpot's empty-group branch is a plain append that draws
// NOTHING (CardGroup.java:463-465), so the first insert into an empty draw pile
// is free and the second is not.
TEST(CardColorlessRaresChrysalis, EmptyDrawPileMakesTheFirstInsertFree) {
    {
        CombatState s = MakeCombat();
        s.card_random_rng = from_seed(11);
        const int32_t before = s.card_random_rng.counter;
        execute_opcode(s, RandomCardToDrawItem(1, CardType::SKILL));
        EXPECT_EQ(s.card_random_rng.counter - before, 1)
            << "one pool roll, zero insert draws";
        EXPECT_EQ(s.draw_count, 1);
    }
    {
        CombatState s = MakeCombat();
        s.card_random_rng = from_seed(11);
        const int32_t before = s.card_random_rng.counter;
        execute_opcode(s, RandomCardToDrawItem(2, CardType::SKILL));
        EXPECT_EQ(s.card_random_rng.counter - before, 3)
            << "two pool rolls, then a free insert and a real one";
        EXPECT_EQ(s.draw_count, 2);
    }
}

// ---------------------------------------------------------------------------
// Magnetism -- PowerId 82, the pre-draw colorless generator
// ---------------------------------------------------------------------------

// The pool draws happen WHILE THE HOOK RUNS (returnTrulyRandomColorlessCardIn-
// Combat is an argument of the queued action's constructor), and only the card
// creation is deferred. Both halves are pinned here.
TEST(CardColorlessRaresMagnetism, RollsAtHookTimeAndCreatesOnePerStack) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAGNETISM, 2);
    s.card_random_rng = from_seed(4242);

    RngStream probe = from_seed(4242);
    CardId expected[2]{};
    for (int i = 0; i < 2; ++i) {
        const int32_t pick = random(
            probe, static_cast<int32_t>(kColorlessCombatPoolCount) - 1);
        expected[i] = kColorlessCombatPool[static_cast<unsigned>(pick)];
    }

    dispatch_at_start_of_turn(s);
    EXPECT_EQ(s.card_random_rng.counter, probe.counter)
        << "both stacks rolled during the hook walk, before any queue drain";
    EXPECT_EQ(s.hand_count, 0) << "only the CREATION is deferred";
    EXPECT_EQ(s.action_count, 2);

    Pump(s);
    ASSERT_EQ(s.hand_count, 2);
    EXPECT_EQ(s.card_random_rng.counter, probe.counter)
        << "the queued creations consume no rng";
    for (int i = 0; i < 2; ++i) {
        const CardInstance& c = s.card_pool[s.hand[i]];
        EXPECT_EQ(c.card_id, static_cast<uint16_t>(expected[i]));
        const CardDef* d = card_def(expected[i]);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(c.upgrade, 0) << "makeCopy() -- a base library card";
        EXPECT_EQ(c.cost_now, card_cost(*d, 0)) << "Magnetism re-costs nothing";
        EXPECT_FALSE(has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN));
    }
}

// applyStartOfTurnPowers is the PRE-DRAW phase (GameActionManager.java:345),
// queued ahead of the turn's DrawCardAction, so the generated card is in hand
// before the five drawn ones.
TEST(CardColorlessRaresMagnetism, CardArrivesBeforeTheTurnsDraw) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAGNETISM, 1);
    for (int i = 0; i < 8; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    s.card_random_rng = from_seed(88);
    RngStream probe = from_seed(88);
    const CardId expected = kColorlessCombatPool[static_cast<unsigned>(random(
        probe, static_cast<int32_t>(kColorlessCombatPoolCount) - 1))];

    Step(s, make_action(ActionVerb::END_TURN));

    ASSERT_EQ(s.hand_count, 6) << "1 generated + the 5-card draw";
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id, static_cast<uint16_t>(expected))
        << "the generated card is in hand BEFORE the draw resolves";
    for (uint8_t i = 1; i < s.hand_count; ++i) {
        EXPECT_EQ(s.card_pool[s.hand[i]].card_id,
                  static_cast<uint16_t>(CardId::STRIKE));
    }
}

// MakeTempCardInHandAction.update:71-77 -- past the 10-card cap the copies are
// created directly in the DISCARD pile.
TEST(CardColorlessRaresMagnetism, FullHandCreatesInTheDiscardPile) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAGNETISM, 2);
    for (int i = 0; i < kHandCap; ++i) {
        AddHand(s, CardId::DEFEND);
    }
    s.card_random_rng = from_seed(5150);
    dispatch_at_start_of_turn(s);
    Pump(s);
    EXPECT_EQ(s.hand_count, kHandCap) << "the hand did not grow";
    EXPECT_EQ(s.discard_count, 2) << "both copies were created in the discard";
}

// The areMonstersBasicallyDead gate (MagnetismPower.java:32): nothing at all --
// no rolls, no queued creations.
TEST(CardColorlessRaresMagnetism, DeadMonstersSuppressTheWholeHook) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAGNETISM, 3);
    s.monsters[0].hp = 0;
    s.card_random_rng = from_seed(9);
    const int32_t before = s.card_random_rng.counter;
    dispatch_at_start_of_turn(s);
    EXPECT_EQ(s.card_random_rng.counter, before);
    EXPECT_EQ(s.action_count, 0);
    Pump(s);
    EXPECT_EQ(s.hand_count, 0);
}

// ---------------------------------------------------------------------------
// Mayhem -- PowerId 81, the pre-draw play-the-top-card generator
// ---------------------------------------------------------------------------

TEST(CardColorlessRaresMayhem, TopCardIsPlayedBeforeTheTurnsDraw) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAYHEM, 1);
    for (int i = 0; i < 8; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    const CardPoolIndex top = AddDrawTop(s, CardId::DEFEND);  // played, not drawn
    s.card_random_rng = from_seed(2024);

    Step(s, make_action(ActionVerb::END_TURN));

    EXPECT_FALSE(PileHas(s.hand, s.hand_count, top))
        << "the played card is not among the five drawn";
    EXPECT_TRUE(PileHas(s.discard, s.discard_count, top))
        << "exhausts = FALSE -- Mayhem files the card normally (contrast Havoc)";
    EXPECT_FALSE(PileHas(s.exhaust, s.exhaust_count, top));
    EXPECT_GT(s.player_block, 0) << "the Defend actually resolved";
    EXPECT_EQ(s.cards_played_this_turn, 1);
}

TEST(CardColorlessRaresMayhem, TwoStacksPlayTwoCardsInDrawOrder) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAYHEM, 2);
    const CardPoolIndex second = AddDrawTop(s, CardId::DEFEND);
    const CardPoolIndex first = AddDrawTop(s, CardId::STRIKE);  // top of pile
    s.card_random_rng = from_seed(606);
    const int32_t before = s.card_random_rng.counter;

    dispatch_at_start_of_turn(s);
    EXPECT_EQ(s.card_random_rng.counter, before)
        << "the target roll is deferred to the queued action, not the hook";
    EXPECT_EQ(s.action_count, 2);
    Pump(s);

    EXPECT_EQ(s.card_random_rng.counter - before, 2)
        << "exactly one getRandomMonster draw per play";
    EXPECT_EQ(s.draw_count, 0);
    ASSERT_EQ(s.discard_count, 2);
    EXPECT_EQ(s.discard[0], first) << "top card first";
    EXPECT_EQ(s.discard[1], second);
    EXPECT_LT(s.monsters[0].hp, 100) << "the Strike hit";
    EXPECT_GT(s.player_block, 0) << "the Defend blocked";
    EXPECT_EQ(s.cards_played_this_turn, 2);
}

// An unplayable status on top: the dequeue-time canUse revalidation refuses it,
// the no-trigger UseCardAction files it to the discard, and it does not count
// as played.
TEST(CardColorlessRaresMayhem, UnplayableTopCardIsFiledWithNoTriggers) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAYHEM, 1);
    const CardPoolIndex wound = AddDrawTop(s, CardId::WOUND);
    ASSERT_TRUE(has_card_flag(s.card_pool[wound].flags, CardFlag::UNPLAYABLE));
    s.card_random_rng = from_seed(31);
    const int16_t hp_before = s.monsters[0].hp;

    dispatch_at_start_of_turn(s);
    Pump(s);

    EXPECT_TRUE(PileHas(s.discard, s.discard_count, wound));
    EXPECT_FALSE(PileHas(s.exhaust, s.exhaust_count, wound));
    EXPECT_EQ(s.cards_played_this_turn, 0)
        << "a refused autoplay never reaches ++cardsPlayedThisTurn";
    EXPECT_EQ(s.monsters[0].hp, hp_before);
    EXPECT_EQ(s.player_block, 0);
}

// PlayTopCardAction.update:38-43 -- an empty draw pile queues an
// EmptyDeckShuffleAction (ONE shuffle_rng draw) and retries.
TEST(CardColorlessRaresMayhem, EmptyDrawReshufflesTheDiscardThenPlays) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAYHEM, 1);
    const CardPoolIndex only = AddDiscard(s, CardId::DEFEND);
    s.card_random_rng = from_seed(17);
    s.shuffle_rng = from_seed(18);
    const int32_t c_before = s.card_random_rng.counter;
    const int32_t sh_before = s.shuffle_rng.counter;

    dispatch_at_start_of_turn(s);
    Pump(s);

    EXPECT_EQ(s.card_random_rng.counter - c_before, 1) << "one target roll";
    EXPECT_EQ(s.shuffle_rng.counter - sh_before, 1) << "one reshuffle";
    EXPECT_TRUE(PileHas(s.discard, s.discard_count, only))
        << "reshuffled in, played, and filed back to the discard";
    EXPECT_GT(s.player_block, 0);
}

// PlayTopCardAction.update:34-37 -- deckSize + discardSize == 0 ends the action
// immediately. The target roll still happens (it is the constructor argument,
// evaluated first), and nothing else does.
TEST(CardColorlessRaresMayhem, BothPilesEmptyIsACleanNoOp) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::MAYHEM, 1);
    s.card_random_rng = from_seed(23);
    s.shuffle_rng = from_seed(24);
    const int32_t c_before = s.card_random_rng.counter;
    const int32_t sh_before = s.shuffle_rng.counter;

    dispatch_at_start_of_turn(s);
    Pump(s);

    EXPECT_EQ(s.card_random_rng.counter - c_before, 1);
    EXPECT_EQ(s.shuffle_rng.counter - sh_before, 0);
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.discard_count, 0);
    EXPECT_EQ(s.exhaust_count, 0);
    EXPECT_EQ(s.cards_played_this_turn, 0);
}

// ---------------------------------------------------------------------------
// Transmutation -- the X-cost colorless generator
// ---------------------------------------------------------------------------

// TransmutationAction.java:43 -- `if (effect > 0)` guards BOTH the loop and the
// energy spend, so X == 0 is a total no-op.
TEST(CardColorlessRaresTransmutation, ZeroEnergyGeneratesNothingAndSpendsNothing) {
    CombatState s = MakeCombat(/*energy=*/0);
    const CardPoolIndex tx = AddHand(s, CardId::TRANSMUTATION);
    s.card_random_rng = from_seed(3);
    const int32_t before = s.card_random_rng.counter;
    Play(s, 0);
    EXPECT_EQ(s.card_random_rng.counter, before) << "no pool draws at all";
    EXPECT_EQ(s.hand_count, 0);
    EXPECT_EQ(s.player_energy, 0);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, tx));
}

TEST(CardColorlessRaresTransmutation, XThreeGeneratesThreeAndZeroesEnergy) {
    CombatState s = MakeCombat(/*energy=*/3);
    AddHand(s, CardId::TRANSMUTATION);
    s.card_random_rng = from_seed(555);
    RngStream probe = from_seed(555);
    CardId expected[3]{};
    for (int i = 0; i < 3; ++i) {
        expected[i] = kColorlessCombatPool[static_cast<unsigned>(random(
            probe, static_cast<int32_t>(kColorlessCombatPoolCount) - 1))];
    }

    Play(s, 0);

    EXPECT_EQ(s.card_random_rng.counter, probe.counter) << "3 draws, no more";
    ASSERT_EQ(s.hand_count, 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(s.card_pool[s.hand[i]].card_id,
                  static_cast<uint16_t>(expected[i]));
        EXPECT_EQ(s.card_pool[s.hand[i]].cost_now, 0) << "setCostForTurn(0)";
        EXPECT_EQ(s.card_pool[s.hand[i]].upgrade, 0) << "base row is not upgraded";
    }
    EXPECT_EQ(s.player_energy, 0) << "p.energy.use(EnergyPanel.totalCount)";
}

// ChemicalX (BOOST = 2) adds two repetitions without changing the energy spent.
TEST(CardColorlessRaresTransmutation, ChemicalXAddsTwoRepetitions) {
    CombatState s = MakeCombat(/*energy=*/1);
    s.relics[0].relic_id = static_cast<uint16_t>(RelicId::CHEMICAL_X);
    s.relics[0].counter = -1;
    s.relic_count = 1;
    AddHand(s, CardId::TRANSMUTATION);
    s.card_random_rng = from_seed(4);
    Play(s, 0);
    EXPECT_EQ(s.hand_count, 3) << "X == 1 plus the relic's 2";
    EXPECT_EQ(s.player_energy, 0);
}

// `if (this.upgraded) c.upgrade()` -- the copies come out UPGRADED, and because
// the upgrade runs BEFORE setCostForTurn(0) the restored cost next turn is the
// UPGRADED row's.
TEST(CardColorlessRaresTransmutation, UpgradedGeneratesUpgradedCopies) {
    CombatState s = MakeCombat(/*energy=*/2);
    AddHand(s, CardId::TRANSMUTATION, /*upgrade=*/1);
    s.card_random_rng = from_seed(1212);
    Play(s, 0);
    ASSERT_EQ(s.hand_count, 2);
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        const CardInstance& c = s.card_pool[s.hand[i]];
        EXPECT_EQ(c.upgrade, 1) << "every copy is upgraded";
        EXPECT_EQ(c.cost_now, 0);
    }
    // Same seed, BASE row: identical cards, un-upgraded. The only difference
    // between the two rows is the upgrade bit.
    CombatState b = MakeCombat(/*energy=*/2);
    AddHand(b, CardId::TRANSMUTATION, /*upgrade=*/0);
    b.card_random_rng = from_seed(1212);
    Play(b, 0);
    ASSERT_EQ(b.hand_count, 2);
    for (uint8_t i = 0; i < b.hand_count; ++i) {
        EXPECT_EQ(b.card_pool[b.hand[i]].card_id, s.card_pool[s.hand[i]].card_id);
        EXPECT_EQ(b.card_pool[b.hand[i]].upgrade, 0);
    }
}

// setCostForTurn, NOT the permanent zeroing Chrysalis uses: the marker is set
// and AbstractRoom.endTurn's sweep restores the registry cost.
TEST(CardColorlessRaresTransmutation, CopiesAreFreeThisTurnOnly) {
    int64_t chosen = -1;
    for (int64_t seed = 1; seed <= 4000 && chosen < 0; ++seed) {
        RngStream probe = from_seed(seed);
        const CardId id = kColorlessCombatPool[static_cast<unsigned>(random(
            probe, static_cast<int32_t>(kColorlessCombatPoolCount) - 1))];
        const CardDef* d = card_def(id);
        if (d != nullptr && card_cost(*d, 0) > 0 &&
            !has_card_flag(card_flags(*d, 0), CardFlag::XCOST)) {
            chosen = seed;
        }
    }
    ASSERT_GE(chosen, 0) << "no seed in range generates a costed colorless card";

    CombatState s = MakeCombat(/*energy=*/1);
    AddHand(s, CardId::TRANSMUTATION);
    s.card_random_rng = from_seed(chosen);
    Play(s, 0);
    ASSERT_EQ(s.hand_count, 1);
    const CardPoolIndex made = s.hand[0];
    const CardDef* d = card_def(static_cast<CardId>(s.card_pool[made].card_id));
    ASSERT_NE(d, nullptr);
    ASSERT_GT(card_cost(*d, 0), 0);
    EXPECT_EQ(s.card_pool[made].cost_now, 0);
    EXPECT_TRUE(
        has_card_flag(s.card_pool[made].flags, CardFlag::COST_MODIFIED_FOR_TURN))
        << "this-turn only -- the bit Chrysalis's copies must NOT carry";

    Step(s, make_action(ActionVerb::END_TURN));
    EXPECT_EQ(s.card_pool[made].cost_now, card_cost(*d, 0))
        << "costForTurn = cost at end of turn";
    EXPECT_FALSE(
        has_card_flag(s.card_pool[made].flags, CardFlag::COST_MODIFIED_FOR_TURN));
}

// Autoplayed off the draw top (the shape Mayhem produces): the repetition count
// comes from the energy snapshot at dequeue and NOTHING is spent.
TEST(CardColorlessRaresTransmutation, AutoplayUsesTheSnapshotAndSpendsNothing) {
    CombatState s = MakeCombat(/*energy=*/2);
    AddDrawTop(s, CardId::TRANSMUTATION);
    s.card_random_rng = from_seed(64);

    ActionQueueItem play{};
    play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    play.src = kActorPlayer;
    play.tgt = kActorRandomEnemy;
    play.flags = kPlayCardFromDrawTop;
    add_to_bottom(s, play);
    Pump(s);

    EXPECT_EQ(s.hand_count, 2) << "two repetitions from the snapshot";
    EXPECT_EQ(s.player_energy, 2) << "an autoplay spends nothing";
}

TEST(CardColorlessRaresTransmutation, OverflowPastTenIsCreatedInTheDiscard) {
    CombatState s = MakeCombat(/*energy=*/3);
    AddHand(s, CardId::TRANSMUTATION);
    for (int i = 0; i < kHandCap - 1; ++i) {
        AddHand(s, CardId::DEFEND);
    }
    ASSERT_EQ(s.hand_count, kHandCap);
    s.card_random_rng = from_seed(71);
    Play(s, 0);
    EXPECT_EQ(s.hand_count, kHandCap)
        << "the play freed one slot, the first copy refilled it";
    EXPECT_EQ(s.discard_count, 2) << "the other two were created in the discard";
}


// ===========================================================================
// B3.11 stage D -- the schema-6 machinery, exercised on its own before the
// three cards that need it.
// ===========================================================================

// PowerSlot's second number exists, is independent of `amount`, and is 0 for
// every power that declares no meaning for it -- the property that lets every
// pre-schema-6 power keep its semantics unchanged.
TEST(PowerSlotCounter, DefaultsToZeroAndIsIndependentOfAmount) {
    CombatState s = MakeCombat();
    AddPlayerPower(s, PowerId::STRENGTH, 3);
    ASSERT_NE(FindPlayerPower(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPlayerPower(s, PowerId::STRENGTH)->counter, 0);

    // A plain APPLY_POWER authors no counter operand, so an ordinary power's
    // slot stays counter == 0 through apply AND through stacking.
    CombatState t = MakeCombat();
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    it.src = kActorPlayer;
    it.tgt = kActorPlayer;
    it.amount = 2;
    it.flags = make_apply_power_flags(PowerId::STRENGTH);
    EXPECT_EQ(it.flags, static_cast<uint32_t>(PowerId::STRENGTH))
        << "the counter operand defaults to 0, byte-identically";
    add_to_bottom(t, it);
    add_to_bottom(t, it);
    DrainActions(t);
    ASSERT_NE(FindPlayerPower(t, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPlayerPower(t, PowerId::STRENGTH)->amount, 4);
    EXPECT_EQ(FindPlayerPower(t, PowerId::STRENGTH)->counter, 0);
}

// The instance key round-trips, and the "no key" encoding is exactly what every
// pre-existing REDUCE_POWER / REMOVE_POWER item already packed.
TEST(PowerInstanceKey, EncodingRoundTripsAndKeylessIsUnchanged) {
    const uint32_t keyless = make_apply_power_flags(PowerId::THE_BOMB);
    EXPECT_FALSE(power_instance_key_present(keyless));

    const uint32_t keyed = make_power_instance_flags(PowerId::THE_BOMB, 3, 40);
    EXPECT_TRUE(power_instance_key_present(keyed));
    EXPECT_EQ(apply_power_id_from_flags(keyed), PowerId::THE_BOMB);
    EXPECT_EQ(power_instance_amount(keyed), 3);
    EXPECT_EQ(power_instance_counter(keyed), 40);

    // Out-of-range operands cannot be expressed and degrade to a keyless
    // (first-match) item rather than a silently truncated wrong one.
    EXPECT_FALSE(power_instance_key_present(
        make_power_instance_flags(PowerId::THE_BOMB, 300, 40)));
    EXPECT_FALSE(power_instance_key_present(
        make_power_instance_flags(PowerId::THE_BOMB, 3, 900)));
}

// An INSTANCED power's REDUCE_POWER must survive an EARLIER instance being
// removed first -- the exact case a slot INDEX would get wrong, because the
// removal compacts the array before the later queued item resolves.
TEST(PowerInstanceKey, ReduceSurvivesAnEarlierInstanceBeingCompactedAway) {
    CombatState s = MakeCombat();
    // Two bombs, fuse 1 then fuse 3, both 40 damage.
    s.player_powers[0] =
        PowerSlot{static_cast<uint16_t>(PowerId::THE_BOMB), 1, 40, 0};
    s.player_powers[1] =
        PowerSlot{static_cast<uint16_t>(PowerId::THE_BOMB), 3, 40, 0};
    s.player_power_count = 2;

    ActionQueueItem r0{};
    r0.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
    r0.src = kActorPlayer;
    r0.tgt = kActorPlayer;
    r0.amount = 1;
    r0.flags = make_power_instance_flags(PowerId::THE_BOMB, 1, 40);
    ActionQueueItem r1 = r0;
    r1.flags = make_power_instance_flags(PowerId::THE_BOMB, 3, 40);
    add_to_bottom(s, r0);
    add_to_bottom(s, r1);
    DrainActions(s);

    ASSERT_EQ(CountPlayerPower(s, PowerId::THE_BOMB), 1)
        << "the fuse-1 instance was removed at zero";
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 0)->amount, 2)
        << "the fuse-3 instance ticked, even though it moved down a slot";
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 0)->counter, 40);
}

// ===========================================================================
// Panache (119) + PowerId::PANACHE (83)
// ===========================================================================

TEST(CardColorlessRaresPanache, RowAndProgram) {
    const CardDef* d = card_def(CardId::PANACHE);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 0);
    EXPECT_EQ(card_cost(*d, 1), 0);
    EXPECT_EQ(d->type, CardType::POWER);
    EXPECT_EQ(d->target_kind, CardTargetKind::SELF);
    EXPECT_FALSE(d->needs_target);
    EXPECT_EQ(card_flags(*d, 0), 0u);
    EXPECT_EQ(card_flags(*d, 1), 0u);

    for (const uint8_t up : {uint8_t{0}, uint8_t{1}}) {
        const CardEffectView eff = card_effect_steps(*d, up);
        ASSERT_EQ(eff.count, 1);
        EXPECT_TRUE(StepOpIs(eff.steps[0].op, Opcode::APPLY_POWER));
        EXPECT_EQ(eff.steps[0].amount, up == 0 ? 10 : 14);
        // No `counter:` operand -- Panache's two numbers coincide at the Java
        // call site, so the apply path reads `amount` as the damage.
        EXPECT_EQ(eff.steps[0].extra,
                  static_cast<uint32_t>(static_cast<uint16_t>(PowerId::PANACHE)));
    }
}

TEST(CardColorlessRaresPanache, AppliesFiveCardCountdownAndTheDamageCounter) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::PANACHE);
    Play(s);
    const PowerSlot* p = FindPlayerPower(s, PowerId::PANACHE);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->amount, 5) << "CARD_AMT, not the card's magicNumber";
    EXPECT_EQ(p->counter, 10) << "the magicNumber IS the damage";

    CombatState u = MakeCombat();
    AddHand(u, CardId::PANACHE, 1);
    Play(u);
    ASSERT_NE(FindPlayerPower(u, PowerId::PANACHE), nullptr);
    EXPECT_EQ(FindPlayerPower(u, PowerId::PANACHE)->amount, 5);
    EXPECT_EQ(FindPlayerPower(u, PowerId::PANACHE)->counter, 14);
}

// The headline case: the 5th card fires the damage at EVERY enemy.
TEST(CardColorlessRaresPanache, FifthCardHitsAllEnemiesForTheAccumulatedDamage) {
    CombatState s = MakeCombat(/*energy=*/9, /*monster_hp=*/60);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 60;
    s.monsters[1].max_hp = 60;
    AddHand(s, CardId::PANACHE);
    for (int i = 0; i < 5; ++i) {
        AddHand(s, CardId::DEFEND);
    }
    Play(s, 0);  // Panache itself
    ASSERT_NE(FindPlayerPower(s, PowerId::PANACHE), nullptr);
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 5)
        << "a card does not count its own play: the ApplyPowerAction is still "
           "queued when the UseCardAction fan-out for that play runs";

    for (int i = 0; i < 4; ++i) {
        Play(s, 0);
        EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 4 - i)
            << "after " << (i + 1) << " cards";
    }
    EXPECT_EQ(s.monsters[0].hp, 60);
    EXPECT_EQ(s.monsters[1].hp, 60);

    Play(s, 0);  // the 5th
    EXPECT_EQ(s.monsters[0].hp, 50);
    EXPECT_EQ(s.monsters[1].hp, 50);
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 5)
        << "the countdown rolls back to 5";
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->counter, 10)
        << "the damage is not consumed";
}

TEST(CardColorlessRaresPanache, CountdownResetsAtTheStartOfTurn) {
    CombatState s = MakeCombat(/*energy=*/9);
    AddHand(s, CardId::PANACHE);
    AddHand(s, CardId::DEFEND);
    AddHand(s, CardId::DEFEND);
    Play(s, 0);
    Play(s, 0);
    Play(s, 0);
    ASSERT_NE(FindPlayerPower(s, PowerId::PANACHE), nullptr);
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 3);

    dispatch_at_start_of_turn(s);
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 5)
        << "atStartOfTurn is a FLAT reset: partial progress is LOST";
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->counter, 10)
        << "the damage is untouched by the turn boundary";
}

TEST(CardColorlessRaresPanache, TwoPanachesShareOneCountdownAndSumTheDamage) {
    CombatState s = MakeCombat(/*energy=*/9, /*monster_hp=*/60);
    AddHand(s, CardId::PANACHE);
    AddHand(s, CardId::PANACHE);
    for (int i = 0; i < 5; ++i) {
        AddHand(s, CardId::DEFEND);
    }
    Play(s, 0);
    Play(s, 0);
    EXPECT_EQ(CountPlayerPower(s, PowerId::PANACHE), 1)
        << "Panache MERGES -- one slot, not two (contrast The Bomb)";
    const PowerSlot* p = FindPlayerPower(s, PowerId::PANACHE);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->counter, 20) << "stackPower sums the damage";
    EXPECT_EQ(p->amount, 4)
        << "stackPower does NOT touch the countdown -- the FIRST Panache "
           "already counted the SECOND one's play";

    // 4 more cards reaches the 5th since the last reset.
    for (int i = 0; i < 4; ++i) {
        Play(s, 0);
    }
    EXPECT_EQ(s.monsters[0].hp, 40) << "one 20-damage hit, not two 10s";
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 5);
}

TEST(CardColorlessRaresPanache, DamageIgnoresStrengthAndVulnerable) {
    CombatState s = MakeCombat(/*energy=*/9, /*monster_hp=*/60);
    AddPlayerPower(s, PowerId::STRENGTH, 5);
    s.monsters[0].powers[0] =
        PowerSlot{static_cast<uint16_t>(PowerId::VULNERABLE), 3, 0, 0};
    s.monsters[0].power_count = 1;
    AddHand(s, CardId::PANACHE);
    for (int i = 0; i < 5; ++i) {
        AddHand(s, CardId::DEFEND);
    }
    Play(s, 0);
    for (int i = 0; i < 5; ++i) {
        Play(s, 0);
    }
    EXPECT_EQ(s.monsters[0].hp, 50)
        << "createDamageMatrix(damage, true) + THORNS: neither the player's "
           "Strength nor the target's Vulnerable moves the number";
}

TEST(CardColorlessRaresPanache, BlockAbsorbsTheHit) {
    CombatState s = MakeCombat(/*energy=*/9, /*monster_hp=*/60);
    s.monsters[0].block = 4;
    AddHand(s, CardId::PANACHE);
    for (int i = 0; i < 5; ++i) {
        AddHand(s, CardId::DEFEND);
    }
    Play(s, 0);
    for (int i = 0; i < 5; ++i) {
        Play(s, 0);
    }
    EXPECT_EQ(s.monsters[0].block, 0);
    EXPECT_EQ(s.monsters[0].hp, 54)
        << "pure damage is still damage(), not a LoseHP";
}

// A MAYHEM-autoplayed card is a successful play and MUST count; a FAILED
// autoplay takes the dontTriggerOnUseCard filing path and must NOT.
TEST(CardColorlessRaresPanache, AutoplayCountsButAFailedAutoplayFilingDoesNot) {
    CombatState s = MakeCombat(/*energy=*/3);
    AddHand(s, CardId::PANACHE);
    Play(s, 0);
    ASSERT_NE(FindPlayerPower(s, PowerId::PANACHE), nullptr);
    ASSERT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 5);

    // A successful draw-top autoplay (the Mayhem shape).
    AddDrawTop(s, CardId::DEFEND);
    ActionQueueItem play{};
    play.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    play.src = kActorPlayer;
    play.tgt = kActorRandomEnemy;
    play.flags = kPlayCardFromDrawTop;
    add_to_bottom(s, play);
    Pump(s);
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 4)
        << "a Mayhem-autoplayed card is a real play and counts";

    // A FAILED autoplay: an unplayable card off the draw top is cancelled at
    // the dequeue-time canUse revalidation and only FILED, with no onUseCard
    // fan-out (GameActionManager.java:285-301).
    AddDrawTop(s, CardId::WOUND);
    add_to_bottom(s, play);
    Pump(s);
    EXPECT_EQ(FindPlayerPower(s, PowerId::PANACHE)->amount, 4)
        << "a cancelled autoplay's no-trigger filing must NOT decrement";
}

// ===========================================================================
// The Bomb (123) + PowerId::THE_BOMB (84, instanced)
// ===========================================================================

TEST(CardColorlessRaresTheBomb, RowAndProgram) {
    const CardDef* d = card_def(CardId::THE_BOMB);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 2);
    EXPECT_EQ(card_cost(*d, 1), 2) << "upgrade moves only the magicNumber";
    EXPECT_EQ(d->type, CardType::SKILL);
    EXPECT_EQ(d->target_kind, CardTargetKind::SELF) << "SELF, not NONE";
    EXPECT_FALSE(d->needs_target);
    EXPECT_EQ(card_flags(*d, 0), 0u);

    for (const uint8_t up : {uint8_t{0}, uint8_t{1}}) {
        const CardEffectView eff = card_effect_steps(*d, up);
        ASSERT_EQ(eff.count, 1);
        EXPECT_TRUE(StepOpIs(eff.steps[0].op, Opcode::APPLY_POWER));
        EXPECT_EQ(eff.steps[0].amount, 3) << "Java's stack amount is the TURNS";
        EXPECT_EQ(apply_power_id_from_flags(eff.steps[0].extra),
                  PowerId::THE_BOMB);
        EXPECT_EQ(apply_power_counter_from_flags(eff.steps[0].extra),
                  up == 0 ? 40 : 50);
    }
    // The registry marks it instanced; nothing else in the registry is.
    const PowerDef* pd = power_def(PowerId::THE_BOMB);
    ASSERT_NE(pd, nullptr);
    EXPECT_TRUE(pd->instanced);
    EXPECT_FALSE(power_def(PowerId::PANACHE)->instanced);
    EXPECT_FALSE(power_def(PowerId::STRENGTH)->instanced);
}

TEST(CardColorlessRaresTheBomb, ExplodesAtTheEndOfTheThirdTurnForFortyDamage) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/200);
    AddHand(s, CardId::THE_BOMB);
    Play(s, 0);
    const PowerSlot* b = FindPlayerPower(s, PowerId::THE_BOMB);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->amount, 3);
    EXPECT_EQ(b->counter, 40);

    dispatch_at_end_of_turn(s);
    DrainActions(s);
    EXPECT_EQ(FindPlayerPower(s, PowerId::THE_BOMB)->amount, 2);
    EXPECT_EQ(s.monsters[0].hp, 200);

    dispatch_at_end_of_turn(s);
    DrainActions(s);
    EXPECT_EQ(FindPlayerPower(s, PowerId::THE_BOMB)->amount, 1);
    EXPECT_EQ(s.monsters[0].hp, 200);

    dispatch_at_end_of_turn(s);
    DrainActions(s);
    EXPECT_EQ(s.monsters[0].hp, 160) << "the end of the THIRD turn";
    EXPECT_EQ(FindPlayerPower(s, PowerId::THE_BOMB), nullptr)
        << "the instance is removed in the SAME end-of-turn it explodes";
}

TEST(CardColorlessRaresTheBomb, UpgradedExplodesForFifty) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/200);
    AddHand(s, CardId::THE_BOMB, 1);
    Play(s, 0);
    ASSERT_NE(FindPlayerPower(s, PowerId::THE_BOMB), nullptr);
    EXPECT_EQ(FindPlayerPower(s, PowerId::THE_BOMB)->counter, 50);
    for (int t = 0; t < 3; ++t) {
        dispatch_at_end_of_turn(s);
        DrainActions(s);
    }
    EXPECT_EQ(s.monsters[0].hp, 150);
}

TEST(CardColorlessRaresTheBomb,
     TwoBombsTickIndependentlyAndExplodeOnDifferentTurns) {
    CombatState s = MakeCombat(/*energy=*/9, /*monster_hp=*/200);
    AddHand(s, CardId::THE_BOMB);
    AddHand(s, CardId::THE_BOMB);
    Play(s, 0);                      // turn 1
    dispatch_at_end_of_turn(s);
    DrainActions(s);
    Play(s, 0);                      // turn 2: the second bomb
    EXPECT_EQ(CountPlayerPower(s, PowerId::THE_BOMB), 2)
        << "instanced: two slots, never a stack of 2";
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 0)->amount, 2);
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 1)->amount, 3);

    dispatch_at_end_of_turn(s);      // end of turn 2
    DrainActions(s);
    EXPECT_EQ(s.monsters[0].hp, 200);
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 0)->amount, 1);
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 1)->amount, 2);

    dispatch_at_end_of_turn(s);      // end of turn 3: the FIRST bomb only
    DrainActions(s);
    EXPECT_EQ(s.monsters[0].hp, 160);
    ASSERT_EQ(CountPlayerPower(s, PowerId::THE_BOMB), 1);
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 0)->amount, 1)
        << "the survivor ticked correctly even though the exploded instance "
           "was compacted out from in front of it";

    dispatch_at_end_of_turn(s);      // end of turn 4: the SECOND bomb
    DrainActions(s);
    EXPECT_EQ(s.monsters[0].hp, 120);
    EXPECT_EQ(CountPlayerPower(s, PowerId::THE_BOMB), 0);
}

TEST(CardColorlessRaresTheBomb, BaseAndUpgradedCoexistWithDistinctDamage) {
    CombatState s = MakeCombat(/*energy=*/9, /*monster_hp=*/300);
    AddHand(s, CardId::THE_BOMB);
    AddHand(s, CardId::THE_BOMB, 1);
    Play(s, 0);
    Play(s, 0);
    ASSERT_EQ(CountPlayerPower(s, PowerId::THE_BOMB), 2);
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 0)->counter, 40);
    EXPECT_EQ(NthPlayerPower(s, PowerId::THE_BOMB, 1)->counter, 50);
    for (int t = 0; t < 3; ++t) {
        dispatch_at_end_of_turn(s);
        DrainActions(s);
    }
    EXPECT_EQ(s.monsters[0].hp, 210) << "40 + 50, both on the same turn";
    EXPECT_EQ(CountPlayerPower(s, PowerId::THE_BOMB), 0);
}

TEST(CardColorlessRaresTheBomb, NoTickWhileNobodyIsLeftInTheFight) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/200);
    AddHand(s, CardId::THE_BOMB);
    Play(s, 0);
    ASSERT_NE(FindPlayerPower(s, PowerId::THE_BOMB), nullptr);
    s.monsters[0].hp = 0;
    dispatch_at_end_of_turn(s);
    EXPECT_EQ(s.action_count, 0)
        << "areMonstersBasicallyDead gates BOTH queued actions";
    EXPECT_EQ(FindPlayerPower(s, PowerId::THE_BOMB)->amount, 3)
        << "the fuse is FROZEN, not merely silent";

    // An ESCAPED monster is also out of the fight, at full HP.
    CombatState e = MakeCombat(/*energy=*/3, /*monster_hp=*/200);
    AddHand(e, CardId::THE_BOMB);
    Play(e, 0);
    e.monsters[0].flags |= kMonsterFlagEscaped;
    dispatch_at_end_of_turn(e);
    EXPECT_EQ(e.action_count, 0);
    EXPECT_EQ(FindPlayerPower(e, PowerId::THE_BOMB)->amount, 3);
}

TEST(CardColorlessRaresTheBomb, DamageIgnoresStrengthAndVulnerable) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/200);
    AddPlayerPower(s, PowerId::STRENGTH, 7);
    AddHand(s, CardId::THE_BOMB);
    Play(s, 0);
    s.monsters[0].powers[s.monsters[0].power_count] =
        PowerSlot{static_cast<uint16_t>(PowerId::VULNERABLE), 3, 0, 0};
    ++s.monsters[0].power_count;
    for (int t = 0; t < 3; ++t) {
        dispatch_at_end_of_turn(s);
        DrainActions(s);
    }
    EXPECT_EQ(s.monsters[0].hp, 160) << "pure THORNS: exactly 40";
}

TEST(CardColorlessRaresTheBomb, ExplodesThroughARealEndOfTurn) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/200);
    s.player_hp = 300;
    s.player_max_hp = 300;
    AddHand(s, CardId::THE_BOMB);
    Play(s, 0);
    EndTurn(s);
    EndTurn(s);
    EXPECT_EQ(s.monsters[0].hp, 200);
    EndTurn(s);
    EXPECT_EQ(s.monsters[0].hp, 160)
        << "the whole pump turn boundary, not just the isolated sweep";
    EXPECT_EQ(FindPlayerPower(s, PowerId::THE_BOMB), nullptr);
}

// The slot cap is UNCHANGED behaviour, documented rather than invented: a full
// 24-slot list makes an application a silent no-op, and an instanced power
// reaches that the same way any other new slot does.
TEST(CardColorlessRaresTheBomb, ApplicationIntoAFullPowerListIsASilentNoOp) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/200);
    for (int i = 0; i < kPowerCap; ++i) {
        AddPlayerPower(s, PowerId::THE_BOMB, 3);
    }
    ASSERT_EQ(s.player_power_count, kPowerCap);
    AddHand(s, CardId::THE_BOMB);
    Play(s, 0);
    EXPECT_EQ(s.player_power_count, kPowerCap) << "no 25th slot";
    EXPECT_EQ(CountPlayerPower(s, PowerId::THE_BOMB), kPowerCap);
    EXPECT_EQ(s.player_energy, 1) << "the play still happened and still paid";
}

// ===========================================================================
// Hand of Greed (114) + opcode DAMAGE_GREED (57)
// ===========================================================================

TEST(CardColorlessRaresHandOfGreed, RowAndProgram) {
    const CardDef* d = card_def(CardId::HAND_OF_GREED);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 2);
    EXPECT_EQ(card_cost(*d, 1), 2);
    EXPECT_EQ(d->type, CardType::ATTACK);
    EXPECT_EQ(d->target_kind, CardTargetKind::ENEMY);
    EXPECT_TRUE(d->needs_target);
    EXPECT_EQ(card_flags(*d, 0), 0u);
    EXPECT_EQ(card_flags(*d, 1), 0u);

    const CardEffectView base = card_effect_steps(*d, 0);
    ASSERT_EQ(base.count, 1);
    EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::DAMAGE_GREED));
    EXPECT_EQ(base.steps[0].amount, 20);
    EXPECT_EQ(damage_greed_gold_from_flags(base.steps[0].extra), 20);
    const CardEffectView up = card_effect_steps(*d, 1);
    ASSERT_EQ(up.count, 1);
    EXPECT_EQ(up.steps[0].amount, 25);
    EXPECT_EQ(damage_greed_gold_from_flags(up.steps[0].extra), 25);
}

TEST(CardColorlessRaresHandOfGreed,
     LethalHitBanksExactlyTheGoldAndNonLethalBanksNothing) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/50);
    AddHand(s, CardId::HAND_OF_GREED);
    Play(s, 0);
    EXPECT_EQ(s.monsters[0].hp, 30);
    EXPECT_EQ(s.combat_gold, 0) << "a survivor pays nothing";

    CombatState k = MakeCombat(/*energy=*/3, /*monster_hp=*/20);
    AddHand(k, CardId::HAND_OF_GREED);
    Play(k, 0);
    EXPECT_EQ(k.monsters[0].hp, 0) << "hp lands exactly on 0";
    EXPECT_EQ(k.combat_gold, 20);

    CombatState u = MakeCombat(/*energy=*/3, /*monster_hp=*/25);
    AddHand(u, CardId::HAND_OF_GREED, 1);
    Play(u, 0);
    EXPECT_EQ(u.monsters[0].hp, 0);
    EXPECT_EQ(u.combat_gold, 25) << "the upgraded row moves BOTH numbers";
}

TEST(CardColorlessRaresHandOfGreed, BlockAbsorbedNonLethalBanksNothing) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/5);
    s.monsters[0].block = 40;
    AddHand(s, CardId::HAND_OF_GREED);
    Play(s, 0);
    EXPECT_EQ(s.monsters[0].hp, 5) << "block soaked the whole hit";
    EXPECT_EQ(s.monsters[0].block, 20);
    EXPECT_EQ(s.combat_gold, 0);
}

// The pipeline is the ORDINARY one -- the deliberate contrast with Panache and
// The Bomb -- so Strength and Vulnerable BOTH apply.
TEST(CardColorlessRaresHandOfGreed, UsesTheOrdinaryDamagePipeline) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/60);
    AddPlayerPower(s, PowerId::STRENGTH, 4);
    s.monsters[0].powers[0] =
        PowerSlot{static_cast<uint16_t>(PowerId::VULNERABLE), 2, 0, 0};
    s.monsters[0].power_count = 1;
    AddHand(s, CardId::HAND_OF_GREED);
    Play(s, 0);
    EXPECT_EQ(s.monsters[0].hp, 24) << "floor((20 + 4) * 1.5) == 36";
    EXPECT_EQ(s.combat_gold, 0);
}

TEST(CardColorlessRaresHandOfGreed, TwoKillsAccumulate) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/15);
    s.monster_count = 2;
    s.monsters[1].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[1].hp = 15;
    s.monsters[1].max_hp = 15;
    AddHand(s, CardId::HAND_OF_GREED);
    AddHand(s, CardId::HAND_OF_GREED);
    Play(s, 0, /*target=*/1);
    EXPECT_EQ(s.combat_gold, 20);
    Play(s, 0, /*target=*/0);
    EXPECT_EQ(s.combat_gold, 40);
}

// A combat-only replay has no run layer, so nothing settles: the accumulator is
// simply carried.
TEST(CardColorlessRaresHandOfGreed, CombatOnlyReplayJustCarriesTheAccumulator) {
    CombatState s = MakeCombat(/*energy=*/3, /*monster_hp=*/20);
    AddHand(s, CardId::HAND_OF_GREED);
    Play(s, 0);
    ASSERT_EQ(s.combat_gold, 20);
    Pump(s);
    EXPECT_EQ(s.combat_gold, 20) << "no combat-layer consumer reads it back";
}

}  // namespace
}  // namespace sts::engine
