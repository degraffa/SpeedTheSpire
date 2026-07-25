// Ironclad RARE cards: the exact 16-card source roster (cards/red constructors
// passing CardRarity.RARE), base/upgraded tier-2 behaviour per card, the
// start-of-turn block persistence Barricade introduces, the general
// recursive-play verb Double Tap uses, and a directed public-API script. Every
// expected number is hand-computed from the decompiled Java cited on the
// registry row.

#include <array>
#include <cstdint>
#include <span>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/advance.hpp"
#include "sts/engine/card_play.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/piles.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
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

CombatState MakeGroup(uint8_t n, int16_t monster_hp = 100, int16_t energy = 6) {
    CombatState s = MakeCombat(energy, monster_hp);
    s.monster_count = n;
    for (uint8_t i = 0; i < n; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = monster_hp;
        s.monsters[i].max_hp = monster_hp;
    }
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

CardPoolIndex AddExhaust(CombatState& s, CardId id, uint8_t upgrade = 0) {
    const CardPoolIndex pi = AddCard(s, id, upgrade);
    s.exhaust[s.exhaust_count++] = pi;
    return pi;
}

void AddPower(CombatState& s, uint8_t actor, PowerId id, int16_t amount) {
    PowerSlot* slots =
        actor == kActorPlayer ? s.player_powers : s.monsters[actor].powers;
    uint8_t* count = actor == kActorPlayer ? &s.player_power_count
                                           : &s.monsters[actor].power_count;
    slots[*count] = PowerSlot{static_cast<uint16_t>(id), amount};
    ++*count;
}

const PowerSlot* FindPower(const CombatState& s, uint8_t actor, PowerId id) {
    const PowerSlot* slots =
        actor == kActorPlayer ? s.player_powers : s.monsters[actor].powers;
    const uint8_t count = actor == kActorPlayer ? s.player_power_count
                                                : s.monsters[actor].power_count;
    for (uint8_t i = 0; i < count; ++i) {
        if (slots[i].power_id == static_cast<uint16_t>(id)) {
            return &slots[i];
        }
    }
    return nullptr;
}

void AddRelic(CombatState& s, RelicId id) {
    s.relics[s.relic_count].relic_id = static_cast<uint16_t>(id);
    s.relics[s.relic_count].counter = 0;
    ++s.relic_count;
}

bool PileHas(const CombatState& s, const CardPoolIndex* pile, uint8_t count,
             CardId id) {
    for (uint8_t i = 0; i < count; ++i) {
        if (s.card_pool[pile[i]].card_id == static_cast<uint16_t>(id)) {
            return true;
        }
    }
    return false;
}

void Play(CombatState& s, uint8_t hand_slot = 0, uint8_t target = 0) {
    ASSERT_TRUE(queue_card_play(s, hand_slot, target));
    pump(s, default_monster_turn);
}

void EndTurn(CombatState& s) {
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);
}

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

// ===========================================================================
// Registry roster + encodings
// ===========================================================================

TEST(CardRaresRegistry, ExactSourceRosterAndStableIds) {
    // The sixteen cards/red/*.java whose constructor passes CardRarity.RARE,
    // in registry-id (alphabetical file) order.
    struct Row {
        CardId id;
        int expect_id;
        CardType type;
        uint8_t cost;
        uint8_t upgraded_cost;
        bool exhaust;
    };
    constexpr std::array<Row, 16> rows{{
        {CardId::BARRICADE, 76, CardType::POWER, 3, 2, false},
        {CardId::BERSERK, 77, CardType::POWER, 0, 0, false},
        {CardId::BLUDGEON, 78, CardType::ATTACK, 3, 3, false},
        {CardId::BRUTALITY, 79, CardType::POWER, 0, 0, false},
        {CardId::CORRUPTION, 80, CardType::POWER, 3, 2, false},
        {CardId::DEMON_FORM, 81, CardType::POWER, 3, 3, false},
        {CardId::DOUBLE_TAP, 82, CardType::SKILL, 1, 1, false},
        {CardId::EXHUME, 83, CardType::SKILL, 1, 0, true},
        {CardId::FEED, 84, CardType::ATTACK, 1, 1, true},
        {CardId::FIEND_FIRE, 85, CardType::ATTACK, 2, 2, true},
        {CardId::IMMOLATE, 86, CardType::ATTACK, 2, 2, false},
        {CardId::IMPERVIOUS, 87, CardType::SKILL, 2, 2, true},
        {CardId::JUGGERNAUT, 88, CardType::POWER, 2, 2, false},
        {CardId::LIMIT_BREAK, 89, CardType::SKILL, 1, 1, true},
        {CardId::OFFERING, 90, CardType::SKILL, 0, 0, true},
        {CardId::REAPER, 91, CardType::ATTACK, 2, 2, true},
    }};
    for (const Row& r : rows) {
        EXPECT_EQ(static_cast<int>(r.id), r.expect_id);
        const CardDef* d = card_def(r.id);
        ASSERT_NE(d, nullptr) << r.expect_id;
        EXPECT_EQ(d->type, r.type) << r.expect_id;
        EXPECT_EQ(d->base_cost, r.cost) << r.expect_id;
        EXPECT_EQ(d->upgraded_cost, r.upgraded_cost) << r.expect_id;
        EXPECT_EQ(has_card_flag(d->flags, CardFlag::EXHAUST), r.exhaust)
            << r.expect_id;
    }
}

TEST(CardRaresRegistry, UpgradeFlagChangesMatchTheJava) {
    // Brutality.upgrade sets isInnate and changes NO number (Brutality.java:35-42).
    const CardDef* brutality = card_def(CardId::BRUTALITY);
    ASSERT_NE(brutality, nullptr);
    EXPECT_FALSE(has_card_flag(brutality->flags, CardFlag::INNATE));
    EXPECT_TRUE(has_card_flag(brutality->upgraded_flags, CardFlag::INNATE));

    // LimitBreak.upgrade CLEARS exhaust and changes no number (LimitBreak.java:38-45).
    const CardDef* lb = card_def(CardId::LIMIT_BREAK);
    ASSERT_NE(lb, nullptr);
    EXPECT_TRUE(has_card_flag(lb->flags, CardFlag::EXHAUST));
    EXPECT_FALSE(has_card_flag(lb->upgraded_flags, CardFlag::EXHAUST));
}

TEST(CardRaresRegistry, PowerRosterAndAppliedAmounts) {
    // Each POWER-applying rare and the stack its use() passes, both rows.
    struct Row { CardId card; PowerId power; int16_t base; int16_t upgraded; };
    constexpr std::array<Row, 6> rows{{
        {CardId::BARRICADE, PowerId::BARRICADE, -1, -1},
        {CardId::BERSERK, PowerId::BERSERK, 1, 1},
        {CardId::BRUTALITY, PowerId::BRUTALITY, 1, 1},
        {CardId::CORRUPTION, PowerId::CORRUPTION, -1, -1},
        {CardId::DEMON_FORM, PowerId::DEMON_FORM, 2, 3},
        {CardId::JUGGERNAUT, PowerId::JUGGERNAUT, 5, 7},
    }};
    for (const Row& r : rows) {
        for (uint8_t up = 0; up < 2; ++up) {
            CombatState s = MakeCombat();
            AddHand(s, r.card, up);
            Play(s, 0);
            const PowerSlot* p = FindPower(s, kActorPlayer, r.power);
            ASSERT_NE(p, nullptr) << static_cast<int>(r.card) << " up=" << up;
            EXPECT_EQ(p->amount, up == 0 ? r.base : r.upgraded);
        }
    }
    // Double Tap is the seventh and carries a different upgrade shape (1 -> 2).
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::DOUBLE_TAP, up);
        Play(s, 0);
        const PowerSlot* p = FindPower(s, kActorPlayer, PowerId::DOUBLE_TAP);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->amount, up == 0 ? 1 : 2);
    }
}

// ===========================================================================
// Barricade -- the start-of-turn block-decay guard
// ===========================================================================

TEST(CardRaresBarricade, AppliesOnceAndNeverStacks) {
    CombatState s = MakeCombat(/*energy=*/6);
    AddHand(s, CardId::BARRICADE);
    AddHand(s, CardId::BARRICADE);
    Play(s, 0);
    const PowerSlot* p = FindPower(s, kActorPlayer, PowerId::BARRICADE);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->amount, -1) << "BarricadePower's ctor marker (:22)";
    Play(s, 0);
    p = FindPower(s, kActorPlayer, PowerId::BARRICADE);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->amount, -1)
        << "Barricade.use skips the ApplyPowerAction when the power is already "
           "present (Barricade.java:32-40) -- it must not stack to -2";
    EXPECT_EQ(s.player_power_count, 1);
}

// THE NAMED ACCEPTANCE CASE: block survives the frozen start-of-turn sequence.
TEST(CardRaresBarricade, BlockPersistsThroughStartOfTurnSequence) {
    // Control: without Barricade the step-6 decay zeroes block
    // (GameActionManager.java:353-355, loseBlock()).
    CombatState control = MakeCombat();
    control.player_block = 24;
    AddDrawTop(control, CardId::STRIKE);
    EndTurn(control);
    EXPECT_EQ(control.player_block, 0);

    CombatState s = MakeCombat();
    AddHand(s, CardId::BARRICADE);
    AddDrawTop(s, CardId::STRIKE);
    Play(s, 0);
    s.player_block = 24;
    const int turn_before = static_cast<int>(s.turn);
    EndTurn(s);
    EXPECT_EQ(static_cast<int>(s.turn), turn_before + 1)
        << "the start-of-turn sequence ran";
    EXPECT_EQ(s.player_block, 24)
        << "hasPower(\"Barricade\") skips the whole loseBlock branch "
           "(GameActionManager.java:353-359)";
    // And it keeps holding across a second full round -- the guard is not a
    // one-shot latch.
    EndTurn(s);
    EXPECT_EQ(s.player_block, 24);
}

TEST(CardRaresBarricade, DoesNotSuppressCalipersOrDecayForOtherStates) {
    // Calipers alone still loses 15 -- Barricade did not swallow that branch.
    CombatState s = MakeCombat();
    AddRelic(s, RelicId::CALIPERS);
    s.player_block = 24;
    AddDrawTop(s, CardId::STRIKE);
    EndTurn(s);
    EXPECT_EQ(s.player_block, 9) << "loseBlock(15) (GameActionManager.java:357)";
}

// ===========================================================================
// Berserk -- self-Vulnerable now, +1 energy every turn after
// ===========================================================================

TEST(CardRaresBerserk, SelfVulnerableThenEnergyEachTurn) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat(/*energy=*/3);
        AddHand(s, CardId::BERSERK, up);
        AddDrawTop(s, CardId::STRIKE);
        Play(s, 0);
        const PowerSlot* v = FindPower(s, kActorPlayer, PowerId::VULNERABLE);
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(v->amount, up == 0 ? 2 : 1)
            << "magic 2, upgradeMagicNumber(-1) (Berserk.java:28,41)";
        const PowerSlot* b = FindPower(s, kActorPlayer, PowerId::BERSERK);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(b->amount, 1) << "BerserkPower(p, 1) -- literal in both rows";

        EndTurn(s);
        EXPECT_EQ(s.player_energy, kIroncladBaseEnergy + 1)
            << "the recharge is the step-6 DrawCardAction's PlayerTurnEffect, "
               "constructed BEFORE the queued GainEnergyAction resolves";
    }
}

// ===========================================================================
// Bludgeon / Impervious / Offering -- the flat-number rares
// ===========================================================================

TEST(CardRaresFlatNumbers, BludgeonImperviousOffering) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::BLUDGEON, up);
        Play(s, 0, 0);
        EXPECT_EQ(s.monsters[0].hp, up == 0 ? 68 : 58);  // 100 - 32 / - 42
    }
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::IMPERVIOUS, up);
        Play(s, 0);
        EXPECT_EQ(s.player_block, up == 0 ? 30 : 40);
        EXPECT_EQ(s.exhaust_count, up == 0 ? 1 : 1) << "exhaust in both rows";
    }
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat(/*energy=*/1);
        AddHand(s, CardId::OFFERING, up);
        for (int i = 0; i < 6; ++i) {
            AddDrawTop(s, CardId::STRIKE);
        }
        Play(s, 0);
        EXPECT_EQ(s.player_hp, 64) << "LoseHPAction(p, p, 6), fixed in both rows";
        EXPECT_EQ(s.player_energy, 3) << "1 - 0 + GainEnergyAction(2)";
        EXPECT_EQ(s.hand_count, up == 0 ? 3 : 5);
    }
}

// ===========================================================================
// Brutality / Demon Form -- the post-draw start-of-turn powers
// ===========================================================================

TEST(CardRaresBrutality, DrawsThenLosesHpAtStartOfTurn) {
    CombatState s = MakeCombat(/*energy=*/1);
    AddHand(s, CardId::BRUTALITY);
    for (int i = 0; i < 8; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    Play(s, 0);
    const int16_t hp = s.player_hp;
    EndTurn(s);
    // Step 6 draws 5, then Brutality's post-draw hook draws 1 more and costs 1 HP
    // (BrutalityPower.java:34-39, DrawCardAction then LoseHPAction).
    EXPECT_EQ(s.hand_count, 6);
    EXPECT_EQ(s.player_hp, hp - 1);
}

TEST(CardRaresDemonForm, StrengthEachTurnAndStacksAcrossPlays) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::DEMON_FORM);
    AddDrawTop(s, CardId::STRIKE);
    Play(s, 0);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::STRENGTH), nullptr)
        << "the Strength arrives at the NEXT turn start, not on play";
    EndTurn(s);
    const PowerSlot* str = FindPower(s, kActorPlayer, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 2);
    EndTurn(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::STRENGTH)->amount, 4);
}

// ===========================================================================
// Corruption -- skills exhaust, drawn skills cost 0, played skills cost nothing
// ===========================================================================

TEST(CardRaresCorruption, AppliesOnceAndRedirectsSkillsToExhaust) {
    CombatState s = MakeCombat(/*energy=*/6);
    AddHand(s, CardId::CORRUPTION);
    AddHand(s, CardId::CORRUPTION);
    AddHand(s, CardId::IMPERVIOUS);
    Play(s, 0);
    const PowerSlot* p = FindPower(s, kActorPlayer, PowerId::CORRUPTION);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->amount, -1);
    Play(s, 0);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::CORRUPTION)->amount, -1)
        << "Corruption.use skips a second ApplyPowerAction (Corruption.java:43-51)";
    EXPECT_EQ(s.player_power_count, 1);
}

TEST(CardRaresCorruption, PlayedSkillSpendsNoEnergyEvenWhenItStillCostsTwo) {
    CombatState s = MakeCombat(/*energy=*/6);
    AddHand(s, CardId::CORRUPTION);   // 3
    AddHand(s, CardId::IMPERVIOUS);   // a 2-cost SKILL already in hand
    Play(s, 0);
    EXPECT_EQ(s.player_energy, 3);
    const CardPoolIndex imp = s.hand[0];
    EXPECT_EQ(s.card_pool[imp].cost_now, 2)
        << "a SKILL already in hand keeps its displayed cost -- "
           "CorruptionPower.onCardDraw only reaches cards drawn afterwards";
    Play(s, 0);
    EXPECT_EQ(s.player_energy, 3)
        << "AbstractPlayer.useCard:1378 skips energy.use for a SKILL under "
           "Corruption";
    EXPECT_EQ(s.player_block, 30);
}

TEST(CardRaresCorruption, DrawnSkillCostsZeroAndIsCostModifiedForTurn) {
    CombatState s = MakeCombat(/*energy=*/6);
    AddHand(s, CardId::CORRUPTION);
    AddDrawTop(s, CardId::IMPERVIOUS);
    Play(s, 0);
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.src = kActorPlayer;
    draw.tgt = kActorPlayer;
    draw.amount = 1;
    execute_opcode(s, draw);
    ASSERT_EQ(s.hand_count, 1);
    const CardInstance& c = s.card_pool[s.hand[0]];
    EXPECT_EQ(c.cost_now, 0) << "CorruptionPower.onCardDraw setCostForTurn(0)";
    EXPECT_TRUE(has_card_flag(c.flags, CardFlag::COST_MODIFIED_FOR_TURN))
        << "setCostForTurn marks the instance so the end-turn sweep restores it";
}

// ===========================================================================
// Double Tap -- the recursive-play verb
// ===========================================================================

TEST(CardRaresDoubleTap, ReplaysTheNextAttackOnceThenRemovesItself) {
    CombatState s = MakeCombat(/*energy=*/6);
    AddHand(s, CardId::DOUBLE_TAP);
    AddHand(s, CardId::STRIKE);
    Play(s, 0);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::DOUBLE_TAP), nullptr);

    Play(s, 0, 0);  // Strike: 6 damage, played twice
    EXPECT_EQ(s.monsters[0].hp, 88) << "100 - 6 - 6";
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::DOUBLE_TAP), nullptr)
        << "--amount hit 0 -> RemoveSpecificPowerAction (DoubleTapPower.java:62-64)";
    EXPECT_EQ(s.player_energy, 4) << "6 - 1 (Double Tap) - 1 (Strike); "
                                     "the replay copy is free";
    EXPECT_EQ(s.hand_count, 0);
    // Double Tap (a SKILL) and the ORIGINAL Strike; the replay copy is purged, so
    // exactly one Strike comes back and none is exhausted.
    EXPECT_EQ(s.discard_count, 2);
    int strikes = 0;
    for (uint8_t i = 0; i < s.discard_count; ++i) {
        if (s.card_pool[s.discard[i]].card_id ==
            static_cast<uint16_t>(CardId::STRIKE)) {
            ++strikes;
        }
    }
    EXPECT_EQ(strikes, 1) << "the purged copy lands in no pile at all";
    EXPECT_EQ(s.exhaust_count, 0);
}

TEST(CardRaresDoubleTap, IgnoresSkillsAndPowersAndDoesNotReplayItsOwnCopy) {
    CombatState s = MakeCombat(/*energy=*/6);
    AddHand(s, CardId::DOUBLE_TAP);
    AddHand(s, CardId::IMPERVIOUS);  // a SKILL
    Play(s, 0);
    Play(s, 0);
    EXPECT_EQ(s.player_block, 30) << "a SKILL is not replayed (card.type check)";
    const PowerSlot* dt = FindPower(s, kActorPlayer, PowerId::DOUBLE_TAP);
    ASSERT_NE(dt, nullptr) << "the charge is not spent on a non-ATTACK";
    EXPECT_EQ(dt->amount, 1);

    // Upgraded Double Tap holds two charges; each ATTACK spends exactly one, and
    // the replay copy (purgeOnUse) never spends another.
    CombatState u = MakeCombat(/*energy=*/6);
    AddHand(u, CardId::DOUBLE_TAP, 1);
    AddHand(u, CardId::STRIKE);
    AddHand(u, CardId::STRIKE);
    Play(u, 0);
    Play(u, 0, 0);
    EXPECT_EQ(u.monsters[0].hp, 88);
    EXPECT_EQ(FindPower(u, kActorPlayer, PowerId::DOUBLE_TAP)->amount, 1)
        << "one charge per PLAYED attack -- `!card.purgeOnUse` (:44) keeps the "
           "copy from spending the second";
    Play(u, 0, 0);
    EXPECT_EQ(u.monsters[0].hp, 76);
    EXPECT_EQ(FindPower(u, kActorPlayer, PowerId::DOUBLE_TAP), nullptr);
}

TEST(CardRaresDoubleTap, UnspentChargeIsRemovedAtEndOfTurn) {
    CombatState s = MakeCombat();
    AddHand(s, CardId::DOUBLE_TAP);
    AddDrawTop(s, CardId::STRIKE);
    Play(s, 0);
    ASSERT_NE(FindPower(s, kActorPlayer, PowerId::DOUBLE_TAP), nullptr);
    EndTurn(s);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::DOUBLE_TAP), nullptr)
        << "atEndOfTurn RemoveSpecificPowerAction (DoubleTapPower.java:68-73)";
}

// The GENERAL form of the same verb -- the half a start-of-turn "play the top
// card of your draw pile" power reuses without any Double-Tap-specific code.
TEST(CardRaresPlayCardOpcode, DrawTopSourcePlaysAndDisposesByFlags) {
    CombatState s = MakeCombat();
    AddDrawTop(s, CardId::STRIKE);
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    it.src = kActorPlayer;
    it.tgt = 0;
    it.flags = kPlayCardFromDrawTop;
    execute_opcode(s, it);
    pump(s, default_monster_turn);
    EXPECT_EQ(s.monsters[0].hp, 94);
    EXPECT_EQ(s.draw_count, 0);
    EXPECT_EQ(s.discard_count, 1) << "no exhaust flag -> the card discards";
    EXPECT_EQ(s.player_energy, 6) << "an autoplayed card pays no energy";

    // The same verb with the exhaust bit is Havoc's disposition.
    CombatState e = MakeCombat();
    AddDrawTop(e, CardId::STRIKE);
    ActionQueueItem ex{};
    ex.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    ex.src = kActorPlayer;
    ex.tgt = 0;
    ex.flags = kPlayCardFromDrawTop | kPlayCardExhaust;
    execute_opcode(e, ex);
    pump(e, default_monster_turn);
    EXPECT_EQ(e.exhaust_count, 1);
    EXPECT_EQ(e.discard_count, 0);

    // Empty draw AND discard: nothing happens, no card is invented.
    CombatState n = MakeCombat();
    ActionQueueItem nothing{};
    nothing.opcode = static_cast<uint16_t>(Opcode::PLAY_CARD);
    nothing.src = kActorPlayer;
    nothing.tgt = 0;
    nothing.flags = kPlayCardFromDrawTop;
    execute_opcode(n, nothing);
    pump(n, default_monster_turn);
    EXPECT_EQ(n.monsters[0].hp, 100);
    EXPECT_EQ(n.hand_count, 0);
}

// ===========================================================================
// Exhume -- pull one card back out of the exhaust pile
// ===========================================================================

TEST(CardRaresExhume, ReturnsTheOnlyExhaustedCardAndSkipsExhumeCopies) {
    CombatState s = MakeCombat();
    const CardPoolIndex burned = AddExhaust(s, CardId::BLUDGEON);
    AddHand(s, CardId::EXHUME);
    Play(s, 0);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], burned);
    // Exhume itself exhausted on play and stays there: it is filtered out of the
    // candidate set (ExhumeAction.java:74-80), not consumed.
    EXPECT_TRUE(PileHas(s, s.exhaust, s.exhaust_count, CardId::EXHUME));
    EXPECT_EQ(s.exhaust_count, 1);

    // With nothing but Exhume copies in the pile, the action does nothing.
    CombatState only = MakeCombat();
    AddExhaust(only, CardId::EXHUME);
    AddHand(only, CardId::EXHUME);
    Play(only, 0);
    EXPECT_EQ(only.hand_count, 0);
    EXPECT_EQ(only.exhaust_count, 2);
}

TEST(CardRaresExhume, PromptsWhenTwoCandidatesAndIsDeadWhileTheHandIsFull) {
    CombatState s = MakeCombat();
    AddExhaust(s, CardId::BLUDGEON);
    AddExhaust(s, CardId::IMPERVIOUS);
    AddHand(s, CardId::EXHUME);
    Play(s, 0);
    ActionMask m{};
    legal_actions(s, m);
    ASSERT_TRUE(m.choice_pending);
    EXPECT_TRUE(m.choice_from_exhaust);
    EXPECT_FALSE(m.choice_from_discard);
    EXPECT_TRUE(m.can_choose[0]);
    EXPECT_TRUE(m.can_choose[1]);
    EXPECT_FALSE(m.can_choose[2]) << "the Exhume copy is not a candidate";
    Step(s, make_action(ActionVerb::CHOOSE, 1, 0));
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[s.hand[0]].card_id,
              static_cast<uint16_t>(CardId::IMPERVIOUS));

    // Hand full at resolve time -> the whole action is a no-op
    // (ExhumeAction.java:40-43).
    CombatState full = MakeCombat();
    AddExhaust(full, CardId::BLUDGEON);
    for (int i = 0; i < kHandCap; ++i) {
        AddHand(full, CardId::STRIKE);
    }
    ActionQueueItem choose{};
    choose.opcode = static_cast<uint16_t>(Opcode::CHOOSE_CARD);
    choose.src = kActorPlayer;
    choose.tgt = kActorPlayer;
    choose.amount = 1;
    choose.flags = make_choose_flags(ChoiceKind::EXHAUST_TO_HAND, false);
    execute_opcode(full, choose);
    EXPECT_EQ(full.hand_count, kHandCap);
    EXPECT_EQ(full.exhaust_count, 1);
}

TEST(CardRaresExhume, ReturnedSkillIsFreeForTheTurnUnderCorruption) {
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::CORRUPTION, -1);
    const CardPoolIndex skill = AddExhaust(s, CardId::IMPERVIOUS);
    AddHand(s, CardId::EXHUME);
    Play(s, 0);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.card_pool[skill].cost_now, 0)
        << "setCostForTurn(-9) (ExhumeAction.java:57-59), clamped to 0";
    EXPECT_TRUE(has_card_flag(s.card_pool[skill].flags,
                              CardFlag::COST_MODIFIED_FOR_TURN));
}

// ===========================================================================
// Feed -- damage, and max HP only when the hit kills
// ===========================================================================

TEST(CardRaresFeed, RaisesMaxHpOnlyOnAKillAndHealsThroughTheRelicSeam) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat(/*energy=*/1, /*monster_hp=*/4);
        AddHand(s, CardId::FEED, up);
        Play(s, 0, 0);
        EXPECT_EQ(s.monsters[0].hp, 0);
        const int gain = up == 0 ? 3 : 4;
        EXPECT_EQ(s.player_max_hp, 80 + gain);
        EXPECT_EQ(s.player_hp, 70 + gain)
            << "increaseMaxHp heals by the same amount (AbstractCreature.java:206)";
    }

    CombatState alive = MakeCombat(/*energy=*/1, /*monster_hp=*/100);
    AddHand(alive, CardId::FEED);
    Play(alive, 0, 0);
    EXPECT_EQ(alive.monsters[0].hp, 90);
    EXPECT_EQ(alive.player_max_hp, 80) << "no kill, no max-HP gain";
    EXPECT_EQ(alive.player_hp, 70);

    // The heal half goes through heal_player_with_relics, so Magic Flower's
    // MathUtils.round(amount * 1.5f) applies: round(3 * 1.5f) == 5.
    CombatState flower = MakeCombat(/*energy=*/1, /*monster_hp=*/4);
    AddRelic(flower, RelicId::MAGIC_FLOWER);
    AddHand(flower, CardId::FEED);
    Play(flower, 0, 0);
    EXPECT_EQ(flower.player_max_hp, 83);
    EXPECT_EQ(flower.player_hp, 75)
        << "Magic Flower is live only because the gain routes through the seam";
}

// ===========================================================================
// Fiend Fire -- exhaust the hand, then one hit per card
// ===========================================================================

TEST(CardRaresFiendFire, ExhaustsTheHandThenHitsOncePerCard) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat(/*energy=*/2);
        AddHand(s, CardId::FIEND_FIRE, up);
        AddHand(s, CardId::STRIKE);
        AddHand(s, CardId::DEFEND);
        AddHand(s, CardId::BASH);
        s.card_random_rng = from_seed(42);
        Play(s, 0, 0);
        EXPECT_EQ(s.hand_count, 0);
        // 3 other cards exhausted + Fiend Fire's own exhaust flag.
        EXPECT_EQ(s.exhaust_count, 4);
        const int per_hit = up == 0 ? 7 : 10;
        EXPECT_EQ(s.monsters[0].hp, 100 - 3 * per_hit);
    }

    // The played card is out of the hand before the action resolves
    // (AbstractPlayer.useCard:1374), so a lone Fiend Fire deals nothing.
    CombatState lone = MakeCombat(/*energy=*/2);
    AddHand(lone, CardId::FIEND_FIRE);
    Play(lone, 0, 0);
    EXPECT_EQ(lone.monsters[0].hp, 100);
    EXPECT_EQ(lone.exhaust_count, 1);
}

// ===========================================================================
// Immolate -- AoE + a Burn into the discard
// ===========================================================================

TEST(CardRaresImmolate, HitsEveryMonsterAndShufflesABurnIntoTheDiscard) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeGroup(3);
        AddHand(s, CardId::IMMOLATE, up);
        Play(s, 0);
        const int dmg = up == 0 ? 21 : 28;
        for (uint8_t i = 0; i < 3; ++i) {
            EXPECT_EQ(s.monsters[i].hp, 100 - dmg) << "slot " << int(i);
        }
        ASSERT_EQ(s.discard_count, 2) << "the Burn + Immolate itself";
        EXPECT_TRUE(PileHas(s, s.discard, s.discard_count, CardId::BURN));
    }
}

// ===========================================================================
// Juggernaut -- block gain reflects THORNS damage at a random enemy
// ===========================================================================

TEST(CardRaresJuggernaut, DamagesOnBlockGainAndIsNotScaledByStrength) {
    CombatState s = MakeCombat(/*energy=*/6);
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 5);
    AddHand(s, CardId::JUGGERNAUT);
    AddHand(s, CardId::IMPERVIOUS);
    Play(s, 0);
    EXPECT_EQ(s.monsters[0].hp, 100) << "applying the power gains no block";
    Play(s, 0);
    EXPECT_EQ(s.player_block, 30);
    EXPECT_EQ(s.monsters[0].hp, 95)
        << "5 THORNS damage -- the THORNS type skips the NORMAL-only "
           "atDamageGive pass, so Strength 5 does NOT scale it";

    // A zero-block gain fires nothing (blockAmount > 0, JuggernautPower.java:36).
    CombatState z = MakeCombat();
    AddPower(z, kActorPlayer, PowerId::JUGGERNAUT, 5);
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = kActorPlayer;
    blk.tgt = kActorPlayer;
    blk.amount = 0;
    execute_opcode(z, blk);
    pump(z, default_monster_turn);
    EXPECT_EQ(z.monsters[0].hp, 100);
}

// ===========================================================================
// Limit Break -- double the current Strength
// ===========================================================================

TEST(CardRaresLimitBreak, DoublesStrengthIncludingNegativeAndNoOpsWithout) {
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::STRENGTH, 3);
    AddHand(s, CardId::LIMIT_BREAK);
    Play(s, 0);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::STRENGTH)->amount, 6);
    EXPECT_EQ(s.exhaust_count, 1);

    CombatState up = MakeCombat();
    AddPower(up, kActorPlayer, PowerId::STRENGTH, -2);
    AddHand(up, CardId::LIMIT_BREAK, 1);
    Play(up, 0);
    EXPECT_EQ(FindPower(up, kActorPlayer, PowerId::STRENGTH)->amount, -4)
        << "hasPower is a presence test, so a penalty doubles too";
    EXPECT_EQ(up.exhaust_count, 0) << "the upgrade clears exhaust";
    EXPECT_EQ(up.discard_count, 1);

    CombatState none = MakeCombat();
    AddHand(none, CardId::LIMIT_BREAK);
    Play(none, 0);
    EXPECT_EQ(FindPower(none, kActorPlayer, PowerId::STRENGTH), nullptr);
}

// ===========================================================================
// Reaper -- AoE, then heal the HP actually taken off
// ===========================================================================

TEST(CardRaresReaper, HealsTheDamageThatActuallyLanded) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeGroup(2);
        AddHand(s, CardId::REAPER, up);
        Play(s, 0);
        const int per = up == 0 ? 4 : 5;
        EXPECT_EQ(s.monsters[0].hp, 100 - per);
        EXPECT_EQ(s.monsters[1].hp, 100 - per);
        EXPECT_EQ(s.player_hp, 70 + 2 * per);
    }

    // Block absorbs, and only the HP that MOVED is healed (lastDamageTaken).
    CombatState blocked = MakeGroup(2);
    blocked.monsters[0].block = 10;
    AddHand(blocked, CardId::REAPER);
    Play(blocked, 0);
    EXPECT_EQ(blocked.monsters[0].hp, 100);
    EXPECT_EQ(blocked.monsters[1].hp, 96);
    EXPECT_EQ(blocked.player_hp, 74) << "only the unblocked 4";

    // A dead monster is skipped, and an overkill heals only its remaining HP.
    CombatState overkill = MakeGroup(2);
    overkill.monsters[0].hp = 1;
    AddHand(overkill, CardId::REAPER);
    Play(overkill, 0);
    EXPECT_EQ(overkill.monsters[0].hp, 0);
    EXPECT_EQ(overkill.player_hp, 75) << "1 + 4, not 4 + 4";

    // The heal goes through the relic seam, so Magic Flower scales it.
    CombatState flower = MakeGroup(1);
    AddRelic(flower, RelicId::MAGIC_FLOWER);
    AddHand(flower, CardId::REAPER);
    Play(flower, 0);
    EXPECT_EQ(flower.player_hp, 76) << "round(4 * 1.5f) == 6";
}

// ===========================================================================
// Directed public-API script (advance()/legal_actions() only)
// ===========================================================================

TEST(CardRaresDirected, BarricadeImperviousDoubleTapBludgeonScript) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/120);
    AddHand(s, CardId::BARRICADE);   // 3
    AddHand(s, CardId::IMPERVIOUS);  // 2
    AddHand(s, CardId::DOUBLE_TAP);  // 1
    for (int i = 0; i < 5; ++i) {
        AddDrawTop(s, CardId::STRIKE);
    }
    AddDrawTop(s, CardId::BLUDGEON);  // top of the draw pile -- drawn first

    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Barricade
    EXPECT_EQ(s.player_energy, 3);

    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Impervious
    EXPECT_EQ(s.player_block, 30);
    EXPECT_EQ(s.player_energy, 1);

    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));  // Double Tap
    EXPECT_EQ(s.player_energy, 0);

    // Nothing left to play -- end the turn and let the block ride.
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_end_turn);
    Step(s, make_action(ActionVerb::END_TURN));
    EXPECT_EQ(s.player_block, 30) << "Barricade holds it through step 6";
    EXPECT_EQ(s.player_energy, kIroncladBaseEnergy);
    EXPECT_EQ(FindPower(s, kActorPlayer, PowerId::DOUBLE_TAP), nullptr)
        << "the unspent charge did not survive the turn";

    // Bludgeon (3) is now affordable and lands once -- Double Tap is gone.
    uint8_t slot = kHandCap;
    for (uint8_t i = 0; i < s.hand_count; ++i) {
        if (s.card_pool[s.hand[i]].card_id ==
            static_cast<uint16_t>(CardId::BLUDGEON)) {
            slot = i;
        }
    }
    ASSERT_LT(slot, s.hand_count);
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play_target[slot][0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, slot, 0));
    EXPECT_EQ(s.monsters[0].hp, 88) << "120 - 32, once";
}

}  // namespace
}  // namespace sts::engine
