// Monster init/turn dispatch + spawn. See
// monster_dispatch.hpp for the scope/rationale. The dispatch tables are a plain
// switch on MonsterId (data-oriented, no virtual dispatch); each new monster
// module adds its case.

#include "sts/engine/monster_dispatch.hpp"

#include <cassert>
#include <cstddef>

#include "sts/engine/action_queue.hpp"     // add_to_bottom, ActionQueueItem, kActorPlayer
#include "sts/engine/monster_cultist.hpp"  // cultist_init / cultist_take_turn
#include "sts/engine/monster_gremlin.hpp"  // the five Act-1 gremlins
#include "sts/engine/monster_gremlin_nob.hpp"  // gremlin_nob_init / _take_turn
#include "sts/engine/monster_guardian.hpp" // The Guardian's mode state machine
#include "sts/engine/monster_hexaghost.hpp"  // Hexaghost orb-count cycle
#include "sts/engine/monster_jaw_worm.hpp" // jaw_worm_init / jaw_worm_take_turn
#include "sts/engine/monster_lagavulin.hpp" // Lagavulin sleep/wake machine
#include "sts/engine/monster_louse.hpp"    // louse_* init / take_turn / pre_battle
#include "sts/engine/monster_sentry.hpp"   // sentry_* init / take_turn / pre_battle
#include "sts/engine/monster_slime.hpp"    // small/medium slime init + turns
#include "sts/engine/monster_slime_large.hpp"  // large slimes + split framework
#include "sts/engine/monster_slime_boss.hpp"   // Slime Boss native AI/split
#include "sts/registry/manifest.hpp"           // generated kMonstersCount

namespace sts::engine {

void queue_monster_move_effects(CombatState& state, uint8_t mi,
                                const sts::registry::MonsterDef& def,
                                uint8_t move) noexcept {
    const sts::registry::MonsterMove* mv = def.move(move);
    if (mv == nullptr) {
        return;  // unknown/empty move id: nothing decided yet (defensive)
    }
    for (uint8_t i = 0; i < mv->effect_count; ++i) {
        const sts::registry::MonsterMoveEffect& e = mv->effects[i];
        ActionQueueItem it{};
        // sts::registry::Opcode is pinned byte-equal to interp.hpp's Opcode
        // (static_asserts in cards.hpp), so the raw cast dispatches correctly.
        it.opcode = static_cast<uint16_t>(e.op);
        it.src = mi;
        it.tgt = (e.target == sts::registry::MonsterMoveTarget::SELF)
                     ? mi
                     : kActorPlayer;
        it.amount = e.amount.at(kMonsterAscension);
        it.flags = e.extra;  // APPLY_POWER: PowerId (make_apply_power_flags packing)
        if (e.op == sts::registry::Opcode::MAKE_CARD) {
            // Monster-authored MAKE_CARD uses the same generated packing as card
            // programs. The interpreter expects CardPile in src and CardId in
            // flags; tgt remains the player to avoid dynamic enemy fan-out.
            it.src = static_cast<uint8_t>((e.extra >> 16) & 0xFFu);
            it.tgt = kActorPlayer;
        }
        add_to_bottom(state, it);
    }
}

// Every MonsterId in the registry has a module, so this switch is exhaustive and
// carries no `default:`: -Wswitch (via -Wall) is what tells you a newly generated
// enumerator needs an init function here, instead of the silent nullptr a
// `default:` used to hand back. The trailing return is NOT the old default -- it
// is unreachable for any declared id, and exists because callers reach this
// through `static_cast<MonsterId>(state.monsters[i].monster_id)`, so a corrupt
// record can present a value that is in no case label at all.
MonsterInitFn monster_init_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::NONE:
            break;  // the empty-slot sentinel (ids.hpp), never a spawnable monster
        case MonsterId::JAW_WORM:
            return &jaw_worm_init;
        case MonsterId::CULTIST:
            return &cultist_init;
        case MonsterId::LOUSE_NORMAL:
            return &louse_normal_init;
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_defensive_init;
        case MonsterId::SPIKE_SLIME_SMALL:
            return &spike_slime_small_init;
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_init;
        case MonsterId::ACID_SLIME_SMALL:
            return &acid_slime_small_init;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_init;
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_init;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_init;
        case MonsterId::SLIME_BOSS:
            return &slime_boss_init;
        case MonsterId::GREMLIN_NOB:
            return &gremlin_nob_init;
        case MonsterId::SENTRY:
            return &sentry_init;
        case MonsterId::LAGAVULIN:
            // The ELITE encounter's `new Lagavulin(true)` (MonsterHelper.java:
            // 439-441). lagavulin_init_awake is the "Lagavulin Event" ctor and has
            // no encounter to spawn it yet.
            return &lagavulin_init;
        case MonsterId::GREMLIN_WARRIOR:
            return &gremlin_warrior_init;
        case MonsterId::GREMLIN_THIEF:
            return &gremlin_thief_init;
        case MonsterId::GREMLIN_FAT:
            return &gremlin_fat_init;
        case MonsterId::GREMLIN_TSUNDERE:
            return &gremlin_tsundere_init;
        case MonsterId::GREMLIN_WIZARD:
            return &gremlin_wizard_init;
        case MonsterId::THE_GUARDIAN:
            return &guardian_init;
        case MonsterId::HEXAGHOST:
            return &hexaghost_init;
    }
    return nullptr;  // NONE, or an id no case label covers (see above)
}

// Exhaustive for the same reason as monster_init_fn; see that comment.
MonsterTurnFn monster_turn_fn(MonsterId id) noexcept {
    switch (id) {
        case MonsterId::NONE:
            break;  // the empty-slot sentinel (ids.hpp), never a spawnable monster
        case MonsterId::JAW_WORM:
            return &jaw_worm_take_turn;
        case MonsterId::CULTIST:
            return &cultist_take_turn;
        case MonsterId::LOUSE_NORMAL:
            return &louse_normal_take_turn;
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_defensive_take_turn;
        case MonsterId::SPIKE_SLIME_SMALL:
            return &spike_slime_small_take_turn;
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_take_turn;
        case MonsterId::ACID_SLIME_SMALL:
            return &acid_slime_small_take_turn;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_take_turn;
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_take_turn;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_take_turn;
        case MonsterId::SLIME_BOSS:
            return &slime_boss_take_turn;
        case MonsterId::GREMLIN_NOB:
            return &gremlin_nob_take_turn;
        case MonsterId::SENTRY:
            return &sentry_take_turn;
        case MonsterId::LAGAVULIN:
            return &lagavulin_take_turn;
        case MonsterId::GREMLIN_WARRIOR:
            return &gremlin_warrior_take_turn;
        case MonsterId::GREMLIN_THIEF:
            return &gremlin_thief_take_turn;
        case MonsterId::GREMLIN_FAT:
            return &gremlin_fat_take_turn;
        case MonsterId::GREMLIN_TSUNDERE:
            return &gremlin_tsundere_take_turn;
        case MonsterId::GREMLIN_WIZARD:
            return &gremlin_wizard_take_turn;
        case MonsterId::THE_GUARDIAN:
            return &guardian_take_turn;
        case MonsterId::HEXAGHOST:
            return &hexaghost_take_turn;
    }
    // dispatch_monster_turn calls the result unconditionally, so this must be a
    // live no-op rather than nullptr.
    return &default_monster_turn;  // NONE, or an id no case label covers
}

MonsterRollMoveFn monster_roll_move_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 21,
                  "new monster: does its turn QUEUE a ROLL_MOVE item (rather "
                  "than rolling inline)? Only then does it register here.");
    // Checked for The Guardian: it queues none. getMove (TheGuardian.java:
    // 226-232) runs only from init's rollMove; every later transition is a
    // direct setMove, so no ROLL_MOVE item ever targets it.
    // Checked for Hexaghost: it queues one on FIVE of its six move bodies
    // (Hexaghost.java:167,176,188,196,209); only the ACTIVATE opener
    // re-telegraphs with a direct setMove (:153).
    switch (id) {
        case MonsterId::SPIKE_SLIME_LARGE:
            return &spike_slime_large_roll_move;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_roll_move;
        // GremlinFat is the only gremlin whose takeTurn ends in a real
        // RollMoveAction (GremlinFat.java:80); the other four re-telegraph with a
        // direct setMove or a queued SetMoveAction and roll nothing.
        case MonsterId::GREMLIN_FAT:
            return &gremlin_fat_roll_move;
        // Hexaghost's queued roll also carries the ChangeStateAction that
        // immediately precedes it, and Inferno's BurnIncreaseAction ahead of
        // that -- the whole run is atomic in the Java queue
        // (monster_hexaghost.hpp).
        case MonsterId::HEXAGHOST:
            return &hexaghost_roll_move;
        default:
            return nullptr;  // rolls inline in its MonsterTurnFn; no queued rolls
    }
}

void roll_monster_move(CombatState& state, uint8_t monster_index) noexcept {
    if (monster_index >= kMonsterCap) {
        return;
    }
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterRollMoveFn fn = monster_roll_move_fn(id);
    if (fn != nullptr) {
        fn(state, monster_index);  // no liveness gate (RollMoveAction.java:17-21)
    }
}

MonsterSpawnAtHpFn monster_spawn_at_hp_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 21,
                  "new monster: can anything spawn it mid-combat (a split, a "
                  "summon)? Only then does it need a spawn-at-fixed-HP init "
                  "here; spawn_monster_at_slot hard-asserts without one.");
    // Checked for The Guardian: nothing spawns it mid-combat. It is a solo boss
    // encounter (encounters.yaml "The Guardian") and neither splits nor summons.
    // Same for Hexaghost: a solo boss encounter (encounters.yaml "Hexaghost")
    // whose six orbs are rendering objects, not monster records
    // (HexaghostOrb.java:19-59) -- nothing is ever spawned mid-combat.
    switch (id) {
        case MonsterId::SPIKE_SLIME_MEDIUM:
            return &spike_slime_medium_spawn_at_hp;
        case MonsterId::ACID_SLIME_MEDIUM:
            return &acid_slime_medium_spawn_at_hp;
        case MonsterId::SPIKE_SLIME_LARGE:  // Slime Boss split children
            return &spike_slime_large_spawn_at_hp;
        case MonsterId::ACID_SLIME_LARGE:
            return &acid_slime_large_spawn_at_hp;
        default:
            return nullptr;  // not mid-combat spawnable
    }
}

void spawn_monster_at_slot(CombatState& state, uint8_t slot, MonsterId id,
                           int16_t hp) noexcept {
    assert(state.monster_count < kMonsterCap &&
           "spawn_monster_at_slot: monster record overflow (kMonsterCap sized "
           "for the fully-split Slime Boss, combat_state.hpp)");
    if (slot > state.monster_count) {
        slot = state.monster_count;  // defensive clamp (addMonster clamps < 0)
    }
    // List-insert: shift records at >= slot up one (MonsterGroup.addMonster,
    // MonsterGroup.java:35-40). Dead records shift too -- index identity among
    // ALL records is what smart positioning counted.
    for (uint8_t i = state.monster_count; i > slot; --i) {
        state.monsters[i] = state.monsters[i - 1];
    }
    state.monsters[slot] = MonsterState{};
    ++state.monster_count;
    // Remap pending monster-turn queue entries (the game's monsterQueue holds
    // object references, immune to list insertion; this engine holds indices).
    for (uint8_t i = 0; i < state.monster_queue_count; ++i) {
        if (state.monster_queue[i].monster_index >= slot) {
            ++state.monster_queue[i].monster_index;
        }
    }
    const MonsterSpawnAtHpFn fn = monster_spawn_at_hp_fn(id);
    assert(fn != nullptr && "spawn_monster_at_slot: monster is not mid-combat "
                            "spawnable (monster_spawn_at_hp_fn)");
    fn(state, slot, hp);  // m.init(): the child's aiRng roll, at resolve time
}

void on_monster_damaged(CombatState& state, uint8_t monster_index,
                        int32_t hp_lost) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 21,
                  "new monster: does its Java class override damage()? Only "
                  "then does it register a post-damage hook here.");
    if (monster_index >= kMonsterCap) {
        return;
    }
    switch (static_cast<MonsterId>(state.monsters[monster_index].monster_id)) {
        case MonsterId::SPIKE_SLIME_LARGE:
        case MonsterId::ACID_SLIME_LARGE:
            large_slime_on_damaged(state, monster_index);
            return;
        case MonsterId::SLIME_BOSS:
            slime_boss_on_damaged(state, monster_index);
            return;
        case MonsterId::LAGAVULIN:
            // The only override that reads how much HP actually moved
            // (Lagavulin.java:199-205); the slime interrupts test resulting HP.
            lagavulin_on_damaged(state, monster_index, hp_lost);
            return;

        // Sentry.damage (Sentry.java:115-122) DOES override damage(), but its
        // whole body after super.damage() is the "hit" spine animation, gated on
        // a non-THORNS hit with output > 0. Nothing there touches combat state or
        // draws RNG, so an empty hook is the complete translation -- hp_lost is
        // deliberately unread. Spelled as a case rather than left to `default:`
        // so the omission is checkable.
        case MonsterId::SENTRY:
            return;

        // TheGuardian.damage (TheGuardian.java:275-292) -- unlike the split
        // interrupts above, this override reads the SIZE of the hit
        // (`tmpHealth - currentHealth`), not the resulting HP. It reconstructs
        // that delta from its own HP baseline (monster_guardian.hpp). This hook
        // now DOES carry the lost HP -- the `hp_lost` parameter Lagavulin's wake
        // test needed -- so the Guardian's private reconstruction is redundant.
        // It is left as written because the two are equivalent here and swapping
        // them is a behaviour-preserving cleanup that wants its own test run,
        // not a change made in passing.
        case MonsterId::THE_GUARDIAN:
            guardian_on_damaged(state, monster_index);
            return;

        // Hexaghost does NOT override damage() (Hexaghost.java declares
        // takeTurn/getMove/changeState/die/update/render and nothing else), so
        // there is genuinely no hook to register. Spelled as a case for the
        // same reason as Sentry above.
        case MonsterId::HEXAGHOST:
            return;
        default:
            return;  // no damage() override
    }
}

MonsterPreBattleFn monster_pre_battle_fn(MonsterId id) noexcept {
    static_assert(sts::registry::manifest::kMonstersCount == 21,
                  "new monster: does it override usePreBattleAction? Read the "
                  "method and either register it here or add an explicit "
                  "nullptr case recording why it needs no engine behaviour.");
    switch (id) {
        case MonsterId::LOUSE_NORMAL:
        case MonsterId::LOUSE_DEFENSIVE:
            return &louse_use_pre_battle_action;  // curl-up roll (monster_hp_rng)

        case MonsterId::SENTRY:
            return &sentry_use_pre_battle_action;  // Artifact 1 (no RNG draw)

        case MonsterId::LAGAVULIN:
            // Asleep: 8 Block + Metallicize(8) (Lagavulin.java:104-107). Awake:
            // a bare setMove(DEBUFF) (:112). No RNG on either branch.
            return &lagavulin_use_pre_battle_action;

        // TheGuardian.usePreBattleAction (TheGuardian.java:122-132) is another
        // pre-battle override with real combat content: past the BGM/ambiance
        // and UnlockTracker lines it applies ModeShiftPower(this, dmgThreshold)
        // to itself and resets the damage accumulator. No RNG.
        case MonsterId::THE_GUARDIAN:
            return &guardian_use_pre_battle_action;

        // Hexaghost.usePreBattleAction (Hexaghost.java:130-134) is entirely
        // meta-progression and presentation: UnlockTracker.markBossAsSeen
        // ("GHOST") and a BGM precache. It touches no combat state and draws no
        // RNG, so -- as with the Slime Boss below -- this nullptr is the
        // complete, correct translation. Spelled as a case rather than left to
        // the `default:` so the omission is checkable.
        case MonsterId::HEXAGHOST:
            return nullptr;

        // The two monsters below DO override usePreBattleAction; both are
        // deliberately nullptr here, and the reasons differ. They are spelled out
        // as cases rather than left to the `default:` so the omission is a
        // recorded decision that a reader can check, not an invisible hole.

        // JawWorm.usePreBattleAction (JawWorm.java:112-118) queues Strength
        // (bellowStr) + Block (bellowBlock) before turn 1, but ONLY when
        // hardMode is set. hardMode comes solely from the 3-arg ctor
        // (JawWorm.java:75-80), used only by the "Jaw Worm Horde" group
        // (MonsterHelper.java:549-550), which only TheBeyond.generateStrongEnemies
        // schedules (TheBeyond.java:109) -- Act 3. The 2-arg ctor the Exordium
        // "Jaw Worm" encounter uses (MonsterHelper.java:397-398) delegates with
        // hard=false (JawWorm.java:71-73). The registry is Exordium-only, so no
        // reachable encounter sets hardMode and there is no divergence today.
        //
        // Whoever adds Act 3 must implement BOTH halves: hardMode also clears
        // firstMove (JawWorm.java:78-80), which skips the forced opening Chomp
        // that getMove would otherwise play (JawWorm.java:150-151). The
        // pre-battle powers are the visible half; the move-selection change is
        // the one that silently shifts the ai_rng sequence.
        case MonsterId::JAW_WORM:
            return nullptr;

        // SlimeBoss.usePreBattleAction (SlimeBoss.java:109-117) is entirely
        // presentation and meta-progression: unsilence BGM, fade ambiance, play
        // the boss track, UnlockTracker.markBossAsSeen("SLIME"). It touches no
        // combat state and draws no RNG, so a headless simulator has nothing to
        // reproduce -- this nullptr is the complete, correct translation.
        case MonsterId::SLIME_BOSS:
            return nullptr;

        // GremlinWarrior.usePreBattleAction (GremlinWarrior.java:63-70) is the
        // real thing: addToBottom ApplyPowerAction(self, AngryPower(self,
        // ascension >= 17 ? 2 : 1)). It draws no RNG. None of the other four
        // gremlins declares the method (they inherit the empty base body), so
        // they fall through to the default below.
        case MonsterId::GREMLIN_WARRIOR:
            return &gremlin_warrior_use_pre_battle_action;

        default:
            // Checked, not assumed: of the 21 registry monsters only JawWorm,
            // LouseNormal, LouseDefensive, SlimeBoss, Sentry, Lagavulin,
            // GremlinWarrior, TheGuardian and Hexaghost declare the method at
            // all. The other twelve (Cultist, GremlinNob, the four small/medium
            // slimes, the two large slimes, and the Thief / Fat / Tsundere /
            // Wizard gremlins) inherit AbstractMonster's empty body
            // (AbstractMonster.java:953-954), so there is genuinely nothing to
            // run for them.
            //
            // The sibling hook AbstractMonster.useUniversalPreBattleAction
            // (:956-968, called from MonsterGroup.java:78) is likewise absent by
            // design: every branch in it is gated on a daily-run modifier
            // ("Lethality", "Time Dilation") or on player blights, none of which
            // exist in the A20 runs this engine simulates.
            return nullptr;
    }
}

void dispatch_monster_turn(CombatState& state, uint8_t monster_index) noexcept {
    const MonsterId id =
        static_cast<MonsterId>(state.monsters[monster_index].monster_id);
    const MonsterTurnFn fn = monster_turn_fn(id);
    fn(state, monster_index);
}

void spawn_group(CombatState& state, std::span<const MonsterId> group) noexcept {
    assert(group.size() <= static_cast<std::size_t>(kMonsterCap) &&
           "spawn_group: group exceeds kMonsterCap");
    state.monster_count = static_cast<uint8_t>(group.size());
    for (uint8_t i = 0; i < group.size(); ++i) {
        const MonsterInitFn init = monster_init_fn(group[i]);
        assert(init != nullptr &&
               "spawn_group: no init fn for this id -- every registry monster "
               "has one, so this is MonsterId::NONE or a corrupt id");
        init(state, i);
    }
}

void use_pre_battle_actions(CombatState& state) noexcept {
    // preBattlePrep runs usePreBattleAction over the group in spawn order
    // (MonsterRoom.onPlayerEntry -> player.preBattlePrep, AbstractPlayer.java:1602).
    // monster_hp_rng is thus consumed in two phases (ctor HP rolls, then curl-ups),
    // both in spawn order -- the stream sees the concatenation.
    for (uint8_t i = 0; i < state.monster_count; ++i) {
        const MonsterId id =
            static_cast<MonsterId>(state.monsters[i].monster_id);
        const MonsterPreBattleFn fn = monster_pre_battle_fn(id);
        if (fn != nullptr) {
            fn(state, i);
        }
    }
}

}  // namespace sts::engine
