// Relic-hook framework -- acquisition-order dispatch + the native escape hatch.
// See relic_hooks.hpp for the full hook inventory, the acquisition-order rule
// (stage-a trap 8), the relic-vs-power interleave at each call site, and the
// combat-storage seam (player_relics() reads CombatState's relic mirror, live as
// of B4.3).
//
// Provenance (each relic body read in full in the decompiled Java before coding):
// registry/relics.yaml carries the per-relic citation. Hook sites:
// AbstractRelic.atBattleStart/atTurnStart/onPlayerEndTurn/onUseCard/onExhaust/
// wasHPLost/onVictory (AbstractRelic.java:492-620). Design doc §5.3.
//
// The native bodies themselves live in per-batch translation units under
// src/engine/relics/ (grouped by the relic tier/batch that introduced them);
// this file keeps the framework plus the relic_native_fn dispatch table,
// mirroring monster_dispatch.cpp's per-monster TUs + function-pointer switch.

#include "sts/engine/relic_hooks.hpp"

#include <cstdint>

#include "relics/relic_native.hpp"      // RelicNativeFn, heal_player
#include "relics/relics_b3_24.hpp"      // B3.24 starter + common relics
#include "relics/relics_b3_25.hpp"      // B3.25 uncommon relics
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (attack check)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/relics.hpp"        // relic_def, RelicDef, RelicHookBinding
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

namespace {

// Queue a data-bound relic hook step, resolved relative to the player (relics are
// player-owned). Unlike power hooks, a relic step's amount is ALWAYS a literal
// (relics carry no stack amount). SELF -> player; ALL_ENEMY/RANDOM_ENEMY -> the
// player's enemies (fanned out at execute time). Queued addToBot.
void queue_relic_step(CombatState& s, const CardEffectStep& step) noexcept {
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(step.op);
    item.src = kActorPlayer;
    switch (step.target) {
        case StepTarget::SELF:
            item.tgt = kActorPlayer;
            break;
        case StepTarget::ALL_ENEMY:
            item.tgt = kActorAllEnemies;
            break;
        case StepTarget::RANDOM_ENEMY:
            item.tgt = kActorRandomEnemy;
            break;
        case StepTarget::CARD_TARGET:
        default:
            item.tgt = kActorPlayer;
            break;
    }
    item.amount = step.amount;
    item.flags = step.extra;  // APPLY_POWER: PowerId; else 0
    if (step.op == static_cast<decltype(step.op)>(Opcode::BLOCK)) {  // registry mirror
        // A relic's block is a direct GainBlockAction (Anchor), not card applyPowers,
        // so it does NOT get Dexterity -- flag op_block to skip the modifyBlock pass.
        item.flags |= kBlockNoPowers;
    }
    add_to_bottom(s, item);
}

// (heal_player now lives in relics/relic_native.hpp -- the native bodies in
// src/engine/relics/ need it too, so it is a shared inline helper rather than a
// file-local static.)

}  // namespace

// --- player_relics: the combat relic view (live as of B4.3) ------------------

RelicView player_relics(CombatState& s) noexcept {
    // B4.3 gave CombatState its relic mirror (s.relics / s.relic_count), so the
    // wired dispatch sites (power_hooks.cpp / action_queue.cpp) now read the live
    // acquisition-ordered list. It is empty (relic_count == 0) until a run
    // populates it -- the run-level fold-back is B4.4 -- so states with no relics
    // (the 20 combat fixtures) still dispatch nothing.
    return RelicView{s.relics, s.relic_count};
}

// --- Generic dispatch (acquisition order) ------------------------------------

void dispatch_relic_hook(CombatState& s, RelicSlot* relics, uint8_t count,
                         RelicHook hook, const RelicHookContext& ctx) noexcept {
    if (relics == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < count; ++i) {  // index order == acquisition order
        const RelicId rid = static_cast<RelicId>(relics[i].relic_id);
        if (rid == RelicId::NONE) {
            continue;
        }
        const RelicDef* def = relic_def(rid);
        if (def == nullptr) {
            continue;
        }
        // engine::RelicHook and registry::RelicHook are pinned byte-equal (relics.hpp).
        const RelicHookBinding* b =
            def->hook_binding(static_cast<sts::registry::RelicHook>(hook));
        if (b == nullptr) {
            continue;  // this relic does not respond to this hook
        }
        if (def->native) {
            dispatch_native_relic_hook(s, hook, rid, relics[i], ctx);
        } else {
            for (uint8_t k = 0; k < b->step_count; ++k) {
                queue_relic_step(s, b->steps[k]);
            }
        }
    }
}

// --- Per-hook entry points ---------------------------------------------------

void dispatch_relics_at_battle_start(CombatState& s, RelicSlot* relics,
                                     uint8_t count) noexcept {
    dispatch_relic_hook(s, relics, count, RelicHook::AT_BATTLE_START,
                        RelicHookContext{});
}

void dispatch_relics_at_turn_start(CombatState& s, RelicSlot* relics,
                                   uint8_t count) noexcept {
    dispatch_relic_hook(s, relics, count, RelicHook::AT_TURN_START,
                        RelicHookContext{});
}

void dispatch_relics_on_player_end_turn(CombatState& s, RelicSlot* relics,
                                        uint8_t count) noexcept {
    dispatch_relic_hook(s, relics, count, RelicHook::ON_PLAYER_END_TURN,
                        RelicHookContext{});
}

void dispatch_relics_on_use_card(CombatState& s, RelicSlot* relics, uint8_t count,
                                 uint16_t card_id, uint8_t pool_index) noexcept {
    RelicHookContext ctx{};
    ctx.card_id = card_id;
    ctx.card_pool_index = pool_index;
    const CardDef* cd = card_def(static_cast<CardId>(card_id));
    ctx.card_is_attack = (cd != nullptr && cd->type == CardType::ATTACK) ? 1 : 0;
    dispatch_relic_hook(s, relics, count, RelicHook::ON_USE_CARD, ctx);
}

void dispatch_relics_on_play_card(CombatState& s, RelicSlot* relics, uint8_t count,
                                  uint16_t card_id) noexcept {
    RelicHookContext ctx{};
    ctx.card_id = card_id;
    dispatch_relic_hook(s, relics, count, RelicHook::ON_PLAY_CARD, ctx);
}

void dispatch_relics_on_exhaust(CombatState& s, RelicSlot* relics, uint8_t count,
                                uint16_t card_id) noexcept {
    RelicHookContext ctx{};
    ctx.card_id = card_id;
    dispatch_relic_hook(s, relics, count, RelicHook::ON_EXHAUST, ctx);
}

void dispatch_relics_on_gained_block(CombatState& s, RelicSlot* relics,
                                     uint8_t count, int32_t amount) noexcept {
    RelicHookContext ctx{};
    ctx.amount = amount;
    dispatch_relic_hook(s, relics, count, RelicHook::ON_GAINED_BLOCK, ctx);
}

void dispatch_relics_was_hp_lost(CombatState& s, RelicSlot* relics, uint8_t count,
                                 int32_t amount) noexcept {
    if (amount <= 0) {
        return;  // wasHPLost only fires on a positive HP loss
    }
    RelicHookContext ctx{};
    ctx.amount = amount;
    dispatch_relic_hook(s, relics, count, RelicHook::WAS_HP_LOST, ctx);
}

void dispatch_relics_on_victory(CombatState& s, RelicSlot* relics,
                                uint8_t count) noexcept {
    dispatch_relic_hook(s, relics, count, RelicHook::ON_VICTORY, RelicHookContext{});
}

void dispatch_relics_on_monster_death(CombatState& s, RelicSlot* relics,
                                      uint8_t count,
                                      uint8_t dead_monster) noexcept {
    // AbstractMonster.die (AbstractMonster.java:933-937): every player relic's
    // onMonsterDeath fires when a monster dies. Wired at the op_damage /
    // op_lose_hp death edge (hp crosses to 0); a no-op without a responding relic.
    RelicHookContext ctx{};
    ctx.dead_monster = dead_monster;
    dispatch_relic_hook(s, relics, count, RelicHook::ON_MONSTER_DEATH, ctx);
}

void dispatch_relics_on_shuffle(CombatState& s, RelicSlot* relics,
                                uint8_t count) noexcept {
    // EmptyDeckShuffleAction constructor (EmptyDeckShuffleAction.java:37-39):
    // every player relic's onShuffle fires as the reshuffle action is created,
    // BEFORE the shuffle itself. Wired in piles.cpp shuffle_discard_into_draw.
    dispatch_relic_hook(s, relics, count, RelicHook::ON_SHUFFLE,
                        RelicHookContext{});
}

void apply_meat_on_the_bone_pre_victory(CombatState& s) noexcept {
    // AbstractRoom.endBattle (AbstractRoom.java:418-420): Meat on the Bone's
    // onTrigger fires BEFORE player.onVictory (so before Burning Blood's heal,
    // regardless of acquisition order). MeatOnTheBone.onTrigger
    // (MeatOnTheBone.java:31-39): heal 12 iff currentHealth <= maxHealth/2.0 and
    // currentHealth > 0 (hp*2 <= max is the exact integer equivalent).
    if (!player_has_relic(s, RelicId::MEAT_ON_THE_BONE)) {
        return;
    }
    if (s.player_hp > 0 &&
        static_cast<int32_t>(s.player_hp) * 2 <= s.player_max_hp) {
        heal_player(s, 12);
    }
}

// --- Native escape hatch -----------------------------------------------------
//
// The dispatch table: RelicId -> the native body's function pointer, or nullptr
// for a relic whose combat body is DEFERRED. Structure mirrors
// monster_dispatch.cpp's monster_init_fn (a plain switch, data-oriented, no
// virtual dispatch); each relic batch adds its cases here and a translation unit
// under src/engine/relics/.

RelicNativeFn relic_native_fn(RelicId id) noexcept {
    switch (id) {
        // B3.24 starter + commons (relics/relics_b3_24.cpp)
        case RelicId::BURNING_BLOOD:
            return &relic_native_burning_blood;
        case RelicId::BLOOD_VIAL:
            return &relic_native_blood_vial;
        case RelicId::CENTENNIAL_PUZZLE:
            return &relic_native_centennial_puzzle;
        case RelicId::ORICHALCUM:
            return &relic_native_orichalcum;
        case RelicId::NUNCHAKU:
            return &relic_native_nunchaku;
        case RelicId::PEN_NIB:
            return &relic_native_pen_nib;
        case RelicId::HAPPY_FLOWER:
            return &relic_native_happy_flower;
        case RelicId::LANTERN:
            return &relic_native_lantern;
        case RelicId::RED_SKULL:
            return &relic_native_red_skull;

        // B3.25 uncommons (relics/relics_b3_25.cpp)
        case RelicId::BLUE_CANDLE:
            return &relic_native_blue_candle;
        case RelicId::GREMLIN_HORN:
            return &relic_native_gremlin_horn;
        case RelicId::HORN_CLEAT:
            return &relic_native_horn_cleat;
        case RelicId::INK_BOTTLE:
            return &relic_native_ink_bottle;
        case RelicId::KUNAI:
            return &relic_native_kunai;
        case RelicId::LETTER_OPENER:
            return &relic_native_letter_opener;
        case RelicId::ORNAMENTAL_FAN:
            return &relic_native_ornamental_fan;
        case RelicId::SHURIKEN:
            return &relic_native_shuriken;
        case RelicId::SUNDIAL:
            return &relic_native_sundial;
        case RelicId::SELF_FORMING_CLAY:
            return &relic_native_self_forming_clay;

        // Native relics whose combat body is DEFERRED (a cross-domain dependency
        // not yet available). Each is a documented no-op today; the relic still
        // dispatches (row + hook registered) so the accounting/wiring is in place.
        //   (BRONZE_SCALES / ODDLY_SMOOTH_STONE un-deferred by the potion-support-
        //    powers follow-up: Thorns/Dexterity now registered, so both are DATA
        //    at_battle_start APPLY_POWER relics -- they no longer route here.)
        //   AKABEKO        -- apply Vigor at battle start; Vigor power row is later.
        //   BOOT           -- onAttack damage floor; a DAMAGE-pipeline modifier.
        //   ART_OF_WAR / ANCIENT_TEA_SET -- cross-turn/cross-room energy flags.
        //   PRESERVED_INSECT -- elite HP scaling (needs room context + HP-scale op).
        //   TOY_ORNITHOPTER is dispatched by run_advance's RunState-owned potion
        //   route (B4.4), not by a CombatState-only hook.
        //   MUMMIFIED_HAND (B3.25) -- onUseCard POWER -> random hand card costs 0
        //   this turn (cardRandomRng); DEFERRED: no POWER CardType exists until the
        //   B3.7 power-card batch, so the trigger condition is unrepresentable.
        //   PANTOGRAPH (B3.25) -- atBattleStart heal 25 in a BOSS fight; DEFERRED:
        //   monsters.yaml has no EnemyType/BOSS metadata and no boss rows exist
        //   yet (Guardian/Hexaghost/Slime Boss are B3.15-B3.17).
        case RelicId::AKABEKO:
        case RelicId::BOOT:
        case RelicId::ART_OF_WAR:
        case RelicId::ANCIENT_TEA_SET:
        case RelicId::PRESERVED_INSECT:
        case RelicId::TOY_ORNITHOPTER:
        case RelicId::MUMMIFIED_HAND:
        case RelicId::PANTOGRAPH:
        default:
            return nullptr;  // an unrecognized / deferred native relic is a safe no-op
    }
}

void dispatch_native_relic_hook(CombatState& s, RelicHook hook, RelicId relic_id,
                                RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept {
    const RelicNativeFn fn = relic_native_fn(relic_id);
    if (fn != nullptr) {
        fn(s, hook, slot, ctx);
    }
}

}  // namespace sts::engine
