// Potion USE mechanics -- see potions.hpp for the full provenance, the
// layer-boundary seam (slot storage/count is RunState, not here), and the
// data-program-vs-native convention.
//
// Provenance: AbstractPotion.use / each potion class (cited per row in
// registry/potions.yaml); AbstractDungeon.returnRandomPotion
// (AbstractDungeon.java:829-850); design doc §5.4, §6, §10 trap 14.

#include "sts/engine/potions.hpp"

#include <cstdint>

#include "sts/engine/action_queue.hpp"  // add_to_bottom, kActor* sentinels
#include "sts/engine/cards.hpp"         // CardEffectStep
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode
#include "sts/engine/relic_hooks.hpp"   // heal_player_with_relics (Magic Flower)
#include "sts/engine/rng_stream.hpp"    // random (potionRng draws)
#include "sts/engine/types.hpp"         // PotionId
#include "sts/registry/manifest.hpp"    // generated kPotionsCount

namespace sts::engine {

namespace {

// Instantiate one registry USE step into an ActionQueueItem and queue it via
// add_to_bottom -- the identical translation card_play.cpp uses for a card's
// use() (a potion's use() is likewise a sequence of addToBot(...) calls). The
// potion is player-owned, so src is the player; tgt is substituted per the
// step's symbolic target (SELF -> player, CARD_TARGET -> the used-on monster,
// ALL_ENEMY/RANDOM_ENEMY -> the execute-time fan-out sentinels).
void queue_use_step(CombatState& s, const CardEffectStep& step,
                    uint8_t target) noexcept {
    ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(step.op);
    item.src = kActorPlayer;
    switch (step.target) {
        case StepTarget::SELF:
            item.tgt = kActorPlayer;
            break;
        case StepTarget::CARD_TARGET:
            item.tgt = target;
            break;
        case StepTarget::ALL_ENEMY:
            item.tgt = kActorAllEnemies;
            break;
        case StepTarget::RANDOM_ENEMY:
            item.tgt = kActorRandomEnemy;
            break;
        default:
            item.tgt = kActorPlayer;
            break;
    }
    item.amount = step.amount;
    item.flags = step.extra;  // APPLY_POWER: PowerId flags; else 0
    if (step.op == static_cast<decltype(step.op)>(Opcode::BLOCK)) {  // registry mirror
        // A potion's block is a direct GainBlockAction (Block Potion), not card
        // applyPowers, so it does NOT get Dexterity -- flag op_block to skip it.
        item.flags |= kBlockNoPowers;
    }
    add_to_bottom(s, item);
}

}  // namespace

// --- Public ------------------------------------------------------------------

bool potion_use_implemented(PotionId id) noexcept {
    const PotionDef* def = potion_def(id);
    if (def == nullptr) {
        return false;
    }
    if (!def->native) {
        // Registry-driven and self-healing: a data effect program always runs,
        // so un-deferring a potion in registry/potions.yaml is enough.
        return true;
    }
    // The `native` rows, i.e. the ones that mean hand-written code. KEEP THIS
    // LIST NEXT TO THE BODIES IT DESCRIBES -- dispatch_native_potion's switch is
    // directly below, and the run-layer three are named with their site.
    static_assert(sts::registry::manifest::kPotionsCount == 33,
                  "new potion: if its row is native:true it must be listed here "
                  "AND have a body (dispatch_native_potion below, or the run "
                  "layer). Falling through to the default means use_potion "
                  "silently refuses it.");
    switch (id) {
        case PotionId::BLOOD_POTION:           // dispatch_native_potion, below
        case PotionId::BLESSING_OF_THE_FORGE:  // dispatch_native_potion, below
        case PotionId::SMOKE_BOMB:             // dispatch_native_potion, below
                                               // (run_advance's step_potion
                                               // still intercepts it first)
        case PotionId::FRUIT_JUICE:            // run layer: use_fruit_juice
        case PotionId::ENTROPIC_BREW:          // run layer: use_entropic_brew
            return true;
        default:
            // DEFERRED: no body anywhere. FAIRY_POTION lands here too, which is
            // correct -- it is never USED (AbstractPotion.isThrown false, it
            // triggers on death), and combat_potion_legal rejects it by name.
            return false;
    }
}

bool use_potion(CombatState& s, PotionId id, uint8_t target) noexcept {
    const PotionDef* def = potion_def(id);
    if (def == nullptr) {
        return false;
    }
    if (!potion_use_implemented(id)) {
        // Fail loud rather than silently consume the slot for no effect. The
        // run layer's legality gate should already have kept this action off the
        // mask; this is the second line of defence for a direct caller.
        return false;
    }
    if (def->native) {
        dispatch_native_potion(s, id, def->potency, target);
        return true;
    }
    for (uint8_t k = 0; k < def->step_count; ++k) {
        queue_use_step(s, def->steps[static_cast<std::size_t>(k)], target);
    }
    return true;
}

void dispatch_native_potion(CombatState& s, PotionId id, int potency,
                            uint8_t /*target*/) noexcept {
    static_assert(sts::registry::manifest::kPotionsCount == 33,
                  "new potion: if it is native AND resolves in combat, its "
                  "use() body belongs in this switch. Native run-layer potions "
                  "(Fruit Juice, Entropic Brew) do not.");
    switch (id) {
        case PotionId::BLOOD_POTION: {
            // HealAction(player, floor(maxHealth * potency/100)). Replicate the
            // game's float math exactly: (int)((float)maxHealth *
            // ((float)potency / 100.0f)) (BloodPotion.java:43). heal() clamps to
            // [0, maxHealth]. potency is the heal PERCENT (20).
            // Routed through the shared in-combat heal seam so Magic Flower's
            // x1.5 applies (MagicFlower.onPlayerHeal, MagicFlower.java:728-735 --
            // the relic hooks AbstractPlayer.heal, so it sees EVERY heal in a
            // combat room, not only relic-sourced ones). Without the relic the
            // seam is the same clamped add this used to spell inline.
            const float ratio = static_cast<float>(potency) / 100.0f;
            const int heal = static_cast<int>(
                static_cast<float>(s.player_max_hp) * ratio);
            heal_player_with_relics(s, heal);
            break;
        }
        case PotionId::BLESSING_OF_THE_FORGE: {
            // BlessingOfTheForge.use (BlessingOfTheForge.java:43-47): addToBot
            // ArmamentsAction(true) -- and nothing else -- when the current
            // room phase is COMBAT (:44). ArmamentsAction's armamentsPlus
            // branch (ArmamentsAction.java:34-44) upgrades EVERY hand card with
            // canUpgrade(), with NO hand-select screen and no potency term.
            //
            // That is Armaments+ exactly (registry/cards.yaml:124,
            // {op: CHOOSE_CARD, choose: upgrade, amount: 99}): op_choose_card's
            // forced branch applies the manipulation to ALL eligible cards when
            // eligible <= amount, and choice_requires_user is false for the same
            // reason, so 99 never blocks the pump. No new machinery -- but the
            // potion cannot be authored as a data program, because CHOOSE_CARD
            // sits in the generator's CARD_CONTEXT op group and the potion
            // domain only admits GENERAL ops (tools/registry_gen/stsgen/
            // steps.py). Hence this native branch, which queues the identical
            // item by hand.
            //
            // KNOWN SHARED GAP (pre-existing, not introduced here, and NOT this
            // task's file to fix): choice_slot_eligible's UPGRADE arm
            // (interp/interp_cards.cpp) tests only !upgraded, while
            // AbstractCard.canUpgrade (AbstractCard.java:672-680) also rejects
            // CURSE and STATUS. Every CHOOSE_CARD{upgrade} consumer inherits it
            // -- Armaments and Armaments+ already do -- so this potion is no
            // worse than the card, and one fix at the eligibility predicate
            // corrects all three.
            //
            // The COMBAT gate at :44 is the run layer's: a potion USE is only
            // offered in RunPhase::COMBAT (combat_potion_legal) or via the
            // two-potion out-of-combat whitelist (noncombat_potion_legal), and
            // this potion is not on that whitelist.
            ActionQueueItem item{};
            item.opcode = static_cast<uint16_t>(Opcode::CHOOSE_CARD);
            item.src = kActorPlayer;
            item.tgt = kActorPlayer;  // hand-source choice: no exclusion index
            item.amount = 99;         // >= any hand size -> forced "upgrade all"
            item.flags = make_choose_flags(ChoiceKind::UPGRADE, /*random=*/false);
            add_to_bottom(s, item);   // addToBot (BlessingOfTheForge.java:45)
            break;
        }
        // --- Deferred native bodies (each lands with its dependency) ---
        // The power-granting potions (Dexterity, Steroid, Speed, Regen, Liquid
        // Bronze, Essence of Steel, Cultist) are now DATA APPLY_POWER programs --
        // their powers were registered by the potion-support-powers follow-up
        // (powers.yaml ids 14-19; Steroid reuses LoseStrength id 13) -- so they no
        // longer route here (use_potion sends them through queue_use_step).
        // Still native + DEFERRED, each on a verb owned elsewhere:
        // In-combat card CHOOSE: ELIXIR, ATTACK/SKILL/POWER/COLORLESS_
        // POTION, GAMBLERS_BREW, LIQUID_MEMORIES. (BLESSING_OF_THE_FORGE is no
        // longer among them -- it needed only the already-live CHOOSE_CARD
        // UPGRADE kind and is implemented above.)
        // Recursive play (a later opcode): DISTILLED_CHAOS, DUPLICATION_POTION
        // (its DuplicationPower re-queues the played card -- the blocker is the
        // opcode, NOT a missing power row). Cost randomization: SNECKO_OIL.
        // Out-of-combat revive: FAIRY_POTION (never USED at all).
        // IMPLEMENTED, but at the RUN layer, so they never arrive here:
        // FRUIT_JUICE and ENTROPIC_BREW (max-HP / slot mutation) --
        // run_advance's step_potion intercepts both ahead of use_potion.
        // potion_use_implemented names them so the legality gate still offers
        // them.
        case PotionId::SMOKE_BOMB:
            // SmokeBomb.use (SmokeBomb.java:37-48): in RoomPhase.COMBAT, mark
            // the room smoked and set the PLAYER's isEscaping + a 2.5s
            // escapeTimer; the timer expiring (AbstractPlayer.
            // updateEscapeAnimation, AbstractPlayer.java:2281-2292) is what
            // ends the battle, unconditionally. It never touches the monsters.
            // With no animation clock all three fields collapse into
            // kCombatFlagPlayerEscaped, set SYNCHRONOUSLY exactly as use()
            // sets isEscaping (the queued VFXAction is presentation); the
            // pump's combat-over check reads the bit at the top of its next
            // step, which is what lets the combat end with monsters still
            // alive. This body was deferred until the liveness predicate
            // could express escape -- an opcode writing COMBAT_OVER was
            // overwritten by the next pump_step -- and landed with the
            // Looter's escape, the other consumer of the same predicate.
            //
            // run_advance's step_potion still intercepts SMOKE_BOMB ahead of
            // use_potion and drives the run-level consequences (no-reward
            // proceed); this combat-layer body is the half a bare CombatState
            // caller gets.
            //
            // A SEPARATE GAP, recorded where the next reader will look:
            // SmokeBomb.canUse (:50-62) rejects the potion when any monster in
            // the group is `type == EnemyType.BOSS` (or has BackAttack, an
            // Act-3 power). It never asks the ROOM. combat_potion_legal
            // (run_advance.cpp) tests `room_type == RoomType::Boss` instead.
            // The two agree everywhere in Act 1, and `enemy_type` is a live
            // registry column with a MonsterDef::is_boss() accessor, so the
            // exact test is available whenever run_advance.cpp is open.
            s.flags |= kCombatFlagPlayerEscaped;
            break;
        default:
            // UNREACHABLE from use_potion: a data potion goes through
            // queue_use_step, an implemented native has a case above, and a
            // DEFERRED native is now refused before dispatch
            // (potion_use_implemented). Left as a safe no-op for a direct
            // caller rather than an assert, since the run-layer two above are
            // legitimate direct-call arguments with nothing to do in combat.
            break;
    }
}

PotionId get_random_potion(RngStream& potion_rng) noexcept {
    // PotionHelper.getRandomPotion(): potions.get(potionRng.random(size - 1)).
    // The pool is PotionId 1..33 in pool order, so index i -> PotionId(i + 1).
    const int idx = random(potion_rng, kPotionPoolSize - 1);
    return static_cast<PotionId>(idx + 1);
}

PotionId return_random_potion(RngStream& potion_rng, bool limited) noexcept {
    // AbstractDungeon.returnRandomPotion(limited): a d100 tier roll, then
    // reject-sample getRandomPotion(). The limited Entropic Brew form discards
    // the first candidate and rejects Fruit Juice thereafter.
    const int roll = random(potion_rng, 0, 99);
    const PotionRarity tier = potion_tier_for_roll(roll);

    auto draw = [&potion_rng]() noexcept {
        return get_random_potion(potion_rng);
    };

    PotionId temp = draw();
    bool spam_check = limited;
    while (spam_check ? true : potion_def(temp)->rarity != tier) {
        spam_check = limited;
        temp = draw();
        if (limited ? temp == PotionId::FRUIT_JUICE : false) {
            continue;
        }
        spam_check = false;
    }
    return temp;
}

}  // namespace sts::engine
