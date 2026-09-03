#include "sts/diff/differ.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"  // generated: the id -> game_id tables

namespace sts::diff {

using namespace sts::engine;

// --- DiffReport helpers -----------------------------------------------------

bool DiffReport::mentions(const std::string& substr) const {
    for (const FieldDiff& d : diffs) {
        if (d.field_name.find(substr) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string DiffReport::to_string() const {
    std::ostringstream os;
    for (const FieldDiff& d : diffs) {
        os << d.field_name << ": " << d.expected_repr << " -> " << d.actual_repr
           << "\n";
    }
    return os.str();
}

namespace {

// --- primitive push / compare ----------------------------------------------

void push(DiffReport& r, std::string name, std::string e, std::string a) {
    r.diffs.push_back(FieldDiff{std::move(name), std::move(e), std::move(a)});
}

void cmp_i(DiffReport& r, const std::string& name, long long e, long long a) {
    if (e != a) push(r, name, std::to_string(e), std::to_string(a));
}

void cmp_u(DiffReport& r, const std::string& name, unsigned long long e,
           unsigned long long a) {
    if (e != a) push(r, name, std::to_string(e), std::to_string(a));
}

// Exact float compare (B4.3 event-pity floats): a replay is deterministic, so
// any bit difference is a real divergence. Bit-compare avoids treating NaN as
// "equal to itself" wrongly and catches -0.0 vs 0.0; the repr is the value.
void cmp_f(DiffReport& r, const std::string& name, float e, float a) {
    if (std::memcmp(&e, &a, sizeof(float)) != 0) {
        push(r, name, std::to_string(e), std::to_string(a));
    }
}

// --- enum reprs: "NAME(n)", driven by the generated registry tables ---------
//
// Every id enum (CardId/PowerId/MonsterId/RelicId/PotionId) is generated from
// registry/*.yaml, and the same generator emits the matching id -> string
// table into the build tree as sts/registry/game_ids.hpp. The reprs below read
// that table rather than restating it, so they cannot fall behind the registry:
// the hand-written switches they replace had drifted to 6 of ~76 cards, 4 of
// ~27 powers and 8 of ~11 monsters, and every id past the end printed as a
// bare integer -- exactly the values a human is trying to identify when a
// golden fixture or an oracle campaign diverges.
//
// The string shown is the registry's *game id* ("Shrug It Off", "Inflame",
// "SpikeSlime_L"), not the C++ enum symbol ("SHRUG_IT_OFF"). It is the only
// complete table the generator emits, and it is the exact spelling carried by
// the oracle's captured JSON, so a divergence line can be grepped straight
// against a capture. The numeric is always kept alongside it, so the raw value
// that the trace stores (and that triage greps for) is still in the output.
//
// This is presentation only: the trace container is a binary struct dump
// (trace.cpp), these strings never reach it, and no comparison decision is
// made from a repr.

// Renders "NAME(n)". `name` is what the generated accessor returned, which is
// empty for BOTH the zero sentinel and any value outside the enum, so the two
// are distinguished here. An unknown id -- from a newer schema, or plain
// garbage -- still renders rather than crashing: a differ is a debugging tool
// and must survive whatever it is handed.
std::string named(std::string_view name, unsigned long long v) {
    std::string out;
    if (!name.empty()) {
        out.assign(name);
    } else {
        out.assign(v == 0 ? "NONE" : "UNKNOWN");
    }
    out += '(';
    out += std::to_string(v);
    out += ')';
    return out;
}

// CombatPhase is hand-written engine API (combat_state.hpp), not registry
// content, so there is no generated table to drive it from and it keeps an
// explicit switch. All four enumerators are listed, so the switch is
// exhaustive; an out-of-range byte leaves `n` empty and falls to named()'s
// unknown form.
std::string phase_repr(uint8_t v) {
    std::string_view n;
    switch (static_cast<CombatPhase>(v)) {
        case CombatPhase::NONE: n = "NONE"; break;
        case CombatPhase::WAITING_ON_USER: n = "WAITING_ON_USER"; break;
        case CombatPhase::RESOLVING: n = "RESOLVING"; break;
        case CombatPhase::COMBAT_OVER: n = "COMBAT_OVER"; break;
    }
    return named(n, v);
}

std::string power_repr(uint16_t v) {
    return named(registry::power_game_id(static_cast<registry::PowerId>(v)), v);
}

std::string card_repr(uint16_t v) {
    return named(registry::card_game_id(static_cast<registry::CardId>(v)), v);
}

std::string monster_repr(uint16_t v) {
    return named(registry::monster_game_id(static_cast<registry::MonsterId>(v)), v);
}

std::string relic_repr(uint16_t v) {
    return named(registry::relic_game_id(static_cast<registry::RelicId>(v)), v);
}

std::string potion_repr(uint16_t v) {
    return named(registry::potion_game_id(static_cast<registry::PotionId>(v)), v);
}

void cmp_phase(DiffReport& r, const std::string& name, uint8_t e, uint8_t a) {
    if (e != a) push(r, name, phase_repr(e), phase_repr(a));
}

void cmp_power_id(DiffReport& r, const std::string& name, uint16_t e, uint16_t a) {
    if (e != a) push(r, name, power_repr(e), power_repr(a));
}

void cmp_card_id(DiffReport& r, const std::string& name, uint16_t e, uint16_t a) {
    if (e != a) push(r, name, card_repr(e), card_repr(a));
}

void cmp_monster_id(DiffReport& r, const std::string& name, uint16_t e, uint16_t a) {
    if (e != a) push(r, name, monster_repr(e), monster_repr(a));
}

void cmp_relic_id(DiffReport& r, const std::string& name, uint16_t e, uint16_t a) {
    if (e != a) push(r, name, relic_repr(e), relic_repr(a));
}

void cmp_potion_id(DiffReport& r, const std::string& name, uint16_t e, uint16_t a) {
    if (e != a) push(r, name, potion_repr(e), potion_repr(a));
}

// --- composite helpers ------------------------------------------------------

void cmp_power_slot(DiffReport& r, const std::string& base, const PowerSlot& e,
                    const PowerSlot& a) {
    cmp_power_id(r, base + ".power_id", e.power_id, a.power_id);
    cmp_i(r, base + ".amount", e.amount, a.amount);
    // The schema-6 second number (Panache's accumulated damage, The Bomb's
    // per-instance damage). 0 for every power that declares no meaning for it,
    // so this line reports nothing on a state built from pre-schema-6 content.
    cmp_i(r, base + ".counter", e.counter, a.counter);
}

void cmp_powers(DiffReport& r, const std::string& base, const PowerSlot* e,
                const PowerSlot* a) {
    for (int i = 0; i < kPowerCap; ++i) {
        cmp_power_slot(r, base + "[" + std::to_string(i) + "]", e[i], a[i]);
    }
}

// A pile is an index array + a count. Compare the count, then the live members
// over [0, max(count)); a member present on only one side reads "(absent)".
void cmp_pile(DiffReport& r, const char* name, const CardPoolIndex* e, uint8_t ec,
              const CardPoolIndex* a, uint8_t ac) {
    cmp_u(r, std::string(name) + "_count", ec, ac);
    const int n = std::max<int>(ec, ac);
    for (int i = 0; i < n; ++i) {
        const bool ein = i < ec;
        const bool ain = i < ac;
        std::string ev = ein ? std::to_string(e[i]) : std::string("(absent)");
        std::string av = ain ? std::to_string(a[i]) : std::string("(absent)");
        if (ev != av) {
            push(r, std::string(name) + "[" + std::to_string(i) + "]",
                 std::move(ev), std::move(av));
        }
    }
}

std::string item_repr(const ActionQueueItem& it) {
    std::ostringstream os;
    os << "{op=" << it.opcode << ",src=" << static_cast<int>(it.src)
       << ",tgt=" << static_cast<int>(it.tgt) << ",amt=" << it.amount
       << ",flags=" << it.flags << "}";
    return os.str();
}

void cmp_ring_item(DiffReport& r, const std::string& base,
                   const ActionQueueItem* e, const ActionQueueItem* a) {
    if (e == nullptr && a == nullptr) return;
    if (e == nullptr || a == nullptr) {
        push(r, base, e ? item_repr(*e) : std::string("(absent)"),
             a ? item_repr(*a) : std::string("(absent)"));
        return;
    }
    cmp_u(r, base + ".opcode", e->opcode, a->opcode);
    cmp_u(r, base + ".src", e->src, a->src);
    cmp_u(r, base + ".tgt", e->tgt, a->tgt);
    cmp_i(r, base + ".amount", e->amount, a->amount);
    cmp_u(r, base + ".flags", e->flags, a->flags);
}

// ActionQueueItem ring (main action queue / pre-turn queue): compare count and
// the live items in logical order (walked via head), NOT the raw backing array
// (stale scratch past count) nor head/tail cursors (internal rotation).
void cmp_item_ring(DiffReport& r, const char* name, const ActionQueueItem* earr,
                   uint8_t eh, uint8_t ec, const ActionQueueItem* aarr,
                   uint8_t ah, uint8_t ac, int cap) {
    cmp_u(r, std::string(name) + ".count", ec, ac);
    const int n = std::max<int>(ec, ac);
    for (int i = 0; i < n; ++i) {
        const ActionQueueItem* ei = (i < ec) ? &earr[(eh + i) % cap] : nullptr;
        const ActionQueueItem* ai = (i < ac) ? &aarr[(ah + i) % cap] : nullptr;
        cmp_ring_item(r, std::string(name) + "[" + std::to_string(i) + "]", ei, ai);
    }
}

void cmp_monster(DiffReport& r, int m, const MonsterState& e,
                 const MonsterState& a) {
    const std::string b = "monsters[" + std::to_string(m) + "]";
    cmp_monster_id(r, b + ".monster_id", e.monster_id, a.monster_id);
    cmp_i(r, b + ".hp", e.hp, a.hp);
    cmp_i(r, b + ".max_hp", e.max_hp, a.max_hp);
    cmp_i(r, b + ".block", e.block, a.block);
    cmp_u(r, b + ".flags", e.flags, a.flags);
    for (int k = 0; k < 3; ++k) {
        cmp_u(r, b + ".move_history[" + std::to_string(k) + "]",
              e.move_history[k], a.move_history[k]);
    }
    cmp_u(r, b + ".intent", e.intent, a.intent);
    cmp_u(r, b + ".power_count", e.power_count, a.power_count);
    cmp_powers(r, b + ".powers", e.powers, a.powers);
}

void cmp_stream(DiffReport& r, const char* name, const RngStream& e,
                const RngStream& a) {
    cmp_u(r, std::string(name) + ".s0", e.s0, a.s0);
    cmp_u(r, std::string(name) + ".s1", e.s1, a.s1);
    cmp_i(r, std::string(name) + ".counter", e.counter, a.counter);
    // `pad` deliberately not compared (value-init-zeroed padding, not state).
}

// --- RunState composite helpers (B1.6) --------------------------------------

// One master-deck card instance: same field set the combat differ walks over
// card_pool, so a deck divergence reads `master_deck[i].card_id` etc.
void cmp_card_instance(DiffReport& r, const std::string& base, const CardInstance& e,
                       const CardInstance& a) {
    cmp_card_id(r, base + ".card_id", e.card_id, a.card_id);
    cmp_u(r, base + ".upgrade", e.upgrade, a.upgrade);
    cmp_u(r, base + ".cost_now", e.cost_now, a.cost_now);
    cmp_u(r, base + ".flags", e.flags, a.flags);
    cmp_u(r, base + ".misc", e.misc, a.misc);
}

std::string card_instance_repr(const CardInstance& c) {
    std::ostringstream os;
    os << "{card_id=" << card_repr(c.card_id) << ",upgrade=" << static_cast<int>(c.upgrade)
       << ",cost=" << static_cast<int>(c.cost_now) << "}";
    return os.str();
}

// A counted array of CardInstance (master deck): compare the count, then the
// live members over [0, max(count)); a member present on only one side reads
// "(absent)". Order is meaningful and compared positionally.
void cmp_deck(DiffReport& r, const char* name, const CardInstance* e, uint16_t ec,
              const CardInstance* a, uint16_t ac) {
    cmp_u(r, std::string(name) + "_count", ec, ac);
    const int n = std::max<int>(ec, ac);
    for (int i = 0; i < n; ++i) {
        const std::string b = std::string(name) + "[" + std::to_string(i) + "]";
        const bool ein = i < ec;
        const bool ain = i < ac;
        if (ein && ain) {
            cmp_card_instance(r, b, e[i], a[i]);
        } else {
            push(r, b, ein ? card_instance_repr(e[i]) : std::string("(absent)"),
                 ain ? card_instance_repr(a[i]) : std::string("(absent)"));
        }
    }
}

}  // namespace

// --- diff_states ------------------------------------------------------------

DiffReport diff_states(const CombatState& e, const CombatState& a) {
    DiffReport r;

    // Fast path: equal content hashes => byte-identical value-initialized states
    // (design doc §4.1), so there is nothing to walk.
    if (hash_state(e) == hash_state(a)) {
        return r;
    }

    // -- header --
    cmp_phase(r, "phase", e.phase, a.phase);
    cmp_u(r, "turn", e.turn, a.turn);
    cmp_u(r, "flags", e.flags, a.flags);

    // -- player scalars --
    cmp_i(r, "player.hp", e.player_hp, a.player_hp);
    cmp_i(r, "player.max_hp", e.player_max_hp, a.player_max_hp);
    cmp_i(r, "player.block", e.player_block, a.player_block);
    cmp_i(r, "player.energy", e.player_energy, a.player_energy);
    cmp_u(r, "player.stance", e.stance, a.stance);
    cmp_u(r, "player.cards_played_this_turn", e.cards_played_this_turn,
          a.cards_played_this_turn);
    cmp_u(r, "player.power_count", e.player_power_count, a.player_power_count);
    cmp_powers(r, "player_powers", e.player_powers, a.player_powers);

    // -- shared card pool (all rows; unused rows are value-init zeroed) --
    for (int i = 0; i < kCardPoolCap; ++i) {
        const CardInstance& ce = e.card_pool[i];
        const CardInstance& ca = a.card_pool[i];
        const std::string b = "card_pool[" + std::to_string(i) + "]";
        cmp_card_id(r, b + ".card_id", ce.card_id, ca.card_id);
        cmp_u(r, b + ".upgrade", ce.upgrade, ca.upgrade);
        cmp_u(r, b + ".cost_now", ce.cost_now, ca.cost_now);
        cmp_u(r, b + ".flags", ce.flags, ca.flags);
        cmp_u(r, b + ".misc", ce.misc, ca.misc);
    }

    // -- piles --
    cmp_pile(r, "hand", e.hand, e.hand_count, a.hand, a.hand_count);
    cmp_pile(r, "draw", e.draw, e.draw_count, a.draw, a.draw_count);
    cmp_pile(r, "discard", e.discard, e.discard_count, a.discard, a.discard_count);
    cmp_pile(r, "exhaust", e.exhaust, e.exhaust_count, a.exhaust, a.exhaust_count);
    cmp_pile(r, "limbo", e.limbo, e.limbo_count, a.limbo, a.limbo_count);

    // -- in-combat gold accrual (settled into RunState at the fold-back) --
    cmp_u(r, "combat_gold", e.combat_gold, a.combat_gold);

    // -- monsters --
    cmp_u(r, "monster_count", e.monster_count, a.monster_count);
    for (int m = 0; m < kMonsterCap; ++m) {
        cmp_monster(r, m, e.monsters[m], a.monsters[m]);
    }

    // -- queues (see cmp_item_ring: logical live items only) --
    cmp_item_ring(r, "action_queue", e.action_queue, e.action_head, e.action_count,
                  a.action_queue, a.action_head, a.action_count, kActionQueueCap);
    cmp_item_ring(r, "pre_turn_actions", e.pre_turn_actions, e.pre_turn_head,
                  e.pre_turn_count, a.pre_turn_actions, a.pre_turn_head,
                  a.pre_turn_count, kPreTurnActionQueueCap);

    // card queue: simple [0,count) array of CardQueueItem.
    cmp_u(r, "card_queue.count", e.card_queue_count, a.card_queue_count);
    {
        const int n = std::max<int>(e.card_queue_count, a.card_queue_count);
        for (int i = 0; i < n; ++i) {
            const bool ein = i < e.card_queue_count;
            const bool ain = i < a.card_queue_count;
            const std::string b = "card_queue[" + std::to_string(i) + "]";
            if (ein && ain) {
                cmp_u(r, b + ".card_index", e.card_queue[i].card_index,
                      a.card_queue[i].card_index);
                cmp_u(r, b + ".target", e.card_queue[i].target,
                      a.card_queue[i].target);
            } else {
                push(r, b,
                     ein ? "{card_index=" + std::to_string(e.card_queue[i].card_index) +
                               ",target=" + std::to_string(e.card_queue[i].target) + "}"
                         : std::string("(absent)"),
                     ain ? "{card_index=" + std::to_string(a.card_queue[i].card_index) +
                               ",target=" + std::to_string(a.card_queue[i].target) + "}"
                         : std::string("(absent)"));
            }
        }
    }

    // monster queue: simple [0,count) array of MonsterQueueItem.
    cmp_u(r, "monster_queue.count", e.monster_queue_count, a.monster_queue_count);
    {
        const int n = std::max<int>(e.monster_queue_count, a.monster_queue_count);
        for (int i = 0; i < n; ++i) {
            const bool ein = i < e.monster_queue_count;
            const bool ain = i < a.monster_queue_count;
            const std::string b = "monster_queue[" + std::to_string(i) + "]";
            if (ein && ain) {
                cmp_u(r, b + ".monster_index", e.monster_queue[i].monster_index,
                      a.monster_queue[i].monster_index);
                cmp_u(r, b + ".flags", e.monster_queue[i].flags,
                      a.monster_queue[i].flags);
            } else {
                push(r, b,
                     ein ? "{monster_index=" +
                               std::to_string(e.monster_queue[i].monster_index) + "}"
                         : std::string("(absent)"),
                     ain ? "{monster_index=" +
                               std::to_string(a.monster_queue[i].monster_index) + "}"
                         : std::string("(absent)"));
            }
        }
    }

    // -- bookkeeping flags (queue-adjacent) --
    cmp_u(r, "turn_has_ended", e.turn_has_ended, a.turn_has_ended);
    cmp_u(r, "monster_attacks_queued", e.monster_attacks_queued,
          a.monster_attacks_queued);

    // -- RNG streams (each named individually so a divergence is attributable to
    //    the specific stream) --
    cmp_stream(r, "monster_hp_rng", e.monster_hp_rng, a.monster_hp_rng);
    cmp_stream(r, "ai_rng", e.ai_rng, a.ai_rng);
    cmp_stream(r, "shuffle_rng", e.shuffle_rng, a.shuffle_rng);
    cmp_stream(r, "card_random_rng", e.card_random_rng, a.card_random_rng);
    cmp_stream(r, "misc_rng", e.misc_rng, a.misc_rng);

    return r;
}

// --- diff_run_states (B1.6) -------------------------------------------------

DiffReport diff_run_states(const RunState& e, const RunState& a) {
    DiffReport r;

    // Fast path: byte-identical value-initialized PODs (padding is value-init
    // zeroed, so byte equality == logical equality). Mirrors diff_states' hash
    // fast path with a plain memcmp (RunState has no dedicated hash).
    if (std::memcmp(&e, &a, sizeof(RunState)) == 0) {
        return r;
    }

    // -- character sheet --
    cmp_i(r, "run_seed", e.run_seed, a.run_seed);
    cmp_i(r, "hp", e.hp, a.hp);
    cmp_i(r, "max_hp", e.max_hp, a.max_hp);
    cmp_i(r, "gold", e.gold, a.gold);
    cmp_u(r, "ascension", e.ascension, a.ascension);
    cmp_u(r, "act", e.act, a.act);
    cmp_u(r, "floor", e.floor, a.floor);
    // The schema-v9 pair (S3.31). `victory_kind` has no dump counterpart --
    // the game's victory / trueVictor are Metrics upload fields, not run state
    // -- so the expected side is derived once, on a victory artifact's last
    // record, from the driver's own terminal verdict (translate.cpp).
    // `act4_floor_base` is written by the Act-4 crossing (S3.32) and is 0 on
    // both sides until then; it is compared from today so the crossing's first
    // capture cannot land it silently.
    cmp_u(r, "victory_kind", e.victory_kind, a.victory_kind);
    cmp_u(r, "act4_floor_base", e.act4_floor_base, a.act4_floor_base);

    // -- master deck (counted, in order) --
    cmp_deck(r, "master_deck", e.master_deck, e.master_deck_count, a.master_deck,
             a.master_deck_count);

    // -- relics (counted; acquisition order == trigger order, so positional) --
    cmp_u(r, "relic_count", e.relic_count, a.relic_count);
    {
        const int n = std::max<int>(e.relic_count, a.relic_count);
        for (int i = 0; i < n; ++i) {
            const std::string b = "relics[" + std::to_string(i) + "]";
            const bool ein = i < e.relic_count;
            const bool ain = i < a.relic_count;
            if (ein && ain) {
                cmp_relic_id(r, b + ".relic_id", e.relics[i].relic_id,
                             a.relics[i].relic_id);
                cmp_i(r, b + ".counter", e.relics[i].counter, a.relics[i].counter);
            } else {
                auto repr = [](const RelicSlot& s) {
                    return "{relic_id=" + relic_repr(s.relic_id) + ",counter=" +
                           std::to_string(s.counter) + "}";
                };
                push(r, b, ein ? repr(e.relics[i]) : std::string("(absent)"),
                     ain ? repr(a.relics[i]) : std::string("(absent)"));
            }
        }
    }

    // -- potions (5 positional slots; PotionId per slot, NONE == empty) --
    for (int i = 0; i < kPotionCap; ++i) {
        cmp_potion_id(r, "potions[" + std::to_string(i) + "]", e.potions[i],
                      a.potions[i]);
    }

    // -- map grid (flattened row-major; placeholder encoding until B4.x) --
    for (int i = 0; i < kMapRows * kMapCols; ++i) {
        const std::string b = "map[" + std::to_string(i) + "]";
        cmp_u(r, b + ".room_type", e.map[i].room_type, a.map[i].room_type);
        cmp_u(r, b + ".edges", e.map[i].edges, a.map[i].edges);
    }

    // -- boss / keys / event-shop placeholders (real semantics at B4.3) --
    for (int i = 0; i < kBossIdCap; ++i) {
        cmp_u(r, "boss_ids[" + std::to_string(i) + "]", e.boss_ids[i], a.boss_ids[i]);
    }
    cmp_u(r, "keys", e.keys, a.keys);
    cmp_u(r, "event_flags", e.event_flags, a.event_flags);
    // The FIRED bitset's second word (event ids 32..63, S2.13). It MUST be
    // compared: the translator writes it from Act-2/3 captures, so a field the
    // differ skipped would be a permanent false-green on exactly the campaigns
    // that first exercise it.
    cmp_u(r, "event_flags_hi", e.event_flags_hi, a.event_flags_hi);
    cmp_u(r, "shop_flags", e.shop_flags, a.shop_flags);

    // -- pity counters riding on the streams --
    cmp_i(r, "card_blizz_randomizer", e.card_blizz_randomizer, a.card_blizz_randomizer);
    cmp_i(r, "blizzard_potion_mod", e.blizzard_potion_mod, a.blizzard_potion_mod);

    // -- schema-v3 additive run inventory (B4.3) --
    // event-pity chances (floats) + shop purge cost + potion-slot count.
    cmp_f(r, "event_pity_monster", e.event_pity_monster, a.event_pity_monster);
    cmp_f(r, "event_pity_shop", e.event_pity_shop, a.event_pity_shop);
    cmp_f(r, "event_pity_treasure", e.event_pity_treasure, a.event_pity_treasure);
    cmp_i(r, "purge_cost", e.purge_cost, a.purge_cost);
    cmp_u(r, "potion_slots", e.potion_slots, a.potion_slots);

    // event/shrine/special pool-membership bitsets (remaining-pool view).
    cmp_u(r, "event_membership", e.event_membership, a.event_membership);
    cmp_u(r, "special_membership", e.special_membership, a.special_membership);
    cmp_u(r, "shrine_membership", e.shrine_membership, a.shrine_membership);

    // relic-pool orders, all 5 tiers: per-tier count + live members [0, count).
    // Order is load-bearing (front/end pop, trap 15), so compared positionally.
    // The members are RelicIds, so they render as names; the numeric stays
    // inside the repr, so an equal pair still compares equal and a differing
    // pair still differs -- the comparison is unchanged, only the rendering.
    for (int t = 0; t < kRelicTierCount; ++t) {
        const std::string tb = "relic_pool[" + std::to_string(t) + "]";
        cmp_u(r, tb + ".count", e.relic_pool_count[t], a.relic_pool_count[t]);
        const int n = std::max<int>(e.relic_pool_count[t], a.relic_pool_count[t]);
        for (int i = 0; i < n; ++i) {
            const std::string b = tb + "[" + std::to_string(i) + "]";
            const bool ein = i < e.relic_pool_count[t];
            const bool ain = i < a.relic_pool_count[t];
            std::string ev = ein ? relic_repr(e.relic_pools[t][i]) : std::string("(absent)");
            std::string av = ain ? relic_repr(a.relic_pools[t][i]) : std::string("(absent)");
            if (ev != av) push(r, b, std::move(ev), std::move(av));
        }
    }

    // -- the boss-chest offers (schema v8 / S2.47): the three entry-popped BOSS
    //    relics plus the reveal bits. Compared UNCONDITIONALLY, like every other
    //    field -- comparability policy (the capture attests the offers only on a
    //    BOSS_REWARD dump) lives in the replay tool's neutralization layer, not
    //    here. The offers render as relic names so a divergence line greps
    //    straight against a capture. This group is what scores design §6 S2-G2
    //    item 2's zero-diff boss-relic pick. --
    for (int i = 0; i < engine::kBossChestOfferCount; ++i) {
        cmp_relic_id(r, "boss_chest.relics[" + std::to_string(i) + "]",
                     e.boss_chest.relics[i], a.boss_chest.relics[i]);
    }
    cmp_u(r, "boss_chest.screen", e.boss_chest.screen, a.boss_chest.screen);
    cmp_u(r, "boss_chest.seen", e.boss_chest.seen, a.boss_chest.seen);
    cmp_u(r, "boss_chest.chose_relic", e.boss_chest.chose_relic,
          a.boss_chest.chose_relic);

    // -- the 9 run-level RNG streams (7 run-scoped + act-scoped map_rng + the
    //    event-scoped neow_rng, B4.3), each named individually so a divergence is
    //    attributable to the stream --
    cmp_stream(r, "monster_rng", e.monster_rng, a.monster_rng);
    cmp_stream(r, "event_rng", e.event_rng, a.event_rng);
    cmp_stream(r, "merchant_rng", e.merchant_rng, a.merchant_rng);
    cmp_stream(r, "card_rng", e.card_rng, a.card_rng);
    cmp_stream(r, "treasure_rng", e.treasure_rng, a.treasure_rng);
    cmp_stream(r, "relic_rng", e.relic_rng, a.relic_rng);
    cmp_stream(r, "potion_rng", e.potion_rng, a.potion_rng);
    cmp_stream(r, "map_rng", e.map_rng, a.map_rng);
    cmp_stream(r, "neow_rng", e.neow_rng, a.neow_rng);

    return r;
}

}  // namespace sts::diff
