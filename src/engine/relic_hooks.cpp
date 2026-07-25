// Relic-hook framework -- acquisition-order dispatch + the native escape hatch.
// See relic_hooks.hpp for the full hook inventory, the acquisition-order rule
// (stage-a trap 8), the relic-vs-power interleave at each call site, and the
// combat-storage seam (player_relics() reads CombatState's relic mirror).
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

// The per-tier relic headers (relics_common.hpp, ...) are deliberately NOT
// included: the generated STS_REGISTRY_NATIVE_RELICS expansion below declares
// every native body itself, so this file has no per-tier dependency and a new
// relic tier never edits it.
#include "relics/relic_native.hpp"      // RelicNativeSig/Fn, heal_player
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

// --- player_relics: the combat relic view -----------------------------------

RelicView player_relics(CombatState& s) noexcept {
    // CombatState carries the relic mirror (s.relics / s.relic_count), so the
    // wired dispatch sites (power_hooks.cpp / action_queue.cpp) read the live
    // acquisition-ordered list. It is empty (relic_count == 0) until a run
    // populates it (run_advance.cpp's enter_combat), so states with no relics
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

void dispatch_relics_at_pre_battle(CombatState& s, RelicSlot* relics,
                                   uint8_t count) noexcept {
    // applyPreCombatLogic (AbstractPlayer.java:1885-1890), reached from the last
    // line of preBattlePrep (:1607) -- i.e. before AbstractRoom's turn-1 block
    // runs at all, so anything queued here resolves ahead of the opening draw.
    // Snecko Eye is the only S1 row that binds it.
    dispatch_relic_hook(s, relics, count, RelicHook::AT_PRE_BATTLE,
                        RelicHookContext{});
}

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

void dispatch_relics_at_turn_start_post_draw(CombatState& s, RelicSlot* relics,
                                             uint8_t count) noexcept {
    // applyStartOfTurnPostDrawRelics (GameActionManager.java:361-362): fired
    // AFTER the start-of-turn DrawCardAction has been queued, so a relic that
    // queues here lands behind the draw. Pocketwatch / Gambling Chip.
    dispatch_relic_hook(s, relics, count, RelicHook::AT_TURN_START_POST_DRAW,
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

void dispatch_relics_on_block_broken(CombatState& s, RelicSlot* relics,
                                     uint8_t count, uint8_t monster) noexcept {
    // AbstractCreature.brokeBlock (AbstractCreature.java:159-167): every player
    // relic's onBlockBroken fires when the creature whose block just hit zero is
    // an AbstractMonster. decrementBlock (:169-183) reaches brokeBlock exactly
    // when the victim had positive block and the incoming damage was >= it.
    RelicHookContext ctx{};
    ctx.block_broken_monster = monster;
    dispatch_relic_hook(s, relics, count, RelicHook::ON_BLOCK_BROKEN, ctx);
}

void heal_player_with_relics(CombatState& s, int32_t amount) noexcept {
    // AbstractPlayer.heal runs every owned relic's onPlayerHeal over the amount
    // before it lands. Magic Flower is the only S1 override
    // (MagicFlower.onPlayerHeal, MagicFlower.java:728-735): inside a COMBAT-phase
    // room the amount becomes MathUtils.round(amount * 1.5f). Everything reached
    // through this function is by construction in combat (it takes a
    // CombatState), so the room-phase test is satisfied by the call site.
    //
    // MathUtils.round is mirrored bit-for-bit by mathutils_round (interp.hpp),
    // including the float multiply happening before the float->double promotion.
    int32_t healed = amount;
    if (healed > 0 && player_has_relic(s, RelicId::MAGIC_FLOWER)) {
        healed = mathutils_round(static_cast<float>(healed) * 1.5f);
    }
    heal_player(s, healed);
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
        heal_player_with_relics(s, 12);
    }
}

// --- Native escape hatch -----------------------------------------------------
//
// The dispatch table -- RelicId -> the native body's function pointer -- is
// GENERATED from registry/relics.yaml. Every row marked `native: true` becomes
// one X(<RelicId>, <handler>) entry of STS_REGISTRY_NATIVE_RELICS (generated
// relic_table.hpp, reached via sts/engine/relics.hpp), the handler name derived
// from the row name by the frozen convention: RelicId::BLUE_CANDLE ->
// relic_native_blue_candle. We expand that one list twice: once for the extern
// declarations, once for the switch. Structure still mirrors
// monster_dispatch.cpp's monster_init_fn (a plain switch, data-oriented, no
// virtual dispatch) -- but a relic batch no longer edits this file at all: it
// adds registry rows, a translation unit under src/engine/relics/, and a
// CMakeLists line. That is what makes the remaining tiers (rares+shop,
// boss+specials) genuinely parallel-safe: they no longer share this table.
//
// Why the extern declarations are generated here rather than #include'd from the
// per-batch headers: each handler is odr-used by the switch below, so a relic
// marked `native: true` whose body NOBODY WROTE is an undefined reference at
// link time instead of a silently missing case. (The old hand-written switch
// answered `default: return nullptr` for a forgotten relic, so it quietly did
// nothing -- exactly the failure this shape removes.) The declarations use
// RelicNativeSig, so a body whose signature drifts also fails to link.
//
// Corollary: "native, but the combat body is DEFERRED" cannot be expressed by
// omission any more -- a deferred relic must define an EXPLICIT empty body in
// the batch TU that registered it, with the deferral reason written at that
// definition. That is a decision on the record rather than a hole, and it is why
// no new registry/relics.yaml field is needed to distinguish "native, handler
// expected" from "native, deliberately no handler yet": the distinction lives
// where the handler would live. The deferred set today (unchanged behaviour --
// each is a no-op call instead of a skipped null call) is:
//   AKABEKO / ART_OF_WAR / ANCIENT_TEA_SET / BOOT / PRESERVED_INSECT /
//   TOY_ORNITHOPTER   -> relics/relics_common.cpp
//   PANTOGRAPH        -> relics/relics_uncommon.cpp
//   DEAD_BRANCH / GAMBLING_CHIP            -> relics/relics_rare.cpp
//   SLING_OF_COURAGE / ORANGE_PELLETS      -> relics/relics_shop.cpp
// (BRONZE_SCALES / ODDLY_SMOOTH_STONE were un-deferred by the potion-support-
// powers follow-up: Thorns/Dexterity are registered, so both are DATA
// at_battle_start APPLY_POWER relics and never route here.)

#define STS_RELIC_NATIVE_DECL(ID, FN) extern RelicNativeSig FN;
STS_REGISTRY_NATIVE_RELICS(STS_RELIC_NATIVE_DECL)
#undef STS_RELIC_NATIVE_DECL

RelicNativeFn relic_native_fn(RelicId id) noexcept {
    switch (id) {
#define STS_RELIC_NATIVE_CASE(ID, FN) \
    case RelicId::ID:                 \
        return &FN;
        STS_REGISTRY_NATIVE_RELICS(STS_RELIC_NATIVE_CASE)
#undef STS_RELIC_NATIVE_CASE
        default:
            return nullptr;  // a non-native / unrecognized relic has no body
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
