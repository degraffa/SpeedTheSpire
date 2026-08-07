// Hexaghost's orb-count move cycle. See the public header for the full Java
// provenance, the orb-modelling decision, and why the queued tail (changeState
// + BurnIncreaseAction) rides on the ROLL_MOVE item.

#include "sts/engine/monster_hexaghost.hpp"

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"  // card_def / card_cost / card_flags (Burn upgrade)
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/registry/monster_table.hpp"
#include "interp/interp_cards.hpp"  // op_make_card (BurnIncreaseAction's three Burns)

namespace sts::engine {
namespace {

// The game's byte move ids, from the generated table (Hexaghost.java:79-84).
constexpr uint8_t kDivider = sts::registry::kHexaghostMoveDivider;
constexpr uint8_t kTackle = sts::registry::kHexaghostMoveTackle;
constexpr uint8_t kInflame = sts::registry::kHexaghostMoveInflame;
constexpr uint8_t kSear = sts::registry::kHexaghostMoveSear;
constexpr uint8_t kActivate = sts::registry::kHexaghostMoveActivate;
constexpr uint8_t kInferno = sts::registry::kHexaghostMoveInferno;

// The literal `i < 6` of the Divider loop (Hexaghost.java:157) -- NOT
// infernoHits, which happens to share the value but is a separate field (:78).
constexpr int kDividerHits = 6;

// changeState "Activate" lights all six (Hexaghost.java:265-268); "Activate
// Orb" stops the cycle at this count (:279).
constexpr uint8_t kAllOrbsLit = 6;

// BurnIncreaseAction's three ShowCardAndAddToDiscardEffect copies
// (BurnIncreaseAction.java:39-48).
constexpr int kBurnIncreaseCopies = 3;

void set_orb_count(MonsterState& m, uint8_t count) noexcept {
    const uint32_t bits = (static_cast<uint32_t>(count)
                           << kMonsterFlagHexaghostOrbShift) &
                          kMonsterFlagHexaghostOrbMask;
    m.flags = (m.flags & ~kMonsterFlagHexaghostOrbMask) | bits;
}

// The two-item tail every move body but ACTIVATE ends with: ChangeStateAction
// then RollMoveAction (Hexaghost.java:166-167,175-176,187-188,195-196,208-209).
// Both halves resolve inside hexaghost_roll_move -- see the header.
void queue_tail(CombatState& s, uint8_t mi) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    it.src = mi;
    it.tgt = mi;
    add_to_bottom(s, it);
}

// SEAR's data program, re-queued rather than handed to
// queue_monster_move_effects because ONE bit of it is per-combat state: the
// created Burn is upgraded once `burnUpgraded` is set (Hexaghost.java:182-186).
// Everything else -- the 6 damage, the 1/2 burn count -- stays a registry
// column; only the upgraded bit is added here.
void queue_sear(CombatState& s, uint8_t mi, const sts::registry::MonsterDef& def,
                bool burn_upgraded) noexcept {
    const sts::registry::MonsterMove* mv = def.move(kSear);
    if (mv == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < mv->effect_count; ++i) {
        const sts::registry::MonsterMoveEffect& e = mv->effects[i];
        ActionQueueItem it{};
        it.opcode = static_cast<uint16_t>(e.op);
        it.src = mi;
        it.tgt = (e.target == sts::registry::MonsterMoveTarget::SELF)
                     ? mi
                     : kActorPlayer;
        it.amount = e.amount.at(kMonsterAscension);
        it.flags = e.extra;
        if (e.op == sts::registry::Opcode::MAKE_CARD) {
            // Same packing split as queue_monster_move_effects
            // (monster_dispatch.cpp): the destination pile rides in the step's
            // `extra` bits [16..23] and moves into the item's `src`.
            it.src = static_cast<uint8_t>((e.extra >> 16) & 0xFFu);
            it.tgt = kActorPlayer;
            if (burn_upgraded) {
                it.flags |= kMakeCardUpgradedBit;  // `c.upgrade()` (:184)
            }
        }
        add_to_bottom(s, it);
    }
}

// Upgrade every Burn in one pile, in place. `c.upgrade()` is a no-op on an
// already-upgraded Burn (Burn.java:58), so the count never passes 1; cost_now
// and flags are refreshed from the registry exactly as op_make_card does for a
// freshly created upgraded copy.
void upgrade_burns_in(CombatState& s, const CardPoolIndex* pile,
                      uint8_t count) noexcept {
    const auto burn = static_cast<uint16_t>(CardId::BURN);
    for (uint8_t i = 0; i < count; ++i) {
        CardInstance& c = s.card_pool[pile[i]];
        if (c.card_id != burn || c.upgrade != 0) {
            continue;
        }
        const CardDef* def = card_def(CardId::BURN);
        if (def == nullptr) {
            return;
        }
        c.upgrade = 1;
        c.cost_now = card_cost(*def, 1);
        c.flags = card_flags(*def, 1);
    }
}

// BurnIncreaseAction.update (BurnIncreaseAction.java:25-51). One action, two
// timed passes, no queue insertions in either -- so it is one step here:
//   duration == 3.0f : upgrade every Burn in the DISCARD pile, then every Burn
//                      in the DRAW pile (:28-35). NOT the hand, NOT exhaust.
//   duration <  1.5f : three fresh Burns, each upgraded and each pushed into
//                      the discard pile by ShowCardAndAddToDiscardEffect's
//                      CONSTRUCTOR (:39-48; the effect object is only the
//                      animation -- ShowCardAndAddToDiscardEffect.java:48 does
//                      `AbstractDungeon.player.discardPile.addToTop(card)`).
void burn_increase(CombatState& s) noexcept {
    upgrade_burns_in(s, s.discard, s.discard_count);
    upgrade_burns_in(s, s.draw, s.draw_count);
    op_make_card(s, static_cast<uint16_t>(CardId::BURN), CardPile::DISCARD,
                 kBurnIncreaseCopies, /*upgraded=*/true);
}

// getMove's selection (Hexaghost.java:224-252), keyed entirely on
// orbActiveCount. `activated` is already true for every call that reaches here
// (see the header), so the :220-222 branch is not reproduced. The Java switch
// carries NO default: a count outside 0..6 leaves the decided move alone, and
// that is reproduced by falling through.
void select_move_from_orbs(MonsterState& m) noexcept {
    switch (hexaghost_orb_count(m)) {
        case 0:  // :226
            set_monster_move(m, kSear, MonsterIntent::ATTACK_DEBUFF);
            return;
        case 1:  // :230
            set_monster_move(m, kTackle, MonsterIntent::ATTACK);
            return;
        case 2:  // :234
            set_monster_move(m, kSear, MonsterIntent::ATTACK_DEBUFF);
            return;
        case 3:  // :238
            set_monster_move(m, kInflame, MonsterIntent::DEFEND_BUFF);
            return;
        case 4:  // :242
            set_monster_move(m, kTackle, MonsterIntent::ATTACK);
            return;
        case 5:  // :246
            set_monster_move(m, kSear, MonsterIntent::ATTACK_DEBUFF);
            return;
        case 6:  // :250
            set_monster_move(m, kInferno, MonsterIntent::ATTACK_DEBUFF);
            return;
        default:
            return;  // unreachable: the 3-bit field only ever holds 0..6
    }
}

}  // namespace

uint8_t hexaghost_orb_count(const MonsterState& m) noexcept {
    return static_cast<uint8_t>((m.flags & kMonsterFlagHexaghostOrbMask) >>
                                kMonsterFlagHexaghostOrbShift);
}

bool hexaghost_burn_upgraded(const MonsterState& m) noexcept {
    return (m.flags & kMonsterFlagHexaghostBurnUpgraded) != 0;
}

uint8_t hexaghost_divider_damage(const MonsterState& m) noexcept {
    return m.pad0;
}

int32_t hexaghost_divider_base(int32_t player_hp) noexcept {
    // `AbstractDungeon.player.currentHealth / 12 + 1` (Hexaghost.java:151).
    // Java int division truncates; currentHealth is never negative in play, and
    // the clamp keeps a transient negative sheet from producing a smaller base
    // than the game's floor of 1.
    const int32_t hp = player_hp < 0 ? 0 : player_hp;
    return hp / 12 + 1;
}

void hexaghost_init(CombatState& s, uint8_t mi) noexcept {
    const auto& def = sts::registry::kHexaghost;
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::HEXAGHOST);
    // setHp(int) is a FIXED SHEET but NOT a free one. Both ctor branches
    // (Hexaghost.java:102-106) call setHp(int), which is literally
    // `setHp(hp, hp)` (AbstractMonster.java:777-779); the two-arg overload then
    // draws `monsterHpRng.random(minHp, maxHp)` unconditionally
    // (AbstractMonster.java:765-767). Random.random(int,int) increments the
    // counter and calls nextInt(end - start + 1) even when start == end
    // (Random.java:58-61), so a degenerate range still consumes one draw and
    // advances the stream. The rolled value can only be the endpoint -- the
    // observable is the STREAM, not the HP.
    const int32_t rolled = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                                  def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(rolled);
    m.max_hp = m.hp;
    m.block = 0;
    m.flags = 0;  // orbActiveCount 0, burnUpgraded false (:92-93)
    m.power_count = 0;
    m.pad0 = 0;  // no Divider base until the ACTIVATE turn computes one
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;

    // AbstractMonster.init -> rollMove -> aiRng.random(99), then getMove
    // (:218-222) ignores `num`: `activated` is still false, so the opener is
    // forced to ACTIVATE. The draw is consumed either way -- it is stream state.
    (void)random(s.ai_rng, 99);
    set_monster_move(m, kActivate, MonsterIntent::UNKNOWN);
}

void hexaghost_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    const auto& def = sts::registry::kHexaghost;
    switch (m.move_history[0]) {
        case kActivate: {
            // takeTurn case 5 (Hexaghost.java:148-154). The queued
            // ChangeStateAction("Activate") lights all six orbs and sets
            // orbActiveCount = 6 (:150,265-269). Applied here rather than
            // through the queue because this is the ONE body with no
            // RollMoveAction: getMove is never called between this turn and the
            // next, and the DIVIDER turn's own "Deactivate" resets the count to
            // 0 before anything reads it. Written anyway so a mid-fight
            // snapshot shows what the game shows.
            set_orb_count(m, kAllOrbsLit);

            // damage.get(2).base = player.currentHealth / 12 + 1 (:151), read
            // SYNCHRONOUSLY inside takeTurn -- i.e. the player's HP at the top
            // of Hexaghost's first turn, after the player's turn-1 plays have
            // resolved and before Divider's own hits land.
            const int32_t base = hexaghost_divider_base(s.player_hp);
            m.pad0 = static_cast<uint8_t>(base > 255 ? 255 : base);

            // A DIRECT setMove (:153) -- there is no RollMoveAction on this
            // path, so no aiRng draw happens between turn 1 and turn 2.
            set_monster_move(m, kDivider, MonsterIntent::ATTACK);
            return;
        }
        case kDivider: {
            // takeTurn case 1 (:156-168): SIX separate DamageActions of
            // damage.get(2) (:157-165), not one 6x hit -- each runs the
            // DamageInfo pipeline on its own, so per-hit block absorption and
            // Vulnerable rounding differ from a single stacked hit. The base is
            // the value stored on the ACTIVATE turn; the ascension sheet has no
            // say in it.
            const int32_t base = static_cast<int32_t>(m.pad0);
            for (int i = 0; i < kDividerHits; ++i) {
                ActionQueueItem it{};
                it.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
                it.src = mi;
                it.tgt = kActorPlayer;
                it.amount = base;
                it.flags = make_damage_flags(DamageType::NORMAL);
                add_to_bottom(s, it);
            }
            queue_tail(s, mi);  // ChangeState("Deactivate") + RollMove (:166-167)
            return;
        }
        case kSear:
            // takeTurn case 4 (:179-189).
            queue_sear(s, mi, def, hexaghost_burn_upgraded(m));
            queue_tail(s, mi);  // ChangeState("Activate Orb") + RollMove (:187-188)
            return;
        case kTackle:   // takeTurn case 2 (:170-177): 2 x damage.get(0)
        case kInflame:  // takeTurn case 3 (:191-197): block 12 + Strength
        case kInferno:  // takeTurn case 6 (:199-210): 6 x damage.get(3)
            queue_monster_move_effects(s, mi, def, m.move_history[0]);
            queue_tail(s, mi);
            return;
        default:
            return;
    }
}

void hexaghost_roll_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];

    // --- (1) the folded tail of the move that just acted --------------------
    switch (m.move_history[0]) {
        case kDivider:
            // ChangeStateAction(DEACTIVATE_ALL_ORBS) (:166,283-290).
            set_orb_count(m, 0);
            break;
        case kInferno:
            // BurnIncreaseAction (:204) resolves BEFORE the change of state.
            burn_increase(s);
            // `burnUpgraded = true` (:205-207) is synchronous in the Java
            // takeTurn; nothing between there and here reads it, and the first
            // reader is the NEXT Sear turn.
            m.flags |= kMonsterFlagHexaghostBurnUpgraded;
            set_orb_count(m, 0);  // ChangeStateAction(DEACTIVATE_ALL_ORBS) (:208)
            break;
        case kSear:
        case kTackle:
        case kInflame: {
            // ChangeStateAction(ACTIVATE_ORB) (:272-282): light the first unlit
            // orb and ++orbActiveCount. The per-orb `activated` booleans are
            // presentation, so only the count survives.
            const uint8_t lit = hexaghost_orb_count(m);
            if (lit < kAllOrbsLit) {
                set_orb_count(m, static_cast<uint8_t>(lit + 1));
            }
            if (hexaghost_orb_count(m) == kAllOrbsLit) {
                // changeState's own setMove(INFERNO) at the sixth orb (:280).
                // The getMove below decides the same move, so this call changes
                // no outcome -- but the Java really does setMove TWICE here, so
                // moveHistory really does take two INFERNO entries, and this
                // engine's 3-slot ring should show the same thing.
                set_monster_move(m, kInferno, MonsterIntent::ATTACK_DEBUFF);
            }
            break;
        }
        default:
            break;
    }

    // --- (2) RollMoveAction -> rollMove -> getMove(aiRng.random(99)) --------
    // getMove NEVER reads `num` (:218-254), so the draw's VALUE is discarded --
    // but the draw itself is stream state and must be consumed.
    (void)random(s.ai_rng, 99);
    select_move_from_orbs(m);
}

}  // namespace sts::engine
