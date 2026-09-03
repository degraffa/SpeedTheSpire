#pragma once

// Card registry -- re-export of the GENERATED effect-program tables (Stage B
// design §4.3/§4.4). The constexpr `CardDef`/`CardEffectStep` table is emitted
// by tools/registry_gen/gen.py from registry/cards.yaml into
// <build>/generated/sts/registry/card_table.hpp; this header aliases it into
// sts::engine so the interpreter (interp.hpp) and card-play resolver
// (card_play.cpp) consume it unchanged. There is NO hand-written card table
// anymore -- registry/cards.yaml is the single source of truth (per-entry
// provenance lives there), and the generated header re-pins every id with a
// `static_assert` (ids are append-only, Stage B design §4.4).
//
// WHY A DATA TABLE (not a dispatch function per card): design doc §6 freezes
// "effect programs are sequences of {op, target, amount, flags}". A constexpr
// step table is the literal encoding of that decision -- the interpreter and
// card-play resolver stay effect-program-driven rather than growing a switch
// per card. Header-only: the table is `constexpr`, no .cpp needed.
//
// The generated table carries its own Opcode mirror (sts::registry::Opcode) so
// the header compiles standalone; the engine's authoritative Opcode (with the
// §6 numbering rationale) stays hand-written in interp.hpp. The static_asserts
// below pin the two byte-equal so they cannot drift.

#include <cstdint>

#include "sts/engine/interp.hpp"          // Opcode (engine authoritative set)
#include "sts/engine/types.hpp"           // CardId, PowerId
#include "sts/registry/card_table.hpp"    // generated: the card tables (build tree)

namespace sts::engine {

// --- Re-exported table types (generated) -------------------------------------

// Card type -- Attack/Skill for target validation (an Attack targets a monster;
// a Skill targets the player/self). Powers/Status/Curse land with Stage B
// content tasks.
using CardType = sts::registry::CardType;

// Exact AbstractCard.CardTarget value. `needs_target` remains the compact UI
// convenience, while this distinction is needed by GameActionManager's
// post-hook ENEMY-only dead/escaping suppression (SELF_AND_ENEMY is excluded).
using CardTargetKind = sts::registry::CardTargetKind;

// When a card's effect program runs. ON_PLAY is the played-card
// path (every ATTACK/SKILL + the playable Slimed status); the unplayable
// statuses/curses instead run their program at a passive hook -- END_OF_TURN
// (Burn/Decay/Doubt/Regret/Shame), ON_DRAW (Void), ON_OTHER_CARD_PLAYED (Pain).
using CardTrigger = sts::registry::CardTrigger;

// Where a single effect step lands, symbolically; card_play.cpp substitutes the
// concrete actor index (kActorPlayer for SELF, the chosen/rolled monster slot
// for CARD_TARGET) at resolution time.
using StepTarget = sts::registry::StepTarget;

// One effect-program step (design doc §6: "{op, target, amount, flags}"). For
// APPLY_POWER, `extra` carries the PowerId (the make_apply_power_flags
// encoding, pinned below); for every other opcode `extra` is 0.
using CardEffectStep = sts::registry::CardEffectStep;

// A registry entry: cost + type + targeting + the effect program.
using CardDef = sts::registry::CardDef;

using sts::registry::kMaxCardSteps;
// CardDef::requires_draw_pile_type's "no such predicate" sentinel (255, outside
// the CardType range). Read by card_can_use_without_target -- Secret Technique
// needs a SKILL in the draw pile, Secret Weapon an ATTACK.
using sts::registry::kNoDrawPileType;
using sts::registry::kPoolableCurseCount;
using sts::registry::kPoolableCurses;
// The Ironclad in-combat ATTACK transform pool (Infernal Blade /
// returnTrulyRandomCardInCombat(ATTACK), AbstractDungeon.java:964-979). Derived
// by the generator from the rows' color/rarity/type/healing columns.
// ORDER is `src_combat_order` (tools/registry_gen/stsgen/emit/cards.py:94-118):
// the rarity-major concatenation of the REVERSED per-rarity library order the
// three src*CardPools hold, i.e. JAVA-EXACT. (These three comments used to
// assert a registry-id-order deviation and point at a "pool-order note in
// gen.py"; the deviation was fixed when src_combat_order landed and gen.py has
// no such note -- a conventions §8 "comment asserting X" running stale.)
using sts::registry::kIroncladAttackPoolCount;
using sts::registry::kIroncladAttackPool;
// The SKILL sibling of the pool above: returnTrulyRandomCardInCombat(SKILL) is
// the SAME method with one CardType changed (Chrysalis.java:34 vs Infernal
// Blade's / Metamorphosis.java:34's ATTACK). Same generator derivation, same
// src_combat_order ordering -- opcode 55's filtered view inherits both.
using sts::registry::kIroncladSkillPoolCount;
using sts::registry::kIroncladSkillPool;
// The POWER sibling, added for Power Potion: DiscoveryAction.generateCardChoices
// (DiscoveryAction.java:105-120) calls that same
// returnTrulyRandomCardInCombat(type) with CardType.POWER
// (PowerPotion.java:40-42). Same derivation, same ordering.
using sts::registry::kIroncladPowerPoolCount;
using sts::registry::kIroncladPowerPool;
// Full non-healing RED combat pool (Discovery / returnTrulyRandomCardInCombat).
using sts::registry::kIroncladCombatPoolCount;
using sts::registry::kIroncladCombatPool;
// Full non-healing COLORLESS uncommon+rare combat pool (Jack of All Trades).
using sts::registry::kColorlessCombatPoolCount;
using sts::registry::kColorlessCombatPool;
// The three per-rarity combat CARD-REWARD pools: every RED non-BASIC
// card split by rarity (AbstractDungeon.initializeCardPools ->
// CardLibrary.addRedCards, CardLibrary.java:1152-1161). No type filter and no
// healing exclusion -- those are the ATTACK transform pool's, not these.
// ORDER is plain CardLibrary iteration order (addToTop appends, CardGroup.java:
// 455-457, so no reversal), and it is PINNED against a live oracle capture that
// reproduces all 27 captured card-reward offer identities -- see the emission
// block in tools/registry_gen/stsgen/emit/cards.py. (This comment used to claim
// the same registry-id-order deviation as kIroncladAttackPool; that claim was
// stale in both places.)
using sts::registry::kIroncladCommonPoolCount;
using sts::registry::kIroncladCommonPool;
using sts::registry::kIroncladUncommonPoolCount;
using sts::registry::kIroncladUncommonPool;
using sts::registry::kIroncladRarePoolCount;
using sts::registry::kIroncladRarePool;
// The nine TYPE-FILTERED views of the three pools above, one per
// (rarity, ATTACK|SKILL|POWER). getCardFromPool(rarity, type, useRng)
// (AbstractDungeon.java:1538-1577) reaches CardGroup.getRandomCard(type,
// useRng) (CardGroup.java:539-552), which filters by type, SORTS the filtered
// view (AbstractCard.compareTo == cardID compare) and indexes it with cardRng.
// Because of that sort these are ORDER-EXACT -- they do NOT inherit the
// registry-id ordering deviation the unsorted pools above carry. The shop's
// five colored slots are the only S1 consumer. kIroncladCommonPowerPool is
// deliberately EMPTY: the Ironclad has no common POWER, which is what makes
// getCardFromPool's POWER branch recurse to the next rarity without spending a
// draw.
using sts::registry::kIroncladCommonAttackPoolCount;
using sts::registry::kIroncladCommonAttackPool;
using sts::registry::kIroncladCommonSkillPoolCount;
using sts::registry::kIroncladCommonSkillPool;
using sts::registry::kIroncladCommonPowerPoolCount;
using sts::registry::kIroncladCommonPowerPool;
using sts::registry::kIroncladUncommonAttackPoolCount;
using sts::registry::kIroncladUncommonAttackPool;
using sts::registry::kIroncladUncommonSkillPoolCount;
using sts::registry::kIroncladUncommonSkillPool;
using sts::registry::kIroncladUncommonPowerPoolCount;
using sts::registry::kIroncladUncommonPowerPool;
using sts::registry::kIroncladRareAttackPoolCount;
using sts::registry::kIroncladRareAttackPool;
using sts::registry::kIroncladRareSkillPoolCount;
using sts::registry::kIroncladRareSkillPool;
using sts::registry::kIroncladRarePowerPoolCount;
using sts::registry::kIroncladRarePowerPool;
// The dungeon COLORLESS card pool (AbstractDungeon.addColorlessCards,
// AbstractDungeon.java:1203-1210: every COLORLESS card that is neither BASIC
// nor SPECIAL rarity nor a STATUS). The two rarity views are what
// getColorlessCardFromPool indexes (AbstractDungeon.java:1579-1595), and they
// are ORDER-EXACT rather than deviating: CardGroup.getRandomCard(useRng,
// rarity) SORTS its rarity-filtered view and AbstractCard.compareTo compares
// cardID strings (CardGroup.java:509-524; AbstractCard.java:2583-2584), so the
// order is derivable from the registry's game_id column alone. Those draws come
// off cardRng, whichever caller asked. The unsorted whole-pool view feeds
// returnTrulyRandomColorlessCardFromAvailable (:998-1014) and DOES carry the
// same interim order deviation as the RED pools above.
using sts::registry::kColorlessUncommonPoolCount;
using sts::registry::kColorlessUncommonPool;
using sts::registry::kColorlessRarePoolCount;
using sts::registry::kColorlessRarePool;
using sts::registry::kColorlessPoolCount;
using sts::registry::kColorlessPool;

// --- The card table (generated from registry/cards.yaml) ---------------------
// Each entry mirrors its use()'s addToBot order exactly; provenance is cited
// per-entry in the YAML.

using sts::registry::kStrike;
using sts::registry::kDefend;
using sts::registry::kBash;
using sts::registry::kShrugItOff;
using sts::registry::kPommelStrike;

// Lookup by CardId. Returns nullptr for CardId::NONE or any id without a
// registry row (a defensive guard; card_play asserts a real card was played).
using sts::registry::card_def;

// --- Upgrade plumbing (two-row lookup by the `upgrade` bit) ------------------
// The generated CardDef carries BOTH a base program (steps/step_count/base_cost/
// flags) and an upgraded program (upgraded_steps/upgraded_step_count/
// upgraded_cost/upgraded_flags). Every row for a card the game can upgrade
// (every type except STATUS/CURSE -- AbstractCard.canUpgrade,
// AbstractCard.java:672-680) MUST author its `upgraded:` block; the generator
// fails generation on a missing one (stsgen/emit/cards.py). Only STATUS/CURSE
// rows may omit it, emitting upgraded == base -- a pair no campfire/Neow
// upgrade can reach. (The old silent upgraded==base fallback for any row is
// how the G6 §8.0 Strike+/Defend+ divergence shipped.) `upgrade` is a COUNT
// (CardInstance.upgrade, u8): 0 == base, >0 == upgraded. (Infinitely-upgradeable
// Searing Blow needs the count rather than a bit -- storage is already a u8
// count, so it is covered.)

// The effect steps (pointer + count) for a card at the given upgrade level.
struct CardEffectView {
    const CardEffectStep* steps;
    uint8_t count;
};

[[nodiscard]] inline CardEffectView card_effect_steps(const CardDef& def,
                                                      uint8_t upgrade) noexcept {
    if (upgrade > 0) {
        return CardEffectView{def.upgraded_steps.data(), def.upgraded_step_count};
    }
    return CardEffectView{def.steps.data(), def.step_count};
}

// The energy cost for a card at the given upgrade level.
[[nodiscard]] inline uint8_t card_cost(const CardDef& def, uint8_t upgrade) noexcept {
    return upgrade > 0 ? def.upgraded_cost : def.base_cost;
}

// The CardFlag bitset for a card at the given upgrade level.
[[nodiscard]] inline uint16_t card_flags(const CardDef& def, uint8_t upgrade) noexcept {
    return upgrade > 0 ? def.upgraded_flags : def.flags;
}

// Card-level targeting at the given upgrade level. Blind+ and Trip+ reassign
// this.target to CardTarget.ALL_ENEMY in upgrade() (Blind.java:48 /
// Trip.java:53) -- the ONLY two cards in the whole game that reassign
// `this.target` inside upgrade() (whole-tree grep, 2026-07-28), so the column
// is bounded forever. Every targeting consumer must go through these, never
// def.target_kind / def.needs_target directly: the ActionMask row shape
// (advance.cpp legal_actions), card_can_use's dead-target rejection, and
// resolve_card_play's GameActionManager.java:264-283 ENEMY-only suppression
// all read the UPGRADE-AWARE kind, because the game reads the live
// `card.target` field at each of those sites. (`random_target` is deliberately
// NOT upgrade-aware: no card changes it in upgrade(), and the generator
// refuses an `upgraded_target:` that would.)
[[nodiscard]] inline CardTargetKind card_target_kind(const CardDef& def,
                                                     uint8_t upgrade) noexcept {
    return upgrade > 0 ? def.upgraded_target_kind : def.target_kind;
}
[[nodiscard]] inline bool card_needs_target(const CardDef& def,
                                            uint8_t upgrade) noexcept {
    return upgrade > 0 ? def.upgraded_needs_target : def.needs_target;
}

// The card's triggerOnExhaust program at the given upgrade level
// (CardGroup.moveToExhaustPile:857 fires card.triggerOnExhaust AFTER the relic
// and power onExhaust hooks; Sentinel is the S1 consumer -- addToTop
// GainEnergyAction 2/3, Sentinel.java:37-43). Empty (count 0) for every card
// without an `on_exhaust:` block.
[[nodiscard]] inline CardEffectView card_on_exhaust_steps(
    const CardDef& def, uint8_t upgrade) noexcept {
    if (upgrade > 0) {
        return CardEffectView{def.upgraded_on_exhaust_steps.data(),
                              def.upgraded_on_exhaust_step_count};
    }
    return CardEffectView{def.on_exhaust_steps.data(),
                          def.on_exhaust_step_count};
}

// --- Drift pins ---------------------------------------------------------------
// The generated table encodes ops as sts::registry::Opcode (its standalone
// mirror of interp.hpp's set) and APPLY_POWER power ids in `extra` via the
// make_apply_power_flags packing. Pin both equal to the engine's so the
// generator's vocabulary cannot silently diverge from the interpreter's.

static_assert(
    static_cast<uint16_t>(sts::registry::Opcode::NOP) ==
            static_cast<uint16_t>(Opcode::NOP) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE) ==
            static_cast<uint16_t>(Opcode::DAMAGE) &&
        static_cast<uint16_t>(sts::registry::Opcode::BLOCK) ==
            static_cast<uint16_t>(Opcode::BLOCK) &&
        static_cast<uint16_t>(sts::registry::Opcode::APPLY_POWER) ==
            static_cast<uint16_t>(Opcode::APPLY_POWER) &&
        static_cast<uint16_t>(sts::registry::Opcode::DRAW) ==
            static_cast<uint16_t>(Opcode::DRAW) &&
        static_cast<uint16_t>(sts::registry::Opcode::GAIN_ENERGY) ==
            static_cast<uint16_t>(Opcode::GAIN_ENERGY) &&
        static_cast<uint16_t>(sts::registry::Opcode::SHUFFLE_IN) ==
            static_cast<uint16_t>(Opcode::SHUFFLE_IN) &&
        static_cast<uint16_t>(sts::registry::Opcode::EXHAUST) ==
            static_cast<uint16_t>(Opcode::EXHAUST) &&
        static_cast<uint16_t>(sts::registry::Opcode::ROLL_MOVE) ==
            static_cast<uint16_t>(Opcode::ROLL_MOVE) &&
        static_cast<uint16_t>(sts::registry::Opcode::MAKE_CARD) ==
            static_cast<uint16_t>(Opcode::MAKE_CARD) &&
        static_cast<uint16_t>(sts::registry::Opcode::SET_COST) ==
            static_cast<uint16_t>(Opcode::SET_COST) &&
        static_cast<uint16_t>(sts::registry::Opcode::LOSE_HP) ==
            static_cast<uint16_t>(Opcode::LOSE_HP) &&
        static_cast<uint16_t>(sts::registry::Opcode::CHOOSE_CARD) ==
            static_cast<uint16_t>(Opcode::CHOOSE_CARD) &&
        static_cast<uint16_t>(sts::registry::Opcode::PLAY_TOP_DRAW) ==
            static_cast<uint16_t>(Opcode::PLAY_TOP_DRAW) &&
        static_cast<uint16_t>(sts::registry::Opcode::REMOVE_POWER) ==
            static_cast<uint16_t>(Opcode::REMOVE_POWER) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_BLOCK) ==
            static_cast<uint16_t>(Opcode::DAMAGE_BLOCK) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_STR_MULT) ==
            static_cast<uint16_t>(Opcode::DAMAGE_STR_MULT) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_PER_STRIKE) ==
            static_cast<uint16_t>(Opcode::DAMAGE_PER_STRIKE) &&
        static_cast<uint16_t>(sts::registry::Opcode::LOSE_HP_PER_HAND) ==
            static_cast<uint16_t>(Opcode::LOSE_HP_PER_HAND) &&
        static_cast<uint16_t>(sts::registry::Opcode::DISCARD_HAND) ==
            static_cast<uint16_t>(Opcode::DISCARD_HAND) &&
        static_cast<uint16_t>(sts::registry::Opcode::REDUCE_POWER) ==
            static_cast<uint16_t>(Opcode::REDUCE_POWER) &&
        static_cast<uint16_t>(sts::registry::Opcode::DROPKICK) ==
            static_cast<uint16_t>(Opcode::DROPKICK) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_UPGRADE_SCALE) ==
            static_cast<uint16_t>(Opcode::DAMAGE_UPGRADE_SCALE) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_RAMPAGE) ==
            static_cast<uint16_t>(Opcode::DAMAGE_RAMPAGE) &&
        static_cast<uint16_t>(sts::registry::Opcode::EXHAUST_NON_ATTACKS) ==
            static_cast<uint16_t>(Opcode::EXHAUST_NON_ATTACKS) &&
        static_cast<uint16_t>(sts::registry::Opcode::CANNOT_LOSE) ==
            static_cast<uint16_t>(Opcode::CANNOT_LOSE) &&
        static_cast<uint16_t>(sts::registry::Opcode::CAN_LOSE) ==
            static_cast<uint16_t>(Opcode::CAN_LOSE) &&
        static_cast<uint16_t>(sts::registry::Opcode::SUICIDE) ==
            static_cast<uint16_t>(Opcode::SUICIDE) &&
        static_cast<uint16_t>(sts::registry::Opcode::SPAWN_MONSTER) ==
            static_cast<uint16_t>(Opcode::SPAWN_MONSTER) &&
        static_cast<uint16_t>(sts::registry::Opcode::SET_MOVE) ==
            static_cast<uint16_t>(Opcode::SET_MOVE) &&
        static_cast<uint16_t>(sts::registry::Opcode::DOUBLE_BLOCK) ==
            static_cast<uint16_t>(Opcode::DOUBLE_BLOCK) &&
        static_cast<uint16_t>(sts::registry::Opcode::BLOCK_PER_NON_ATTACK) ==
            static_cast<uint16_t>(Opcode::BLOCK_PER_NON_ATTACK) &&
        static_cast<uint16_t>(sts::registry::Opcode::SPOT_WEAKNESS) ==
            static_cast<uint16_t>(Opcode::SPOT_WEAKNESS) &&
        static_cast<uint16_t>(sts::registry::Opcode::RANDOM_ATTACK_TO_HAND) ==
            static_cast<uint16_t>(Opcode::RANDOM_ATTACK_TO_HAND) &&
        static_cast<uint16_t>(sts::registry::Opcode::PLAY_CARD) ==
            static_cast<uint16_t>(Opcode::PLAY_CARD) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_FEED) ==
            static_cast<uint16_t>(Opcode::DAMAGE_FEED) &&
        static_cast<uint16_t>(sts::registry::Opcode::FIEND_FIRE) ==
            static_cast<uint16_t>(Opcode::FIEND_FIRE) &&
        static_cast<uint16_t>(sts::registry::Opcode::DOUBLE_STRENGTH) ==
            static_cast<uint16_t>(Opcode::DOUBLE_STRENGTH) &&
        static_cast<uint16_t>(sts::registry::Opcode::VAMPIRE_DAMAGE_ALL) ==
            static_cast<uint16_t>(Opcode::VAMPIRE_DAMAGE_ALL) &&
        static_cast<uint16_t>(sts::registry::Opcode::HEAL) ==
            static_cast<uint16_t>(Opcode::HEAL) &&
        static_cast<uint16_t>(sts::registry::Opcode::ESCAPE) ==
            static_cast<uint16_t>(Opcode::ESCAPE) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_DRAW_PILE) ==
            static_cast<uint16_t>(Opcode::DAMAGE_DRAW_PILE) &&
        static_cast<uint16_t>(sts::registry::Opcode::CONDITIONAL_DRAW) ==
            static_cast<uint16_t>(Opcode::CONDITIONAL_DRAW) &&
        static_cast<uint16_t>(sts::registry::Opcode::RESHUFFLE_ALL) ==
            static_cast<uint16_t>(Opcode::RESHUFFLE_ALL) &&
        static_cast<uint16_t>(sts::registry::Opcode::MADNESS) ==
            static_cast<uint16_t>(Opcode::MADNESS) &&
        static_cast<uint16_t>(sts::registry::Opcode::DARK_SHACKLES) ==
            static_cast<uint16_t>(Opcode::DARK_SHACKLES) &&
        static_cast<uint16_t>(sts::registry::Opcode::DISCOVERY) ==
            static_cast<uint16_t>(Opcode::DISCOVERY) &&
        static_cast<uint16_t>(sts::registry::Opcode::ENLIGHTENMENT) ==
            static_cast<uint16_t>(Opcode::ENLIGHTENMENT) &&
        static_cast<uint16_t>(
            sts::registry::Opcode::RANDOM_COLORLESS_TO_HAND) ==
            static_cast<uint16_t>(Opcode::RANDOM_COLORLESS_TO_HAND) &&
        static_cast<uint16_t>(sts::registry::Opcode::USE_CARD) ==
            static_cast<uint16_t>(Opcode::USE_CARD) &&
        static_cast<uint16_t>(sts::registry::Opcode::UPGRADE_ALL) ==
            static_cast<uint16_t>(Opcode::UPGRADE_ALL) &&
        static_cast<uint16_t>(sts::registry::Opcode::DRAW_PILE_FETCH) ==
            static_cast<uint16_t>(Opcode::DRAW_PILE_FETCH) &&
        static_cast<uint16_t>(sts::registry::Opcode::DAMAGE_GREED) ==
            static_cast<uint16_t>(Opcode::DAMAGE_GREED) &&
        static_cast<uint16_t>(sts::registry::Opcode::REMOVE_DEBUFFS) ==
            static_cast<uint16_t>(Opcode::REMOVE_DEBUFFS) &&
        static_cast<uint16_t>(sts::registry::Opcode::UPGRADE_RANDOM_CARD) ==
            static_cast<uint16_t>(Opcode::UPGRADE_RANDOM_CARD) &&
        static_cast<uint16_t>(sts::registry::Opcode::RANDOMIZE_HAND_COST) ==
            static_cast<uint16_t>(Opcode::RANDOMIZE_HAND_COST) &&
        static_cast<uint16_t>(sts::registry::Opcode::RED_SKULL_ENTRY) ==
            static_cast<uint16_t>(Opcode::RED_SKULL_ENTRY) &&
        static_cast<uint16_t>(sts::registry::Opcode::VAMPIRE_DAMAGE) ==
            static_cast<uint16_t>(Opcode::VAMPIRE_DAMAGE) &&
        static_cast<uint16_t>(sts::registry::Opcode::BLOCK_RANDOM_MONSTER) ==
            static_cast<uint16_t>(Opcode::BLOCK_RANDOM_MONSTER) &&
        static_cast<uint16_t>(sts::registry::Opcode::OBTAIN_CARD) ==
            static_cast<uint16_t>(Opcode::OBTAIN_CARD) &&
        static_cast<uint16_t>(sts::registry::Opcode::CLEAR_CARD_QUEUE) ==
            static_cast<uint16_t>(Opcode::CLEAR_CARD_QUEUE) &&
        static_cast<uint16_t>(sts::registry::Opcode::END_PLAYER_TURN) ==
            static_cast<uint16_t>(Opcode::END_PLAYER_TURN) &&
        static_cast<uint16_t>(sts::registry::Opcode::APPLY_STASIS) ==
            static_cast<uint16_t>(Opcode::APPLY_STASIS) &&
        static_cast<uint16_t>(sts::registry::Opcode::STASIS_RETURN) ==
            static_cast<uint16_t>(Opcode::STASIS_RETURN) &&
        static_cast<uint16_t>(sts::registry::Opcode::RITUAL_DAGGER) ==
            static_cast<uint16_t>(Opcode::RITUAL_DAGGER) &&
        static_cast<uint16_t>(sts::registry::Opcode::CODEX) ==
            static_cast<uint16_t>(Opcode::CODEX) &&
        static_cast<uint16_t>(sts::registry::Opcode::OBTAIN_POTION) ==
            static_cast<uint16_t>(Opcode::OBTAIN_POTION),
    "generated sts::registry::Opcode must stay byte-equal to interp.hpp's "
    "Opcode (design doc §6 numbering; append-only)");

static_assert(kBash.steps[1].extra == make_apply_power_flags(PowerId::VULNERABLE),
              "generated APPLY_POWER `extra` must use the make_apply_power_flags "
              "packing (interp.hpp)");
// The counter operand added to that packing (bits 16..31) DEFAULTS to 0, which
// is what every APPLY_POWER step packed before it existed. Bash is the pin: an
// operand-free row must stay byte-identical, not merely compile.
static_assert(kBash.steps[1].extra ==
                  static_cast<uint32_t>(
                      static_cast<uint16_t>(PowerId::VULNERABLE)),
              "an APPLY_POWER step that authors no `counter:` must pack the bare "
              "PowerId, unchanged from before the counter operand existed");

// Card-flag bit constants: the generated header emits kCardFlag*
// mirroring gen.py's CARD_FLAGS; pin them byte-equal to the engine's CardFlag
// (types.hpp) so the codegen vocabulary cannot drift from the interpreter's.
static_assert(
    sts::registry::kCardFlagExhaust == card_flag_bit(CardFlag::EXHAUST) &&
        sts::registry::kCardFlagEthereal == card_flag_bit(CardFlag::ETHEREAL) &&
        sts::registry::kCardFlagInnate == card_flag_bit(CardFlag::INNATE) &&
        sts::registry::kCardFlagUnplayable == card_flag_bit(CardFlag::UNPLAYABLE) &&
        sts::registry::kCardFlagRetain == card_flag_bit(CardFlag::RETAIN) &&
        sts::registry::kCardFlagXcost == card_flag_bit(CardFlag::XCOST) &&
        sts::registry::kCardFlagCostModifiedForTurn ==
            card_flag_bit(CardFlag::COST_MODIFIED_FOR_TURN) &&
        sts::registry::kCardFlagPurgeOnUse ==
            card_flag_bit(CardFlag::PURGE_ON_USE),
    "generated kCardFlag* must stay byte-equal to types.hpp's CardFlag "
    "(append-only; mirrored in gen.py CARD_FLAGS)");

}  // namespace sts::engine
