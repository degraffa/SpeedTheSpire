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

#include "sts/engine/relic_hooks.hpp"

#include <cstdint>

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

// Heal the player by `n`, clamped to max HP (HealAction semantics). No HEAL opcode
// exists (and none is added for B3.24); a pure heal has no queue-ordering interplay
// with other S1 relic effects, so it is applied directly at dispatch time.
void heal_player(CombatState& s, int32_t n) noexcept {
    int32_t hp = static_cast<int32_t>(s.player_hp) + n;
    if (hp > s.player_max_hp) {
        hp = s.player_max_hp;
    }
    s.player_hp = static_cast<int16_t>(hp);
}

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

void dispatch_native_relic_hook(CombatState& s, RelicHook hook, RelicId relic_id,
                                RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept {
    switch (relic_id) {
        case RelicId::BURNING_BLOOD:
            // BurningBlood.onVictory: heal 6 at combat end (clamped to max HP).
            if (hook == RelicHook::ON_VICTORY) {
                heal_player(s, 6);
            }
            return;

        case RelicId::BLOOD_VIAL:
            // BloodVial.atBattleStart: heal 2 (clamped).
            if (hook == RelicHook::AT_BATTLE_START) {
                heal_player(s, 2);
            }
            return;

        case RelicId::CENTENNIAL_PUZZLE:
            // CentennialPuzzle.wasHPLost: the FIRST HP loss in a combat draws 3.
            // slot.counter is the once-per-combat flag (0 = not yet fired).
            if (hook == RelicHook::WAS_HP_LOST && slot.counter == 0) {
                slot.counter = 1;
                ActionQueueItem draw{};
                draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
                draw.src = kActorPlayer;
                draw.tgt = kActorPlayer;
                draw.amount = 3;
                add_to_top(s, draw);  // addToTop (CentennialPuzzle.java:44)
            }
            return;

        case RelicId::ORICHALCUM:
            // Orichalcum.onPlayerEndTurn: if the player has 0 block, gain 6.
            if (hook == RelicHook::ON_PLAYER_END_TURN && s.player_block == 0) {
                ActionQueueItem blk{};
                blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
                blk.src = kActorPlayer;
                blk.tgt = kActorPlayer;
                blk.amount = 6;
                blk.flags = kBlockNoPowers;  // direct GainBlockAction -- no Dexterity
                add_to_top(s, blk);  // addToTop (Orichalcum.java:38)
            }
            return;

        case RelicId::NUNCHAKU:
            // Nunchaku.onUseCard: every 10th ATTACK played grants 1 energy. The
            // counter persists in the RelicSlot (stage-a §4.3's {relic_id, counter}).
            if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
                ++slot.counter;
                if (slot.counter % 10 == 0) {
                    slot.counter = 0;
                    ActionQueueItem e{};
                    e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
                    e.src = kActorPlayer;
                    e.tgt = kActorPlayer;
                    e.amount = 1;
                    add_to_bottom(s, e);  // addToBot (Nunchaku.java:48)
                }
            }
            return;

        case RelicId::PEN_NIB:
            // PenNib.onUseCard: counts ATTACKs; the 10th is empowered (double
            // damage) then the counter resets. The double-damage PenNib power is
            // DEFERRED (not yet in powers.yaml, B3.4); the COUNTER is live here so
            // the accounting is correct when the power lands. counter persists in
            // the RelicSlot (stage-a §4.3).
            if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
                ++slot.counter;
                if (slot.counter >= 10) {
                    slot.counter = 0;  // PenNib.java:44-47 (empowerment: DEFERRED)
                }
            }
            return;

        case RelicId::HAPPY_FLOWER:
            // HappyFlower.atTurnStart: every 3rd turn-start grants 1 energy. counter
            // persists in the RelicSlot. (The first-turn +2 quirk -- counter starts
            // at AbstractRelic's -1 -- is DEFERRED; the 3-turn cadence is live.)
            if (hook == RelicHook::AT_TURN_START) {
                ++slot.counter;
                if (slot.counter >= 3) {
                    slot.counter = 0;
                    ActionQueueItem e{};
                    e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
                    e.src = kActorPlayer;
                    e.tgt = kActorPlayer;
                    e.amount = 1;
                    add_to_bottom(s, e);  // addToBot (HappyFlower.java:60)
                }
            }
            return;

        case RelicId::LANTERN:
            // Lantern.atTurnStart: +1 energy on the FIRST turn only. slot.counter is
            // the fired-flag (0 = not yet).
            if (hook == RelicHook::AT_TURN_START && slot.counter == 0) {
                slot.counter = 1;
                ActionQueueItem e{};
                e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
                e.src = kActorPlayer;
                e.tgt = kActorPlayer;
                e.amount = 1;
                add_to_top(s, e);  // addToTop (Lantern.java:59)
            }
            return;

        case RelicId::RED_SKULL:
            // RedSkull.onBloodied (routed through wasHPLost): when the HP loss drops
            // the player to <=50% max HP and Red Skull is not already active, gain 3
            // Strength. slot.counter is the isActive flag (0 = inactive). The
            // onNotBloodied -3 on healing back over 50% is DEFERRED (needs a
            // heal-cross hook). Strength IS registered (id 1).
            if (hook == RelicHook::WAS_HP_LOST && slot.counter == 0 &&
                static_cast<int32_t>(s.player_hp) * 2 <= s.player_max_hp) {
                slot.counter = 1;
                ActionQueueItem gain{};
                gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
                gain.src = kActorPlayer;
                gain.tgt = kActorPlayer;
                gain.amount = 3;
                gain.flags = make_apply_power_flags(PowerId::STRENGTH);
                add_to_top(s, gain);  // addToTop (RedSkull.java:54)
            }
            return;

        case RelicId::BLUE_CANDLE:
            // BlueCandle.onUseCard (BlueCandle.java:39-46): a played CURSE loses
            // the player 1 HP (addToBot LoseHPAction) and exhausts (card.exhaust /
            // action.exhaustCard -- the instance EXHAUST flag, read by
            // move_card_hand_to_pile AFTER the fan-out, the Corruption mechanism).
            if (hook == RelicHook::ON_USE_CARD) {
                const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
                if (cd == nullptr || cd->type != CardType::CURSE) {
                    return;
                }
                if (ctx.card_pool_index < kCardPoolCap) {
                    s.card_pool[ctx.card_pool_index].flags |=
                        card_flag_bit(CardFlag::EXHAUST);
                }
                ActionQueueItem hp{};
                hp.opcode = static_cast<uint16_t>(Opcode::LOSE_HP);
                hp.src = kActorPlayer;
                hp.tgt = kActorPlayer;
                hp.amount = 1;
                add_to_bottom(s, hp);  // addToBot (BlueCandle.java:42)
            }
            return;

        case RelicId::GREMLIN_HORN:
            // GremlinHorn.onMonsterDeath (GremlinHorn.java:50-57): +1 energy and
            // draw 1 -- but NOT for the last monster (!areMonstersBasicallyDead():
            // some OTHER monster must still be alive).
            if (hook == RelicHook::ON_MONSTER_DEATH) {
                bool other_alive = false;
                for (uint8_t m = 0; m < s.monster_count; ++m) {
                    if (m != ctx.dead_monster && s.monsters[m].hp > 0) {
                        other_alive = true;
                        break;
                    }
                }
                if (!other_alive) {
                    return;
                }
                ActionQueueItem e{};
                e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
                e.src = kActorPlayer;
                e.tgt = kActorPlayer;
                e.amount = 1;
                add_to_bottom(s, e);   // addToBot GainEnergyAction(1) (:54)
                ActionQueueItem d{};
                d.opcode = static_cast<uint16_t>(Opcode::DRAW);
                d.src = kActorPlayer;
                d.tgt = kActorPlayer;
                d.amount = 1;
                add_to_bottom(s, d);   // addToBot DrawCardAction(player, 1) (:55)
            }
            return;

        case RelicId::HORN_CLEAT:
            // HornCleat: atBattleStart arms (counter=0); atTurnStart increments
            // while armed and fires ONCE at turn 2 (14 block, then counter=-1 ==
            // the grayscale latch -- no further increments); onVictory clears to
            // -1 (the next battle start re-arms). HornCleat.java:31-53.
            if (hook == RelicHook::AT_BATTLE_START) {
                slot.counter = 0;
            } else if (hook == RelicHook::AT_TURN_START) {
                if (slot.counter >= 0) {  // !grayscale (fired == -1 latch)
                    ++slot.counter;
                    if (slot.counter == 2) {
                        ActionQueueItem blk{};
                        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
                        blk.src = kActorPlayer;
                        blk.tgt = kActorPlayer;
                        blk.amount = 14;
                        blk.flags = kBlockNoPowers;  // direct GainBlockAction
                        add_to_bottom(s, blk);  // addToBot (HornCleat.java:43)
                        slot.counter = -1;
                    }
                }
            } else if (hook == RelicHook::ON_VICTORY) {
                slot.counter = -1;  // HornCleat.java:50-53
            }
            return;

        case RelicId::INK_BOTTLE:
            // InkBottle.onUseCard (InkBottle.java:33-45): every card played
            // counts; the 10th draws 1 and resets. No victory reset -- the counter
            // persists across combats in the RelicSlot (stage-a §4.3).
            if (hook == RelicHook::ON_USE_CARD) {
                ++slot.counter;
                if (slot.counter == 10) {
                    slot.counter = 0;
                    ActionQueueItem d{};
                    d.opcode = static_cast<uint16_t>(Opcode::DRAW);
                    d.src = kActorPlayer;
                    d.tgt = kActorPlayer;
                    d.amount = 1;
                    add_to_bottom(s, d);  // addToBot DrawCardAction(1) (:40)
                }
            }
            return;

        case RelicId::KUNAI:
            // Kunai (Kunai.java:35-55): per-turn attack counter; every 3rd ATTACK
            // grants 1 Dexterity. atTurnStart resets to 0; onVictory to -1.
            if (hook == RelicHook::AT_TURN_START) {
                slot.counter = 0;
            } else if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
                ++slot.counter;
                if (slot.counter % 3 == 0) {
                    slot.counter = 0;
                    ActionQueueItem gain{};
                    gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
                    gain.src = kActorPlayer;
                    gain.tgt = kActorPlayer;
                    gain.amount = 1;
                    gain.flags = make_apply_power_flags(PowerId::DEXTERITY);
                    add_to_bottom(s, gain);  // addToBot (Kunai.java:47)
                }
            } else if (hook == RelicHook::ON_VICTORY) {
                slot.counter = -1;
            }
            return;

        case RelicId::LETTER_OPENER:
            // LetterOpener (LetterOpener.java:37-57): per-turn SKILL counter;
            // every 3rd SKILL deals 5 THORNS damage to ALL enemies (flat -- THORNS
            // skips the NORMAL-only power pipeline).
            if (hook == RelicHook::AT_TURN_START) {
                slot.counter = 0;
            } else if (hook == RelicHook::ON_USE_CARD) {
                const CardDef* cd = card_def(static_cast<CardId>(ctx.card_id));
                if (cd == nullptr || cd->type != CardType::SKILL) {
                    return;
                }
                ++slot.counter;
                if (slot.counter % 3 == 0) {
                    slot.counter = 0;
                    ActionQueueItem dmg{};
                    dmg.opcode = static_cast<uint16_t>(Opcode::DAMAGE);
                    dmg.src = kActorPlayer;
                    dmg.tgt = kActorAllEnemies;
                    dmg.amount = 5;
                    dmg.flags = make_damage_flags(DamageType::THORNS);
                    add_to_bottom(s, dmg);  // addToBot DamageAllEnemies (:49)
                }
            } else if (hook == RelicHook::ON_VICTORY) {
                slot.counter = -1;
            }
            return;

        case RelicId::ORNAMENTAL_FAN:
            // OrnamentalFan (OrnamentalFan.java:34-54): per-turn attack counter;
            // every 3rd ATTACK gains 4 block (direct GainBlockAction, no Dexterity).
            if (hook == RelicHook::AT_TURN_START) {
                slot.counter = 0;
            } else if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
                ++slot.counter;
                if (slot.counter % 3 == 0) {
                    slot.counter = 0;
                    ActionQueueItem blk{};
                    blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
                    blk.src = kActorPlayer;
                    blk.tgt = kActorPlayer;
                    blk.amount = 4;
                    blk.flags = kBlockNoPowers;
                    add_to_bottom(s, blk);  // addToBot (OrnamentalFan.java:46)
                }
            } else if (hook == RelicHook::ON_VICTORY) {
                slot.counter = -1;
            }
            return;

        case RelicId::SHURIKEN:
            // Shuriken (Shuriken.java:35-55): per-turn attack counter; every 3rd
            // ATTACK grants 1 Strength.
            if (hook == RelicHook::AT_TURN_START) {
                slot.counter = 0;
            } else if (hook == RelicHook::ON_USE_CARD && ctx.card_is_attack) {
                ++slot.counter;
                if (slot.counter % 3 == 0) {
                    slot.counter = 0;
                    ActionQueueItem gain{};
                    gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
                    gain.src = kActorPlayer;
                    gain.tgt = kActorPlayer;
                    gain.amount = 1;
                    gain.flags = make_apply_power_flags(PowerId::STRENGTH);
                    add_to_bottom(s, gain);  // addToBot (Shuriken.java:47)
                }
            } else if (hook == RelicHook::ON_VICTORY) {
                slot.counter = -1;
            }
            return;

        case RelicId::SUNDIAL:
            // Sundial.onShuffle (Sundial.java:45-53): every 3rd reshuffle grants 2
            // energy. onEquip counter=0; NO victory reset (persists across combats).
            if (hook == RelicHook::ON_SHUFFLE) {
                ++slot.counter;
                if (slot.counter == 3) {
                    slot.counter = 0;
                    ActionQueueItem e{};
                    e.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
                    e.src = kActorPlayer;
                    e.tgt = kActorPlayer;
                    e.amount = 2;
                    add_to_bottom(s, e);  // addToBot GainEnergyAction(2) (:51)
                }
            }
            return;

        case RelicId::SELF_FORMING_CLAY:
            // SelfFormingClay.wasHPLost (SelfFormingClay.java:32-37): any positive
            // in-combat HP loss applies Next Turn Block 3 (addToTop; stacks).
            // dispatch_relics_was_hp_lost already gates amount > 0.
            if (hook == RelicHook::WAS_HP_LOST) {
                ActionQueueItem gain{};
                gain.opcode = static_cast<uint16_t>(Opcode::APPLY_POWER);
                gain.src = kActorPlayer;
                gain.tgt = kActorPlayer;
                gain.amount = 3;
                gain.flags = make_apply_power_flags(PowerId::NEXT_TURN_BLOCK);
                add_to_top(s, gain);  // addToTop (SelfFormingClay.java:35)
            }
            return;

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
            return;  // an unrecognized / deferred native relic is a safe no-op
    }
}

}  // namespace sts::engine
