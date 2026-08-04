// encode_public_view -- see include/sts/engine/public_view.hpp for the
// contract and docs/public-view-audit.md for the field-by-field audit this
// encoder is held to.

#include "sts/engine/public_view.hpp"

#include <algorithm>

#include "sts/engine/relic_hooks.hpp"  // player_has_relic (Runic Dome)

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

    // Player powers: the full list (the ObsBuffer stub carries none).
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
    // encode_observation does (observation.hpp carries the full provenance
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
    }
}

}  // namespace sts::engine
