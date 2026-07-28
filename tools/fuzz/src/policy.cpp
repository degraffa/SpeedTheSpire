#include "sts/fuzz/policy.hpp"

#include <cassert>
#include <cstring>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/map_gen.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/potions.hpp"

namespace sts::fuzz {

using engine::Action;
using engine::ActionVerb;
using engine::make_action;
using engine::RunController;
using engine::RunActionMask;
using engine::RunPhase;

// --- names -------------------------------------------------------------------

const char* policy_name(PolicyKind k) noexcept {
    switch (k) {
        case PolicyKind::RANDOM: return "random";
        case PolicyKind::GREEDY_DAMAGE: return "greedy_damage";
        case PolicyKind::GREEDY_BLOCK: return "greedy_block";
        case PolicyKind::HOARD_GOLD: return "hoard_gold";
        case PolicyKind::ALWAYS_EVENT: return "always_event";
        case PolicyKind::COUNT: break;
    }
    return "?";
}

bool policy_from_name(const std::string& name, PolicyKind& out) noexcept {
    for (uint8_t i = 0; i < static_cast<uint8_t>(PolicyKind::COUNT); ++i) {
        const auto k = static_cast<PolicyKind>(i);
        if (name == policy_name(k)) {
            out = k;
            return true;
        }
    }
    return false;
}

const char* move_cat_name(MoveCat c) noexcept {
    switch (c) {
        case MoveCat::PLAY_CARD: return "play_card";
        case MoveCat::PLAY_CARD_TARGET: return "play_card_target";
        case MoveCat::END_TURN: return "end_turn";
        case MoveCat::COMBAT_CHOOSE: return "combat_choose";
        case MoveCat::USE_POTION: return "use_potion";
        case MoveCat::USE_POTION_TARGET: return "use_potion_target";
        case MoveCat::NEOW_PROCEED: return "neow_proceed";
        case MoveCat::MAP_NODE: return "map_node";
        case MoveCat::MAP_BOSS: return "map_boss";
        case MoveCat::REWARD_CLAIM: return "reward_claim";
        case MoveCat::REWARD_TAKE_CARD: return "reward_take_card";
        case MoveCat::REWARD_SKIP_CARD: return "reward_skip_card";
        case MoveCat::REWARD_SING: return "reward_sing";
        case MoveCat::REWARD_PROCEED: return "reward_proceed";
        case MoveCat::REST: return "rest";
        case MoveCat::SMITH: return "smith";
        case MoveCat::LIFT: return "lift";
        case MoveCat::TOKE: return "toke";
        case MoveCat::DIG: return "dig";
        case MoveCat::SMITH_CARD: return "smith_card";
        case MoveCat::TOKE_CARD: return "toke_card";
        case MoveCat::TREASURE_OPEN: return "treasure_open";
        case MoveCat::TREASURE_SKIP: return "treasure_skip";
        case MoveCat::EVENT_OPTION: return "event_option";
        case MoveCat::EVENT_GRID: return "event_grid";
        case MoveCat::CHOICE_CONFIRM: return "choice_confirm";
        case MoveCat::SHOP: return "shop";
        case MoveCat::COUNT: break;
    }
    return "?";
}

// --- enumeration -------------------------------------------------------------

namespace {

[[nodiscard]] bool potion_is_targeted(const engine::PotionDef& def) noexcept {
    for (uint8_t i = 0; i < def.step_count; ++i) {
        if (def.steps[i].target == engine::StepTarget::CARD_TARGET) return true;
    }
    return false;
}

struct Sink {
    Move* out;
    size_t cap;
    size_t n = 0;

    void add(Action a, MoveCat c) noexcept {
        // A cap overflow would silently truncate the legal set and make the
        // policy's choice depend on enumeration order -- assert rather than
        // quietly narrow the search space.
        assert(n < cap && "kMoveCap too small for the engine's legal set");
        if (n < cap) {
            out[n].action = a;
            out[n].cat = c;
            ++n;
        }
    }
};

}  // namespace

size_t enumerate_moves(const RunController& rc, const RunActionMask& mask, Move* out,
                       size_t cap) noexcept {
    Sink s{out, cap};
    const auto phase = static_cast<RunPhase>(rc.phase);

    // USE_POTION is a run-layer verb and is legal in several phases, so it is
    // enumerated first and unconditionally from the run mask.
    for (int p = 0; p < engine::kPotionCap; ++p) {
        const engine::PotionDef* potion = engine::potion_def(
            static_cast<engine::PotionId>(rc.run.potions[p]));
        const bool targeted = potion != nullptr && potion_is_targeted(*potion);
        if (mask.can_use_potion[p] && !targeted) {
            s.add(make_action(ActionVerb::USE_POTION, static_cast<uint8_t>(p)),
                  MoveCat::USE_POTION);
        }
        for (int m = 0; m < engine::kMonsterCap; ++m) {
            if (mask.can_use_potion_target[p][m]) {
                s.add(make_action(ActionVerb::USE_POTION, static_cast<uint8_t>(p),
                                  static_cast<uint8_t>(m)),
                      MoveCat::USE_POTION_TARGET);
            }
        }
    }

    switch (phase) {
        case RunPhase::NEOW:
            // Every Neow move shares the NEOW_PROCEED coverage bucket: the
            // blessing's four option buttons, the card / master-deck-grid /
            // reward sub-screens a payout opens, and the final press that opens
            // the map. One bucket for what is really five screens is
            // deliberate. MoveCat is a SHARED, append-only namespace
            // (docs/stage-b-tasks.md's shared-namespace table); splitting Neow
            // finer means the orchestrator ALLOCATING new values, not a task
            // taking them locally. What is lost is coverage resolution inside
            // floor 0; legality and enumeration are exact either way.
            for (int i = 0; i < engine::kNeowOptionCount; ++i) {
                if (mask.can_choose_neow_option[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::NEOW_PROCEED);
                }
            }
            for (int i = 0; i < engine::kMasterDeckCap; ++i) {
                if (mask.can_choose_master_deck[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::NEOW_PROCEED);
                }
            }
            for (int i = 0; i < engine::kRewardCardCap; ++i) {
                if (mask.can_take_card[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::NEOW_PROCEED);
                }
            }
            for (int i = 0; i < engine::kRewardItemCap; ++i) {
                if (mask.can_claim_reward[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::NEOW_PROCEED);
                }
            }
            if (mask.can_skip_card) {
                s.add(make_action(ActionVerb::CHOOSE, engine::kChooseSkipCard),
                      MoveCat::NEOW_PROCEED);
            }
            if (mask.can_sing) {
                s.add(make_action(ActionVerb::CHOOSE, engine::kChooseSing),
                      MoveCat::NEOW_PROCEED);
            }
            if (mask.can_proceed) {
                s.add(make_action(ActionVerb::CHOOSE, engine::kChooseProceed),
                      MoveCat::NEOW_PROCEED);
            }
            break;

        case RunPhase::MAP_CHOICE:
            for (int x = 0; x < engine::kMapCols; ++x) {
                if (mask.can_choose_node[x]) {
                    s.add(make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(x)),
                          MoveCat::MAP_NODE);
                }
            }
            if (mask.can_choose_boss) {
                s.add(make_action(ActionVerb::CHOOSE, engine::kChooseBoss),
                      MoveCat::MAP_BOSS);
            }
            break;

        case RunPhase::COMBAT: {
            const engine::ActionMask& cm = mask.combat;
            if (cm.choice_pending) {
                if (cm.choice_from_generated) {
                    for (uint8_t i = 0; i < engine::kDiscoveryChoiceCount; ++i) {
                        if (cm.can_choose[i]) {
                            s.add(make_action(ActionVerb::CHOOSE, i),
                                  MoveCat::COMBAT_CHOOSE);
                        }
                    }
                    // A TYPED discovery screen also carries the Skip button
                    // (advance.hpp can_skip_choice) -- enumerate it so the
                    // soak exercises the skip's wasted-regen burn too.
                    if (cm.can_skip_choice) {
                        s.add(make_action(ActionVerb::CHOOSE,
                                          engine::kChooseSkipCard),
                              MoveCat::COMBAT_CHOOSE);
                    }
                    break;
                }
                const engine::ActionQueueItem& front =
                    rc.combat.action_queue[rc.combat.action_head];
                if (cm.choice_optional) {
                    // A zero-to-N screen: every legal toggle (picking a card up
                    // OR putting one back) plus the confirm button. The confirm
                    // is enumerated even with NOTHING selected -- that empty
                    // confirm is the reachability this category exists to
                    // measure, and offering it from the first step is what makes
                    // the random policy hit it constantly rather than only after
                    // it happens to deselect everything again.
                    for (int i = 0; i < rc.combat.hand_count; ++i) {
                        if (engine::optional_choice_slot_legal(
                                rc.combat, front, static_cast<uint8_t>(i))) {
                            s.add(make_action(ActionVerb::CHOOSE,
                                              static_cast<uint8_t>(i)),
                                  MoveCat::COMBAT_CHOOSE);
                        }
                    }
                    if (cm.can_confirm_choice) {
                        s.add(make_action(ActionVerb::CONFIRM),
                              MoveCat::CHOICE_CONFIRM);
                    }
                    break;
                }
                const engine::ChoiceKind kind =
                    engine::choose_kind_from_flags(front.flags);
                const uint8_t type_filter =
                    engine::choose_type_filter_from_flags(front.flags);
                int source_count = rc.combat.hand_count;
                if (cm.choice_from_discard) source_count = rc.combat.discard_count;
                if (cm.choice_from_exhaust) source_count = rc.combat.exhaust_count;
                // A draw-source choice (Secret Technique / Secret Weapon)
                // enumerates DRAW-pile slots, filtered to one CardType --
                // choice_slot_eligible applies that filter, so most of the pile
                // is normally rejected below.
                if (cm.choice_from_draw) source_count = rc.combat.draw_count;
                for (int i = 0; i < source_count; ++i) {
                    if (engine::choice_slot_eligible(
                            rc.combat, static_cast<uint8_t>(i), kind,
                            type_filter)) {
                        s.add(make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(i)),
                              MoveCat::COMBAT_CHOOSE);
                    }
                }
                break;
            }
            for (int i = 0; i < engine::kHandCap; ++i) {
                // A targeted card is offered ONLY through can_play_target: the
                // untargeted spelling of an enemy-target card is not a legal
                // move, and can_play[i] deliberately keeps its affordability
                // meaning (advance.hpp), so the two views must be combined here.
                bool targeted = false;
                for (int t = 0; t < engine::kMonsterCap; ++t) {
                    if (cm.can_play_target[i][t]) {
                        targeted = true;
                        s.add(make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(i),
                                          static_cast<uint8_t>(t)),
                              MoveCat::PLAY_CARD_TARGET);
                    }
                }
                if (!targeted && cm.can_play[i]) {
                    const engine::CardInstance& ci =
                        rc.combat.card_pool[rc.combat.hand[i]];
                    const engine::CardDef* def =
                        engine::card_def(static_cast<engine::CardId>(ci.card_id));
                    // A needs_target card with no live target is not playable;
                    // can_play[i] (affordability) would still be set.
                    if (def == nullptr || !def->needs_target) {
                        s.add(make_action(ActionVerb::PLAY_CARD, static_cast<uint8_t>(i)),
                              MoveCat::PLAY_CARD);
                    }
                }
            }
            if (cm.can_end_turn) {
                s.add(make_action(ActionVerb::END_TURN), MoveCat::END_TURN);
            }
            break;
        }

        case RunPhase::COMBAT_REWARD:
            if (rc.rewards.open_card_item != engine::kNoOpenCardReward) {
                for (int j = 0; j < engine::kRewardCardCap; ++j) {
                    if (mask.can_take_card[j]) {
                        s.add(make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(j)),
                              MoveCat::REWARD_TAKE_CARD);
                    }
                }
                if (mask.can_skip_card) {
                    s.add(make_action(ActionVerb::CHOOSE, engine::kChooseSkipCard),
                          MoveCat::REWARD_SKIP_CARD);
                }
                if (mask.can_sing) {
                    s.add(make_action(ActionVerb::CHOOSE, engine::kChooseSing),
                          MoveCat::REWARD_SING);
                }
            } else {
                for (int i = 0; i < engine::kRewardItemCap; ++i) {
                    if (mask.can_claim_reward[i]) {
                        s.add(make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(i)),
                              MoveCat::REWARD_CLAIM);
                    }
                }
                // The pending-bottle overlay: claiming a Bottled trio relic
                // replaces the claim mask with the mandatory 1-pick
                // master-deck grid (run_advance.hpp). Mask-driven like every
                // other arm, and it shares REWARD_CLAIM rather than spending
                // a new MoveCat -- the same one-bucket-per-phase call the
                // NEOW_PROCEED and SHOP comments make (MoveCat is a shared,
                // append-only namespace; docs/stage-b-tasks.md).
                for (int i = 0; i < engine::kMasterDeckCap; ++i) {
                    if (mask.can_choose_master_deck[i]) {
                        s.add(make_action(ActionVerb::CHOOSE,
                                          static_cast<uint8_t>(i)),
                              MoveCat::REWARD_CLAIM);
                    }
                }
                if (mask.can_proceed) {
                    s.add(make_action(ActionVerb::CHOOSE, engine::kChooseProceed),
                          MoveCat::REWARD_PROCEED);
                }
            }
            break;

        case RunPhase::REST_SITE: {
            const auto screen = static_cast<engine::RestScreen>(rc.rest.screen);
            if (screen == engine::RestScreen::MENU) {
                const engine::RestMenu menu = engine::build_rest_menu(rc.run);
                for (uint8_t i = 0; i < menu.count; ++i) {
                    if (!mask.can_choose_rest[i]) {
                        continue;
                    }
                    MoveCat cat = MoveCat::REST;
                    switch (static_cast<engine::RestOptionKind>(
                        menu.entries[i].kind)) {
                        case engine::RestOptionKind::REST:
                            cat = MoveCat::REST;
                            break;
                        case engine::RestOptionKind::SMITH:
                            cat = MoveCat::SMITH;
                            break;
                        case engine::RestOptionKind::LIFT:
                            cat = MoveCat::LIFT;
                            break;
                        case engine::RestOptionKind::TOKE:
                            cat = MoveCat::TOKE;
                            break;
                        case engine::RestOptionKind::DIG:
                            cat = MoveCat::DIG;
                            break;
                    }
                    s.add(make_action(ActionVerb::CHOOSE, i), cat);
                }
            } else if (screen == engine::RestScreen::DREAM_CATCHER) {
                for (int j = 0; j < engine::kRewardCardCap; ++j) {
                    if (mask.can_take_card[j]) {
                        s.add(make_action(ActionVerb::CHOOSE,
                                          static_cast<uint8_t>(j)),
                              MoveCat::REWARD_TAKE_CARD);
                    }
                }
                if (mask.can_skip_card) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      engine::kChooseSkipCard),
                          MoveCat::REWARD_SKIP_CARD);
                }
                if (mask.can_sing) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      engine::kChooseSing),
                          MoveCat::REWARD_SING);
                }
            } else {
                const MoveCat cat = screen == engine::RestScreen::SMITH
                                        ? MoveCat::SMITH_CARD
                                        : MoveCat::TOKE_CARD;
                for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
                    if (mask.can_choose_master_deck[i]) {
                        s.add(make_action(ActionVerb::CHOOSE,
                                          static_cast<uint8_t>(i)),
                              cat);
                    }
                }
            }
            break;
        }

        case RunPhase::TREASURE_ROOM:
            if (mask.can_open_chest) {
                s.add(make_action(ActionVerb::CHOOSE,
                                  engine::kChooseOpenChest),
                      MoveCat::TREASURE_OPEN);
            }
            if (mask.can_proceed) {
                s.add(make_action(ActionVerb::CHOOSE,
                                  engine::kChooseProceed),
                      MoveCat::TREASURE_SKIP);
            }
            break;

        case RunPhase::EVENT_DIALOG:
            for (int i = 0; i < engine::kMasterDeckCap; ++i) {
                if (mask.can_choose_master_deck[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::EVENT_GRID);
                }
            }
            for (int i = 0; i < engine::kEventOptionCap; ++i) {
                if (mask.can_choose_event_option[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::EVENT_OPTION);
                }
            }
            break;

        case RunPhase::SHOP:
            // The purge grid is modal, so exactly one of these two loops ever
            // produces moves.
            for (int i = 0; i < engine::kMasterDeckCap; ++i) {
                if (mask.can_choose_master_deck[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::SHOP);
                }
            }
            for (int i = 0; i < engine::kShopItemCount; ++i) {
                if (mask.can_buy_shop_item[i]) {
                    s.add(make_action(ActionVerb::CHOOSE,
                                      static_cast<uint8_t>(i)),
                          MoveCat::SHOP);
                }
            }
            if (mask.can_purge) {
                s.add(make_action(ActionVerb::CHOOSE, engine::kChooseShopPurge),
                      MoveCat::SHOP);
            }
            if (mask.can_proceed) {
                s.add(make_action(ActionVerb::CHOOSE, engine::kChooseProceed),
                      MoveCat::SHOP);
            }
            break;

        case RunPhase::NONE:
        case RunPhase::ROOM_UNIMPLEMENTED:
        case RunPhase::RUN_OVER:
            break;
    }
    return s.n;
}

// --- heuristic scoring -------------------------------------------------------

namespace {

// CardEffectStep::op is the GENERATED table's own Opcode mirror
// (sts::registry::Opcode), which cards.hpp static_asserts byte-equal to the
// engine's authoritative enum. Switch on the mirror -- the pin is what makes
// that safe, and reaching for the engine spelling here only adds a cast.
using sts::registry::Opcode;

// Crude but honest card scoring: sum the effect program's damage-ish and
// block-ish `amount`s at the instance's upgrade level. This is a COVERAGE
// generator, not an agent -- it only has to bias play toward long fights
// (greedy_block) and short ones (greedy_damage) so both shapes get exercised.
// Dynamic-base opcodes (DAMAGE_BLOCK / DAMAGE_STR_MULT / DAMAGE_PER_STRIKE)
// contribute their static `amount`; that under- or over-counts, which costs
// nothing here.
struct CardScore {
    int damage = 0;
    int block = 0;
};

[[nodiscard]] CardScore score_card(const engine::CombatState& cs, uint8_t hand_slot) noexcept {
    CardScore sc;
    const engine::CardInstance& ci = cs.card_pool[cs.hand[hand_slot]];
    const engine::CardDef* def = engine::card_def(static_cast<engine::CardId>(ci.card_id));
    if (def == nullptr) {
        return sc;
    }
    const engine::CardEffectView v = engine::card_effect_steps(*def, ci.upgrade);
    for (uint8_t i = 0; i < v.count; ++i) {
        const engine::CardEffectStep& st = v.steps[i];
        switch (st.op) {
            case Opcode::DAMAGE:
            case Opcode::DAMAGE_STR_MULT:
            case Opcode::DAMAGE_PER_STRIKE:
            case Opcode::DAMAGE_UPGRADE_SCALE:
            case Opcode::DAMAGE_RAMPAGE:
                sc.damage += st.amount;
                break;
            case Opcode::DAMAGE_BLOCK:
                sc.damage += cs.player_block;
                break;
            case Opcode::BLOCK:
                sc.block += st.amount;
                break;
            default:
                break;
        }
    }
    return sc;
}

// Destination room type for a MAP_NODE move. At MAP_CHOICE the controller is
// still on floor F, and the destination row is F (run_advance.cpp's
// next_room_transition increments the floor BEFORE reading the row), so the
// node is map[run_state_map_index(col, floor)].
[[nodiscard]] engine::RoomType map_move_room(const RunController& rc, Move m) noexcept {
    if (m.cat == MoveCat::MAP_BOSS) {
        return engine::RoomType::Boss;
    }
    const int col = engine::action_arg0(m.action);
    const int row = static_cast<int>(rc.run.floor);
    if (row < 0 || row >= engine::kMapRows || col < 0 || col >= engine::kMapCols) {
        return engine::RoomType::None;
    }
    return static_cast<engine::RoomType>(
        rc.run.map[engine::run_state_map_index(col, row)].room_type);
}

[[nodiscard]] bool is_combat_room(engine::RoomType r) noexcept {
    return r == engine::RoomType::Monster || r == engine::RoomType::Elite ||
           r == engine::RoomType::Boss;
}

// The lowest-HP live monster slot, or -1. Ties go to the lowest slot index so
// the choice stays a pure function of the state.
[[nodiscard]] int weakest_monster(const engine::CombatState& cs) noexcept {
    int best = -1;
    int best_hp = 0;
    for (int i = 0; i < cs.monster_count; ++i) {
        const engine::MonsterState& m = cs.monsters[i];
        if (m.hp <= 0) continue;
        if (best < 0 || m.hp < best_hp) {
            best = i;
            best_hp = m.hp;
        }
    }
    return best;
}

// Per-move preference, higher is better. Every policy shares this shape so the
// tie-break (uniform among the maxima, one PolicyRng draw) is identical
// everywhere and each policy is exactly a scoring function.
[[nodiscard]] int move_score(PolicyKind kind, const RunController& rc, Move m) noexcept {
    const engine::CombatState& cs = rc.combat;

    switch (m.cat) {
        case MoveCat::PLAY_CARD:
        case MoveCat::PLAY_CARD_TARGET: {
            const CardScore sc = score_card(cs, engine::action_arg0(m.action));
            int base = 0;
            if (kind == PolicyKind::GREEDY_DAMAGE) {
                base = 100 + sc.damage * 4 + sc.block;
            } else if (kind == PolicyKind::GREEDY_BLOCK) {
                base = 100 + sc.block * 4 + sc.damage;
            } else {  // HOARD_GOLD / ALWAYS_EVENT fight greedily-ish
                base = 100 + sc.damage * 2 + sc.block * 2;
            }
            if (m.cat == MoveCat::PLAY_CARD_TARGET) {
                // Focus fire: finish the weakest monster first (fewer enemy
                // turns => deeper runs). +2 keeps it below any card-quality
                // difference so it only breaks ties between targets.
                const int weak = weakest_monster(cs);
                if (weak >= 0 && engine::action_arg1(m.action) == weak) {
                    base += 2;
                }
            }
            return base;
        }
        case MoveCat::END_TURN:
            // Strictly below any playable card so the greedy policies empty
            // their hand before passing.
            return 1;
        case MoveCat::COMBAT_CHOOSE:
            return 50;
        case MoveCat::CHOICE_CONFIRM:
            // Deliberately EQUAL to a toggle. Above it, a heuristic policy would
            // confirm the moment the screen opened and the toggles would never
            // be exercised; below it, it would pick the whole hand up before
            // ever pressing the button and the EMPTY confirm would be
            // unreachable outside the random policy. Equal scores put both on
            // the tie-break, so every prefix length gets hit.
            return 50;
        case MoveCat::USE_POTION:
        case MoveCat::USE_POTION_TARGET:
            // Potions are rare and their code paths are worth hitting, but a
            // policy that always drinks would never test the full-slots gate.
            // Slightly below a card play: taken when nothing better exists.
            return 60;
        case MoveCat::NEOW_PROCEED:
            return 10;
        case MoveCat::MAP_NODE:
        case MoveCat::MAP_BOSS: {
            const engine::RoomType r = map_move_room(rc, m);
            if (kind == PolicyKind::ALWAYS_EVENT) {
                // Steer AWAY from combat: the ?-room / shop / rest / treasure
                // nodes exercise non-combat routing. Treasure and Rest
                // continue; a ? now RESOLVES (B4.10) -- to a monster combat,
                // a chest, a parked shop, or an event selection that parks
                // until its body lands; a static shop parks directly.
                // Treasure scores above the generic non-combat weight so
                // this policy reliably reaches the chest phase.
                if (r == engine::RoomType::Treasure) return 110;
                return is_combat_room(r) ? 10 : 100;
            }
            // Every other heuristic wants depth, and depth lives in combat:
            // shop nodes and resolved-event selections still park.
            if (r == engine::RoomType::Elite) return 110;  // longest fights
            if (r == engine::RoomType::Monster) return 100;
            if (r == engine::RoomType::Boss) return 90;
            return 10;
        }
        case MoveCat::REWARD_CLAIM: {
            const uint8_t idx = engine::action_arg0(m.action);
            const auto kind_of = static_cast<engine::RewardItemKind>(
                idx < engine::kRewardItemCap ? rc.rewards.items[idx].kind : 0);
            if (kind == PolicyKind::HOARD_GOLD) {
                if (kind_of == engine::RewardItemKind::GOLD) return 200;
                // A CARDS item must score BELOW proceed for this policy. It
                // does not want cards, so it would claim (opening the pick
                // screen), Skip (which by design leaves the item claimable --
                // run_advance.hpp), and loop there forever. Found by this
                // tool's own first smoke run: 200k of 208k actions went into
                // that 2-cycle.
                if (kind_of == engine::RewardItemKind::CARDS) return 1;
                return 60;
            }
            return 100;
        }
        case MoveCat::REWARD_TAKE_CARD:
            return kind == PolicyKind::HOARD_GOLD ? 10 : 100;
        case MoveCat::REWARD_SKIP_CARD:
            return kind == PolicyKind::HOARD_GOLD ? 200 : 20;
        case MoveCat::REWARD_SING:
            return 30;
        case MoveCat::REWARD_PROCEED:
            // Below any claim, so rewards are actually consumed before leaving.
            return 5;
        case MoveCat::REST:
            return 80;
        case MoveCat::SMITH:
            return 90;
        case MoveCat::LIFT:
            return 110;
        case MoveCat::TOKE:
            return 70;
        case MoveCat::DIG:
            return 100;
        case MoveCat::SMITH_CARD:
        case MoveCat::TOKE_CARD:
            return 50;
        case MoveCat::TREASURE_OPEN:
            return 100;
        case MoveCat::TREASURE_SKIP:
            return 5;
        case MoveCat::EVENT_OPTION:
            // All dialog options score equal, so the tie-break explores every
            // branch uniformly -- exactly what a coverage generator wants.
            return 100;
        case MoveCat::EVENT_GRID:
            return 100;
        case MoveCat::SHOP:
            // One weight for the whole bucket, and it must stay ABOVE the
            // implicit floor so a shop is actually shopped in rather than
            // walked through -- but the Proceed that leaves shares the bucket,
            // so an unaffordable shop still exits on the first pick.
            return kind == PolicyKind::HOARD_GOLD ? 10 : 100;
        case MoveCat::COUNT:
            break;
    }
    return 0;
}

}  // namespace

size_t policy_pick(PolicyKind kind, const RunController& rc, const Move* moves, size_t n,
                   PolicyRng& rng) noexcept {
    assert(n > 0);
    if (kind == PolicyKind::RANDOM) {
        return rng.below(static_cast<uint32_t>(n));
    }

    // Argmax with uniform tie-break. ONE rng draw per decision regardless of
    // how many maxima there are (reservoir sampling would consume a variable
    // number and make the policy stream depend on the tie count).
    int best = move_score(kind, rc, moves[0]);
    size_t ties = 1;
    for (size_t i = 1; i < n; ++i) {
        const int s = move_score(kind, rc, moves[i]);
        if (s > best) {
            best = s;
            ties = 1;
        } else if (s == best) {
            ++ties;
        }
    }
    uint32_t pick = rng.below(static_cast<uint32_t>(ties));
    for (size_t i = 0; i < n; ++i) {
        if (move_score(kind, rc, moves[i]) == best) {
            if (pick == 0) return i;
            --pick;
        }
    }
    return 0;  // unreachable
}

}  // namespace sts::fuzz
