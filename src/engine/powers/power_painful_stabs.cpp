// Painful Stabs -- native power-hook body. One translation unit per power; see
// power_native.hpp for the dispatch plumbing and power_painful_stabs.hpp for
// what this power does.

#include "power_painful_stabs.hpp"

#include <cstdint>

#include "power_native.hpp"             // PowerNativeSig (the shared handler signature)
#include "sts/engine/action_queue.hpp"  // add_to_bottom / kActor*
#include "sts/engine/combat_state.hpp"
#include "sts/engine/interp.hpp"        // Opcode, CardPile, DamageType, make_make_card_flags
#include "sts/engine/types.hpp"

namespace sts::engine {

void power_native_painful_stabs(CombatState& s, Hook hook,
                                const HookContext& ctx) noexcept {
    if (hook != Hook::ON_INFLICT_DAMAGE) {
        return;
    }
    // PainfulStabsPower.onInflictDamage (PainfulStabsPower.java:39-44):
    //
    //     if (damageAmount > 0 && info.type != DamageInfo.DamageType.THORNS)
    //         this.addToBot(new MakeTempCardInDiscardAction(new Wound(), 1));
    //
    // THE GUARD IS SPLIT, and both halves are spelled so the split is checkable:
    //   * `damageAmount > 0` is the DISPATCHER's -- dispatch_on_inflict_damage
    //     (power_hooks.cpp) returns early at amount <= 0, because the Java's only
    //     call site sits inside AbstractPlayer.damage's own `if (damageAmount >
    //     0)` block. A fully-blocked stab therefore never reaches here at all.
    //     Re-tested below anyway: it costs one comparison and makes this body
    //     correct on its own terms rather than on a caller's promise.
    //   * `info.type != THORNS` is THIS body's, and it is the one thing that
    //     makes the power native. HP_LOSS is NOT excluded -- the Java tests
    //     THORNS alone -- so a hit of type HP_LOSS from this owner would still
    //     make a Wound. Nothing in the Book of Stabbing's kit produces one, but
    //     the condition is transcribed, not narrowed.
    if (ctx.amount <= 0) {
        return;
    }
    if (ctx.damage_type == static_cast<uint8_t>(DamageType::THORNS)) {
        return;
    }
    // MakeTempCardInDiscardAction(new Wound(), 1) -- ONE copy per HIT, not per
    // turn: the Book of Stabbing's STAB queues one DamageAction per stab
    // (BookOfStabbing.java:89-92) and each of them dispatches this hook
    // separately. addToBot, so the Wounds land behind whatever the attack itself
    // already queued.
    ActionQueueItem mk{};
    mk.opcode = static_cast<uint16_t>(Opcode::MAKE_CARD);
    mk.src = static_cast<uint8_t>(CardPile::DISCARD);  // the destination pile
    mk.tgt = kActorPlayer;  // unused by MAKE_CARD; must not read as a sentinel
    mk.amount = 1;          // the literal 1 at :42 -- NOT the power's amount,
                            // which is the -1 marker
    mk.flags = make_make_card_flags(static_cast<uint16_t>(CardId::WOUND));
    add_to_bottom(s, mk);
}

}  // namespace sts::engine
