#pragma once

// combat_vitals.hpp -- the INDEX-NORMALISED combat-vitals projection, and the
// comparison over it, behind `replay_run_diff --vitals`.
//
// THE GAP THIS CLOSES. `--replay` compares RUN-level state at every record
// (hp, gold, relics, deck, potions, map, streams). A combat-internal drift --
// the sim dealing a different number to a monster, a Time Warp counter one
// off, a randomly generated card differing in hand -- is invisible to that
// compare until it surfaces as a run-level symptom several records later (a
// monster killed a play early, a different draw, a follower mismatch), far
// downstream of the cause. The raw `--combat` CombatState walk cannot be the
// answer: its piles are index lists into an engine-internal `card_pool` whose
// layout the translator lays out deterministically but differently from a live
// sim (every record differs from the first card play on), and it is a
// diagnosis print by design.
//
// THE PROJECTION. Both sides -- the translated capture and the live sim -- are
// projected to the same plain-data `CombatVitals`: scalars (turn, player block
// and energy), each monster by SLOT (identity, hp, block, the two liveness
// flags) and every power list and every pile as a MULTISET keyed by game id
// (powers: id -> the sorted amounts of every instance with that id; cards:
// (id, upgrades) -> count). Pool indices and pile order never enter the
// projection, so the two sides can only differ on what the game's rules make
// observable. Draw-pile ORDER is hidden (PROTOCOL.md §3.10: the dump's draw
// pile is not the shuffled order), so contents are exactly what may be
// compared there; for hand/discard/exhaust order carries no rule either.
//
// The capture side is filled by the translator DURING its typed walk (the same
// parse that builds CombatState, so the id join is the registry join every
// other consumer gets, TheBombPower normalisation included) and lands on
// `TranslatedRecord::vitals`; the sim side is `vitals_from_combat_state` over
// the live CombatState. Unresolved ids (an id the registry does not know, under
// tolerant translation) keep their RAW capture string with `known == false`,
// so the compare can REPORT them by name rather than drop the slot -- strict
// translation, `--replay`'s mode, aborts the file on the first one. The SIM
// side has one unresolved shape of its own: a `NONE` id inside a live count,
// which is a slot some engine body zeroed in place without compacting the
// list. It carries an empty `id` with `known == false` and renders as
// `?<none>`, so a stale slot is a reported row and not an invisible one.
//
// WHAT IS DELIBERATELY NOT COMPARED, and why (each an exclusion, not a paper):
//   * a GONE monster's block and powers. `AbstractMonster.update` clears
//     `powers` only when its death animation's `deathTimer` expires (:867-873,
//     1.0-1.8 wall-clock seconds after die()), so a dump taken inside that
//     window still lists them and one taken after does not; nothing rule-level
//     reads a dead monster's block or powers. A HALF-DEAD monster (Darkling,
//     Awakened One phase 1) is `isDeadOrEscaped` too but still takes its turn
//     and keeps its powers, so its block and powers ARE compared.
//   * per-card cost / misc / flag state, and power `counter`s. Neither is on
//     the vitals bar this projection answers; both remain in the raw
//     `--combat` walk.
//   * monster intent / move history: `move_id` is compared by the raw walk and
//     the intent banner is display-derived (PROTOCOL.md §3.12).
//   * on a HAND_SELECT record, a card the CAPTURE'S hand is missing. The action
//     that opens `HandCardSelectScreen` first REMOVES its ineligible cards from
//     `p.hand` into a private list of its own and appends them back in
//     `returnCards()` afterwards (ArmamentsAction.java:45-91 is the one this
//     campaign hits: every `cannotUpgrade` card -- a curse, an already-upgraded
//     card -- leaves the hand for the duration of the prompt). The protocol
//     serializes neither that list nor the opening action, and `screen_state
//     .hand` is the same filtered `p.hand` group as `combat_state.hand`, so
//     those cards are simply not in the dump. The engine keeps the whole hand
//     and filters by eligibility at choice time instead
//     (interp_cards.cpp's `choice_slot_eligible`), so the sim legitimately
//     holds them. The hand is therefore compared as CONTAINMENT on such a
//     record -- `CombatVitals::hand_partial` -- : a key the capture has MORE of
//     is still a divergence and still reported, a key the SIM has more of is
//     the held-aside remainder and is not judged. Every other pile, and the
//     hand on every non-HAND_SELECT record, compares as an exact multiset.
//
// Dependency shape: engine + generated registry only (no JSON) -- the
// translator, which does own the JSON, is what fills the capture side.
//
// HOW THIS IS VERIFIED: by real artifacts, not by a unit test. Every exclusion
// above was written because a captured run produced the row, and the evidence
// of record is `--vitals` over whole campaign JSONL -- the committed three-act
// CI corpus and the campaign captures named in this change's commit body. A
// projection can be made blind by construction (a key that folds two cards
// together, a slot skipped because a monster is dead) and only a real run has
// the variety to catch that; a constructed state proves only what it was built
// to prove.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sts/engine/combat_state.hpp"

namespace sts::translate {

// One power instance. `id` is the registry game id (the capture's string for a
// known power; the RAW capture string when unresolved). Instanced powers (The
// Bomb) appear once per instance; the compare groups by id.
struct VitalsPower {
    std::string id;
    int amount = 0;
    bool known = true;
};

// One card instance, by identity: (game id, timesUpgraded).
struct VitalsCard {
    std::string id;
    int upgrades = 0;
    bool known = true;
};

struct VitalsMonster {
    std::string id;
    bool known = true;
    int hp = 0;
    int block = 0;
    bool half_dead = false;  // AbstractMonster.halfDead
    bool gone = false;       // AbstractCreature.isDeadOrEscaped
    std::vector<VitalsPower> powers;
};

struct CombatVitals {
    int turn = 0;
    int player_block = 0;
    int player_energy = 0;
    std::vector<VitalsPower> player_powers;
    std::vector<VitalsMonster> monsters;  // by slot, dead/escaped included
    std::vector<VitalsCard> hand;
    std::vector<VitalsCard> draw;
    std::vector<VitalsCard> discard;
    std::vector<VitalsCard> exhaust;
    // The in-flight cards: the game's `limbo` group plus `card_in_play`
    // (player.cardInUse), which is where a card sits while a hand-select it
    // opened is up; the sim's limbo pile is the same set.
    std::vector<VitalsCard> limbo;
    // Set on the CAPTURE side only, by the translator, for a HAND_SELECT
    // record: `hand` is then the opening action's ELIGIBLE subset (the
    // exclusion above), so the compare judges it by containment rather than
    // equality. Never set by `vitals_from_combat_state` -- the sim's hand is
    // always whole.
    bool hand_partial = false;
};

// The sim side of the compare: project a live CombatState. Monster liveness
// uses the engine's own predicates (combat_state.hpp): gone ==
// monster_dead_or_escaped, half_dead == monster_half_dead. Ids are rendered
// through the generated game-id tables; a NONE id inside a live count is
// carried as unknown (empty id, known == false) rather than skipped.
[[nodiscard]] CombatVitals vitals_from_combat_state(const engine::CombatState& s);

// One differing field, rendered GAME -> SIM like sts::diff::FieldDiff.
struct VitalsFieldDiff {
    std::string field;
    std::string game;
    std::string sim;
};

struct VitalsReport {
    std::vector<VitalsFieldDiff> diffs;
    // Every unresolved id met on either side, as "<domain>:<id>" (domain in
    // {power, card, monster}), deduplicated and sorted -- the caller reports
    // each once per file.
    std::vector<std::string> unknown_ids;

    [[nodiscard]] bool empty() const noexcept { return diffs.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return diffs.size(); }
    // Multi-line "field: game -> sim" rendering, one row per line.
    [[nodiscard]] std::string to_string() const;
};

// Compare the capture's vitals (`game`) against the sim's (`sim`). Field names:
//   turn, player.block, player.energy, player.powers[<id>],
//   monsters.count, monsters[i].id / .hp / .gone / .half_dead / .block /
//   .powers[<id>], and <pile>[<card key>] for hand / draw / discard / exhaust
//   / limbo, where the card key is "<id>", "<id>+" or "<id>+N" by upgrades.
// A power row's value is the amount, or "[a|b]" for several instances, or
// "(absent)"; a pile row's value is the count of that key.
[[nodiscard]] VitalsReport diff_combat_vitals(const CombatVitals& game,
                                              const CombatVitals& sim);

}  // namespace sts::translate
