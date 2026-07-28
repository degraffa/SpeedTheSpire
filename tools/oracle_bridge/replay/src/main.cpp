// replay_run_diff -- the manual run-level replay harness for oracle spot-diffs.
//
// Usage:
//   replay_run_diff <run.jsonl> [<run2.jsonl> ...]
//                   [--replay | --neow | --shop]
//                   [--verbose] [--pool-evidence] [--stop-on-diff]
//
// FOUR MODES, one per acceptance read-out, all over the same artifacts:
//
//   (default) the combat-reward spot-diff  -- B4.5, see spot_diff_one below
//   --replay  the whole-run replay          -- diagnosis, see replay_one below
//   --neow    the floor-0 blessing spot-diff -- B4.14, see neow_spot_diff_one
//   --shop    the merchant spot-diff         -- B4.8,  see shop_spot_diff_one
//
// The three spot-diff modes all SEED the simulator from a translated RunState
// rather than re-driving the run from `run_begin`, which is what makes them
// independent of combat fidelity; only --replay re-drives, and only it needs
// every intervening room to be modelled.
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
// over every room kind: a command the run layer has no analogue for ends the
// replay with an explicit `STOP` reason and a count of how many records were
// verified first. The reason is the point -- a stop should say WHICH deferred
// body it hit, not describe the symptom, which is why a capture driving a grid
// the sim never opened now names the relic whose `onEquip` is deferred instead
// of complaining about the index. It is therefore a REPLAY harness for the room
// content that is modelled, not the "seed a sim replay from any translated
// RunState" adapter -- that one resumes from a mid-run state without re-driving
// the prefix, and is still open.
//
// SCREEN-DRIVEN COMMAND MAPPING. CommunicationMod commands are screen-relative,
// so the artifact's `screen_type` (parsed here, alongside the translator's
// typed output) selects the interpretation. The table lives in
// `command_map.hpp`, which has its own gtest (`replay_command_map_test`):
//
//   EVENT   choose i        -> CHOOSE(i)          Neow blessing / event dialog
//   EVENT   choose (1 page) -> DEFERRED, see below, once the event has exited
//   MAP     choose i        -> CHOOSE(next_nodes[i].x) or CHOOSE(kChooseBoss)
//   MAP     return          -> no-op (a pure UI dismissal)
//   COMBAT_REWARD choose i  -> CHOOSE(i)          claim item i / open the cards
//   COMBAT_REWARD proceed   -> DEFERRED, see below
//   CARD_REWARD choose i    -> CHOOSE(i)          take offered card i
//   CARD_REWARD skip        -> CHOOSE(kChooseSkipCard)
//   NONE    play i [t]      -> PLAY_CARD(i-1, t)  the game's index is 1-based
//   NONE    end             -> END_TURN
//   NONE    potion use s t  -> USE_POTION(s, t)
//   (any)   potion discard s-> DISCARD_POTION(s)   screen-independent: the belt
//                                                  is on the top panel
//   CHEST   proceed         -> CHOOSE(kChooseProceed)
//   REST    choose i        -> CHOOSE(i)
//   GRID    choose i        -> buffer the pick     see GridSession
//   GRID    cancel          -> drop the buffer
//   GRID    proceed         -> flush the buffer as CHOOSE(master-deck index)
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
// An event's terminal one-button page bounces IDENTICALLY, for the same reason
// -- AbstractEvent.openMap leaves the room's dialog mounted behind a dismissable
// map -- and is elided the same way, keyed off the simulator's phase rather
// than the button's label. The full citation trail is on `map_command`.
//
// The exit code is the number of files that ended with a divergence or an
// unmapped command (0 == every file replayed clean to its terminal).

#include <algorithm>
#include <cctype>
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
#include "sts/engine/neow.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/shop.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/translate/translate.hpp"

#include "command_map.hpp"

namespace {

using nlohmann::json;
using namespace sts::engine;
using namespace sts::replay;

// --- the screen context the translator does not carry -----------------------

// `ScreenInfo` and the whole command mapping live in `command_map.hpp`, which
// depends on nothing but the engine's run-layer headers so the table can be
// tested directly (`replay_command_map_test`). What stays here is the JSON pass
// that FILLS one: the translator's output is RunState/CombatState, so the
// transient screen the command was typed at has to be read from the artifact a
// second time. Only the presentation fields the mapping consults are kept, plus
// the CARD_REWARD offer, which is the pool-order evidence.

void read_stock(const json& screen_state, const char* key,
                std::vector<StockRow>& out) {
    const auto it = screen_state.find(key);
    if (it == screen_state.end() || !it->is_array()) return;
    for (const json& row : *it) {
        StockRow r;
        r.id = row.value("id", std::string{});
        r.name = row.value("name", std::string{});
        r.rarity = row.value("rarity", std::string{});
        r.price = row.value("price", 0);
        r.upgrades = row.value("upgrades", 0);
        out.push_back(std::move(r));
    }
}

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
        if (const auto cl = gs.find("choice_list"); cl != gs.end() && cl->is_array()) {
            for (const json& c : *cl)
                s.choice_list.push_back(c.is_string() ? c.get<std::string>()
                                                      : std::string{});
        }
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
            if (s.screen_type == "SHOP_SCREEN") {
                s.shop_screen = true;
                read_stock(*ss, "cards", s.shop_cards);
                read_stock(*ss, "relics", s.shop_relics);
                read_stock(*ss, "potions", s.shop_potions);
                s.purge_cost = ss->value("purge_cost", 0);
                s.purge_available = ss->value("purge_available", false);
            }
        }
        out.push_back(std::move(s));
    }
    return out;
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
//   purge_cost -- shop state; the run layer has no shop room, so a replayed
//       run never moves it. (The --shop mode DOES compare it: that mode drives
//       the merchant directly, where the ramp is the point.)
//   neow_rng -- the oracle block emits it, but only at floor 0: NeowEvent's rng
//       is event-scoped, so every later dump omits the key and the translator
//       leaves a value-init stream behind. (The --neow mode DOES compare it:
//       every record it looks at is a floor-0 record that carries the value.)
void neutralize_incomparable(RunState& s) noexcept {
    for (auto& c : s.master_deck) c.cost_now = 0;
    for (auto& n : s.map) n = MapNode{};
    s.purge_cost = 0;
    s.neow_rng = RngStream{};
}

// The floor-0 / merchant subset of the above. `map[]` is still unavailable from
// a capture, and a master-deck row's display cost is still not a schema field,
// but neither the Neow nor the shop read-out has any reason to drop purge_cost
// or neow_rng -- both are carried by every record those modes compare.
void neutralize_presentation_only(RunState& s) noexcept {
    for (auto& c : s.master_deck) c.cost_now = 0;
    for (auto& n : s.map) n = MapNode{};
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
        case RunPhase::SHOP: return "SHOP";
    }
    return "?";
}

struct Options {
    bool verbose = false;
    bool pool_evidence = false;
    bool stop_on_diff = false;
    bool combat = false;   // also diff the in-combat CombatState (diagnosis aid)
    bool full_replay = false;  // whole-run replay instead of the reward spot-diff
    bool neow = false;         // the floor-0 blessing spot-diff
    bool shop = false;         // the merchant spot-diff
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
    GridSession grid;

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

        // A grid session lives across records, so it is opened here rather than
        // in the table: the first GRID record snapshots the index space, and it
        // is dropped again the moment the capture leaves the screen.
        if (s.screen_type != "GRID") grid = GridSession{};

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
        if (m.kind == MapKind::GRID_PICK || m.kind == MapKind::GRID_CANCEL ||
            m.kind == MapKind::GRID_COMMIT) {
            if (!grid.open) open_grid_session(rc, grid);
            if (m.kind == MapKind::GRID_CANCEL) {
                grid.pending.clear();
                continue;
            }
            if (m.kind == MapKind::GRID_PICK) grid.pending.push_back(m.grid_index);
            // A two-pick grid has no confirm button: it commits on the pick that
            // fills it, and the only evidence of that is the screen being gone
            // from the next record. Flush on either signal.
            const bool closes =
                k + 1 >= screens.size() || screens[k + 1].screen_type != "GRID";
            if (m.kind != MapKind::GRID_COMMIT && !closes) continue;
            for (const int g : grid.pending) {
                if (g < 0 || g >= static_cast<int>(grid.filtered.size())) {
                    v.stop_reason = "seq " + std::to_string(rec.seq) + ": grid index " +
                                    std::to_string(g) + " is off the sim's " +
                                    std::to_string(grid.filtered.size()) +
                                    "-row grid";
                    return v;
                }
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(
                                         grid.filtered[static_cast<std::size_t>(g)])));
            }
            if (sim_grid_open(rc)) {
                // The sim wants picks the capture did not make, so the two are
                // not looking at the same grid. Stop: handing the next command
                // to a controller still parked on a grid would apply it to the
                // wrong screen. (The --neow mode carries the same guard.)
                v.stop_reason = "seq " + std::to_string(rec.seq) +
                                ": the sim's grid outlived the capture's " +
                                std::to_string(grid.pending.size()) + " pick(s)";
                return v;
            }
            grid = GridSession{};
            continue;
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

// --- the Neow spot-diff (--neow) ---------------------------------------------
//
// Floor 0 is the cheapest oracle comparison in the set, because nothing has to
// be replayed to reach it: `run_begin(seed, 20)` IS the blessing screen
// (neow.hpp, "the sim rolls this at run start"). Three checkpoints per seed:
//
//   OPTIONS     at the four-button record -- the four option meanings, joined
//               to the capture's localized labels, and the whole RunState with
//               neowRng included (the oracle block carries it here).
//   ACTIVATION  at the record immediately after the option was pressed -- the
//               drawback and the payout have run. This is where a boss swap's
//               ACQUISITION is proved: Burning Blood gone, the boss pool popped
//               by one, relicRng untouched.
//   POST-CHOICE at the first floor-0 MAP record -- the payout's sub-screen has
//               resolved and Neow is finished.
//
// A boss relic whose onEquip body is DEFERRED (Astrolabe and Empty Cage each
// open a grid the sim does not) reaches ACTIVATION and stops there. That is the
// documented divergence class, and it is exactly why ACTIVATION is a checkpoint
// of its own rather than a step on the way to the last one.

// THE LABEL JOIN. The capture carries NeowReward's localized optionLabel; the
// sim carries the meaning. The table is written in the RENDER direction -- sim
// meaning -> the label the game would have printed -- rather than as a parse,
// because three of the labels interpolate a number (the 10 % max-HP bonus and
// its double, the 10 % max-HP loss, and the current-HP damage) and rendering
// checks those numbers as part of the same comparison. A parse would have to
// throw them away. Category 2's label is the DRAWBACK text followed by the
// reward text, in the order they were rolled (NeowReward.java:105-122).
[[nodiscard]] std::string neow_reward_label(NeowRewardType t, int hp_bonus) {
    switch (t) {
        case NeowRewardType::THREE_CARDS: return "Choose a Card to obtain";
        case NeowRewardType::ONE_RANDOM_RARE_CARD: return "Obtain a random rare Card";
        case NeowRewardType::REMOVE_CARD: return "Remove a Card from your deck";
        case NeowRewardType::UPGRADE_CARD: return "Upgrade a Card";
        case NeowRewardType::TRANSFORM_CARD: return "Transform a Card";
        case NeowRewardType::RANDOM_COLORLESS: return "Choose a colorless Card to obtain";
        case NeowRewardType::THREE_SMALL_POTIONS: return "Obtain 3 random Potions";
        case NeowRewardType::RANDOM_COMMON_RELIC: return "Obtain a random common Relic";
        case NeowRewardType::TEN_PERCENT_HP_BONUS:
            return "Max HP +" + std::to_string(hp_bonus);
        case NeowRewardType::THREE_ENEMY_KILL:
            return "Enemies in your next three combats have 1 HP";
        case NeowRewardType::HUNDRED_GOLD: return "Obtain 100 Gold";
        case NeowRewardType::RANDOM_COLORLESS_2:
            return "Choose a rare colorless Card to obtain";
        case NeowRewardType::REMOVE_TWO: return "Remove 2 Cards";
        case NeowRewardType::ONE_RARE_RELIC: return "Obtain a random rare Relic";
        case NeowRewardType::THREE_RARE_CARDS: return "Choose a rare Card to obtain";
        case NeowRewardType::TWO_FIFTY_GOLD: return "Gain 250 Gold";
        case NeowRewardType::TRANSFORM_TWO_CARDS: return "Transform 2 Cards";
        case NeowRewardType::TWENTY_PERCENT_HP_BONUS:
            return "Max HP +" + std::to_string(hp_bonus * 2);
        case NeowRewardType::BOSS_RELIC:
            return "Lose your starting Relic Obtain a random boss Relic";
        case NeowRewardType::NONE: break;
    }
    return "(none)";
}

[[nodiscard]] std::string neow_drawback_label(NeowDrawback d, int hp_bonus, int hp) {
    switch (d) {
        case NeowDrawback::TEN_PERCENT_HP_LOSS:
            return "Lose " + std::to_string(hp_bonus) + " Max HP";
        case NeowDrawback::NO_GOLD: return "Lose all Gold";
        case NeowDrawback::CURSE: return "Obtain a Curse";
        case NeowDrawback::PERCENT_DAMAGE:
            // NeowReward.java:206 -- currentHealth / 10 * 3, integer-divided.
            return "Take " + std::to_string(hp / 10 * 3) + " damage";
        case NeowDrawback::NONE: break;
    }
    return "";
}

// The whole label for one slot, drawback first.
[[nodiscard]] std::string neow_option_label(const NeowState& n, int slot, int hp) {
    const auto d = static_cast<NeowDrawback>(n.option_drawback[slot]);
    const std::string reward =
        neow_reward_label(static_cast<NeowRewardType>(n.option_type[slot]),
                          n.hp_bonus);
    if (d == NeowDrawback::NONE) return reward;
    return neow_drawback_label(d, n.hp_bonus, hp) + " " + reward;
}

// `GridSession` and `open_grid_session` moved to command_map.hpp when --replay
// grew the same need. The buffering is part of what a captured grid command
// MEANS -- the game selects on click and commits on a button, the run layer
// does both at once -- so it belongs beside the table and its gtest rather than
// inside one mode. This mode's use of it is unchanged.

struct NeowVerdict {
    std::string seed_string;
    bool options_clean = false;
    bool activation_clean = false;
    bool post_clean = false;
    bool post_reached = false;
    std::string chosen;      // the rendered meaning of the option taken
    std::string stop_reason; // why POST-CHOICE was not reached, when it was not
};

// Print a report and say whether it was empty.
[[nodiscard]] bool report_checkpoint(const char* what, const std::string& seed,
                                     const RunState& expected, const RunState& actual) {
    RunState e = expected;
    RunState a = actual;
    neutralize_presentation_only(e);
    neutralize_presentation_only(a);
    const sts::diff::DiffReport rep = sts::diff::diff_run_states(e, a);
    if (rep.empty()) {
        std::printf("  %-11s OK   %s\n", what, seed.c_str());
        return true;
    }
    std::printf("  %-11s DIFF %s (%zu field%s)\n%s\n", what, seed.c_str(), rep.size(),
                rep.size() == 1 ? "" : "s", rep.to_string().c_str());
    return false;
}

[[nodiscard]] NeowVerdict neow_spot_diff_one(const std::string& path, const Options& opts) {
    NeowVerdict v;
    const sts::translate::TranslatedRun run = sts::translate::translate_file(path);
    const std::vector<ScreenInfo> screens = read_screens(path);
    if (screens.size() != run.records.size())
        throw std::runtime_error("screen/record count mismatch in " + path);
    v.seed_string = run.seed_string;

    // The blessing screen is the EVENT record whose Neow dialog has all four
    // buttons up; the intro [Talk] screen has one (PROTOCOL "Event-scoped").
    std::size_t k = 0;
    for (; k < screens.size(); ++k)
        if (screens[k].event_id == "Neow Event" && screens[k].option_labels.size() == 4)
            break;
    if (k == screens.size()) {
        v.stop_reason = "no four-option Neow blessing record";
        return v;
    }

    RunController rc = run_begin(run.seed, 20);

    // 1. OPTIONS.
    bool labels_ok = true;
    for (int i = 0; i < kNeowOptionCount; ++i) {
        const std::string sim = neow_option_label(rc.neow, i, rc.run.hp);
        if (sim == screens[k].option_labels[static_cast<std::size_t>(i)]) continue;
        labels_ok = false;
        std::printf("  OPTION %d   DIFF %s\n      game: %s\n      sim : %s\n", i,
                    run.seed_string.c_str(),
                    screens[k].option_labels[static_cast<std::size_t>(i)].c_str(),
                    sim.c_str());
    }
    const bool state_ok =
        report_checkpoint("OPTIONS", run.seed_string, run.records[k].run, rc.run);
    v.options_clean = labels_ok && state_ok;
    if (labels_ok && opts.verbose) {
        std::printf("  OPTIONS OK  %s: [%s | %s | %s | %s]\n", run.seed_string.c_str(),
                    neow_option_label(rc.neow, 0, rc.run.hp).c_str(),
                    neow_option_label(rc.neow, 1, rc.run.hp).c_str(),
                    neow_option_label(rc.neow, 2, rc.run.hp).c_str(),
                    neow_option_label(rc.neow, 3, rc.run.hp).c_str());
    }

    // 2. Press the recorded option, then walk to the first MAP record.
    const std::vector<std::string> p = split_ws(run.records[k].action_command);
    if (p.size() < 2 || p[0] != "choose") {
        v.stop_reason = "blessing command is not a choose";
        return v;
    }
    const int chosen = std::stoi(p[1]);
    if (chosen < 0 || chosen >= kNeowOptionCount) {
        v.stop_reason = "blessing command chose slot " + std::to_string(chosen);
        return v;
    }
    v.chosen = neow_option_label(rc.neow, chosen, rc.run.hp);
    step(rc, make_action(ActionVerb::CHOOSE, static_cast<uint8_t>(chosen)));

    if (k + 1 >= run.records.size()) {
        v.stop_reason = "artifact ends at the blessing";
        return v;
    }
    v.activation_clean =
        report_checkpoint("ACTIVATION", run.seed_string, run.records[k + 1].run, rc.run);

    GridSession grid;
    for (std::size_t j = k + 1; j < run.records.size(); ++j) {
        const ScreenInfo& s = screens[j];
        if (s.floor != 0) {
            v.stop_reason = "left floor 0 without a map record";
            return v;
        }
        if (s.screen_type == "MAP") {
            v.post_reached = true;
            v.post_clean =
                report_checkpoint("POST-CHOICE", run.seed_string, run.records[j].run, rc.run);
            return v;
        }
        if (s.screen_type != "GRID") grid = GridSession{};

        const std::vector<std::string> c = split_ws(run.records[j].action_command);
        if (c.empty()) {
            v.stop_reason = "empty command at seq " + std::to_string(run.records[j].seq);
            return v;
        }
        if (s.screen_type == "GRID") {
            if (!grid.open) {
                // A grid the BLESSING did not open belongs to whatever the
                // payout handed over -- in practice a boss relic whose onEquip
                // body is deferred (Astrolabe, Empty Cage). The acquisition
                // above is still proved; the body is not the blessing's.
                if (rc.neow.screen != static_cast<uint8_t>(NeowScreen::GRID)) {
                    std::string who = "?";
                    if (rc.run.relic_count > 0)
                        who = std::string(sts::registry::relic_game_id(
                            static_cast<RelicId>(
                                rc.run.relics[rc.run.relic_count - 1].relic_id)));
                    v.stop_reason =
                        "the capture opens a grid the blessing did not: " + who +
                        "'s onEquip body is deferred";
                    return v;
                }
                open_grid_session(rc, grid);
            }
            if (c[0] == "cancel") {
                grid.pending.clear();
                continue;
            }
            if (c[0] == "choose" && c.size() >= 2) grid.pending.push_back(std::stoi(c[1]));
            const bool closes = j + 1 >= screens.size() || screens[j + 1].screen_type != "GRID";
            if (c[0] != "proceed" && !closes) continue;
            for (const int g : grid.pending) {
                if (g < 0 || g >= static_cast<int>(grid.filtered.size())) {
                    v.stop_reason = "grid index " + std::to_string(g) + " is off the deck";
                    return v;
                }
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(grid.filtered[static_cast<std::size_t>(g)])));
            }
            if (rc.neow.screen == static_cast<uint8_t>(NeowScreen::GRID)) {
                // The sim still wants picks the capture did not make: the grid
                // belongs to a relic body the sim defers, not to the blessing.
                v.stop_reason = "sim's Neow grid outlived the capture's picks "
                                "(a deferred relic onEquip owns this grid)";
                return v;
            }
            grid = GridSession{};
            continue;
        }

        const MappedCommand m = map_command(rc, s, run.records[j].action_command);
        if (m.kind == MapKind::UNMAPPED || m.kind == MapKind::TERMINAL) {
            v.stop_reason = "seq " + std::to_string(run.records[j].seq) + " cmd '" +
                            run.records[j].action_command + "': " +
                            (m.reason.empty() ? "run terminal" : m.reason);
            return v;
        }
        // The Neow reward screen's Proceed is not the combat-reward Proceed the
        // shared mapper defers: it closes the potion screen and finishes the
        // payout (run_advance's NeowScreen::ITEM_REWARD arm).
        if (s.screen_type == "COMBAT_REWARD" && c[0] == "proceed") {
            step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
            continue;
        }
        for (const Action a : m.actions) step(rc, a);
    }
    v.stop_reason = "artifact exhausted before a map record";
    return v;
}

// --- the merchant spot-diff (--shop) -----------------------------------------
//
// A shop is a pure function of the state the room entry sees, so this mode does
// not replay anything either. Per shop VISIT:
//
//   STOCK      seed a RunState from the last record BEFORE the map choice that
//              entered the room, call generate_shop, and compare the result
//              against the captured SHOP_SCREEN: seven card ids + prices, three
//              relics, three potions, the purge cost, and the merchant's own
//              stream/pool accounting against the first in-room record.
//   PURCHASES  restart from the first IN-ROOM record's RunState (which already
//              carries the room-entry bookkeeping the merchant build is not
//              responsible for), drive the recorded buys/purges, and diff the
//              whole RunState against every subsequent in-room record.
//
// The shop's card pools are game_id-sorted by CardGroup.getRandomCard before
// the draw (b48_shop_spotdiff.md §4's known-benign list), so unlike a combat
// reward there is no library-order carve-out here: a card-id mismatch is a
// divergence.
//
// WHY THIS DRIVES ShopState DIRECTLY AND NOT A RunController PARKED IN
// RunPhase::SHOP. The phase exists and its CHOOSE flow has its own tests, but
// parking a controller mid-run needs the transient run scaffolding a capture
// cannot supply (map cursor, encounter lists and their cursors) -- the same
// B1.6 gap the reward mode works around. A merchant needs none of it: it is a
// pure function of the streams, the relic pools, the owned relics and the
// ascension, all of which the translated RunState carries. Driving the module
// keeps the read-out about the merchant.

[[nodiscard]] std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

// Which shop row a `choose i` names, resolved through the capture's own
// choice_list: the game lists the AFFORDABLE unsold rows by display name, so
// the index means nothing without it.
enum class ShopPick : uint8_t { NONE, COLORED, COLORLESS, RELIC, POTION, PURGE };

struct ShopTarget {
    ShopPick what = ShopPick::NONE;
    uint8_t index = 0;
};

[[nodiscard]] ShopTarget resolve_shop_choice(const ScreenInfo& s, int choice) {
    ShopTarget t;
    if (choice < 0 || choice >= static_cast<int>(s.choice_list.size())) return t;
    const std::string want = lower(s.choice_list[static_cast<std::size_t>(choice)]);
    if (want == "purge") {
        t.what = ShopPick::PURGE;
        return t;
    }
    for (std::size_t i = 0; i < s.shop_cards.size(); ++i) {
        if (lower(s.shop_cards[i].name) != want) continue;
        t.what = i < kShopColoredCount ? ShopPick::COLORED : ShopPick::COLORLESS;
        t.index = static_cast<uint8_t>(i < kShopColoredCount ? i : i - kShopColoredCount);
        return t;
    }
    for (std::size_t i = 0; i < s.shop_relics.size(); ++i) {
        if (lower(s.shop_relics[i].name) != want) continue;
        t.what = ShopPick::RELIC;
        t.index = static_cast<uint8_t>(i);
        return t;
    }
    for (std::size_t i = 0; i < s.shop_potions.size(); ++i) {
        if (lower(s.shop_potions[i].name) != want) continue;
        t.what = ShopPick::POTION;
        t.index = static_cast<uint8_t>(i);
        return t;
    }
    return t;
}

struct ShopVerdict {
    int visits = 0;         // merchants built
    int screens = 0;        // merchants whose stock the capture actually shows
    int stock_clean = 0;
    int purchase_clean = 0;  // visits whose whole in-room record walk diffed clean
    int purchase_partial = 0;
    int failures = 0;
};

// One stock row, sim side against capture side.
void diff_stock_row(const char* group, std::size_t i, const std::string& game_id,
                    int game_price, int game_upgrade, const std::string& sim_id,
                    int sim_price, int sim_upgrade, std::vector<std::string>& out) {
    if (game_id == sim_id && game_price == sim_price && game_upgrade == sim_upgrade)
        return;
    out.push_back(std::string(group) + "[" + std::to_string(i) + "]: " + game_id + "@" +
                  std::to_string(game_price) + (game_upgrade ? "+" : "") + " -> " + sim_id +
                  "@" + std::to_string(sim_price) + (sim_upgrade ? "+" : ""));
}

[[nodiscard]] int card_base_price_from_capture(const std::string& rarity) {
    if (rarity == "RARE") return card_base_price(RewardCardRarity::RARE);
    if (rarity == "UNCOMMON") return card_base_price(RewardCardRarity::UNCOMMON);
    return card_base_price(RewardCardRarity::COMMON);
}

[[nodiscard]] ShopVerdict shop_spot_diff_one(const std::string& path, const Options& opts) {
    ShopVerdict v;
    const sts::translate::TranslatedRun run = sts::translate::translate_file(path);
    const std::vector<ScreenInfo> screens = read_screens(path);
    if (screens.size() != run.records.size())
        throw std::runtime_error("screen/record count mismatch in " + path);

    for (std::size_t k = 0; k < screens.size(); ++k) {
        // A visit opens at the first in-ShopRoom record of a new floor.
        if (screens[k].room_type != "ShopRoom") continue;
        if (k > 0 && screens[k - 1].room_type == "ShopRoom" &&
            screens[k - 1].floor == screens[k].floor)
            continue;
        if (k == 0) continue;  // no pre-entry record to seed from
        ++v.visits;
        const int floor = screens[k].floor;

        // 1. STOCK, off the pre-entry state.
        RunState rs = run.records[k - 1].run;
        const ShopState shop = generate_shop(rs);

        std::vector<std::string> fail;
        auto stream = [&](const char* name, const RngStream& e, const RngStream& a) {
            if (e.s0 == a.s0 && e.s1 == a.s1 && e.counter == a.counter) return;
            fail.push_back(std::string(name) + ": counter " + std::to_string(e.counter) +
                           " -> " + std::to_string(a.counter) +
                           (e.s0 == a.s0 && e.s1 == a.s1 ? "" : " (raw state too)"));
        };
        const RunState& entered = run.records[k].run;
        stream("merchant_rng", entered.merchant_rng, rs.merchant_rng);
        stream("card_rng", entered.card_rng, rs.card_rng);
        stream("potion_rng", entered.potion_rng, rs.potion_rng);
        if (entered.card_blizz_randomizer != rs.card_blizz_randomizer)
            fail.push_back("card_blizz_randomizer moved across the merchant build");
        if (entered.purge_cost != rs.purge_cost)
            fail.push_back("purge_cost: " + std::to_string(entered.purge_cost) + " -> " +
                           std::to_string(rs.purge_cost));
        for (std::size_t t = 0; t < kRelicTierCount; ++t) {
            if (entered.relic_pool_count[t] != rs.relic_pool_count[t]) {
                fail.push_back("relic_pool[" + std::to_string(t) + "].count: " +
                               std::to_string(entered.relic_pool_count[t]) + " -> " +
                               std::to_string(rs.relic_pool_count[t]));
                continue;
            }
            for (uint8_t i = 0; i < rs.relic_pool_count[t]; ++i) {
                if (entered.relic_pools[t][i] == rs.relic_pools[t][i]) continue;
                fail.push_back("relic_pool[" + std::to_string(t) + "][" +
                               std::to_string(i) + "] differs");
                break;
            }
        }

        // The captured stock, when the run actually opened the merchant.
        std::size_t sk = k;
        for (; sk < screens.size(); ++sk) {
            if (screens[sk].floor != floor || screens[sk].room_type != "ShopRoom") {
                sk = screens.size();
                break;
            }
            if (screens[sk].shop_screen) break;
        }
        if (sk < screens.size() && screens[sk].shop_screen) {
            ++v.screens;
            const ScreenInfo& sc = screens[sk];
            for (std::size_t i = 0; i < sc.shop_cards.size(); ++i) {
                const ShopSlot& slot = i < kShopColoredCount
                                           ? shop.colored[i]
                                           : shop.colorless[i - kShopColoredCount];
                diff_stock_row("card", i, sc.shop_cards[i].id, sc.shop_cards[i].price,
                               sc.shop_cards[i].upgrades,
                               std::string(sts::registry::card_game_id(
                                   static_cast<CardId>(slot.id))),
                               slot.price, slot.upgrade, fail);
            }
            for (std::size_t i = 0; i < sc.shop_relics.size(); ++i) {
                diff_stock_row("relic", i, sc.shop_relics[i].id, sc.shop_relics[i].price, 0,
                               std::string(sts::registry::relic_game_id(
                                   static_cast<RelicId>(shop.relics[i].id))),
                               shop.relics[i].price, 0, fail);
            }
            for (std::size_t i = 0; i < sc.shop_potions.size(); ++i) {
                diff_stock_row("potion", i, sc.shop_potions[i].id, sc.shop_potions[i].price, 0,
                               std::string(sts::registry::potion_game_id(
                                   static_cast<PotionId>(shop.potions[i].id))),
                               shop.potions[i].price, 0, fail);
            }
            if (sc.purge_cost != shop.actual_purge_cost)
                fail.push_back("purge_cost on the shelf: " + std::to_string(sc.purge_cost) +
                               " -> " + std::to_string(shop.actual_purge_cost));
            if (sc.purge_available != (shop.purge_available != 0))
                fail.push_back("purge_available differs");
            // THE SALE INDEX, inferred independently of the sim: the capture has
            // no sale flag, but exactly one COLORED slot is priced at about half
            // its own base (ShopScreen halves before every discount), so the
            // cheapest price/base ratio names it.
            int sale = -1;
            double best = 1e9;
            for (std::size_t i = 0; i < kShopColoredCount && i < sc.shop_cards.size(); ++i) {
                const double ratio = static_cast<double>(sc.shop_cards[i].price) /
                                     card_base_price_from_capture(sc.shop_cards[i].rarity);
                if (ratio >= best) continue;
                best = ratio;
                sale = static_cast<int>(i);
            }
            if (sale != static_cast<int>(shop.sale_index) || best > 0.75)
                fail.push_back("sale slot: the capture's cheapest colored price/base ratio "
                               "is slot " + std::to_string(sale) + " at " +
                               std::to_string(best) + " -> sim sale_index " +
                               std::to_string(shop.sale_index));
        }

        if (fail.empty()) {
            ++v.stock_clean;
            std::printf("STOCK    OK   %s floor=%d merchantRng %u->%u cardRng %u->%u "
                        "potionRng %u->%u purge=%d sale=%u\n",
                        run.seed_string.c_str(), floor,
                        run.records[k - 1].run.merchant_rng.counter, rs.merchant_rng.counter,
                        run.records[k - 1].run.card_rng.counter, rs.card_rng.counter,
                        run.records[k - 1].run.potion_rng.counter, rs.potion_rng.counter,
                        shop.actual_purge_cost, shop.sale_index);
        } else {
            ++v.failures;
            std::printf("STOCK    DIFF %s floor=%d\n", run.seed_string.c_str(), floor);
            for (const auto& f : fail) std::printf("    %s\n", f.c_str());
        }
        if (opts.verbose && sk < screens.size() && screens[sk].shop_screen) {
            for (std::size_t i = 0; i < screens[sk].shop_cards.size(); ++i)
                std::printf("    card  %zu %-20s %4d\n", i, screens[sk].shop_cards[i].id.c_str(),
                            screens[sk].shop_cards[i].price);
        }

        // 2. PURCHASES, from the first in-room record.
        RunState buy = run.records[k].run;
        ShopState live = shop;
        // The floor-scoped miscRng a bought relic's onEquip would draw (War
        // Paint / Whetstone). The translator routes the oracle block's miscRng
        // into CombatState on EVERY record, in combat or not, so this is the
        // real value even though the shop room is not a fight.
        RngStream misc = run.records[k].combat.misc_rng;
        std::string stop;
        int compared = 0;
        std::size_t j = k + 1;
        GridSession grid;
        for (; j < screens.size(); ++j) {
            if (screens[j].floor != floor || screens[j].room_type != "ShopRoom") break;
            RunState e = run.records[j].run;
            RunState a = buy;
            neutralize_presentation_only(e);
            neutralize_presentation_only(a);
            // neowRng is floor-0 only; a shop record carries no value for it.
            e.neow_rng = RngStream{};
            a.neow_rng = RngStream{};
            const sts::diff::DiffReport rep = sts::diff::diff_run_states(e, a);
            ++compared;
            if (!rep.empty()) {
                ++v.failures;
                std::printf("PURCHASE DIFF %s floor=%d seq=%d (%zu field%s)\n%s\n",
                            run.seed_string.c_str(), floor, run.records[j].seq, rep.size(),
                            rep.size() == 1 ? "" : "s", rep.to_string().c_str());
                stop = "divergence";
                break;
            }

            const std::vector<std::string> c = split_ws(run.records[j].action_command);
            if (c.empty()) break;
            // The belt is drawn over the shop room too, and the captures use
            // it: STS00052 throws away the Fear Potion it had just bought,
            // three records after the purchase. This is not a merchant action
            // at all -- it moves no gold and no stream -- but it DOES empty a
            // RunState potion slot, so a walk that skipped it diffed the slot
            // against the capture for the rest of the visit. That is exactly
            // what the "STS00052 shop screen potions[0] FearPotion" row was.
            if (c[0] == "potion" && c.size() >= 3 && c[1] == "discard") {
                const int slot = std::stoi(c[2]);
                if (slot < 0 || slot >= kPotionCap ||
                    buy.potions[static_cast<std::size_t>(slot)] ==
                        static_cast<uint16_t>(PotionId::NONE)) {
                    stop = "seq " + std::to_string(run.records[j].seq) + " cmd '" +
                           run.records[j].action_command + "' names an empty slot";
                    break;
                }
                buy.potions[static_cast<std::size_t>(slot)] =
                    static_cast<uint16_t>(PotionId::NONE);
                continue;
            }
            if (screens[j].screen_type == "GRID") {
                if (!grid.open) {
                    grid.open = true;
                    grid.filtered.clear();
                    grid.pending.clear();
                    for (uint16_t i = 0; i < buy.master_deck_count; ++i)
                        if (shop_purge_card_legal(buy, live, i))
                            grid.filtered.push_back(i);
                }
                if (c[0] == "cancel") {
                    grid.pending.clear();
                    continue;
                }
                if (c[0] == "choose" && c.size() >= 2) grid.pending.push_back(std::stoi(c[1]));
                const bool closes =
                    j + 1 >= screens.size() || screens[j + 1].screen_type != "GRID";
                if (c[0] != "proceed" && !closes) continue;
                for (const int g : grid.pending) {
                    if (g < 0 || g >= static_cast<int>(grid.filtered.size()) ||
                        !shop_purge_card(buy, live,
                                         static_cast<uint16_t>(
                                             grid.filtered[static_cast<std::size_t>(g)]))) {
                        stop = "purge grid pick " + std::to_string(g) + " was refused";
                        break;
                    }
                }
                grid = GridSession{};
                if (!stop.empty()) break;
                continue;
            }
            if (screens[j].screen_type == "MAP") {
                // The map is up over the shop room. `return` dismisses it and
                // the visit continues; `choose` is the step onto the next
                // floor, so this record is the visit's last comparable state.
                if (c[0] == "choose") break;
                continue;
            }
            if (c[0] == "leave" || c[0] == "proceed" || c[0] == "return") continue;
            if (screens[j].screen_type == "SHOP_ROOM" && c[0] == "choose") continue;
            if (screens[j].screen_type != "SHOP_SCREEN" || c[0] != "choose") {
                stop = "seq " + std::to_string(run.records[j].seq) + " cmd '" +
                       run.records[j].action_command +
                       "' is not a merchant action and has no run-layer analogue";
                break;
            }
            const ShopTarget t =
                resolve_shop_choice(screens[j], c.size() >= 2 ? std::stoi(c[1]) : -1);
            bool applied = false;
            switch (t.what) {
                case ShopPick::COLORED:
                    applied = shop_buy_card(buy, live, t.index, /*colorless=*/false);
                    break;
                case ShopPick::COLORLESS:
                    applied = shop_buy_card(buy, live, t.index, /*colorless=*/true);
                    break;
                case ShopPick::RELIC:
                    applied = shop_buy_relic(buy, misc, live, t.index);
                    break;
                case ShopPick::POTION:
                    applied = shop_buy_potion(buy, live, t.index);
                    break;
                case ShopPick::PURGE:
                    applied = shop_purge_legal(buy, live);  // opens the grid, spends nothing
                    break;
                case ShopPick::NONE:
                    break;
            }
            if (!applied) {
                stop = "seq " + std::to_string(run.records[j].seq) + " cmd '" +
                       run.records[j].action_command + "' did not resolve to a legal row";
                break;
            }
        }
        if (stop.empty()) {
            ++v.purchase_clean;
            std::printf("PURCHASE OK   %s floor=%d %d in-room record%s compared, gold=%d "
                        "deck=%u purge_cost=%d\n",
                        run.seed_string.c_str(), floor, compared, compared == 1 ? "" : "s",
                        buy.gold, buy.master_deck_count, buy.purge_cost);
        } else {
            ++v.purchase_partial;
            std::printf("PURCHASE PART %s floor=%d %d in-room record%s compared; stop: %s\n",
                        run.seed_string.c_str(), floor, compared, compared == 1 ? "" : "s",
                        stop.c_str());
        }
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
        else if (a == "--neow") opts.neow = true;
        else if (a == "--shop") opts.shop = true;
        else files.push_back(a);
    }
    if (files.empty() ||
        static_cast<int>(opts.full_replay) + static_cast<int>(opts.neow) +
                static_cast<int>(opts.shop) > 1) {
        std::fprintf(stderr,
                     "usage: replay_run_diff <run.jsonl> [...] "
                     "[--replay | --neow | --shop]\n"
                     "         [--verbose] [--pool-evidence] [--stop-on-diff] [--combat]\n"
                     "  default: per-reward-screen spot-diff seeded from the capture\n"
                     "  --replay: whole-run replay from run_begin, diffed per record\n"
                     "  --neow:   floor-0 blessing spot-diff (options / activation / "
                     "post-choice)\n"
                     "  --shop:   merchant spot-diff (stock, prices, sale slot, purchases)\n"
                     "  the mode flags are mutually exclusive\n");
        return 2;
    }

    if (opts.neow) {
        int failures = 0;
        int full = 0;
        int activation_only = 0;
        for (const std::string& f : files) {
            std::printf("=== %s\n", f.c_str());
            try {
                const NeowVerdict v = neow_spot_diff_one(f, opts);
                const bool clean = v.options_clean && v.activation_clean && v.post_clean;
                std::printf("%s %s: options %s, activation %s, post-choice %s%s%s\n",
                            clean ? "OK   " : "PART ", v.seed_string.c_str(),
                            v.options_clean ? "clean" : "DIFF",
                            v.activation_clean ? "clean" : "DIFF",
                            v.post_reached ? (v.post_clean ? "clean" : "DIFF") : "not reached",
                            v.stop_reason.empty() ? "" : "; stop: ",
                            v.stop_reason.c_str());
                if (!v.chosen.empty())
                    std::printf("      took: %s\n", v.chosen.c_str());
                if (clean) ++full;
                else if (v.options_clean && v.activation_clean) ++activation_only;
                else ++failures;
            } catch (const std::exception& e) {
                std::printf("ERROR %s: %s\n", f.c_str(), e.what());
                ++failures;
            }
        }
        std::printf("--- %zu seed(s): %d fully zero-diff, %d clean through activation "
                    "only, %d diverged ---\n", files.size(), full, activation_only, failures);
        return failures;
    }

    if (opts.shop) {
        int failures = 0;
        ShopVerdict total{};
        for (const std::string& f : files) {
            std::printf("=== %s\n", f.c_str());
            try {
                const ShopVerdict v = shop_spot_diff_one(f, opts);
                total.visits += v.visits;
                total.screens += v.screens;
                total.stock_clean += v.stock_clean;
                total.purchase_clean += v.purchase_clean;
                total.purchase_partial += v.purchase_partial;
                total.failures += v.failures;
                if (v.failures != 0) ++failures;
            } catch (const std::exception& e) {
                std::printf("ERROR %s: %s\n", f.c_str(), e.what());
                ++failures;
            }
        }
        std::printf("--- %zu file(s): %d merchant(s) built (%d with a visible shelf), "
                    "stock clean %d, purchase walks clean %d (+%d partial), "
                    "%d divergence(s) ---\n",
                    files.size(), total.visits, total.screens, total.stock_clean,
                    total.purchase_clean, total.purchase_partial, total.failures);
        return failures;
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
