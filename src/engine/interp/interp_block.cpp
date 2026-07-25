// BLOCK-domain opcode bodies -- the modifyBlock gain path and the two derived
// block verbs (moved verbatim out of interp.cpp's anonymous namespace; see
// interp_ops.hpp for the split's rationale).

#include "interp_block.hpp"

#include <cstdint>

#include "interp_ops.hpp"               // actor_powers / actor_block
#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"         // card_def / CardType (non-ATTACK scan)
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, kBlockNoPowers, mathutils_floor
#include "sts/engine/piles.hpp"         // exhaust_card
#include "sts/engine/power_hooks.hpp"   // dispatch_on_gained_block
#include "sts/engine/types.hpp"
#include "sts/registry/manifest.hpp"    // generated kPowersCount

namespace sts::engine {

namespace {

// modifyBlock (block-gain hook, distinct from the damage hooks in
// interp_damage.cpp):
// DexterityPower.modifyBlock adds `amount`; FrailPower multiplies by 0.75f.
// Iterate in power-list order and floor once after all modifiers, matching
// AbstractCard.applyPowersToBlock. Applied by op_block for CARD block only
// (power/relic/potion block sets kBlockNoPowers).
// The `default: return blk` below is a deliberate subset (most powers do not
// touch block gain), so -Wswitch-enum would only add noise. The static_assert is
// what makes a new power impossible to add without re-reading this switch.
[[nodiscard]] float modify_block(float blk, PowerSlot p) noexcept {
    static_assert(sts::registry::manifest::kPowersCount == 33,
                  "new power: does it override modifyBlock (block-gain scaling, "
                  "as Dexterity and Frail do)? Add a case here if so.");
    // Checked for the Guardian's two powers, neither needs a case: ModeShiftPower
    // overrides only updateDescription (ModeShiftPower.java:27-30) and
    // SharpHidePower only updateDescription + onUseCard (SharpHidePower.java:
    // 38-49). Neither touches block gain.
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::DEXTERITY: {
            const float m = blk + static_cast<float>(p.amount);
            return m < 0.0f ? 0.0f : m;                 // modifyBlock floors at 0
        }
        case PowerId::FRAIL:
            return blk * 0.75f;                         // FrailPower.java:59-61
        default:
            return blk;
    }
}

}  // namespace

// BLOCK: tgt gains `amount` block. CARD block (flags & kBlockNoPowers == 0) runs
// the gainer's modifyBlock hooks (Dexterity) with a single floor, mirroring
// AbstractCard.applyPowers + GainBlockAction; power/relic/potion block sets
// kBlockNoPowers (a direct GainBlockAction -- no modifyBlock) so it takes the
// straight add. With no Dexterity present the modifyBlock pass is the identity and
// gain == amount, so the 20 combat fixtures (no Dexterity) stay byte-identical.
void op_block(CombatState& s, uint8_t tgt, int amount, uint32_t flags) noexcept {
    int16_t* blk = actor_block(s, tgt);
    if (blk == nullptr) {
        return;
    }
    int gain = amount;
    if ((flags & kBlockNoPowers) == 0u) {
        float tmp = static_cast<float>(amount);
        const PowerView pv = actor_powers(s, tgt);
        for (uint8_t i = 0; i < pv.count; ++i) {
            tmp = modify_block(tmp, pv.slots[i]);   // Dexterity: + amount, floor 0
        }
        if (tmp < 0.0f) {
            tmp = 0.0f;                             // GainBlockAction post-clamp
        }
        gain = mathutils_floor(tmp);
    }
    int nb = *blk + gain;
    if (nb < 0) {
        nb = 0;
    }
    *blk = static_cast<int16_t>(nb);
    // onGainedBlock (Juggernaut): fires after the block gain, on the actor's own
    // powers, only for a positive gain (AbstractCreature.addBlock:426-433), with
    // the ACTUAL block gained (post-Dexterity). No-op unless a power binds
    // ON_GAINED_BLOCK -- so the skeleton BLOCK is unchanged.
    dispatch_on_gained_block(s, tgt, gain);
}

// --- Block-manipulation opcode bodies ----------------------------------------

// DOUBLE_BLOCK (Entrench / DoubleYourBlockAction.update:24-30): if tgt has
// block, addBlock(currentBlock). A direct addBlock -- no card applyPowers, so
// no Dexterity/Frail (kBlockNoPowers) -- and the onGainedBlock hooks fire via
// op_block. Zero block does nothing (no hook fires).
void op_double_block(CombatState& s, uint8_t tgt) noexcept {
    const int16_t* blk = actor_block(s, tgt);
    if (blk == nullptr || *blk <= 0) {
        return;
    }
    op_block(s, tgt, *blk, kBlockNoPowers);
}

// BLOCK_PER_NON_ATTACK (Second Wind / BlockPerNonAttackAction.update:29-42):
// collect every non-ATTACK hand card; the queued ExhaustSpecificCardActions
// resolve FIRST and in REVERSE hand order (each addToTop lands in front of the
// previous), then the per-card GainBlockActions. The exhausts run synchronously
// here (each firing the §5.5 onExhaust chain -- a Sentinel's addToTop energy
// lands at the queue front, ahead of the block items, matching the game's
// resolve-immediately-after-that-exhaust ordering); the block gains are queued
// as per-card CARD-style BLOCK items (flags 0): SecondWind.use passes
// `this.block`, the card's applyPowers value, so Dexterity/Frail modify each
// per-card gain exactly as op_block's modify-block pass does.
void op_block_per_non_attack(CombatState& s, int block_per_card) noexcept {
    // Snapshot the non-Attack pool indices first (exhausting compacts the hand).
    CardPoolIndex to_exhaust[kHandCap];
    int n = 0;
    for (uint8_t i = 0; i < s.hand_count && n < kHandCap; ++i) {
        const CardPoolIndex pi = s.hand[i];
        const CardDef* def = card_def(static_cast<CardId>(s.card_pool[pi].card_id));
        if (def != nullptr && def->type != CardType::ATTACK) {
            to_exhaust[n++] = pi;
        }
    }
    for (int k = n - 1; k >= 0; --k) {  // reverse hand order: last card first
        exhaust_card(s, to_exhaust[k]);
    }
    for (int k = 0; k < n; ++k) {
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = kActorPlayer;
        blk.tgt = kActorPlayer;
        blk.amount = block_per_card;
        blk.flags = 0;  // card-style block: Dexterity/Frail apply per gain
        add_to_bottom(s, blk);
    }
}

}  // namespace sts::engine
