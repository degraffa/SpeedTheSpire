// replay_run_diff -- the manual run-level replay harness for oracle spot-diffs.
//
// Usage:
//   replay_run_diff <run.jsonl> [<run2.jsonl> ...]
//                   [--verbose] [--pool-evidence] [--stop-on-diff]
//
// WHAT IT DOES. A campaign artifact is a sequence of (game state, command)
// records: record k holds the live game's state BEFORE its `action_command` was
// issued. This tool re-drives the SIMULATOR through the same commands from
// `run_begin(seed, ascension)` and, at every record, compares the simulator's
// RunState against the artifact's translated RunState with `diff_run_states`.
// That is the whole "sim side driven by hand" step the reward spot-diff needs,
// turned into a committed, re-runnable binary instead of a scratch main.
//
// WHAT IT COVERS, HONESTLY. It generalizes over runs, seeds and floors, but not
// over every room kind: a command the run layer has no analogue for (a shop
// screen, a potion discard) ends the replay with an explicit `STOP` reason and
// a count of how many records were verified first. It is therefore a REPLAY
// harness for the room content that is modelled, not the "seed a sim replay
// from any translated RunState" adapter -- that one resumes from a mid-run
// state without re-driving the prefix, and is still open.
//
// SCREEN-DRIVEN COMMAND MAPPING. CommunicationMod commands are screen-relative,
// so the artifact's `screen_type` (parsed here, alongside the translator's
// typed output) selects the interpretation:
//
//   EVENT   choose i        -> CHOOSE(i)          Neow blessing / event dialog
//   MAP     choose i        -> CHOOSE(next_nodes[i].x) or CHOOSE(kChooseBoss)
//   MAP     return          -> no-op (a pure UI dismissal)
//   COMBAT_REWARD choose i  -> CHOOSE(i)          claim item i / open the cards
//   COMBAT_REWARD proceed   -> DEFERRED, see below
//   CARD_REWARD choose i    -> CHOOSE(i)          take offered card i
//   CARD_REWARD skip        -> CHOOSE(kChooseSkipCard)
//   NONE    play i [t]      -> PLAY_CARD(i-1, t)  the game's index is 1-based
//   NONE    end             -> END_TURN
//   NONE    potion use s t  -> USE_POTION(s, t)
//   CHEST   proceed         -> CHOOSE(kChooseProceed)
//   REST    choose i        -> CHOOSE(i)
//   GRID    choose i        -> CHOOSE(i)
//   HAND_SELECT choose i    -> CHOOSE(i)
//
// WHY `proceed` IS DEFERRED. Pressing Proceed on a reward screen that still has
// unclaimed items shows the map WITHOUT leaving the room, and the driver's
// random-legal policy bounces proceed/return several times before it finally
// picks a node. The run layer has one door -- CHOOSE(kChooseProceed) leaves the
// reward screen for MAP_CHOICE and there is no way back -- so replaying each
// bounce literally would desync. Instead the reward screen is left lazily: the
// harness holds the simulator in COMBAT_REWARD and issues the proceed
// immediately before the map `choose` that actually moves. Neither side's
// RunState changes across a bounce, so every intervening record is still
// compared, and the comparison is what proves the elision was sound.
//
// The exit code is the number of files that ended with a divergence or an
// unmapped command (0 == every file replayed clean to its terminal).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "sts/diff/differ.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/translate/translate.hpp"

namespace {

using nlohmann::json;
using namespace sts::engine;

// --- the screen context the translator does not carry -----------------------

// The translator's output is RunState/CombatState; the transient screen the
// command was typed at is deliberately not part of either. This tool needs it
// to interpret the command, so it does one extra light JSON pass over the same
// file and keeps only the few presentation fields the mapping table above
// consults (plus the CARD_REWARD offer, which is the pool-order evidence).
struct ScreenInfo {
    std::string screen_type;
    int floor = 0;
    std::string room_type;
    std::vector<int> map_next_x;
    bool boss_available = false;
    std::vector<std::string> card_offer;     // CARD_REWARD: offered game ids
    std::vector<std::string> reward_types;   // COMBAT_REWARD: rewards[].reward_type
    std::vector<std::string> option_labels;  // EVENT: the dialog buttons
    std::string event_id;
};

[[nodiscard]] std::vector<ScreenInfo> read_screens(const std::string& path) {
    std::vector<ScreenInfo> out;
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const json rec = json::parse(line);
        if (rec.value("record_kind", std::string{}) != "action") continue;
        ScreenInfo s;
        const json& gs = rec.at("state_json").at("game_state");
        s.screen_type = gs.value("screen_type", std::string{});
        s.floor = gs.value("floor", 0);
        s.room_type = gs.value("room_type", std::string{});
        const auto ss = gs.find("screen_state");
        if (ss != gs.end() && ss->is_object()) {
            if (const auto n = ss->find("next_nodes"); n != ss->end() && n->is_array()) {
                for (const json& node : *n) s.map_next_x.push_back(node.value("x", -1));
            }
            s.boss_available = ss->value("boss_available", false);
            if (const auto c = ss->find("cards"); c != ss->end() && c->is_array()) {
                for (const json& card : *c) s.card_offer.push_back(card.value("id", std::string{}));
            }
            if (const auto r = ss->find("rewards"); r != ss->end() && r->is_array()) {
                for (const json& item : *r)
                    s.reward_types.push_back(item.value("reward_type", std::string{}));
            }
            s.event_id = ss->value("event_id", std::string{});
            if (const auto o = ss->find("options"); o != ss->end() && o->is_array()) {
                for (const json& opt : *o)
                    s.option_labels.push_back(opt.value("label", std::string{}));
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

// --- command parsing ---------------------------------------------------------

[[nodiscard]] std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream is(s);
    std::string tok;
    while (is >> tok) parts.push_back(tok);
    return parts;
}

// What one artifact command becomes on the sim side.
enum class MapKind : uint8_t {
    ACTIONS,    // apply `actions` in order
    NOOP,       // a pure UI command with no run-layer effect
    LEAVE_ROOM, // proceed out of the reward screen first, then apply `actions`
    TERMINAL,   // the run ended here; stop cleanly
    UNMAPPED,   // no run-layer analogue -- stop and say so
};

struct MappedCommand {
    MapKind kind = MapKind::UNMAPPED;
    std::vector<Action> actions;
    std::string reason;  // set when UNMAPPED
};

// The game's grid screens list a FILTERED master deck (getPurgeableCards drops
// curses, getUpgradableCards additionally drops already-upgraded cards) and its
// `choose i` indexes that filtered list. The run layer instead addresses the
// stable master-deck index and publishes which of those are legal, so the
// translation from one to the other is "the i-th legal master-deck index".
[[nodiscard]] int grid_index_to_master_deck(const RunController& rc, int filtered) {
    if (filtered < 0) return -1;
    RunActionMask m{};
    legal_actions(rc, m);
    int seen = 0;
    for (int i = 0; i < kMasterDeckCap; ++i) {
        if (!m.can_choose_master_deck[i]) continue;
        if (seen == filtered) return i;
        ++seen;
    }
    return -1;
}

[[nodiscard]] MappedCommand map_command(const RunController& rc, const ScreenInfo& s,
                                        const std::string& cmd) {
    const std::vector<std::string> p = split_ws(cmd);
    MappedCommand m;
    if (p.empty()) {
        m.reason = "empty command";
        return m;
    }
    const std::string& verb = p[0];
    auto arg = [&](std::size_t i) -> int {
        return i < p.size() ? std::stoi(p[i]) : -1;
    };

    if (verb == "__terminal_observed__") {
        m.kind = MapKind::TERMINAL;
        return m;
    }

    if (s.screen_type == "NONE") {  // in combat
        if (verb == "play") {
            const int hand_1based = arg(1);
            const int target = p.size() >= 3 ? arg(2) : 0;
            if (hand_1based < 1) {
                m.reason = "play with no card index";
                return m;
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::PLAY_CARD,
                                            static_cast<uint8_t>(hand_1based - 1),
                                            static_cast<uint8_t>(target < 0 ? 0 : target)));
            return m;
        }
        if (verb == "end") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::END_TURN));
            return m;
        }
        if (verb == "potion" && p.size() >= 3 && p[1] == "use") {
            const int slot = arg(2);
            const int target = p.size() >= 4 ? arg(3) : 0;
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::USE_POTION,
                                            static_cast<uint8_t>(slot),
                                            static_cast<uint8_t>(target < 0 ? 0 : target)));
            return m;
        }
        m.reason = "combat command '" + verb + "' has no run-layer analogue";
        return m;
    }

    if (s.screen_type == "EVENT") {
        // Neow's dialog has two framing screens the run layer does not model:
        // the opening [Talk] (NeowEvent screenNum 0->3, no state change) and the
        // closing [Leave] (screenNum 99), which is the one that opens the map.
        // A [Leave] pressed again after the map is already up is another of the
        // policy's UI bounces -- see the header note on `proceed`.
        const bool single = s.option_labels.size() == 1;
        if (single && s.option_labels[0] == "Talk") {
            m.kind = MapKind::NOOP;
            return m;
        }
        if (single && s.option_labels[0] == "Leave") {
            if (rc.phase == static_cast<uint8_t>(RunPhase::NEOW)) {
                m.kind = MapKind::ACTIONS;
                m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            } else {
                m.kind = MapKind::NOOP;
            }
            return m;
        }
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        m.reason = "event command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "REST" || s.screen_type == "HAND_SELECT") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        if (verb == "proceed") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        m.reason = s.screen_type + " command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "GRID") {
        if (verb == "choose") {
            const int deck_index = grid_index_to_master_deck(rc, arg(1));
            if (deck_index < 0) {
                m.reason = "grid choose index has no legal master-deck slot";
                return m;
            }
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(deck_index)));
            return m;
        }
        if (verb == "proceed") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        m.reason = "grid command '" + verb +
                   "' has no run-layer analogue (a grid pick cannot be undone)";
        return m;
    }

    if (s.screen_type == "COMBAT_REWARD") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        if (verb == "proceed") {
            // See the header note: leaving is deferred to the map choice.
            m.kind = MapKind::NOOP;
            return m;
        }
        m.reason = "reward-screen command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "CARD_REWARD") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE,
                                            static_cast<uint8_t>(arg(1))));
            return m;
        }
        if (verb == "skip") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseSkipCard));
            return m;
        }
        if (verb == "sing" || verb == "bowl") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseSing));
            return m;
        }
        m.reason = "card-screen command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "CHEST") {
        if (verb == "choose") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseOpenChest));
            return m;
        }
        if (verb == "proceed") {
            m.kind = MapKind::ACTIONS;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, kChooseProceed));
            return m;
        }
        m.reason = "chest command '" + verb + "' is not modelled";
        return m;
    }

    if (s.screen_type == "MAP") {
        if (verb == "return") {
            m.kind = MapKind::NOOP;
            return m;
        }
        if (verb == "choose") {
            const int idx = arg(1);
            // choice_list at a map screen is next_nodes in order, with the boss
            // edge appended when it is offered.
            uint8_t dst = 0;
            if (idx >= 0 && idx < static_cast<int>(s.map_next_x.size())) {
                dst = static_cast<uint8_t>(s.map_next_x[static_cast<std::size_t>(idx)]);
            } else if (s.boss_available) {
                dst = kChooseBoss;
            } else {
                m.reason = "map choose index out of range";
                return m;
            }
            m.kind = MapKind::LEAVE_ROOM;
            m.actions.push_back(make_action(ActionVerb::CHOOSE, dst));
            return m;
        }
        m.reason = "map command '" + verb + "' is not modelled "
                   "(the run layer has no out-of-combat potion discard)";
        return m;
    }

    m.reason = "screen '" + s.screen_type + "' is not modelled by the run layer";
    return m;
}

// --- what is comparable ------------------------------------------------------

// A handful of RunState fields cannot be compared between a captured run and a
// replayed one, and each has a specific reason that is NOT "the sim is wrong".
// They are neutralized on BOTH sides so the surviving diff is signal. Nothing
// in the acceptance's field table is touched: gold, the potion slots, every
// master-deck card_id/upgrade, both pity counters and all eight run streams
// except neow_rng stay exactly as they are.
//
//   master_deck[].cost_now -- the capture reports each card's DISPLAY cost;
//       the sim only assigns cost_now to combat instances, leaving master-deck
//       rows at 0. Deck identity is (card_id, upgrade), both still compared.
//   map[] -- the capture's MAP screen exposes only the current node and its
//       outgoing edges, never the whole grid, so the translator has nothing to
//       write. Map generation has its own node-for-node oracle proof (B4.1/B4.2).
//   purge_cost -- shop state; the run layer has no shop, so it never moves off
//       its initial value.
//   neow_rng -- the fork's oracle block emits 13 streams and neowRng is not one
//       of them, so the capture carries no value to compare against.
void neutralize_incomparable(RunState& s) noexcept {
    for (auto& c : s.master_deck) c.cost_now = 0;
    for (auto& n : s.map) n = MapNode{};
    s.purge_cost = 0;
    s.neow_rng = RngStream{};
}

// DURING a combat the run layer deliberately does not write the live sheet back
// into RunState: hp/max_hp live in CombatState, gold accrued mid-combat sits in
// the combat accumulator, and relic counters tick on the combat mirror. All of
// it is settled in one place when the fight ends (run_advance.cpp's
// fold_back_combat). The capture, by contrast, reports the game's live values on
// every in-combat dump, so a mid-combat record can only be compared against the
// folded projection. This applies exactly that projection to a COPY -- it makes
// the comparison STRICTER (the live HP is now checked at every action), it never
// runs on a reward record, and the fields it touches are compared as usual
// everywhere else.
void project_live_combat_sheet(const RunController& rc, RunState& actual) noexcept {
    if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) return;
    actual.hp = rc.combat.player_hp;
    actual.max_hp = rc.combat.player_max_hp;
    actual.gold += static_cast<int32_t>(rc.combat.combat_gold);
    const uint8_t n = rc.combat.relic_count < rc.run.relic_count
                          ? rc.combat.relic_count : rc.run.relic_count;
    for (uint8_t i = 0; i < n; ++i) actual.relics[i].counter = rc.combat.relics[i].counter;
}

// A deck divergence whose count and upgrades agree but whose card id differs is
// the documented card-pool library-order deviation, not a stream bug (the
// runbook's read-out rule). Recognizing it here is what lets the read-out say
// "streams and pity zero-diff; only the picked identity moved".
[[nodiscard]] bool is_deck_identity_diff(const sts::diff::FieldDiff& d) {
    return d.field_name.rfind("master_deck[", 0) == 0 &&
           d.field_name.size() > 8 &&
           d.field_name.compare(d.field_name.size() - 8, 8, ".card_id") == 0;
}

// --- driving -----------------------------------------------------------------

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

[[nodiscard]] const char* phase_name(uint8_t p) {
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
    }
    return "?";
}

struct Options {
    bool verbose = false;
    bool pool_evidence = false;
    bool stop_on_diff = false;
    bool combat = false;   // also diff the in-combat CombatState (diagnosis aid)
    bool full_replay = false;  // whole-run replay instead of the reward spot-diff
};

// One file's verdict.
struct Verdict {
    int records_compared = 0;
    int reward_records_compared = 0;
    int diverged_at = -1;        // first record with a REAL divergence
    int deck_identity_records = 0;  // records whose only diff was library order
    std::string stop_reason;
    bool clean = false;          // no real divergence anywhere
};

// Print the sim's assembled reward offer beside the artifact's, which is the
// raw material the card-pool library-order recovery consumes.
void print_pool_evidence(const std::string& seed_string, int floor,
                         const RunController& rc, const ScreenInfo& s) {
    std::printf("POOL floor=%d seed=%s cardRng.counter=%u offered=[",
                floor, seed_string.c_str(), rc.run.card_rng.counter);
    for (std::size_t i = 0; i < s.card_offer.size(); ++i)
        std::printf("%s%s", i ? "|" : "", s.card_offer[i].c_str());
    std::printf("] sim=[");
    const uint8_t open = rc.rewards.open_card_item;
    if (open != kNoOpenCardReward && open < rc.rewards.count) {
        const RunRewardItem& it = rc.rewards.items[open];
        for (uint8_t i = 0; i < it.card_count; ++i)
            std::printf("%s%s", i ? "|" : "",
                        std::string(sts::registry::card_game_id(
                                        static_cast<CardId>(it.card_ids[i])))
                            .c_str());
    }
    std::printf("]\n");
}

[[nodiscard]] Verdict replay_one(const std::string& path, const Options& opts) {
    Verdict v;
    const sts::translate::TranslatedRun run = sts::translate::translate_file(path);
    const std::vector<ScreenInfo> screens = read_screens(path);
    if (screens.size() != run.records.size())
        throw std::runtime_error("screen/record count mismatch in " + path);

    RunController rc = run_begin(run.seed, 20);

    for (std::size_t k = 0; k < run.records.size(); ++k) {
        const sts::translate::TranslatedRecord& rec = run.records[k];
        const ScreenInfo& s = screens[k];
        const bool is_reward = s.screen_type == "COMBAT_REWARD" ||
                               s.screen_type == "CARD_REWARD";

        RunState expected = rec.run;
        RunState actual = rc.run;
        project_live_combat_sheet(rc, actual);
        neutralize_incomparable(expected);
        neutralize_incomparable(actual);
        const sts::diff::DiffReport rep = sts::diff::diff_run_states(expected, actual);
        ++v.records_compared;
        if (is_reward) ++v.reward_records_compared;

        std::size_t deck_id_diffs = 0;
        for (const auto& d : rep.diffs)
            if (is_deck_identity_diff(d)) ++deck_id_diffs;
        const bool only_library_order = !rep.empty() && deck_id_diffs == rep.size();

        if (only_library_order) {
            ++v.deck_identity_records;
            std::printf("LIBORD seq=%d floor=%d screen=%s cmd='%s': %zu deck identity "
                        "field%s differ; count/upgrade/streams/pity all equal\n",
                        rec.seq, s.floor, s.screen_type.c_str(),
                        rec.action_command.c_str(), rep.size(),
                        rep.size() == 1 ? "" : "s");
            std::printf("%s\n", rep.to_string().c_str());
        } else if (!rep.empty()) {
            if (v.diverged_at < 0) v.diverged_at = static_cast<int>(k);
            std::printf("DIFF seq=%d floor=%d screen=%s sim_phase=%s cmd='%s' (%zu field%s)\n",
                        rec.seq, s.floor, s.screen_type.c_str(), phase_name(rc.phase),
                        rec.action_command.c_str(), rep.size(),
                        rep.size() == 1 ? "" : "s");
            std::printf("%s\n", rep.to_string().c_str());
            if (opts.stop_on_diff) {
                v.stop_reason = "first divergence";
                return v;
            }
        } else if (opts.verbose) {
            std::printf("ok   seq=%d floor=%d screen=%-13s sim_phase=%-18s cmd='%s'\n",
                        rec.seq, s.floor, s.screen_type.c_str(),
                        phase_name(rc.phase), rec.action_command.c_str());
        }

        // Optional diagnosis aid: when a run-level divergence appears mid-fight,
        // the cause is almost always inside the combat, and the capture carries
        // a full CombatState for every in-combat dump. This is a triage print,
        // not part of the acceptance -- the combat schema has plenty of fields
        // the capture cannot supply, so it is never a pass/fail signal.
        if (opts.combat && rec.in_combat &&
            rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            const sts::diff::DiffReport crep = sts::diff::diff_states(rec.combat, rc.combat);
            if (!crep.empty())
                std::printf("  combat seq=%d: %zu field(s)\n%s\n", rec.seq, crep.size(),
                            crep.to_string().c_str());
        }

        if (opts.pool_evidence && s.screen_type == "CARD_REWARD")
            print_pool_evidence(run.seed_string, s.floor, rc, s);

        const MappedCommand m = map_command(rc, s, rec.action_command);
        if (m.kind == MapKind::TERMINAL) {
            v.stop_reason = "run terminal";
            v.clean = v.diverged_at < 0;
            return v;
        }
        if (m.kind == MapKind::UNMAPPED) {
            v.stop_reason = "seq " + std::to_string(rec.seq) + " cmd '" +
                            rec.action_command + "': " + m.reason;
            return v;
        }
        if (m.kind == MapKind::LEAVE_ROOM &&
            rc.phase == static_cast<uint8_t>(RunPhase::COMBAT_REWARD)) {
            step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
        }
        for (const Action a : m.actions) step(rc, a);

        if (rc.phase == static_cast<uint8_t>(RunPhase::ROOM_UNIMPLEMENTED)) {
            v.stop_reason = "sim parked at ROOM_UNIMPLEMENTED (room_type=" +
                            std::to_string(rc.room_type) + ") after seq " +
                            std::to_string(rec.seq);
            return v;
        }
    }
    v.stop_reason = "artifact exhausted";
    v.clean = v.diverged_at < 0;
    return v;
}

// --- the reward spot-diff (the acceptance read-out) --------------------------
//
// The whole-run replay above needs every room between the seed and the reward
// screen to be modelled. The reward screens themselves do not: a reward screen
// is a pure function of the state the battle-over block sees, and the capture
// records that state exactly (streams, pity, deck, relics, potion slots) on the
// last in-combat dump. So this mode SEEDS the simulator from that translated
// RunState and exercises only what B4.5 owns, per reward screen:
//
//   1. ASSEMBLY. assemble_combat_rewards() off the captured pre-battle-over
//      streams. Compared against the next captured record: cardRng /
//      treasureRng / potionRng / relicRng, card_blizz_randomizer,
//      blizzard_potion_mod, plus the offered item list and card identities
//      against the captured reward screen.
//   2. CLAIM. A RunController parked in COMBAT_REWARD with the captured
//      post-assembly RunState and the assembled screen, driven through the
//      artifact's own claim commands, then diffed whole against the captured
//      post-claim record -- which is where gold, potions[] and master_deck[]
//      are proved.
//
// Seeding from a translated RunState instead of re-driving the prefix is what
// makes this independent of combat fidelity, and it is why one deferred relic
// body upstream cannot hide a reward-layer defect.

[[nodiscard]] RoomType room_type_from_capture(const std::string& s) noexcept {
    if (s == "MonsterRoomElite") return RoomType::Elite;
    if (s == "MonsterRoomBoss") return RoomType::Boss;
    if (s == "EventRoom") return RoomType::Event;
    return RoomType::Monster;
}

[[nodiscard]] const char* reward_kind_name(uint8_t k) {
    switch (static_cast<RewardItemKind>(k)) {
        case RewardItemKind::NONE: return "NONE";
        case RewardItemKind::GOLD: return "GOLD";
        case RewardItemKind::POTION: return "POTION";
        case RewardItemKind::RELIC: return "RELIC";
        case RewardItemKind::CARDS: return "CARD";
        case RewardItemKind::STOLEN_GOLD: return "STOLEN_GOLD";
    }
    return "?";
}

struct SpotVerdict {
    int screens = 0;
    int assembly_clean = 0;
    int claim_clean = 0;
    int claim_library_order_only = 0;
    int failures = 0;
};

// Compare only the fields the reward ASSEMBLY moves. Everything else on the
// state (hp after Burning Blood, the room bookkeeping) belongs to the combat
// end, not to B4.5, and is deliberately out of this comparison.
void diff_assembly_fields(const RunState& expected, const RunState& actual,
                          std::vector<std::string>& out) {
    auto stream = [&](const char* name, const RngStream& e, const RngStream& a) {
        if (e.s0 != a.s0 || e.s1 != a.s1 || e.counter != a.counter) {
            char buf[256];
            std::snprintf(buf, sizeof buf,
                          "%s: {%llu,%llu,%u} -> {%llu,%llu,%u}", name,
                          static_cast<unsigned long long>(e.s0),
                          static_cast<unsigned long long>(e.s1), e.counter,
                          static_cast<unsigned long long>(a.s0),
                          static_cast<unsigned long long>(a.s1), a.counter);
            out.emplace_back(buf);
        }
    };
    stream("card_rng", expected.card_rng, actual.card_rng);
    stream("treasure_rng", expected.treasure_rng, actual.treasure_rng);
    stream("potion_rng", expected.potion_rng, actual.potion_rng);
    stream("relic_rng", expected.relic_rng, actual.relic_rng);
    if (expected.card_blizz_randomizer != actual.card_blizz_randomizer) {
        out.emplace_back("card_blizz_randomizer: " +
                         std::to_string(expected.card_blizz_randomizer) + " -> " +
                         std::to_string(actual.card_blizz_randomizer));
    }
    if (expected.blizzard_potion_mod != actual.blizzard_potion_mod) {
        out.emplace_back("blizzard_potion_mod: " +
                         std::to_string(expected.blizzard_potion_mod) + " -> " +
                         std::to_string(actual.blizzard_potion_mod));
    }
}

[[nodiscard]] SpotVerdict spot_diff_one(const std::string& path, const Options& opts) {
    SpotVerdict v;
    const sts::translate::TranslatedRun run = sts::translate::translate_file(path);
    const std::vector<ScreenInfo> screens = read_screens(path);
    if (screens.size() != run.records.size())
        throw std::runtime_error("screen/record count mismatch in " + path);

    for (std::size_t k = 1; k < run.records.size(); ++k) {
        if (screens[k].screen_type != "COMBAT_REWARD") continue;
        if (!run.records[k - 1].in_combat) continue;  // not the screen's opening
        ++v.screens;

        const ScreenInfo& open = screens[k];
        const int floor = open.floor;
        bool ok = true;

        // 1. assembly, off the captured pre-battle-over state.
        RunState rs = run.records[k - 1].run;
        RngStream misc = run.records[k - 1].combat.misc_rng;
        RewardScreen screen{};
        assemble_combat_rewards(rs, misc, room_type_from_capture(open.room_type),
                                RewardOutcome::KILLED, screen);

        std::vector<std::string> afail;
        diff_assembly_fields(run.records[k].run, rs, afail);

        // the item list itself
        std::vector<std::string> sim_kinds;
        for (uint8_t i = 0; i < screen.count; ++i)
            sim_kinds.emplace_back(reward_kind_name(screen.items[i].kind));
        if (sim_kinds != open.reward_types) {
            std::string e, a;
            for (const auto& x : open.reward_types) e += (e.empty() ? "" : ",") + x;
            for (const auto& x : sim_kinds) a += (a.empty() ? "" : ",") + x;
            afail.push_back("reward items: [" + e + "] -> [" + a + "]");
        }
        if (afail.empty()) {
            ++v.assembly_clean;
            std::printf("ASSEMBLY OK   %s floor=%d seq=%d items=[",
                        run.seed_string.c_str(), floor, run.records[k].seq);
            for (uint8_t i = 0; i < screen.count; ++i)
                std::printf("%s%s", i ? "," : "", reward_kind_name(screen.items[i].kind));
            std::printf("]\n");
        } else {
            ok = false;
            ++v.failures;
            std::printf("ASSEMBLY DIFF %s floor=%d seq=%d\n", run.seed_string.c_str(),
                        floor, run.records[k].seq);
            for (const auto& f : afail) std::printf("    %s\n", f.c_str());
        }

        // The card offer -- printed whenever there is one, because this is the
        // pool library-order evidence the read-out is here to collect.
        for (uint8_t i = 0; i < screen.count; ++i) {
            if (screen.items[i].kind != static_cast<uint8_t>(RewardItemKind::CARDS)) continue;
            std::printf("  OFFER %s floor=%d sim=[", run.seed_string.c_str(), floor);
            for (uint8_t c = 0; c < screen.items[i].card_count; ++c)
                std::printf("%s%s", c ? "|" : "",
                            std::string(sts::registry::card_game_id(
                                static_cast<CardId>(screen.items[i].card_ids[c]))).c_str());
            // the captured offer only appears once the pick screen is opened
            std::printf("] game=[");
            for (std::size_t j = k; j < screens.size(); ++j) {
                if (screens[j].screen_type == "CARD_REWARD" && !screens[j].card_offer.empty()) {
                    for (std::size_t c = 0; c < screens[j].card_offer.size(); ++c)
                        std::printf("%s%s", c ? "|" : "", screens[j].card_offer[c].c_str());
                    break;
                }
                if (screens[j].screen_type != "COMBAT_REWARD" &&
                    screens[j].screen_type != "MAP" &&
                    screens[j].screen_type != "CARD_REWARD") break;
            }
            std::printf("]\n");
        }

        // 2. the claim flow, from the captured post-assembly state.
        RunController rc{};
        rc.run = run.records[k].run;
        rc.combat = run.records[k - 1].combat;
        rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
        rc.room_type = static_cast<uint8_t>(room_type_from_capture(open.room_type));
        rc.combat_outcome = static_cast<uint8_t>(RunCombatOutcome::KILLED);
        rc.rewards = screen;
        rc.cur_x = 0;

        std::size_t j = k;
        for (; j < run.records.size(); ++j) {
            const std::string& st = screens[j].screen_type;
            if (st != "COMBAT_REWARD" && st != "CARD_REWARD" && st != "MAP") break;
            if (st == "MAP" && split_ws(run.records[j].action_command)[0] == "choose") break;
            const MappedCommand m = map_command(rc, screens[j], run.records[j].action_command);
            if (m.kind != MapKind::ACTIONS && m.kind != MapKind::NOOP) break;
            for (const Action a : m.actions) step(rc, a);
        }
        // The record we stopped at is the post-claim state.
        if (j < run.records.size()) {
            RunState expected = run.records[j].run;
            RunState actual = rc.run;
            neutralize_incomparable(expected);
            neutralize_incomparable(actual);
            const sts::diff::DiffReport rep = sts::diff::diff_run_states(expected, actual);
            std::size_t deck_only = 0;
            for (const auto& d : rep.diffs)
                if (is_deck_identity_diff(d)) ++deck_only;
            if (rep.empty()) {
                ++v.claim_clean;
                std::printf("CLAIM    OK   %s floor=%d seq=%d..%d gold=%d deck=%u\n",
                            run.seed_string.c_str(), floor, run.records[k].seq,
                            run.records[j].seq, rc.run.gold, rc.run.master_deck_count);
            } else if (deck_only == rep.size()) {
                ++v.claim_library_order_only;
                std::printf("CLAIM    LIBORD %s floor=%d seq=%d..%d: only picked-card "
                            "identity differs\n%s\n", run.seed_string.c_str(), floor,
                            run.records[k].seq, run.records[j].seq, rep.to_string().c_str());
            } else {
                ok = false;
                ++v.failures;
                std::printf("CLAIM    DIFF %s floor=%d seq=%d..%d (%zu field(s))\n%s\n",
                            run.seed_string.c_str(), floor, run.records[k].seq,
                            run.records[j].seq, rep.size(), rep.to_string().c_str());
            }
        }
        (void)ok;
        (void)opts;
    }
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--verbose") opts.verbose = true;
        else if (a == "--pool-evidence") opts.pool_evidence = true;
        else if (a == "--stop-on-diff") opts.stop_on_diff = true;
        else if (a == "--combat") opts.combat = true;
        else if (a == "--replay") opts.full_replay = true;
        else files.push_back(a);
    }
    if (files.empty()) {
        std::fprintf(stderr,
                     "usage: replay_run_diff <run.jsonl> [...] [--replay]\n"
                     "         [--verbose] [--pool-evidence] [--stop-on-diff] [--combat]\n"
                     "  default: per-reward-screen spot-diff seeded from the capture\n"
                     "  --replay: whole-run replay from run_begin, diffed per record\n");
        return 2;
    }

    if (!opts.full_replay) {
        int failures = 0;
        SpotVerdict total{};
        for (const std::string& f : files) {
            std::printf("=== %s\n", f.c_str());
            try {
                const SpotVerdict v = spot_diff_one(f, opts);
                std::printf("%s %s: %d reward screen(s), assembly clean %d, "
                            "claim clean %d (+%d library-order-only), failures %d\n",
                            v.failures == 0 ? "OK   " : "FAIL ", f.c_str(), v.screens,
                            v.assembly_clean, v.claim_clean, v.claim_library_order_only,
                            v.failures);
                total.screens += v.screens;
                total.assembly_clean += v.assembly_clean;
                total.claim_clean += v.claim_clean;
                total.claim_library_order_only += v.claim_library_order_only;
                total.failures += v.failures;
                if (v.failures != 0) ++failures;
            } catch (const std::exception& e) {
                std::printf("ERROR %s: %s\n", f.c_str(), e.what());
                ++failures;
            }
        }
        std::printf("--- %zu file(s): %d reward screen(s), assembly clean %d, "
                    "claim clean %d (+%d library-order-only), %d failing file(s) ---\n",
                    files.size(), total.screens, total.assembly_clean, total.claim_clean,
                    total.claim_library_order_only, failures);
        return failures;
    }

    int failures = 0;
    for (const std::string& f : files) {
        std::printf("=== %s\n", f.c_str());
        try {
            const Verdict v = replay_one(f, opts);
            std::printf("%s %s: %d record%s compared (%d on reward screens), "
                        "%d library-order-only; stop: %s\n",
                        v.clean ? "CLEAN" : "PART ", f.c_str(), v.records_compared,
                        v.records_compared == 1 ? "" : "s",
                        v.reward_records_compared, v.deck_identity_records,
                        v.stop_reason.c_str());
            if (!v.clean) ++failures;
        } catch (const std::exception& e) {
            std::printf("ERROR %s: %s\n", f.c_str(), e.what());
            ++failures;
        }
    }
    std::printf("--- %zu file(s), %d not clean ---\n", files.size(), failures);
    return failures;
}
