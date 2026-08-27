// STS-SCRIPT v1 emission. See script.hpp for the artifact's contract and the
// planner README for the normative step-kind schema.

#include "sts/planner/script.hpp"

#include <cstdio>
#include <memory>

#include "sts/fuzz/fuzz_run.hpp"  // hash_controller (the replay fidelity check)

#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/interp.hpp"  // discovery_choice_card
#include "sts/engine/map_gen.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/schema.hpp"
#include "sts/engine/shop.hpp"
#include "sts/planner/seed_scan.hpp"  // json_escape
#include "sts/registry/game_ids.hpp"

namespace sts::planner {

using engine::Action;
using engine::ActionVerb;
using engine::RunActionMask;
using engine::RunController;
using engine::RunPhase;

namespace {

[[nodiscard]] const char* phase_text(uint8_t p) noexcept {
    switch (static_cast<RunPhase>(p)) {
        case RunPhase::NONE: return "NONE";
        case RunPhase::NEOW: return "NEOW";
        case RunPhase::MAP_CHOICE: return "MAP_CHOICE";
        case RunPhase::COMBAT: return "COMBAT";
        case RunPhase::COMBAT_REWARD: return "COMBAT_REWARD";
        case RunPhase::ROOM_UNIMPLEMENTED: return "ROOM_UNIMPLEMENTED";
        case RunPhase::RUN_OVER: return "RUN_OVER";
        case RunPhase::REST_SITE: return "REST_SITE";
        case RunPhase::TREASURE_ROOM: return "TREASURE_ROOM";
        case RunPhase::EVENT_DIALOG: return "EVENT_DIALOG";
        case RunPhase::SHOP: return "SHOP";
        case RunPhase::BOSS_TREASURE: return "BOSS_TREASURE";
    }
    return "?";
}

[[nodiscard]] const char* reward_kind_text(engine::RewardItemKind k) noexcept {
    switch (k) {
        case engine::RewardItemKind::GOLD: return "GOLD";
        case engine::RewardItemKind::POTION: return "POTION";
        case engine::RewardItemKind::RELIC: return "RELIC";
        case engine::RewardItemKind::CARDS: return "CARD";
        case engine::RewardItemKind::STOLEN_GOLD: return "STOLEN_GOLD";
        case engine::RewardItemKind::NONE: break;
    }
    return "NONE";
}

struct Json {
    std::string s;
    bool first = true;
    void open() { s += '{'; }
    void close() { s += '}'; }
    void key(const char* k) {
        if (!first) s += ',';
        first = false;
        s += '"';
        s += k;
        s += "\":";
    }
    void kv(const char* k, long long v) {
        key(k);
        s += std::to_string(v);
    }
    void kv(const char* k, const std::string& v) {
        key(k);
        s += '"' + json_escape(v) + '"';
    }
    void kv(const char* k, std::string_view v) {
        kv(k, std::string(v));
    }
    void kv(const char* k, const char* v) { kv(k, std::string(v)); }
    void kvb(const char* k, bool v) {
        key(k);
        s += v ? "true" : "false";
    }
};

// nth occurrence of (card_id, upgrade) among hand slots before `slot` -- the
// position disambiguator the live matcher counts the same way over the dump's
// hand list.
[[nodiscard]] int hand_ordinal(const engine::CombatState& cs,
                               uint8_t slot) noexcept {
    const engine::CardInstance& c = cs.card_pool[cs.hand[slot]];
    int ord = 0;
    for (uint8_t i = 0; i < slot && i < cs.hand_count; ++i) {
        const engine::CardInstance& o = cs.card_pool[cs.hand[i]];
        if (o.card_id == c.card_id && o.upgrade == c.upgrade) ++ord;
    }
    return ord;
}

// nth occurrence of the master-deck row's identity before `index`, in
// master-deck order (the dump's deck/grid order is the same master order).
[[nodiscard]] int deck_ordinal(const engine::RunState& rs,
                               uint16_t index) noexcept {
    const engine::CardInstance& c = rs.master_deck[index];
    int ord = 0;
    for (uint16_t i = 0; i < index && i < engine::kMasterDeckCap; ++i) {
        const engine::CardInstance& o = rs.master_deck[i];
        if (o.card_id == c.card_id && o.upgrade == c.upgrade) ++ord;
    }
    return ord;
}

void put_deck_card(Json& j, const engine::RunState& rs, uint16_t index) {
    if (index >= rs.master_deck_count || index >= engine::kMasterDeckCap) {
        j.kv("card", "");
        return;
    }
    const engine::CardInstance& c = rs.master_deck[index];
    j.kv("card", sts::registry::card_game_id(
                     static_cast<sts::registry::CardId>(c.card_id)));
    j.kv("up", static_cast<long long>(c.upgrade));
    j.kv("ord", deck_ordinal(rs, index));
}

// The open card-pick reward item, if any.
[[nodiscard]] const engine::RunRewardItem* open_card_item(
    const RunController& rc) noexcept {
    if (rc.rewards.open_card_item == engine::kNoOpenCardReward ||
        rc.rewards.open_card_item >= engine::kRewardItemCap) {
        return nullptr;
    }
    return &rc.rewards.items[rc.rewards.open_card_item];
}

void put_offer_card(Json& j, const RunController& rc, uint8_t slot) {
    const engine::RunRewardItem* item = open_card_item(rc);
    if (item == nullptr || slot >= item->card_count ||
        slot >= engine::kRewardCardCap) {
        j.kv("card", "");
        return;
    }
    j.kv("card", sts::registry::card_game_id(
                     static_cast<sts::registry::CardId>(item->card_ids[slot])));
    j.kv("up", static_cast<long long>(item->card_upgrades[slot]));
    // Offer lists are tiny and duplicates are rare but legal (two identical
    // colorless offers); count the same way the deck ordinal does.
    int ord = 0;
    for (uint8_t i = 0; i < slot; ++i) {
        if (item->card_ids[i] == item->card_ids[slot] &&
            item->card_upgrades[i] == item->card_upgrades[slot]) {
            ++ord;
        }
    }
    j.kv("ord", ord);
}

// claim step: the reward row's kind, payload identity, and the ordinal among
// rows of the SAME IDENTITY -- the dump's rewards[] is index-parallel with the
// sim's items[], but the identity join is what survives a cosmetic reorder.
//
// S2.43 (2026-08-27): this ordinal used to count rows of the same KIND, which
// contradicted the `id` emitted beside it and made the pair unfollowable. The
// live matcher (driver/script_policy_cmd.py `_match_claim` /
// `_reward_row_matches`) filters the dump's rewards[] by rtype AND payload id
// and then takes the ord-th SURVIVOR, exactly as `_match_take_card`,
// hand_ordinal and deck_ordinal do for cards; a kind ordinal indexes a
// different list, so any screen whose earlier same-kind rows carry a DIFFERENT
// id emitted an ord the follower could not resolve. Neow's three-potion
// blessing is the canonical shape: three POTION rows with three distinct ids,
// claim row 1 -> kind ordinal 1, but only ONE row bears that id, so the
// follower's `_nth_index(..., 1)` returned None and the line died on a
// PHANTOM divergence ("reward screen has no #1 POTION row") with the sim and
// the game in perfect agreement. Rows with no payload identity (GOLD,
// STOLEN_GOLD, CARDS, the keys) emit no `id`, the matcher then joins on rtype
// alone, and the kind IS the identity -- so their ordinal is unchanged.
void put_claim(Json& j, const RunController& rc, uint8_t index) {
    j.kv("k", "claim");
    if (index >= rc.rewards.count || index >= engine::kRewardItemCap) {
        j.kv("rtype", "NONE");
        return;
    }
    const engine::RunRewardItem& item = rc.rewards.items[index];
    const auto kind = static_cast<engine::RewardItemKind>(item.kind);
    j.kv("rtype", reward_kind_text(kind));
    // The two kinds whose row carries a payload identity -- and so the two for
    // which `id` is emitted and the ordinal must be an IDENTITY ordinal.
    const bool has_id = kind == engine::RewardItemKind::RELIC ||
                        kind == engine::RewardItemKind::POTION;
    if (kind == engine::RewardItemKind::RELIC) {
        j.kv("id", sts::registry::relic_game_id(
                       static_cast<sts::registry::RelicId>(item.id)));
    } else if (kind == engine::RewardItemKind::POTION) {
        j.kv("id", sts::registry::potion_game_id(
                       static_cast<sts::registry::PotionId>(item.id)));
    }
    int ord = 0;
    for (uint8_t i = 0; i < index; ++i) {
        const engine::RunRewardItem& o = rc.rewards.items[i];
        if (o.kind != item.kind) continue;
        if (has_id && o.id != item.id) continue;
        ++ord;
    }
    j.kv("ord", ord);
}

[[nodiscard]] bool potion_is_targeted(uint16_t potion_id) noexcept {
    const engine::PotionDef* def = engine::potion_def(
        static_cast<engine::PotionId>(potion_id));
    if (def == nullptr) return false;
    for (uint8_t i = 0; i < def->step_count; ++i) {
        if (def->steps[i].target == engine::StepTarget::CARD_TARGET) return true;
    }
    return false;
}

}  // namespace

std::string script_step_json(const RunController& rc, Action a, uint32_t index,
                             std::string& error) {
    const auto verb = engine::action_verb(a);
    const uint8_t arg0 = engine::action_arg0(a);
    const uint8_t arg1 = engine::action_arg1(a);
    const auto phase = static_cast<RunPhase>(rc.phase);

    Json j;
    j.open();
    j.kv("i", static_cast<long long>(index));
    j.kv("floor", static_cast<long long>(rc.run.floor));
    j.kv("act", static_cast<long long>(rc.run.act));
    j.kv("phase", phase_text(rc.phase));

    switch (verb) {
        case ActionVerb::END_TURN:
            j.kv("k", "end");
            j.close();
            return j.s;

        case ActionVerb::CONFIRM:
            j.kv("k", "confirm");
            j.close();
            return j.s;

        case ActionVerb::PLAY_CARD: {
            const engine::CombatState& cs = rc.combat;
            if (arg0 >= cs.hand_count) {
                error = "play: hand slot out of range";
                return {};
            }
            const engine::CardInstance& ci = cs.card_pool[cs.hand[arg0]];
            const engine::CardDef* def = engine::card_def(
                static_cast<engine::CardId>(ci.card_id));
            j.kv("k", "play");
            j.kv("card", sts::registry::card_game_id(
                             static_cast<sts::registry::CardId>(ci.card_id)));
            j.kv("up", static_cast<long long>(ci.upgrade));
            j.kv("ord", hand_ordinal(cs, arg0));
            j.kv("hand", static_cast<long long>(cs.hand_count));
            const bool targeted =
                def != nullptr && engine::card_needs_target(*def, ci.upgrade);
            j.kv("t", targeted ? static_cast<long long>(arg1) : -1);
            if (targeted && arg1 < cs.monster_count) {
                j.kv("tmon",
                     sts::registry::monster_game_id(
                         static_cast<sts::registry::MonsterId>(
                             cs.monsters[arg1].monster_id)));
            }
            j.close();
            return j.s;
        }

        case ActionVerb::USE_POTION: {
            if (arg0 >= engine::kPotionCap) {
                error = "potion: slot out of range";
                return {};
            }
            const uint16_t pid = rc.run.potions[arg0];
            j.kv("k", "potion");
            j.kv("slot", static_cast<long long>(arg0));
            j.kv("potion", sts::registry::potion_game_id(
                               static_cast<sts::registry::PotionId>(pid)));
            j.kv("t", potion_is_targeted(pid) ? static_cast<long long>(arg1)
                                              : -1);
            j.close();
            return j.s;
        }

        case ActionVerb::DISCARD_POTION: {
            if (arg0 >= engine::kPotionCap) {
                error = "potion discard: slot out of range";
                return {};
            }
            j.kv("k", "potion_discard");
            j.kv("slot", static_cast<long long>(arg0));
            j.kv("potion",
                 sts::registry::potion_game_id(
                     static_cast<sts::registry::PotionId>(rc.run.potions[arg0])));
            j.close();
            return j.s;
        }

        case ActionVerb::CHOOSE:
            break;  // decoded below, by phase
    }

    // --- CHOOSE, by phase ---------------------------------------------------
    switch (phase) {
        case RunPhase::NEOW: {
            const engine::NeowState& n = rc.neow;
            switch (static_cast<engine::NeowScreen>(n.screen)) {
                case engine::NeowScreen::BLESSING:
                    j.kv("k", "neow");
                    j.kv("index", static_cast<long long>(arg0));
                    break;
                case engine::NeowScreen::CARD_REWARD:
                    if (arg0 == engine::kChooseSkipCard) {
                        j.kv("k", "skip_card");
                    } else if (arg0 == engine::kChooseSing) {
                        j.kv("k", "sing");
                    } else {
                        j.kv("k", "take_card");
                        put_offer_card(j, rc, arg0);
                    }
                    break;
                case engine::NeowScreen::GRID:
                    if (arg0 == engine::kChooseProceed) {
                        j.kv("k", "proceed");
                        j.kv("ctx", "neow");
                    } else {
                        j.kv("k", "grid");
                        j.kv("ctx", "neow");
                        put_deck_card(j, rc.run, arg0);
                    }
                    break;
                case engine::NeowScreen::ITEM_REWARD:
                    if (arg0 == engine::kChooseProceed) {
                        j.kv("k", "proceed");
                        j.kv("ctx", "neow");
                    } else if (open_card_item(rc) != nullptr) {
                        if (arg0 == engine::kChooseSkipCard) {
                            j.kv("k", "skip_card");
                        } else if (arg0 == engine::kChooseSing) {
                            j.kv("k", "sing");
                        } else {
                            j.kv("k", "take_card");
                            put_offer_card(j, rc, arg0);
                        }
                    } else {
                        put_claim(j, rc, arg0);
                    }
                    break;
                case engine::NeowScreen::DONE:
                    j.kv("k", "proceed");
                    j.kv("ctx", "neow");
                    break;
            }
            break;
        }

        case RunPhase::MAP_CHOICE:
            if (arg0 == engine::kChooseBoss) {
                j.kv("k", "map_boss");
            } else {
                j.kv("k", "map");
                j.kv("x", static_cast<long long>(arg0));
                const int row = static_cast<int>(rc.run.floor) -
                                engine::act_floor_base(
                                    static_cast<int>(rc.run.act));
                if (row >= 0 && row < engine::kMapRows &&
                    arg0 < engine::kMapCols) {
                    // engine::room_symbol is CommunicationMod's own node
                    // symbol vocabulary (MapRoomNode.getRoomSymbol) -- the
                    // join key the live matcher uses beside the column.
                    const char sym[2] = {
                        engine::room_symbol(static_cast<engine::RoomType>(
                            rc.run.map[engine::run_state_map_index(arg0, row)]
                                .room_type)),
                        '\0'};
                    j.kv("sym", sym);
                } else {
                    j.kv("sym", "");
                }
            }
            break;

        case RunPhase::COMBAT: {
            // A CHOOSE inside combat is an open choice screen. Record the
            // source pile plus the chosen card's identity where the pile is
            // indexable; the live screen is GRID / HAND_SELECT / the
            // discovery card screen.
            const engine::CombatState& cs = rc.combat;
            engine::RunActionMask mask;
            engine::legal_actions(rc, mask);
            const engine::ActionMask& cm = mask.combat;
            j.kv("k", "choose_card");
            if (cm.choice_from_generated) {
                j.kv("src", "generated");
                if (arg0 == engine::kChooseSkipCard) {
                    j.kv("card", "");
                    j.kv("skip", 1LL);
                } else {
                    const engine::ActionQueueItem& front =
                        cs.action_queue[cs.action_head];
                    j.kv("card",
                         sts::registry::card_game_id(
                             static_cast<sts::registry::CardId>(
                                 engine::discovery_choice_card(front, arg0))));
                    j.kv("index", static_cast<long long>(arg0));
                }
                break;
            }
            const char* src = "hand";
            int count = cs.hand_count;
            const engine::CardPoolIndex* pile = cs.hand;
            if (cm.choice_from_discard) {
                src = "discard";
                count = cs.discard_count;
                pile = cs.discard;
            } else if (cm.choice_from_exhaust) {
                src = "exhaust";
                count = cs.exhaust_count;
                pile = cs.exhaust;
            } else if (cm.choice_from_draw) {
                src = "draw";
                count = cs.draw_count;
                pile = cs.draw;
            }
            j.kv("src", src);
            j.kv("index", static_cast<long long>(arg0));
            if (arg0 < count) {
                const engine::CardInstance& ci = cs.card_pool[pile[arg0]];
                j.kv("card",
                     sts::registry::card_game_id(
                         static_cast<sts::registry::CardId>(ci.card_id)));
                j.kv("up", static_cast<long long>(ci.upgrade));
                int ord = 0;
                for (int i = 0; i < arg0 && i < count; ++i) {
                    const engine::CardInstance& o = cs.card_pool[pile[i]];
                    if (o.card_id == ci.card_id && o.upgrade == ci.upgrade) {
                        ++ord;
                    }
                }
                j.kv("ord", ord);
            }
            break;
        }

        case RunPhase::COMBAT_REWARD:
            if (rc.pending_bottle != 0) {
                j.kv("k", "grid");
                j.kv("ctx", "bottle");
                put_deck_card(j, rc.run, arg0);
            } else if (open_card_item(rc) != nullptr) {
                if (arg0 == engine::kChooseSkipCard) {
                    j.kv("k", "skip_card");
                } else if (arg0 == engine::kChooseSing) {
                    j.kv("k", "sing");
                } else {
                    j.kv("k", "take_card");
                    put_offer_card(j, rc, arg0);
                }
            } else if (arg0 == engine::kChooseProceed) {
                j.kv("k", "proceed");
                j.kv("ctx", "combat_reward");
            } else {
                put_claim(j, rc, arg0);
            }
            break;

        case RunPhase::REST_SITE: {
            const auto screen = static_cast<engine::RestScreen>(rc.rest.screen);
            if (screen == engine::RestScreen::MENU) {
                const engine::RestMenu menu = engine::build_rest_menu(rc.run);
                j.kv("k", "rest");
                const char* opt = "?";
                if (arg0 < menu.count) {
                    switch (static_cast<engine::RestOptionKind>(
                        menu.entries[arg0].kind)) {
                        case engine::RestOptionKind::REST: opt = "rest"; break;
                        case engine::RestOptionKind::SMITH: opt = "smith"; break;
                        case engine::RestOptionKind::LIFT: opt = "lift"; break;
                        case engine::RestOptionKind::TOKE: opt = "toke"; break;
                        case engine::RestOptionKind::DIG: opt = "dig"; break;
                        case engine::RestOptionKind::RECALL: opt = "recall"; break;
                    }
                }
                j.kv("opt", opt);
            } else if (screen == engine::RestScreen::DREAM_CATCHER) {
                if (arg0 == engine::kChooseSkipCard) {
                    j.kv("k", "skip_card");
                } else if (arg0 == engine::kChooseSing) {
                    j.kv("k", "sing");
                } else {
                    j.kv("k", "take_card");
                    put_offer_card(j, rc, arg0);
                }
            } else {
                const char* ctx =
                    screen == engine::RestScreen::SMITH ? "smith" : "toke";
                if (arg0 == engine::kChooseCancelGrid) {
                    j.kv("k", "grid_cancel");
                    j.kv("ctx", ctx);
                } else {
                    j.kv("k", "grid");
                    j.kv("ctx", ctx);
                    put_deck_card(j, rc.run, arg0);
                }
            }
            break;
        }

        case RunPhase::TREASURE_ROOM:
            if (arg0 == engine::kChooseProceed) {
                j.kv("k", "proceed");
                j.kv("ctx", "treasure_skip");
            } else {
                j.kv("k", "open_chest");
            }
            break;

        case RunPhase::EVENT_DIALOG: {
            if (rc.event.grid_kind !=
                static_cast<uint8_t>(engine::EventGridKind::NONE)) {
                j.kv("k", "grid");
                j.kv("ctx", "event");
                put_deck_card(j, rc.run, arg0);
                break;
            }
            j.kv("k", "event");
            // Qualified: the registry's event_game_id is also visible via ADL
            // (the seed_scan.cpp jsonl writer hit the same ambiguity).
            j.kv("event", sts::planner::event_game_id(
                              static_cast<registry::EventId>(
                                  rc.event.event_id)));
            j.kv("opt", static_cast<long long>(arg0));
            // The live `choose N` indexes CommunicationMod's choice_list,
            // which lists ENABLED buttons only; the sim ordinal is the
            // full-list position. Publish both (command_map.hpp documents the
            // two index spaces; this is the inverse translation).
            const engine::EventDialogImpl* impl =
                engine::event_dialog_impl(rc.event.event_id);
            long long enabled_index = -1;
            if (impl != nullptr) {
                engine::EventDialogMenu menu{};
                impl->build_menu(rc, rc.event, menu);
                if (arg0 < menu.count && menu.enabled[arg0]) {
                    enabled_index = 0;
                    for (uint8_t i = 0; i < arg0; ++i) {
                        if (menu.enabled[i]) ++enabled_index;
                    }
                }
            }
            j.kv("index", enabled_index);
            break;
        }

        case RunPhase::SHOP: {
            const engine::ShopState& shop = rc.shop;
            if (static_cast<engine::ShopScreenKind>(shop.screen) ==
                engine::ShopScreenKind::PURGE_GRID) {
                if (arg0 == engine::kChooseCancelGrid) {
                    j.kv("k", "grid_cancel");
                    j.kv("ctx", "shop_purge");
                } else {
                    j.kv("k", "grid");
                    j.kv("ctx", "shop_purge");
                    put_deck_card(j, rc.run, arg0);
                }
            } else if (arg0 == engine::kChooseProceed) {
                j.kv("k", "proceed");
                j.kv("ctx", "shop");
            } else {
                // This policy never buys; the generic decode keeps the
                // emitter total over legal trajectories from other policies.
                j.kv("k", "shop");
                j.kv("index", static_cast<long long>(arg0));
            }
            break;
        }

        case RunPhase::BOSS_TREASURE: {
            const engine::BossChestState& chest = rc.run.boss_chest;
            switch (static_cast<engine::BossChestScreen>(chest.screen)) {
                case engine::BossChestScreen::CLOSED:
                    if (arg0 == engine::kChooseProceed) {
                        j.kv("k", "proceed");
                        j.kv("ctx", "boss_chest");
                    } else {
                        j.kv("k", "boss_open");
                    }
                    break;
                case engine::BossChestScreen::RELIC_SELECT:
                    if (arg0 == engine::kChooseCancelGrid) {
                        j.kv("k", "boss_skip");
                    } else {
                        j.kv("k", "boss_pick");
                        if (arg0 < engine::kBossChestOfferCount) {
                            j.kv("relic",
                                 sts::registry::relic_game_id(
                                     static_cast<sts::registry::RelicId>(
                                         chest.relics[arg0])));
                        }
                    }
                    break;
                case engine::BossChestScreen::EQUIP_GRID:
                    if (arg0 == engine::kChooseProceed) {
                        j.kv("k", "proceed");
                        j.kv("ctx", "boss_equip");
                    } else if (arg0 == engine::kChooseCancelGrid) {
                        j.kv("k", "grid_cancel");
                        j.kv("ctx", "boss_equip");
                    } else {
                        j.kv("k", "grid");
                        j.kv("ctx", "boss_equip");
                        put_deck_card(j, rc.run, arg0);
                    }
                    break;
                case engine::BossChestScreen::EQUIP_ITEM_REWARD:
                    if (arg0 == engine::kChooseProceed) {
                        j.kv("k", "proceed");
                        j.kv("ctx", "boss_equip");
                    } else if (open_card_item(rc) != nullptr) {
                        if (arg0 == engine::kChooseSkipCard) {
                            j.kv("k", "skip_card");
                        } else if (arg0 == engine::kChooseSing) {
                            j.kv("k", "sing");
                        } else {
                            j.kv("k", "take_card");
                            put_offer_card(j, rc, arg0);
                        }
                    } else {
                        put_claim(j, rc, arg0);
                    }
                    break;
                case engine::BossChestScreen::DONE:
                    j.kv("k", "proceed");
                    j.kv("ctx", "boss_chest");
                    break;
            }
            break;
        }

        case RunPhase::NONE:
        case RunPhase::ROOM_UNIMPLEMENTED:
        case RunPhase::RUN_OVER:
            error = std::string("CHOOSE in non-interactive phase ") +
                    phase_text(rc.phase);
            return {};
    }

    j.close();
    return j.s;
}

ScriptEmit emit_script(int64_t seed_value, const std::string& seed_text,
                       uint8_t ascension, const char* policy_name,
                       uint64_t policy_seed,
                       const std::vector<Action>& trajectory,
                       const char* end_reason, uint64_t final_hash) {
    ScriptEmit out;

    // The replay itself. Heap-allocate the controller (it is large and this
    // is a tool path, not the batch hot loop).
    auto rc = std::make_unique<RunController>(
        engine::run_begin(seed_value, ascension));

    std::vector<std::string> steps;
    steps.reserve(trajectory.size());
    uint8_t max_act = 0;
    uint32_t max_floor = 0;
    for (uint32_t i = 0; i < trajectory.size(); ++i) {
        std::string err;
        std::string line = script_step_json(*rc, trajectory[i], i, err);
        if (line.empty()) {
            out.error = "step " + std::to_string(i) + ": " + err;
            return out;
        }
        steps.push_back(std::move(line));
        engine::Action a = trajectory[i];
        engine::StepResult res{};
        engine::advance({rc.get(), 1}, {&a, 1}, {&res, 1});
        if (rc->run.act > max_act) max_act = rc->run.act;
        if (rc->run.floor > max_floor) {
            max_floor = static_cast<uint32_t>(rc->run.floor);
        }
    }

    // Replay fidelity: the re-driven trajectory must land on the scanned
    // row's terminal controller hash. A mismatch means the trajectory and the
    // engine disagree -- a finding, never a formatting problem.
    if (final_hash != 0 && fuzz::hash_controller(*rc) != final_hash) {
        out.error = "replay hash mismatch: script replay did not reproduce "
                    "the scanned terminal state";
        return out;
    }

    char hash_buf[32];
    std::snprintf(hash_buf, sizeof(hash_buf), "%016llx",
                  static_cast<unsigned long long>(final_hash));

    Json h;
    h.open();
    h.kv("format", kScriptFormat);
    h.kv("seed", seed_text);
    h.kv("seed_int", static_cast<long long>(seed_value));
    h.kv("ascension", static_cast<long long>(ascension));
    h.kv("policy", policy_name);
    h.kv("policy_seed", static_cast<long long>(policy_seed));
    h.kv("engine_schema", static_cast<long long>(engine::SCHEMA_VERSION));
    h.kv("steps", static_cast<long long>(steps.size()));
    h.kv("final_hash", hash_buf);
    h.kv("end_reason", end_reason);
    h.kvb("victory", engine::run_is_victory(*rc));
    h.kv("max_act", static_cast<long long>(max_act));
    h.kv("max_floor", static_cast<long long>(max_floor));
    h.close();

    out.lines.reserve(steps.size() + 1);
    out.lines.push_back(std::move(h.s));
    for (std::string& s : steps) out.lines.push_back(std::move(s));
    out.ok = true;
    return out;
}

}  // namespace sts::planner
