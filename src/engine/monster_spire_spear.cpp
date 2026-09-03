// SpireSpear: the mirrored moveCount % 3 cycle, the ascension-branched BURN_STRIKE
// pile, the PIERCER all-allies fan-out and the skewerCount hit count. See
// monster_spire_spear.hpp for provenance and the four readings this body adds on
// top of the six its twin's header carries. The shared die() body it registers
// (spire_guard_die_after) is defined in monster_spire_shield.cpp.

#include "sts/engine/monster_spire_spear.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, CardPile, make_* flag helpers
#include "sts/engine/monster_dispatch.hpp"  // move helpers, queue_monster_move_effect*
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kBurnStrike = sts::registry::kSpireSpearMoveBurnStrike;  // 1
constexpr uint8_t kPiercer = sts::registry::kSpireSpearMovePiercer;        // 2
constexpr uint8_t kSkewer = sts::registry::kSpireSpearMoveSkewer;          // 3

// getMove (SpireSpear.java:116-140) -- header note (1). `num` is not a parameter
// because the Java body never reads it; the caller still spends the draw.
void spire_spear_decide_move(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    switch (spire_guard_move_count(m)) {
        case 0:
            // `if (!lastMove(BURN_STRIKE)) setMove(BURN_STRIKE, ATTACK_DEBUFF,
            //  damage.get(0).base, 2, true); else setMove(PIERCER, BUFF);`
            // (:119-126). The 5-arg setMove's `2` is the multi-hit COUNT shown on
            // the intent and `true` is isMultiDamage -- both presentation; the
            // recorded state is the move id and the intent.
            if (!last_move_is(m, kBurnStrike)) {
                set_monster_move(m, kBurnStrike, MonsterIntent::ATTACK_DEBUFF);
            } else {
                set_monster_move(m, kPiercer, MonsterIntent::BUFF);
            }
            break;
        case 1:
            // `setMove(SKEWER, ATTACK, damage.get(1).base, skewerCount, true)`
            // (:127-130) -- unconditional and drawless.
            set_monster_move(m, kSkewer, MonsterIntent::ATTACK);
            break;
        default:
            // `if (aiRng.randomBoolean()) setMove(PIERCER, BUFF);
            //  else setMove(BURN_STRIKE, ...)` (:131-137) -- the extra aiRng draw,
            // on case 2 where the Shield's sits on case 0.
            if (random_boolean(s.ai_rng)) {
                set_monster_move(m, kPiercer, MonsterIntent::BUFF);
            } else {
                set_monster_move(m, kBurnStrike, MonsterIntent::ATTACK_DEBUFF);
            }
            break;
    }
    spire_guard_bump_move_count(m);  // `++this.moveCount` (:139)
}

}  // namespace

void spire_spear_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m = MonsterState{};
    m.monster_id = static_cast<uint16_t>(MonsterId::SPIRE_SPEAR);
    // The `super(...)` HP argument is the LITERAL 160 (:50) -- no draw -- and the
    // setHp under it (:57-61) is the ONE monster_hp_rng draw, over a one-wide
    // range that still consumes the stream (monster_spire_shield.hpp note 1).
    // Together with the Shield's, this is the fight's SECOND and last HP draw.
    const int32_t hp =
        random(s.monster_hp_rng,
               sts::registry::kSpireSpear.hp_min(kMonsterAscension),
               sts::registry::kSpireSpear.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.draw_x = kSpireSpearDrawX;  // the ctor's offsetX (:50)
    // `moveCount = 0` (:42) and `skewerCount` (:63,:67) -- the latter needs no
    // storage: it is a pure function of the ascension (header note 3c), and this
    // engine resolves every tier at kMonsterAscension.
    //
    // init() -> rollMove -> getMove(aiRng.random(99)); `num` unread on every arm
    // and still SPENT.
    (void)random(s.ai_rng, 99);
    spire_spear_decide_move(s, mi);
}

void spire_spear_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // usePreBattleAction (:73-80) -- ARTIFACT ONLY, header note (2). No
    // Surrounded: SpireShield.java:71 is the game's only source of it, and no
    // BackAttack marker is queued here for the same reason (the Shield's
    // pre-battle, which runs FIRST because MonsterHelper builds the Shield first,
    // has already queued both).
    ActionQueueItem art{};
    art.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    art.src = mi;
    art.tgt = mi;
    art.amount = (kMonsterAscension >= 18) ? 2 : 1;  // (:75-79)
    art.flags = make_apply_power_flags(PowerId::ARTIFACT);
    add_to_bottom(s, art);
}

void spire_spear_take_turn(CombatState& s, uint8_t mi) noexcept {
    const uint8_t move = s.monsters[mi].move_history[0];
    const sts::registry::MonsterDef& def = sts::registry::kSpireSpear;

    if (move == kBurnStrike) {
        // (:85-97). TWO separate DamageActions on damage.get(0) (steps 0 and 1 --
        // the row authors them literally because BURN_STRIKE_COUNT is flat), each
        // preceded by presentation only, then the two Burns.
        for (int i = 0; i < kSpireSpearBurnStrikeCount; ++i) {
            queue_monster_move_effect(s, mi, def, kBurnStrike,
                                      static_cast<uint8_t>(i),
                                      kMoveTargetFromStep);
        }
        if (kMonsterAscension >= 18) {
            // `MakeTempCardInDrawPileAction(new Burn(), 2, false, true)` (:92) --
            // the 4-arg overload: randomSpot FALSE, toBottom FALSE
            // (MakeTempCardInDrawPileAction.java:44-46), so both copies go to the
            // TOP of the draw pile and NO cardRandomRng draw is spent. This is the
            // arm the row authors (step 2, pile DRAW).
            queue_monster_move_effect(s, mi, def, kBurnStrike, 2,
                                      kMoveTargetFromStep);
        } else {
            // `MakeTempCardInDiscardAction(new Burn(), 2)` (:95). THE ROW COULD
            // NOT CARRY THIS ARM -- the pile rides in a step's `extra`, which has
            // no tier column (header note 3a) -- so the item is built here with
            // the same packing queue_monster_move_effect uses: CardPile in `src`,
            // CardId in `flags`, the player in `tgt`. Unreachable at
            // kMonsterAscension 20 and written out so that a real threaded
            // ascension is a one-line change at the right site.
            ActionQueueItem burn{};
            burn.opcode = static_cast<uint16_t>(Opcode::MAKE_CARD);
            burn.src = static_cast<uint8_t>(CardPile::DISCARD);
            burn.tgt = kActorPlayer;
            burn.amount = 2;
            burn.flags = make_make_card_flags(static_cast<uint16_t>(CardId::BURN));
            add_to_bottom(s, burn);
        }
    } else if (move == kPiercer) {
        // (:98-103): `for (m : getMonsters().monsters) addToBottom(new
        //  ApplyPowerAction(m, this, new StrengthPower(m, 2), 2));` -- EVERY
        // member, ITSELF INCLUDED, no liveness filter. One authored SELF-targeted
        // +2 template, retargeted per member (header note 3b).
        for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
            queue_monster_move_effect(s, mi, def, kPiercer, 0, i);
        }
    } else if (move == kSkewer) {
        // (:104-111): `for (i = 0; i < skewerCount; ++i)` queueing ChangeState /
        // Wait / DamageAction each pass. SEPARATE hits, so block and every per-hit
        // power apply per hit (header note 4). The COUNT is the one number the row
        // could not carry (header note 3c).
        const int hits = spire_spear_skewer_count(kMonsterAscension);
        for (int i = 0; i < hits; ++i) {
            queue_monster_move_effect(s, mi, def, kSkewer, 0, kMoveTargetFromStep);
        }
    }
    // `addToBottom(new RollMoveAction(this))` (:113) -- OUTSIDE the switch, so
    // every move body reaches it; the twin of SpireShield.java:110 and the reason
    // this class registers a monster_roll_move_fn.
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

void spire_spear_roll_move(CombatState& s, uint8_t mi) noexcept {
    // RollMoveAction -> rollMove -> getMove(aiRng.random(99)); `num` unread.
    (void)random(s.ai_rng, 99);
    spire_spear_decide_move(s, mi);
}

}  // namespace sts::engine
