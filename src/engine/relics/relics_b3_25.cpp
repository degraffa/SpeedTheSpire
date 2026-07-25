// B3.25 uncommon relics -- native hook bodies (moved verbatim out of
// relic_hooks.cpp's escape-hatch switch; see relic_native.hpp for the split's
// rationale). Parameters a body does not read are left unnamed to keep -Wextra
// quiet; the signature is the uniform RelicNativeFn.

#include "relics_b3_25.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (curse/skill checks)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"

namespace sts::engine {

void relic_native_blue_candle(CombatState& s, RelicHook hook,
                              RelicSlot& /*slot*/,
                              const RelicHookContext& ctx) noexcept {
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
}

void relic_native_gremlin_horn(CombatState& s, RelicHook hook,
                               RelicSlot& /*slot*/,
                               const RelicHookContext& ctx) noexcept {
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
}

void relic_native_horn_cleat(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& /*ctx*/) noexcept {
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
}

void relic_native_ink_bottle(CombatState& s, RelicHook hook, RelicSlot& slot,
                             const RelicHookContext& /*ctx*/) noexcept {
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
}

void relic_native_kunai(CombatState& s, RelicHook hook, RelicSlot& slot,
                        const RelicHookContext& ctx) noexcept {
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
}

void relic_native_letter_opener(CombatState& s, RelicHook hook, RelicSlot& slot,
                                const RelicHookContext& ctx) noexcept {
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
}

void relic_native_ornamental_fan(CombatState& s, RelicHook hook, RelicSlot& slot,
                                 const RelicHookContext& ctx) noexcept {
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
}

void relic_native_shuriken(CombatState& s, RelicHook hook, RelicSlot& slot,
                           const RelicHookContext& ctx) noexcept {
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
}

void relic_native_sundial(CombatState& s, RelicHook hook, RelicSlot& slot,
                          const RelicHookContext& /*ctx*/) noexcept {
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
}

void relic_native_self_forming_clay(CombatState& s, RelicHook hook,
                                    RelicSlot& /*slot*/,
                                    const RelicHookContext& /*ctx*/) noexcept {
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
}

// --- DEFERRED combat bodies --------------------------------------------------
//
// These two B3.25 uncommons are registered `native: true` with their hook
// inventory but have NO combat body yet: each depends on registry content that
// does not exist at this point in the build order. They are DELIBERATELY EMPTY
// definitions, not omissions -- the dispatch table generated from
// registry/relics.yaml (STS_REGISTRY_NATIVE_RELICS, expanded in relic_hooks.cpp)
// odr-uses a handler for every `native: true` row, so a relic whose body nobody
// wrote is an UNDEFINED REFERENCE at link time rather than a silent no-op.
// Behaviour is unchanged from the old `default: return nullptr;` grouping: a
// no-op call instead of a skipped null call.

// MummifiedHand.onUseCard (MummifiedHand.java:38-72) -- when a POWER card is
// played, filter the hand for cost > 0 && costForTurn > 0 && !freeToPlayOnce
// minus cardQueue members, pick one via cardRandomRng.random(0, n-1) (no draw
// when the filtered list is empty) and setCostForTurn(0). DEFERRED: the registry
// has no POWER CardType yet (power cards land in B3.7), so the trigger condition
// is unrepresentable.
void relic_native_mummified_hand(CombatState& /*s*/, RelicHook /*hook*/,
                                 RelicSlot& /*slot*/,
                                 const RelicHookContext& /*ctx*/) noexcept {}

// Pantograph.atBattleStart (Pantograph.java:32-40) -- if any monster.type ==
// EnemyType.BOSS, addToTop HealAction(player, 25). DEFERRED: monsters.yaml
// carries no EnemyType/BOSS metadata and no boss rows exist yet
// (Guardian/Hexaghost/Slime Boss are B3.15-B3.17).
void relic_native_pantograph(CombatState& /*s*/, RelicHook /*hook*/,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& /*ctx*/) noexcept {}

}  // namespace sts::engine
