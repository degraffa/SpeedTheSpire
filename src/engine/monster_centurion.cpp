// The Centurion: native move selection and turn body -- the first getMove in the
// roster that reads the LIVE MONSTER COUNT. See monster_centurion.hpp for
// provenance, the draw accounting and the duplicated-walk note.

#include "sts/engine/monster_centurion.hpp"

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem
#include "sts/engine/interp.hpp"            // Opcode
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, move-history helpers, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kSlash = sts::registry::kCenturionMoveSlash;      // 1
constexpr uint8_t kProtect = sts::registry::kCenturionMoveProtect;  // 2
constexpr uint8_t kFury = sts::registry::kCenturionMoveFury;        // 3

// The aliveCount walk (Centurion.java:134-138, duplicated verbatim at :150-154):
//
//     int aliveCount = 0;
//     for (AbstractMonster m : AbstractDungeon.getMonsters().monsters) {
//         if (m.isDying || m.isEscaping) continue;
//         ++aliveCount;
//     }
//
// INCLUDES THE CENTURION ITSELF -- there is no `m == this` exclusion, unlike
// GainBlockRandomMonsterAction's valid-target walk. It also visits DEAD RECORDS,
// which the game never removes from the group (MonsterGroup.java:35-40), and
// rejects them through the isDying/isEscaping pair that monster_dead_or_escaped
// is exactly.
[[nodiscard]] uint8_t alive_count(const CombatState& s) noexcept {
    uint8_t n = 0;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (!monster_basically_dead(s.monsters[i])) {
            ++n;
        }
    }
    return n;
}

// Arms 1 and 3's shared tail: with a living ally, block; alone, fury.
// `aliveCount > 1` counts SELF, so "> 1" means "somebody else is still here".
void protect_or_fury(CombatState& s, MonsterState& m) noexcept {
    if (alive_count(s) > 1) {
        set_monster_move(m, kProtect, MonsterIntent::DEFEND);  // (:139-140,:155-156)
    } else {
        // (:143-144,:159): setMove(FURY, ATTACK, damage.get(1).base,
        // furyHits, true) -- the `3, true` is the multi-hit TELEGRAPH; the three
        // hits themselves are the registry program's three DAMAGE steps.
        set_monster_move(m, kFury, MonsterIntent::ATTACK);
    }
}

// getMove (Centurion.java:132-160). No ascension branch anywhere in it. `num` is
// the aiRng.random(99) the caller already drew.
void centurion_get_move(CombatState& s, uint8_t mi, int32_t num) noexcept {
    MonsterState& m = s.monsters[mi];
    if (num >= 65 && !last_two_moves_are(m, kProtect) &&
        !last_two_moves_are(m, kFury)) {
        protect_or_fury(s, m);  // (:133-145)
        return;
    }
    if (!last_two_moves_are(m, kSlash)) {
        set_monster_move(m, kSlash, MonsterIntent::ATTACK);  // (:146-149)
        return;
    }
    protect_or_fury(s, m);  // (:150-159) -- the same two lines again
}

}  // namespace

void centurion_init(CombatState& s, uint8_t mi) noexcept {
    // The ctor is `super(...)` + setHp(min, max): exactly one monster_hp_rng
    // inclusive draw, currentHealth == maxHealth (AbstractMonster.java:765-775);
    // at A20 the A7 column (78, 83) is the live one (Centurion.java:57-61).
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(MonsterId::CENTURION);
    const int32_t hp = random(
        s.monster_hp_rng, sts::registry::kCenturion.hp_min(kMonsterAscension),
        sts::registry::kCenturion.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;  // unused by this monster
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
    // AbstractMonster.init -> rollMove -> getMove(aiRng.random(99)). This getMove
    // READS num on the first call -- there is no forced opener -- so the turn-1
    // move is seed-dependent, and an empty move history sends all three
    // lastTwoMoves predicates false.
    //
    // A SUBTLETY THE SPAWN ORDER MAKES REAL, AND WHY IT IS ALREADY HANDLED.
    // aliveCount is read HERE, during spawn -- and this engine FOLDS each
    // monster's ctor and init into one call, so a Centurion at slot 0 would
    // otherwise roll its opening move while slot 1 was still a zeroed record and
    // could never open on PROTECT. The game does NOT work that way: the group's
    // ctor constructs every member (MonsterGroup.java:31-33) and MonsterGroup.
    // init() then init()s them all in a SECOND pass (:62-66), so the very first
    // member's getMove already sees the whole group, alive and at full HP.
    // spawn_group closes that gap by pre-marking every slot of the group as
    // constructed-and-alive before it runs any init (monster_dispatch.cpp); this
    // monster and the Healer's needToHeal are the two readers that need it.
    centurion_get_move(s, mi, random(s.ai_rng, 99));
}

void centurion_roll_move(CombatState& s, uint8_t mi) noexcept {
    const int32_t num = random(s.ai_rng, 99);
    centurion_get_move(s, mi, num);
}

void centurion_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn (Centurion.java:82-110). Every case is presentation plus the
    // registry program: SLASH's playSfx / ChangeState("MACE_HIT") / Wait, FURY's
    // per-hit repeat of the same three, and PROTECT's bare WaitAction. playSfx is
    // UNSEEDED MathUtils (:113), so none of it costs a seeded draw. PROTECT's one
    // step is the BLOCK_RANDOM_MONSTER opcode, which resolves its recipient -- and
    // spends its conditional ai_rng draw -- at EXECUTE time. The RollMoveAction at
    // :107 sits OUTSIDE the switch, so all three cases reach it.
    queue_monster_move_effects(s, mi, sts::registry::kCenturion,
                               s.monsters[mi].move_history[0]);
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

}  // namespace sts::engine
