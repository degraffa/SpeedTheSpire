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
        case PotionId::ELIXIR:                 // dispatch_native_potion, below
        case PotionId::ATTACK_POTION:          // dispatch_native_potion, below
        case PotionId::SKILL_POTION:           // (the four DISCOVERY potions --
        case PotionId::POWER_POTION:           //  one body, pool selector in the
        case PotionId::COLORLESS_POTION:       //  item's src byte)
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
        case PotionId::ELIXIR: {
            // Elixir.use (Elixir.java:44-49): in RoomPhase.COMBAT, a single
            // addToBot(new ExhaustAction(false, true, true)). getPotency (:51-54)
            // is 0 -- the potion has no number of its own.
            //
            // The 3-arg ctor is (isRandom, anyNumber, canPickZero) and forwards
            // amount 99 (ExhaustAction.java:56-58). Walk ExhaustAction.update
            // (:73-110) in branch order with those values:
            //   :76-79  empty hand      -> isDone, nothing happens.
            //   :80-89  `!anyNumber && hand.size() <= amount` -- UNREACHABLE,
            //           anyNumber is true. This is the branch that would exhaust
            //           the whole hand with no screen.
            //   :90-94  isRandom -- UNREACHABLE, isRandom is false. So ELIXIR
            //           SPENDS NO card_random_rng AT ALL, on any path.
            //   :96-99  open(TEXT[0], 99, true, true) -- the OPTIONAL zero-to-99
            //           screen, ended only by the confirm button.
            //   :102-108 on retrieval, walk selectedCards.group IN PICK ORDER
            //           calling moveToExhaustPile, so the exhaust-pile order is
            //           the pick order and each card's onExhaust fires in it.
            //
            // That is EXACTLY Purity's authored program with the amount raised
            // (registry/cards.yaml:1856,
            //  {op: CHOOSE_CARD, choose: exhaust, amount: 3, optional: true}),
            // whose provenance block reasons through the same branch table --
            // Purity is ExhaustAction(magicNumber, false, true, true). So there
            // is no new machinery here: kChoiceOptionalBit, ActionVerb::CONFIRM
            // and optional_choice_slot_legal / toggle_optional_choice_slot /
            // resolve_optional_choice all already carry it.
            //
            // amount 99 IS THE CORRECT AUTHORED VALUE and must not be "tidied"
            // down to kHandCap: optional_choice_slot_legal compares the pick
            // count against item.amount, and 99 is what the Java compares
            // against. The 4-bit runtime selected-count nibble
            // (kChoiceSelectedShift) is safe regardless, because the SELECTION
            // is capped by hand size, which is <= kHandCap == 10.
            //
            // Like Blessing of the Forge this cannot be a data program:
            // CHOOSE_CARD is a CARD_CONTEXT op and the potion domain admits only
            // GENERAL ops (tools/registry_gen/stsgen/steps.py). Hence the
            // hand-built item. And like Blessing's, the RoomPhase.COMBAT guard
            // at Elixir.java:46 is the run layer's -- a potion USE is offered
            // only in RunPhase::COMBAT, and Elixir is not on the two-potion
            // out-of-combat whitelist (noncombat_potion_legal).
            ActionQueueItem item{};
            item.opcode = static_cast<uint16_t>(Opcode::CHOOSE_CARD);
            item.src = kActorPlayer;
            item.tgt = kActorPlayer;  // hand-source choice: no exclusion index
            item.amount = 99;         // ExhaustAction.java:56-58
            item.flags = make_choose_flags(ChoiceKind::EXHAUST, /*random=*/false,
                                           /*copies=*/1, kChoiceNoTypeFilter,
                                           /*optional=*/true);
            add_to_bottom(s, item);   // addToBot (Elixir.java:47)
            break;
        }
        case PotionId::ATTACK_POTION:
        case PotionId::SKILL_POTION:
        case PotionId::POWER_POTION:
        case PotionId::COLORLESS_POTION: {
            // The four "discover" potions are ONE shape with one argument
            // changed. Each use() is a single addToBot:
            //   AttackPotion.java:40-42     DiscoveryAction(CardType.ATTACK, potency)
            //   SkillPotion.java:40-42      DiscoveryAction(CardType.SKILL, potency)
            //   PowerPotion.java:40-42      DiscoveryAction(CardType.POWER, potency)
            //   ColorlessPotion.java:38-40  DiscoveryAction(true, potency)
            // All four getPotency return 1, all four isThrown false, none takes
            // a target. Their initializeData reads hasRelic("SacredBark") only
            // to pick a description string -- the actual doubling is
            // AbstractPotion.getPotency's, and it doubles POTENCY, which for
            // these is DiscoveryAction's `amount`, i.e. THE NUMBER OF COPIES
            // CREATED (1 -> 2). It does NOT change the three-card offer or the
            // pool. That is why the copy count is the second operand here.
            //
            // Opcode DISCOVERY (50) already does the whole lifecycle -- the
            // rejection-sampled three-card offer persisted in the item, the pump
            // intercept, the can_choose[0..2] mask, and the cost-0-this-turn
            // creation. What this adds is the POOL SELECTOR and the COPY COUNT,
            // in the item's otherwise-unused src/tgt bytes (interp.hpp
            // discovery_pool / discovery_copies). No struct change, so no
            // SCHEMA_VERSION bump.
            //
            // NATIVE rather than a data program even though DISCOVERY is in
            // GENERAL_OPS: a data step's `extra` becomes item.flags
            // (queue_use_step above), and for a DISCOVERY item flags IS the
            // offer slot -- a naively authored row would mispack the item and
            // make discovery_choice_prepared read garbage. Authoring these as
            // data needs a generator packer for DISCOVERY's pool/copies first;
            // until then the domain check cannot catch the mistake, so the
            // hand-built item is the safe form.
            ActionQueueItem item{};
            item.opcode = static_cast<uint16_t>(Opcode::DISCOVERY);
            item.src = static_cast<uint8_t>(
                id == PotionId::ATTACK_POTION    ? DiscoveryPool::ATTACK
                : id == PotionId::SKILL_POTION   ? DiscoveryPool::SKILL
                : id == PotionId::POWER_POTION   ? DiscoveryPool::POWER
                                                 : DiscoveryPool::COLORLESS);
            item.tgt = static_cast<uint8_t>(potency);  // DiscoveryAction.amount
            item.amount = 0;  // the "offer not yet generated" sentinel
            item.flags = 0;
            add_to_bottom(s, item);
            break;
        }
        // --- Deferred native bodies (each lands with its dependency) ---
        // The power-granting potions (Dexterity, Steroid, Speed, Regen, Liquid
        // Bronze, Essence of Steel, Cultist) are now DATA APPLY_POWER programs --
        // their powers were registered by the potion-support-powers follow-up
        // (powers.yaml ids 14-19; Steroid reuses LoseStrength id 13) -- so they no
        // longer route here (use_potion sends them through queue_use_step).
        // Still native + DEFERRED, each on a verb owned elsewhere:
        // In-combat card CHOOSE: GAMBLERS_BREW, LIQUID_MEMORIES.
        // (BLESSING_OF_THE_FORGE, ELIXIR and the four DISCOVERY potions are no
        // longer among them -- Blessing needed only the already-live CHOOSE_CARD
        // UPGRADE kind, Elixir only the already-live OPTIONAL EXHAUST one, and
        // the discover four only a pool selector and a copy count on the
        // already-live DISCOVERY opcode; all six are implemented above.)
        // Recursive play (a later opcode): DISTILLED_CHAOS, DUPLICATION_POTION
        // (its DuplicationPower re-queues the played card -- the blocker is the
        // opcode, NOT a missing power row).
        // SNECKO_OIL is NO LONGER among them: RANDOMIZE_HAND_COST (opcode 60)
        // landed and its row is now a two-step DATA program, so it never reaches
        // this switch at all.
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
            // The legality half lives in combat_potion_legal (run_advance.cpp),
            // which now applies SmokeBomb.canUse (:50-63) as written: reject
            // when ANY MONSTER IN THE GROUP is `type == EnemyType.BOSS`, walking
            // the whole group with no liveness gate, rather than asking whether
            // the ROOM is a boss room. (It tested `room_type == RoomType::Boss`
            // until the wave-C potions stage closed this gap.) The BackAttack
            // clause at :54-56 is an Act-3 power with no S1 row and is named at
            // that site rather than invented as state.
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
