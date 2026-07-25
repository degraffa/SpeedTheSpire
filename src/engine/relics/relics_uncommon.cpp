// UNCOMMON-tier relics -- native hook bodies (moved verbatim out of
// relic_hooks.cpp's escape-hatch switch; see relic_native.hpp for the split's
// rationale). Parameters a body does not read are left unnamed to keep -Wextra
// quiet; the signature is the uniform RelicNativeFn.

#include "relics_uncommon.hpp"

#include <cstdint>

#include "relic_native.hpp"             // heal_player (HealAction clamp)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / add_to_top / kActor*
#include "sts/engine/cards.hpp"         // card_def, CardType (curse/skill checks)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, make_apply_power_flags
#include "sts/engine/rng_stream.hpp"    // random (Mummified Hand's cardRandomRng draw)
#include "sts/engine/run_state.hpp"     // RelicSlot
#include "sts/engine/types.hpp"
#include "sts/registry/monster_table.hpp"  // monster_def, MonsterDef::is_boss

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

void relic_native_mummified_hand(CombatState& s, RelicHook hook,
                                 RelicSlot& /*slot*/,
                                 const RelicHookContext& ctx) noexcept {
    // MummifiedHand.onUseCard (MummifiedHand.java:38-72). Playing a POWER card
    // (:39) discounts one random OTHER hand card to 0 for the turn.
    //
    // The Java, line by line:
    //   :43-49  groupCopy = every hand card with
    //             cost > 0 && costForTurn > 0 && !freeToPlayOnce
    //   :50-54  minus every card currently sitting in actionManager.cardQueue
    //   :56-64  if groupCopy is EMPTY the method falls through with c == null --
    //           NO cardRandomRng draw happens at all. Getting this wrong
    //           desynchronises the shared cardRandomRng stream, so the empty
    //           case returns before the draw below.
    //   :61     c = groupCopy.get(cardRandomRng.random(0, size - 1))
    //   :67     c.setCostForTurn(0)
    if (hook != RelicHook::ON_USE_CARD) {
        return;
    }
    const CardDef* played = card_def(static_cast<CardId>(ctx.card_id));
    if (played == nullptr || played->type != CardType::POWER) {
        return;  // MummifiedHand.java:39
    }

    CardPoolIndex candidates[kHandCap];
    uint8_t n = 0;
    for (uint8_t i = 0; i < s.hand_count && n < kHandCap; ++i) {
        const CardPoolIndex pi = s.hand[i];

        // THE PLAYED CARD ITSELF. AbstractPlayer.useCard removes it from the
        // hand (:1374) BEFORE the UseCardAction it queued at :1371 ever
        // executes, so the Java's hand.group cannot contain it when this hook
        // fires. The engine collapses UseCardAction into resolve_card_play and
        // dispatches onUseCard BEFORE move_card_hand_to_pile (card_play.cpp
        // step 5), so here the source card IS still in the hand -- exclude it
        // explicitly. Same limbo/cardInUse correction count_strikes_excluding
        // (Perfected Strike) and choice_excluded_index (Headbutt) already make.
        if (pi == ctx.card_pool_index) {
            continue;
        }

        // THE cardQueue EXCLUSION (:50-54). The brief's prior audit called this
        // analogue-less; it is not. CombatState carries a real cardQueue
        // (s.card_queue / card_queue_count, design §5.1) and PLAY_TOP_DRAW
        // (Havoc) genuinely parks a card in the HAND while its play sits queued
        // (interp_cards.cpp op_play_top_draw), which is exactly the state the
        // Java loop guards against. So implement it rather than document it
        // away. It is currently belt-and-braces -- op_play_top_draw also sets
        // that card's cost_now to 0, so the costForTurn > 0 filter below
        // already rejects it -- but it is one loop, and it keeps the engine
        // correct if a future queue-a-hand-card verb (Duplication) lands.
        bool queued = false;
        for (uint8_t q = 0; q < s.card_queue_count; ++q) {
            if (s.card_queue[q].card_index == pi) {
                queued = true;
                break;
            }
        }
        if (queued) {
            continue;
        }

        const CardInstance& inst = s.card_pool[pi];
        const CardDef* cd = card_def(static_cast<CardId>(inst.card_id));
        if (cd == nullptr) {
            continue;
        }
        // c.cost > 0 (:44). gen.py maps the game's sentinel costs onto flags
        // with base_cost 0 -- X-cost (Java cost -1, the XCOST flag) and
        // unplayable statuses/curses (Java cost -2, the UNPLAYABLE flag) both
        // land at base_cost 0 -- so the single `> 0` test rejects exactly what
        // the Java rejects.
        if (card_cost(*cd, inst.upgrade) == 0) {
            continue;
        }
        // c.costForTurn > 0 (:44) -- the per-instance runtime cost.
        if (inst.cost_now == 0) {
            continue;
        }
        // !c.freeToPlayOnce (:44) has NO engine analogue: the flag is set only
        // on a card the game is in the act of auto-playing (GameActionManager
        // :216-218, AbstractPlayer:1366-1368) and is never observable on a card
        // resting in hand in S1 scope. The cost_now > 0 test above already
        // rejects every free-this-turn instance the engine can produce.
        candidates[n++] = pi;
    }

    if (n == 0) {
        return;  // :56-64 -- no candidate, and crucially NO cardRandomRng draw.
    }
    const int32_t pick = random(s.card_random_rng, 0, n - 1);  // :61
    CardInstance& chosen = s.card_pool[candidates[pick]];
    // setCostForTurn(0) (:67 -> AbstractCard.java:2001-2011): costForTurn = 0
    // and, because the new value differs from cost (every candidate has
    // cost > 0), isCostModifiedForTurn = true -- so the discount reverts in the
    // end-of-turn sweep (reset_cost_for_turn, AbstractRoom.endTurn:397-405).
    chosen.cost_now = 0;
    chosen.flags = static_cast<uint16_t>(
        chosen.flags | card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN));
}

void relic_native_pantograph(CombatState& s, RelicHook hook,
                             RelicSlot& /*slot*/,
                             const RelicHookContext& /*ctx*/) noexcept {
    // Pantograph.atBattleStart (Pantograph.java:32-40):
    //
    //     for (AbstractMonster m : AbstractDungeon.getMonsters().monsters) {
    //         if (m.type != AbstractMonster.EnemyType.BOSS) continue;
    //         this.flash();
    //         this.addToTop(new HealAction(player, player, 25, 0.0f));
    //         this.addToTop(new RelicAboveCreatureAction(player, this));
    //         return;
    //     }
    //
    // WHAT IT KEYS ON: the MONSTER's EnemyType, not the room. Pantograph never
    // touches AbstractRoom / MonsterRoomBoss -- it scans the live monster group
    // for a BOSS-typed member. That distinction is observable: a boss-typed
    // monster met outside a boss room still triggers it, and a boss room whose
    // group somehow held no BOSS-typed monster would not. So the metadata this
    // needs is per-MONSTER (AbstractMonster.type, AbstractMonster.java:99),
    // which is why it now lives in registry/monsters.yaml as `enemy_type` and
    // reaches here as MonsterDef::is_boss() -- no hard-coded MonsterId list, and
    // The Guardian / Hexaghost light up the moment their rows land.
    //
    // TIMING: atBattleStart, not onEquip (which Pantograph does not override --
    // the tier/sound-only ctor at :22-24 is its whole equip behaviour) and not
    // atBattleStartPreDraw (the separate AbstractRelic hook at :503; Pantograph
    // overrides :32 atBattleStart). RelicHook::AT_BATTLE_START is the engine's
    // pinned mirror of exactly that hook.
    //
    // ONCE, NOT PER BOSS: the Java `return`s inside the loop after the first
    // BOSS-typed member, so a hypothetical two-boss group heals 25 total, not
    // 50. The break below is that `return`.
    //
    // AMOUNT AND CLAMP: HEAL_AMT is 25 (Pantograph.java:20, and the literal 25
    // passed at :36). The heal goes through HealAction -> AbstractCreature.heal
    // (HealAction.java:31-33 -> AbstractCreature.java:386-417), whose only S1
    // modifiers are the Endless-mode FullBelly blight halving (:387-389, not S1)
    // and the relic/power onPlayerHeal / onHeal fan-out (:393-399, no S1 relic or
    // power binds either); what DOES apply is the clamp to maxHealth (:401-403).
    // heal_player is exactly that clamp -- the same call Blood Vial's
    // atBattleStart body makes (relics_common.cpp), and like it the heal is
    // applied directly rather than queued: the Java addToTop pair is a
    // RelicAboveCreatureAction (pure VFX) plus a duration-0 HealAction, and a
    // pure clamped heal has no queue-ordering interplay with any other S1
    // battle-start effect.
    if (hook != RelicHook::AT_BATTLE_START) {
        return;
    }
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        const sts::registry::MonsterDef* def = sts::registry::monster_def(
            static_cast<MonsterId>(s.monsters[i].monster_id));
        if (def == nullptr || !def->is_boss()) {
            continue;  // `if (m.type != EnemyType.BOSS) continue;` (:34)
        }
        heal_player_with_relics(s, 25);  // addToTop HealAction(player, 25) (:36)
        return;              // (:38) -- first BOSS member only
    }
}

}  // namespace sts::engine
