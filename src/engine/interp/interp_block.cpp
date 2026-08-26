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
#include "sts/engine/rng_stream.hpp"    // random (BLOCK_RANDOM_MONSTER's ai_rng pick)
#include "sts/engine/types.hpp"
#include "sts/registry/manifest.hpp"    // generated kPowersCount
#include "sts/registry/monster_table.hpp"  // MonsterIntent (the ESCAPE telegraph)

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
    // Checked for Mayhem and Magnetism (the two colorless-RARE POWER cards'
    // start-of-turn generators), neither of which needs a
    // case in EITHER block pass: MayhemPower's only overrides are
    // updateDescription and atStartOfTurn (MayhemPower.java:28-39) and
    // MagnetismPower's are updateDescription, stackPower and atStartOfTurn
    // (MagnetismPower.java:30-49) -- no modifyBlock, no modifyBlockLast.
    // Checked for Panache and The Bomb, neither of which needs a case in EITHER
    // block pass: PanachePower's only overrides are updateDescription,
    // stackPower, onUseCard and atStartOfTurn (PanachePower.java:40-67) and
    // TheBombPower's are atEndOfTurn and updateDescription (TheBombPower.java:
    // 40-53). Both are damage SOURCES and touch no block path at all.
    // Checked for Vigor and Pen Nib: both are attacker-side atDamageGive
    // scalers and neither overrides modifyBlock or modifyBlockLast
    // (VigorPower and PenNibPower read in full), so this guard moves and no
    // case is added in either block pass.
    // Checked for Regenerate Monster (PowerId::REGENERATE_MONSTER, id 91):
    // its only override is atEndOfTurn (RegenerateMonsterPower.java:37-43),
    // read in full -- it touches no block path at all.
    // Checked for Duplication (id 92): it overrides only updateDescription,
    // onUseCard and atEndOfRound (DuplicationPower.java:19-72, read in full)
    // -- no modifyBlock or modifyBlockLast -- so the count moves and no case
    // is added.
    // Checked for S2.21's two powers, neither of which needs a case in EITHER
    // block pass: HEX overrides only onUseCard (HexPower.java:36-42) and FLIGHT
    // only atStartOfTurn / atDamageFinalReceive / onAttacked / onRemove
    // (FlightPower.java:47-78) -- both read in full. Flight halves incoming
    // DAMAGE, which is interp_damage.cpp's third pass; it does not touch a block
    // gain, so the count moves here with no case.
    // Checked for S2.22's one power, MALLEABLE (id 95): it overrides ONLY
    // onAttacked, atEndOfTurn, atEndOfRound and stackPower (MalleablePower.java:
    // 42-82, read in full) -- so neither block pass gains a case. It GRANTS block
    // (a queued GainBlockAction from onAttacked), which is a producer, not a
    // modifier; that block arrives here as an ordinary direct-add BLOCK item and
    // is deliberately NOT scaled by the owner's own Dexterity/Frail, exactly as
    // GainBlockAction never is.
    // Checked for S2.23's two powers, neither of which needs a case in either
    // block pass: MINION overrides only updateDescription (MinionPower.java:
    // 30-33) and PAINFUL_STABS only updateDescription and onInflictDamage
    // (PainfulStabsPower.java:34-44) -- neither touches block at all.
    // Checked for S2.25's three powers, none of which needs a case: REGROW
    // overrides only updateDescription (RegrowPower.java:30-33), EXPLOSIVE only
    // updateDescription + duringTurn (ExplosivePower.java:41-57) and
    // GENERIC_STRENGTH_UP only updateDescription + atEndOfRound
    // (GenericStrengthUpPower.java:29-39) -- none touches block.
    // Checked for S2.27's two powers, neither of which touches block: SLOW
    // overrides onAfterUseCard, atEndOfRound and atDamageReceive
    // (SlowPower.java, read in full) and INTANGIBLE_MONSTER overrides
    // atDamageFinalReceive and atEndOfTurn (IntangiblePower.java, read in full).
    // Both count moves here are caseless.
    // Checked for S2.24's one power, STASIS (id 98): it overrides ONLY
    // updateDescription and onDeath (StasisPower.java:32-44, read in full) --
    // neither block pass gains a case.
    static_assert(sts::registry::manifest::kPowersCount == 72,
                  "new power: does it override modifyBlock (block-gain scaling, "
                  "as Dexterity and Frail do)? Add a case here if so. This guard "
                  "covers BOTH block passes -- check modifyBlockLast in "
                  "modify_block_last just below in the same pass; No Block is "
                  "the game's only overrider of it.");
    // S2.26 (kPowersCount 56 -> 60): CONSTRICTED, FADING, SHIFTING and
    // REACTIVE. Checked one at a time against BOTH count-guarded families,
    // and all four are pure count moves. ConstrictedPower overrides only
    // atEndOfTurn; FadingPower only duringTurn; ReactivePower only
    // onAttacked. ShiftingPower overrides only onAttacked too -- and it is
    // the one worth naming, because it LOOKS like a mitigation power: it
    // returns damageAmount UNCHANGED (ShiftingPower.java:41) and swaps the
    // OWNER's Strength instead, so no pass here sees it.
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
            return blk * 0.75f;                         // FrailPower.java:58-60
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
    // GainBlockAction.update:52-54 -- `if (!target.isDying && !target.isDead
    // && ...) target.addBlock(amount);`. A RESOLVE-TIME liveness read on the
    // RECIPIENT, and the sibling of the isDeadOrEscaped early-out
    // ApplyPowerAction carries (interp_powers.cpp op_apply_power).
    //
    // isDying/isDead, NOT isDeadOrEscaped, and the difference is real: a
    // HALF-DEAD monster is neither dying nor dead (its die() was suppressed), so
    // it DOES gain block -- unlike a power aimed at it, which the apply-side
    // guard rejects. Both Java conditions collapse to `hp <= 0 && !halfDead` here
    // (this engine models isDying that way and has no separate post-animation
    // isDead), and an ESCAPED monster is deliberately not excluded, because
    // GainBlockAction does not test isEscaping.
    //
    // WHY IT LANDS NOW (S2.28): Deca's Square of Protection queues one
    // GainBlockAction per monster RECORD with no liveness filter at all
    // (Deca.java:122-128), so a dead Donu is queued a block it must not receive.
    // Before Act 3 every BLOCK recipient was the player or a self-buffing live
    // monster, so the guard had nothing to reject -- except in one pre-existing
    // corner it also closes: a monster killed mid-turn by Thorns whose own move
    // had already queued a self-BLOCK behind the damage.
    if (tgt != kActorPlayer && tgt < kMonsterCap) {
        const MonsterState& m = s.monsters[tgt];
        if (m.hp <= 0 && !monster_half_dead(m)) {
            return;
        }
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
    // The per-card GainBlockActions are addToTop, one per collected card
    // (BlockPerNonAttackAction.java:35-37), so they resolve in REVERSE hand
    // order and every one PRECEDES the pending UseCardAction -- a forward walk
    // pushing each to the top reproduces exactly that. The order is not
    // cosmetic (the op_dropkick precedent's sibling, S2.43 triage): each gain
    // fires ON_GAINED_BLOCK, and Juggernaut's per-gain
    // DamageRandomEnemyAction draws card_random_rng at ITS execute over the
    // live monsters -- a mid-sequence kill reshapes the pool, so the gains'
    // resolve order is observable through the target sequence. (Sentinel's
    // exhaust-chain energy items sit behind these after the push; the game
    // resolved them earlier, but GAIN_ENERGY commutes with BLOCK and with the
    // queued THORNS damage, so the boundary state is identical.)
    for (int k = 0; k < n; ++k) {
        ActionQueueItem blk{};
        blk.opcode = static_cast<uint16_t>(Opcode::BLOCK);
        blk.src = kActorPlayer;
        blk.tgt = kActorPlayer;
        blk.amount = block_per_card;
        blk.flags = 0;  // card-style block: Dexterity/Frail apply per gain
        add_to_top(s, blk);  // addToTop (BlockPerNonAttackAction.java:36)
    }
}

// BLOCK_RANDOM_MONSTER (GainBlockRandomMonsterAction.update,
// GainBlockRandomMonsterAction.java:26-42) -- the Centurion's Protect
// (Centurion.java:93). The Java, minus the FlashAtkImgEffect:
//
//     ArrayList<AbstractMonster> valid = new ArrayList<>();
//     for (AbstractMonster m : AbstractDungeon.getMonsters().monsters) {
//         if (m == this.source || m.intent == Intent.ESCAPE || m.isDying)
//             continue;
//         valid.add(m);
//     }
//     this.target = !valid.isEmpty()
//         ? valid.get(AbstractDungeon.aiRng.random(valid.size() - 1))
//         : this.source;
//     if (this.target != null) this.target.addBlock(this.amount);
//
// THREE DETAILS ARE LOAD-BEARING, and each is why this is an opcode rather than
// a BLOCK step with a clever target.
//
// (1) THE ESCAPE FILTER READS THE TELEGRAPHED INTENT, NOT THE ESCAPED FLAG.
//     `m.intent == Intent.ESCAPE` is the intent the ally is currently SHOWING,
//     so an ally that has merely ANNOUNCED its exit -- a Looter or Mugger on its
//     Smoke Bomb turn, which telegraphs ESCAPE for a turn it is still present and
//     fighting -- is skipped while it is very much alive. It also covers the
//     already-gone case for free, because both thieves RE-telegraph ESCAPE as
//     they leave (Looter.java:131, Mugger.java:132). This does NOT test
//     kMonsterFlagEscaped, and substituting monster_dead_or_escaped here would be
//     wrong in exactly the announced-but-still-present window.
//
// (2) AN EMPTY VALID LIST SPENDS NO DRAW AT ALL. The ternary evaluates
//     aiRng.random only on the non-empty arm, so a solo Centurion's Protect and
//     one with a live ally move the shared ai_rng stream by DIFFERENT amounts --
//     an observable, seed-level difference, not a cosmetic one.
//
// (3) THE WALK VISITS DEAD RECORDS. MonsterGroup never removes a dead monster
//     (SuicideAction.java:29-34 / AbstractMonster.java:925-951), so the loop sees
//     them and rejects them only through `isDying`, which this engine models as
//     hp <= 0. An escaped-but-alive ally is rejected by (1), not by hp.
//
// The block itself is `target.addBlock(amount)` -- a DIRECT add, exactly like
// GainBlockAction's, so neither modifyBlock pass runs. That is op_block's
// kBlockNoPowers form, which is also how it picks up onGainedBlock.
void op_block_random_monster(CombatState& s, uint8_t src, int amount) noexcept {
    if (src >= kMonsterCap) {
        return;  // the Java's source is always the acting monster
    }
    uint8_t valid[kMonsterCap];
    uint8_t n = 0;
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        if (i == src) {
            continue;  // `m == this.source`
        }
        const MonsterState& m = s.monsters[i];
        if (m.intent ==
            static_cast<uint8_t>(sts::registry::MonsterIntent::ESCAPE)) {
            continue;  // `m.intent == Intent.ESCAPE` -- the TELEGRAPH, see (1)
        }
        if (m.hp <= 0) {
            continue;  // `m.isDying`
        }
        valid[n++] = i;
    }
    uint8_t tgt = src;  // the empty-list arm: `this.target = this.source`
    if (n > 0) {
        // ONE inclusive draw over 0..n-1, and ONLY here -- see (2).
        const int32_t pick = random(s.ai_rng, static_cast<int32_t>(n) - 1);
        tgt = valid[static_cast<uint8_t>(pick)];
    }
    op_block(s, tgt, amount, kBlockNoPowers);  // a direct addBlock
}

}  // namespace sts::engine
