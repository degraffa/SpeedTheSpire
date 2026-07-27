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

// AbstractCard.applyPowersToBlock (AbstractCard.java:2291-2307) is TWO passes
// over the SAME power list, not one:
//
//     for (AbstractPower p : player.powers) tmp = p.modifyBlock(tmp, this);
//     for (AbstractPower p : player.powers) tmp = p.modifyBlockLast(tmp);
//
// then a single clamp at 0 and a single MathUtils.floor. The two functions below
// are those two passes, and op_block runs them in that order. Folding the second
// hook into the first would be observably wrong: a power that zeroes the block in
// modifyBlockLast must beat EVERY modifyBlock contributor, including ones that sit
// LATER in the power list. With a list of [No Block, Dexterity 3] a single pass
// yields 3 (No Block zeroes, then Dexterity adds); the game yields 0, because
// Dexterity's whole contribution is made in the first pass and the zeroing happens
// after all of it. The order is the reason, so it has a named test.
//
// Pass 1, modifyBlock (the block-gain hook, distinct from the damage hooks in
// interp_damage.cpp): DexterityPower.modifyBlock adds `amount`
// (DexterityPower.java:86-92); FrailPower multiplies by 0.75f
// (FrailPower.java:58-60). Applied by op_block for CARD block only
// (power/relic/potion block sets kBlockNoPowers).
// The `default: return blk` below is a deliberate subset (most powers do not
// touch block gain), so -Wswitch-enum would only add noise. The static_assert is
// what makes a new power impossible to add without re-reading BOTH switches.
[[nodiscard]] float modify_block(float blk, PowerSlot p) noexcept {
    // Checked for Confusion (Snecko Eye's ConfusionPower), which needs no
    // case: its ONLY override is onCardDraw (ConfusionPower.java:38-48).
    // Checked for the two monster powers added alongside the slavers and the
    // Fungi Beast, neither of which needs a case:
    // EntanglePower overrides only playApplyPowerSfx / updateDescription /
    // atEndOfTurn (EntanglePower.java:31-46) and SporeCloudPower only
    // updateDescription / onDeath (SporeCloudPower.java:28-42).
    // Checked for the Looter's Thievery, which needs no case: ThieveryPower's
    // ONLY override is updateDescription (ThieveryPower.java:27-30).
    static_assert(sts::registry::manifest::kPowersCount == 45,
                  "new power: does it override modifyBlock (block-gain scaling, "
                  "as Dexterity and Frail do)? Add a case here if so. This guard "
                  "covers BOTH block passes -- check modifyBlockLast in "
                  "modify_block_last just below in the same pass; No Block is "
                  "the game's only overrider of it.");
    // Checked for the Guardian's two powers, neither needs a case: ModeShiftPower
    // overrides only updateDescription (ModeShiftPower.java:27-30) and
    // SharpHidePower only updateDescription + onUseCard (SharpHidePower.java:
    // 38-49). Neither touches block gain.
    // Checked for the six red-rare powers. Two of them look like block cases and
    // neither is: BARRICADE does not SCALE a block gain, it suppresses the
    // start-of-turn DECAY, which is a branch in the start-of-turn sequence
    // (GameActionManager.java:353-359) and not a modifyBlock override --
    // BarricadePower overrides only updateDescription (BarricadePower.java:27-30);
    // and JUGGERNAUT only READS the gain in onGainedBlock (JuggernautPower.java:
    // 34-40), which fires AFTER this pass and returns nothing. The other four
    // (Berserk / Brutality / Demon Form / Double Tap) touch no block path at all.
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

// Pass 2, modifyBlockLast. AbstractPower.modifyBlockLast is a pass-through and
// NoBlockPower is the whole game's only overrider of it (verified by searching
// the decompiled tree: AbstractPower.java declares it, NoBlockPower.java
// overrides it, AbstractCard.java calls it, and nothing else mentions it).
// NoBlockPower.modifyBlockLast (NoBlockPower.java:58-60) returns 0.0f
// unconditionally -- it does not scale, it discards, which is precisely why it
// has to run after every modifyBlock contributor rather than among them.
// No guard of its own: the kPowersCount static_assert in modify_block above
// covers this switch too and says so, keeping interp_block.cpp at exactly one
// count-guard site.
[[nodiscard]] float modify_block_last(float blk, PowerSlot p) noexcept {
    switch (static_cast<PowerId>(p.power_id)) {
        case PowerId::NO_BLOCK:
            return 0.0f;                                // NoBlockPower.java:58-60
        default:
            return blk;
    }
}

}  // namespace

// BLOCK: tgt gains `amount` block. CARD block (flags & kBlockNoPowers == 0) runs
// the gainer's block-modifier hooks in AbstractCard.applyPowersToBlock's order --
// the whole modifyBlock pass, THEN the whole modifyBlockLast pass -- with a single
// clamp and a single floor after both; power/relic/potion block sets
// kBlockNoPowers (a direct GainBlockAction -- neither pass runs) so it takes the
// straight add. With neither a Dexterity/Frail nor a No Block present both passes
// are the identity and gain == amount, so the 20 combat fixtures stay
// byte-identical.
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
        // The SECOND pass, over the SAME list from the start
        // (AbstractCard.java:2296-2298). Structurally separate, not a case folded
        // into the loop above: No Block must beat a Dexterity that sits after it.
        for (uint8_t i = 0; i < pv.count; ++i) {
            tmp = modify_block_last(tmp, pv.slots[i]);   // No Block: -> 0
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
