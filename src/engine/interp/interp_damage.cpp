// DAMAGE-domain opcode bodies -- the DamageInfo float pipeline and the two HP
// paths that land it (moved verbatim out of interp.cpp's anonymous namespace;
// see interp_ops.hpp for the split's rationale).
//
// Provenance: DamageInfo.java:35-100, MathUtils.java:217, StrengthPower.java:
// 92-98, VulnerablePower.java:61-73, WeakPower.java:61-70. Design doc §5.5, §6,
// §10 trap 1.

#include "interp_damage.hpp"

#include <cstdint>

#include "interp_ops.hpp"                   // actor_powers / actor_hp / actor_block
#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"             // CardId (Blood for Blood cost update)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_dispatch.hpp"  // on_monster_damaged (split interrupt)
#include "sts/engine/potions.hpp"          // potion_def (Fairy in a Bottle potency)
#include "sts/engine/power_hooks.hpp"       // power hook dispatch (wasHPLost/onAttacked)
#include "sts/engine/relic_hooks.hpp"       // player_has_relic (Paper Phrog) + onMonsterDeath dispatch
#include "sts/engine/types.hpp"
#include "sts/registry/manifest.hpp"        // generated kPowersCount

namespace sts::engine {

namespace {

// --- Damage hooks (NORMAL damage only; skeleton powers) ---------------------
// Iteration order == power-list order == application order (design doc §4.1).
// Every hook is a float op mirroring the cited Java exactly.

// atDamageGive: attacker-owned hooks. StrengthPower.atDamageGive (+amount),
// WeakPower.atDamageGive (*0.75f). Others pass through. `strength_mult` scales the
// Strength contribution (Heavy Blade counts Strength x magicNumber by temporarily
// multiplying strength.amount before applyPowers; HeavyBlade.java:47-56). The
// default 1 is bit-identical to an unmultiplied hook: float(amount) * 1.0f == float(
// amount), so every non-Heavy-Blade damage number is unchanged.
// The `default: return dmg` below is a deliberate subset, not an oversight: most
// powers do not touch this hook. -Wswitch-enum would flag all 27 here and is not
// enabled (it fires ~629 times across the tree and most of those sites should
// not be exhaustive). This static_assert buys the property that actually
// matters -- a new power cannot land without someone looking at this switch.
[[nodiscard]] float at_damage_give(float dmg, PowerSlot p,
                                   int strength_mult = 1) noexcept {
    // Checked for the Guardian's two powers, neither needs a case: ModeShiftPower
    // overrides only updateDescription (ModeShiftPower.java:27-30) and
    // SharpHidePower only updateDescription + onUseCard (SharpHidePower.java:
    // 38-49). Sharp Hide QUEUES damage, but it does not scale anyone else's.
    // Checked for the six red-rare powers, none needs a case: Barricade
    // (BarricadePower.java:27-30) and Berserk (BerserkPower.java:27-42) override
    // only updateDescription (+ atStartOfTurn); Brutality (:29-39), Demon Form
    // (:27-36) and Double Tap (:37-73) hook the turn/card lifecycle, not the
    // damage pipeline; Juggernaut (:34-45) only READS a block gain. Demon Form is
    // the one that looks like a case and is not -- it GRANTS Strength, whose own
    // case below already does the scaling.
    // Checked for Confusion (Snecko Eye's ConfusionPower), which needs no
    // case: its ONLY override is onCardDraw (ConfusionPower.java:38-48).
    // Checked for the two monster powers added alongside the slavers and the
    // Fungi Beast, neither of which needs a case:
    // EntanglePower overrides only playApplyPowerSfx / updateDescription /
    // atEndOfTurn (EntanglePower.java:31-46) and SporeCloudPower only
    // updateDescription / onDeath (SporeCloudPower.java:28-42).
    // Checked for the Looter's Thievery, which needs no case: ThieveryPower's
    // ONLY override is updateDescription (ThieveryPower.java:27-30) -- a pure
    // marker; the steal itself rides DamageAction's stealGold, not a power hook.
    // Checked for No Block, which needs no case: its ONLY damage-relevant answer
    // is "none". NoBlockPower declares atEndOfRound (NoBlockPower.java:39-50),
    // updateDescription (:52-55) and modifyBlockLast (:58-60) -- the last of those
    // is on the BLOCK path (interp_block.cpp), not this pipeline.
    // Checked for Mayhem and Magnetism (the two colorless-RARE POWER cards'
    // start-of-turn generators), neither of which needs a
    // case in ANY of this file's three passes: MayhemPower's only overrides are
    // updateDescription and atStartOfTurn (MayhemPower.java:28-39) and
    // MagnetismPower's are updateDescription, stackPower and atStartOfTurn
    // (MagnetismPower.java:30-49). Both are card GENERATORS -- they touch no
    // damage number at all.
    // Checked for Panache and The Bomb, neither of which needs a case in ANY of
    // this file's three passes -- and both LOOK like damage powers, which is
    // exactly why they were read in full. PanachePower's only overrides are
    // updateDescription, stackPower, onUseCard and atStartOfTurn
    // (PanachePower.java:40-67); TheBombPower's are atEndOfTurn and
    // updateDescription (TheBombPower.java:40-53). Each QUEUES damage; neither
    // SCALES anyone else's, which is the only question these switches ask. Their
    // own damage is moreover PURE (createDamageMatrix(..., true), DamageInfo.
    // java:126-136) and typed THORNS, so it does not reach this pipeline at all.
    // Checked for Vigor and Pen Nib, the two relic-granted powers: BOTH override
    // atDamageGive and BOTH have a case below. Vigor
    // (com.megacrit.cardcrawl.powers.watcher.VigorPower:41-47) adds its amount;
    // PenNibPower (:51-57) doubles. Neither overrides atDamageReceive,
    // atDamageFinalReceive, modifyBlock or modifyBlockLast -- both classes were
    // read in full -- so those three switches take the count move and no case.
    // Checked for Regenerate Monster (PowerId::REGENERATE_MONSTER, id 91): its
    // only override is atEndOfTurn (RegenerateMonsterPower.java:37-43), read in
    // full -- no atDamage* or block hook at all, so this file's three
    // count-guard sites plus interp_block.cpp's one take the move together.
    // Checked for Duplication (DuplicationPower.java:19-72, read in full):
    // only updateDescription, onUseCard and atEndOfRound -- no atDamage* hook
    // anywhere -- so all three of this file's counts move again, caseless.
    static_assert(sts::registry::manifest::kPowersCount == 53,
                  "new power: does it override atDamageGive (attacker-side "
                  "damage scaling, as Strength and Weak do)? Add a case here if "
                  "so. Check atDamageFinalGive below in the same pass -- it is "
                  "still a pass-through because no power overrides it yet.");
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::STRENGTH:                        // StrengthPower.java:96
            return dmg + static_cast<float>(p.amount) *
                             static_cast<float>(strength_mult);
        case PowerId::VIGOR:
            // VigorPower.atDamageGive (VigorPower.java:41-47): `damage +=
            // (float)this.amount`. Priority 5 (the AbstractPower default -- the
            // ctor sets none), so the sort puts it in the same class as
            // Strength: both are additive, so their relative order is
            // exact-arithmetic-safe, but both must land BEFORE Pen Nib's
            // doubling, and the sort is what guarantees that.
            return dmg + static_cast<float>(p.amount);
        case PowerId::PEN_NIB:
            // PenNibPower.atDamageGive (PenNibPower.java:51-57): `damage * 2.0f`,
            // a FLAT double that never reads the slot's amount. Priority 6
            // (PenNibPower.java:36) puts it after every priority-5 addend and
            // before Frail(10)/Intangible(75)/Weak(99), so the game computes
            // ((base + Str + Vigor) * 2) * 0.75 under a Weak -- reproduced here
            // by slot ORDER (sort_powers_like_the_game, interp_powers.cpp),
            // not by a special case in this walk.
            return dmg * 2.0f;
        case PowerId::WEAK:
            return dmg * 0.75f;                         // WeakPower.java:67
        default:
            return dmg;
    }
}

// atDamageReceive: target-owned hooks. VulnerablePower.atDamageReceive: *1.5f,
// or *1.75f for a Vulnerable MONSTER when the player owns Paper Phrog
// (VulnerablePower.java:67-70 -- `!owner.isPlayer && player.hasRelic("Paper
// Frog")`; live, because Paper Phrog is a registered relic -- this is no longer
// the unreachable branch the relic-free skeleton documented). The player-side
// Odd Mushroom *1.25f branch (VulnerablePower.java:64-66) is live too, and is
// tested FIRST exactly as the Java tests it: `owner.isPlayer && hasRelic("Odd
// Mushroom")` precedes the Paper Frog test, and the two are mutually exclusive by
// the owner test anyway. Without either relic the 1.5f multiply is byte-identical
// to a relic-free hook (fixtures unchanged).
[[nodiscard]] float at_damage_receive(const CombatState& s, uint8_t owner_actor,
                                      float dmg, PowerSlot p) noexcept {
    // Checked for the Guardian's two powers: neither overrides atDamageReceive
    // (ModeShiftPower.java:27-30 / SharpHidePower.java:38-49). In particular
    // Mode Shift does NOT reduce incoming damage -- it only counts it
    // (TheGuardian.java:281-284), which is why it needs no hook at all.
    // Checked for the six red-rare powers: none overrides atDamageReceive
    // (Barricade / Berserk / Brutality / Demon Form / Double Tap / Juggernaut --
    // their only overrides are updateDescription plus one lifecycle hook each),
    // so none needed a case.
    // Checked for Confusion (Snecko Eye's ConfusionPower), which needs no
    // case: its ONLY override is onCardDraw (ConfusionPower.java:38-48).
    // Checked for the two monster powers added alongside the slavers and the
    // Fungi Beast, neither of which needs a case:
    // EntanglePower overrides only playApplyPowerSfx / updateDescription /
    // atEndOfTurn (EntanglePower.java:31-46) and SporeCloudPower only
    // updateDescription / onDeath (SporeCloudPower.java:28-42).
    // Checked for the Looter's Thievery, which needs no case: ThieveryPower's
    // ONLY override is updateDescription (ThieveryPower.java:27-30).
    // Checked for No Block: same answer as the pass above -- its only override
    // outside its own lifecycle is modifyBlockLast (NoBlockPower.java:58-60), on
    // the BLOCK path, so no case is needed here either.
    // Checked for Mayhem and Magnetism: same answer as the pass above -- neither
    // overrides any atDamage* hook (MayhemPower.java:28-39 /
    // MagnetismPower.java:30-49).
    // Checked for Panache and The Bomb: same answer as the pass above -- both
    // QUEUE damage and neither overrides any atDamage* hook
    // (PanachePower.java:40-67 / TheBombPower.java:40-53).
    // Checked for Regenerate Monster: same answer as the pass above -- its only
    // override is atEndOfTurn (RegenerateMonsterPower.java:37-43), read in full.
    // Duplication: no atDamage* override either (see at_damage_give's note).
    static_assert(sts::registry::manifest::kPowersCount == 53,
                  "new power: does it override atDamageReceive (target-side "
                  "damage scaling, as Vulnerable does)? Add a case here if so. "
                  "Check atDamageFinalReceive below in the same pass -- it is "
                  "NOT a pass-through: INTANGIBLE has a case there "
                  "(IntangiblePlayerPower.java:43-49). Its twin "
                  "atDamageFinalGive still is one.");
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::VULNERABLE:
            if (owner_actor == kActorPlayer &&
                player_has_relic(s, RelicId::ODD_MUSHROOM)) {
                return dmg * 1.25f;                     // VulnerablePower.java:65
            }
            if (owner_actor != kActorPlayer &&
                player_has_relic(s, RelicId::PAPER_PHROG)) {
                return dmg * 1.75f;                     // VulnerablePower.java:68
            }
            return dmg * 1.5f;                          // VulnerablePower.java:70
        default:
            return dmg;
    }
}

// atDamageFinalGive: no registered power overrides it (Strength/Vulnerable/Weak
// leave AbstractPower's identity default). Kept as an explicit call site so a
// future power that hooks the "final give" pass slots in without moving the
// pipeline.
[[nodiscard]] float at_damage_final_give(float dmg, PowerSlot /*p*/) noexcept {
    return dmg;
}
// atDamageFinalReceive: IntangiblePlayerPower caps incoming damage at 1
// (IntangiblePlayerPower.java:43-49 -- `if (damage > 1.0f) damage = 1.0f`, with
// NO DamageType test, but this pass only runs for NORMAL damage because THORNS
// and HP_LOSS skip DamageInfo.applyPowers entirely). The separate,
// type-agnostic guard in AbstractPlayer.damage (:1397-1399) is what catches
// those two; see op_damage below.
[[nodiscard]] float at_damage_final_receive(float dmg, PowerSlot p) noexcept {
    // Checked for the Guardian's two powers: neither overrides
    // atDamageFinalReceive (ModeShiftPower.java:27-30 / SharpHidePower.java:
    // 38-49). NOTE FOR THE NEXT READER: this is the THIRD kPowersCount site in
    // this file -- the other two are atDamageGive and atDamageReceive above. It is
    // the youngest of the three (it arrived with Intangible), so a branch cut
    // before Intangible landed will not conflict here and will leave this count
    // stale while fixing the other two. Update all three together.
    // Checked for the six red-rare powers: none overrides atDamageFinalReceive,
    // so none needed a case.
    // Checked for Confusion (Snecko Eye's ConfusionPower), which needs no
    // case: its ONLY override is onCardDraw (ConfusionPower.java:38-48).
    // Checked for the two monster powers added alongside the slavers and the
    // Fungi Beast, neither of which needs a case:
    // EntanglePower overrides only playApplyPowerSfx / updateDescription /
    // atEndOfTurn (EntanglePower.java:31-46) and SporeCloudPower only
    // updateDescription / onDeath (SporeCloudPower.java:28-42).
    // Checked for the Looter's Thievery, which needs no case: ThieveryPower's
    // ONLY override is updateDescription (ThieveryPower.java:27-30).
    // Checked for No Block: it overrides no atDamage* hook at all (its whole
    // damage-side surface is empty -- NoBlockPower.java:39-60), so no case here.
    // Checked for Mayhem and Magnetism: neither overrides atDamageFinalReceive
    // (MayhemPower.java:28-39 / MagnetismPower.java:30-49). All three of this
    // file's counts moved together, as the note above requires.
    // Checked for Panache and The Bomb: neither overrides atDamageFinalReceive
    // (PanachePower.java:40-67 / TheBombPower.java:40-53). All three of this
    // file's counts moved together again.
    // Checked for Regenerate Monster: neither overrides atDamageFinalReceive
    // (RegenerateMonsterPower.java:37-43, read in full). All three of this
    // file's counts moved together with interp_block.cpp's, again.
    // file's counts moved together again -- and again for Duplication (see
    // at_damage_give's note).
    static_assert(sts::registry::manifest::kPowersCount == 53,
                  "new power: does it override atDamageFinalReceive (the last "
                  "target-side pass, as Intangible does)? Add a case here if so.");
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::INTANGIBLE:
            return dmg > 1.0f ? 1.0f : dmg;   // IntangiblePlayerPower.java:44-48
        default:
            return dmg;
    }
}

// Player stance hooks. The skeleton is stanceless (design doc §4.2: stance
// field is a "0 = None" placeholder), so both are documented identity stubs --
// written as real call sites so a future stance system attaches here without
// touching the pipeline shape.
[[nodiscard]] float stance_at_damage_give(const CombatState& /*s*/, float dmg) noexcept {
    return dmg;  // AbstractStance.atDamageGive default (no stance active)
}
[[nodiscard]] float stance_at_damage_receive(const CombatState& /*s*/, float dmg) noexcept {
    return dmg;  // AbstractStance.atDamageReceive default (no stance active)
}

void cards_took_player_damage(CombatState& s) noexcept;

[[nodiscard]] bool actor_has_power(const CombatState& s, uint8_t actor,
                                   PowerId id) noexcept {
    const PowerView pv = actor_powers(s, actor);
    for (uint8_t i = 0; i < pv.count; ++i) {
        if (pv.slots[i].power_id == static_cast<uint16_t>(id) &&
            pv.slots[i].amount > 0) {
            return true;
        }
    }
    return false;
}

// --- Relic/power modifiers on the RECEIVE path (AbstractPlayer.damage) -------
//
// AbstractPlayer.damage (AbstractPlayer.java:1387-1512) is one long ordered
// chain, and these helpers are its steps in that order. LoseHPAction routes
// through the SAME method with DamageType.HP_LOSS (LoseHPAction.java:41), which
// is why op_lose_hp calls them too; only decrementBlock and the NORMAL-only
// onAttacked fan-out differ between the two entry points.

// Step 1 (AbstractPlayer.java:1397-1399), BEFORE decrementBlock:
//     if (damageAmount > 1 && this.hasPower("IntangiblePlayer")) damageAmount = 1;
// This guard is type-agnostic, so it also caps THORNS and HP_LOSS -- the damage
// types that never reach IntangiblePlayerPower.atDamageFinalReceive because they
// skip DamageInfo.applyPowers. It is on AbstractPlayer only (monsters carry a
// different IntangiblePower).
[[nodiscard]] int intangible_cap(const CombatState& s, uint8_t tgt,
                                 int dmg) noexcept {
    if (tgt == kActorPlayer && dmg > 1 &&
        actor_has_power(s, kActorPlayer, PowerId::INTANGIBLE)) {
        return 1;
    }
    return dmg;
}

// Step 2 (AbstractPlayer.java:1412-1415), after decrementBlock: the VICTIM's
// powers' onAttackedToChangeDamage. BufferPower.onAttackedToChangeDamage
// (BufferPower.java:44-47) returns 0 unconditionally and, when the incoming
// amount was positive, addToTop ReducePowerAction(owner, owner, "Buffer", 1) --
// so a stack is spent only for a hit that would otherwise have landed, and the
// return is 0 either way (identical when the amount is already 0).
[[nodiscard]] int apply_buffer(CombatState& s, uint8_t tgt, int dmg) noexcept {
    if (!actor_has_power(s, tgt, PowerId::BUFFER)) {
        return dmg;
    }
    if (dmg > 0) {
        ActionQueueItem reduce{};
        reduce.opcode = static_cast<uint16_t>(Opcode::REDUCE_POWER);
        reduce.src = tgt;
        reduce.tgt = tgt;
        reduce.amount = 1;
        reduce.flags = make_apply_power_flags(PowerId::BUFFER);
        add_to_top(s, reduce);
    }
    return 0;
}

// The PLAYER-AS-ATTACKER relic pass, AbstractMonster.damage:639-643 (and its
// twin AbstractPlayer.damage:1399-1403 for a player hitting itself). Boot is the
// only relic in the game that overrides it.
//
// PLACEMENT IS THE WHOLE FINDING, and the deferral note this replaces had it
// backwards. `onAttackToChangeDamage` reads like an attacker-side pre-block
// modifier, and it is NOT: both call sites run `damageAmount =
// this.decrementBlock(info, damageAmount);` FIRST and only then walk the
// player's relics. So the number Boot sees is the UNBLOCKED RESIDUE, and a
// 4-damage hit into 2 block deals 5, not 4 -- while a 3-damage hit fully soaked
// by 3 block deals 0, because `damageAmount > 0` fails. That is also what the
// relic's own text says ("unblocked attack damage"), and it is why the
// pre-block insertion point would have been wrong in the direction that inflates
// every partially-blocked hit.
//
// Ordering against the other two integer-tail modifiers, straight off the two
// Java methods: onAttackToChangeDamage (here) precedes the victim's
// onAttackedToChangeDamage (Buffer, :646/:1405-1407), which precedes onAttacked
// (Thorns, and Torii on the player side, :1425-1432), which precedes
// onLoseHpLast (Tungsten Rod, :1433-1435). Boot is therefore the FIRST of the
// four and Torii/Tungsten Rod stay last -- they are victim-side and Boot is
// attacker-side, so on the player's own turn they never even see the same hit.
//
// Boot.onAttackToChangeDamage (Boot.java:30-38):
//     if (info.owner != null && info.type != HP_LOSS && info.type != THORNS
//         && damageAmount > 0 && damageAmount < 5) { flash();
//         addToBot(RelicAboveCreatureAction); return 5; }
// THRESHOLD = 5 (:19); the description's "4" (:27) is display text.
//
// `info.owner != null` needs no expression: every hit this engine produces has an
// actor. The gate that DOES matter is the enclosing `if (info.owner ==
// AbstractDungeon.player)` at the call site -- Boot fires only when the PLAYER is
// the attacker, which is `src == kActorPlayer` here. The queued
// RelicAboveCreatureAction is cosmetic and deliberately dropped, but note that
// the Java hook does touch the action queue from inside a damage computation.
//
// RelicHook::ON_ATTACK (value 12) stays allocated and unfired: it has no
// dispatcher, and RelicHookContext carries no damage in/out channel, so building
// one for a single relic would be a much larger change than this. The row's
// `on_attack: []` binding is documentation of WHICH Java hook this bespoke site
// reproduces -- the Magic Flower / Torii / Tungsten Rod precedent.
[[nodiscard]] int apply_boot(const CombatState& s, uint8_t src, int dmg,
                             DamageType type) noexcept {
    if (src == kActorPlayer && type != DamageType::HP_LOSS &&
        type != DamageType::THORNS && dmg > 0 && dmg < 5 &&
        player_has_relic(s, RelicId::BOOT)) {
        return 5;
    }
    return dmg;
}

// Step 3 (AbstractPlayer.java:1430-1432): the player's relics' onAttacked, run
// AFTER the victim powers' onAttacked (Thorns / Flame Barrier). Torii.onAttacked
// (Torii.java:31-38) turns a NORMAL, non-THORNS, non-HP_LOSS hit of 2..5
// into 1. `info.owner != null` holds for every hit this engine produces.
[[nodiscard]] int apply_torii(const CombatState& s, uint8_t tgt, int dmg,
                              DamageType type) noexcept {
    if (tgt == kActorPlayer && type == DamageType::NORMAL && dmg > 1 &&
        dmg <= 5 && player_has_relic(s, RelicId::TORII)) {
        return 1;
    }
    return dmg;
}

// Step 4 (AbstractPlayer.java:1433-1435): the player's relics' onLoseHpLast --
// the LAST modifier before the `if (damageAmount > 0)` block, so a 1-damage hit
// reduced to 0 here fires no wasHPLost at all. TungstenRod.onLoseHpLast
// (TungstenRod.java:26-32): positive damage becomes damage - 1.
[[nodiscard]] int apply_tungsten_rod(const CombatState& s, uint8_t tgt,
                                     int dmg) noexcept {
    if (tgt == kActorPlayer && dmg > 0 &&
        player_has_relic(s, RelicId::TUNGSTEN_ROD)) {
        return dmg - 1;
    }
    return dmg;
}

// Step 5 (AbstractPlayer.java:1482-1497): once the HP write has taken the player
// below 1, a revive source may fire INSTEAD of dying -- currentHealth is pinned
// at 0, the source heals, and damage() RETURNS so the death branch never runs.
// There are TWO sources, and the Java's structure decides between them:
//
//     if (!hasRelic("Mark of the Bloom")) {
//         if (hasPotion("FairyPotion")) { ...consume ONE fairy...; return; }
//         else if (hasRelic("Lizard Tail") && counter == -1) { ...; return; }
//     }
//
// So Mark of the Bloom BLOCKS BOTH, and a held Fairy takes precedence: because
// the Lizard Tail arm is an `else if` on `hasPotion`, holding a Fairy means the
// Lizard Tail is not even CONSULTED, let alone spent. Mark of the Bloom is an
// Act-2+ boss relic with no S1 row, so its test is genuinely constant-true here.
// FAIRY_POTION, however, DOES have a row (PotionId 31) -- an earlier version of
// this comment said it did not, which is what kept the fairy arm from existing.
//
// The FAIRY half reads CombatState.flags' armed count, mirrored in at combat
// entry from the run's belt: see kCombatFlagFairyArmedShift (combat_state.hpp)
// for why the belt itself cannot be consulted from here and why a post-hoc
// run-layer revive would be observably wrong. Consuming decrements the mirror;
// the run layer burns the real slots at fold-back by comparing the belt against
// what is left. Exactly ONE fairy is consumed per lethal event -- the Java loop
// `return`s on the first match -- so a second held Fairy survives for later.
//
// FairyPotion.use (FairyPotion.java:36-45):
//     float percent = this.potency / 100.0f;
//     int healAmt = (int)((float)player.maxHealth * percent);
//     if (healAmt < 1) healAmt = 1;
//     player.heal(healAmt, true);
// The MIN-1 clamp is on the HEAL, not on the result, and HP is pinned to 0
// BEFORE the heal, so the outcome is exactly healAmt (clamped to maxHealth by
// heal, AbstractCreature.java:386-395) and never hp + healAmt. Routed through
// the shared in-combat heal seam so Magic Flower's x1.5 applies: its
// onPlayerHeal is `phase == COMBAT`-gated (MagicFlower.java:30-37) and this site
// IS combat. Sacred Bark doubles potency 30 -> 60, i.e. a revive at 60% of max
// HP; the relic has no engine hook, so def->potency is what arrives.
//
// LizardTail.onTrigger heals maxHealth/2 (min 1) and sets the counter to -2
// (LizardTail.java:28-45).
void try_player_revive(CombatState& s) noexcept {
    if (s.player_hp > 0) {
        return;
    }
    const uint8_t fairies = combat_fairy_armed(s.flags);
    if (fairies > 0) {
        s.flags = with_combat_fairy_armed(s.flags,
                                          static_cast<uint8_t>(fairies - 1));
        s.player_hp = 0;  // this.currentHealth = 0, BEFORE the heal
        const PotionDef* def = potion_def(PotionId::FAIRY_POTION);
        const int potency = def == nullptr ? 0 : def->potency;
        const float percent = static_cast<float>(potency) / 100.0f;
        int heal =
            static_cast<int>(static_cast<float>(s.player_max_hp) * percent);
        if (heal < 1) {
            heal = 1;  // FairyPotion.java:40-42 -- on the HEAL, not the result
        }
        heal_player_with_relics(s, heal);
        return;  // the `else if`: a Lizard Tail is not consulted at all
    }
    for (uint8_t i = 0; i < s.relic_count; ++i) {
        RelicSlot& slot = s.relics[i];
        if (slot.relic_id != static_cast<uint16_t>(RelicId::LIZARD_TAIL) ||
            slot.counter != -1) {
            continue;
        }
        slot.counter = -2;  // setCounter(-2) == usedUp
        int amount = s.player_max_hp / 2;
        if (amount < 1) {
            amount = 1;
        }
        heal_player_with_relics(s, amount);
        return;
    }
}

// Blood for Blood.tookDamage: after each positive in-combat player HP-loss
// EVENT, updateCost(-1) on every copy in hand/discard/draw (but not exhaust or
// limbo/cardInUse). The reduction is per event, not per HP point, and clamps at
// zero. AbstractPlayer.updateCardsOnDamage (AbstractPlayer.java:1518-1530).
void cards_took_player_damage(CombatState& s) noexcept {
    auto update = [&](const CardPoolIndex* pile, uint8_t count) noexcept {
        for (uint8_t i = 0; i < count; ++i) {
            CardInstance& c = s.card_pool[pile[i]];
            if (c.card_id == static_cast<uint16_t>(CardId::BLOOD_FOR_BLOOD) &&
                c.cost_now > 0) {
                --c.cost_now;
            }
        }
    };
    update(s.hand, s.hand_count);
    update(s.discard, s.discard_count);
    update(s.draw, s.draw_count);
}

}  // namespace

// --- Opcode bodies ----------------------------------------------------------

// DAMAGE: compute output via the pipeline, then land it on tgt -- block absorbs
// first (decrementBlock: block soaks up to its value), remainder hits hp,
// currentHealth clamped >= 0. (Death/onDeath handling is not yet modeled; the
// pump's hp<=0 check drives the COMBAT_OVER transition.)
void op_damage(CombatState& s, uint8_t src, uint8_t tgt, int base,
               int strength_mult,
               DamageType type, bool pure, bool source_null) noexcept {
    if (tgt != kActorPlayer && tgt >= kMonsterCap) {
        return;
    }
    // THORNS / HP_LOSS damage skips the NORMAL-only power pipeline (every skeleton
    // applyPowers hook is `if (type == NORMAL)` in the Java): a Vulnerable attacker
    // does NOT amplify reflected Thorns, and player Strength/Weak do not scale it.
    // A PURE item (kDamagePure -- createDamageMatrix(amount, true), DamageInfo.
    // java:126-136) skips the pipeline too, whatever its type: applyPowers is
    // never called on it, so no float op runs at all (the surviving NORMAL
    // non-pure path is untouched -- FP contract preserved by omission, not
    // reordering). NORMAL non-pure damage runs the full DamageInfo.applyPowers
    // pipeline, carrying the Strength multiplier (Heavy-Blade-style attacks).
    const int out = (type == DamageType::NORMAL && !pure)
                        ? compute_damage(s, src, tgt, base, strength_mult)
                        : (base < 0 ? 0 : base);
    int16_t* hp = actor_hp(s, tgt);
    int16_t* blk = actor_block(s, tgt);
    if (hp == nullptr || blk == nullptr) {
        return;
    }
    // AbstractPlayer.damage:1397-1399 -- the Intangible cap runs BEFORE
    // decrementBlock, so a capped hit is soaked by 1 block rather than by its
    // full pre-cap size.
    int dmg = intangible_cap(s, tgt, out);
    int block = *blk;
    const int old_block = block;
    if (dmg >= block) {
        dmg -= block;
        block = 0;
    } else {
        block -= dmg;
        dmg = 0;
    }
    *blk = static_cast<int16_t>(block);
    // AbstractCreature.decrementBlock -> brokeBlock (AbstractCreature.java:
    // 159-183): the player's relics' onBlockBroken fan-out fires ONLY when the
    // creature whose block just hit zero is an AbstractMonster, and only when
    // that block was positive and the incoming damage was >= it. Hand Drill.
    if (tgt != kActorPlayer && old_block > 0 && block == 0) {
        const RelicView rv = player_relics(s);
        dispatch_relics_on_block_broken(s, rv.relics, rv.count, tgt);
    }
    // The PLAYER-AS-ATTACKER relic pass, immediately after decrementBlock and
    // BEFORE the victim's onAttackedToChangeDamage (AbstractMonster.java:639-643
    // / AbstractPlayer.java:1399-1403). The Boot -- see apply_boot for why this
    // is a post-block site, which is the opposite of what the hook's name
    // suggests. A null-source hit skips it: the call-site gate is
    // `info.owner == AbstractDungeon.player` (AbstractMonster.java:639), which
    // a null owner fails -- src still reads kActorPlayer for a null-source item
    // (the queue slot has no null encoding), so the bit is what carries it.
    if (!source_null) {
        dmg = apply_boot(s, src, dmg, type);
    }
    // The victim's powers' onAttackedToChangeDamage (AbstractPlayer.java:
    // 1412-1415), between decrementBlock and the onAttacked fan-out. Buffer --
    // whose Java body has NO owner test (BufferPower.java:41-47), so a
    // null-source hit still spends it.
    dmg = apply_buffer(s, tgt, dmg);
    // onAttacked (AbstractPlayer.damage:1425-1426): the VICTIM's powers fire on a
    // NORMAL attack from a DISTINCT attacker -- AFTER decrementBlock and REGARDLESS
    // of whether damage penetrated (Thorns reflects even a fully-blocked hit). A
    // THORNS/HP_LOSS incoming does NOT trigger onAttacked (ThornsPower's own type
    // guard), so it is dispatched only for NORMAL damage with src != tgt. A
    // NULL-SOURCE NORMAL hit still dispatches -- the game's loop runs
    // unconditionally and the `info.owner != null` gates live in the power
    // BODIES, which read HookContext::source_null -- so a future
    // owner-INsensitive body fires exactly as in the game. No-op unless a
    // power binds ON_ATTACKED, so skeleton/relic-free DAMAGE is unchanged.
    if (type == DamageType::NORMAL && src != tgt) {
        dispatch_on_attacked(s, tgt, src, dmg, source_null);
    }
    // The player's relics' onAttacked, then onLoseHpLast -- the last two
    // modifiers before the HP write (AbstractPlayer.java:1430-1435). Torii's
    // own gate includes `info.owner != null` (Torii.java:32); Tungsten Rod's
    // onLoseHpLast has no owner test (TungstenRod.java:26-32).
    if (!source_null) {
        dmg = apply_torii(s, tgt, dmg, type);
    }
    dmg = apply_tungsten_rod(s, tgt, dmg);
    const int old_hp = *hp;
    int new_hp = old_hp - dmg;
    if (new_hp < 0) {
        new_hp = 0;
    }
    *hp = static_cast<int16_t>(new_hp);
    // wasHPLost (AbstractPlayer.damage:1445-1447): fires on the VICTIM's powers
    // for the HP actually lost, with the ATTACKER as source. Rupture's guard
    // (source == victim) means unblocked enemy damage does NOT grant Strength --
    // only self-inflicted (card) HP loss does; Plated Armor's guard also reads the
    // damage `type` (it does not decrement on THORNS/HP_LOSS) AND
    // `info.owner != null` (PlatedArmorPower.java:58), which is why the
    // null-source bit rides along. No-op without those, so skeleton DAMAGE is
    // unchanged.
    const int hp_lost = old_hp - new_hp;
    dispatch_was_hp_lost(s, tgt, src, hp_lost, static_cast<uint8_t>(type),
                         source_null);
    if (tgt == kActorPlayer && hp_lost > 0) {
        cards_took_player_damage(s);
    }
    if (tgt == kActorPlayer) {
        try_player_revive(s);  // AbstractPlayer.java:1482-1497
    }
    // Monster death edge -> the dying monster's OWN powers' onDeath, then relics
    // onMonsterDeath (AbstractMonster.die:925-937; Spore Cloud and Gremlin Horn).
    // Fires once, when this hit drops the monster from positive HP to 0. The two
    // loops run in die()'s order -- powers first (:928-932), relics second
    // (:933-937) -- and both AFTER the HP write, so a power that asks "is the
    // battle ending?" sees this monster as already dying, exactly as the Java's
    // `isDying = true` at :927 arranges. Runs BEFORE the damage() override seam
    // below: die() is called synchronously inside super.damage(), while the
    // override's post-super check sees isDying and never split-telegraphs a
    // lethal hit. No-op with an empty relic mirror and no ON_DEATH power
    // (fixtures unchanged).
    if (tgt != kActorPlayer && old_hp > 0 && new_hp == 0) {
        dispatch_on_death(s, tgt);
        const RelicView rv = player_relics(s);
        dispatch_relics_on_monster_death(s, rv.relics, rv.count, tgt);
    }
    // Monster damage() override seam: the large slimes' split interrupt
    // wraps super.damage() and runs AFTER it, for EVERY DamageInfo type
    // (AcidSlime_L.java:142-152 / SpikeSlime_L.java:130-140 -- the guard reads
    // only resulting state, so a fully-blocked hit still checks). Dispatched by
    // monster_id; no-op for monsters without an override. hp_lost is what
    // Lagavulin's `currentHealth != previousHealth` wake test reads, so a hit
    // its block fully absorbed passes 0 here.
    if (tgt != kActorPlayer) {
        on_monster_damaged(s, tgt, hp_lost);
    }
}

// LOSE_HP: `tgt` loses `amount` HP directly, bypassing block (LoseHPAction /
// DamageInfo.HP_LOSS). Fires wasHPLost with source == tgt (SELF) -- the card /
// self HP-loss path that Rupture attributes to (Hemokinesis, Offering, Combust).
void op_lose_hp(CombatState& s, uint8_t tgt, int amount) noexcept {
    if (amount <= 0) {
        return;
    }
    int16_t* hp = actor_hp(s, tgt);
    if (hp == nullptr) {
        return;
    }
    // LoseHPAction routes through creature.damage() with DamageType.HP_LOSS
    // (LoseHPAction.java:41), so the player's receive chain runs here too -- minus
    // decrementBlock (HP_LOSS skips it, AbstractCreature.java:170) and minus
    // Torii (its own body excludes HP_LOSS). Every one of these is a no-op
    // without the relic/power that owns it, so the existing self-damage cards
    // are byte-unchanged.
    int dmg = intangible_cap(s, tgt, amount);   // AbstractPlayer.java:1397-1399
    dmg = apply_buffer(s, tgt, dmg);            // AbstractPlayer.java:1412-1415
    dmg = apply_tungsten_rod(s, tgt, dmg);      // AbstractPlayer.java:1433-1435
    const int old_hp = *hp;
    int new_hp = old_hp - dmg;
    if (new_hp < 0) {
        new_hp = 0;
    }
    *hp = static_cast<int16_t>(new_hp);
    // source == victim (self), HP_LOSS type (bypasses block; not a THORNS/NORMAL
    // attack -- so it drives Rupture but not Thorns/Plated Armor's attack guards).
    const int hp_lost = old_hp - new_hp;
    dispatch_was_hp_lost(s, tgt, tgt, hp_lost,
                         static_cast<uint8_t>(DamageType::HP_LOSS));
    if (tgt == kActorPlayer && hp_lost > 0) {
        cards_took_player_damage(s);
    }
    if (tgt == kActorPlayer) {
        try_player_revive(s);  // AbstractPlayer.java:1482-1497
    }
    // A direct HP loss can also kill a monster -> the same die() power-then-relic
    // dispatch (AbstractMonster.die:925-937), before the override seam as above.
    if (tgt != kActorPlayer && old_hp > 0 && new_hp == 0) {
        dispatch_on_death(s, tgt);
        const RelicView rv = player_relics(s);
        dispatch_relics_on_monster_death(s, rv.relics, rv.count, tgt);
    }
    // LoseHPAction also routes through creature.damage() (LoseHPAction.java:41),
    // so the monster damage() override seam fires here too. LOSE_HP bypasses
    // block, so hp_lost is the whole amount unless the monster died first.
    if (tgt != kActorPlayer) {
        on_monster_damaged(s, tgt, hp_lost);
    }
}

// DAMAGE_FEED (FeedAction.update, FeedAction.java:34-47): the ordinary damage
// pipeline, then -- ONLY if the hit LEFT the target dead -- the player's
// increaseMaxHp(amount, false). The Java condition is
//     !(!isDying && currentHealth > 0 || halfDead || hasPower("Minion"))
// i.e. gain when the target is dying-or-at-zero and is neither halfDead nor a
// Minion. Neither halfDead nor MinionPower has an S1 Act-1 producer (no monster
// sets halfDead and there is no Minion power row), so those two terms are
// constant-false here and the test is "did this hit take it to zero".
// increaseMaxHp (AbstractCreature.java:199-208) is maxHealth += amount FOLLOWED BY
// heal(amount, true), so the heal runs the onPlayerHeal relic pass -- it goes
// through heal_player_with_relics, never a raw HP write. The heal is SYNCHRONOUS
// (a direct heal() call, not a queued HealAction), unlike Reaper's.
void op_damage_feed(CombatState& s, uint8_t src, uint8_t tgt, int base,
                    int max_hp_gain) noexcept {
    if (tgt >= kMonsterCap) {
        return;
    }
    op_damage(s, src, tgt, base);
    if (s.monsters[tgt].hp > 0 || max_hp_gain <= 0) {
        return;
    }
    s.player_max_hp = static_cast<int16_t>(s.player_max_hp + max_hp_gain);
    heal_player_with_relics(s, max_hp_gain);
}

// DAMAGE_GREED (GreedAction.update, GreedAction.java:33-46): the ORDINARY damage
// pipeline -- HandOfGreed.use builds a plain DamageInfo(p, damage,
// damageTypeForTurn) (HandOfGreed.java:33) and the action just calls
// target.damage(info) (:36), so Strength/Vulnerable/Weak all apply and block
// absorbs, unlike the pure-damage matrices Panache and The Bomb queue -- and
// then, only if the hit left the target dead, `gold` into the run's purse.
//
// The Java gate (:37), read literally, is
//     !(!isDying && currentHealth > 0 || halfDead || hasPower("Minion"))
// i.e. pay out when the target is dying-or-at-zero AND is neither halfDead nor a
// Minion. Both of those exclusions are STRUCTURALLY constant-false in S1 and are
// recorded here rather than modelled as state that nothing can set:
//   * halfDead -- no Act-1 monster is half-dead-capable (the game's only
//     producers are Awakened One and the Darklings, both out of scope), which is
//     exactly the reading combat_state.hpp's liveness-predicate comment already
//     records for every other halfDead term in the engine. There is no halfDead
//     field to consult, and inventing one would be state with no writer.
//   * hasPower("Minion") -- registry/powers.yaml has NO Minion row (searched
//     before writing this, not assumed), so no creature in the registry can carry
//     it. When a Minion power lands, this is the site that must gain the test.
// So the live test is "did this hit take the target to zero", which is the same
// shape DAMAGE_FEED (op_damage_feed above) already uses for the identical Java
// condition, with the same two terms inert for the same reasons.
//
// gainGold itself is NOT called here: the combat layer has no purse. The gold
// accrues in CombatState.combat_gold and the run layer settles the total through
// the single gain_gold door at the combat fold-back (run_advance.cpp
// fold_back_combat) -- so Ectoplasm's suppression and the onGainGold relic seam
// still see it exactly once, and a combat-only replay simply carries the number.
void op_damage_greed(CombatState& s, uint8_t src, uint8_t tgt, int base,
                     int gold) noexcept {
    if (tgt >= kMonsterCap) {
        return;
    }
    op_damage(s, src, tgt, base);
    if (s.monsters[tgt].hp > 0 || gold <= 0) {
        return;
    }
    int32_t total = static_cast<int32_t>(s.combat_gold) + gold;
    if (total > 0xFFFF) {
        total = 0xFFFF;  // saturate rather than wrap (unreachable in S1: 7
                         // monster records x 25 gold is 175)
    }
    s.combat_gold = static_cast<uint16_t>(total);
}

// VAMPIRE_DAMAGE_ALL (VampireDamageAllEnemiesAction.update, :53-77): damage every
// monster that is not dying/escaping, in GROUP (slot) order, accumulating each
// target's lastDamageTaken -- min(post-block damage, its HP before the hit),
// AbstractMonster.java:669, which is exactly the HP it lost. When the sum is
// positive the action addToBot's HealAction(player, sum), so the heal is QUEUED
// and resolves after anything the hits themselves queued at the front.
void op_vampire_damage_all(CombatState& s, int base) noexcept {
    int healed = 0;
    for (uint8_t i = 0; i < s.monster_count; ++i) {
        if (monster_dead_or_escaped(s.monsters[i])) {
            continue;  // isDying || currentHealth <= 0 || isEscaping (:60)
        }
        const int16_t before = s.monsters[i].hp;
        op_damage(s, kActorPlayer, i, base);
        healed += before - s.monsters[i].hp;
    }
    if (healed <= 0) {
        return;
    }
    ActionQueueItem heal{};
    heal.opcode = static_cast<uint16_t>(Opcode::HEAL);
    heal.src = kActorPlayer;
    heal.tgt = kActorPlayer;
    heal.amount = healed;
    add_to_bottom(s, heal);  // addToBot HealAction(source, source, healAmount) (:72)
}

// HEAL (HealAction.update, HealAction.java:30-34 -> AbstractCreature.heal): the
// player's heal, routed through heal_player_with_relics so the onPlayerHeal relic
// pass (Magic Flower) applies and the result is clamped to max HP. A monster
// target is a no-op HERE -- the ONE S1 effect that heals a monster,
// RegenerateMonsterPower (PowerId::REGENERATE_MONSTER, id 91), does not come
// through this opcode at all: it is native and writes the monster's HP
// directly at end of turn (power_regenerate_monster.cpp), the same escape
// hatch the player's own REGEN (id 18) uses. So this op_heal no-op is not a
// dead branch to "fix" if a monster-heal need ever shows up here -- it means
// that need is data-program-shaped and REGENERATE_MONSTER's native precedent
// is not it.
void op_heal(CombatState& s, uint8_t tgt, int amount) noexcept {
    if (tgt != kActorPlayer || amount <= 0) {
        return;
    }
    heal_player_with_relics(s, amount);
}

// DropkickAction.update: test Vulnerable when the action resolves. Damage is
// first; if the condition was true, GainEnergyAction then DrawCardAction follow.
void op_dropkick(CombatState& s, const ActionQueueItem& item) noexcept {
    const bool vulnerable = actor_has_power(s, item.tgt, PowerId::VULNERABLE);
    op_damage(s, item.src, item.tgt, item.amount);
    if (!vulnerable) {
        return;
    }
    ActionQueueItem energy{};
    energy.opcode = static_cast<uint16_t>(Opcode::GAIN_ENERGY);
    energy.src = kActorPlayer;
    energy.tgt = kActorPlayer;
    energy.amount = 1;
    add_to_bottom(s, energy);
    ActionQueueItem draw{};
    draw.opcode = static_cast<uint16_t>(Opcode::DRAW);
    draw.src = kActorPlayer;
    draw.tgt = kActorPlayer;
    draw.amount = 1;
    add_to_bottom(s, draw);
}

// --- Public: DAMAGE pipeline -------------------------------------------------

int compute_damage(const CombatState& s, uint8_t src, uint8_t tgt,
                   int base) noexcept {
    return compute_damage(s, src, tgt, base, /*strength_mult=*/1);
}

int compute_damage(const CombatState& s, uint8_t src, uint8_t tgt, int base,
                   int strength_mult) noexcept {
    const PowerView owner = actor_powers(s, src);
    const PowerView target = actor_powers(s, tgt);
    float tmp = static_cast<float>(base);

    if (src != kActorPlayer) {
        // Monster-owned attack (DamageInfo.java:39-70). Target is (for the
        // skeleton) the player, so the stance hook is atDamageReceive and sits
        // AFTER the target's atDamageReceive loop. (strength_mult is only ever != 1
        // for the player's Heavy Blade, so it is a no-op on this branch.)
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_give(tmp, owner.slots[i], strength_mult);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(s, tgt, tmp, target.slots[i]);
        }
        tmp = stance_at_damage_receive(s, tmp);
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_final_give(tmp, owner.slots[i]);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_final_receive(tmp, target.slots[i]);
        }
    } else {
        // Player-owned attack (DamageInfo.java:71-99). Owner is the player, so
        // the stance hook is atDamageGive and sits AFTER the owner's
        // atDamageGive loop, BEFORE the target's atDamageReceive loop.
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_give(tmp, owner.slots[i], strength_mult);
        }
        tmp = stance_at_damage_give(s, tmp);
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_receive(s, tgt, tmp, target.slots[i]);
        }
        for (uint8_t i = 0; i < owner.count; ++i) {
            tmp = at_damage_final_give(tmp, owner.slots[i]);
        }
        for (uint8_t i = 0; i < target.count; ++i) {
            tmp = at_damage_final_receive(tmp, target.slots[i]);
        }
    }

    int out = mathutils_floor(tmp);  // one floor, at the very end (trap 1)
    if (out < 0) {
        out = 0;
    }
    return out;
}

}  // namespace sts::engine
