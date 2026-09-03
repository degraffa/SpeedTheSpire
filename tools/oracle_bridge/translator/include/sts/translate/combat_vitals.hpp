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
//   * per-card misc / flag state, and power `counter`s. Neither is on the
//     vitals bar this projection answers; both remain in the raw `--combat`
//     walk. Per-card COST is no longer on this list -- it has its own compare,
//     `diff_combat_costs`, over the same projection; see the block below.
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

// One card instance, by identity: (game id, timesUpgraded), plus the DISPLAYED
// energy cost the `--costs` compare (below) reads. `--vitals` never looks at
// `cost`: its multiset key is (id, upgrades) and stays exactly that.
struct VitalsCard {
    std::string id;
    int upgrades = 0;
    bool known = true;
    // AbstractCard.costForTurn AS THE GAME REPORTS IT, sentinels included:
    // -1 == X-cost, -2 == unplayable, otherwise the energy actually charged.
    // Capture side: `cost` straight out of the dump (GameStateConverter
    // .convertCardToJson:822), taken BEFORE the translator's clamp of the
    // sentinels to CardInstance::cost_now's unsigned 0. Sim side: the same
    // number reconstructed from the live instance -- -1 for an XCOST row, -2
    // for an UNPLAYABLE one, else `cost_now` -- because the registry maps the
    // game's two negative cost sentinels onto those flags with base_cost 0
    // (tools/registry_gen/stsgen/emit/cards.py `parse_card_flags`), so a
    // sentinel is stored as a flag and a real cost as a number.
    int cost = 0;
    // False on a capture card whose dump carried NO `cost` key. The stock
    // converter always emits one, so this is only ever false for a capture
    // made by some future/older emitter; `CombatVitals::costs_available`
    // aggregates it per record and the compare declines such a record rather
    // than reading a defaulted 0 as a claim. Always true on the sim side.
    bool cost_known = true;
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
    // Set on the CAPTURE side by the translator: every card of every pile in
    // this record carried a `cost` key, so `diff_combat_costs` may read them.
    // A record where it is false is DECLINED by the costs compare and counted,
    // never compared against a defaulted zero. Always true on the sim side.
    bool costs_available = true;
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
    // `diff_combat_costs` only: the rows it recognised as the game's
    // ANIMATION-DEFERRED `resetAttributes` and therefore kept OUT of `diffs`.
    // They are carried rather than dropped so the caller can count them, name
    // them under --verbose, and never mistake "tolerated" for "not there".
    // Always empty from `diff_combat_vitals`.
    std::vector<VitalsFieldDiff> tolerated;
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

// ---- the in-combat COST compare (S3.53 (a), behind `--costs`) --------------
//
// THE GAP THIS CLOSES. `--replay` compares RunState, which has no in-combat
// card at all, and the two combat-side compares above it both look away from
// cost by construction: `--vitals` keys its piles on (id, upgrades) and says so,
// and `--combat`'s raw CombatState walk is index-sensitive and is a diagnosis
// print, never a verdict. So no acceptance surface in this repository has ever
// compared a card's live cost against a capture -- which is how a whole
// cost-state family (Corruption's combat-persistent zero, the
// COST_MODIFIED_FOR_TURN / SAVED_BASE_COST bookkeeping, and the
// `resetAttributes`-on-every-move-into-draw/discard rule) reached the S2 depth
// wave undetected (s2-verification.md §9 limit 2).
//
// WHAT IS COMPARED. Per pile (hand / draw / discard / exhaust / limbo), the
// cards are grouped by the SAME (id, upgrades) key `--vitals` uses and each
// group's costs are compared as a SORTED MULTISET. Two Strikes at 1 and 0 read
// `[0|1]`, so one instance's drift is one row that names both cost lists --
// rather than the two phantom rows a (id, upgrades, cost) key would produce
// (`Strike` 2 -> 1 and `Strike@0` 0 -> 1), neither of which says "cost".
// Pile order never enters it, exactly as in `--vitals`: the dump's draw pile is
// not the shuffled order (PROTOCOL.md §3.10), so a positional cost compare
// there would be comparing the protocol's shuffle, not the rules.
//
// WHAT THE DUMP CANNOT SUPPLY, stated precisely because it bounds the claim.
// `convertCardToJson` emits exactly ONE cost number per card, `card.costForTurn`
// (:822). It does NOT emit `AbstractCard.cost` (the combat BASE cost a permanent
// writer -- Confusion, Blood for Blood, Enlightenment+ -- moves), nor
// `isCostModified` / `isCostModifiedForTurn`, nor `freeToPlayOnce`. So the
// engine's SAVED_BASE_COST payload, its COST_MODIFIED_FOR_TURN bit and its
// FREE_TO_PLAY_ONCE bit are each only OBSERVABLE here through the costForTurn
// they later produce -- a mis-set bit is caught at the reset that restores the
// wrong number, one record later, not at the moment it is set. Closing that
// last gap needs three more fields from the fork; it is a filed emitter gap,
// not something this compare can infer.
//
// THE ONE TOLERATED SHAPE, and why it is not an exclusion of convenience.
// Two of the three seams at which the game runs `resetAttributes` are
// ANIMATION-DEFERRED, so the CAPTURE is behind the rules on exactly those
// transitions while the headless model, which collapses the animation, is
// already at the settled value:
//
//   * a move into the DRAW or DISCARD pile hands the card to a `Soul`, which
//     reaches its pile a beat later and only then runs `clearPowers() ->
//     resetAttributes()` (Soul.java:193-231; piles.hpp `reset_cost_for_turn`
//     has the full seam list). The recorded case is STS101166 floor 20:
//     `Bash+(cost 0)` in the discard at seq 330 and `Bash+(cost 2)` at 331.
//   * an EXHAUST runs through `ExhaustCardEffect.update:41-43`, an
//     AbstractGameEffect with a duration, and resets when it expires.
//
// The third seam -- the end-turn sweep over draw/discard/hand
// (AbstractRoom.endTurn:397-405) -- is a direct call, not an effect, so it is
// NOT deferred; the HAND and LIMBO are therefore never excused here.
//
// So a row is classified as the deferred reset, moved to `tolerated` and kept
// out of `diffs`, iff ALL of: the pile is draw / discard / exhaust; the two
// cost lists are the same length; and, AFTER CANCELLING WHAT THE TWO SIDES
// HAVE IN COMMON, every remaining capture cost is 0 -- the `setCostForTurn`
// family (Corruption, Mummified Hand, Infernal Blade, Madness) that every
// observed instance of the lag belongs to -- while every remaining sim cost is
// positive. The cancellation is what makes it right for a key with several
// instances; `deferred_reset_shape` in the .cpp says why a positional walk is
// not.
//
// THE RESIDUAL WEAKNESS, named rather than glossed: with the fields the dump
// carries this shape is not distinguishable, at a single record, from an
// engine that simply failed to zero a card sitting in one of those three
// piles. `isCostModifiedForTurn` is exactly the field that would separate them
// and the fork does not emit it (the gap above). Two things bound the cost of
// that: the shape excuses ONLY a capture-side 0 against a higher sim value in
// those three piles -- a live `Blood for Blood: 3 -> 4` stays a reported
// divergence -- and the HAND, which is where a cost modifier is actually
// spent, is compared with no tolerance at all. The caller counts and prints
// every tolerated record.
//
// The HAND_SELECT containment rule is inherited unchanged: on such a record the
// capture's hand is the opening action's eligible SUBSET, so a key whose costs
// the capture merely has FEWER of is not judged, while a cost the capture has
// and the sim does not is still a divergence.
//
// Field names: `<pile>[<card key>].cost`, with the same card key `--vitals`
// prints (`Strike_R`, `Bash+`, `Searing Blow+3`). A value is the sorted cost
// list (`1`, `[0|1]`) or `(absent)`.
[[nodiscard]] VitalsReport diff_combat_costs(const CombatVitals& game,
                                             const CombatVitals& sim);

}  // namespace sts::translate
