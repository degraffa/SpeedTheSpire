// The five Act-1 gremlins: native move selection + turn bodies. See
// monster_gremlin.hpp for provenance, the unreachable-escape finding, the
// per-stream draw accounting, and why the Tsundere's block target is rolled
// inside takeTurn.

#include "sts/engine/monster_gremlin.hpp"

#include <cassert>

#include "sts/engine/action_queue.hpp"      // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/interp.hpp"            // Opcode, kBlockNoPowers, make_apply_power_flags
#include "sts/engine/monster_dispatch.hpp"  // queue_monster_move_effects, set_monster_move, kMonsterAscension
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"

namespace sts::engine {
namespace {

constexpr uint8_t kScratch = sts::registry::kGremlinWarriorMoveScratch;    // 1
constexpr uint8_t kPuncture = sts::registry::kGremlinThiefMovePuncture;    // 1
constexpr uint8_t kBlunt = sts::registry::kGremlinFatMoveBlunt;            // 2
constexpr uint8_t kProtect = sts::registry::kGremlinTsundereMoveProtect;   // 1
constexpr uint8_t kBash = sts::registry::kGremlinTsundereMoveBash;         // 2
constexpr uint8_t kDopeMagic = sts::registry::kGremlinWizardMoveDopeMagic; // 1
constexpr uint8_t kCharge = sts::registry::kGremlinWizardMoveCharge;       // 2

// GremlinWizard.CHARGE_LIMIT (GremlinWizard.java:42): the third charge tick is
// the one that telegraphs Ultimate Blast.
constexpr uint8_t kChargeLimit = 3;
// GremlinWizard.currentCharge's field initialiser (GremlinWizard.java:43).
constexpr uint8_t kChargeStart = 1;

// Every gremlin ctor is `super(...)` + setHp(min, max): exactly one
// monster_hp_rng inclusive draw, currentHealth == maxHealth
// (AbstractMonster.java:765-775). No gremlin adds a starting power.
void init_common(CombatState& s, uint8_t mi, MonsterId id,
                 const sts::registry::MonsterDef& def) noexcept {
    MonsterState& m = s.monsters[mi];
    m.monster_id = static_cast<uint16_t>(id);
    const int32_t hp = random(s.monster_hp_rng, def.hp_min(kMonsterAscension),
                              def.hp_max(kMonsterAscension));
    m.hp = static_cast<int16_t>(hp);
    m.max_hp = static_cast<int16_t>(hp);
    m.block = 0;
    m.flags = 0;
    m.power_count = 0;
    m.pad0 = 0;
    m.move_history[0] = 0;
    m.move_history[1] = 0;
    m.move_history[2] = 0;
}

// AbstractMonster.init -> rollMove -> getMove(aiRng.random(99))
// (AbstractMonster.java:705-715). All five gremlin getMove overrides ignore
// `num` and force one fixed opening move, but the draw still happens and still
// moves the stream, so it is taken and discarded.
void init_roll_discarded(CombatState& s, MonsterState& m, uint8_t move,
                         MonsterIntent intent) noexcept {
    (void)random(s.ai_rng, 99);
    set_monster_move(m, move, intent);
}

// SetMoveAction (SetMoveAction.java:52-56): a QUEUED setMove, resolved after the
// turn's other actions rather than synchronously.
void queue_set_move(CombatState& s, uint8_t mi, uint8_t move,
                    MonsterIntent intent) noexcept {
    ActionQueueItem it{};
    it.opcode = static_cast<uint16_t>(Opcode::SET_MOVE);
    it.src = mi;
    it.tgt = mi;
    it.amount = move;
    it.flags = static_cast<uint32_t>(intent);
    add_to_bottom(s, it);
}

// A monster record counts as alive exactly when the Java's !isDying holds: die()
// runs the moment currentHealth hits 0 (AbstractMonster.java:925-935) and dead
// records stay in the group. isEscaping is likewise false for every gremlin here
// (monster_gremlin.hpp note (1)).
[[nodiscard]] bool record_alive(const CombatState& s, uint8_t i) noexcept {
    return i < s.monster_count && s.monsters[i].hp > 0;
}

}  // namespace

// --- Gremlin Warrior ---------------------------------------------------------

void gremlin_warrior_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::GREMLIN_WARRIOR,
                sts::registry::kGremlinWarrior);
    // GremlinWarrior.getMove (:130-132): unconditional Scratch.
    init_roll_discarded(s, s.monsters[mi], kScratch, MonsterIntent::ATTACK);
}

void gremlin_warrior_use_pre_battle_action(CombatState& s, uint8_t mi) noexcept {
    // GremlinWarrior.usePreBattleAction (:63-70): addToBottom ApplyPowerAction(
    // this, this, new AngryPower(this, ascension >= 17 ? 2 : 1)). The stack size
    // is an ascension if/else on a POWER amount, which the registry's tier
    // columns (HP ranges, roll ranges, move-effect amounts) do not cover -- so
    // the branch is spelled out here, mirroring the Java's shape.
    const int32_t angry_stacks = (kMonsterAscension >= 17) ? 2 : 1;
    ActionQueueItem apply{};
    apply.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
    apply.src = mi;
    apply.tgt = mi;
    apply.amount = angry_stacks;
    apply.flags = make_apply_power_flags(PowerId::ANGRY);
    add_to_bottom(s, apply);
}

void gremlin_warrior_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn case SCRATCH (:75-84): DamageAction, then a QUEUED SetMoveAction
    // re-asserting Scratch (the escapeNext branch is unreachable, note (1)).
    queue_monster_move_effects(s, mi, sts::registry::kGremlinWarrior, kScratch);
    queue_set_move(s, mi, kScratch, MonsterIntent::ATTACK);
}

// --- Gremlin Thief -----------------------------------------------------------

void gremlin_thief_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::GREMLIN_THIEF, sts::registry::kGremlinThief);
    // GremlinThief.getMove (:117-118): unconditional Puncture.
    init_roll_discarded(s, s.monsters[mi], kPuncture, MonsterIntent::ATTACK);
}

void gremlin_thief_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn case PUNCTURE (:62-71): DamageAction, then a QUEUED SetMoveAction.
    queue_monster_move_effects(s, mi, sts::registry::kGremlinThief, kPuncture);
    queue_set_move(s, mi, kPuncture, MonsterIntent::ATTACK);
}

// --- Gremlin Fat -------------------------------------------------------------

void gremlin_fat_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::GREMLIN_FAT, sts::registry::kGremlinFat);
    // GremlinFat.getMove (:130-131): unconditional Blunt.
    init_roll_discarded(s, s.monsters[mi], kBlunt, MonsterIntent::ATTACK_DEBUFF);
}

void gremlin_fat_roll_move(CombatState& s, uint8_t mi) noexcept {
    (void)random(s.ai_rng, 99);  // rollMove's draw; getMove ignores the value
    set_monster_move(s.monsters[mi], kBlunt, MonsterIntent::ATTACK_DEBUFF);
}

void gremlin_fat_take_turn(CombatState& s, uint8_t mi) noexcept {
    // takeTurn case BLUNT (:69-82), in addToBottom order: DamageAction,
    // ApplyPowerAction(Weak 1), [ascension >= 17] ApplyPowerAction(Frail 1),
    // RollMoveAction. The first two are the registry program; the Frail step is
    // conditional on the ascension BRANCH rather than on an amount, which a tier
    // column cannot express, so it is queued here (GremlinFat.java:73-75).
    queue_monster_move_effects(s, mi, sts::registry::kGremlinFat, kBlunt);
    if (kMonsterAscension >= 17) {
        ActionQueueItem frail{};
        frail.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
        frail.src = mi;
        frail.tgt = kActorPlayer;
        frail.amount = 1;  // new FrailPower(player, 1, true)
        frail.flags = make_apply_power_flags(PowerId::FRAIL);
        add_to_bottom(s, frail);
    }
    // The one gremlin whose turn ends in a real RollMoveAction (:80).
    ActionQueueItem roll{};
    roll.opcode = static_cast<uint16_t>(Opcode::ROLL_MOVE);
    roll.src = mi;
    roll.tgt = mi;
    add_to_bottom(s, roll);
}

// --- Gremlin Tsundere --------------------------------------------------------

void gremlin_tsundere_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::GREMLIN_TSUNDERE,
                sts::registry::kGremlinTsundere);
    // GremlinTsundere.getMove (:123-124): unconditional Protect.
    init_roll_discarded(s, s.monsters[mi], kProtect, MonsterIntent::DEFEND);
}

void gremlin_tsundere_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    if (m.move_history[0] != kProtect) {
        // takeTurn case BASH (:90-98): DamageAction, then a QUEUED SetMoveAction
        // re-asserting Bash. Bash never switches back to Protect.
        queue_monster_move_effects(s, mi, sts::registry::kGremlinTsundere, kBash);
        queue_set_move(s, mi, kBash, MonsterIntent::ATTACK);
        return;
    }

    // takeTurn case PROTECT (:72-88).
    //
    // (a) GainBlockRandomMonsterAction(this, blockAmt) (:73). The block amount is
    //     the registry's tiered PROTECT step; the target is every OTHER monster
    //     that is neither escaping nor dying, picked with one aiRng draw, and the
    //     SOURCE ITSELF when that list is empty (GainBlockRandomMonsterAction.
    //     java:30-39). addBlock is called directly, so no modifyBlock pass runs
    //     -> kBlockNoPowers.
    const sts::registry::MonsterMove* protect =
        sts::registry::kGremlinTsundere.move(kProtect);
    assert(protect != nullptr && protect->effect_count == 1);
    const int32_t block_amount = protect->effects[0].amount.at(kMonsterAscension);

    uint8_t valid[kMonsterCap] = {};
    uint8_t valid_count = 0;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (i != mi && record_alive(s, i)) {
            valid[valid_count++] = i;
        }
    }
    uint8_t target = mi;
    if (valid_count > 0) {
        const int32_t pick = random(s.ai_rng, valid_count - 1);
        target = valid[pick];
    }
    ActionQueueItem blk{};
    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
    blk.src = mi;
    blk.tgt = target;
    blk.amount = block_amount;
    blk.flags = kBlockNoPowers;
    add_to_bottom(s, blk);

    // (b) aliveCount over the whole group, counting ITSELF (:74-78), then a
    //     SYNCHRONOUS setMove: keep protecting while somebody else is up
    //     (aliveCount > 1), otherwise start bashing (:83-88).
    uint8_t alive_count = 0;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (record_alive(s, i)) {
            ++alive_count;
        }
    }
    if (alive_count > 1) {
        set_monster_move(m, kProtect, MonsterIntent::DEFEND);
    } else {
        set_monster_move(m, kBash, MonsterIntent::ATTACK);
    }
}

// --- Gremlin Wizard ----------------------------------------------------------

void gremlin_wizard_init(CombatState& s, uint8_t mi) noexcept {
    init_common(s, mi, MonsterId::GREMLIN_WIZARD, sts::registry::kGremlinWizard);
    MonsterState& m = s.monsters[mi];
    m.pad0 = kChargeStart;  // currentCharge = 1 (GremlinWizard.java:43)
    // GremlinWizard.getMove (:144): unconditional Charging.
    init_roll_discarded(s, m, kCharge, MonsterIntent::UNKNOWN);
}

void gremlin_wizard_take_turn(CombatState& s, uint8_t mi) noexcept {
    MonsterState& m = s.monsters[mi];
    if (m.move_history[0] == kCharge) {
        // takeTurn case CHARGE (:69-83): ++currentCharge SYNCHRONOUSLY, then
        // presentation only (TextAboveCreatureAction / TalkAction / SFX). At the
        // limit the Ultimate Blast telegraph is a QUEUED SetMoveAction (:79);
        // below it, a direct setMove keeps charging (:82).
        m.pad0 = static_cast<uint8_t>(m.pad0 + 1);
        if (m.pad0 == kChargeLimit) {
            queue_set_move(s, mi, kDopeMagic, MonsterIntent::ATTACK);
        } else {
            set_monster_move(m, kCharge, MonsterIntent::UNKNOWN);
        }
        return;
    }
    // takeTurn case DOPE_MAGIC (:85-98): currentCharge = 0 SYNCHRONOUSLY, then
    // DamageAction, then a direct setMove -- Ultimate Blast again from A17
    // (:92-94), otherwise back to charging (:96). At the fixed A20 difficulty the
    // wizard therefore blasts every turn once it has cast once.
    m.pad0 = 0;
    queue_monster_move_effects(s, mi, sts::registry::kGremlinWizard, kDopeMagic);
    if (kMonsterAscension >= 17) {
        set_monster_move(m, kDopeMagic, MonsterIntent::ATTACK);
    } else {
        set_monster_move(m, kCharge, MonsterIntent::UNKNOWN);
    }
}

}  // namespace sts::engine
