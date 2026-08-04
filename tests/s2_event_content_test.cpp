// Tier-2 suite for the S2 event-content IDENTITY rows: the eight Act-2/3
// event-reachable SPECIAL relics (registry/relics.yaml ids 143-150) and the five
// Act-2/3 event- and relic-granted SPECIAL cards (registry/cards.yaml ids
// 128-132).
//
// These are identity rows, so most of what is pinned here is identity: tier,
// rarity, colour, cost, target, flags, the translator join keys, and the
// registry-wide invariants an S2 row could break if it were authored wrong (a
// stray pool_order on a SPECIAL relic would insert an id into a shuffled pool
// and move every later relicRng draw; a stray non-SPECIAL rarity on one of the
// five cards would insert it into a generated card pool).
//
// Three things here are NOT identity, because the ledger block asks for them:
//   * Necronomicon's onEquip rider -- the only S2 relic body that lands with
//     this task, because the surface it needs (add_card_to_master_deck) already
//     exists and the pickup schema makes a listed-but-unwritten surface a link
//     error rather than a silent no-op.
//   * Necronomicurse's two unremovability sites, which are separate questions
//     with separate Java (purgeability at CardGroup.java:981, cursed-ness at
//     AbstractPlayer.java:744) and are asserted separately below.
//   * The asserted INERTNESS of every deferred body. Each row whose Java defines
//     a hook this task did not implement is pinned to bind nothing and do
//     nothing, so the task that writes the body sees a failing test rather than
//     silently changing behaviour nobody was watching.

#include <cstdint>

#include "gtest/gtest.h"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/event_framework.hpp"  // event_player_is_cursed
#include "sts/engine/interp.hpp"
#include "sts/engine/relic_hooks.hpp"
#include "sts/engine/relic_pools.hpp"      // acquire_relic
#include "sts/engine/relics.hpp"
#include "sts/engine/rest_sites.hpp"       // rest_card_purgeable
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"

namespace sts::engine {
namespace {

// The eight S2 SPECIAL relic rows, in registry id order.
constexpr RelicId kS2Relics[] = {
    RelicId::BLOODY_IDOL,        RelicId::ENCHIRIDION,
    RelicId::NILRYS_CODEX,       RelicId::NECRONOMICON,
    RelicId::MUTAGENIC_STRENGTH, RelicId::NLOTHS_GIFT,
    RelicId::RED_MASK,           RelicId::MARK_OF_THE_BLOOM,
};

// The five S2 card rows, in registry id order.
constexpr CardId kS2Cards[] = {
    CardId::APPARITION, CardId::BITE, CardId::JAX,
    CardId::RITUAL_DAGGER, CardId::NECRONOMICURSE,
};

RunState MakeRun() {
    RunState rs{};
    rs.hp = 50;
    rs.max_hp = 80;
    rs.gold = 10;
    return rs;
}

void push_card(RunState& rs, CardId id, uint8_t upgrade = 0) {
    CardInstance& c = rs.master_deck[rs.master_deck_count++];
    c = CardInstance{};
    c.card_id = static_cast<uint16_t>(id);
    c.upgrade = upgrade;
}

CombatState MakeState() {
    CombatState s{};
    s.player_hp = 70;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 50;
    s.monsters[0].max_hp = 50;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    return s;
}

RelicView give(CombatState& s, RelicId id) {
    s.relics[0] = RelicSlot{};
    s.relics[0].relic_id = static_cast<uint16_t>(id);
    s.relics[0].counter = -1;
    s.relic_count = 1;
    return player_relics(s);
}

// The generated CardEffectStep::op is sts::registry::Opcode, a distinct type
// from sts::engine::Opcode by design (cards.hpp carries the drift static_assert),
// so compare by underlying value the way card_play.cpp does.
bool StepOpIs(sts::registry::Opcode actual, Opcode expected) {
    return static_cast<uint16_t>(actual) == static_cast<uint16_t>(expected);
}

int card_step_count(CardId id, uint8_t upgrade) {
    const CardDef* d = card_def(id);
    return d == nullptr ? -1 : static_cast<int>(card_effect_steps(*d, upgrade).count);
}

}  // namespace

// ============================================================================
// Relic identity -- ids 143-150
// ============================================================================

// Every S2 relic row is RelicTier.SPECIAL with NO pool_order. That pairing is
// the load-bearing one: SPECIAL means "in no shuffled dungeon pool", and
// pool_order == -1 is how the generated table says so. A row that carried both a
// SPECIAL tier and a pool_order, or a non-SPECIAL tier, would insert an id into
// a shuffled tier and move every later relicRng draw for the whole run.
TEST(S2EventContent, EveryS2RelicIsSpecialTierAndInNoPool) {
    for (RelicId id : kS2Relics) {
        const sts::registry::RelicDef* d = relic_def(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
        EXPECT_EQ(d->tier, sts::registry::RelicTier::SPECIAL)
            << static_cast<int>(id);
        EXPECT_EQ(d->pool_order, -1) << static_cast<int>(id);
    }
}

// The registered SPECIAL tier is now CLOSED: RelicLibrary.initialize registers
// eighteen SPECIAL relics, and every one has a row. (RedCirclet is a nineteenth
// SPECIAL class but is never registered, so getRelic falls back to Circlet --
// the note on relics.yaml id 35.) The BOSS roster is re-asserted alongside it
// because these two tiers are the pair the pool invariant separates.
TEST(S2EventContent, SpecialTierIsClosedAtEighteenRows) {
    int special = 0;
    int boss = 0;
    for (const sts::registry::RelicDef* d : sts::registry::kRelicDefs) {
        if (d->tier == sts::registry::RelicTier::SPECIAL) {
            ++special;
            EXPECT_EQ(d->pool_order, -1)
                << "a SPECIAL relic belongs to no shuffled pool";
        } else if (d->tier == sts::registry::RelicTier::BOSS) {
            ++boss;
        }
    }
    // 11 before this task (9 Act-1 event specials + Circlet + Odd Mushroom)
    // + this task's 8.
    EXPECT_EQ(special, 19);
    EXPECT_EQ(boss, 22);
}

// The translator join keys. Four of the eight are NOT the class name and three
// carry punctuation the class name does not; getting one wrong makes relic
// translation throw rather than silently mismatch, which is why each is pinned.
TEST(S2EventContent, S2RelicGameIdsJoinTheOracleStrings) {
    EXPECT_EQ(sts::registry::relic_from_game_id("Bloody Idol"),
              RelicId::BLOODY_IDOL);
    EXPECT_EQ(sts::registry::relic_from_game_id("Enchiridion"),
              RelicId::ENCHIRIDION);
    // Apostrophe, and a space -- the class is NilrysCodex.
    EXPECT_EQ(sts::registry::relic_from_game_id("Nilry's Codex"),
              RelicId::NILRYS_CODEX);
    EXPECT_EQ(sts::registry::relic_from_game_id("Necronomicon"),
              RelicId::NECRONOMICON);
    // One word, NOT the display name "Mutagenic Strength".
    EXPECT_EQ(sts::registry::relic_from_game_id("MutagenicStrength"),
              RelicId::MUTAGENIC_STRENGTH);
    // Apostrophe the class name (NlothsGift) does not carry.
    EXPECT_EQ(sts::registry::relic_from_game_id("Nloth's Gift"),
              RelicId::NLOTHS_GIFT);
    EXPECT_EQ(sts::registry::relic_from_game_id("Red Mask"), RelicId::RED_MASK);
    EXPECT_EQ(sts::registry::relic_from_game_id("Mark of the Bloom"),
              RelicId::MARK_OF_THE_BLOOM);
    // The near-misses that must NOT join.
    EXPECT_EQ(sts::registry::relic_from_game_id("Mutagenic Strength"),
              RelicId::NONE);
    EXPECT_EQ(sts::registry::relic_from_game_id("Nloths Gift"), RelicId::NONE);
    EXPECT_EQ(sts::registry::relic_from_game_id("Nilrys Codex"), RelicId::NONE);
}

// EVERY S2 relic row is combat-inert: zero bound hooks, native == false, and
// dispatching every RelicHook at it queues nothing and moves no HP. Five of the
// eight DO have a combat or run-layer body in the Java (Bloody Idol's
// onGainGold, Enchiridion's atPreBattle, Nilry's Codex's onPlayerEndTurn,
// Mutagenic Strength's atBattleStart, Red Mask's atBattleStart, N'loth's Gift's
// reward-chance multiplier and Mark of the Bloom's onPlayerHeal); each is
// DEFERRED to the S2 task named in its registry row. This test is what makes
// that deferral visible: the task that writes a body has to come here and change
// it, instead of quietly binding a hook nobody re-checked.
TEST(S2EventContent, EveryS2RelicIsInertUntilItsBodyTaskLands) {
    for (RelicId id : kS2Relics) {
        const sts::registry::RelicDef* d = relic_def(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
        EXPECT_EQ(d->hook_count, 0) << "relic " << static_cast<int>(id);
        EXPECT_FALSE(d->native) << "relic " << static_cast<int>(id);

        CombatState s = MakeState();
        const RelicView rv = give(s, id);
        for (int h = 0; h < kRelicHookCount; ++h) {
            dispatch_relic_hook(s, rv.relics, rv.count,
                                static_cast<RelicHook>(h), RelicHookContext{});
        }
        EXPECT_EQ(s.action_count, 0) << "relic " << static_cast<int>(id);
        EXPECT_EQ(s.player_hp, 70) << "relic " << static_cast<int>(id);
        EXPECT_EQ(s.player_power_count, 0) << "relic " << static_cast<int>(id);
        EXPECT_EQ(s.monsters[0].power_count, 0)
            << "relic " << static_cast<int>(id);
    }
}

// None of the eight seeds a counter. Necronomicon is the one that could look
// like it does -- its `activated` latch (Necronomicon.java:28) is a PRIVATE
// boolean, not AbstractRelic.counter, which the class never writes -- so the
// observable counter stays AbstractRelic's inherited -1 and a stray
// initial_counter here would be a live CommunicationMod divergence on every
// acquisition path (the Centennial Puzzle STS00068 incident).
TEST(S2EventContent, NoS2RelicSeedsACounter) {
    for (RelicId id : kS2Relics) {
        const sts::registry::RelicDef* d = relic_def(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
        EXPECT_EQ(d->initial_counter, -1) << static_cast<int>(id);
    }
}

// ============================================================================
// Necronomicon's onEquip rider -- the one S2 relic body that lands here
// ============================================================================

// Necronomicon.onEquip (Necronomicon.java:39-45) adds a Necronomicurse to the
// master deck through ShowCardAndObtainEffect, i.e. the ordinary obtain door.
// The plain 3-argument acquire_relic runs it (this is an on_equip, not an
// on_equip_screen: there is no confirmation grid), the curse APPENDS at the end
// of the deck, and no rng is spent.
TEST(S2EventContent, NecronomiconEquipAppendsItsCurseAndSpendsNoRng) {
    RunState rs = MakeRun();
    push_card(rs, CardId::STRIKE);
    push_card(rs, CardId::DEFEND);
    RngStream misc = from_seed(7);

    ASSERT_EQ(acquire_relic(rs, misc, RelicId::NECRONOMICON),
              RelicAcquireResult::ACQUIRED);
    ASSERT_EQ(rs.relic_count, 1);
    EXPECT_EQ(rs.relics[0].relic_id,
              static_cast<uint16_t>(RelicId::NECRONOMICON));
    EXPECT_EQ(rs.relics[0].counter, -1);

    ASSERT_EQ(rs.master_deck_count, 3);
    EXPECT_EQ(rs.master_deck[2].card_id,
              static_cast<uint16_t>(CardId::NECRONOMICURSE));
    EXPECT_EQ(rs.master_deck[2].upgrade, 0);
    EXPECT_EQ(misc.counter, 0);
}

// ShowCardAndObtainEffect's constructor (ShowCardAndObtainEffect.java:30-45)
// tests Omamori BEFORE anything else and, for a CURSE-coloured card with a
// charge left, spends the charge and marks itself done in the constructor -- so
// update() never runs and the curse never reaches the deck. add_card_to_master_
// deck is that door, so a held Omamori charge eats the Necronomicurse exactly as
// it eats Calling Bell's.
TEST(S2EventContent, AHeldOmamoriChargeEatsTheNecronomicurse) {
    RunState rs = MakeRun();
    push_card(rs, CardId::STRIKE);
    rs.relics[0].relic_id = static_cast<uint16_t>(RelicId::OMAMORI);
    rs.relics[0].counter = 2;
    rs.relic_count = 1;
    RngStream misc = from_seed(7);

    ASSERT_EQ(acquire_relic(rs, misc, RelicId::NECRONOMICON),
              RelicAcquireResult::ACQUIRED);
    EXPECT_EQ(rs.master_deck_count, 1);  // the curse never landed
    EXPECT_EQ(rs.relics[0].counter, 1);  // one charge spent
}

// ============================================================================
// Card identity -- ids 128-132
// ============================================================================

// Every S2 card row is CardRarity.SPECIAL. This is the single fact that keeps
// all five out of every generated pool: addColorlessCards (AbstractDungeon.java:
// 1203-1210) skips BASIC and SPECIAL, and the generator's pool filters read the
// rarity column. A wrong rarity here would silently add a member to a live pool
// and move a cardRng draw.
TEST(S2EventContent, EveryS2CardIsSpecialRarityAndJoinsNoPool) {
    for (CardId id : kS2Cards) {
        const CardDef* d = card_def(id);
        ASSERT_NE(d, nullptr) << static_cast<int>(id);
    }
    // The four pools an S2 card could have leaked into, with the counts they
    // held before this task.
    EXPECT_EQ(sts::registry::kColorlessCombatPoolCount, 34);
    EXPECT_EQ(sts::registry::kColorlessUncommonPoolCount, 20);
    EXPECT_EQ(sts::registry::kColorlessRarePoolCount, 15);
    EXPECT_EQ(sts::registry::kPoolableCurseCount, 10);
    for (CardId id : sts::registry::kColorlessPool) {
        for (CardId s2 : kS2Cards) {
            EXPECT_NE(id, s2) << "an S2 SPECIAL card leaked into a card pool";
        }
    }
    for (CardId id : sts::registry::kPoolableCurses) {
        EXPECT_NE(id, CardId::NECRONOMICURSE)
            << "Necronomicurse is excluded from the curse pool BY NAME "
               "(AbstractDungeon.java:1215, CardLibrary.java:1025/:1034), on "
               "top of the SPECIAL-rarity rule";
    }
}

// The translator join keys. Three of the five are not the class name.
TEST(S2EventContent, S2CardGameIdsJoinTheOracleStrings) {
    // Apparition's cardID is "Ghostly" (Apparition.java:22).
    EXPECT_EQ(sts::registry::card_from_game_id("Ghostly"), CardId::APPARITION);
    EXPECT_EQ(sts::registry::card_from_game_id("Apparition"), CardId::NONE);
    EXPECT_EQ(sts::registry::card_from_game_id("Bite"), CardId::BITE);
    // Three dots, all of them significant (JAX.java:23).
    EXPECT_EQ(sts::registry::card_from_game_id("J.A.X."), CardId::JAX);
    EXPECT_EQ(sts::registry::card_from_game_id("JAX"), CardId::NONE);
    // One word (RitualDagger.java:22), not "Ritual Dagger".
    EXPECT_EQ(sts::registry::card_from_game_id("RitualDagger"),
              CardId::RITUAL_DAGGER);
    EXPECT_EQ(sts::registry::card_from_game_id("Ritual Dagger"), CardId::NONE);
    EXPECT_EQ(sts::registry::card_from_game_id("Necronomicurse"),
              CardId::NECRONOMICURSE);
}

// Apparition (Apparition.java:25-44): cost 1 SKILL, SELF, exhaust AND ethereal;
// one Intangible stack; the upgrade drops ETHEREAL ONLY -- it touches neither
// cost nor amount nor exhaust, so the two effect programs are identical and the
// whole delta is a flag.
TEST(S2EventContent, ApparitionIsAnEtherealExhaustIntangibleWhoseUpgradeIsAFlag) {
    const CardDef* d = card_def(CardId::APPARITION);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, sts::registry::CardType::SKILL);
    EXPECT_EQ(card_cost(*d, 0), 1);
    EXPECT_EQ(card_cost(*d, 1), 1);
    EXPECT_FALSE(d->needs_target);

    const uint16_t base_flags = card_flags(*d, 0);
    const uint16_t up_flags = card_flags(*d, 1);
    EXPECT_TRUE(has_card_flag(base_flags, CardFlag::EXHAUST));
    EXPECT_TRUE(has_card_flag(base_flags, CardFlag::ETHEREAL));
    EXPECT_TRUE(has_card_flag(up_flags, CardFlag::EXHAUST));
    EXPECT_FALSE(has_card_flag(up_flags, CardFlag::ETHEREAL));

    for (uint8_t u = 0; u < 2; ++u) {
        const CardEffectView v = card_effect_steps(*d, u);
        ASSERT_EQ(v.count, 1) << int{u};
        EXPECT_TRUE(StepOpIs(v.steps[0].op, Opcode::APPLY_POWER));
        EXPECT_EQ(v.steps[0].amount, 1);
        EXPECT_EQ(apply_power_id_from_flags(v.steps[0].extra),
                  PowerId::INTANGIBLE);
    }
}

// Bite (Bite.java:33-59): cost 1 ATTACK at one enemy, 7 damage then a QUEUED
// heal of 2; upgradeDamage(1) and upgradeMagicNumber(1) make that 8 and 3. The
// order matters -- damage is queued first -- and the heal is queued rather than
// applied inline, which is what routes it through the onPlayerHeal relic seam.
TEST(S2EventContent, BiteDamagesThenHealsAndBothHalvesUpgrade) {
    const CardDef* d = card_def(CardId::BITE);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, sts::registry::CardType::ATTACK);
    EXPECT_EQ(card_cost(*d, 0), 1);
    EXPECT_EQ(card_cost(*d, 1), 1);
    EXPECT_TRUE(d->needs_target);
    EXPECT_EQ(card_flags(*d, 0), 0u);  // no exhaust: Bite is reusable

    const CardEffectView base = card_effect_steps(*d, 0);
    ASSERT_EQ(base.count, 2);
    EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::DAMAGE));
    EXPECT_EQ(base.steps[0].amount, 7);
    EXPECT_TRUE(StepOpIs(base.steps[1].op, Opcode::HEAL));
    EXPECT_EQ(base.steps[1].amount, 2);

    const CardEffectView up = card_effect_steps(*d, 1);
    ASSERT_EQ(up.count, 2);
    EXPECT_EQ(up.steps[0].amount, 8);
    EXPECT_EQ(up.steps[1].amount, 3);
}

// J.A.X. (JAX.java:26-42): cost 0 SKILL, SELF, NO exhaust; LoseHPAction(3) --
// the literal 3, which the upgrade never touches -- then Strength equal to the
// magic number, 2 base and 3 upgraded. The HP loss is queued FIRST.
TEST(S2EventContent, JaxLosesThreeHpThenGainsStrengthAndOnlyStrengthUpgrades) {
    const CardDef* d = card_def(CardId::JAX);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, sts::registry::CardType::SKILL);
    EXPECT_EQ(card_cost(*d, 0), 0);
    EXPECT_EQ(card_cost(*d, 1), 0);
    EXPECT_FALSE(d->needs_target);
    EXPECT_EQ(card_flags(*d, 0), 0u);
    EXPECT_EQ(card_flags(*d, 1), 0u);

    const CardEffectView base = card_effect_steps(*d, 0);
    ASSERT_EQ(base.count, 2);
    EXPECT_TRUE(StepOpIs(base.steps[0].op, Opcode::LOSE_HP));
    EXPECT_EQ(base.steps[0].amount, 3);
    EXPECT_TRUE(StepOpIs(base.steps[1].op, Opcode::APPLY_POWER));
    EXPECT_EQ(base.steps[1].amount, 2);
    EXPECT_EQ(apply_power_id_from_flags(base.steps[1].extra), PowerId::STRENGTH);

    const CardEffectView up = card_effect_steps(*d, 1);
    ASSERT_EQ(up.count, 2);
    EXPECT_EQ(up.steps[0].amount, 3);  // the HP loss does NOT upgrade
    EXPECT_EQ(up.steps[1].amount, 3);
}

// Ritual Dagger's identity is authored (cost 1 exhausting ATTACK at one enemy),
// but its EFFECT PROGRAM is deliberately EMPTY at both levels: the card's damage
// is per-instance run-persistent state in AbstractCard.misc that grows on a kill
// (RitualDaggerAction.java:34-58), which needs a bespoke opcode and a writer for
// CardInstance.misc, neither of which exists. The empty program is the loud
// spelling of that gap -- a bare 15-damage attack would be a silent one -- and
// this test is what the body task (S2.31, Nest) has to come back and change.
TEST(S2EventContent, RitualDaggerCarriesItsIdentityAndAnExplicitlyEmptyProgram) {
    const CardDef* d = card_def(CardId::RITUAL_DAGGER);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, sts::registry::CardType::ATTACK);
    EXPECT_EQ(card_cost(*d, 0), 1);
    EXPECT_EQ(card_cost(*d, 1), 1);
    EXPECT_TRUE(d->needs_target);
    EXPECT_TRUE(has_card_flag(card_flags(*d, 0), CardFlag::EXHAUST));
    EXPECT_TRUE(has_card_flag(card_flags(*d, 1), CardFlag::EXHAUST));
    EXPECT_EQ(card_step_count(CardId::RITUAL_DAGGER, 0), 0);
    EXPECT_EQ(card_step_count(CardId::RITUAL_DAGGER, 1), 0);
}

// ============================================================================
// Necronomicurse -- the two unremovability sites, which are different questions
// ============================================================================

// Necronomicurse is an unplayable CURSE with no effect program and, unlike
// Ascender's Bane, NO ethereal flag -- it stays in hand (the Curse of the Bell
// shape). Its triggerOnExhaust respawn (Necronomicurse.java:43-49) is deferred:
// CardTrigger has no ON_EXHAUST member, so the row runs its (empty) program on
// the default ON_PLAY trigger it can never reach.
TEST(S2EventContent, NecronomicurseIsAnInertUnplayableNonEtherealCurse) {
    const CardDef* d = card_def(CardId::NECRONOMICURSE);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->type, sts::registry::CardType::CURSE);
    const uint16_t flags = card_flags(*d, 0);
    EXPECT_TRUE(has_card_flag(flags, CardFlag::UNPLAYABLE));
    EXPECT_FALSE(has_card_flag(flags, CardFlag::ETHEREAL));
    EXPECT_FALSE(has_card_flag(flags, CardFlag::EXHAUST));
    EXPECT_EQ(card_step_count(CardId::NECRONOMICURSE, 0), 0);
}

// CardGroup.getPurgeableCards (CardGroup.java:978-985) excludes exactly three
// ids by name at :981. All three are registry rows now, so the mirror is
// complete: no rest-site, event or shop purge can remove any of them, while an
// ordinary curse stays purgeable.
TEST(S2EventContent, NecronomicurseIsNotPurgeableAndTheExclusionListIsClosed) {
    const CardId unpurgeable[] = {CardId::NECRONOMICURSE,
                                  CardId::CURSE_OF_THE_BELL,
                                  CardId::ASCENDERS_BANE};
    for (CardId id : unpurgeable) {
        CardInstance c{};
        c.card_id = static_cast<uint16_t>(id);
        EXPECT_FALSE(rest_card_purgeable(c)) << static_cast<int>(id);
    }
    CardInstance ordinary{};
    ordinary.card_id = static_cast<uint16_t>(CardId::STRIKE);
    EXPECT_TRUE(rest_card_purgeable(ordinary));
}

// AbstractPlayer.isCursed (AbstractPlayer.java:741-748) is a DIFFERENT question
// from purgeability, with its own exclusion list at :744 -- and Necronomicurse is
// on it. A deck holding only Necronomicurse is therefore NOT cursed, so the
// Fountain of Cleansing's gate stays shut; one ordinary curse alongside it opens
// the gate.
TEST(S2EventContent, NecronomicurseAloneDoesNotMakeThePlayerCursed) {
    RunState rs = MakeRun();
    push_card(rs, CardId::STRIKE);
    push_card(rs, CardId::NECRONOMICURSE);
    EXPECT_FALSE(event_player_is_cursed(rs));

    push_card(rs, CardId::CURSE_OF_THE_BELL);
    push_card(rs, CardId::ASCENDERS_BANE);
    EXPECT_FALSE(event_player_is_cursed(rs));

    push_card(rs, CardId::REGRET);
    EXPECT_TRUE(event_player_is_cursed(rs));
}

}  // namespace sts::engine
