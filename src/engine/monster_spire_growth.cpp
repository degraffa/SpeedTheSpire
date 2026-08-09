// The Spire Growth: native move selection and turn body. See
// monster_spire_growth.hpp for provenance, the five-gate getMove and the draw
// accounting.

#include "sts/engine/monster_spire_growth.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kTackle = sts::registry::kSpireGrowthMoveQuickTackle;   // 1
constexpr uint8_t kConstrict = sts::registry::kSpireGrowthMoveConstrict;  // 2
constexpr uint8_t kSmash = sts::registry::kSpireGrowthMoveSmash;          // 3

// `AbstractDungeon.player.hasPower("Constricted")` (SpireGrowth.java:102,110).
// A plain presence walk over the player's slot list -- the same shape as
// find_power in powers/power_native.hpp, kept local because that header is the
// native-POWER plumbing and a monster module has no business including it. Two
// callers in one function is what earns it a name (conventions section 7); one
// MONSTER is not what earns it a place in monster_dispatch.hpp.
[[nodiscard]] bool player_has_constricted(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id ==
            static_cast<uint16_t>(PowerId::CONSTRICTED)) {
            return true;
        }
    }
    return false;
}

// The engine's live entry point: decide at the fixed ascension, reading the real
// player power list.
void spire_growth_get_move(CombatState& s, MonsterState& m,
                           int32_t num) noexcept {
    spire_growth_decide_move(m, num, player_has_constricted(s),
                             kMonsterAscension);
}

}  // namespace

// getMove (SpireGrowth.java:100-119). Five ordered gates, each an early return;
// the Java writes them as five `if (...) { setMove(...); return; }` blocks plus a
// fallthrough, and that structure is preserved literally because the ORDER is
// the whole content -- every gate but the last can be reached only by every
// earlier one having failed.
//
// `num` is the aiRng.random(99) the caller already drew.
void spire_growth_decide_move(MonsterState& m, int32_t num,
                              bool player_constricted,
                              int32_t ascension) noexcept {
    // (1) A17+ ONLY: the Constrict gate, HOISTED above the num branch (:102-105).
    // Identical in condition to gate (3) below -- what A17 buys is priority, not
    // a new option. At kMonsterAscension this is always live, so an unafflicted
    // player is Constricted at the first non-repeat opportunity and the `num`
    // roll never gets a say on the opening move.
    if (ascension >= 17 && !player_constricted && !last_move_is(m, kConstrict)) {
        set_monster_move(m, kConstrict, MonsterIntent::STRONG_DEBUFF);
        return;
    }
    // (2) The only gate that reads the roll (:106-109). lastTwoMoves, not
    // lastMove: two tackles in a row are allowed, three are not.
    if (num < 50 && !last_two_moves_are(m, kTackle)) {
        set_monster_move(m, kTackle, MonsterIntent::ATTACK);
        return;
    }
    // (3) The unhoisted Constrict gate (:110-113). Below A17 this is the ONLY
    // one, which is the entire behavioural difference the ascension makes: the
    // player gets Constricted later, not never.
    if (!player_constricted && !last_move_is(m, kConstrict)) {
        set_monster_move(m, kConstrict, MonsterIntent::STRONG_DEBUFF);
        return;
    }
    // (4) Smash unless it was the last two decisions (:114-117).
    if (!last_two_moves_are(m, kSmash)) {
        set_monster_move(m, kSmash, MonsterIntent::ATTACK);
        return;
    }
    // (5) The fallthrough (:118). Note it can produce a THIRD consecutive
    // Quick Tackle even though gate (2) refuses to -- gate (2)'s lastTwoMoves
    // test guards only its own branch.
    set_monster_move(m, kTackle, MonsterIntent::ATTACK);
}

void spire_growth_init(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::SPIRE_GROWTH);
    // setHp(190) / setHp(170) -- the ONE-ARG overload (SpireGrowth.java:53-57),
    // which is `setHp(hp, hp)` (AbstractMonster.java:777-779) and therefore
    // STILL ONE monsterHpRng draw over a degenerate range. The Hexaghost shape.
    // Do NOT carry the Spheric Guardian's skip-the-draw reasoning over: that
    // monster never calls setHp at all, and so do the Transient and the Maw in
    // this very batch.
    const int32_t hp = random(
        s.monster_hp_rng, sts::registry::kSpireGrowth.hp_min(kMonsterAscension),
        sts::registry::kSpireGrowth.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // unused by this monster: its AI is history + num + a player query
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). The value IS
    // read (gate 2 consults it), so unlike the Guardian / Looter / Transient this
    // draw is not discarded -- though at A20 gate 1 answers first from an empty
    // history and an unafflicted player, so the opening telegraph is CONSTRICT
    // whatever the roll says.
    spire_growth_get_move(s, m, random(s.ai_rng, 99));
}

void spire_growth_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    spire_growth_get_move(s, s.monsters[mi], num);
}

void spire_growth_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (SpireGrowth.java:74-98). The Animate*AttackAction /
    // ChangeStateAction / WaitAction around the real steps are presentation and
    // draw no seeded RNG. The RollMoveAction at :97 sits OUTSIDE the switch, so
    // all three cases reach it.
    const uint8_t move = s.monsters[mi].move_history[0];
    if (move == kConstrict) {
        // CONSTRICT IS HAND-QUEUED, and the reason is the SOURCE.
        //
        //   new ConstrictedPower(AbstractDungeon.player, THIS, constrictDmg)
        //
        // is a THREE-arg ctor whose middle argument is the applying creature
        // (:85,88), and that source is observable: ConstrictedPower's own tick
        // builds its DamageInfo with the SOURCE as owner, and
        // dispatch_was_hp_lost's Rupture guard fires only when source == victim.
        // The effect grammar has no way to name "the acting monster's slot
        // index", so the row authors the AMOUNT (which is all it can pin) and
        // this queues the item with the index packed into the APPLY_POWER
        // counter operand -- where op_apply_power's new-slot path writes it into
        // PowerSlot.counter. See power_constricted.hpp.
        const sts::registry::MonsterMove* mv =
            sts::registry::kSpireGrowth.move(kConstrict);
        // The guard SKIPS THE APPLY, it does not RETURN: the RollMoveAction
        // below is outside the Java's switch and every case reaches it, so an
        // early return here would leave the monster without a fresh telegraph
        // -- a stuck-move state strictly worse than the missing apply it is
        // guarding. Unreachable either way (the row above guarantees one step).
        if (mv != nullptr && mv->effect_count != 0) {
            ActionQueueItem apply{};
            apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
            apply.src = mi;
            apply.tgt = kActorPlayer;
            apply.amount = mv->effects[0].amount.at(kMonsterAscension);
            apply.flags = make_apply_power_flags(PowerId::CONSTRICTED,
                                                 /*counter=*/static_cast<int>(mi));
            add_to_bottom(s, apply);
        }
    } else {
        queue_monster_move_effects(s, mi, sts::registry::kSpireGrowth, move);
    }
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
