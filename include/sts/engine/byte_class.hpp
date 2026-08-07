#pragma once

// THE TOTAL-BYTE CLASSIFICATION TRIPWIRE (docs/training-plan.md §2.6b): every
// byte of `RunController` classified public / hidden / derived / padding, in a
// table that must TILE `sizeof(RunController)` exactly.
//
// WHAT IT IS FOR. docs/public-view-audit.md is the field-by-field completeness
// proof for `encode_public_view`, and it says of itself: *"Completeness is the
// deliverable, and it is not test-enforced. A field this table forgets is
// twin-invariant -- the T0.5 twin suite compares two states that both lack it,
// so it passes."* This header is the EXECUTABLE FORM of that table, and it
// closes exactly that hole: a state field added without an audit row does not
// leak, does not fail the twin suite, and changes no behaviour -- it just
// silently stops being observed, which cripples learning invisibly (plan §2.1).
// The one thing such a field cannot avoid doing is moving a `sizeof` or an
// `offsetof`, so that is what is checked.
//
// The tripwire does NOT check that a classification is CORRECT -- no test can,
// since "public" is a claim about what a player could have observed. It checks
// that somebody made the claim, at every byte. The twin suite (twin.hpp) is
// what punishes a wrong `public`; the audit doc is what a reviewer reads for a
// wrong `hidden`.
//
// HOW A FAILURE READS. Each row carries its own `offsetof`, and the checker
// walks the rows against a cursor: a row that does not start where the previous
// one ended is reported with the RANGE and the members on either side of it --
// "unclassified bytes [4712, 4716) between `pad2` and `knowledge`" -- because
// the fix for a growth failure is to add the row for the field you just added,
// and the message should name where. A row that starts EARLY is reported as an
// overlap, and a table that runs out before the struct does is reported against
// "<end>", which is the shape a field appended at the tail produces.
//
// GAPS ARE DECLARED, NOT INFERRED. A member row's offset and size come from the
// struct itself, so they follow it; an implicit alignment gap between two
// members is declared as its own row with a LITERAL byte count. That asymmetry
// is deliberate: deriving a gap's size from its neighbours would let the table
// re-fit itself around any change, which is the one thing a tripwire must not
// do. Engine structs declare their padding explicitly (conventions §8), so the
// only literal gaps here are the ones `MonsterLists` creates -- its
// `std::string_view` members force pointer alignment.
//
// NESTING. `run`, `combat` and the transient structs whose members do not share
// one class are decomposed into sub-tables, each of which must tile its own
// struct. That is what gives `RunState.run_seed` its own `hidden` row -- the
// classification plan §2.6b names explicitly.

#include <cstddef>
#include <cstdint>
#include <utility>

#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/knowledge.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/shop.hpp"
#include "sts/engine/treasure_rooms.hpp"

namespace sts::engine {

// The audit doc's Class vocabulary, verbatim (public-view-audit.md, "Class
// vocabulary"). `PUBLIC_COND` is its `public-cond`; `MIXED` is what a row
// carries when its own members disagree -- resolved by a sub-table where the
// disagreement is by byte range, and named in the row's note where it is not.
enum class ByteClass : uint8_t {
    PUBLIC = 0,       // observed, or derivable from observed history
    PUBLIC_COND = 1,  // public except under a declared observability transform
    DERIVED = 2,      // deterministic function of public state / public history
    HIDDEN = 3,       // an RNG realization the player has not observed
    PADDING = 4,      // declared pad bytes; no information content
    MIXED = 5,        // members disagree -- see `sub`, or the row's note
};

[[nodiscard]] constexpr const char* byte_class_name(ByteClass c) noexcept {
    switch (c) {
        case ByteClass::PUBLIC: return "public";
        case ByteClass::PUBLIC_COND: return "public-cond";
        case ByteClass::DERIVED: return "derived";
        case ByteClass::HIDDEN: return "hidden";
        case ByteClass::PADDING: return "padding";
        case ByteClass::MIXED: return "mixed";
    }
    return "?";
}

struct ClassTable;  // forward: a row may point at one

// `offset` sentinel for a declared implicit-alignment gap: a gap has no member
// to take an offsetof from, so it is placed wherever the cursor stands.
inline constexpr std::size_t kDeclaredGap = static_cast<std::size_t>(-1);

// One classified byte range of one struct.
struct ClassRow {
    const char* member;     // member name, or a parenthesised gap description
    std::size_t offset;     // offsetof(Struct, member), or kDeclaredGap
    std::size_t size;       // bytes this row claims
    ByteClass cls;
    const ClassTable* sub;  // non-null iff the split is BY BYTE RANGE
    const char* note;       // the audit-doc reason, one line
};

// A struct's complete classification.
struct ClassTable {
    const char* struct_name;
    std::size_t total;      // sizeof(the struct) -- what the rows must tile
    const ClassRow* rows;
    std::size_t row_count;
};

// A member row. `sizeof(std::declval<S&>().m)` is an unevaluated operand, so it
// needs no object and stays constexpr.
#define STS_BC_ROW(S, m, cls_, note_)                                        \
    ClassRow {                                                               \
        #m, offsetof(S, m), sizeof(std::declval<S&>().m), cls_, nullptr,     \
            note_                                                            \
    }

// A member row whose class is resolved by a sub-table.
#define STS_BC_SUB(S, m, table_, note_)                                      \
    ClassRow {                                                               \
        #m, offsetof(S, m), sizeof(std::declval<S&>().m), ByteClass::MIXED,  \
            &table_, note_                                                   \
    }

// A declared implicit-alignment gap. `bytes` is a literal on purpose.
#define STS_BC_GAP(name_, bytes_, note_)                                     \
    ClassRow { name_, kDeclaredGap, bytes_, ByteClass::PADDING, nullptr, note_ }

#define STS_BC_TABLE(name_, S, rows_)                                        \
    inline constexpr ClassTable name_ {                                      \
        #S, sizeof(S), rows_, sizeof(rows_) / sizeof(rows_[0])               \
    }

// --- The tables --------------------------------------------------------------
//
// Every `note` is the one-line form of that member's Notes cell in
// docs/public-view-audit.md; the doc is the long form and stays the reviewed
// artifact. Keep the two in step -- a note that contradicts the doc is a
// documentation conflict, which conventions §4 makes stop-the-line.

inline constexpr ClassRow kMonsterListsRows[] = {
    STS_BC_ROW(MonsterLists, monster_list, ByteClass::MIXED,
               "audit 8.7: prefix [0,monster_cursor) public, suffix a "
               "monsterRng realization. No sub-table: the split is by INDEX "
               "against a runtime cursor, not by byte range, so it is "
               "encode_public_view's prefix copy and the twin suite that hold "
               "it"),
    STS_BC_ROW(MonsterLists, monster_list_count, ByteClass::HIDDEN,
               "audit 8.7: a generated LENGTH is a property of the unrevealed "
               "suffix; the public counts are the cursors on RunController"),
    STS_BC_GAP("(alignment gap after monster_list_count)", 7,
               "std::string_view forces 8-byte alignment on the next array"),
    STS_BC_ROW(MonsterLists, elite_list, ByteClass::MIXED, "as monster_list"),
    STS_BC_ROW(MonsterLists, elite_list_count, ByteClass::HIDDEN,
               "as monster_list_count"),
    STS_BC_GAP("(alignment gap after elite_list_count)", 7, "as above"),
    STS_BC_ROW(MonsterLists, boss_list, ByteClass::MIXED,
               "audit 8.7: boss_list[0] is public from the map screen from act "
               "start; the rest is the same suffix realization"),
    STS_BC_ROW(MonsterLists, boss_list_count, ByteClass::HIDDEN,
               "as monster_list_count"),
    STS_BC_GAP("(tail alignment gap)", 7,
               "rounds MonsterLists back to its 8-byte alignment"),
};
STS_BC_TABLE(kMonsterListsTable, MonsterLists, kMonsterListsRows);

inline constexpr ClassRow kTreasureChestRows[] = {
    STS_BC_ROW(TreasureChest, size, ByteClass::PUBLIC,
               "audit 8.4: the chest model on the floor shows its size before "
               "any interaction"),
    STS_BC_ROW(TreasureChest, relic_tier, ByteClass::HIDDEN,
               "audit 8.4: AbstractChest.randomizeReward rolls the contents at "
               "CONSTRUCTION; the player learns them only from the open action. "
               "Public once opened -- the reveal-timing tests pin both halves"),
    STS_BC_ROW(TreasureChest, has_gold, ByteClass::HIDDEN, "as relic_tier"),
    STS_BC_ROW(TreasureChest, opened, ByteClass::PUBLIC,
               "audit 8.4: the player performed the action"),
};
STS_BC_TABLE(kTreasureChestTable, TreasureChest, kTreasureChestRows);

inline constexpr ClassRow kBossChestRows[] = {
    STS_BC_ROW(BossChestState, relics, ByteClass::HIDDEN,
               "audit 8.4 (the same masking trap as TreasureChest, one room "
               "later): BossChest's constructor pops all three at ROOM ENTRY "
               "(BossChest.java:35-39) and the player sees them only by opening "
               "the chest. Public once `seen` -- and it stays public after a "
               "SKIP, which closes the chest but cannot unsee it"),
    STS_BC_ROW(BossChestState, screen, ByteClass::PUBLIC,
               "audit 8.4: which screen is up -- the player is looking at it"),
    STS_BC_ROW(BossChestState, seen, ByteClass::PUBLIC,
               "audit 8.4: the player performed the open action. This, not "
               "`screen`, is the reveal flag the encoder gates `relics` on"),
    STS_BC_ROW(BossChestState, chose_relic, ByteClass::PUBLIC,
               "audit 8.4: TreasureRoomBoss.choseRelic -- the relic is on the "
               "player's own relic bar"),
    STS_BC_ROW(BossChestState, pad, ByteClass::PADDING, ""),
};
STS_BC_TABLE(kBossChestTable, BossChestState, kBossChestRows);

inline constexpr ClassRow kEventDialogRows[] = {
    STS_BC_ROW(EventDialogState, event_id, ByteClass::PUBLIC,
               "audit 8.5: which event the room resolved to"),
    STS_BC_ROW(EventDialogState, screen, ByteClass::PUBLIC,
               "audit 8.5: the page the dialog is on"),
    STS_BC_ROW(EventDialogState, grid_kind, ByteClass::PUBLIC,
               "audit 8.5: which card grid is open"),
    STS_BC_ROW(EventDialogState, scratch0, ByteClass::MIXED,
               "audit 8.5: per-EVENT publicity, resolved by "
               "event_scratch_public_mask rather than by byte range. Dead "
               "Adventurer packs a miscRng loot shuffle and its elite id here "
               "(hidden); every other event prints its scratch on the dialog"),
    STS_BC_ROW(EventDialogState, scratch1, ByteClass::MIXED, "as scratch0"),
    STS_BC_ROW(EventDialogState, scratch2, ByteClass::MIXED, "as scratch0"),
    STS_BC_ROW(EventDialogState, scratch3, ByteClass::MIXED, "as scratch0"),
    STS_BC_ROW(EventDialogState, board, ByteClass::MIXED,
               "audit 8.5: Match and Keep deals twelve FACE-DOWN cards; a "
               "slot's identity is hidden until flipped or matched, `taken` is "
               "public. Per slot, not per byte range"),
};
STS_BC_TABLE(kEventDialogTable, EventDialogState, kEventDialogRows);

inline constexpr ClassRow kRunStateRows[] = {
    STS_BC_ROW(RunState, run_seed, ByteClass::HIDDEN,
               "audit 6 / plan 2.6b, NAMED EXPLICITLY: knowing it is "
               "seed-cracking"),
    STS_BC_ROW(RunState, master_deck, ByteClass::PUBLIC,
               "audit 6: engine order is the fold of observed acquisitions AND "
               "the index space can_choose_master_deck[] addresses"),
    STS_BC_ROW(RunState, master_deck_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(RunState, hp, ByteClass::PUBLIC, "always-block"),
    STS_BC_ROW(RunState, max_hp, ByteClass::PUBLIC, "always-block"),
    STS_BC_ROW(RunState, pad_gold_align, ByteClass::PADDING, ""),
    STS_BC_ROW(RunState, gold, ByteClass::PUBLIC, "always-block"),
    STS_BC_ROW(RunState, ascension, ByteClass::PUBLIC, "always-block"),
    STS_BC_ROW(RunState, act, ByteClass::PUBLIC, "always-block"),
    STS_BC_ROW(RunState, floor, ByteClass::PUBLIC, "always-block"),
    STS_BC_ROW(RunState, relics, ByteClass::PUBLIC,
               "audit 6: relics with their displayed counters"),
    STS_BC_ROW(RunState, relic_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(RunState, pad_relic, ByteClass::PADDING, ""),
    STS_BC_ROW(RunState, potions, ByteClass::PUBLIC,
               "the belt, public in every phase"),
    STS_BC_ROW(RunState, map, ByteClass::PUBLIC,
               "audit 6: the full current-act map is drawn from act start"),
    STS_BC_ROW(RunState, boss_ids, ByteClass::PUBLIC,
               "audit 6: the act boss is displayed on the map screen"),
    STS_BC_ROW(RunState, keys, ByteClass::PUBLIC, "audit 6"),
    STS_BC_ROW(RunState, pad_keys, ByteClass::PADDING, ""),
    STS_BC_ROW(RunState, event_flags, ByteClass::PUBLIC,
               "one-shot FIRED bitset (ids 1..31; ids 32..63 are in "
               "event_flags_hi below); the fires were observed"),
    STS_BC_ROW(RunState, shop_flags, ByteClass::PUBLIC, "as event_flags"),
    STS_BC_ROW(RunState, card_blizz_randomizer, ByteClass::PUBLIC,
               "plan 1: pity counters are perfect-memory tracker state"),
    STS_BC_ROW(RunState, blizzard_potion_mod, ByteClass::PUBLIC, "as above"),
    STS_BC_ROW(RunState, event_pity_monster, ByteClass::PUBLIC, "as above"),
    STS_BC_ROW(RunState, event_pity_shop, ByteClass::PUBLIC, "as above"),
    STS_BC_ROW(RunState, event_pity_treasure, ByteClass::PUBLIC, "as above"),
    STS_BC_ROW(RunState, purge_cost, ByteClass::PUBLIC,
               "displayed in every shop"),
    STS_BC_ROW(RunState, potion_slots, ByteClass::PUBLIC, ""),
    STS_BC_ROW(RunState, pad_potion_slots, ByteClass::PADDING, ""),
    STS_BC_ROW(RunState, event_membership, ByteClass::PUBLIC,
               "remaining-pool membership: the initial lists are public rules "
               "and every removal was observed"),
    STS_BC_ROW(RunState, special_membership, ByteClass::PUBLIC, "as above"),
    STS_BC_ROW(RunState, shrine_membership, ByteClass::PUBLIC, "as above"),
    STS_BC_ROW(RunState, pad_membership, ByteClass::PADDING, ""),
    STS_BC_ROW(RunState, relic_pools, ByteClass::HIDDEN,
               "audit 6: the remaining order/composition of each tier's "
               "[0,count) window is a shuffle realization; resampled by "
               "resample_relic_pool_remainders"),
    STS_BC_ROW(RunState, relic_pool_count, ByteClass::DERIVED,
               "audit 6: initial tier size (a public rule) minus observed pops"),
    STS_BC_ROW(RunState, pad_relic_pools, ByteClass::PADDING, ""),
    STS_BC_ROW(RunState, pad_rng_align_lo, ByteClass::PADDING,
               "narrowed 6 -> 2 by S2.13; the other 4 bytes became "
               "event_flags_hi, which is why no offset moved"),
    STS_BC_ROW(RunState, event_flags_hi, ByteClass::PUBLIC,
               "as event_flags: the FIRED bitset's second word, ids 32..63"),
    STS_BC_ROW(RunState, monster_rng, ByteClass::HIDDEN,
               "audit 6: run-/act-/event-scoped stream states are realizations"),
    STS_BC_ROW(RunState, event_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(RunState, merchant_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(RunState, card_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(RunState, treasure_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(RunState, relic_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(RunState, potion_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(RunState, map_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(RunState, neow_rng, ByteClass::HIDDEN, "as above"),
};
STS_BC_TABLE(kRunStateTable, RunState, kRunStateRows);

inline constexpr ClassRow kCombatStateRows[] = {
    STS_BC_ROW(CombatState, phase, ByteClass::PUBLIC, "audit 1: screen identity"),
    STS_BC_ROW(CombatState, pad_header, ByteClass::PADDING, ""),
    STS_BC_ROW(CombatState, turn, ByteClass::PUBLIC,
               "audit 1: the displayed turn counter"),
    STS_BC_ROW(CombatState, flags, ByteClass::PUBLIC,
               "audit 3: carried whole, every allocated bit audited bit-by-bit"),
    STS_BC_ROW(CombatState, player_hp, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, player_max_hp, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, player_block, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, player_energy, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, stance, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, cards_played_this_turn, ByteClass::PUBLIC,
               "own observed plays"),
    STS_BC_ROW(CombatState, player_power_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, pad_player, ByteClass::PADDING, ""),
    STS_BC_ROW(CombatState, player_powers, ByteClass::PUBLIC,
               "audit 1/2: the full list; PowerSlot.pad0 is that struct's own "
               "declared padding"),
    STS_BC_ROW(CombatState, card_pool, ByteClass::DERIVED,
               "audit 1: pool INDICES are engine bookkeeping; every card's "
               "public content reaches the view by value through the piles"),
    STS_BC_ROW(CombatState, hand, ByteClass::PUBLIC,
               "audit 1: the hand is on screen, in engine order"),
    STS_BC_ROW(CombatState, draw, ByteClass::HIDDEN,
               "audit 1: CONTENTS are public, the ARRANGEMENT is a shuffle "
               "realization. The view carries the multiset canonically sorted; "
               "order knowledge reaches it only as KnowledgeState constraints"),
    STS_BC_ROW(CombatState, discard, ByteClass::PUBLIC,
               "audit 1: engine order is the fold of observed arrivals"),
    STS_BC_ROW(CombatState, exhaust, ByteClass::PUBLIC,
               "audit 1: every exhaust was observed"),
    STS_BC_ROW(CombatState, limbo, ByteClass::PUBLIC,
               "audit 1: mid-resolution holding zone; arrivals observed"),
    STS_BC_ROW(CombatState, hand_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, draw_count, ByteClass::PUBLIC,
               "the pile SIZE is on screen; only its order is hidden"),
    STS_BC_ROW(CombatState, discard_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, exhaust_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, limbo_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, monster_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, combat_gold, ByteClass::PUBLIC,
               "audit 1: gold gains are displayed as they happen"),
    STS_BC_ROW(CombatState, pad_monsters, ByteClass::PADDING,
               "declared by T0.5 after this tripwire found it implicit -- see "
               "combat_state.hpp and conventions section 8"),
    STS_BC_ROW(CombatState, monsters, ByteClass::MIXED,
               "audit 2: per MonsterState member. All public/public-cond except "
               "pad0, which can hold an unrevealed construction roll (the Louse "
               "bite damage) and is excluded wholesale; revealed rolls arrive "
               "through the KnowledgeState projection. draw_x (schema v7, in "
               "what was pad1) is DERIVED -- a per-encounter constant fixed by "
               "the monster's identity and spawn slot, both public -- so it is "
               "recoverable and needs no PvMonster field of its own"),
    STS_BC_ROW(CombatState, action_queue, ByteClass::DERIVED,
               "audit 1: resolution transients, deterministic from the observed "
               "action history. They legitimately MOVE THE MASK, which is why "
               "make_hidden_twin must not perturb them (twin.hpp)"),
    STS_BC_ROW(CombatState, action_head, ByteClass::DERIVED, "as action_queue"),
    STS_BC_ROW(CombatState, action_tail, ByteClass::DERIVED, "as action_queue"),
    STS_BC_ROW(CombatState, action_count, ByteClass::DERIVED, "as action_queue"),
    STS_BC_ROW(CombatState, pad_actionq, ByteClass::PADDING, ""),
    STS_BC_ROW(CombatState, pre_turn_actions, ByteClass::DERIVED,
               "audit 1: as action_queue"),
    STS_BC_ROW(CombatState, pre_turn_head, ByteClass::DERIVED, "as above"),
    STS_BC_ROW(CombatState, pre_turn_tail, ByteClass::DERIVED, "as above"),
    STS_BC_ROW(CombatState, pre_turn_count, ByteClass::DERIVED, "as above"),
    STS_BC_ROW(CombatState, turn_has_ended, ByteClass::DERIVED,
               "audit 1: pump bookkeeping, deterministic at every boundary. "
               "Named in T0.2's note as NOT twin material"),
    STS_BC_ROW(CombatState, card_queue, ByteClass::DERIVED,
               "audit 1: pending plays the player queued"),
    STS_BC_ROW(CombatState, card_queue_count, ByteClass::DERIVED, "as above"),
    STS_BC_ROW(CombatState, pad_cardq, ByteClass::PADDING, ""),
    STS_BC_ROW(CombatState, monster_queue, ByteClass::DERIVED,
               "audit 1: turn-order bookkeeping"),
    STS_BC_ROW(CombatState, monster_queue_count, ByteClass::DERIVED, "as above"),
    STS_BC_ROW(CombatState, monster_attacks_queued, ByteClass::DERIVED,
               "as above"),
    STS_BC_ROW(CombatState, relics, ByteClass::PUBLIC,
               "audit 1: the combat relic mirror is what the screen shows while "
               "a combat is live"),
    STS_BC_ROW(CombatState, relic_count, ByteClass::PUBLIC, ""),
    STS_BC_ROW(CombatState, pending_obtain_count, ByteClass::PUBLIC,
               "audit 1: an in-combat master-deck obtain is announced on screen "
               "as it happens. Not separately encoded in PublicView because the "
               "run layer DRAINS it into master_deck (which IS encoded) within "
               "the same pump step, so no PublicView a caller can observe is "
               "ever taken while it is non-zero. These two took over what was "
               "pad_relics[7] in schema v7"),
    STS_BC_ROW(CombatState, pending_obtain, ByteClass::PUBLIC, "as above"),
    STS_BC_ROW(CombatState, pad_rng_align, ByteClass::PADDING,
               "declared by T0.5 after this tripwire found it implicit -- see "
               "combat_state.hpp and conventions section 8"),
    STS_BC_ROW(CombatState, monster_hp_rng, ByteClass::HIDDEN,
               "audit 1: the five floor-scoped stream states are exactly the "
               "realizations the contract hides"),
    STS_BC_ROW(CombatState, ai_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(CombatState, shuffle_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(CombatState, card_random_rng, ByteClass::HIDDEN, "as above"),
    STS_BC_ROW(CombatState, misc_rng, ByteClass::HIDDEN, "as above"),
};
STS_BC_TABLE(kCombatStateTable, CombatState, kCombatStateRows);

inline constexpr ClassRow kRunControllerRows[] = {
    STS_BC_SUB(RunController, run, kRunStateTable, "audit 6"),
    STS_BC_SUB(RunController, combat, kCombatStateTable, "audit 1"),
    STS_BC_ROW(RunController, phase, ByteClass::PUBLIC,
               "audit 5: screen identity"),
    STS_BC_ROW(RunController, cur_x, ByteClass::PUBLIC,
               "audit 5: own map position"),
    STS_BC_ROW(RunController, room_type, ByteClass::PUBLIC,
               "audit 5: the entered room's kind"),
    STS_BC_ROW(RunController, combat_outcome, ByteClass::PUBLIC,
               "audit 5: how the last combat ended -- observed"),
    STS_BC_GAP("(alignment gap before `lists`)", 4,
               "MonsterLists holds std::string_view, so it is 8-aligned"),
    STS_BC_SUB(RunController, lists, kMonsterListsTable, "audit 5 / 8.7"),
    STS_BC_ROW(RunController, monster_cursor, ByteClass::PUBLIC,
               "audit 5: the consumed-prefix length"),
    STS_BC_ROW(RunController, elite_cursor, ByteClass::PUBLIC, "audit 5"),
    STS_BC_ROW(RunController, boss_cursor, ByteClass::PUBLIC, "audit 5"),
    STS_BC_ROW(RunController, pad1, ByteClass::PADDING, ""),
    STS_BC_ROW(RunController, emerald_x, ByteClass::PUBLIC,
               "audit 5: the burning-elite node is drawn on the map"),
    STS_BC_ROW(RunController, emerald_y, ByteClass::PUBLIC, "audit 5"),
    STS_BC_ROW(RunController, pad_emerald, ByteClass::PADDING, ""),
    STS_BC_ROW(RunController, rewards, ByteClass::PUBLIC,
               "audit 8.1: public WHILE ON SCREEN. The encoder gates it on the "
               "phase, never on emptiness -- Dead Adventurer pre-stocks it "
               "before its combat starts"),
    STS_BC_ROW(RunController, rest, ByteClass::PUBLIC,
               "audit 8.2: only the screen id is stored; the option list is "
               "rebuilt per call and reaches the consumer through the mask"),
    STS_BC_ROW(RunController, shop, ByteClass::PUBLIC,
               "audit 8.3: the whole current-visit stock is drawn with prices"),
    STS_BC_SUB(RunController, treasure_chest, kTreasureChestTable,
               "audit 8.4 -- the one masking trap plan 2.1 names"),
    STS_BC_SUB(RunController, boss_chest, kBossChestTable,
               "audit 8.4 -- the boss chest is the SECOND instance of the same "
               "trap: contents drawn at entry, revealed by the open action"),
    STS_BC_SUB(RunController, event, kEventDialogTable, "audit 8.5"),
    STS_BC_ROW(RunController, neow, ByteClass::PUBLIC,
               "audit 8.6: the four rolled blessings ARE the screen"),
    STS_BC_ROW(RunController, pending_bottle, ByteClass::PUBLIC,
               "audit 5: the modal overlay is on screen"),
    STS_BC_ROW(RunController, pad2, ByteClass::PADDING, ""),
    STS_BC_ROW(RunController, knowledge, ByteClass::PUBLIC,
               "audit 10: a record OF public reveals. Never carried raw (its "
               "chain holds pool indices); projected as per-slot order "
               "constraints"),
    STS_BC_ROW(RunController, pad_tail, ByteClass::PADDING,
               "declared by T0.5 after this tripwire found it implicit -- see "
               "run_advance.hpp and conventions section 8"),
};
STS_BC_TABLE(kRunControllerTable, RunController, kRunControllerRows);

// --- The checker -------------------------------------------------------------

// The result of walking one table. `ok` is the only thing a passing test reads;
// everything else exists so a FAILING one says what to do.
struct TilingFault {
    bool ok = true;
    const char* struct_name = "";
    const char* before = "";  // member preceding the fault ("<start>" at 0)
    const char* after = "";   // member following it ("<end>" past the last row)
    std::size_t from = 0;     // first unclassified / overlapping byte
    std::size_t to = 0;       // one past the last
    bool overlap = false;     // true: a row starts before the cursor
};

// Walk `t`'s rows against `total` bytes.
//
// `total` is a PARAMETER rather than `t.total` so the negative test can hand
// this exact checker a struct that grew by a scratch field and watch it fire --
// the mechanism under test is then literally the mechanism in use, not a
// re-implementation of it (tests/tripwire_test.cpp).
[[nodiscard]] constexpr TilingFault check_tiling(const ClassTable& t,
                                                 std::size_t total) noexcept {
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < t.row_count; ++i) {
        const ClassRow& r = t.rows[i];
        const char* before = i == 0 ? "<start>" : t.rows[i - 1].member;
        if (r.offset != kDeclaredGap && r.offset != cursor) {
            TilingFault f;
            f.ok = false;
            f.struct_name = t.struct_name;
            f.before = before;
            f.after = r.member;
            f.overlap = r.offset < cursor;
            f.from = f.overlap ? r.offset : cursor;
            f.to = f.overlap ? cursor : r.offset;
            return f;
        }
        if (cursor + r.size > total) {
            TilingFault f;
            f.ok = false;
            f.struct_name = t.struct_name;
            f.before = before;
            f.after = r.member;
            f.overlap = true;
            f.from = total;
            f.to = cursor + r.size;
            return f;
        }
        cursor += r.size;
    }
    if (cursor != total) {
        TilingFault f;
        f.ok = false;
        f.struct_name = t.struct_name;
        f.before = t.row_count == 0 ? "<start>" : t.rows[t.row_count - 1].member;
        f.after = "<end>";
        f.from = cursor;
        f.to = total;
        return f;
    }
    return TilingFault{};
}

[[nodiscard]] constexpr bool tiles(const ClassTable& t) noexcept {
    return check_tiling(t, t.total).ok;
}

// COMPILE-TIME half of the tripwire. Adding, removing, reordering or resizing a
// member of any classified struct moves an `offsetof` or a `sizeof`, and these
// fail at BUILD time -- before anything gets as far as running a test. The
// runtime test in tests/tripwire_test.cpp is what turns each of them into a
// message naming the byte range and the neighbouring members.
static_assert(tiles(kRunControllerTable),
              "RunController grew or moved without a classification row -- add "
              "the row in byte_class.hpp AND the audit row in "
              "docs/public-view-audit.md, and re-check encode_public_view. "
              "tests/tripwire_test.cpp prints the byte range and the "
              "neighbouring members");
static_assert(tiles(kRunStateTable), "RunState classification no longer tiles");
static_assert(tiles(kCombatStateTable),
              "CombatState classification no longer tiles");
static_assert(tiles(kMonsterListsTable),
              "MonsterLists classification no longer tiles");
static_assert(tiles(kTreasureChestTable),
              "TreasureChest classification no longer tiles");
static_assert(tiles(kEventDialogTable),
              "EventDialogState classification no longer tiles");

}  // namespace sts::engine
