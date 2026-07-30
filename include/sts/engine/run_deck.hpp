#pragma once

// Master-deck editing hooks. Parasite.java:onRemoveFromMasterDeck calls
// AbstractCreature.decreaseMaxHealth(3); this helper makes that effect part of
// the actual removal transaction rather than a combat-only card special case.
// The ADD-side transaction fires every owned relic's onObtainCard in acquisition
// order, so future reward/shop/event card grants route through one door.

#include <cstdint>

#include "sts/engine/cards.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"

namespace sts::engine {

// --- The Bottled trio's per-instance marker (master-deck `flags`) ------------
//
// AbstractCard.inBottleFlame / inBottleLightning / inBottleTornado, the three
// per-INSTANCE booleans the bottles' onEquip grids set on one master-deck card
// (BottledFlame.java:63-77 and siblings). Their two consumers are
// CardGroup.initializeDeck's innate collection (CardGroup.java:928-950 -- the
// bottled instance is placed on top of the opening draw exactly as an Innate
// card is) and CardGroup.getGroupWithoutBottledCards (CardGroup.java:1084-1091
// -- the bottled instance is excluded from the purge/transform grids that read
// that filter).
//
// ENCODING DECISION (Wave-C track 2 stage 3). The marker lives in the
// otherwise-virgin `CardInstance.flags` of the MASTER-DECK row, as its own
// three low bits -- it is deliberately NOT a `CardFlag`:
//   * The master-deck `flags` field and the combat `card_pool[i].flags` field
//     share a struct member but have NO shared consumer: both combat builders
//     REBUILD combat flags from the registry (`card_flags(*def, up)`,
//     src/engine/run_advance.cpp step 3 / src/engine/advance.cpp), and nothing
//     writes a master-deck row's flags back from a combat instance (in-combat
//     upgrades are combat-local). So the two fields are distinct namespaces
//     already, and naming this one keeps `CardFlag` bit 15 -- the LAST free
//     combat flag bit (types.hpp:137-152) -- unspent.
//   * `CardInstance.misc` is NOT usable: the oracle emits the Java's real
//     per-instance `misc` counter (GameStateConverter.convertCardToJson) and
//     the translator maps it (translate.cpp parse_card), so bottle bits there
//     would collide with a genuine game field.
//   * Storing the target on the RELIC side (RelicSlot.counter = deck index)
//     would diverge from the game, which leaves BottledFlame.counter at
//     AbstractRelic's -1 -- the Centennial Puzzle divergence class
//     (combat_state.hpp:126-151).
// The bits were previously always zero, so no struct layout changes and no
// SCHEMA_VERSION bump (the CombatState.flags bit-4 precedent,
// combat_state.hpp:141-151). The differ compares master_deck[i].flags
// (tools/diff_harness/src/differ.cpp cmp_card_instance), so the bits are on
// the campaign-verdict surface: the fork's convertCardToJson emits the three
// booleans and the translator sets the same bits (absent keys -- every capture
// made before the fork change -- default to 0, which is correct because no
// existing capture takes a bottle).
//
// One bit per bottle rather than one shared bit because the Java keeps three
// distinct fields and the oracle can then validate WHICH bottle holds the
// card. A single card can never carry two of them: each bottle's grid is
// type-filtered (ATTACK/SKILL/POWER are disjoint) and relic pools never issue
// the same bottle twice.
inline constexpr uint16_t kMasterCardInBottleFlame = 1u << 0;
inline constexpr uint16_t kMasterCardInBottleLightning = 1u << 1;
inline constexpr uint16_t kMasterCardInBottleTornado = 1u << 2;
inline constexpr uint16_t kMasterCardBottleMask =
    kMasterCardInBottleFlame | kMasterCardInBottleLightning |
    kMasterCardInBottleTornado;

// Which bottle, when one is mid-choice or being applied. Values are the bit
// positions above plus one; NONE is the value-init default.
enum class MasterBottleKind : uint8_t {
    NONE = 0,
    FLAME = 1,      // Bottled Flame     -> kMasterCardInBottleFlame
    LIGHTNING = 2,  // Bottled Lightning -> kMasterCardInBottleLightning
    TORNADO = 3,    // Bottled Tornado   -> kMasterCardInBottleTornado
};

[[nodiscard]] constexpr uint16_t master_bottle_bit(MasterBottleKind k) noexcept {
    return static_cast<uint16_t>(
        k == MasterBottleKind::NONE
            ? 0u
            : 1u << (static_cast<unsigned>(k) - 1u));
}

// `c.inBottleFlame || c.inBottleLightning || c.inBottleTornado` -- the exact
// disjunction initializeDeck (CardGroup.java:938) and
// getGroupWithoutBottledCards (CardGroup.java:1087) both test.
[[nodiscard]] constexpr bool master_card_bottled(
    const CardInstance& card) noexcept {
    return (card.flags & kMasterCardBottleMask) != 0u;
}

// The card type each bottle's grid filters on (BottledFlame.java:43/:51
// getAttacks / BottledLightning getSkills / BottledTornado getPowers).
[[nodiscard]] constexpr CardType master_bottle_card_type(
    MasterBottleKind k) noexcept {
    switch (k) {
        case MasterBottleKind::FLAME:
            return CardType::ATTACK;
        case MasterBottleKind::LIGHTNING:
            return CardType::SKILL;
        case MasterBottleKind::TORNADO:
            return CardType::POWER;
        case MasterBottleKind::NONE:
        default:
            return CardType::CURSE;  // matches no bottle-eligible card
    }
}

// The relics' onObtainCard pass over a just-appended master-deck row: every owned
// relic, in ACQUISITION order (design §4.3 / stage-a trap 8), gets its
// onObtainCard body run against `card`. Defined in run_deck.cpp, where the
// RelicId -> handler table is GENERATED from registry/relics.yaml `pickup:
// on_obtain_card` -- the per-relic bodies live in per-tier translation units
// under src/engine/relics/, not in this header, so adding a relic with an
// obtain-time effect neither edits a shared switch nor rebuilds this header's
// consumers.
void dispatch_relics_on_obtain_card(RunState& run, CardInstance& card,
                                    const CardDef& def) noexcept;

// AbstractRelic.onMasterDeckChange, fired from every master-deck edit. Du-Vu
// Doll is the only S1 override (DuVuDoll.java:43-53): its counter is the
// number of CURSE-type cards in the master deck, RECOMPUTED from scratch each
// time rather than adjusted by the delta -- which is what the Java does, and is
// also what keeps a RunState assembled by hand (tests, the fixture loader)
// consistent without a separate seeding step. Inline rather than a generated
// dispatch surface: one relic does not justify a fourth pickup table, and the
// row's `pickup: on_equip` already covers the acquisition-time half.
inline void dispatch_relics_on_master_deck_change(RunState& run) noexcept {
    int16_t curses = 0;
    for (uint16_t i = 0; i < run.master_deck_count; ++i) {
        const CardDef* def =
            card_def(static_cast<CardId>(run.master_deck[i].card_id));
        if (def != nullptr && def->type == CardType::CURSE) {
            ++curses;
        }
    }
    for (uint8_t i = 0; i < run.relic_count; ++i) {
        if (run.relics[i].relic_id ==
            static_cast<uint16_t>(RelicId::DU_VU_DOLL)) {
            run.relics[i].counter = curses;
        }
    }
}

// ShowCardAndObtainEffect's CONSTRUCTOR-time gate. A curse is offered to the
// first owned Omamori; a nonzero charge consumes the effect immediately.
// Keeping this gate separable from the later append matters for Big Fish: its
// curse effect is constructed before the event obtains its relic, but the card
// joins the deck after that relic is already owned. Thus a newly rolled
// Omamori cannot eat the pending Regret, while a newly rolled Darkstone Periapt
// does see Regret's later onObtainCard pass.
[[nodiscard]] inline bool omamori_blocks_card_obtain(RunState& run,
                                                     CardId id) noexcept {
    const CardDef* def = card_def(id);
    if (def == nullptr || def->type != CardType::CURSE) {
        return false;
    }
    for (uint8_t i = 0; i < run.relic_count; ++i) {
        RelicSlot& slot = run.relics[i];
        if (slot.relic_id == static_cast<uint16_t>(RelicId::OMAMORI)) {
            if (slot.counter != 0) {
                --slot.counter;
                return true;
            }
            break;  // getRelic returns the first copy, even when used up.
        }
    }
    return false;
}

// The UPDATE-time half of ShowCardAndObtainEffect after its constructor gate
// has already allowed the card: append, onObtainCard in relic acquisition
// order, then onMasterDeckChange. Callers normally want the combined door
// below; staged effects such as Big Fish deliberately call the two halves at
// their separate Java-visible times.
[[nodiscard]] inline bool add_card_to_master_deck_after_omamori(
    RunState& run, CardId id, uint8_t upgrade = 0) noexcept {
    const CardDef* def = card_def(id);
    if (def == nullptr) {
        return false;
    }
    if (run.master_deck_count >= kMasterDeckCap) {
        return false;
    }
    CardInstance& c = run.master_deck[run.master_deck_count];
    c = CardInstance{};
    c.card_id = static_cast<uint16_t>(id);
    c.upgrade = upgrade;
    ++run.master_deck_count;

    dispatch_relics_on_obtain_card(run, c, *def);
    dispatch_relics_on_master_deck_change(run);
    return true;
}

// ShowCardAndObtainEffect's ordinary collapsed acquisition door: the
// constructor-time Omamori check followed immediately by the later append and
// relic passes. Returns false only when an unblocked card cannot be appended.
[[nodiscard]] inline bool add_card_to_master_deck(RunState& run, CardId id,
                                                  uint8_t upgrade = 0) noexcept {
    if (omamori_blocks_card_obtain(run, id)) {
        return true;
    }
    return add_card_to_master_deck_after_omamori(run, id, upgrade);
}

// CardGroup.addToTop -- the card lands at master-deck INDEX 0 and every later
// row shifts up. Only one Act-1 site uses it: NoteForYourself.buttonEffect
// (NoteForYourself.java:56-66), which also bypasses ShowCardAndObtainEffect
// entirely -- it runs every relic's onObtainCard by hand BEFORE the insert and
// onMasterDeckChange after it, so there is no Omamori door on this path (and
// the card it grants is never a curse). Deck ORDER is observable: grids and the
// event board index the master deck positionally.
[[nodiscard]] inline bool add_card_to_master_deck_top(
    RunState& run, CardId id, uint8_t upgrade = 0) noexcept {
    const CardDef* def = card_def(id);
    if (def == nullptr || run.master_deck_count >= kMasterDeckCap) {
        return false;
    }
    CardInstance c{};
    c.card_id = static_cast<uint16_t>(id);
    c.upgrade = upgrade;
    dispatch_relics_on_obtain_card(run, c, *def);
    for (uint16_t i = run.master_deck_count; i > 0; --i) {
        run.master_deck[i] = run.master_deck[static_cast<uint16_t>(i - 1)];
    }
    run.master_deck[0] = c;
    ++run.master_deck_count;
    dispatch_relics_on_master_deck_change(run);
    return true;
}

// Removes one master-deck row, preserving order. Returns false for an invalid
// row without changing the run. The generated per-card removal field is zero
// for every poolable curse except Parasite.
[[nodiscard]] inline bool remove_master_deck_card(RunState& run,
                                                   uint16_t index) noexcept {
    if (index >= run.master_deck_count) {
        return false;
    }

    const CardDef* def =
        card_def(static_cast<CardId>(run.master_deck[index].card_id));
    const int loss = def == nullptr ? 0 : def->on_remove_max_hp_loss;

    for (uint16_t i = static_cast<uint16_t>(index + 1);
         i < run.master_deck_count; ++i) {
        run.master_deck[static_cast<uint16_t>(i - 1)] = run.master_deck[i];
    }
    --run.master_deck_count;
    run.master_deck[run.master_deck_count] = CardInstance{};

    if (loss != 0) {
        const int adjusted = static_cast<int>(run.max_hp) - loss;
        run.max_hp = static_cast<int16_t>(adjusted < 1 ? 1 : adjusted);
        if (run.hp > run.max_hp) {
            run.hp = run.max_hp;
        }
    }
    dispatch_relics_on_master_deck_change(run);
    return true;
}

}  // namespace sts::engine
