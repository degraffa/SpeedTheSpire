#include "sts/diff/differ.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/engine/types.hpp"

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

// --- enum reprs: "NAME(n)" when known, bare "n" otherwise -------------------

std::string named(const char* name, unsigned long long v) {
    if (name != nullptr) return std::string(name) + "(" + std::to_string(v) + ")";
    return std::to_string(v);
}

std::string phase_repr(uint8_t v) {
    const char* n = nullptr;
    switch (static_cast<CombatPhase>(v)) {
        case CombatPhase::NONE: n = "NONE"; break;
        case CombatPhase::WAITING_ON_USER: n = "WAITING_ON_USER"; break;
        case CombatPhase::RESOLVING: n = "RESOLVING"; break;
        case CombatPhase::COMBAT_OVER: n = "COMBAT_OVER"; break;
    }
    return named(n, v);
}

std::string power_repr(uint16_t v) {
    const char* n = nullptr;
    switch (static_cast<PowerId>(v)) {
        case PowerId::NONE: n = "NONE"; break;
        case PowerId::STRENGTH: n = "STRENGTH"; break;
        case PowerId::VULNERABLE: n = "VULNERABLE"; break;
        case PowerId::WEAK: n = "WEAK"; break;
        // The B3.2 framework powers (Artifact..Rage) have no named repr here yet;
        // they fall through to the raw-value form (named() below). The fixtures
        // carry only the three skeleton powers.
        default: break;
    }
    return named(n, v);
}

std::string card_repr(uint16_t v) {
    const char* n = nullptr;
    switch (static_cast<CardId>(v)) {
        case CardId::NONE: n = "NONE"; break;
        case CardId::STRIKE: n = "STRIKE"; break;
        case CardId::DEFEND: n = "DEFEND"; break;
        case CardId::BASH: n = "BASH"; break;
        case CardId::SHRUG_IT_OFF: n = "SHRUG_IT_OFF"; break;
        case CardId::POMMEL_STRIKE: n = "POMMEL_STRIKE"; break;
    }
    return named(n, v);
}

std::string monster_repr(uint16_t v) {
    const char* n = nullptr;
    switch (static_cast<MonsterId>(v)) {
        case MonsterId::NONE: n = "NONE"; break;
        case MonsterId::JAW_WORM: n = "JAW_WORM"; break;
    }
    return named(n, v);
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

// --- composite helpers ------------------------------------------------------

void cmp_power_slot(DiffReport& r, const std::string& base, const PowerSlot& e,
                    const PowerSlot& a) {
    cmp_power_id(r, base + ".power_id", e.power_id, a.power_id);
    cmp_i(r, base + ".amount", e.amount, a.amount);
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
                cmp_u(r, b + ".relic_id", e.relics[i].relic_id, a.relics[i].relic_id);
                cmp_i(r, b + ".counter", e.relics[i].counter, a.relics[i].counter);
            } else {
                auto repr = [](const RelicSlot& s) {
                    return "{relic_id=" + std::to_string(s.relic_id) + ",counter=" +
                           std::to_string(s.counter) + "}";
                };
                push(r, b, ein ? repr(e.relics[i]) : std::string("(absent)"),
                     ain ? repr(a.relics[i]) : std::string("(absent)"));
            }
        }
    }

    // -- potions (5 positional slots; PotionId per slot, NONE == empty) --
    for (int i = 0; i < kPotionCap; ++i) {
        cmp_u(r, "potions[" + std::to_string(i) + "]", e.potions[i], a.potions[i]);
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
    cmp_u(r, "shop_flags", e.shop_flags, a.shop_flags);

    // -- pity counters riding on the streams --
    cmp_i(r, "card_blizz_randomizer", e.card_blizz_randomizer, a.card_blizz_randomizer);
    cmp_i(r, "blizzard_potion_mod", e.blizzard_potion_mod, a.blizzard_potion_mod);

    // -- the 8 run-level RNG streams (7 run-scoped + act-scoped map_rng), each
    //    named individually so a divergence is attributable to the stream --
    cmp_stream(r, "monster_rng", e.monster_rng, a.monster_rng);
    cmp_stream(r, "event_rng", e.event_rng, a.event_rng);
    cmp_stream(r, "merchant_rng", e.merchant_rng, a.merchant_rng);
    cmp_stream(r, "card_rng", e.card_rng, a.card_rng);
    cmp_stream(r, "treasure_rng", e.treasure_rng, a.treasure_rng);
    cmp_stream(r, "relic_rng", e.relic_rng, a.relic_rng);
    cmp_stream(r, "potion_rng", e.potion_rng, a.potion_rng);
    cmp_stream(r, "map_rng", e.map_rng, a.map_rng);

    return r;
}

}  // namespace sts::diff
