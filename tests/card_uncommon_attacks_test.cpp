// B3.5 Ironclad uncommon attacks: exact 11-card source roster, base/upgraded
// tier-2 behavior, per-instance counters, cost hooks, X-cost, and a directed
// public-advance script. Expected values are hand-computed from the cited Java.

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
#include "sts/engine/powers.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {
namespace {

CombatState MakeCombat(int16_t energy = 6, int16_t monster_hp = 100) {
    CombatState s{};
    s.player_hp = 80;
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

CombatState MakeThree(int16_t energy = 3) {
    CombatState s = MakeCombat(energy, 100);
    s.monster_count = 3;
    for (uint8_t i = 0; i < 3; ++i) {
        s.monsters[i].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[i].hp = 100;
        s.monsters[i].max_hp = 100;
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

void GiveRelic(CombatState& s, RelicId id) {
    s.relics[s.relic_count].relic_id = static_cast<uint16_t>(id);
    s.relics[s.relic_count].counter = -1;
    ++s.relic_count;
}

CardPoolIndex FindReplayCopy(const CombatState& s, CardId id,
                             CardPoolIndex original) {
    for (int pi = 0; pi < kCardPoolCap; ++pi) {
        if (pi != original &&
            s.card_pool[pi].card_id == static_cast<uint16_t>(id)) {
            return static_cast<CardPoolIndex>(pi);
        }
    }
    return static_cast<CardPoolIndex>(kCardPoolCap);
}

void AddPower(CombatState& s, uint8_t actor, PowerId id, int16_t amount) {
    PowerSlot* slots = actor == kActorPlayer ? s.player_powers
                                             : s.monsters[actor].powers;
    uint8_t* count = actor == kActorPlayer ? &s.player_power_count
                                           : &s.monsters[actor].power_count;
    slots[*count] = PowerSlot{static_cast<uint16_t>(id), amount, 0, 0};
    ++*count;
}

const PowerSlot* FindMonsterPower(const CombatState& s, uint8_t actor,
                                  PowerId id) {
    for (uint8_t i = 0; i < s.monsters[actor].power_count; ++i) {
        if (s.monsters[actor].powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.monsters[actor].powers[i];
        }
    }
    return nullptr;
}

const PowerSlot* FindPlayerPower(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
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

StepResult Step(CombatState& s, Action a) {
    StepResult r{};
    advance(std::span<CombatState>(&s, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&r, 1));
    return r;
}

TEST(CardUncommonAttacksRegistry, ExactSourceRosterAndStableIds) {
    const std::array<CardId, 11> ids{
        CardId::BLOOD_FOR_BLOOD, CardId::CARNAGE, CardId::DROPKICK,
        CardId::HEMOKINESIS, CardId::PUMMEL, CardId::RAMPAGE,
        CardId::RECKLESS_CHARGE, CardId::SEARING_BLOW, CardId::SEVER_SOUL,
        CardId::UPPERCUT, CardId::WHIRLWIND};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        EXPECT_EQ(static_cast<int>(ids[i]), 40 + static_cast<int>(i));
        const CardDef* d = card_def(ids[i]);
        ASSERT_NE(d, nullptr);
        EXPECT_EQ(d->type, CardType::ATTACK);
    }
    EXPECT_EQ(static_cast<uint16_t>(Opcode::DROPKICK), 21);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::DAMAGE_UPGRADE_SCALE), 22);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::DAMAGE_RAMPAGE), 23);
    EXPECT_EQ(static_cast<uint16_t>(Opcode::EXHAUST_NON_ATTACKS), 24);
}

TEST(CardUncommonBloodForBlood, BaseAndUpgradeDamageRows) {
    const CardDef* d = card_def(CardId::BLOOD_FOR_BLOOD);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(card_cost(*d, 0), 4);
    EXPECT_EQ(card_cost(*d, 1), 3);
    CombatState b = MakeCombat();
    AddHand(b, CardId::BLOOD_FOR_BLOOD);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 82);
    CombatState u = MakeCombat();
    AddHand(u, CardId::BLOOD_FOR_BLOOD, 1);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 78);
}

TEST(CardUncommonBloodForBlood, PositiveHpLossReducesEveryActiveCopyPerEvent) {
    CombatState s = MakeCombat();
    const CardPoolIndex hand = AddHand(s, CardId::BLOOD_FOR_BLOOD);
    const CardPoolIndex draw = AddCard(s, CardId::BLOOD_FOR_BLOOD);
    s.draw[s.draw_count++] = draw;
    const CardPoolIndex discard = AddCard(s, CardId::BLOOD_FOR_BLOOD, 1);
    s.discard[s.discard_count++] = discard;
    const CardPoolIndex exhaust = AddCard(s, CardId::BLOOD_FOR_BLOOD);
    s.exhaust[s.exhaust_count++] = exhaust;

    ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
    hit.src = 0;
    hit.tgt = kActorPlayer;
    hit.amount = 9;
    execute_opcode(s, hit);
    EXPECT_EQ(s.player_hp, 71);
    EXPECT_EQ(s.card_pool[hand].cost_now, 3);
    EXPECT_EQ(s.card_pool[draw].cost_now, 3);
    EXPECT_EQ(s.card_pool[discard].cost_now, 2);
    EXPECT_EQ(s.card_pool[exhaust].cost_now, 4)
        << "exhaust is not scanned by updateCardsOnDamage";

    s.player_block = 20;
    execute_opcode(s, hit);
    EXPECT_EQ(s.player_hp, 71);
    EXPECT_EQ(s.card_pool[hand].cost_now, 3)
        << "fully blocked damage is not an HP-loss event";
}

TEST(CardUncommonBloodForBlood, UpgradePreservesCombatReducedCost) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::BLOOD_FOR_BLOOD);
    s.card_pool[pi].cost_now = 2;
    ASSERT_TRUE(choice_slot_eligible(s, 0, ChoiceKind::UPGRADE));
    apply_choice_selection(s, 0, ChoiceKind::UPGRADE);
    EXPECT_EQ(s.card_pool[pi].upgrade, 1);
    EXPECT_EQ(s.card_pool[pi].cost_now, 1);
}

TEST(CardUncommonCarnage, EtherealBaseAndUpgradedDamage) {
    const CardDef* d = card_def(CardId::CARNAGE);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::ETHEREAL));
    CombatState b = MakeCombat();
    AddHand(b, CardId::CARNAGE);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 80);
    CombatState u = MakeCombat();
    AddHand(u, CardId::CARNAGE, 1);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 72);
}

TEST(CardUncommonCarnage, EtherealExhaustsIfUnplayedAtEndOfTurn) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::CARNAGE);
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
}

TEST(CardUncommonDropkick, ConditionCheckedAtExecution) {
    CombatState b = MakeCombat(3);
    AddHand(b, CardId::DROPKICK);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 95);
    EXPECT_EQ(b.player_energy, 2);
    EXPECT_EQ(b.hand_count, 0);

    CombatState u = MakeCombat(3);
    AddHand(u, CardId::DROPKICK, 1);
    const CardPoolIndex top = AddDrawTop(u, CardId::STRIKE);
    AddPower(u, 0, PowerId::VULNERABLE, 2);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 88) << "8 * 1.5 Vulnerable";
    EXPECT_EQ(u.player_energy, 3) << "pay 1, then gain 1";
    ASSERT_EQ(u.hand_count, 1);
    EXPECT_EQ(u.hand[0], top);
}

TEST(CardUncommonHemokinesis, SelfLossThenBaseAndUpgradedDamage) {
    CombatState b = MakeCombat();
    AddHand(b, CardId::HEMOKINESIS);
    Play(b);
    EXPECT_EQ(b.player_hp, 78);
    EXPECT_EQ(b.monsters[0].hp, 85);
    CombatState u = MakeCombat();
    AddHand(u, CardId::HEMOKINESIS, 1);
    Play(u);
    EXPECT_EQ(u.player_hp, 78);
    EXPECT_EQ(u.monsters[0].hp, 80);
}

// --- The play-time damage lock (kDamageOwnerLocked) -------------------------
// A card's damage number is fixed by calculateCardDamage at useCard
// (AbstractPlayer.java:1362), BEFORE use() queues its actions (:1369), and the
// DamageAction lands that DamageInfo unchanged (DamageAction.java:88). Strength
// the card's OWN earlier actions grant -- via Rupture (RupturePower.java:32-37,
// `info.owner == owner`) on a self-inflicted HP loss -- reaches the NEXT card,
// never this one. Live witnesses: seeds STS205854 / STS230126 (Hemokinesis+ with
// Rupture 2 / 4) and STS204756 (Pain + Rupture 2 + Perfected Strike+).

TEST(CardDamageLock, HemokinesisOwnRuptureStrengthDoesNotScaleItsHit) {
    // Hemokinesis.use: addToBot(LoseHP 2) THEN addToBot(DamageAction 20)
    // (Hemokinesis.java:35-36). The loss resolves first, Rupture 2 grants +2
    // Strength, and the 20 must still land as 20 -- not 22.
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::RUPTURE, 2);
    AddHand(s, CardId::HEMOKINESIS, 1);
    Play(s);
    EXPECT_EQ(s.player_hp, 78);
    EXPECT_EQ(s.monsters[0].hp, 80) << "locked at play time: 20, not 20 + 2";
    ASSERT_NE(FindPlayerPower(s, PowerId::STRENGTH), nullptr)
        << "Rupture DID fire -- the Strength is real, it just arrives too late";
    EXPECT_EQ(FindPlayerPower(s, PowerId::STRENGTH)->amount, 2);

    // And the NEXT attack sees it: Strike 6 + 2.
    AddHand(s, CardId::STRIKE);
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 72);
}

TEST(CardDamageLock, PainTriggeredRuptureStrengthDoesNotScaleTheTriggeringAoE) {
    // Pain.triggerOnOtherCardPlayed addToTop's a LoseHPAction(1) (Pain.java:
    // 34-36) from useCard:1372 -- so it resolves BEFORE the played card's own
    // damage. With Rupture 2 the player has +2 Strength by the time Cleave's
    // DamageAllEnemiesAction lands, and the game still deals the play-time 8
    // to every monster: `multiDamage` was fixed at useCard. One queued AoE
    // item serves all three targets, so this also pins the owner-stage lock
    // surviving the execute-time fan-out.
    CombatState s = MakeThree();
    AddPower(s, kActorPlayer, PowerId::RUPTURE, 2);
    AddHand(s, CardId::PAIN);
    AddHand(s, CardId::CLEAVE);
    Play(s, /*hand_slot=*/1);
    EXPECT_EQ(s.player_hp, 79) << "Pain's 1 HP";
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ(s.monsters[i].hp, 92) << "monster " << i << ": 8, not 8 + 2";
    }
    ASSERT_NE(FindPlayerPower(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(FindPlayerPower(s, PowerId::STRENGTH)->amount, 2);
}

TEST(CardDamageLock, PainTriggeredRuptureStrengthDoesNotScalePerfectedStrike) {
    // The STS204756 shape exactly: Perfected Strike+ (6 + 3 per Strike-tagged
    // card in hand/draw/discard, itself included) with Pain in hand and
    // Rupture 2. Two Strike-tagged cards (itself + a Strike in hand) -> 12
    // locked; the sim used to land 14 and kill a 13-HP target the game left
    // at 1 -- which is how the Blue Slaver survived to attack in the capture.
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/13);
    AddPower(s, kActorPlayer, PowerId::RUPTURE, 2);
    AddHand(s, CardId::PAIN);
    AddHand(s, CardId::STRIKE);
    AddHand(s, CardId::PERFECTED_STRIKE, 1);
    Play(s, /*hand_slot=*/2);
    EXPECT_EQ(s.player_hp, 79);
    EXPECT_EQ(s.monsters[0].hp, 1) << "12 locked at play time, not 14";
}

TEST(CardDamageLock, RandomTargetHitsStayExecuteTime) {
    // The deliberate exception: AttackDamageRandomEnemyAction.update re-runs
    // info.applyPowers against the rolled target at EVERY hit, so Sword
    // Boomerang's three hits DO see the Strength Pain's loss just granted.
    CombatState s = MakeCombat();
    AddPower(s, kActorPlayer, PowerId::RUPTURE, 2);
    AddHand(s, CardId::PAIN);
    AddHand(s, CardId::SWORD_BOOMERANG);
    Play(s, /*hand_slot=*/1);
    EXPECT_EQ(s.player_hp, 79);
    EXPECT_EQ(s.monsters[0].hp, 100 - 3 * (3 + 2))
        << "each hit re-applies powers at resolve time";
}

TEST(CardUncommonPummel, FourOrFiveHitsAndExhaust) {
    CombatState b = MakeCombat();
    const CardPoolIndex bp = AddHand(b, CardId::PUMMEL);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 92);
    EXPECT_TRUE(PileHas(b.exhaust, b.exhaust_count, bp));
    CombatState u = MakeCombat();
    const CardPoolIndex up = AddHand(u, CardId::PUMMEL, 1);
    Play(u);
    EXPECT_EQ(u.monsters[0].hp, 90);
    EXPECT_TRUE(PileHas(u.exhaust, u.exhaust_count, up));
}

TEST(CardUncommonRampage, BaseInstanceScalesFiveAfterEachPlay) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::RAMPAGE);
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 92);
    EXPECT_EQ(s.card_pool[pi].misc, 5);
    s.hand[s.hand_count++] = pi;
    --s.discard_count;
    s.player_energy = 6;
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 79) << "second hit is 8+5";
    EXPECT_EQ(s.card_pool[pi].misc, 10);
}

TEST(CardUncommonRampage, UpgradedInstanceScalesEight) {
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::RAMPAGE, 1);
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 92);
    EXPECT_EQ(s.card_pool[pi].misc, 8);
    s.hand[s.hand_count++] = pi;
    --s.discard_count;
    s.player_energy = 6;
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 76) << "second hit is 8+8";
    EXPECT_EQ(s.card_pool[pi].misc, 16);
}

// --- The makeSameInstanceOf uuid group (S2.43 campaign defect, 2026-08-27) ---
//
// Rampage.use queues DamageAction then ModifyDamageAction(this.uuid, magic)
// (Rampage.java:36-39), and ModifyDamageAction.update writes EVERY card
// GetAllInBattleInstances.get(uuid) returns (ModifyDamageAction.java:26-33),
// a walk that covers cardInUse plus all five piles INCLUDING LIMBO
// (GetAllInBattleInstances.java:12-38). DoubleTapPower.onUseCard builds its
// replay with makeSameInstanceOf (DoubleTapPower.java:50), which copies the
// stats AND the uuid (AbstractCard.java:819-823), and parks it in limbo (:51).
// So the accumulator is ONE number shared by the original and its live copies:
// the replay reads what the original's own ModifyDamageAction just wrote.
//
// LIVE GROUND TRUTH (oracle capture run_STS100009_a20_ironclad.jsonl, A20
// Ironclad, floor 1, Cultist): the Cultist stands at 21/53 HP when a base
// Rampage is played under a 1-stack Double Tap, and the combat ENDS on that
// play -- 8 + 13 == 21 exactly. The engine used to snapshot misc into the copy
// at copy time, which is BEFORE the original's ModifyDamageAction resolves
// (resolve_card_play queues the program at step 4 and fires ON_USE_CARD at
// step 5, mirroring AbstractPlayer.useCard:1369-1370), so it dealt 8 + 8 == 16
// and left the Cultist alive on 5.
TEST(DoubleTap, ReplayedRampageReadsTheGrownMisc) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/21);
    AddPower(s, kActorPlayer, PowerId::DOUBLE_TAP, 1);
    const CardPoolIndex pi = AddHand(s, CardId::RAMPAGE);

    Play(s);

    EXPECT_EQ(s.monsters[0].hp, 0)
        << "8 then 13 kills the capture's 21-HP Cultist on this very play";
    EXPECT_EQ(s.card_pool[pi].misc, 10)
        << "both ModifyDamageActions land on the uuid group's owning row";
    const CardPoolIndex copy = FindReplayCopy(s, CardId::RAMPAGE, pi);
    ASSERT_LT(copy, kCardPoolCap);
    EXPECT_TRUE(
        has_card_flag(s.card_pool[copy].flags, CardFlag::REPLAY_MISC_LINK));
    EXPECT_EQ(s.card_pool[copy].misc, pi)
        << "the copy's misc word is the link, not a second counter";
}

// The write-back half: the REPLAY's ModifyDamageAction also reaches the
// original, which by then sits in the discard pile (GetAllInBattleInstances
// walks discardPile too). A later play of the original therefore opens at
// 8 + 10 == 18, not 8 + 5 == 13.
TEST(DoubleTap, ReplayedRampageGrowthPersistsOnTheOriginalInstance) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/200);
    AddPower(s, kActorPlayer, PowerId::DOUBLE_TAP, 1);
    const CardPoolIndex pi = AddHand(s, CardId::RAMPAGE);

    Play(s);
    ASSERT_EQ(s.monsters[0].hp, 200 - 8 - 13);
    ASSERT_EQ(s.card_pool[pi].misc, 10);

    // Replay the original out of the discard pile, with no Double Tap left.
    s.hand[s.hand_count++] = pi;
    --s.discard_count;
    s.player_energy = 6;
    Play(s);

    EXPECT_EQ(s.monsters[0].hp, 200 - 8 - 13 - 18) << "third hit is 8+10";
    EXPECT_EQ(s.card_pool[pi].misc, 15);
}

// Necronomicon's replay is the SAME machinery (Necronomicon.java:70-77 --
// makeSameInstanceOf, purgeOnUse, front-of-queue autoplay), so it shared the
// defect and shares the fix. Its gate is `costForTurn >= 2` with no
// !purgeOnUse conjunct (:62), so the reachable shape is a cost-raised Rampage.
TEST(Necronomicon, ReplayedRampageReadsTheGrownMisc) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/21);
    GiveRelic(s, RelicId::NECRONOMICON);
    const CardPoolIndex pi = AddHand(s, CardId::RAMPAGE);
    s.card_pool[pi].cost_now = 2;  // `card.costForTurn >= 2` (:62)

    Play(s);

    EXPECT_EQ(s.monsters[0].hp, 0) << "8 then 13, exactly as under Double Tap";
    EXPECT_EQ(s.card_pool[pi].misc, 10);
}

// Ritual Dagger's growth is the same GetAllInBattleInstances write
// (RitualDaggerAction.java:39-46), so a kill scored by the replay copy must
// grow the ORIGINAL -- the S2.34 deviation this closes. The original is in the
// exhaust pile by then (Ritual Dagger exhausts), which that walk also covers.
// 20 HP: the original's 15 does not kill, the replay's 15 does.
TEST(DoubleTap, ReplayedRitualDaggerKillGrowsTheOriginalInstance) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/20);
    AddPower(s, kActorPlayer, PowerId::DOUBLE_TAP, 1);
    const CardPoolIndex pi = AddHand(s, CardId::RITUAL_DAGGER);
    s.card_pool[pi].misc = 15;  // the ctor's misc (RitualDagger.java:27-29)

    Play(s);

    EXPECT_LE(s.monsters[0].hp, 0);
    EXPECT_EQ(s.card_pool[pi].misc, 18)
        << "the replay copy's kill grows the original, not the row that purges";
    EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, pi));
}

// The negative: a replay copy is only linked when the card's misc IS a
// uuid-shared counter. Strike keeps no counter and Searing Blow's scaling is
// baked from its upgrade COUNT at queue time (card_play.cpp), not from misc --
// so neither copy carries the link, and both plays deal the same number.
TEST(DoubleTap, ReplayedNonAccumulatingAttacksAreUnlinkedAndDoNotGrow) {
    CombatState s = MakeCombat(/*energy=*/6, /*monster_hp=*/200);
    AddPower(s, kActorPlayer, PowerId::DOUBLE_TAP, 1);
    const CardPoolIndex strike = AddHand(s, CardId::STRIKE);
    Play(s);
    EXPECT_EQ(s.monsters[0].hp, 200 - 6 - 6) << "6 then 6, no accumulator";
    EXPECT_EQ(s.card_pool[strike].misc, 0);
    const CardPoolIndex strike_copy = FindReplayCopy(s, CardId::STRIKE, strike);
    ASSERT_LT(strike_copy, kCardPoolCap);
    EXPECT_FALSE(has_card_flag(s.card_pool[strike_copy].flags,
                               CardFlag::REPLAY_MISC_LINK));
    EXPECT_EQ(s.card_pool[strike_copy].misc, 0);

    CombatState b = MakeCombat(/*energy=*/6, /*monster_hp=*/200);
    AddPower(b, kActorPlayer, PowerId::DOUBLE_TAP, 1);
    const CardPoolIndex blow = AddHand(b, CardId::SEARING_BLOW, /*upgrade=*/2);
    Play(b);
    EXPECT_EQ(b.monsters[0].hp, 200 - 21 - 21)
        << "the upgrade-count scale is per play, not a growing counter";
    EXPECT_EQ(b.card_pool[blow].misc, 0);
    const CardPoolIndex blow_copy =
        FindReplayCopy(b, CardId::SEARING_BLOW, blow);
    ASSERT_LT(blow_copy, kCardPoolCap);
    EXPECT_FALSE(has_card_flag(b.card_pool[blow_copy].flags,
                               CardFlag::REPLAY_MISC_LINK));
}

TEST(CardUncommonRecklessCharge, MakesDazedAtRandomDrawSpotBaseAndUpgraded) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::RECKLESS_CHARGE, up);
        AddDrawTop(s, CardId::STRIKE);
        s.card_random_rng = from_seed(42);
        const int32_t before = s.card_random_rng.counter;
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 100 - (up == 0 ? 7 : 10));
        EXPECT_EQ(s.card_random_rng.counter, before + 1);
        bool found = false;
        for (uint8_t i = 0; i < s.draw_count; ++i) {
            const CardInstance& c = s.card_pool[s.draw[i]];
            if (c.card_id == static_cast<uint16_t>(CardId::DAZED)) {
                found = true;
                EXPECT_TRUE(has_card_flag(c.flags, CardFlag::ETHEREAL));
            }
        }
        EXPECT_TRUE(found);
    }
}

TEST(CardUncommonSearingBlow, UpgradeCountUsesTriangularDamageFormula) {
    const std::array<int, 4> expected{12, 16, 21, 27};
    for (uint8_t n = 0; n < expected.size(); ++n) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::SEARING_BLOW, n);
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 100 - expected[n]) << "upgrade count " << +n;
    }
}

TEST(CardUncommonSearingBlow, RemainsUpgradeableWithoutSchemaChange) {
    static_assert(sizeof(CardInstance) == 8);
    CombatState s = MakeCombat();
    const CardPoolIndex pi = AddHand(s, CardId::SEARING_BLOW, 1);
    for (uint8_t want = 2; want <= 4; ++want) {
        EXPECT_TRUE(choice_slot_eligible(s, 0, ChoiceKind::UPGRADE));
        apply_choice_selection(s, 0, ChoiceKind::UPGRADE);
        EXPECT_EQ(s.card_pool[pi].upgrade, want);
        EXPECT_EQ(s.card_pool[pi].cost_now, 2);
    }
}

TEST(CardUncommonSeverSoul, ExhaustsAllNonAttacksBeforeBaseOrUpgradedDamage) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::SEVER_SOUL, up);
        const CardPoolIndex attack = AddHand(s, CardId::STRIKE);
        const CardPoolIndex skill = AddHand(s, CardId::DEFEND);
        const CardPoolIndex status = AddHand(s, CardId::DAZED);
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 100 - (up == 0 ? 16 : 22));
        EXPECT_TRUE(PileHas(s.hand, s.hand_count, attack));
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, skill));
        EXPECT_TRUE(PileHas(s.exhaust, s.exhaust_count, status));
        EXPECT_EQ(s.hand_count, 1);
    }
}

TEST(CardUncommonUppercut, BaseAndUpgradeApplyBothDebuffsAfterDamage) {
    for (uint8_t up = 0; up < 2; ++up) {
        CombatState s = MakeCombat();
        AddHand(s, CardId::UPPERCUT, up);
        Play(s);
        EXPECT_EQ(s.monsters[0].hp, 87);
        const PowerSlot* weak = FindMonsterPower(s, 0, PowerId::WEAK);
        const PowerSlot* vuln = FindMonsterPower(s, 0, PowerId::VULNERABLE);
        ASSERT_NE(weak, nullptr);
        ASSERT_NE(vuln, nullptr);
        EXPECT_EQ(weak->amount, up == 0 ? 1 : 2);
        EXPECT_EQ(vuln->amount, up == 0 ? 1 : 2);
    }
}

TEST(CardUncommonWhirlwind, XCostRepeatsAoeAndConsumesAllEnergy) {
    CombatState b = MakeThree(3);
    AddHand(b, CardId::WHIRLWIND);
    Play(b);
    EXPECT_EQ(b.player_energy, 0);
    for (uint8_t i = 0; i < b.monster_count; ++i) {
        EXPECT_EQ(b.monsters[i].hp, 85);
    }
    CombatState u = MakeThree(2);
    AddHand(u, CardId::WHIRLWIND, 1);
    Play(u);
    EXPECT_EQ(u.player_energy, 0);
    for (uint8_t i = 0; i < u.monster_count; ++i) {
        EXPECT_EQ(u.monsters[i].hp, 84);
    }
}

TEST(CardUncommonAttacksDirected, HemokinesisUnlocksReducedBloodForBloodPlay) {
    CombatState s = MakeCombat(/*energy=*/4, /*monster_hp=*/100);
    AddHand(s, CardId::HEMOKINESIS);
    const CardPoolIndex bfb = AddHand(s, CardId::BLOOD_FOR_BLOOD);

    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.player_hp, 78);
    EXPECT_EQ(s.monsters[0].hp, 85);
    ASSERT_EQ(s.hand_count, 1);
    EXPECT_EQ(s.hand[0], bfb);
    EXPECT_EQ(s.card_pool[bfb].cost_now, 3)
        << "Hemokinesis HP loss fired Blood for Blood.tookDamage";
    EXPECT_EQ(s.player_energy, 3);

    ActionMask mask{};
    legal_actions(s, mask);
    ASSERT_TRUE(mask.can_play[0]);
    Step(s, make_action(ActionVerb::PLAY_CARD, 0, 0));
    EXPECT_EQ(s.monsters[0].hp, 67);
    EXPECT_EQ(s.player_energy, 0);
}

}  // namespace
}  // namespace sts::engine
