// encode_public_view -- see include/sts/engine/public_view.hpp for the
// contract and docs/public-view-audit.md for the field-by-field audit this
// encoder is held to.

#include "sts/engine/public_view.hpp"

#include <algorithm>

#include "sts/engine/relic_hooks.hpp"  // player_has_relic (Runic Dome)
#include "sts/registry/encounter_table.hpp"  // encounter_by_game_id (prefix ids)
#include "sts/registry/ids.hpp"              // EventId (scratch publicity)

namespace sts::engine {

namespace {

[[nodiscard]] PvCard pv_card(const CardInstance& c) noexcept {
    return PvCard{c.card_id, c.upgrade, c.cost_now, c.flags};
}

// Copy one pile (indices into the card pool) out as card VALUES. Slots past
// `count` stay value-init zero (the caller zeroed the whole struct).
void pv_pile(const CombatState& s, const CardPoolIndex* pile, int count,
             PvCard* out, int cap) noexcept {
    const int n = count < cap ? count : cap;
    for (int i = 0; i < n; ++i) {
        out[i] = pv_card(s.card_pool[pile[i]]);
    }
}

// The draw multiset's canonical order: ascending (card_id, upgrade, cost_now,
// flags). This key is part of the schema (public_view.hpp) -- it is what makes
// two states differing only in hidden draw ORDER encode byte-identically.
[[nodiscard]] bool pv_card_less(const PvCard& a, const PvCard& b) noexcept {
    if (a.card_id != b.card_id) return a.card_id < b.card_id;
    if (a.upgrade != b.upgrade) return a.upgrade < b.upgrade;
    if (a.cost_now != b.cost_now) return a.cost_now < b.cost_now;
    return a.flags < b.flags;
}

void encode_combat_section(const CombatState& s, PublicView& out) noexcept {
    out.combat_active = 1;
    out.combat_phase = s.phase;
    out.stance = s.stance;
    out.turn = s.turn;
    out.combat_gold = s.combat_gold;
    out.combat_flags = s.flags;

    out.player_hp = s.player_hp;
    out.player_max_hp = s.player_max_hp;
    out.player_block = s.player_block;
    out.player_energy = s.player_energy;
    out.cards_played_this_turn = s.cards_played_this_turn;

    // Player powers: the full list (the 240-byte stub carries none).
    out.player_power_count = s.player_power_count;
    const int ppn =
        s.player_power_count < kPowerCap ? s.player_power_count : kPowerCap;
    for (int p = 0; p < ppn; ++p) {
        const PowerSlot& ps = s.player_powers[p];
        out.player_powers[p] = PvPower{ps.power_id, ps.amount, ps.counter};
    }

    // Piles. Hand / discard / exhaust / limbo keep engine order (public:
    // every arrival was an observed event); the draw pile is sorted into the
    // canonical multiset order because its ORDER is the hidden realization.
    out.hand_count = s.hand_count;
    out.draw_count = s.draw_count;
    out.discard_count = s.discard_count;
    out.exhaust_count = s.exhaust_count;
    out.limbo_count = s.limbo_count;
    pv_pile(s, s.hand, s.hand_count, out.hand, kHandCap);
    pv_pile(s, s.draw, s.draw_count, out.draw, kDrawCap);
    pv_pile(s, s.discard, s.discard_count, out.discard, kDiscardCap);
    pv_pile(s, s.exhaust, s.exhaust_count, out.exhaust, kExhaustCap);
    pv_pile(s, s.limbo, s.limbo_count, out.limbo, kLimboCap);
    const int dn = s.draw_count < kDrawCap ? s.draw_count : kDrawCap;
    std::sort(out.draw, out.draw + dn, pv_card_less);  // in-place; no heap

    // Monsters. Runic Dome hides the telegraphed intent exactly as
    // omniscient_encode_observation does  [omniscient-boundary-ok]
    // (omniscient_observation.hpp carries the full provenance  [omniscient-boundary-ok]
    // write-up: two rendering guards, AbstractMonster.java:258/:749, so the
    // observation layer is the ONE place the suppression belongs, and
    // MonsterState.intent itself is never touched). move_history stays
    // visible: past moves were observed as they resolved.
    const bool hide_intents = player_has_relic(s, RelicId::RUNIC_DOME);
    out.monster_count = s.monster_count;
    const int mn = s.monster_count < kMonsterCap ? s.monster_count : kMonsterCap;
    for (int m = 0; m < mn; ++m) {
        const MonsterState& ms = s.monsters[m];
        PvMonster& om = out.monsters[m];
        om.monster_id = ms.monster_id;
        om.hp = ms.hp;
        om.max_hp = ms.max_hp;
        om.block = ms.block;
        om.flags = ms.flags;
        om.move_history[0] = ms.move_history[0];
        om.move_history[1] = ms.move_history[1];
        om.move_history[2] = ms.move_history[2];
        om.intent = hide_intents ? 0 : ms.intent;
        om.occupied = 1;
        // ms.pad0 (per-type scratch, may hold an unrevealed construction
        // roll) is deliberately NOT copied -- see public_view.hpp.
        om.power_count = ms.power_count;
        const int pn = ms.power_count < kPowerCap ? ms.power_count : kPowerCap;
        for (int p = 0; p < pn; ++p) {
            const PowerSlot& ps = ms.powers[p];
            om.powers[p] = PvPower{ps.power_id, ps.amount, ps.counter};
        }
    }
}

// --- KnowledgeState projection (T0.2; knowledge.hpp, plan §2.2/§3.1) ---------
//
// The draw pile is encoded as a canonically SORTED multiset, so order knowledge
// cannot be expressed by rearranging it -- unsorting would put the hidden
// arrangement straight back into the bytes. It is expressed instead as two
// annotations parallel to draw[]: the 1-based rank of that card in the known
// relative-order chain, and its 1-based exact from-top position when it has one.
//
// A chain entry is a CardPoolIndex, which is engine bookkeeping and never
// information; what it names is a card VALUE, so the projection walks the chain
// top-first and binds each entry to the LOWEST not-yet-annotated sorted slot
// holding that value. Identical values are interchangeable under the multiset
// encoding, so that tie-break is canonical: two states with the same knowledge
// over the same multiset annotate identically regardless of pool indices.
void encode_knowledge(const CombatState& s, const KnowledgeState& k,
                      PublicView& out) noexcept {
    out.knowledge_chain_count = k.chain_count;
    out.knowledge_exact_prefix = k.exact_prefix;
    out.knowledge_full_order = k.full_order;

    const int dn = s.draw_count < kDrawCap ? s.draw_count : kDrawCap;
    const int cn = k.chain_count < kKnowledgeChainCap
                       ? static_cast<int>(k.chain_count)
                       : kKnowledgeChainCap;
    for (int c = 0; c < cn; ++c) {
        const PvCard want = pv_card(s.card_pool[k.chain[c]]);
        for (int i = 0; i < dn; ++i) {
            if (out.draw_constraint_rank[i] != 0) continue;
            const PvCard& have = out.draw[i];
            if (have.card_id != want.card_id || have.upgrade != want.upgrade ||
                have.cost_now != want.cost_now || have.flags != want.flags) {
                continue;
            }
            out.draw_constraint_rank[i] = static_cast<uint8_t>(c + 1);
            if (c < static_cast<int>(k.exact_prefix)) {
                out.draw_exact_pos[i] = static_cast<uint8_t>(c + 1);
            }
            break;
        }
    }

    // Revealed monster construction rolls (a Louse's bite damage becomes public
    // the first time its BITE intent is telegraphed). MonsterState.pad0, where
    // the engine keeps the raw roll, stays excluded wholesale -- this is the
    // declared reveal channel.
    for (int m = 0; m < kMonsterCap; ++m) {
        out.monster_roll_known[m] = k.monster_roll_known[m];
        out.monster_roll[m] =
            k.monster_roll_known[m] != 0 ? k.monster_roll[m] : uint8_t{0};
    }
}

// --- Per-event scratch publicity ---------------------------------------------
//
// EventDialogState.scratch0..3 are EVENT-DEFINED, so they cannot be carried
// wholesale. Most events park a DISPLAYED number there; Dead Adventurer parks
// two unrevealed miscRng realizations. The switch has no `default:` so that a
// new EventId is a -Wswitch diagnostic at exactly the site that must classify
// it, rather than silently inheriting somebody else's answer.
//
// Return value: bit i set == scratch[i] is public and carried.
[[nodiscard]] uint8_t event_scratch_public_mask(uint16_t event_id) noexcept {
    switch (static_cast<sts::registry::EventId>(event_id)) {
        // DEAD ADVENTURER -- the one hidden row. dead_enter (events/
        // exordium_events_i.cpp, DeadAdventurer.java:61-207) packs a miscRng
        // JDK shuffle of {gold, nothing, relic} into scratch0 and the identity
        // of the elite it will spring (3 Sentries / Gremlin Nob / Lagavulin)
        // into scratch1, both at ROOM ENTRY. Neither is on screen: the reward
        // order is revealed one search at a time and the elite only when the
        // fight starts. scratch0's high bits (the search count) ARE public, but
        // they are derivable from the observed presses, so the whole word is
        // masked rather than carried under a transform.
        case sts::registry::EventId::DEAD_ADVENTURER:
            return 0;

        // Everything else: scratch is either unused (always zero) or holds a
        // number the dialog displays --
        //   SCRAP_OOZE   scratch0 = the shown chance %, scratch1 = the damage
        //                (both ramp visibly on every failed dig)
        //   WORLD_OF_GOOP  scratch0 = the gold the "clean it up" option shows
        //   FACE_TRADER  scratch0 = the gold offer, scratch1 = the HP cost
        //   WE_MEET_AGAIN  scratch0/1/2 = the three offered items (a potion
        //                slot, a gold amount, a master-deck index) -- rolled at
        //                entry and printed on the three buttons
        //   MATCH_AND_KEEP  scratch0 = attempts left, scratch1 = the currently
        //                flipped slot (the BOARD identities are masked
        //                separately, see PvEventBoardCard)
        case sts::registry::EventId::NONE:
        case sts::registry::EventId::BIG_FISH:
        case sts::registry::EventId::THE_CLERIC:
        case sts::registry::EventId::GOLDEN_IDOL:
        case sts::registry::EventId::GOLDEN_WING:
        case sts::registry::EventId::WORLD_OF_GOOP:
        case sts::registry::EventId::LIARS_GAME:
        case sts::registry::EventId::LIVING_WALL:
        case sts::registry::EventId::MUSHROOMS:
        case sts::registry::EventId::SCRAP_OOZE:
        case sts::registry::EventId::SHINING_LIGHT:
        case sts::registry::EventId::MATCH_AND_KEEP:
        case sts::registry::EventId::GOLDEN_SHRINE:
        case sts::registry::EventId::TRANSMORGRIFIER:
        case sts::registry::EventId::PURIFIER:
        case sts::registry::EventId::UPGRADE_SHRINE:
        case sts::registry::EventId::WHEEL_OF_CHANGE:
        case sts::registry::EventId::ACCURSED_BLACKSMITH:
        case sts::registry::EventId::BONFIRE_ELEMENTALS:
        case sts::registry::EventId::DESIGNER:
        case sts::registry::EventId::DUPLICATOR:
        case sts::registry::EventId::FACE_TRADER:
        case sts::registry::EventId::FOUNTAIN_OF_CLEANSING:
        case sts::registry::EventId::KNOWING_SKULL:
        case sts::registry::EventId::LAB:
        case sts::registry::EventId::NLOTH:
        case sts::registry::EventId::NOTE_FOR_YOURSELF:
        case sts::registry::EventId::SECRET_PORTAL:
        case sts::registry::EventId::THE_JOUST:
        case sts::registry::EventId::WE_MEET_AGAIN:
        case sts::registry::EventId::THE_WOMAN_IN_BLUE:
            return 0x0Fu;

        // S2.02's twenty Act-2/3 eventList rows (registry/events.yaml ids
        // 32-51). They are IDENTITY ROWS: no body is linked, so nothing ever
        // writes their scratch and it reads zero whichever answer this switch
        // gives. Masked rather than carried because that is the only choice
        // that stays correct if a body lands WITHOUT revisiting this site --
        // Mind Bloom's boss shuffle and Cursed Tome's book draw are exactly
        // the Dead-Adventurer shape (a miscRng realization parked at entry and
        // not on screen), and a leak here is invisible to every downstream
        // twin test. S2.31-S2.33 own the reclassification: a body that parks a
        // DISPLAYED number moves its id up into the 0x0F group above, with the
        // same one-line justification the rows there carry.
        case sts::registry::EventId::ADDICT:
        case sts::registry::EventId::BACK_TO_BASICS:
        case sts::registry::EventId::BEGGAR:
        case sts::registry::EventId::COLOSSEUM:
        case sts::registry::EventId::CURSED_TOME:
        case sts::registry::EventId::DRUG_DEALER:
        case sts::registry::EventId::FORGOTTEN_ALTAR:
        case sts::registry::EventId::GHOSTS:
        case sts::registry::EventId::MASKED_BANDITS:
        case sts::registry::EventId::NEST:
        case sts::registry::EventId::THE_LIBRARY:
        case sts::registry::EventId::THE_MAUSOLEUM:
        case sts::registry::EventId::VAMPIRES:
        case sts::registry::EventId::FALLING:
        case sts::registry::EventId::MIND_BLOOM:
        case sts::registry::EventId::THE_MOAI_HEAD:
        case sts::registry::EventId::MYSTERIOUS_SPHERE:
        case sts::registry::EventId::SENSORY_STONE:
        case sts::registry::EventId::TOMB_OF_LORD_RED_MASK:
        case sts::registry::EventId::WINDING_HALLS:
            return 0;
    }
    // An id outside the generated enum (tests use kSyntheticEventId): carry
    // nothing. Masking is the safe direction -- it under-informs, it cannot
    // leak.
    return 0;
}

// --- Run-phase screen sections -------------------------------------------------

// The consumed prefix of one encounter list, as registry EncounterDef ids. Only
// [0, consumed) is written: the unconsumed suffix is a monsterRng realization
// (T0.4 continues it as a Markov chain rather than rerolling it).
void encode_prefix(const std::string_view* list, uint8_t list_count,
                   int consumed, uint8_t* out, int cap) noexcept {
    const int n = consumed < static_cast<int>(list_count)
                      ? consumed
                      : static_cast<int>(list_count);
    const int limit = n < cap ? n : cap;
    for (int i = 0; i < limit; ++i) {
        const sts::registry::EncounterDef* def =
            sts::registry::encounter_by_game_id(list[i]);
        out[i] = def != nullptr ? def->id : uint8_t{0};
    }
}

[[nodiscard]] uint8_t encounter_id_of(std::string_view key) noexcept {
    const sts::registry::EncounterDef* def =
        sts::registry::encounter_by_game_id(key);
    return def != nullptr ? def->id : uint8_t{0};
}

void encode_always_block(const RunController& rc, PublicView& out) noexcept {
    const RunState& rs = rc.run;

    out.gold = rs.gold;
    out.event_pity_monster = rs.event_pity_monster;
    out.event_pity_shop = rs.event_pity_shop;
    out.event_pity_treasure = rs.event_pity_treasure;
    out.event_flags = rs.event_flags;
    // The v3 tail append (S2.13). Assigned beside its v2 sibling rather than
    // next to action_mask because the two words are one field semantically;
    // only the LAYOUT position had to go to the struct tail.
    out.event_flags_hi = rs.event_flags_hi;
    out.shop_flags = rs.shop_flags;
    out.run_hp = rs.hp;
    out.run_max_hp = rs.max_hp;
    out.card_blizz_randomizer = rs.card_blizz_randomizer;
    out.blizzard_potion_mod = rs.blizzard_potion_mod;
    out.purge_cost = rs.purge_cost;
    out.floor = rs.floor;
    out.event_membership = rs.event_membership;
    out.special_membership = rs.special_membership;
    out.shrine_membership = rs.shrine_membership;
    out.ascension = rs.ascension;
    for (int i = 0; i < kBossIdCap; ++i) {
        out.boss_ids[i] = rs.boss_ids[i];
    }
    // Reserved fields whose declared meaning is live today (see the header).
    out.keys_reserved = rs.keys;
    out.act_reserved = rs.act;

    // Master deck: ENGINE ORDER. Unlike the draw pile this order is not a
    // hidden realization -- it is the fold of observed acquisitions -- and it
    // is the index space the mask's can_choose_master_deck[] addresses, so
    // sorting it here would desynchronize the two halves of the observation.
    const int dc = rs.master_deck_count < kMasterDeckCap
                       ? static_cast<int>(rs.master_deck_count)
                       : kMasterDeckCap;
    out.master_deck_count = static_cast<uint16_t>(dc);
    for (int i = 0; i < dc; ++i) {
        out.master_deck[i] = pv_card(rs.master_deck[i]);
    }

    // Relics + displayed counters. While a combat is live the counters that
    // matter are the MIRROR's (in-combat ticks land there until fold-back), so
    // the mirror is the source exactly then.
    const bool in_combat = rc.phase == static_cast<uint8_t>(RunPhase::COMBAT);
    const RelicSlot* src = in_combat ? rc.combat.relics : rs.relics;
    const uint8_t src_count = in_combat ? rc.combat.relic_count : rs.relic_count;
    const int rn = src_count < kRelicCap ? static_cast<int>(src_count) : kRelicCap;
    out.relic_count = static_cast<uint8_t>(rn);
    for (int i = 0; i < rn; ++i) {
        out.relics[i] = PvRelic{src[i].relic_id, src[i].counter};
    }

    // The full current-act map, plus the burning-elite node drawn on it.
    for (int i = 0; i < kMapRows * kMapCols; ++i) {
        out.map[i] = PvMapNode{rs.map[i].room_type, rs.map[i].edges};
    }
    out.emerald_x = rc.emerald_x;
    out.emerald_y = rc.emerald_y;

    // Screen-flow scalars.
    out.cur_x = rc.cur_x;
    out.room_type = rc.room_type;
    out.combat_outcome = rc.combat_outcome;
    out.pending_bottle = rc.pending_bottle;

    // Consumed encounter prefix + the encounter currently being fought.
    out.monster_cursor = rc.monster_cursor;
    out.elite_cursor = rc.elite_cursor;
    out.boss_cursor = rc.boss_cursor;
    encode_prefix(rc.lists.monster_list.data(), rc.lists.monster_list_count,
                  rc.monster_cursor, out.monster_prefix, kMaxMonsterList);
    encode_prefix(rc.lists.elite_list.data(), rc.lists.elite_list_count,
                  rc.elite_cursor, out.elite_prefix, kMaxEliteList);
    // The act boss is public from the map screen from the moment the act
    // starts, so boss_list[0] is carried whether or not the cursor passed it.
    const int boss_public =
        rc.boss_cursor > 0 ? static_cast<int>(rc.boss_cursor) : 1;
    encode_prefix(rc.lists.boss_list.data(), rc.lists.boss_list_count,
                  boss_public, out.boss_prefix, kMaxBossList);

    // The encounter the player is INSIDE. Entering the room revealed it; the
    // cursor still points at it (it advances on room exit). Event combats are
    // not drawn from these lists and so report 0 -- their monsters are on
    // screen in the combat section instead.
    if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT) ||
        rc.phase == static_cast<uint8_t>(RunPhase::COMBAT_REWARD)) {
        switch (static_cast<RoomType>(rc.room_type)) {
            case RoomType::Monster:
                if (rc.monster_cursor < rc.lists.monster_list_count) {
                    out.current_encounter_id = encounter_id_of(
                        rc.lists.monster_list[rc.monster_cursor]);
                }
                break;
            case RoomType::Elite:
                if (rc.elite_cursor < rc.lists.elite_list_count) {
                    out.current_encounter_id =
                        encounter_id_of(rc.lists.elite_list[rc.elite_cursor]);
                }
                break;
            case RoomType::Boss:
                if (rc.boss_cursor < rc.lists.boss_list_count) {
                    out.current_encounter_id =
                        encounter_id_of(rc.lists.boss_list[rc.boss_cursor]);
                }
                break;
            default:
                break;
        }
    }
}

void encode_rewards(const RewardScreen& s, PublicView& out) noexcept {
    out.rewards.active = 1;
    out.rewards.count = s.count;
    out.rewards.open_card_item = s.open_card_item;
    const int n = s.count < kRewardItemCap ? static_cast<int>(s.count)
                                           : kRewardItemCap;
    for (int i = 0; i < n; ++i) {
        const RunRewardItem& in = s.items[i];
        PvRewardItem& o = out.rewards.items[i];
        o.gold = in.gold;
        o.bonus_gold = in.bonus_gold;
        o.id = in.id;
        o.kind = in.kind;
        o.card_count = in.card_count;
        for (int c = 0; c < kRewardCardCap; ++c) {
            o.card_ids[c] = in.card_ids[c];
            o.card_upgrades[c] = in.card_upgrades[c];
        }
    }
}

void encode_shop(const ShopState& s, PublicView& out) noexcept {
    const auto slot = [](const ShopSlot& in) noexcept {
        return PvShopSlot{in.id, in.price, in.sold, in.upgrade};
    };
    out.shop.active = 1;
    for (int i = 0; i < kShopColoredCount; ++i) out.shop.colored[i] = slot(s.colored[i]);
    for (int i = 0; i < kShopColorlessCount; ++i) out.shop.colorless[i] = slot(s.colorless[i]);
    for (int i = 0; i < kShopRelicCount; ++i) out.shop.relics[i] = slot(s.relics[i]);
    for (int i = 0; i < kShopPotionCount; ++i) out.shop.potions[i] = slot(s.potions[i]);
    out.shop.actual_purge_cost = s.actual_purge_cost;
    out.shop.sale_index = s.sale_index;
    out.shop.screen = s.screen;
    out.shop.purge_available = s.purge_available;
}

void encode_event(const EventDialogState& s, PublicView& out) noexcept {
    out.event.active = 1;
    out.event.event_id = s.event_id;
    out.event.screen = s.screen;
    out.event.grid_kind = s.grid_kind;

    const uint8_t mask = event_scratch_public_mask(s.event_id);
    out.event.scratch_public_mask = mask;
    const int16_t raw[4] = {s.scratch0, s.scratch1, s.scratch2, s.scratch3};
    for (int i = 0; i < 4; ++i) {
        out.event.scratch[i] = (mask & (1u << i)) != 0 ? raw[i] : int16_t{0};
    }

    // The Match-and-Keep board: identities masked until the slot is flipped
    // (scratch1 names the one currently face up) or matched away (taken).
    const bool board_live =
        static_cast<sts::registry::EventId>(s.event_id) ==
        sts::registry::EventId::MATCH_AND_KEEP;
    for (int i = 0; i < kEventBoardCap; ++i) {
        PvEventBoardCard& o = out.event.board[i];
        o.taken = s.board[i].taken;
        const bool revealed =
            board_live && (s.board[i].taken != 0 || s.scratch1 == i);
        o.revealed = revealed ? uint8_t{1} : uint8_t{0};
        if (revealed) {
            o.card_id = s.board[i].card_id;
            o.upgrade = s.board[i].upgrade;
        }
    }
}

void encode_neow(const NeowState& s, PublicView& out) noexcept {
    out.neow.active = 1;
    out.neow.hp_bonus = s.hp_bonus;
    for (int i = 0; i < kNeowGridPickCap; ++i) out.neow.grid_picked[i] = s.grid_picked[i];
    for (int i = 0; i < kNeowOptionCount; ++i) {
        out.neow.option_type[i] = s.option_type[i];
        out.neow.option_drawback[i] = s.option_drawback[i];
    }
    out.neow.screen = s.screen;
    out.neow.chosen = s.chosen;
    out.neow.grid_mode = s.grid_mode;
    out.neow.grid_needed = s.grid_needed;
    out.neow.grid_done = s.grid_done;
}

// Every screen section is gated on the screen being ON SCREEN, never merely on
// the struct being non-empty: a transient screen struct can legitimately hold
// rolls the player has not seen (Dead Adventurer loads rc.rewards with the
// unsearched rewards before its combat begins), and a stale one holds rolls
// from a room already left.
void encode_screens(const RunController& rc, PublicView& out) noexcept {
    const RunPhase phase = static_cast<RunPhase>(rc.phase);

    const bool neow_item_screen =
        phase == RunPhase::NEOW &&
        rc.neow.screen == static_cast<uint8_t>(NeowScreen::ITEM_REWARD);
    if (phase == RunPhase::COMBAT_REWARD || neow_item_screen) {
        encode_rewards(rc.rewards, out);
    }
    if (phase == RunPhase::SHOP) {
        encode_shop(rc.shop, out);
    }
    // The event id survives into ROOM_UNIMPLEMENTED (a resolved event with no
    // body parks there with the selection committed), and it is public: the
    // player is standing in that room. The room-kind guard matters -- a stall
    // on an unimplemented ENCOUNTER parks in the same phase, and rc.event may
    // then still hold a previous room's finished dialog.
    const bool unimplemented_event =
        phase == RunPhase::ROOM_UNIMPLEMENTED &&
        static_cast<RoomType>(rc.room_type) == RoomType::Event;
    if (phase == RunPhase::EVENT_DIALOG || unimplemented_event) {
        encode_event(rc.event, out);
    }
    // NeowState is also the boss chest's re-homed equip-screen storage
    // (boss_chest.hpp), so the grid the player is looking at there must reach
    // the observation too -- and it is gated on the SCREEN being up, never on
    // the struct being non-empty, exactly like every other section here (the
    // blessing fields it also carries are stale floor-0 data by then, but they
    // are public floor-0 data, so carrying them leaks nothing).
    const bool boss_chest_equip_screen =
        phase == RunPhase::BOSS_TREASURE &&
        (rc.boss_chest.screen ==
             static_cast<uint8_t>(BossChestScreen::EQUIP_GRID) ||
         rc.boss_chest.screen ==
             static_cast<uint8_t>(BossChestScreen::EQUIP_ITEM_REWARD));
    if (phase == RunPhase::NEOW || boss_chest_equip_screen) {
        encode_neow(rc.neow, out);
    }
    // The boss chest (boss_chest.hpp). Two reserved v1 fields get their DECLARED
    // meanings here -- the audit note's additive case 1, so no width, no offset
    // and no PUBLIC_VIEW_VERSION moves:
    //   boss_relic_choice_reserved[3] : the three offered relic ids, but ONLY
    //       once the chest has been opened. Before that they are drawn-but-
    //       unseen, the same masking trap the ordinary chest's contents are, and
    //       a twin whose unopened offers differ must encode identically.
    //   chest_opened                  : `seen`, not `screen == RELIC_SELECT` --
    //       a SKIP closes the chest again but the player has still seen inside.
    // chest_size stays 0: a BossChest has no size roll (no getRandomChest call).
    if (phase == RunPhase::BOSS_TREASURE) {
        out.chest_opened = rc.boss_chest.seen;
        if (rc.boss_chest.seen != 0) {
            for (int i = 0; i < kBossChestOfferCount; ++i) {
                out.boss_relic_choice_reserved[i] = rc.boss_chest.relics[i];
            }
        }
    }
    if (phase == RunPhase::REST_SITE) {
        out.rest_screen = rc.rest.screen;
    }
    // TreasureChest: the size is on the room before any interaction; the
    // contents are a construction-time roll revealed only by the open action.
    // The struct stays live through the post-open reward screen and is zeroed
    // when the room is left.
    if (phase == RunPhase::TREASURE_ROOM || phase == RunPhase::COMBAT_REWARD) {
        out.chest_size = rc.treasure_chest.size;
        out.chest_opened = rc.treasure_chest.opened;
        if (rc.treasure_chest.opened != 0) {
            out.chest_relic_tier = rc.treasure_chest.relic_tier;
            out.chest_has_gold = rc.treasure_chest.has_gold;
        }
    }
}

}  // namespace

void encode_public_view(const RunController& rc, PublicView& out) noexcept {
    // Value-init zero-fills every byte -- members, reserved fields and the
    // explicit pad members alike -- so the fills below only write the live
    // data and everything else reads a deterministic zero.
    out = PublicView{};
    out.public_view_version = PUBLIC_VIEW_VERSION;
    out.run_phase = rc.phase;

    // The potion belt is RunState-owned and public in every phase.
    for (int i = 0; i < kPotionCap; ++i) {
        out.potions[i] = rc.run.potions[i];
    }
    out.potion_slot_count = rc.run.potion_slots;

    if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
        encode_combat_section(rc.combat, out);
        // The knowledge layer is combat-scoped (reset by enter_combat) and its
        // draw annotations index the combat section's sorted draw pile, so it
        // is projected here and nowhere else.
        encode_knowledge(rc.combat, rc.knowledge, out);
    }

    encode_always_block(rc, out);
    encode_screens(rc, out);

    // The mask channel (plan §2.1). Filled last so it is unmistakably a
    // function of `rc` alone, exactly like everything above it -- if a legality
    // bit is ever computed from hidden state, the twin tests now see it.
    legal_actions(rc, out.action_mask.mask);
}

}  // namespace sts::engine
