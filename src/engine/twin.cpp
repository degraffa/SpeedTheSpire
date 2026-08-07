// make_hidden_twin -- see twin.hpp for the contract, the delegation to
// resample_hidden, and the KNOWN MASK LEAK the draw pin works around.

#include "sts/engine/twin.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "sts/engine/advance.hpp"
#include "sts/engine/combat_state.hpp"

namespace sts::engine {

bool draw_choice_pending(const RunController& rc) noexcept {
    if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) {
        return false;
    }
    ActionMask m{};
    legal_actions(rc.combat, m);
    return m.choice_pending && m.choice_from_draw;
}

void make_hidden_twin(RunController& rc, SamplerRng& rng) noexcept {
    // The pin is decided BEFORE the resample and restored after it, so the
    // sampler's draw sequence -- and therefore the twin's determinism in
    // `rng` -- is identical either way.
    const bool pin_draw = draw_choice_pending(rc);
    CardPoolIndex saved_draw[kDrawCap];
    if (pin_draw) {
        std::memcpy(saved_draw, rc.combat.draw, sizeof(saved_draw));
    }

    resample_hidden(rc, rng);

    if (pin_draw) {
        std::memcpy(rc.combat.draw, saved_draw, sizeof(saved_draw));
    }
}

RunController make_hidden_twin(const RunController& rc,
                               int64_t twin_seed) noexcept {
    RunController twin;
    // Object-representation copy: see the header note on why `=` is not used.
    std::memcpy(static_cast<void*>(&twin), static_cast<const void*>(&rc),
                sizeof(RunController));
    SamplerRng rng = sampler_rng_from_seed(twin_seed);
    make_hidden_twin(twin, rng);
    return twin;
}

// --- PublicView difference diagnostic ----------------------------------------

namespace {

using FieldSpan = PublicViewFieldSpan;

// Every PublicView member, in layout order. The table is a DIAGNOSTIC, not a
// contract: a member missing from it degrades a failure message, it does not
// weaken a test. `PublicViewFieldTableIsOrderedAndComplete` (tests/twin_test.cpp)
// still checks that it is sorted and ends at sizeof(PublicView), because an
// out-of-order entry would silently mis-name every field after it.
#define STS_PV_FIELD(name) FieldSpan{offsetof(PublicView, name), #name}

constexpr FieldSpan kFields[] = {
    STS_PV_FIELD(public_view_version),
    STS_PV_FIELD(run_phase),
    STS_PV_FIELD(combat_active),
    STS_PV_FIELD(combat_phase),
    STS_PV_FIELD(stance),
    STS_PV_FIELD(turn),
    STS_PV_FIELD(combat_gold),
    STS_PV_FIELD(combat_flags),
    STS_PV_FIELD(player_hp),
    STS_PV_FIELD(player_max_hp),
    STS_PV_FIELD(player_block),
    STS_PV_FIELD(player_energy),
    STS_PV_FIELD(cards_played_this_turn),
    STS_PV_FIELD(player_power_count),
    STS_PV_FIELD(hand_count),
    STS_PV_FIELD(draw_count),
    STS_PV_FIELD(discard_count),
    STS_PV_FIELD(exhaust_count),
    STS_PV_FIELD(limbo_count),
    STS_PV_FIELD(monster_count),
    STS_PV_FIELD(player_powers),
    STS_PV_FIELD(hand),
    STS_PV_FIELD(draw),
    STS_PV_FIELD(discard),
    STS_PV_FIELD(exhaust),
    STS_PV_FIELD(limbo),
    STS_PV_FIELD(monsters),
    STS_PV_FIELD(potions),
    STS_PV_FIELD(potion_slot_count),
    STS_PV_FIELD(keys_reserved),
    STS_PV_FIELD(boss_relic_choice_reserved),
    STS_PV_FIELD(second_boss_reserved),
    STS_PV_FIELD(act_reserved),
    STS_PV_FIELD(pad_tail),
    STS_PV_FIELD(gold),
    STS_PV_FIELD(event_pity_monster),
    STS_PV_FIELD(event_pity_shop),
    STS_PV_FIELD(event_pity_treasure),
    STS_PV_FIELD(event_flags),
    STS_PV_FIELD(shop_flags),
    STS_PV_FIELD(run_hp),
    STS_PV_FIELD(run_max_hp),
    STS_PV_FIELD(card_blizz_randomizer),
    STS_PV_FIELD(blizzard_potion_mod),
    STS_PV_FIELD(purge_cost),
    STS_PV_FIELD(floor),
    STS_PV_FIELD(master_deck_count),
    STS_PV_FIELD(event_membership),
    STS_PV_FIELD(special_membership),
    STS_PV_FIELD(boss_ids),
    STS_PV_FIELD(ascension),
    STS_PV_FIELD(shrine_membership),
    STS_PV_FIELD(relic_count),
    STS_PV_FIELD(cur_x),
    STS_PV_FIELD(room_type),
    STS_PV_FIELD(combat_outcome),
    STS_PV_FIELD(pending_bottle),
    STS_PV_FIELD(emerald_x),
    STS_PV_FIELD(emerald_y),
    STS_PV_FIELD(monster_cursor),
    STS_PV_FIELD(elite_cursor),
    STS_PV_FIELD(boss_cursor),
    STS_PV_FIELD(current_encounter_id),
    STS_PV_FIELD(rest_screen),
    STS_PV_FIELD(chest_size),
    STS_PV_FIELD(chest_relic_tier),
    STS_PV_FIELD(chest_has_gold),
    STS_PV_FIELD(chest_opened),
    STS_PV_FIELD(knowledge_chain_count),
    STS_PV_FIELD(knowledge_exact_prefix),
    STS_PV_FIELD(knowledge_full_order),
    STS_PV_FIELD(monster_roll_known),
    STS_PV_FIELD(monster_roll),
    STS_PV_FIELD(monster_prefix),
    STS_PV_FIELD(elite_prefix),
    STS_PV_FIELD(boss_prefix),
    STS_PV_FIELD(draw_constraint_rank),
    STS_PV_FIELD(draw_exact_pos),
    STS_PV_FIELD(master_deck),
    STS_PV_FIELD(relics),
    STS_PV_FIELD(map),
    STS_PV_FIELD(rewards),
    STS_PV_FIELD(shop),
    STS_PV_FIELD(event),
    STS_PV_FIELD(neow),
    STS_PV_FIELD(action_mask),
    // v3 (S2.13) tail append -- the FIRED bitset's second word. This table is
    // scanned in ascending offset order by public_view_field_at, so a tail
    // append belongs at the end here too.
    STS_PV_FIELD(event_flags_hi),
};

#undef STS_PV_FIELD

constexpr std::size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

}  // namespace

const char* public_view_field_at(std::size_t offset) noexcept {
    if (offset >= sizeof(PublicView)) {
        return "<out of range>";
    }
    const char* best = "<before the first field>";
    for (std::size_t i = 0; i < kFieldCount; ++i) {
        if (kFields[i].offset <= offset) {
            best = kFields[i].name;
        } else {
            break;
        }
    }
    return best;
}

PublicViewDiff public_view_first_difference(const PublicView& a,
                                            const PublicView& b) noexcept {
    const auto* pa = reinterpret_cast<const unsigned char*>(&a);
    const auto* pb = reinterpret_cast<const unsigned char*>(&b);
    for (std::size_t i = 0; i < sizeof(PublicView); ++i) {
        if (pa[i] != pb[i]) {
            return PublicViewDiff{false, i, public_view_field_at(i)};
        }
    }
    return PublicViewDiff{true, 0, ""};
}

std::size_t public_view_field_count() noexcept { return kFieldCount; }

PublicViewFieldSpan public_view_field(std::size_t index) noexcept {
    if (index >= kFieldCount) {
        return PublicViewFieldSpan{sizeof(PublicView), "<out of range>"};
    }
    return kFields[index];
}

}  // namespace sts::engine
