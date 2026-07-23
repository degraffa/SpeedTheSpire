// Potion USE mechanics -- see potions.hpp for the full provenance, the B4.3
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
#include "sts/engine/rng_stream.hpp"    // random (potionRng draws)
#include "sts/engine/types.hpp"         // PotionId

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

bool use_potion(CombatState& s, PotionId id, uint8_t target) noexcept {
    const PotionDef* def = potion_def(id);
    if (def == nullptr) {
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
    switch (id) {
        case PotionId::BLOOD_POTION: {
            // HealAction(player, floor(maxHealth * potency/100)). Replicate the
            // game's float math exactly: (int)((float)maxHealth *
            // ((float)potency / 100.0f)) (BloodPotion.java:43). heal() clamps to
            // [0, maxHealth]. potency is the heal PERCENT (20).
            const float ratio = static_cast<float>(potency) / 100.0f;
            const int heal = static_cast<int>(
                static_cast<float>(s.player_max_hp) * ratio);
            int hp = static_cast<int>(s.player_hp) + heal;
            if (hp > s.player_max_hp) {
                hp = s.player_max_hp;
            }
            s.player_hp = static_cast<int16_t>(hp);
            break;
        }
        // --- Deferred native bodies (land with their dependency; B3.23 Log) ---
        // The power-granting potions (Dexterity, Steroid, Speed, Regen, Liquid
        // Bronze, Essence of Steel, Cultist) are now DATA APPLY_POWER programs --
        // their powers were registered by the potion-support-powers follow-up
        // (powers.yaml ids 14-19; Steroid reuses LoseStrength id 13) -- so they no
        // longer route here (use_potion sends them through queue_use_step).
        // Still native + DEFERRED, each on a verb owned elsewhere:
        // In-combat card CHOOSE (B3.4): ELIXIR, ATTACK/SKILL/POWER/COLORLESS_
        // POTION, GAMBLERS_BREW, LIQUID_MEMORIES, BLESSING_OF_THE_FORGE.
        // Recursive play (a later opcode): DISTILLED_CHAOS, DUPLICATION_POTION
        // (its DuplicationPower re-queues the played card -- the blocker is the
        // opcode, NOT a missing power row). Cost randomization: SNECKO_OIL.
        // Run-layer mutation (B4.x): FRUIT_JUICE, ENTROPIC_BREW. Combat escape
        // (B3.15) / out-of-combat revive: SMOKE_BOMB, FAIRY_POTION.
        default:
            // No combat-state change until the dependency lands. Not reachable
            // for a data potion (use_potion routes those through queue_use_step).
            break;
    }
}

PotionId return_random_potion(RngStream& potion_rng) noexcept {
    // AbstractDungeon.returnRandomPotion(false): a d100 tier roll, then reject-
    // sample getRandomPotion() until the rolled potion's rarity matches the
    // tier. limited == false, so the "Fruit Juice" spam guard is inert (the
    // loop's only exit condition is the rarity match).
    const int roll = random(potion_rng, 0, 99);
    const PotionRarity tier = potion_tier_for_roll(roll);

    // getRandomPotion(): potions.get(potionRng.random(size - 1)). The pool is
    // PotionId 1..33 in pool order, so index i -> PotionId(i + 1).
    auto draw = [&potion_rng]() noexcept {
        const int idx = random(potion_rng, kPotionPoolSize - 1);
        return static_cast<PotionId>(idx + 1);
    };

    PotionId temp = draw();
    while (potion_def(temp)->rarity != tier) {
        temp = draw();
    }
    return temp;
}

}  // namespace sts::engine
