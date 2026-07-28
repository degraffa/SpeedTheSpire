// Action-queue mechanics + getNextAction pump. See action_queue.hpp for the
// design/provenance notes, including the two places our model diverges from a
// naive reading of the source (preTurnActions storage gap; monster_attacks_queued
// reset placement).
//
// Provenance: GameActionManager.getNextAction (GameActionManager.java:185-367),
// addToBottom/addToTop/addToTurnStart (96-100, 139-149), addCardQueueItem
// (102-116), callEndOfTurnActions (369-377). Design doc §5.1-§5.4, §10 trap 9.

#include "sts/engine/action_queue.hpp"

#include <cassert>

#include "sts/engine/card_play.hpp"  // resolve_card_play wired into pump_step step 3
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"  // execute_opcode wired into pump_step
#include "sts/engine/piles.hpp"   // reset_cost_for_turn (end-turn sweep)
#include "sts/engine/power_hooks.hpp"  // start/end-of-turn power dispatch
#include "sts/engine/relic_hooks.hpp"  // start/end-of-turn relic dispatch
#include "sts/registry/manifest.hpp"   // generated kRelicsCount
#include "sts/registry/monster_table.hpp"  // monster_def, MonsterDef::is_boss

namespace sts::engine {

// The start-of-turn DrawCardAction (start_of_turn below) queues kOpcodeDrawCard,
// which must equal the interpreter's real DRAW opcode so the queued item
// actually draws when popped. Kept in lockstep here.
static_assert(kOpcodeDrawCard == static_cast<uint16_t>(Opcode::DRAW),
              "start-of-turn DrawCard opcode must match interp.hpp Opcode::DRAW");

// --- energyMaster / masterHandSize (see the derivation note in the header) ---

bool combat_is_elite_or_boss(const CombatState& s) noexcept {
    // SlaversCollar.beforeEnergyPrep (SlaversCollar.java:47-51):
    //     boolean isEliteOrBoss = getCurrRoom().eliteTrigger;
    //     for (m : getMonsters().monsters) if (m.type == EnemyType.BOSS) isEliteOrBoss = true;
    // The loop has no liveness gate -- it walks the whole monster GROUP, and it
    // runs in preBattlePrep, before anything can have died anyway.
    if (combat_is_elite_room(s.flags)) {
        return true;
    }
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        const auto* def = sts::registry::monster_def(
            static_cast<MonsterId>(s.monsters[i].monster_id));
        if (def != nullptr && def->is_boss()) {
            return true;
        }
    }
    return false;
}

int16_t energy_master(const CombatState& s) noexcept {
    // The COMPLETE set of energyMaster writers in the game, from
    // `grep -rn energyMaster com/`: these ten relics' onEquip (one `++` each,
    // one matching `--` in onUnequip), SlaversCollar.beforeEnergyPrep (the
    // conditional per-combat `++` handled below), and VoidEssence, an Act-4
    // blight with no S1 row.
    //
    // This is a deliberate SUBSET switch over the relic table, so it carries a
    // row-count pin rather than the link-error property the generated dispatch
    // surfaces have (relic_pools.cpp:18-30 states that distinction).
    static_assert(sts::registry::manifest::kRelicsCount == 142,
                  "new relic: does its onEquip touch "
                  "AbstractDungeon.player.energy.energyMaster? Only the ten "
                  "listed below do today, plus Slaver's Collar's conditional "
                  "per-combat increment. If the new one does, it belongs in "
                  "this switch.");
    int32_t master = kIroncladBaseEnergy;
    // Per SLOT, not per distinct id: the Java increment is per relic instance,
    // and the mirror preserves duplicates.
    for (uint8_t i = 0; i < s.relic_count; ++i) {
        switch (static_cast<RelicId>(s.relics[i].relic_id)) {
            case RelicId::FUSION_HAMMER:        // FusionHammer.java:47-49
            case RelicId::VELVET_CHOKER:        // VelvetChoker.java:48-50
            case RelicId::RUNIC_DOME:           // RunicDome.java:45-47
            case RelicId::CURSED_KEY:           // CursedKey.java:68-70
            case RelicId::BUSTED_CROWN:         // BustedCrown.java:46-48
            case RelicId::ECTOPLASM:            // Ectoplasm.java:45-47
            case RelicId::SOZU:                 // Sozu.java:45-47
            case RelicId::PHILOSOPHERS_STONE:   // PhilosopherStone.java:57-59
            case RelicId::COFFEE_DRIPPER:       // CoffeeDripper.java:47-49
            case RelicId::MARK_OF_PAIN:         // MarkOfPain.java:38-40
                ++master;
                break;
            case RelicId::SLAVERS_COLLAR:
                // SlaversCollar.beforeEnergyPrep (SlaversCollar.java:46-57),
                // called BY NAME from AbstractPlayer.preBattlePrep
                // (AbstractPlayer.java:1589-1590) -- between
                // drawPile.initializeDeck (:1583) and energy.prep() (:1591), so
                // the increment is in force for the whole combat including its
                // first recharge. Not an atPreBattle hook, and deliberately not
                // modelled as one.
                //
                // Balanced against onVictory by construction rather than by
                // stored state -- the derivation note in action_queue.hpp spells
                // out why the delta between combats is provably 0.
                if (combat_is_elite_or_boss(s)) {
                    ++master;
                }
                break;
            default:
                break;
        }
    }
    return static_cast<int16_t>(master);
}

int32_t game_hand_size(const CombatState& s) noexcept {
    // The complete set of masterHandSize writers that are S1 registry rows:
    // Snecko Eye alone (SneckoEye.java:29-32). The only other writers in the
    // game are the in-combat DrawPower / DrawReductionPower (DrawPower.java:38,
    // :43; DrawReductionPower.java:37, :51), which write gameHandSize -- the
    // per-combat snapshot -- not the master, and neither has a registry row.
    static_assert(sts::registry::manifest::kRelicsCount == 142,
                  "new relic: does its onEquip touch "
                  "AbstractPlayer.masterHandSize? Only Snecko Eye does today.");
    int32_t hand = kStartOfTurnDrawCount;
    for (uint8_t i = 0; i < s.relic_count; ++i) {
        if (s.relics[i].relic_id == static_cast<uint16_t>(RelicId::SNECKO_EYE)) {
            hand += 2;  // SneckoEye.HAND_MODIFICATION (SneckoEye.java:18)
        }
    }
    return hand;
}

namespace {

// A lethal DamageAction calls clearPostCombatActions, which deliberately KEEPS
// UseCardAction (GameActionManager.java:130-136; DamageAction.java:88-91).
// Therefore terminal filing still performs gameplay-visible work: Strange
// Spoon consumes its boolean, and moveToExhaustPile fires relic/power/card
// onExhaust hooks. The headless pump otherwise keeps its established immediate
// terminal halt. Rebuild the ring without USE_CARD, preserving every other
// abandoned action, then resolve the saved filing actions in queue order. Any
// hook actions they add land behind the preserved queue, exactly as addToBot
// from UseCardAction's later position would.
void resolve_pending_use_card_actions_at_terminal(CombatState& s) noexcept {
    ActionQueueItem kept[kActionQueueCap]{};
    ActionQueueItem filing[kActionQueueCap]{};
    uint8_t kept_count = 0;
    uint8_t filing_count = 0;
    const uint8_t original_count = s.action_count;
    for (uint8_t i = 0; i < original_count; ++i) {
        const uint8_t src = static_cast<uint8_t>(
            (static_cast<unsigned>(s.action_head) + i) % kActionQueueCap);
        const ActionQueueItem item = s.action_queue[src];
        if (static_cast<Opcode>(item.opcode) == Opcode::USE_CARD) {
            filing[filing_count++] = item;
        } else {
            kept[kept_count++] = item;
        }
    }
    s.action_head = 0;
    s.action_tail = 0;
    s.action_count = 0;
    for (uint8_t i = 0; i < kept_count; ++i) {
        add_to_bottom(s, kept[i]);
    }
    for (uint8_t i = 0; i < filing_count; ++i) {
        execute_opcode(s, filing[i]);
    }
}

// !areMonstersBasicallyDead() (MonsterGroup.java:90-95): a monster is in the
// fight unless `isDying || isEscaping`. The engine models isDying as hp <= 0
// and isEscaping as kMonsterFlagEscaped (monster_dead_or_escaped,
// combat_state.hpp) -- an escaped monster is ALIVE and OUT of the fight, so a
// bare hp test would keep a mugged battle open forever after the Looter leaves.
[[nodiscard]] bool any_monster_alive(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_dead_or_escaped(s.monsters[i])) {
            return true;
        }
    }
    return false;
}

// MonsterGroup.applyPreTurnLogic (MonsterGroup.java:98-105): the monster-side
// turn-start walk. For every monster that is neither dying nor escaping, clear
// its block unless it has Barricade, then fire its start-of-turn powers.
//
// WHERE IT IS CALLED FROM, AND WHY THE .java DOES NOT SAY SO. The decompiled tree
// shows `MonsterStartTurnAction` -- applyPreTurnLogic's only caller -- as
// referenced by nothing, because `AbstractRoom.endTurn` ends with
// `addToBottom((AbstractGameAction)new /* Unavailable Anonymous Inner Class!! */)`
// (AbstractRoom.java:409): CFR dropped the anonymous class that queues it. The
// call site is therefore grounded in BYTECODE --
// `AbstractRoom.endTurn (bytecode AbstractRoom$1, javap) -- CFR-dropped anonymous
// class`. `javap -c com.megacrit.cardcrawl.rooms.AbstractRoom` shows endTurn at
// offsets 167-178 constructing `AbstractRoom$1` and passing it to
// `GameActionManager.addToBottom`, and `javap -c AbstractRoom$1` shows update()
// as, in order:
//
//     addToBot(new EndTurnAction())                              // 0-8
//     addToBot(new WaitAction(1.2f))                             // 11-21
//     if (!this$0.skipMonsterTurn)                               // 24-31
//         addToBot(new MonsterStartTurnAction())                 // 34-42
//     AbstractDungeon.actionManager.monsterAttacksQueued = false // 45-51
//
// `javap -c MonsterStartTurnAction` then shows update() calling
// `AbstractDungeon.getCurrRoom().monsters.applyPreTurnLogic()` (offsets 16-22).
//
// THAT FIXES THE ORDER, and it is why this runs where it does. Those three
// actions drain out of `actions` before GameActionManager's update loop can reach
// its `!monsterAttacksQueued` branch (GameActionManager.java:303-307), so
// applyPreTurnLogic resolves after the end-of-turn discard and IMMEDIATELY BEFORE
// queueMonsters -- which is exactly pump_step step 4 below. It is also strictly
// BEFORE the monster's move and strictly AFTER the previous round's
// applyEndOfTurnPowers, which is what keeps a Metallicize monster's armour at one
// tick rather than a running total.
//
// `loseBlock()` is the no-argument overload -- `loseBlock(this.currentBlock)`
// (AbstractCreature.java:485-487) -- a flat zeroing, with no Calipers-style
// partial retention on the monster side. The single gate is
// `hasPower("Barricade")`, a bare presence test as on the player path in
// start_of_turn. There is no BackAttack or intent gate: beyond isDying/isEscaping
// the walk is unconditional.
void apply_pre_turn_logic(CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        MonsterState& m = s.monsters[i];
        if (monster_dead_or_escaped(m)) {
            continue;
        }
        bool has_barricade = false;
        for (uint8_t p = 0; p < m.power_count; ++p) {
            if (m.powers[p].power_id == static_cast<uint16_t>(PowerId::BARRICADE)) {
                has_barricade = true;
                break;
            }
        }
        if (!has_barricade) {
            m.block = 0;  // loseBlock()
        }
        // m.applyStartOfTurnPowers() (AbstractCreature.java:529-533) -- the
        // monster side of AT_START_OF_TURN, which nothing dispatched before. No
        // S1 monster-ownable power binds that hook today, so it is a no-op in
        // every current fixture; it is here because it is the other half of the
        // method, and omitting half a method is how the block clear went missing.
        dispatch_monster_at_start_of_turn(s, i);
    }
}

// queueMonsters equivalent (GameActionManager.java:306 ->
// MonsterGroup.queueMonsters, MonsterGroup.java:117-122): enqueue every live
// monster, in slot order. The Java guard is `isDeadOrEscaped() && !halfDead`;
// halfDead has no S1 producer, so the skip is exactly monster_dead_or_escaped.
void queue_monsters(CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_dead_or_escaped(s.monsters[i])) {
            assert(s.monster_queue_count < kMonsterQueueCap &&
                   "monster_queue overflow (design doc §4.1: hard assert)");
            s.monster_queue[s.monster_queue_count].monster_index = i;
            s.monster_queue[s.monster_queue_count].flags = 0;
            ++s.monster_queue_count;
        }
    }
}

// Pop the head (index 0) of the card queue, shifting the tail down one slot
// (ArrayList.remove(0); GameActionManager.java:298,325 style).
void card_queue_pop_front(CombatState& s) noexcept {
    assert(s.card_queue_count > 0);
    for (uint8_t i = 0; i + 1 < s.card_queue_count; ++i) {
        s.card_queue[i] = s.card_queue[i + 1];
    }
    --s.card_queue_count;
}

// Pop the head (index 0) of the monster queue (remove(0)).
void monster_queue_pop_front(CombatState& s) noexcept {
    assert(s.monster_queue_count > 0);
    for (uint8_t i = 0; i + 1 < s.monster_queue_count; ++i) {
        s.monster_queue[i] = s.monster_queue[i + 1];
    }
    --s.monster_queue_count;
}

// callEndOfTurnActions (GameActionManager.java:369-377 / design doc §5.4).
// Every listener in the skeleton is a no-op: no relics, no orbs, and the three
// skeleton powers (Strength/Vulnerable/Weak) have no end-of-turn hook; hand
// cards' triggerOnEndOfTurnForPlayingCard (Burn/Regret/Decay) are not in scope.
// The sequence's *structure* lives here as a documented stub so future listeners
// can attach without moving the call site.
void call_end_of_turn_actions(CombatState& s) noexcept {
    // Frozen §5.4 order (GameActionManager.callEndOfTurnActions:369-377):
    //   applyEndOfTurnRelics -> relics onPlayerEndTurn (acq order; Orichalcum)
    //   applyEndOfTurnPreCardPowers             -- Metallicize (atEndOfTurnPreEndTurnCards)
    //   TriggerEndOfTurnOrbsAction               -- no orbs
    //   hand cards triggerOnEndOfTurnForPlayingCard -- Burn/Regret/Decay (card-level)
    //   stance.onEndOfTurn                       -- stanceless
    // then applyEndOfTurnTriggers (Combust atEndOfTurn) fires via the queued
    // discard sequence (AbstractCreature.java:548-553) -- AFTER the pre-card
    // powers and hand triggers. All queue via add_to_bottom, so call order ==
    // resolution order: Metallicize block lands before Combust's HP loss/damage.
    {
        const RelicView rv = player_relics(s);  // applyEndOfTurnRelics
        dispatch_relics_on_player_end_turn(s, rv.relics, rv.count);
    }
    dispatch_at_end_of_turn_pre_card(s);   // Metallicize
    // Hand-card end-of-turn triggers (Burn/Decay/Doubt/Regret/Shame) queue here,
    // before at-end-of-turn powers. Each one PLAYS ITSELF out of the hand (the
    // Java re-queues it into the cardQueue with dontTriggerOnUseCard), so it
    // reaches the discard pile through its own USE_CARD -- ahead of the
    // DiscardAtEndOfTurnAction sweep queued below, which is exactly the pile
    // order the game produces. See dispatch_card_end_of_turn.
    dispatch_card_end_of_turn(s);
    // stance.onEndOfTurn -- stanceless stub.
    dispatch_at_end_of_turn(s);            // Combust
    // DiscardAtEndOfTurnAction follows the sentinel's card/power effects. Its
    // ethereal sweep and ordinary hand discard are collapsed into one action.
    ActionQueueItem discard_hand{};
    discard_hand.opcode = static_cast<uint16_t>(Opcode::DISCARD_HAND);
    discard_hand.src = kActorPlayer;
    discard_hand.tgt = kActorPlayer;
    add_to_bottom(s, discard_hand);
    // All no-op unless a hook-bearing power is present (fixtures unchanged).
}

// Which of the game's two call sites is running the start-of-turn sequence.
// They are NOT the same sequence: the combat-start block (AbstractRoom.java:
// 236-258) omits the end-of-round pass that getNextAction's step 6 opens with --
// see begin_first_turn below for why the game can never reach step 6 on turn 1.
enum class TurnStart : uint8_t {
    kCombatStart,      // AbstractRoom.update turn-1 block (AbstractRoom.java:236-258)
    kSubsequentTurn,   // getNextAction step 6 (GameActionManager.java:329-366)
};

// The start-of-turn DrawCardAction(gameHandSize), queued addToBot. One helper
// because the two call sites queue it at DIFFERENT points of the sequence (see
// the block comment inside start_of_turn) and the item must stay identical.
void queue_start_of_turn_draw(CombatState& s) noexcept {
    ActionQueueItem draw{};
    draw.opcode = kOpcodeDrawCard;
    draw.src = kActorPlayer;
    draw.tgt = kActorPlayer;
    // gameHandSize, not the bare base: Snecko Eye's onEquip adds 2 to
    // masterHandSize (SneckoEye.java:29-32), which preBattlePrep copies into
    // gameHandSize (AbstractPlayer.java:1579) and BOTH draw sites then read --
    // turn 1 at AbstractRoom.java:242 and turn N at GameActionManager.java:361.
    // Deriving it in this shared helper is what keeps the two sites identical.
    draw.amount = game_hand_size(s);
    draw.flags = 0;
    add_to_bottom(s, draw);
}

// Start-of-turn sequence (design doc §5.2 step 6; GameActionManager.java:
// 329-366). Stubs are named where a future subsystem would attach.
void start_of_turn(CombatState& s, TurnStart when) noexcept {
    // monsters.applyEndOfTurnPowers() (GameActionManager.java:331) -- the
    // atEndOfRound dispatch (monster atEndOfTurn -> player atEndOfRound -> monster
    // atEndOfRound). The Cultist's Ritual Strength ramp fires here. No-op unless a
    // power binds these hooks, so jaw-worm-only fixtures are byte-identical.
    //
    // It belongs to step 6 ALONE. The combat-start block has no counterpart line,
    // so a round has to have ended for this to run -- running it while priming
    // turn 1 gives every power that binds an end-of-round hook and is present at
    // combat start one free trigger before the player acts.
    if (when == TurnStart::kSubsequentTurn) {
        dispatch_at_end_of_round(s);
    }
    s.cards_played_this_turn = 0;               // player.cardsPlayedThisTurn = 0
    // orbsChanneledThisTurn.clear() -- no orbs.

    // THE TWO CALL SITES QUEUE THEIR DRAW ON OPPOSITE SIDES OF THE RELIC HOOKS,
    // and the difference is load-bearing for where a relic's addToBot lands:
    //
    //   * The combat-start block queues DrawCardAction FIRST
    //     (AbstractRoom.java:242), then fires applyStartOfCombatLogic -- every
    //     relic's atBattleStart (:245, AbstractPlayer.java:1892-1901) -- and
    //     then applyStartOfTurnRelics (:253). So on turn 1 an atBattleStart /
    //     atTurnStart body's IMMEDIATE writes happen before the draw resolves,
    //     while its addToBot items land BEHIND the queued draw (an addToTop
    //     lands ahead of it). Stone Calendar is the arithmetic witness for the
    //     immediate half: atBattleStart's counter = 0 precedes turn 1's
    //     atTurnStart ++, so the capture reads 1 on turn 1 (STS00683 seq 141,
    //     both G6 campaigns) -- an engine that dispatched AT_BATTLE_START after
    //     the whole start-of-turn sequence read 0 all fight and missed the
    //     turn-7 THORNS.
    //
    //   * Step 6 (GameActionManager.java:342-361) runs applyStartOfTurnRelics
    //     BEFORE it queues its DrawCardAction (:361), so on later turns an
    //     atTurnStart addToBot lands AHEAD of the draw.
    //
    // AT_BATTLE_START is dispatched HERE, inside the shared turn-1 block,
    // because this function is the one both construction paths reach through
    // begin_first_turn -- bolting the dispatch onto one caller is exactly how
    // the run layer inverted the counter relics while combat_begin dispatched
    // the hook not at all (G6 campaign 2 spot-diff §8.0).
    //
    // Known deviation, unobservable: in the Java the combat-start block's
    // GainEnergyAndEnableControlsAction is queued at :240, ahead of the draw,
    // so an addToTop atBattleStart body (Girya.java:532, DuVuDoll.java:31)
    // resolves before the energy gain too; here energy is an inline SET (below)
    // that no queued item ever reads, so only the order against the DRAW is
    // observable -- and that order is faithful on both halves.
    if (when == TurnStart::kCombatStart) {
        queue_start_of_turn_draw(s);            // AbstractRoom.java:242
        const RelicView rv = player_relics(s);
        dispatch_relics_at_battle_start(s, rv.relics, rv.count);  // :245
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);    // :253
    } else {
        // applyStartOfTurnRelics -> relics atTurnStart (acq order; Happy
        // Flower, Lantern). PreDrawCards -- card-level hooks not in scope.
        const RelicView rv = player_relics(s);
        dispatch_relics_at_turn_start(s, rv.relics, rv.count);
    }
    // applyStartOfTurnPowers (§5.2 step 6, pre-draw): Berserk/Mayhem/Magnetism
    // energy/play; applyStartOfTurnOrbs -- no orbs. No-op without such a power.
    // (The combat-start block calls it at AbstractRoom.java:256, after the
    // post-draw relic line; no registered power binds it at combat start -- both
    // shapes that could, Brutality and Demon Form, arrive only by card play --
    // so the shared position is step 6's.)
    dispatch_at_start_of_turn(s);

    // Energy recharge (EnergyManager.recharge(), EnergyManager.java:25-40; see
    // kIroncladBaseEnergy in action_queue.hpp). The real game performs this every
    // turn via a presentation-coupled effect that still affects outcomes, so it
    // is in scope.
    //
    // Two branches, not one. The default is EnergyPanel.setEnergy(energyMaster)
    // -- a SET, so unspent energy does NOT carry over. With Ice Cream owned it is
    // EnergyPanel.addEnergy(energyMaster) instead: an ADD, capped at 999
    // (EnergyPanel.addEnergy, EnergyPanel.java:59-68). The third branch, the
    // Conserve power, is Silent-only and has no registry row. Retires the
    // stage-a SET-to-constant simplification; the no-relic path below is
    // byte-identical to the unconditional SET it replaces.
    //
    // The Ice Cream branch is kSubsequentTurn ONLY, and the derivation is a chain
    // worth spelling out because the two call sites reach energy by different
    // routes entirely:
    //   * Ice Cream lives in EnergyManager.recharge (EnergyManager.java:26-34),
    //     and recharge() has exactly ONE caller in the whole game: the
    //     PlayerTurnEffect constructor (PlayerTurnEffect.java:46).
    //   * PlayerTurnEffect is constructed only by DrawCardAction's 3-arg ctor
    //     when `endTurnDraw` is true (DrawCardAction.java:32-34).
    //   * Step 6 passes true (`new DrawCardAction(null, gameHandSize, true)`,
    //     GameActionManager.java:361) -- so a later turn DOES run recharge().
    //   * The combat-start block passes the 2-arg ctor (`new DrawCardAction(
    //     player, gameHandSize)`, AbstractRoom.java:242), which delegates with
    //     endTurnDraw = false (DrawCardAction.java:41-43) -- so turn 1 NEVER
    //     reaches recharge(), and Ice Cream's ADD is unreachable there.
    // Turn 1's energy comes from GainEnergyAndEnableControlsAction instead
    // (AbstractRoom.java:241), whose update calls player.gainEnergy(energyMaster)
    // = EnergyPanel.addEnergy (AbstractPlayer.java:1555-1558) with NO Ice Cream
    // test -- an add onto a panel EnergyManager.prep() has just zeroed
    // (EnergyManager.java:21-24), i.e. exactly the plain SET below.
    //
    // BOTH branches use energy_master(s), not the bare constant, and that is not
    // symmetry for its own sake: Ice Cream's ADD is `EnergyPanel.addEnergy(
    // this.energy)` (EnergyManager.java:31), and `this.energy` is the value
    // prep() copied out of energyMaster -- so Fusion Hammer + Ice Cream carries
    // +4 per turn, not +3. The turn-1 route reaches the master too, by the
    // different path traced above (AbstractRoom.java:240).
    const int32_t master = energy_master(s);
    if (when == TurnStart::kSubsequentTurn &&
        player_has_relic(s, RelicId::ICE_CREAM)) {
        int32_t charged = static_cast<int32_t>(s.player_energy) + master;
        if (charged > 999) {
            charged = 999;
        }
        s.player_energy = static_cast<int16_t>(charged);
    } else {
        s.player_energy = static_cast<int16_t>(master);
    }
    // NOTE: monster_attacks_queued is deliberately NOT reset here -- it is
    // cleared at the end-turn sentinel instead (see action_queue.hpp note (2)).
    s.turn_has_ended = 0;                        // this.turnHasEnded = false
    ++s.turn;                                    // ++turn

    // Block decay (GameActionManager.java:353-359). Barricade/Blur keep block;
    // Calipers loses 15 (AbstractCreature.loseBlock(int), clamped at 0) instead
    // of zeroing. Calipers is a registered relic and Barricade is now a
    // registered power, so both of those branches are LIVE; Blur is Silent-only
    // and out of S1 scope, so its branch structure is present and never taken.
    //
    // kSubsequentTurn ONLY. This whole paragraph is step 6's; the combat-start
    // block (AbstractRoom.java:236-258) has no loseBlock line of any kind, which
    // stands to reason -- block is a per-turn resource and turn 1 has no previous
    // turn to decay. It is INERT today either way (both entry points reach
    // begin_first_turn with player_block == 0, advance.cpp / run_advance.cpp), so
    // gating it moves no fixture; it is gated because leaving a step-6-only line
    // running at combat start is exactly the shape of the end-of-round bug this
    // fix exists to remove, and it would come alive the moment anything grants
    // block before turn 1 (a Barricade-like power, or an atBattleStart block
    // relic) -- at which point turn 1 would silently eat it.
    //
    // BARRICADE SITS ON THIS SIDE OF THE TURN-1 GATE, with Calipers, and the Java
    // leaves no choice: the whole `if (!hasPower("Barricade") && !hasPower("Blur"))
    // { if (!hasRelic("Calipers")) loseBlock(); else loseBlock(15); }` paragraph is
    // GameActionManager.java:353-359, inside getNextAction's step-6 branch, and the
    // combat-start block (AbstractRoom.java:236-258) contains no loseBlock line of
    // any kind. Barricade is not a rule that ADDS something at turn start; it is a
    // guard on a decay that only step 6 performs, so on the combat-start side there
    // is nothing for it to guard. Putting it on the other side of the gate would
    // also be unobservable-by-construction and therefore untestable.
    if (when == TurnStart::kSubsequentTurn) {
        // hasPower is a PRESENCE test: BarricadePower's amount is the -1 marker
        // its ctor sets (BarricadePower.java:22), never a positive stack.
        bool has_barricade = false;
        for (uint8_t i = 0; i < s.player_power_count; ++i) {
            if (s.player_powers[i].power_id ==
                static_cast<uint16_t>(PowerId::BARRICADE)) {
                has_barricade = true;
                break;
            }
        }
        const bool has_blur = false;       // Silent-only; no registry row in S1
        const bool has_calipers = player_has_relic(s, RelicId::CALIPERS);
        if (!has_barricade && !has_blur) {
            if (!has_calipers) {
                s.player_block = 0;                              // loseBlock()
            } else {
                s.player_block = static_cast<int16_t>(           // loseBlock(15)
                    s.player_block > 15 ? s.player_block - 15 : 0);
            }
        }
    }

    // Queue DrawCardAction(gameHandSize) (GameActionManager.java:361). The pump
    // only enqueues a well-formed item here; the DRAW opcode does the drawing
    // when it is popped. kSubsequentTurn ONLY: the combat-start block queued its
    // draw at the TOP of this function (AbstractRoom.java:242 -- before the
    // relic hooks), so queueing here again would draw twice. Both sites draw
    // game_hand_size(s) -- the Snecko-enlarged field -- via the shared helper.
    if (when == TurnStart::kSubsequentTurn) {
        queue_start_of_turn_draw(s);
    }
    // applyStartOfTurnPostDrawRelics then applyStartOfTurnPostDrawPowers
    // (GameActionManager.java:361-363), both queued AFTER the DrawCardAction so
    // their effects resolve after the draw. Relics first, in acquisition order
    // (Pocketwatch's conditional 3-card draw; Gambling Chip binds this hook too,
    // with a deferred body). No-op without such a relic or power.
    //
    // The RELIC phase is deliberately UNGATED -- unlike the end-of-round pass,
    // the Ice Cream branch and the block decay above, applyStartOfTurnPostDrawRelics
    // is one of the lines the combat-start block DOES carry (AbstractRoom.java:254,
    // alongside applyStartOfTurnRelics at :253), so it belongs to every turn.
    {
        const RelicView rv = player_relics(s);
        dispatch_relics_at_turn_start_post_draw(s, rv.relics, rv.count);
    }
    // The POWER phase, by contrast, is kSubsequentTurn ONLY. The two halves of
    // "post draw" are NOT a pair the game keeps together: the combat-start block
    // carries the relic line (AbstractRoom.java:254) and calls
    // applyStartOfTurnPowers (:256), but it has no applyStartOfTurnPostDrawPowers
    // line at all. GameActionManager.java:363 -- inside the step-6 branch, right
    // after the post-draw relics at :362 -- is the whole game's only call to it
    // (grep-confirmed: two occurrences in the reference tree, the other being the
    // definition, AbstractCreature.java:541-545).
    //
    // Running it at combat start gave every power that binds
    // atStartOfTurnPostDraw and is present before the player acts one free
    // trigger -- exactly the shape of the end-of-round pass gated at the top of
    // this function. BrutalityPower.atStartOfTurnPostDraw (BrutalityPower.java:
    // 34-39) and DemonFormPower.atStartOfTurnPostDraw (DemonFormPower.java:32-36)
    // both bind the hook, so turn 1 would have run one extra draw plus HP loss,
    // or one extra Strength gain, that AbstractRoom's turn-1 block does not
    // contain.
    //
    // Inert for all landed content -- both powers are applied only by playing
    // their card, so neither can be on the player when begin_first_turn runs, and
    // nothing in the pre-battle pass or the relic mirror grants either -- which is
    // why no fixture moves. combat_start_test constructs the state directly to
    // make the divergence observable anyway.
    if (when == TurnStart::kSubsequentTurn) {
        dispatch_at_start_of_turn_post_draw(s);
    }
    // EnableEndTurnButtonAction (line 364) is modeled by step 7 handing control
    // back to the player once the queued DrawCard has drained; no separate item.
}

}  // namespace

// --- Combat start ------------------------------------------------------------

void begin_first_turn(CombatState& s, MonsterTurnFn take_turn) noexcept {
    // clear() (GameActionManager.java:431-433) opens a combat at turn 1 with
    // turnHasEnded false; monsterAttacksQueued is true from its field initializer
    // (GameActionManager.java:76) and only endTurn clears it. Here turn starts at
    // 0 so the shared sequence's ++turn lands it on 1.
    s.turn = 0;
    s.monster_attacks_queued = 1;
    // Deliberately NOT 1: with turnHasEnded false the pump below cannot take the
    // step-6 branch, which is exactly the game's situation on turn 1.
    s.turn_has_ended = 0;

    // AbstractRoom.java:229-235 -- while anything is queued, update() drains it and
    // waitTimer does not tick, so usePreBattleAction's actions resolve BEFORE the
    // turn-1 block. monsterAttacksQueued is already true, so step 4 cannot queue a
    // monster turn ahead of the player.
    pump(s, take_turn);
    if (s.phase == static_cast<uint8_t>(CombatPhase::COMBAT_OVER)) {
        return;
    }

    // The turn-1 block itself (AbstractRoom.java:236-258): the SAME start-of-turn
    // machinery every later turn runs, minus the end-of-round pass -- energy,
    // start-of-turn relics/powers, ++turn, the opening DrawCardAction.
    start_of_turn(s, TurnStart::kCombatStart);
    pump(s, take_turn);  // resolve the queued opening draw -> WAITING_ON_USER
}

// --- Monster-turn extension point -------------------------------------------

void default_monster_turn(CombatState& /*state*/,
                          uint8_t /*monster_index*/) noexcept {
    // No-op default monster turn (the extension point when no AI is supplied).
    // jaw_worm_take_turn (monster_jaw_worm.cpp) provides the real Jaw Worm turn.
}

// --- Queue insertion primitives ---------------------------------------------

void add_to_bottom(CombatState& s, ActionQueueItem item) noexcept {
    assert(s.action_count < kActionQueueCap &&
           "action_queue overflow (design doc §4.1: hard assert)");
    s.action_queue[s.action_tail] = item;
    s.action_tail = static_cast<uint8_t>((s.action_tail + 1) % kActionQueueCap);
    ++s.action_count;
}

void add_to_top(CombatState& s, ActionQueueItem item) noexcept {
    assert(s.action_count < kActionQueueCap &&
           "action_queue overflow (design doc §4.1: hard assert)");
    s.action_head =
        static_cast<uint8_t>((s.action_head + kActionQueueCap - 1) %
                             kActionQueueCap);
    s.action_queue[s.action_head] = item;
    ++s.action_count;
}

void add_to_turn_start(CombatState& s, ActionQueueItem item) noexcept {
    assert(s.pre_turn_count < kPreTurnActionQueueCap &&
           "pre_turn_actions overflow (design doc §4.1: hard assert)");
    s.pre_turn_head =
        static_cast<uint8_t>((s.pre_turn_head + kPreTurnActionQueueCap - 1) %
                             kPreTurnActionQueueCap);
    s.pre_turn_actions[s.pre_turn_head] = item;
    ++s.pre_turn_count;
}

void add_card_to_queue_bottom(CombatState& s, CardQueueItem item) noexcept {
    assert(s.card_queue_count < kCardQueueCap &&
           "card_queue overflow (design doc §4.1: hard assert)");
    s.card_queue[s.card_queue_count] = item;
    ++s.card_queue_count;
}

void add_card_to_queue_top(CombatState& s, CardQueueItem item) noexcept {
    assert(s.card_queue_count < kCardQueueCap &&
           "card_queue overflow (design doc §4.1: hard assert)");
    // TRAP 9 (design doc §10 item 9; GameActionManager.java:102-108): when the
    // queue is non-empty the new item goes to index 1 (the currently-resolving
    // head stays at index 0); only an empty queue takes it at index 0.
    if (s.card_queue_count == 0) {
        s.card_queue[0] = item;
    } else {
        for (uint8_t i = s.card_queue_count; i >= 2; --i) {
            s.card_queue[i] = s.card_queue[i - 1];
        }
        s.card_queue[1] = item;
    }
    ++s.card_queue_count;
}

CardQueueItem make_end_turn_sentinel() noexcept {
    CardQueueItem c{};
    c.card_index = kEndTurnSentinel;
    c.target = 0;
    return c;
}

bool is_end_turn_sentinel(CardQueueItem item) noexcept {
    return item.card_index == kEndTurnSentinel;
}

// --- Low-level ring pops -----------------------------------------------------

bool pop_action_front(CombatState& s, ActionQueueItem& out) noexcept {
    if (s.action_count == 0) {
        return false;
    }
    out = s.action_queue[s.action_head];
    s.action_head = static_cast<uint8_t>((s.action_head + 1) % kActionQueueCap);
    --s.action_count;
    return true;
}

bool pop_pre_turn_front(CombatState& s, ActionQueueItem& out) noexcept {
    if (s.pre_turn_count == 0) {
        return false;
    }
    out = s.pre_turn_actions[s.pre_turn_head];
    s.pre_turn_head =
        static_cast<uint8_t>((s.pre_turn_head + 1) % kPreTurnActionQueueCap);
    --s.pre_turn_count;
    return true;
}

// --- Pump --------------------------------------------------------------------

PumpStepResult pump_step(CombatState& s, MonsterTurnFn take_turn) noexcept {
    PumpStepResult r{};

    // Combat-over check, recomputed at the top of EVERY step. Three exits:
    //   * player dead -- NOT gated on cannotLose (the Java latch only guards
    //     the victory branch).
    //   * player escaped (Smoke Bomb) -- kCombatFlagPlayerEscaped. The Java
    //     ends this battle from the player's escape-timer expiry
    //     (AbstractPlayer.updateEscapeAnimation:2286-2291, an unconditional
    //     endBattle()), also not gated on cannotLose; unreachable mid-split
    //     anyway, because the latch is only ever set inside a monster's own
    //     resolution window while the player cannot act.
    //   * nobody left IN the fight -- any_monster_alive is the
    //     areMonstersBasicallyDead complement, so a monster that ESCAPED
    //     (alive, out of the fight) ends the battle exactly as a dead one does
    //     (updateEscapeAnimation:902-904's areMonstersDead check). Gated on the
    //     cannotLose latch: between a splitting slime's SUICIDE and its
    //     children's SPAWN_MONSTER actions no monster is alive, and the game
    //     keeps the battle open exactly because endBattle() checks !cannotLose
    //     (AbstractMonster.updateDeathAnimation:869; CannotLoseAction/
    //     CanLoseAction:12-15).
    // The terminal STATE distinguishes the three: player_hp <= 0 is a defeat;
    // kCombatFlagPlayerEscaped is the player's escape; otherwise every monster
    // record reads dead (hp <= 0) or escaped (kMonsterFlagEscaped, hp > 0) --
    // so an escape terminal is distinct from a kill by inspection.
    if (s.player_hp <= 0 || (s.flags & kCombatFlagPlayerEscaped) != 0u ||
        (!any_monster_alive(s) && (s.flags & kCombatFlagCannotLose) == 0u)) {
        // Resolve pending UseCardAction filing exactly: the game retains those
        // actions after lethal damage, so their Spoon RNG and onExhaust fan-out
        // remain gameplay-visible. Then normalize any limbo entry which never
        // acquired a USE_CARD (a terminal-cancelled queued autoplay).
        resolve_pending_use_card_actions_at_terminal(s);
        normalize_terminal_card_queue(s);
        flush_limbo_at_combat_over(s);
        s.phase = static_cast<uint8_t>(CombatPhase::COMBAT_OVER);
        r.outcome = PumpOutcome::COMBAT_OVER;
        return r;
    }
    s.phase = static_cast<uint8_t>(CombatPhase::RESOLVING);

    // 1. actions non-empty -> pop front, execute. The popped item is dispatched
    //    through the effect interpreter via execute_opcode (NOP/unrecognized
    //    opcodes are safe no-ops, so a value-init'd item still drains harmlessly).
    if (s.action_count > 0) {
        // CHOOSE-in-combat: a CHOOSE_CARD at the front that needs a
        // real player prompt (choice_requires_user) BLOCKS the pump -- leave it at
        // the head and hand control back to the player (the "hand card select
        // screen" is open). advance(CHOOSE, hand_slot) resolves one selection and
        // re-pumps. Forced / RANDOM choices auto-resolve via execute_opcode below.
        ActionQueueItem& front = s.action_queue[s.action_head];
        if (static_cast<Opcode>(front.opcode) == Opcode::DISCOVERY) {
            // DiscoveryAction opens a generated three-card reward screen. Build
            // the offer once, persist it in the queue item, and block until
            // advance(CHOOSE, offer_slot) consumes the item.
            prepare_discovery_choice(s, front);
            s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
            r.outcome = PumpOutcome::WAITING_ON_USER;
            return r;
        }
        // A DRAW-source CHOOSE_CARD (Secret Technique / Secret Weapon) bills its
        // temp browse group's card_random_rng draws the first time the item is
        // reached here -- SkillFromDeckToHandAction / AttackFromDeckToHandAction
        // build that group on the action's first update tick, before they know
        // whether a screen will open, so the cost lands on the blocking path AND
        // on both auto-resolve paths. A latch bit in the item makes it once even
        // though a blocked item is re-examined every pump step. No-op for every
        // other choice kind.
        prepare_choice_draw_source(s, front);
        if (static_cast<Opcode>(front.opcode) == Opcode::CHOOSE_CARD &&
            choice_requires_user(s, front)) {
            s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
            r.outcome = PumpOutcome::WAITING_ON_USER;
            return r;
        }
        pop_action_front(s, r.executed);
        execute_opcode(s, r.executed);
        r.outcome = PumpOutcome::RAN_ACTION;
        return r;
    }

    // 2. else preTurnActions non-empty -> pop front, execute (dispatched too).
    if (s.pre_turn_count > 0) {
        pop_pre_turn_front(s, r.executed);
        execute_opcode(s, r.executed);
        r.outcome = PumpOutcome::RAN_PRE_TURN;
        return r;
    }

    // 3. else cardQueue non-empty -> resolve head. Either the end-turn sentinel
    //    (null-card) or a real card play (§5.3), resolved by
    //    resolve_card_play (card_play.cpp): it runs the hook fan-outs,
    //    ++cards_played_this_turn, the trap-10 target resolution, queues the
    //    card's effect actions via add_to_bottom (they resolve on later pump
    //    iterations via step 1) followed by the USE_CARD filing action, moves
    //    the card hand->LIMBO, and deducts energy. The card reaches its
    //    destination pile when USE_CARD executes -- after its own effects.
    //    Keep the resolving card at index 0 until resolve_card_play returns,
    //    exactly as GameActionManager.getNextAction does. DoubleTapPower's
    //    UseCardAction-constructor hook can insert a replay at index 1 while
    //    that call is active; removing the original afterwards promotes the
    //    replay ahead of every play that was already queued (trap 9).
    if (s.card_queue_count > 0) {
        const CardQueueItem head = s.card_queue[0];
        if (is_end_turn_sentinel(head)) {
            s.turn_has_ended = 1;              // (endTurn(): turnHasEnded = true)
            s.monster_attacks_queued = 0;      // prime step 4 (see hpp note (2))
            // AbstractRoom.endTurn:397-405: the moment the turn ends,
            // every draw/discard/hand card's costForTurn resets to its cost --
            // SYNCHRONOUSLY, before any queued end-of-turn action resolves. Only
            // COST_MODIFIED_FOR_TURN rows (Infernal Blade's setCostForTurn(0)
            // attack) change; Blood for Blood's updateCost edits `cost` itself
            // and is untouched (reset_cost_for_turn's bit gate).
            for (uint8_t i = 0; i < s.draw_count; ++i) {
                reset_cost_for_turn(s, s.draw[i]);
            }
            for (uint8_t i = 0; i < s.discard_count; ++i) {
                reset_cost_for_turn(s, s.discard[i]);
            }
            for (uint8_t i = 0; i < s.hand_count; ++i) {
                reset_cost_for_turn(s, s.hand[i]);
            }
            call_end_of_turn_actions(s);       // §5.4 stub sequence
            r.outcome = PumpOutcome::END_TURN_SENTINEL;
        } else {
            resolve_card_play(s, head);        // (§5.3): dequeue-resolve
            r.outcome = PumpOutcome::RAN_CARD_QUEUE;
        }
        card_queue_pop_front(s);  // Java removes index 0 after useCard returns
        return r;
    }

    // 4. else if !monsterAttacksQueued -> set it, queue all live monsters.
    if (s.monster_attacks_queued == 0) {
        s.monster_attacks_queued = 1;
        // MonsterStartTurnAction, queued by AbstractRoom.endTurn's CFR-dropped
        // anonymous class and resolved out of `actions` before this branch can be
        // reached -- see apply_pre_turn_logic above for the javap evidence that
        // pins both the call site and this ordering.
        apply_pre_turn_logic(s);
        // skipMonsterTurn (GameActionManager.java:305) is tied to mechanics the
        // skeleton lacks (e.g. Entangled); future extension point, not queued.
        // Note it gates BOTH lines here in the Java: MonsterStartTurnAction is
        // only queued when it is clear (AbstractRoom$1 offsets 24-31), exactly as
        // queueMonsters is (GameActionManager.java:304-306).
        queue_monsters(s);
        r.outcome = PumpOutcome::QUEUED_MONSTERS;
        return r;
    }

    // 5. else monsterQueue non-empty -> pop head; if alive, take turn + apply
    //    turn powers. take_turn is the monster-turn seam.
    if (s.monster_queue_count > 0) {
        const uint8_t mi = s.monster_queue[0].monster_index;
        monster_queue_pop_front(s);
        // Step-5 liveness gate (GameActionManager.java:310): `!isDeadOrEscaped()
        // || halfDead` -- a monster that died OR ESCAPED while queued behind a
        // sibling forfeits its turn (halfDead has no S1 producer).
        if (mi < s.monster_count && !monster_dead_or_escaped(s.monsters[mi])) {
            take_turn(s, mi);            // m.takeTurn()
            // m.applyTurnPowers() -- stub (no monster powers with a turn hook).
        }
        r.monster_index = mi;
        r.outcome = PumpOutcome::RAN_MONSTER;
        return r;
    }

    // 6. else if turnHasEnded and monsters alive -> start-of-turn sequence.
    if (s.turn_has_ended && any_monster_alive(s)) {
        start_of_turn(s, TurnStart::kSubsequentTurn);
        r.outcome = PumpOutcome::STARTED_TURN;
        return r;
    }

    // 7. else -> control returns to the player.
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    r.outcome = PumpOutcome::WAITING_ON_USER;
    return r;
}

void pump(CombatState& s, MonsterTurnFn take_turn) noexcept {
    for (;;) {
        const PumpStepResult r = pump_step(s, take_turn);
        if (r.outcome == PumpOutcome::WAITING_ON_USER ||
            r.outcome == PumpOutcome::COMBAT_OVER) {
            return;
        }
    }
}

}  // namespace sts::engine
