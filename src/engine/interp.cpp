// Effect interpreter -- opcode dispatch. See interp.hpp for the full design/
// provenance notes and the scope-boundary/field-encoding decisions (SHUFFLE_IN &
// ROLL_MOVE stubs, NOP == 0, APPLY_POWER flags encoding, EXHAUST via `amount`
// pool index, kActorPlayer actor sentinel).
//
// This TU is the DISPATCHER only: the dynamic-target fan-out (kActorAllEnemies /
// kActorRandomEnemy) and the execute_opcode switch. The op bodies live in
// per-domain translation units under src/engine/interp/ -- interp_damage.cpp
// (the DamageInfo pipeline, DAMAGE / LOSE_HP / DROPKICK), interp_block.cpp,
// interp_powers.cpp, interp_cards.cpp -- with the shared actor views in
// interp/interp_ops.hpp. See that header for the split's rationale.

#include "sts/engine/interp.hpp"

#include <cstdint>

#include "interp/interp_block.hpp"          // op_block / op_double_block / op_block_per_non_attack
#include "interp/interp_cards.hpp"          // op_make_card / op_choose_card / op_play_top_draw / ...
#include "interp/interp_damage.hpp"         // op_damage / op_lose_hp / op_dropkick
#include "interp/interp_ops.hpp"            // actor_powers (the DRAW No-Draw gate)
#include "interp/interp_powers.hpp"         // op_apply_power / op_remove_power / op_reduce_power / op_spot_weakness
#include "relics/relic_native.hpp"          // op_red_skull_entry (RED_SKULL_ENTRY)
#include "sts/engine/action_queue.hpp"
#include "sts/engine/card_play.hpp"     // roll_random_target (dequeue-time random enemy)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/monster_dispatch.hpp"  // roll_monster_move / spawn_monster_at_slot
#include "sts/engine/piles.hpp"   // draw_cards / shuffle_discard_into_draw / reshuffle_all / exhaust_card
#include "sts/engine/power_hooks.hpp"   // power hook dispatch (onCardDraw)
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- Public: dispatch --------------------------------------------------------

// DRAW and EXHAUST (and SHUFFLE_IN) are implemented in piles.cpp
// (draw_cards / exhaust_card / shuffle_discard_into_draw); the dispatch below
// delegates to them.

void execute_opcode(CombatState& s, const ActionQueueItem& item) noexcept {
    // Dynamic-target resolution at EXECUTE time (interp.hpp decision (4)).
    if (item.tgt == kActorAllEnemies) {
        // AoE: a SEPARATE op (and, for DAMAGE, a separate DamageInfo) per LIVE
        // monster (DamageAllEnemiesAction skips isDeadOrEscaped,
        // DamageAllEnemiesAction.java:56-83 -- so an escaped Looter is not hit).
        // Snapshotting the live set here matches the game resolving the AoE
        // action in place.
        for (uint8_t i = 0; i < s.monster_count; ++i) {
            if (!monster_dead_or_escaped(s.monsters[i])) {
                ActionQueueItem one = item;
                one.tgt = i;
                execute_opcode(s, one);
            }
        }
        return;
    }
    if (item.tgt == kActorRandomEnemy) {
        // One uniformly-random LIVE monster, one card_random_rng draw per hit
        // (AttackDamageRandomEnemyAction re-rolls per resolution).
        const uint8_t t = roll_random_target(s);
        if (t != kActorPlayer) {
            ActionQueueItem one = item;
            one.tgt = t;
            execute_opcode(s, one);
        }
        return;
    }
    switch (static_cast<Opcode>(item.opcode)) {
        case Opcode::NOP:
            return;  // reserved safe no-op (value-init / unrecognized item)
        case Opcode::DAMAGE:
            // Plain DAMAGE: strength_mult 1; `flags` carries the DamageType
            // in its low byte (0 == NORMAL for card attacks; THORNS for
            // reflected damage) plus the pure-matrix / null-source bits 8..9
            // (Explosive Potion's createDamageMatrix(amount, true) with a
            // null owner, ExplosivePotion.java:52).
            op_damage(s, item.src, item.tgt, item.amount, /*strength_mult=*/1,
                      damage_type_from_flags(item.flags),
                      damage_is_pure(item.flags),
                      damage_source_is_null(item.flags));
            return;
        case Opcode::BLOCK:
            op_block(s, item.tgt, item.amount, item.flags);
            return;
        case Opcode::APPLY_POWER:
            // `flags` bits 0..14 = PowerId, bit 15 = source-kind override,
            // high 16 = the applied instance's counter operand.
            op_apply_power(s, item.src, item.tgt,
                           apply_power_id_from_flags(item.flags), item.amount,
                           apply_power_counter_from_flags(item.flags),
                           apply_power_is_source_monster(item.flags));
            return;
        case Opcode::DRAW: {
            // DrawCardAction.update:69-73: while the player has No Draw,
            // a DrawCardAction ends immediately -- NOTHING is drawn (any amount,
            // including the start-of-turn 5; the power self-removes at end of
            // turn before the next start-of-turn draw). The check ignores the
            // slot amount: NoDrawPower carries the -1 no-amount marker.
            {
                const PowerView pv = actor_powers(s, kActorPlayer);
                for (uint8_t i = 0; i < pv.count; ++i) {
                    if (pv.slots[i].power_id ==
                        static_cast<uint16_t>(PowerId::NO_DRAW)) {
                        return;
                    }
                }
            }
            // draw, then fire onCardDraw per newly-drawn card (in draw order) --
            // Corruption zeroes a drawn skill's cost, Evolve/FireBreathing (later)
            // react to statuses. No-op without such a power.
            const uint8_t before = s.hand_count;
            const int drawn = draw_cards(s, item.amount);  // piles.cpp: cap + reshuffle
            for (int i = 0; i < drawn; ++i) {
                const uint8_t hi = static_cast<uint8_t>(before + i);
                if (hi >= s.hand_count) {
                    break;
                }
                const CardPoolIndex pi = s.hand[hi];
                // c.triggerWhenDrawn() fires FIRST (AbstractPlayer.draw:1642):
                // Void queues its energy loss before the power onCardDraw fan-out.
                dispatch_card_on_draw(s, pi);
                dispatch_on_card_draw(s, pi, s.card_pool[pi].card_id);
            }
            return;
        }
        case Opcode::GAIN_ENERGY:
            // player_energy += amount; no max-energy field to clamp against
            // (Ironclad base 3 is a constant, no relic/potion raises it here).
            s.player_energy = static_cast<int16_t>(s.player_energy + item.amount);
            return;
        case Opcode::SHUFFLE_IN:
            shuffle_discard_into_draw(s);  // piles.cpp: shuffle_rng + JDK LCG
            return;
        case Opcode::EXHAUST:
            // CURRENTLY UNREACHABLE (verified at the cleanup-interp refactor):
            // nothing queues Opcode::EXHAUST -- no site in src/ builds such an
            // item, and no registry YAML authors `{op: EXHAUST}`, because the
            // operand is a card-POOL index (`amount`) that a YAML author has no
            // way to name. Every in-scope exhaust reaches piles.cpp by another
            // route (the EXHAUST card flag in resolve_card_play, CHOOSE_CARD's
            // ChoiceKind::EXHAUST, EXHAUST_NON_ATTACKS, BLOCK_PER_NON_ATTACK).
            // The opcode is retained (opcodes are append-only) and becomes
            // reachable as soon as a C++ consumer that already holds a pool
            // index queues the item.
            exhaust_card(s, item.amount);  // piles.cpp
            return;
        case Opcode::ROLL_MOVE:
            // Dispatch to the target's queued-roll body if it registers one
            // (large slimes); a no-op for inline-rolling monsters. NO
            // liveness gate -- RollMoveAction.update rolls even on a dead
            // split parent (RollMoveAction.java:17-21).
            roll_monster_move(s, item.tgt);
            return;
        case Opcode::MAKE_CARD:
            op_make_card(s, make_card_id_from_flags(item.flags),
                         static_cast<CardPile>(item.src), item.amount,
                         make_card_upgraded_from_flags(item.flags));
            return;
        case Opcode::SET_COST:
            // CURRENTLY UNREACHABLE -- see op_set_cost (interp/interp_cards.cpp)
            // for the verification and what would make it reachable.
            op_set_cost(s, item.src, item.amount);
            return;
        case Opcode::LOSE_HP:
            op_lose_hp(s, item.tgt, item.amount);
            return;
        case Opcode::LOSE_HP_PER_HAND:
            // Regret: lose HP == the hand size LOCKED AT TRIGGER TIME. The game
            // stores it on the card -- `magicNumber = baseMagicNumber =
            // player.hand.size()` inside triggerOnEndOfTurnForPlayingCard
            // (Regret.java:35-39) -- and only then queues the play whose
            // LoseHPAction reads it back (Regret.java:28-33). The count is
            // therefore taken while the hand is still whole; it CANNOT be read
            // here, because the end-of-turn autoplay has already pulled every
            // triggering curse (Regret included) out of the hand by the time
            // this action resolves. dispatch_card_end_of_turn stamps `amount`.
            op_lose_hp(s, item.tgt, item.amount);
            return;
        case Opcode::DISCARD_HAND:
            discard_hand_at_end_of_turn(s);
            return;
        case Opcode::CHOOSE_CARD:
            // Reached only on the auto path (RANDOM or forced-all); a real prompt
            // is intercepted by the pump (choice_requires_user) before execute.
            op_choose_card(s, item);
            return;
        case Opcode::PLAY_TOP_DRAW:
            // The source card (Havoc) is in the limbo pile; no exclusion operand.
            op_play_top_draw(s);
            return;
        case Opcode::USE_CARD:
            // UseCardAction.update as a queued action: the Strange Spoon roll,
            // then file the played card (`amount` = pool index) out of the
            // limbo pile (op_use_card; queued by resolve_card_play).
            op_use_card(s, item);
            return;
        case Opcode::REMOVE_POWER:
            // The whole flags word: an instanced power's item additionally
            // carries the {amount, counter} instance key (interp.hpp).
            op_remove_power(s, item.tgt, apply_power_id_from_flags(item.flags),
                            item.flags);
            return;
        case Opcode::REDUCE_POWER:
            op_reduce_power(s, item.tgt,
                            apply_power_id_from_flags(item.flags), item.amount,
                            item.flags);
            return;
        case Opcode::DROPKICK:
            op_dropkick(s, item);
            return;
        case Opcode::DAMAGE_UPGRADE_SCALE:
            return;  // baked into DAMAGE by card_play.cpp
        case Opcode::DAMAGE_RAMPAGE: {
            const auto pi = static_cast<CardPoolIndex>(item.flags & 0xFFu);
            const int increment = static_cast<int>(item.flags >> 8u);
            if (pi >= kCardPoolCap) {
                return;
            }
            CardInstance& c = s.card_pool[pi];
            op_damage(s, item.src, item.tgt,
                      item.amount + static_cast<int>(c.misc));
            const int next = static_cast<int>(c.misc) + increment;
            c.misc = static_cast<uint16_t>(next > UINT16_MAX ? UINT16_MAX : next);
            return;
        }
        case Opcode::EXHAUST_NON_ATTACKS:
            op_exhaust_non_attacks(s);
            return;
        case Opcode::DOUBLE_BLOCK:
            op_double_block(s, item.tgt);
            return;
        case Opcode::BLOCK_PER_NON_ATTACK:
            op_block_per_non_attack(s, item.amount);
            return;
        case Opcode::SPOT_WEAKNESS:
            op_spot_weakness(s, item.tgt, item.amount);
            return;
        case Opcode::RANDOM_ATTACK_TO_HAND:
            op_random_attack_to_hand(s);
            return;
        case Opcode::PLAY_CARD:
            // The general recursive-play verb. `amount` is the source card-pool
            // index (ignored for the draw-top source), `flags` the kPlayCard* set.
            op_play_card(s, item.tgt, item.amount, item.flags);
            return;
        case Opcode::DAMAGE_FEED:
            // Feed: `flags` carries the max-HP gained when the hit kills.
            op_damage_feed(s, item.src, item.tgt, item.amount,
                           static_cast<int>(item.flags));
            return;
        case Opcode::FIEND_FIRE:
            op_fiend_fire(s, item.tgt, item.amount);
            return;
        case Opcode::DOUBLE_STRENGTH:
            op_double_strength(s);
            return;
        case Opcode::VAMPIRE_DAMAGE_ALL:
            op_vampire_damage_all(s, item.amount);
            return;
        case Opcode::HEAL:
            op_heal(s, item.tgt, item.amount);
            return;
        case Opcode::DAMAGE_BLOCK:
            // Body Slam: base == the player's CURRENT block (read here at execute
            // time), then the normal DamageInfo pipeline (Strength/Vulnerable still
            // apply). `src` is the player (queue_effect_step), `amount` is unused.
            op_damage(s, item.src, item.tgt, s.player_block);
            return;
        case Opcode::DAMAGE_STR_MULT:
            // Heavy Blade: `amount` base with Strength counted x `flags` (the
            // magicNumber multiplier), then the normal pipeline.
            op_damage(s, item.src, item.tgt, item.amount,
                      static_cast<int>(item.flags));
            return;
        case Opcode::DAMAGE_PER_STRIKE:
            // Baked into a plain DAMAGE at queue time (card_play.cpp); never
            // reaches execute in practice. Safe no-op if it somehow does.
            return;
        case Opcode::DAMAGE_DRAW_PILE:
            // Mind Blast: base == the DRAW PILE SIZE (read here at execute time),
            // then the normal DamageInfo pipeline. `src` is the player
            // (queue_effect_step), `amount` is unused -- the same shape as
            // DAMAGE_BLOCK above.
            op_damage(s, item.src, item.tgt, static_cast<int>(s.draw_count));
            return;
        case Opcode::CONDITIONAL_DRAW:
            op_conditional_draw(s, item.amount,
                                conditional_draw_type_from_flags(item.flags));
            return;
        case Opcode::RESHUFFLE_ALL:
            // The fused Deep Breath double shuffle (piles.cpp). The played Deep
            // Breath is in the limbo pile, so the discard scanned is exactly
            // the game's -- the former `tgt` exclusion stamp is folded away.
            reshuffle_all(s);
            return;
        case Opcode::MADNESS:
            op_madness(s);
            return;
        case Opcode::DARK_SHACKLES:
            op_dark_shackles(s, item.tgt, item.amount);
            return;
        case Opcode::DISCOVERY:
            // A DISCOVERY item is prepared and intercepted at the action-queue
            // head, then consumed by advance(CHOOSE). It never executes as an
            // ordinary popped opcode; direct execution is a safe no-op.
            return;
        case Opcode::ENLIGHTENMENT:
            op_enlightenment(s, item.amount != 0);
            return;
        case Opcode::RANDOM_COLORLESS_TO_HAND:
            // Jack of All Trades (flags 0) and Transmutation's per-X repetition
            // (kColorlessToHand* -- cost 0 for the turn, and the upgraded row's
            // copy on the upgraded card).
            op_random_colorless_to_hand(s, item.amount, item.flags);
            return;
        case Opcode::RANDOM_CARD_TO_DRAW:
            // Chrysalis / Metamorphosis: `amount` picks over the RED combat pool
            // filtered to the CardType in `flags`, ALL rolled before ANY of the
            // `amount` random-spot draw-pile insertions.
            op_random_card_to_draw(s, item.amount,
                                   random_card_to_draw_type_from_flags(item.flags));
            return;
        case Opcode::CANNOT_LOSE:
            // CannotLoseAction.update (CannotLoseAction.java:12-15): latch the
            // room's cannotLose so the pump's all-monsters-dead victory gate
            // stays closed across the split's suicide-then-spawn window.
            s.flags |= kCombatFlagCannotLose;
            return;
        case Opcode::CAN_LOSE:
            s.flags &= ~kCombatFlagCannotLose;  // CanLoseAction.java:12-15
            return;
        case Opcode::SUICIDE: {
            // SuicideAction.update (SuicideAction.java:29-36): gold = 0 (no
            // per-monster gold field), currentHealth = 0, die(relicTrigger).
            // The split passes triggerRelics == false (flags bit0 clear): no
            // power onDeath / relic onMonsterDeath dispatch. That is now a REAL
            // difference rather than a formality -- Spore Cloud binds ON_DEATH
            // (powers/power_spore_cloud.cpp) and Gremlin Horn binds
            // onMonsterDeath -- and both are correctly silent here, exactly as
            // AbstractMonster.die(false) skips both loops (AbstractMonster.java:
            // 925-937). Block is NOT cleared -- SuicideAction bypasses damage()'s
            // block-break.
            if (item.tgt >= kMonsterCap) {
                return;
            }
            s.monsters[item.tgt].hp = 0;
            return;
        }
        case Opcode::SPAWN_MONSTER:
            // SpawnMonsterAction.update (SpawnMonsterAction.java:42-73):
            // insert the record at the pre-computed slot and run the child's
            // init() aiRng roll at RESOLVE time (monster_dispatch.cpp).
            spawn_monster_at_slot(
                s, item.tgt, static_cast<MonsterId>(item.flags & 0xFFFFu),
                static_cast<int16_t>(item.amount));
            return;
        case Opcode::SET_MOVE: {
            // SetMoveAction.update (SetMoveAction.java:52-56) -> setMove:
            // pushes move history + intent, no liveness check.
            if (item.tgt >= kMonsterCap) {
                return;
            }
            set_monster_move(s.monsters[item.tgt],
                             static_cast<uint8_t>(item.amount),
                             static_cast<MonsterIntent>(item.flags & 0xFFu));
            return;
        }
        case Opcode::ESCAPE:
            // EscapeAction.update (EscapeAction.java:21-28) -> escape()
            // (AbstractMonster.java:915-919): the monster leaves the fight
            // alive. The Java sets isEscaping and a 3s timer whose expiry
            // latches `escaped` (updateEscapeAnimation:894-906); with no
            // animation clock both collapse into the one flag bit, set at
            // resolve time. HP, block and powers are untouched -- an escaped
            // monster is NOT dying, which is why every "in the fight" read
            // goes through monster_dead_or_escaped. The battle-end that
            // :902-904 performs (areMonstersDead && !cannotLose -> endBattle)
            // needs no code here: pump_step recomputes the combat-over
            // predicate from the flags at the top of every step.
            if (item.tgt >= kMonsterCap) {
                return;
            }
            s.monsters[item.tgt].flags |= kMonsterFlagEscaped;
            return;
        case Opcode::UPGRADE_ALL:
            op_upgrade_all(s);
            return;
        case Opcode::DAMAGE_GREED:
            // Hand of Greed: the ordinary damage pipeline, then `flags` gold
            // into the combat accumulator if the hit left the target dead.
            op_damage_greed(s, item.src, item.tgt, item.amount,
                            damage_greed_gold_from_flags(item.flags));
            return;
        case Opcode::DRAW_PILE_FETCH:
            // Violence / DrawPileToHandAction.update: `amount` cards of the
            // CardType in `flags` out of the draw pile and into the hand.
            op_draw_pile_fetch(s, item.amount,
                               draw_pile_fetch_type_from_flags(item.flags));
            return;
        case Opcode::REMOVE_DEBUFFS:
            // Orange Pellets / RemoveDebuffsAction: enumerate tgt's DEBUFFs HERE,
            // at resolve time, and queue one addToTop REMOVE_POWER per hit.
            op_remove_debuffs(s, item.tgt);
            return;
        case Opcode::UPGRADE_RANDOM_CARD:
            // Warped Tongs / UpgradeRandomCardAction: one shuffle_rng draw over
            // the pre-filtered hand, and none at all when nothing is eligible.
            op_upgrade_random_card(s);
            return;
        case Opcode::RANDOMIZE_HAND_COST:
            // Snecko Oil / RandomizeHandCostAction: one card_random_rng random(3)
            // per hand card whose BASE cost is non-negative, in hand order, and
            // the resulting cost is PERMANENT for the instance.
            op_randomize_hand_cost(s);
            return;
        case Opcode::RED_SKULL_ENTRY:
            // Red Skull's battle-start decider (RedSkull$1): re-test
            // `!isActive && player.isBloodied` HERE, at resolve time -- after
            // the addToTop battle-start heals have settled -- and grant +3 via
            // the game's direct addPower. No operands.
            op_red_skull_entry(s);
            return;
        default:
            return;  // any unrecognized opcode is a safe no-op (decision (3))
    }
}

}  // namespace sts::engine
