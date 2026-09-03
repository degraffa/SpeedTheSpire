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
    static_assert(sts::registry::manifest::kRelicsCount == 150,
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
    // The complete set of masterHandSize writers that are registry rows: Snecko
    // Eye alone (SneckoEye.java:29-32). The other two writers in the game are the
    // in-combat DrawPower / DrawReductionPower (DrawPower.java:38,:43;
    // DrawReductionPower.java:31-34,:47-50), which write gameHandSize -- the
    // per-combat SNAPSHOT -- rather than the master.
    //
    // DRAW REDUCTION IS NOW A REGISTRY ROW (id 111, S2.28) and IS derived below.
    // The clause this comment used to end on ("and neither has a registry row")
    // is DELETED rather than amended, per conventions section 8: the prerequisite
    // arrived. DrawPower stays out and that is a positive finding, not an
    // omission -- its only sources in the game are Watcher/Silent cards and the
    // Snecko Skull, none of which is in Acts 1-3.
    static_assert(sts::registry::manifest::kRelicsCount == 150,
                  "new relic: does its onEquip touch "
                  "AbstractPlayer.masterHandSize? Only Snecko Eye does today.");
    int32_t hand = kStartOfTurnDrawCount;
    for (uint8_t i = 0; i < s.relic_count; ++i) {
        if (s.relics[i].relic_id == static_cast<uint16_t>(RelicId::SNECKO_EYE)) {
            hand += 2;  // SneckoEye.HAND_MODIFICATION (SneckoEye.java:18)
        }
    }
    // DrawReductionPower: onInitialApplication `--gameHandSize` (:31-34) and
    // onRemove `++gameHandSize` (:47-50) are a BALANCED PAIR around the power's
    // lifetime, so a PRESENCE test is exactly equivalent to replaying both
    // writes -- with no stored field, and no way for the two to drift apart.
    //
    // PRESENCE, NOT THE STACK COUNT, and that is the Java's own arithmetic rather
    // than a simplification: onInitialApplication fires ONLY on the first
    // application, so a second Head Slam raises `amount` to 2 while the hand
    // shrinks by exactly ONE card (AbstractCreature.addPower hands the amount to
    // the live object and discards the freshly constructed one, :506-513). See
    // powers/power_draw_reduction.hpp.
    for (uint8_t i = 0; i < s.player_power_count && i < kPowerCap; ++i) {
        if (s.player_powers[i].power_id ==
            static_cast<uint16_t>(PowerId::DRAW_REDUCTION)) {
            --hand;
            break;
        }
    }
    return hand;
}

namespace {

// The pump's three combat terminals are three DIFFERENT Java events, and what
// the action queue does at each of them differs, so the resolver below is told
// which one it is rather than being handed a single "did we win" bit. Defeat
// wins a tie: a player at 0 alongside an emptied field is the defeat terminal
// (AbstractPlayer.damage latches the death inside the hit; see the derivation
// on survives_clear_post_combat).
enum class TerminalKind : uint8_t { kVictory, kDefeat, kEscape };

// Termination bound for the terminal drain below. The Java's drain is
// frame-driven and has no bound at all; a headless resolver needs one so a
// self-requeueing item cannot spin forever. Four full rings.
constexpr uint16_t kTerminalDrainSteps = static_cast<uint16_t>(
    kActionQueueCap * 4);

// A lethal damage action calls clearPostCombatActions, whose survivor set is
// FOUR-ARMED (GameActionManager.java:130-137, the loop at :134):
//
//     if (e instanceof HealAction || e instanceof GainBlockAction ||
//         e instanceof UseCardAction ||
//         e.actionType == AbstractGameAction.ActionType.DAMAGE) continue;
//
// and the survivors genuinely RESOLVE before victory settlement: endBattle()
// is deathTimer-gated (AbstractMonster.die arms deathTimer, :928-950;
// updateDeathAnimation calls endBattle only once it runs out, :866-871), so
// AbstractRoom.update keeps draining the queue for the whole death animation
// (AbstractRoom.java:264-267). All four arms remain gameplay-visible: filing
// can spend Strange Spoon RNG / fire onExhaust; Reaper's
// VampireDamageAllEnemiesAction queues its heal before invoking the clear
// (STS300219 seq 23-24); and a THORNS DamageAction queued BEHIND the killing
// blow -- the Guardian's Sharp Hide onUseCard retaliation, addToBot'd after
// Blood for Blood's own damage -- still lands on the player, because the
// dying-owner cancel exempts THORNS (DamageAction.java:65,70; the engine's
// damage_attacker_cancelled, S2.49). Keeping only two of the four arms dropped
// exactly that hit (seed STS431342: 4 hp, the Sharp Hide amount).
//
// The DAMAGE arm maps to every opcode whose modelled Java action sets
// actionType = DAMAGE -- verified per class, because the name is not the type:
// DropkickAction is ActionType.BLOCK (and not a GainBlockAction, so the game
// CLEARS it) and FiendFireAction is ActionType.WAIT (cleared too).
//
// SCOPE: the widened set applies to the VICTORY terminal only. Every one of
// clearPostCombatActions' call sites is gated on areMonstersBasicallyDead()
// (DamageAction.java:88-91 and its 19 siblings); the player-death and
// player-escape terminals have no such call.
//
// AND THE PLAYER-DEATH TERMINAL RESOLVES NOTHING AT ALL (S2.43 residual, seed
// STS103364 floor 35). It used to keep the USE_CARD/HEAL pair -- a survivor
// set inherited from before the terminals were told apart, never derived for
// this one -- and the Java says the opposite. The player's death is LATCHED
// synchronously inside the hit that lands it: AbstractPlayer.damage sets
// `isDead = true` and constructs `new DeathScreen(...)` in the same statement
// pair (AbstractPlayer.java:1500-1501), and that constructor assigns
// `AbstractDungeon.screen = CurrentScreen.DEATH` (DeathScreen.java:86). From
// the next frame on, AbstractDungeon.update's screen switch takes its DEATH
// arm -- `case 17: { deathScreen.update(); break; }` (AbstractDungeon.java:
// 2092-2095; ordinal 17 == DEATH, AbstractDungeon$1.java:172) -- which, unlike
// the NONE/map arms (:2021-2023) and every screen arm that keeps the room
// alive underneath it (:2031, :2065, :2071, :2080, :2085), does NOT call
// `currMapNode.room.update()`. AbstractRoom.update is the ONLY caller of
// `actionManager.update()` in the whole game -- its three sites are
// AbstractRoom.java:231, :265 and :364, all inside that one method -- so the
// action queue FREEZES on the item that killed the player. Nothing behind it
// ever ticks again: not a queued HealAction, not the killing card's own
// UseCardAction.
//
// That is a correctness bug, not a cosmetic one, because the pump classifies
// its own terminal from `player_hp` AFTER this resolution runs. On seed
// STS103364 the player played Bite at 2 hp into a 3-HP Spiker holding Thorns
// 9: the queue became [DAMAGE(thorns 9 -> player), HEAL(2), USE_CARD(Bite)],
// the thorns landed and took the player to 0 -- correctly -- and then Bite's
// own HealAction resolved past the death for +2, so the pump read hp > 0, took
// the VICTORY branch with two monsters (36 and 21 HP) still standing, paid
// Burning Blood's 6 for a final 8 hp, and burned the reward-setup RNG. The
// live game is GAME_OVER at 0 hp. A heal must not be able to un-kill the
// player; the fix is the Java's, which is that the queue simply stops.
//
// The ESCAPE terminal is deliberately left as it was. Its Java shape is a
// third thing again -- endBattle() runs from the escape-timer expiry with NO
// clearPostCombatActions call, and AbstractRoom.update:277 then waits for
// `actions.isEmpty()`, so the queue drains in FULL -- and no capture has
// witnessed the difference. Widening it is a separate derivation.
//
// The headless pump otherwise keeps its established immediate terminal halt.
// Rebuild the ring without the survivor shapes, preserving every abandoned
// action, then resolve the survivors in their original queue order through
// execute_opcode -- which keeps damage_attacker_cancelled live, so a dying
// owner's NON-thorns queued hits still cancel (S2.49) and the THORNS
// exemption is what decides. Hook actions a survivor adds land behind the
// preserved queue, exactly as addToBot from its later position would.
[[nodiscard]] bool survives_clear_post_combat(Opcode opcode,
                                              TerminalKind kind) noexcept {
    if (kind == TerminalKind::kDefeat) {
        // The frozen queue, derived above: no arm of the allowlist applies,
        // because clearPostCombatActions was never called and the drain that
        // would have run the survivors never happens either.
        return false;
    }
    const bool victory = kind == TerminalKind::kVictory;
    switch (opcode) {
        case Opcode::USE_CARD:  // instanceof UseCardAction (:134)
        case Opcode::HEAL:      // instanceof HealAction (:134)
            return true;
        case Opcode::BLOCK:     // instanceof GainBlockAction (:134)
            return victory;
        // actionType == DAMAGE, one Java class citation per opcode:
        case Opcode::DAMAGE:               // DamageAction.java:34
        case Opcode::LOSE_HP:              // LoseHPAction.java:29
        case Opcode::DAMAGE_BLOCK:         // Body Slam's DamageAction
        case Opcode::DAMAGE_STR_MULT:      // Heavy Blade's DamageAction
        case Opcode::DAMAGE_PER_STRIKE:    // Perfected Strike's DamageAction
        case Opcode::LOSE_HP_PER_HAND:     // Regret's LoseHPAction
        case Opcode::DAMAGE_UPGRADE_SCALE: // Searing Blow's DamageAction
        case Opcode::DAMAGE_RAMPAGE:       // Rampage's DamageAction
        case Opcode::SUICIDE:              // SuicideAction (DAMAGE ctor)
        case Opcode::DAMAGE_FEED:          // FeedAction (DAMAGE ctor)
        case Opcode::VAMPIRE_DAMAGE_ALL:   // VampireDamageAllEnemiesAction
        case Opcode::DAMAGE_DRAW_PILE:     // Mind Blast's DamageAction
        case Opcode::DAMAGE_GREED:         // GreedAction (DAMAGE ctor)
        case Opcode::VAMPIRE_DAMAGE:       // VampireDamageAction
        case Opcode::RITUAL_DAGGER:        // RitualDaggerAction (DAMAGE ctor)
            return victory;
        default:
            // DROPKICK (ActionType.BLOCK, not a GainBlockAction) and
            // FIEND_FIRE (ActionType.WAIT) are cleared ON PURPOSE; everything
            // else the game clears too.
            return false;
    }
}

void resolve_pending_post_combat_actions_at_terminal(
    CombatState& s, TerminalKind kind) noexcept {
    ActionQueueItem kept[kActionQueueCap]{};
    ActionQueueItem post_combat[kActionQueueCap]{};
    uint8_t kept_count = 0;
    uint8_t post_combat_count = 0;
    const uint8_t original_count = s.action_count;
    for (uint8_t i = 0; i < original_count; ++i) {
        const uint8_t src = static_cast<uint8_t>(
            (static_cast<unsigned>(s.action_head) + i) % kActionQueueCap);
        const ActionQueueItem item = s.action_queue[src];
        const Opcode opcode = static_cast<Opcode>(item.opcode);
        if (survives_clear_post_combat(opcode, kind)) {
            post_combat[post_combat_count++] = item;
        } else {
            kept[kept_count++] = item;
        }
    }
    // THE RING BECOMES THE SURVIVOR LIST, NOT THE ABANDONED ONE (S3.44). The
    // survivors used to resolve out of the `post_combat` snapshot above while
    // the ring held `kept`, and that dropped every action a survivor QUEUES
    // while it resolves -- the item landed in a ring nothing would ever pop
    // again. The Java keeps popping: `clearPostCombatActions` prunes the queue
    // once, and AbstractRoom.update then drains what is left to EMPTY, because
    // the COMPLETE transition is gated on `actions.isEmpty()`
    // (AbstractRoom.java:277) and the monster's endBattle() is deathTimer-gated
    // behind it (AbstractMonster.java:866-871) -- so an action queued DURING
    // that drain is drained too.
    //
    // THE THORNS CASE IS EXACTLY THIS. A THORNS `DamageAction` queued by a
    // survivor is the shape the game reaches on every lethal turn at the Heart:
    // `BeatOfDeathPower.onAfterUseCard` addToBot's one THORNS hit at the player
    // (BeatOfDeathPower.java:40-44) and `onAfterUseCard` fires from
    // `UseCardAction.update` (:75, its two onAfterUseCard loops at :76-86) --
    // i.e. from a SURVIVOR (`instanceof UseCardAction`,
    // GameActionManager.java:134). Snapshot-resolving the
    // survivors dropped that hit; draining the ring lands it, in queue order,
    // before the pump adjudicates. The retaliation already queued BEHIND the
    // killing blow (the Guardian's Sharp Hide, addToBot'd from the
    // `UseCardAction` CONSTRUCTOR at play time, SharpHidePower.java:43-49) was
    // already landing -- it is a DAMAGE survivor, and the four-arm set below
    // kept it -- and it keeps landing unchanged.
    //
    // Draining the ring also fixes the ORDER of a survivor's own additions:
    // `addToTop` from a resolving survivor (ThornsPower.onAttacked,
    // ThornsPower.java:51-58, is the one that fires here) must run BEFORE the
    // next survivor, and popping the
    // front is what makes that true. The snapshot form ran the whole snapshot
    // first and never ran the insertion at all.
    s.action_head = 0;
    s.action_tail = 0;
    s.action_count = 0;
    for (uint8_t i = 0; i < post_combat_count; ++i) {
        add_to_bottom(s, post_combat[i]);
    }
    // THE SURVIVORS DRAIN ONE AT A TIME, AND THE PLAYER'S DEATH STOPS THE
    // DRAIN. This loop is a frame-by-frame drain in the Java, not a batch: each
    // survivor is one actionManager.update tick inside AbstractRoom.update.
    // The moment one of them takes the player to zero, AbstractPlayer.damage
    // latches `isDead` and stands the DeathScreen up in the same statement
    // (AbstractPlayer.java:1500-1501; DeathScreen.java:86), and from the next
    // frame AbstractDungeon.update's DEATH arm (:2092-2095) stops calling
    // `currMapNode.room.update()` -- so actionManager.update never runs again
    // (its only three call sites are AbstractRoom.java:231, :265 and :364, all
    // inside AbstractRoom.update) and every remaining survivor is frozen where
    // it stands.
    //
    // THE MUTUAL KILL IS THIS CASE, and it is why the check cannot live only at
    // the top of pump_step. When the player's attack kills the LAST monster and
    // that monster's Thorns kills the player, the pump arrives here with
    // player_hp still positive, so `kind` is kVictory and the clear really did
    // happen (areMonstersBasicallyDead was true -- DamageAction.java:88-91).
    // The THORNS DamageAction is a survivor, it resolves, and the player dies
    // HERE. Anything queued behind it -- Bite's HealAction is the witnessed one
    // -- must not resolve, or it would raise player_hp back above zero and
    // the run layer's finish_combat_after_action, which classifies from the
    // final HP, would read this terminal as a victory. The game's own
    // tie-break is exactly this ordering: the monster's endBattle() is
    // deathTimer-gated (~2s of frames, AbstractMonster.java:866-871), the
    // player's death is not gated at all, so DEATH always wins a mutual kill.
    //
    // The unresolved survivors are pushed back rather than dropped, to keep
    // this resolver's standing property that no queued action is vaporized;
    // they land behind the abandoned non-survivors, and the pump halts at
    // COMBAT_OVER without ever reading either group again.
    //
    // WHAT A SURVIVOR QUEUES IS FILTERED BY THE SAME FOUR-ARM SET, not admitted
    // wholesale. `clearPostCombatActions` is not a one-shot: every damage-shaped
    // action re-calls it whenever it finds the field empty (DamageAction.java:
    // 88-91 and its 19 siblings), and the field IS empty for the whole of this
    // drain -- nothing here can revive a monster. So a non-survivor that arrives
    // mid-drain is exactly as abandoned as one that was already queued, and it
    // joins `kept`. This keeps the change to the ORDERING question the terminal
    // actually poses and leaves the survivor set itself untouched: no action
    // CLASS starts resolving at a terminal that did not resolve there before.
    for (uint16_t step = 0; step < kTerminalDrainSteps; ++step) {
        if (s.player_hp <= 0 || s.action_count == 0) {
            break;
        }
        ActionQueueItem item{};
        if (!pop_action_front(s, item)) {
            break;
        }
        execute_opcode(s, item);
        // Re-apply the clear to whatever that survivor just queued.
        ActionQueueItem still[kActionQueueCap]{};
        uint8_t still_count = 0;
        const uint8_t pending = s.action_count;
        for (uint8_t i = 0; i < pending; ++i) {
            const uint8_t src = static_cast<uint8_t>(
                (static_cast<unsigned>(s.action_head) + i) % kActionQueueCap);
            const ActionQueueItem q = s.action_queue[src];
            if (survives_clear_post_combat(static_cast<Opcode>(q.opcode),
                                           kind)) {
                still[still_count++] = q;
            } else if (kept_count < kActionQueueCap) {
                kept[kept_count++] = q;
            }
        }
        s.action_head = 0;
        s.action_tail = 0;
        s.action_count = 0;
        for (uint8_t i = 0; i < still_count; ++i) {
            add_to_bottom(s, still[i]);
        }
    }
    // Rebuild the halted ring: the abandoned actions first, then whatever the
    // drain did not reach (a player death stopped it, or the step bound did).
    // The bound exists only so this loop is provably finite -- the Java's drain
    // is frame-driven and unbounded, but a headless resolver must not be able
    // to spin on a self-requeueing item; kActionQueueCap * 4 is four full rings,
    // far past anything a terminal queue reaches.
    ActionQueueItem unreached[kActionQueueCap]{};
    const uint8_t unreached_count = s.action_count;
    for (uint8_t i = 0; i < unreached_count; ++i) {
        const uint8_t src = static_cast<uint8_t>(
            (static_cast<unsigned>(s.action_head) + i) % kActionQueueCap);
        unreached[i] = s.action_queue[src];
    }
    s.action_head = 0;
    s.action_tail = 0;
    s.action_count = 0;
    for (uint8_t i = 0; i < kept_count && s.action_count < kActionQueueCap;
         ++i) {
        add_to_bottom(s, kept[i]);
    }
    for (uint8_t i = 0; i < unreached_count && s.action_count < kActionQueueCap;
         ++i) {
        add_to_bottom(s, unreached[i]);
    }
}

// !areMonstersBasicallyDead() (MonsterGroup.java:90-95): a monster is in the
// fight unless `isDying || isEscaping`. The engine models isDying as hp <= 0
// and isEscaping as kMonsterFlagEscaped -- an escaped monster is ALIVE and OUT
// of the fight, so a bare hp test would keep a mugged battle open forever after
// the Looter leaves.
//
// This is monster_basically_dead, NOT monster_dead_or_escaped: the Java
// predicate here has no halfDead term, so a HALF-DEAD monster is still IN the
// fight and keeps the combat open. That is what gives the Darkling its
// REINCARNATE turn and the Awakened One its REBIRTH -- with the targeting
// predicate the combat would end the moment the last record hit 0 HP and the
// revival could never happen.
[[nodiscard]] bool any_monster_alive(const CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_basically_dead(s.monsters[i])) {
            return true;
        }
    }
    return false;
}

// MonsterGroup.applyPreTurnLogic (MonsterGroup.java:98-105): the monster-side
// turn-start walk. For every monster that is neither dying nor escaping, clear
// its block unless it has Barricade, then fire its start-of-turn powers.
//
// WHERE IT IS CALLED FROM. The recovered AbstractRoom$1.java (from the shipped,
// build-identity-checked desktop jar; see that file's provenance header) is the
// anonymous action at AbstractRoom.endTurn:409. Its update() is, in order:
//
//     addToBot(new EndTurnAction())                              // 0-8
//     addToBot(new WaitAction(1.2f))                             // 11-21
//     if (!this$0.skipMonsterTurn)                               // 24-31
//         addToBot(new MonsterStartTurnAction())                 // 34-42
//     AbstractDungeon.actionManager.monsterAttacksQueued = false // 45-51
//
// MonsterStartTurnAction.update then calls
// `AbstractDungeon.getCurrRoom().monsters.applyPreTurnLogic()`.
//
// THE QUEUE INTERLEAVE IS LOAD-BEARING. DiscardAtEndOfTurnAction is already
// ahead of AbstractRoom$1. Ethereal exhaustion from that action queues primary
// onExhaust work (Feel No Pain's GainBlockAction) behind AbstractRoom$1.
// AbstractRoom$1 then appends MonsterStartTurnAction behind that primary work.
// When the primary block resolves, a secondary action it creates (Juggernaut's
// DamageRandomEnemyAction) appends behind MonsterStartTurnAction. Therefore the
// order is [Feel No Pain block, monster block clear, Juggernaut damage], not a
// single undifferentiated drain immediately before queueMonsters. The internal
// kOpcodeMonsterStartTurn marker preserves that position. STS304016 is the
// witness: the marker clears 11 Curl Up block from a 3-HP Defensive Louse before
// Juggernaut hits, so it dies without taking its turn.
//
// `loseBlock()` is the no-argument overload -- `loseBlock(this.currentBlock)`
// (AbstractCreature.java:485-487) -- a flat zeroing, with no Calipers-style
// partial retention on the monster side. The single gate is
// `hasPower("Barricade")`, a bare presence test as on the player path in
// start_of_turn. There is no BackAttack or intent gate: beyond isDying/isEscaping
// the walk is unconditional -- and, in particular, no halfDead term, so a
// half-dead monster DOES lose its block and DOES run its start-of-turn powers
// (monster_basically_dead, not monster_dead_or_escaped).
void apply_pre_turn_logic(CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        MonsterState& m = s.monsters[i];
        if (monster_basically_dead(m)) {
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
        // monster side of AT_START_OF_TURN. It landed with NO binder at all (it
        // is here because it is the other half of the method, and omitting half
        // a method is how the block clear above went missing); FLIGHT
        // (powers.yaml id 94, S2.21) is the FIRST one. FlightPower.atStartOfTurn
        // (FlightPower.java:47-51) restores `amount` to the value its ctor
        // stored, so a Byrd whose Flight was worn down by attacks is back at full
        // strength every time its own turn begins. Still a no-op in every S1
        // fixture: no Act-1 monster carries a power that binds this hook.
        dispatch_monster_at_start_of_turn(s, i);
    }
}

// queueMonsters equivalent (GameActionManager.java:306 ->
// MonsterGroup.queueMonsters, MonsterGroup.java:117-122): enqueue every live
// monster, in slot order. The Java guard is `isDeadOrEscaped() && !halfDead`,
// i.e. skip only those that are dead-or-escaped AND NOT half-dead -- which is
// exactly the complement of monster_basically_dead. The `&& !halfDead` term is
// the whole point: a half-dead monster IS queued and DOES take its turn, which
// is the turn on which the Darkling reincarnates and the Awakened One is reborn.
void queue_monsters(CombatState& s) noexcept {
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (!monster_basically_dead(s.monsters[i])) {
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
    // so an addToTop atBattleStart body (Girya.java:41, DuVuDoll.java:72)
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
        // Resolve the clearPostCombatActions survivor set exactly: the game
        // retains those actions after lethal damage (four arms on a VICTORY,
        // see survives_clear_post_combat; USE_CARD/HEAL on an escape; NOTHING
        // past a player death, where the queue freezes with the room), so
        // their Spoon RNG, onExhaust fan-out, and THORNS retaliation remain
        // gameplay-visible exactly where the game keeps them. Then normalize
        // any limbo entry which never acquired a USE_CARD (a terminal-cancelled
        // queued autoplay -- and, past a death, the killing card itself, whose
        // UseCardAction is now abandoned with the rest of the queue).
        //
        // DEFEAT WINS A TIE, and it is tested FIRST here for a reason beyond
        // classification: nothing resolves on that arm, so no queued heal can
        // raise `player_hp` back above zero and turn the pump's own reading of
        // this state into a victory. The Java latches the death inside the hit
        // (AbstractPlayer.java:1500-1501); this ordering is that latch.
        //
        // The `kind` chosen here is only the SURVIVOR SET. It is not the
        // outcome: at a MUTUAL KILL the player is still standing when this test
        // runs -- the clear really did happen, so kVictory is the right set --
        // and the death arrives inside the resolver, on the Thorns survivor
        // itself. The resolver stops there, and the outcome is read off the
        // final `player_hp` by finish_combat_after_action.
        const TerminalKind kind =
            s.player_hp <= 0
                ? TerminalKind::kDefeat
                : ((s.flags & kCombatFlagPlayerEscaped) != 0u
                       ? TerminalKind::kEscape
                       : TerminalKind::kVictory);
        resolve_pending_post_combat_actions_at_terminal(s, kind);
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
        if (static_cast<Opcode>(front.opcode) == Opcode::CODEX) {
            // Nilry's Codex (CodexAction.update). The DISCOVERY shape -- one
            // offer prepared into the item, then block for advance(CHOOSE) --
            // with CodexAction's own front gate (:29-32): every monster
            // basically dead consumes the action with NO screen and NO draws
            // (isDone before the duration branch). A halfDead monster counts
            // as alive (MonsterGroup.areMonstersBasicallyDead:90-95), so a
            // codex still fires mid-Darkling fight.
            bool basically_dead = true;
            for (uint8_t i = 0; i < s.monster_count; ++i) {
                if (!monster_basically_dead(s.monsters[i])) {
                    basically_dead = false;
                    break;
                }
            }
            if (basically_dead) {
                pop_action_front(s, r.executed);
                r.outcome = PumpOutcome::RAN_ACTION;
                return r;
            }
            prepare_codex_choice(s, front);
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
        if (r.executed.opcode == kOpcodeMonsterStartTurn) {
            // AbstractRoom$1's queued MonsterStartTurnAction. Keeping this as a
            // real queue marker, rather than folding it into step 4 below, is
            // load-bearing: a primary end-of-turn action (Feel No Pain block)
            // runs before the marker, while a secondary action it queues
            // (Juggernaut damage) runs after it.
            //
            // It is ALSO where `turnHasEnded` flips. AbstractRoom$1.update
            // queues [EndTurnAction, WaitAction, MonsterStartTurnAction] in one
            // go, and EndTurnAction.update's whole body is
            // `AbstractDungeon.actionManager.endTurn()`
            // (EndTurnAction.java:12-19), i.e. `this.turnHasEnded = true`
            // (GameActionManager.java:179-183). So the flag is FALSE for the
            // entire end-of-turn window -- the relic hooks, Metallicize, the
            // hand cards' end-of-turn self-plays, Combust, and the hand discard
            // -- and only true from here to the next
            // GainEnergyAndEnableControlsAction.
            //
            // WHY IT MATTERS: `VulnerablePower`'s ctor latches justApplied on
            // `actionManager.turnHasEnded && isSourceMonster`
            // (VulnerablePower.java:36-38) -- BOTH clauses -- and a latched
            // Vulnerable skips its first atEndOfRound decrement (:44-48). A
            // Fungi Beast killed by the player's own Combust dies inside the
            // end-of-turn window, so its Spore Cloud's
            // `new VulnerablePower(player, amount, true)`
            // (SporeCloudPower.java:36-43) is built with turnHasEnded still
            // FALSE and DOES decrement that round. Flipping the flag at the
            // end-turn sentinel instead left the player on Vulnerable 2 where
            // the game reads 1 (capture s2v3_wave1_STS207337_ps96, floor 14,
            // seq 199).
            s.turn_has_ended = 1;
            apply_pre_turn_logic(s);
        } else {
            execute_opcode(s, r.executed);
            if (static_cast<Opcode>(r.executed.opcode) ==
                Opcode::DISCARD_HAND) {
                // discard_hand_at_end_of_turn synchronously exhausts ethereal
                // cards, so their primary hook actions are already at the
                // queue bottom. Append MonsterStartTurnAction behind those
                // primaries; anything a primary queues later lands behind this
                // marker, exactly as in AbstractRoom$1.
                ActionQueueItem monster_start{};
                monster_start.opcode = kOpcodeMonsterStartTurn;
                add_to_bottom(s, monster_start);
            }
        }
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
            // NOT `turn_has_ended = 1` -- that flag flips at the MARKER below,
            // not here. `GameActionManager.endTurn()` (:179-183) is the only
            // writer of `turnHasEnded = true`, and its only caller is
            // `EndTurnAction.update` (EndTurnAction.java:12-19), which
            // AbstractRoom$1.update queues to the BOTTOM together with the
            // MonsterStartTurnAction this marker stands for -- i.e. AFTER
            // everything AbstractRoom.endTurn (:393-411) queued ahead of it:
            // applyEndOfTurnTriggers' power actions (Combust's
            // DamageAllEnemies), ClearCardQueueAction and
            // DiscardAtEndOfTurnAction. Setting it here instead put the whole
            // end-of-turn window on the wrong side of the flag; see the
            // marker branch for the divergence that measured it.
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
        // MonsterStartTurnAction has already resolved as the internal action
        // marker queued immediately after DISCARD_HAND. It cannot be folded
        // into this branch: STS304016 proves that second-order actions queued by
        // Feel No Pain -> Juggernaut must sit behind the block-clear marker.
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
        // sibling forfeits its turn. The `|| halfDead` disjunct makes that whole
        // condition the complement of monster_basically_dead, and it is exactly
        // the REBIRTH/REINCARNATE turn: a half-dead monster sits at 0 HP and
        // still acts.
        if (mi < s.monster_count && !monster_basically_dead(s.monsters[mi])) {
            take_turn(s, mi);            // m.takeTurn()
            // m.applyTurnPowers() (GameActionManager.java:322-323). This was a
            // stub reading "no monster powers with a turn hook" -- that
            // prerequisite has ARRIVED (conventions section 8: a comment
            // justifying inert code by a missing prerequisite is a bug signal,
            // so it is replaced rather than amended). Explosive and Fading both
            // bind DURING_TURN, and both rely on firing AFTER the turn body: the
            // monster attacks on the turn it self-destructs.
            dispatch_during_turn(s, mi);
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

// Unceasing Top's onRefreshHand (UnceasingTop.java:48-54), fired at the pump's
// idle boundary -- the engine's equivalent of the per-frame refreshHandLayout
// seam (CardGroup.java:231) at the ONE moment the Java gate can pass. The
// gate, term by term:
//   * actions.isEmpty()        -> s.action_count == 0. Mid-queue hand
//     refreshes never fire in the game either (the term fails there), so
//     idle-only is exact, not an approximation. The same term is what makes
//     `!AbstractDungeon.isScreenUp` redundant here: an engine "screen" is a
//     pending CHOOSE_CARD at the queue head, which keeps action_count > 0.
//   * hand.isEmpty()           -> s.hand_count == 0.
//   * canDraw                  -> s.turn >= 1. The latch is set at
//     atTurnStart, cleared ONLY at atPreBattle (:33-41), so it is exactly
//     "some turn has started this combat" -- which keeps the pre-battle
//     pump's idle (empty hand, full draw pile) from firing a draw the game
//     does not.
//   * !turnHasEnded            -> structural at an idle: end-turn processing
//     runs to completion inside the END step and never idles.
//   * !disabledUntilEndOfTurn  -> structurally unreachable: the game sets it
//     when the card queue's only entry is the end-turn autoplay
//     (GameActionManager.java:204-206), a window inside that same atomic END
//     step; no engine idle can observe it.
//   * no "No Draw"             -> the player-power scan (interp.cpp's DRAW
//     gate idiom; NoDrawPower carries the -1 no-amount marker, so only the
//     id is read).
//   * piles nonempty           -> draw_count + discard_count > 0, which is
//     also what terminates the refire loop: each fired draw either fills the
//     hand (gate fails on hand_count) or finds the piles empty.
// Effect: addToBot DrawCardAction(player, 1) (:52-53; the flash and
// RelicAboveCreatureAction are presentation), then keep pumping -- the hand
// can empty again this turn and the game's Top fires again.
//
// First live witness: s243_breadth STS432297 (Hexaghost, floor 16) -- the
// game's Top drew on a mid-turn emptied hand, the sim's deferred row did
// not, and the fight forked on draw order. The registry row's DEFERRED note
// is amended by the commit that adds this.
[[nodiscard]] bool unceasing_top_fires(const CombatState& s) noexcept {
    if (s.hand_count != 0 || s.action_count != 0 || s.turn < 1) {
        return false;
    }
    if (static_cast<int>(s.draw_count) + static_cast<int>(s.discard_count) ==
        0) {
        return false;
    }
    if (!player_has_relic(s, RelicId::UNCEASING_TOP)) {
        return false;
    }
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id ==
            static_cast<uint16_t>(PowerId::NO_DRAW)) {
            return false;
        }
    }
    return true;
}

void pump(CombatState& s, MonsterTurnFn take_turn) noexcept {
    for (;;) {
        const PumpStepResult r = pump_step(s, take_turn);
        if (r.outcome == PumpOutcome::COMBAT_OVER) {
            return;
        }
        if (r.outcome != PumpOutcome::WAITING_ON_USER) {
            continue;
        }
        if (!unceasing_top_fires(s)) {
            return;
        }
        ActionQueueItem draw{};
        draw.opcode = kOpcodeDrawCard;
        draw.src = kActorPlayer;
        draw.tgt = kActorPlayer;
        draw.amount = 1;
        draw.flags = 0;
        add_to_bottom(s, draw);
    }
}

}  // namespace sts::engine
