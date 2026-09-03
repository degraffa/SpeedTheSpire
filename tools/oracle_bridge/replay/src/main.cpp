// replay_run_diff -- the manual run-level replay harness for oracle spot-diffs.
//
// Usage:
//   replay_run_diff <run.jsonl> [<run2.jsonl> ...]
//                   [--replay | --neow | --shop | --treasure | --event]
//                   [--verbose] [--pool-evidence] [--stop-on-diff]
//                   [--combat] [--combat-summary] [--trace-powers] [--vitals]
//                                                          (--replay triage aids)
//
// SIX MODES, one per acceptance read-out, all over the same artifacts:
//
//   (default)  the combat-reward spot-diff   -- B4.5,  see spot_diff_one below
//   --replay   the whole-run replay          -- diagnosis, see replay_one below
//   --neow     the floor-0 blessing spot-diff -- B4.14, see neow_spot_diff_one
//   --shop     the merchant spot-diff        -- B4.8,  see shop_spot_diff_one
//   --treasure the chest spot-diff           -- B4.7,  see treasure_spot_diff_one
//   --event    the ?-room selection spot-diff -- B4.10/B4.13, see
//              event_spot_diff_one
//
// The five spot-diff modes all SEED the simulator from a translated RunState
// rather than re-driving the run from `run_begin`, which is what makes them
// independent of combat fidelity; only --replay re-drives, and only it needs
// every intervening room to be modelled. That independence is the whole point
// for the two newest: B4.7's chests and B4.13's shrines sit deep in Act 1,
// where a full replay has to survive every intervening fight first.
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
//   NONE    play i [t]      -> PLAY_CARD(i-1, t), except 0 -> hand slot 9
//   NONE    end             -> END_TURN
//   NONE    potion use s t  -> USE_POTION(s, t)
//   (any)   potion discard s-> DISCARD_POTION(s)   screen-independent: the belt
//                                                  is on the top panel
//   CHEST   proceed         -> CHOOSE(kChooseProceed)
//   REST    choose i        -> CHOOSE(i)
//   GRID    choose i        -> buffer the pick     see GridSession
//   GRID    cancel          -> drop the buffer
//   GRID    proceed         -> flush the buffer as CHOOSE(master-deck index)
//   HAND_SELECT choose i    -> CHOOSE(Nth legal full-hand slot)
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
// --vitals (with --replay): THE COMBAT-VITALS COMPARE. The run-level differ
// sees a combat-internal drift -- a different damage number on a monster, a
// Time Warp count one off, a different randomly generated card in hand -- only
// once it becomes a run-level symptom (a monster killed a play early, a
// different draw), records after the cause. `--vitals` compares, at every
// in-combat record, an INDEX-NORMALISED projection of both sides
// (sts/translate/combat_vitals.hpp): turn, player block / energy / powers,
// each monster slot's identity / hp / block / liveness / powers, and every
// pile's CONTENTS as a (card, upgrades) multiset -- draw-pile order is hidden
// by the protocol, so contents are exactly what is comparable there. It is a
// second report beside the run-level one: the CLEAN/PART verdict, the exit
// code and every existing line are unchanged, and its own summary line says
// `vitals-clean` or `vitals-divergent` with the FIRST differing record. The
// first vitals divergence is printed in full as a `VDIFF` block (GAME -> SIM);
// later ones as one line each unless --verbose. Records the run-level compare
// already tolerates as capture races (obtain / Entropic Brew / transform
// preview / Smoke-Bomb escape) and the A20 double-boss handoff are skipped and
// counted rather than compared, because their dump is mid-animation on the
// capture side; a record where the sim has already left the fight is skipped
// and counted too (the run-level DIFF owns that). `--combat` remains the raw
// CombatState walk for diagnosis; the two are independent.
//
// The exit code is the number of files that ended with a divergence or an
// unmapped command (0 == every file replayed clean to its terminal); a vitals
// divergence alone does not change it.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "sts/diff/differ.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/shop.hpp"
#include "sts/engine/treasure_rooms.hpp"
#include "sts/engine/types.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/translate/combat_vitals.hpp"
#include "sts/translate/translate.hpp"

#include "command_map.hpp"
#include "fired_accum.hpp"
#include "mk_board.hpp"

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
        s.gold = gs.value("gold", -1);
        s.room_type = gs.value("room_type", std::string{});
        if (const auto cl = gs.find("choice_list"); cl != gs.end() && cl->is_array()) {
            for (const json& c : *cl)
                s.choice_list.push_back(c.is_string() ? c.get<std::string>()
                                                      : std::string{});
        }
        const auto ss = gs.find("screen_state");
        if (ss != gs.end() && ss->is_object()) {
            if (const auto n = ss->find("next_nodes"); n != ss->end() && n->is_array()) {
                for (const json& node : *n) {
                    s.map_next_x.push_back(node.value("x", -1));
                    s.map_next_symbol.push_back(node.value("symbol", std::string{}));
                }
            }
            s.boss_available = ss->value("boss_available", false);
            if (const auto c = ss->find("cards"); c != ss->end() && c->is_array()) {
                for (const json& card : *c) s.card_offer.push_back(card.value("id", std::string{}));
            }
            if (const auto r = ss->find("rewards"); r != ss->end() && r->is_array()) {
                for (const json& item : *r) {
                    CaptureRewardRow row;
                    row.type = item.value("reward_type", std::string{});
                    row.gold = item.value("gold", 0);
                    if (const auto rl = item.find("relic");
                        rl != item.end() && rl->is_object())
                        row.relic_id = rl->value("id", std::string{});
                    // A SAPPHIRE_KEY row carries its linked base relic under
                    // `link`, which is what proves the pairing rather than
                    // inferring it from adjacency (RewardItem.java:86-93).
                    if (const auto lk = item.find("link");
                        lk != item.end() && lk->is_object())
                        row.link_id = lk->value("id", std::string{});
                    s.reward_types.push_back(row.type);
                    s.reward_rows.push_back(std::move(row));
                }
            }
            s.event_id = ss->value("event_id", std::string{});
            s.event_name = ss->value("event_name", std::string{});
            s.chest_type = ss->value("chest_type", std::string{});
            s.chest_open = ss->value("chest_open", false);
            if (const auto o = ss->find("options"); o != ss->end() && o->is_array()) {
                for (const json& opt : *o) {
                    s.option_labels.push_back(opt.value("label", std::string{}));
                    // A DISABLED button carries no `choice_index`: it occupies a
                    // slot in the sim's option ordinals but none in the
                    // command's index space. Keeping the two lists parallel,
                    // with -1 for such a button, is what lets `map_command`
                    // translate a `choose N` between them -- see
                    // `ScreenInfo::option_choice_index`.
                    s.option_choice_index.push_back(opt.value("choice_index", -1));
                }
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
    // `boss_ids` is COMPARED now: run_begin mirrors the rolled act boss
    // (`boss_list[0]`) into `boss_ids[0]` in the same EncounterId space the
    // translator writes from `act_boss`, so the old neutralization here is
    // gone (it existed only while the run layer had no writer for the field).
    // `keys` -- the OPPOSITE gap: the SIM writes the Ruby bit when a capture's
    // recall press is replayed (RestOptionKind::RECALL), but neither
    // CommunicationMod's game_state nor the fork's oracle block exposes the
    // run's key booleans, so the translator has nothing to write and the
    // capture side is structurally 0. The recall itself IS still validated --
    // the sim must walk past the spent campfire exactly as the capture does,
    // or every later record diverges. The field comes back if the fork ever
    // emits the three Settings.has*Key booleans.
    s.keys = 0;
}

// RunState.boss_chest (schema v8) is the CONDITIONAL version of the `keys`
// shape above, so it takes the PAIR: the capture attests the offers only on a
// BOSS_REWARD dump -- the translator sets `seen` there and nowhere else --
// while the sim rightly holds the entry-popped offers for the whole room
// (BossChest.java:35-39 pops at entry; a skip closes the screen without
// clearing them). On every other record the capture side is structurally
// value-init zero, so the group is neutralized on both sides; on an attested
// record it is compared in full, which is exactly the zero-diff boss-relic
// pick S2-G2 item 2 scores. Gating on the EXPECTED (capture) side keeps a sim
// that wrongly shows the screen from hiding: its own `seen` would then face a
// zeroed expected... which this gate erases too -- but such a sim also
// advertises the wrong legal actions and desyncs on the very next command, so
// the walk itself is the guard there.
void neutralize_unattested_boss_chest(RunState& expected,
                                      RunState& actual) noexcept {
    if (expected.boss_chest.seen == 0) {
        expected.boss_chest = sts::engine::BossChestState{};
        actual.boss_chest = sts::engine::BossChestState{};
    }
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
// into RunState: hp/max_hp live in CombatState and relic counters tick on the
// combat mirror, all settled when the fight ends (run_advance.cpp's
// fold_back_combat). The capture, by contrast, reports the game's live values on
// every in-combat dump, so a mid-combat record can only be compared against the
// folded projection. This applies exactly that projection to a COPY -- it makes
// the comparison STRICTER (the live HP is now checked at every action), it never
// runs on a reward record, and the fields it touches are compared as usual
// everywhere else.
//
// GOLD is deliberately NOT projected any more: since S2.48 the run layer
// charges steals AND banks the Hand-of-Greed payout into RunState.gold at
// every in-combat step boundary (sync_live_gold), so at comparison time the
// unbanked remainder is zero and the purse is already the game's live purse.
// The old `actual.gold += combat_gold` here predated that and double-credited
// exactly the banked amount -- the +40-for-a-+20-kill family the s243_breadth
// campaigns surfaced (STS431475: ColorlessPotion -> Hand of Greed -> kill).
void project_live_combat_sheet(const RunController& rc, RunState& actual) noexcept {
    if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) return;
    actual.hp = rc.combat.player_hp;
    actual.max_hp = rc.combat.player_max_hp;
    const uint8_t n = rc.combat.relic_count < rc.run.relic_count
                          ? rc.combat.relic_count : rc.run.relic_count;
    for (uint8_t i = 0; i < n; ++i) actual.relics[i].counter = rc.combat.relics[i].counter;
}

// --combat-summary: one record's worth of the sim's combat sheet, in the
// capture's vocabulary. `hist[0]` is the monster's decided NEXT move (the
// capture's `move_id`), hist[1..2] its `last_move_id` / `second_last_move_id`.
void print_power_list(const PowerSlot* slots, uint8_t count) {
    std::printf("[");
    for (uint8_t i = 0; i < count; ++i) {
        const std::string id(sts::registry::power_game_id(
            static_cast<sts::registry::PowerId>(slots[i].power_id)));
        std::printf("%s%s:%d", i == 0 ? "" : " ", id.c_str(), slots[i].amount);
    }
    std::printf("]");
}

void print_combat_summary(int seq, const CombatState& c) {
    std::printf("  sim seq=%d turn=%u php=%d blk=%d en=%d fairy=%u pow=", seq,
                static_cast<unsigned>(c.turn), c.player_hp, c.player_block,
                c.player_energy, static_cast<unsigned>(combat_fairy_armed(c.flags)));
    print_power_list(c.player_powers, c.player_power_count);
    std::printf("\n");
    for (uint8_t mi = 0; mi < c.monster_count; ++mi) {
        const MonsterState& mo = c.monsters[mi];
        const std::string id(sts::registry::monster_game_id(
            static_cast<sts::registry::MonsterId>(mo.monster_id)));
        std::printf("      mon %s hp=%d/%d blk=%d hist=[%u,%u,%u] intent=%u pow=",
                    id.c_str(), mo.hp, mo.max_hp, mo.block, mo.move_history[0],
                    mo.move_history[1], mo.move_history[2], mo.intent);
        print_power_list(mo.powers, mo.power_count);
        std::printf("\n");
    }
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

// `phase_name` moved to command_map.hpp when the two UNMAPPED reasons there
// needed the same spelling (see its comment). It is used unqualified below via
// `using namespace sts::replay`.

// --- semantic pile read-out (the --combat diagnosis aid's useful half) --------
//
// `diff_states` compares card_pool BY SLOT, and the capture's translator
// allocates slots in dump order while the engine allocates them in deck order,
// so every in-combat record shows dozens of card_pool[i] / hand[i] index
// differences that mean nothing. What a divergence hunt actually needs is the
// piles BY CONTENT -- "Name+@cost" in pile order (draw: bottom..top, the
// CardGroup.group order on both sides) -- and the cardRandomRng counter,
// printed only when they disagree. The STS228756 Dead Branch hunt (2026-09-02)
// found its first differing record with exactly this print and nothing else.
[[nodiscard]] std::string pile_text(const CombatState& cs, const CardPoolIndex* pile,
                                    uint8_t count) {
    std::string out;
    for (uint8_t i = 0; i < count; ++i) {
        const CardInstance& ci = cs.card_pool[pile[i]];
        if (i != 0) out += ' ';
        out += std::string(sts::registry::card_game_id(static_cast<CardId>(ci.card_id)));
        if (ci.upgrade != 0) out += '+';
        out += '@';
        out += std::to_string(static_cast<int>(ci.cost_now));
    }
    return out;
}

void print_semantic_pile_diff(int seq, const CombatState& cap, const CombatState& sim) {
    struct Row { const char* name; std::string a; std::string b; };
    const Row rows[] = {
        {"hand", pile_text(cap, cap.hand, cap.hand_count),
         pile_text(sim, sim.hand, sim.hand_count)},
        {"draw", pile_text(cap, cap.draw, cap.draw_count),
         pile_text(sim, sim.draw, sim.draw_count)},
        {"discard", pile_text(cap, cap.discard, cap.discard_count),
         pile_text(sim, sim.discard, sim.discard_count)},
        {"exhaust", pile_text(cap, cap.exhaust, cap.exhaust_count),
         pile_text(sim, sim.exhaust, sim.exhaust_count)},
    };
    bool any = cap.card_random_rng.counter != sim.card_random_rng.counter;
    for (const Row& r : rows) any = any || r.a != r.b;
    if (!any) return;
    std::printf("  piles seq=%d: cardRandomRng.counter capture=%d sim=%d\n", seq,
                cap.card_random_rng.counter, sim.card_random_rng.counter);
    for (const Row& r : rows) {
        if (r.a == r.b) continue;
        std::printf("    %-7s capture: %s\n    %-7s sim:     %s\n", r.name, r.a.c_str(),
                    r.name, r.b.c_str());
    }
}

struct Options {
    bool verbose = false;
    bool pool_evidence = false;
    bool stop_on_diff = false;
    bool combat = false;   // also diff the in-combat CombatState (diagnosis aid)
    bool trace_powers = false;  // per-record monster power lists, both sides
    bool combat_summary = false;  // print the sim's combat sheet per in-combat record
    bool vitals = false;   // the index-normalised combat-vitals compare (--replay only)
    bool full_replay = false;  // whole-run replay instead of the reward spot-diff
    bool neow = false;         // the floor-0 blessing spot-diff
    bool shop = false;         // the merchant spot-diff
    bool treasure = false;     // the chest spot-diff
    bool event = false;        // the ?-room selection spot-diff
};

// One file's verdict.
//
// THE STOP IS NOT THE FRONTIER, AND THE SUMMARY MUST SAY SO. `stop_reason`
// answers "why did the replay end", which is a different question from "where
// did the two sides first disagree" -- the replay keeps comparing every record
// after a divergence, on purpose, because the shape of the drift is the
// evidence. When the two answers differ the stop is DOWNSTREAM, and reading it
// as the frontier is a documented, expensive mistake: STS00042 of
// `b45_rewards_oracle_20260727T204809Z_claude01` diverged at seq 18 (Fusion
// Hammer's / Philosopher's Stone's then-deferred `energyMaster` +1 -- SINCE
// LANDED, so that run wants re-running; the lesson here does not depend on it --
// so the sim was an energy short every turn and never finished the fight) and
// stopped
// fourteen records later at seq 32, the first multi-option event page the stuck
// controller was handed. The obligation row was filed off the stop, and asked
// whether the ENGINE had an event/combat-boundary defect. It does not. So the
// first divergence is now carried out of here and printed beside the stop.

// THE OBTAIN RACE, recognized narrowly. `ShowCardAndObtainEffect` adds a
// transformed / obtained card to the master deck only when its ANIMATION
// completes (ShowCardAndObtainEffect.java:30-45,94-108 -- the constructor
// stores the card, `update` obtains it), while the removal is immediate. Every capture
// dump taken in between therefore shows a deck one card SHORT of the state the
// rules describe, and the card appears in the first dump after the effect
// finishes -- in b14_accept's STS00009 that is the first record of the NEXT
// floor, so a mode seeded from the pre-entry record starts a card behind. That
// is the capture-fidelity gap B1.3 deferred and B5.2 carries as an obligation
// row, not an engine defect.
//
// The recognition is deliberately narrow, because a wide one would hide a real
// deck divergence: EVERY differing field must be `master_deck_count` or a
// `master_deck[i]` whose index is at or past the SHORTER deck's END -- i.e. one
// side has strictly more cards and the shared prefix is identical. An
// event-selection defect cannot produce that shape: it would move eventRng, a
// pool bit or event_flags, and any of those makes this return false.
//
// WHICH SIDE IS AHEAD DEPENDS ON THE MODE, so the parameters name the ROLE
// rather than the source, and each caller says which is which.
//
//   --event  seeds the sim from the PRE-ENTRY record, so the sim starts a card
//            behind a capture whose animation has since finished: ahead = the
//            capture.
//   --replay steps the sim command by command and diffs BEFORE applying record
//            k, so the sim has already obtained the card that record k's
//            mid-animation dump does not yet show: ahead = the SIM. Same effect,
//            same narrowness, mirrored -- and it is why the whole-run differ
//            could not simply reuse the --event call and had to be told the
//            direction. (Deferred-obligations row "`--replay` lacks `--event`'s
//            obtain-race recognition".)
// The field-name projection the escape-settlement classifier consumes
// (readout_shapes.hpp keeps the field-set rule JSON-free and unit-tested).
[[nodiscard]] std::vector<std::string> diff_field_names(
    const sts::diff::DiffReport& rep) {
    std::vector<std::string> names;
    names.reserve(rep.diffs.size());
    for (const auto& d : rep.diffs) names.push_back(d.field_name);
    return names;
}

[[nodiscard]] bool is_obtain_race(const sts::diff::DiffReport& rep,
                                  const RunState& ahead,
                                  const RunState& behind) {
    if (rep.empty()) return false;
    if (ahead.master_deck_count <= behind.master_deck_count) return false;
    for (const auto& d : rep.diffs) {
        if (d.field_name == "master_deck_count") continue;
        if (d.field_name.rfind("master_deck[", 0) != 0) return false;
        const std::size_t lb = d.field_name.find('[');
        const std::size_t rb = d.field_name.find(']');
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1)
            return false;
        const int idx = std::atoi(d.field_name.substr(lb + 1, rb - lb - 1).c_str());
        if (idx < static_cast<int>(behind.master_deck_count)) return false;
    }
    return true;
}

[[nodiscard]] bool is_entropic_brew_obtain_race(
    const sts::diff::DiffReport& rep,
    const sts::translate::TranslatedRun& run,
    std::size_t record_index,
    const RunState& actual,
    const RunState& expected) {
    if (record_index == 0 || record_index + 1 >= run.records.size() ||
        !is_potion_obtain_animation_fields(diff_field_names(rep))) {
        return false;
    }
    const std::vector<std::string> use =
        split_ws(run.records[record_index - 1].action_command);
    if (use.size() < 3 || use[0] != "potion" || use[1] != "use") {
        return false;
    }
    const int slot = std::atoi(use[2].c_str());
    if (slot < 0 || slot >= kPotionCap ||
        run.records[record_index - 1].run
                .potions[static_cast<std::size_t>(slot)] !=
            static_cast<uint16_t>(PotionId::ENTROPIC_BREW)) {
        return false;
    }
    // A command typed during the animation normally must not touch the belt.
    // One narrow exception is independently provable: discarding a DIFFERENT
    // occupied slot. STS302912 uses Entropic Brew in slot 1, the next dump
    // catches the generated potion before its ObtainPotionEffect lands, and
    // that dump's command discards slot 0. The following dump must then equal
    // the sim belt exactly except for that one explicitly emptied slot.
    const std::vector<std::string> during =
        split_ws(run.records[record_index].action_command);
    int discarded_slot = -1;
    if (!during.empty() && during[0] == "potion") {
        if (during.size() != 3 || during[1] != "discard") return false;
        discarded_slot = std::atoi(during[2].c_str());
        if (discarded_slot < 0 || discarded_slot >= kPotionCap ||
            actual.potions[static_cast<std::size_t>(discarded_slot)] ==
                static_cast<uint16_t>(PotionId::NONE)) {
            return false;
        }
    }
    const RunState& settled = run.records[record_index + 1].run;
    bool saw_delayed_slot = false;
    for (int i = 0; i < kPotionCap; ++i) {
        const std::size_t p = static_cast<std::size_t>(i);
        if (actual.potions[p] != expected.potions[p]) {
            if (expected.potions[p] !=
                    static_cast<uint16_t>(PotionId::NONE) ||
                actual.potions[p] ==
                    static_cast<uint16_t>(PotionId::NONE)) {
                return false;
            }
            if (i == discarded_slot) return false;
            saw_delayed_slot = true;
        }
        const uint16_t want_settled =
            i == discarded_slot
                ? static_cast<uint16_t>(PotionId::NONE)
                : actual.potions[p];
        if (settled.potions[p] != want_settled) {
            return false;
        }
    }
    return saw_delayed_slot;
}

// The ordinary Smoke-Bomb settlement shape deliberately rejects relic counters:
// a generic counter change can be an unrelated combat defect.  This extension
// admits one only when the capture itself proves the exact sequence whose Java
// onVictory hooks write -1: it spent Smoke Bomb immediately before this row,
// every changed counter is non-negative -> -1, and the next capture row is the
// simulator's whole normalized RunState.  That final equality means a wrong
// reward roll, pool mutation, or counter reset cannot hide behind the one-frame
// animation window.
[[nodiscard]] bool is_escape_settlement_counter_reset_race(
    const sts::diff::DiffReport& rep,
    const sts::translate::TranslatedRun& run,
    std::size_t record_index,
    const RunState& actual,
    const RunState& expected) {
    if (record_index == 0 || record_index + 1 >= run.records.size() ||
        !is_escape_settlement_with_relic_counter_resets(diff_field_names(rep))) {
        return false;
    }
    const std::vector<std::string> use =
        split_ws(run.records[record_index - 1].action_command);
    if (use.size() < 3 || use[0] != "potion" || use[1] != "use") {
        return false;
    }
    const int slot = std::atoi(use[2].c_str());
    if (slot < 0 || slot >= kPotionCap || use[2] != std::to_string(slot) ||
        run.records[record_index - 1].run
                .potions[static_cast<std::size_t>(slot)] !=
            static_cast<uint16_t>(PotionId::SMOKE_BOMB)) {
        return false;
    }

    bool saw_counter = false;
    for (const sts::diff::FieldDiff& d : rep.diffs) {
        if (!is_relic_counter_field(d.field_name)) {
            continue;
        }
        constexpr std::size_t kPrefixSize = sizeof("relics[") - 1;
        const std::size_t close = d.field_name.find(']');
        const int relic_index = std::atoi(
            d.field_name.substr(kPrefixSize, close - kPrefixSize).c_str());
        if (relic_index < 0 ||
            relic_index >= static_cast<int>(expected.relic_count) ||
            relic_index >= static_cast<int>(actual.relic_count) ||
            expected.relics[static_cast<std::size_t>(relic_index)].counter < 0 ||
            actual.relics[static_cast<std::size_t>(relic_index)].counter != -1) {
            return false;
        }
        saw_counter = true;
    }
    if (!saw_counter) {
        return false;
    }

    RunState settled = run.records[record_index + 1].run;
    neutralize_incomparable(settled);
    if (!sts::diff::diff_run_states(settled, actual).empty()) {
        return false;
    }
    // State equality above already entails this, but name the semantic fact so
    // the counter requirement remains local if normalisation ever grows.
    for (const sts::diff::FieldDiff& d : rep.diffs) {
        if (!is_relic_counter_field(d.field_name)) continue;
        constexpr std::size_t kPrefixSize = sizeof("relics[") - 1;
        const std::size_t close = d.field_name.find(']');
        const int relic_index = std::atoi(
            d.field_name.substr(kPrefixSize, close - kPrefixSize).c_str());
        if (settled.relics[static_cast<std::size_t>(relic_index)].counter != -1) {
            return false;
        }
    }
    return true;
}

struct Verdict {
    int records_compared = 0;
    int reward_records_compared = 0;
    int diverged_at = -1;        // first record INDEX with a REAL divergence
    int diverged_seq = -1;       // ...and that record's artifact seq / floor /
    int diverged_floor = -1;     //    screen, which is what a reader quotes
    std::string diverged_screen;
    std::size_t diverged_fields = 0;
    int deck_identity_records = 0;  // records whose only diff was library order
    int obtain_race_records = 0;    // ...whose only diff was an obtain animation
    int escape_race_records = 0;    // ...the Smoke-Bomb escape-settlement race
    int preview_race_records = 0;   // ...a curse transform-preview cardRng burn
    int post_victory_ending_records = 0;  // Spire-Heart cinematic tail skipped
    int double_boss_handoff_records = 0;  // ...compared against the NEXT record
    std::string stop_reason;
    bool clean = false;          // no real divergence anywhere

    // --vitals (combat_vitals.hpp). Its own frontier, never folded into `clean`.
    int vitals_records = 0;            // in-combat records vitals-compared
    int vitals_diverged_records = 0;   // ...of which differed
    int vitals_skipped_race = 0;       // in-combat records skipped: tolerated race / handoff
    int vitals_skipped_phase = 0;      // ...skipped: the sim is already out of the fight
    int vitals_first_seq = -1;         // the FIRST vitals divergence
    int vitals_first_floor = -1;
    int vitals_first_turn = -1;
    std::size_t vitals_first_fields = 0;
    std::set<std::string> vitals_unknown_ids;  // "<domain>:<id>", each reported once
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

// --trace-powers: one line per in-combat record. For each monster RECORD (the
// dump's monsters[] is spawn-order-parallel with the sim's) print
// `<PowerId>:<amount>` for every slot on both sides and mark the record
// MISMATCH when the id -> amount joins differ. A power the translator cannot
// map (PowerId::NONE) prints as `?` and never counts as a mismatch by itself.
[[nodiscard]] std::string power_list_text(const PowerSlot* slots,
                                          uint8_t count) {
    std::string out;
    for (uint8_t i = 0; i < count && i < kPowerCap; ++i) {
        if (!out.empty()) out += ',';
        const auto id = static_cast<PowerId>(slots[i].power_id);
        out += id == PowerId::NONE
                   ? std::string("?")
                   : std::string(sts::registry::power_game_id(id));
        out += ':' + std::to_string(slots[i].amount);
    }
    return out.empty() ? std::string("-") : out;
}

[[nodiscard]] bool power_lists_differ(const PowerSlot* a, uint8_t na,
                                      const PowerSlot* b, uint8_t nb) {
    // Order-insensitive: the game's list is application order with re-sorts,
    // the sim's is its own priority sort; the join is by id.
    const auto amount_of = [](const PowerSlot* s, uint8_t n,
                              uint16_t id) -> int {
        for (uint8_t i = 0; i < n && i < kPowerCap; ++i)
            if (s[i].power_id == id) return s[i].amount;
        return -32768;
    };
    for (uint8_t i = 0; i < na && i < kPowerCap; ++i) {
        if (a[i].power_id == static_cast<uint16_t>(PowerId::NONE)) continue;
        if (amount_of(b, nb, a[i].power_id) != a[i].amount) return true;
    }
    for (uint8_t i = 0; i < nb && i < kPowerCap; ++i) {
        if (b[i].power_id == static_cast<uint16_t>(PowerId::NONE)) continue;
        if (amount_of(a, na, b[i].power_id) != b[i].amount) return true;
    }
    return false;
}

void print_monster_power_trace(const sts::translate::TranslatedRecord& rec,
                               const RunController& rc, const ScreenInfo& s) {
    const CombatState& cap = rec.combat;
    const CombatState& sim = rc.combat;
    const uint8_t n = cap.monster_count < sim.monster_count ? sim.monster_count
                                                            : cap.monster_count;
    bool mismatch = false;
    std::string body;
    for (uint8_t i = 0; i < n && i < kMonsterCap; ++i) {
        const MonsterState* cm = i < cap.monster_count ? &cap.monsters[i] : nullptr;
        const MonsterState* sm = i < sim.monster_count ? &sim.monsters[i] : nullptr;
        if (cm != nullptr && sm != nullptr &&
            power_lists_differ(cm->powers, cm->power_count, sm->powers,
                               sm->power_count)) {
            mismatch = true;
        }
        const auto id = static_cast<MonsterId>(
            sm != nullptr ? sm->monster_id : cm->monster_id);
        body += "\n      m" + std::to_string(i) + " " +
                std::string(sts::registry::monster_game_id(id)) + " game{" +
                (cm != nullptr ? power_list_text(cm->powers, cm->power_count)
                               : std::string("absent")) +
                "} sim{" +
                (sm != nullptr ? power_list_text(sm->powers, sm->power_count)
                               : std::string("absent")) +
                "}";
    }
    std::printf("POWERS%s seq=%d floor=%d cmd='%s' game[turn=%d hand=%d E=%d] "
                "sim[turn=%d hand=%d E=%d]%s\n",
                mismatch ? " MISMATCH" : "", rec.seq, s.floor,
                rec.action_command.c_str(), static_cast<int>(cap.turn),
                static_cast<int>(cap.hand_count),
                static_cast<int>(cap.player_energy), static_cast<int>(sim.turn),
                static_cast<int>(sim.hand_count),
                static_cast<int>(sim.player_energy), body.c_str());
}

[[nodiscard]] Verdict replay_one(const std::string& path, const Options& opts) {
    Verdict v;
    const sts::translate::TranslatedRun run = sts::translate::translate_file(path);
    std::vector<ScreenInfo> screens = read_screens(path);
    // The translator skips a victory artifact's trailing Spire-Heart ending
    // cinematic (translate.hpp's field comment); the screen read-out walks the
    // raw file and still counts those records, so drop the same tail here --
    // it is at the end by construction (the terminal follows it directly).
    if (run.post_victory_ending_records > 0 &&
        screens.size() ==
            run.records.size() +
                static_cast<std::size_t>(run.post_victory_ending_records)) {
        screens.resize(run.records.size());
        v.post_victory_ending_records = run.post_victory_ending_records;
    }
    if (screens.size() != run.records.size())
        throw std::runtime_error("screen/record count mismatch in " + path);

    RunController rc = run_begin(run.seed, 20);
    GridSession grid;
    // Cross-record FIRED accumulation (fired_accum.hpp): the translator's
    // per-record derivation is act-local, so from Act 2 on the expected side
    // must carry the union of everything the capture attested earlier or an
    // Act-1 fire false-REDs every later record. Exact for a floor-1 walk --
    // which --replay structurally is -- and a byte-exact no-op within Act 1.
    sts::replay::FiredAccum fired;
    bool pending_curse_transform_preview = false;

    for (std::size_t k = 0; k < run.records.size(); ++k) {
        const bool curse_transform_preview_window =
            pending_curse_transform_preview;
        pending_curse_transform_preview = false;
        const sts::translate::TranslatedRecord& rec = run.records[k];
        const ScreenInfo& s = screens[k];

        // s2-design §5 trap 5 / S2.43: hand the capture's wall-clock playtime
        // to the ONE rule that reads it, SecretPortal's getShrine gate
        // (AbstractDungeon.java:1929-1933). Without it every Act-3 shrine draw
        // past 800 s replays as the WRONG EVENT -- not because SecretPortal is
        // missed, but because `tmp.get(rng.random(tmp.size() - 1))` (:1937)
        // draws an index into a list one entry too short. The engine never
        // advances this field; a capture that predates the fork's playtime
        // anchor leaves it at the engine's 0.0f, i.e. the old pin.
        //
        // This record's playtime is the value at the state the command is
        // issued FROM, so it lags the roll it feeds by the fraction of a
        // second the room transition takes -- immaterial against an 800 s
        // threshold, and the only alternative (the next record's value) lags
        // in the other direction.
        if (rec.has_playtime) rc.playtime_seconds = rec.playtime;
        const bool is_reward = s.screen_type == "COMBAT_REWARD" ||
                               s.screen_type == "CARD_REWARD";

        RunState expected = rec.run;
        RunState actual = rc.run;
        fired.fold(expected);
        project_live_combat_sheet(rc, actual);
        neutralize_incomparable(expected);
        neutralize_incomparable(actual);
        neutralize_unattested_boss_chest(expected, actual);
        const sts::diff::DiffReport rep = sts::diff::diff_run_states(expected, actual);
        ++v.records_compared;
        if (is_reward) ++v.reward_records_compared;

        std::size_t deck_id_diffs = 0;
        for (const auto& d : rep.diffs)
            if (is_deck_identity_diff(d)) ++deck_id_diffs;
        const bool only_library_order = !rep.empty() && deck_id_diffs == rep.size();
        const bool escape_counter_reset =
            is_escape_settlement_counter_reset_race(rep, run, k, actual, expected);

        // THE A20 DOUBLE-BOSS HANDOFF (command_map.hpp's is_double_boss_handoff
        // carries the derivation). The capture is parked on the first Act-3
        // boss room's bare proceed button; the simulator ran ProceedButton's
        // goToDoubleBoss inline off the boss's death and is already inside the
        // second fight. This record's own state therefore CANNOT be the right
        // comparand -- but the very next one can, and is: it is the capture's
        // own POST-proceed state, i.e. the game's answer to the question the
        // sim has already answered. So the record is not skipped, it is
        // compared SHIFTED, which makes this line a zero-diff assertion rather
        // than an exception. (The ordinary comparison at k+1 then repeats it
        // verbatim -- a NOOP command moves nothing -- so nothing is taken on
        // trust either way.)
        //
        // WHAT THIS CANNOT HIDE, said out loud: the sim's PRE-transition values
        // of the fields the crossing overwrites (a relic counter atBattleStart
        // hard-sets to 0, an hp the +25 Pantograph heal would clamp to max).
        // Those are unobservable by construction rather than excused -- the
        // crossing erases them on both sides, so no later record can depend on
        // them; everything the crossing does NOT write is still compared at
        // full strength, because a field the capture leaves equal across its
        // own two records is compared against a value identical to this
        // record's.
        const bool double_boss_handoff =
            !rep.empty() && is_double_boss_handoff(rc, s) &&
            k + 1 < run.records.size() && screens[k + 1].floor == s.floor + 1;
        sts::diff::DiffReport handoff_rep;
        // Set by every RACE branch below: the capture's dump is mid-animation
        // for this record, so the run-level compare excuses it and the vitals
        // compare must not judge it either.
        bool capture_race = false;
        if (double_boss_handoff) {
            RunState after = run.records[k + 1].run;
            // Fold on a COPY: the real accumulator advances at k+1, in order.
            sts::replay::FiredAccum probe = fired;
            probe.fold(after);
            RunState actual_after = rc.run;
            project_live_combat_sheet(rc, actual_after);
            neutralize_incomparable(after);
            neutralize_incomparable(actual_after);
            neutralize_unattested_boss_chest(after, actual_after);
            handoff_rep = sts::diff::diff_run_states(after, actual_after);
        }

        if (double_boss_handoff) {
            if (handoff_rep.empty()) {
                ++v.double_boss_handoff_records;
                std::printf(
                    "HANDOFF seq=%d floor=%d screen=COMPLETE cmd='%s': the A20 "
                    "double boss -- the capture is holding the first Act-3 boss "
                    "room's proceed button (no reward screen exists, "
                    "AbstractRoom.java:327) while the sim already ran "
                    "goToDoubleBoss (ProceedButton.java:210-220) off the kill; "
                    "compared against the capture's own post-proceed record "
                    "instead, zero-diff\n",
                    rec.seq, s.floor, rec.action_command.c_str());
            } else {
                if (v.diverged_at < 0) {
                    v.diverged_at = static_cast<int>(k);
                    v.diverged_seq = rec.seq;
                    v.diverged_floor = s.floor;
                    v.diverged_screen = s.screen_type;
                    v.diverged_fields = handoff_rep.size();
                }
                std::printf(
                    "DIFF seq=%d floor=%d screen=%s sim_phase=%s cmd='%s' "
                    "(%zu field%s, against the capture's POST-proceed record: "
                    "the A20 double-boss crossing itself)\n",
                    rec.seq, s.floor, s.screen_type.c_str(), phase_name(rc.phase),
                    rec.action_command.c_str(), handoff_rep.size(),
                    handoff_rep.size() == 1 ? "" : "s");
                std::printf("%s\n", handoff_rep.to_string().c_str());
                if (opts.stop_on_diff) {
                    v.stop_reason = "first divergence";
                    return v;
                }
            }
        } else if (only_library_order) {
            ++v.deck_identity_records;
            std::printf("LIBORD seq=%d floor=%d screen=%s cmd='%s': %zu deck identity "
                        "field%s differ; count/upgrade/streams/pity all equal\n",
                        rec.seq, s.floor, s.screen_type.c_str(),
                        rec.action_command.c_str(), rep.size(),
                        rep.size() == 1 ? "" : "s");
            std::printf("%s\n", rep.to_string().c_str());
        } else if (is_obtain_race(rep, /*ahead=*/actual, /*behind=*/expected)) {
            // A ShowCardAndObtainEffect that has not finished animating on the
            // capture side. Reported and counted, never a divergence -- the
            // same call `--event` makes, mirrored (see is_obtain_race).
            ++v.obtain_race_records;
            capture_race = true;
            std::printf("RACE  seq=%d floor=%d screen=%s cmd='%s': the sim's deck holds "
                        "%u card%s this dump does not -- ShowCardAndObtainEffect is "
                        "still animating capture-side (the B1.3/B5.2 obtain-race "
                        "capture gap); the shared prefix is identical\n",
                        rec.seq, s.floor, s.screen_type.c_str(),
                        rec.action_command.c_str(),
                        static_cast<unsigned>(actual.master_deck_count -
                                              expected.master_deck_count),
                        actual.master_deck_count - expected.master_deck_count == 1
                            ? "" : "s");
        } else if (is_entropic_brew_obtain_race(
                       rep, run, k, actual, expected)) {
            // Entropic Brew's out-of-combat ObtainPotionEffects have advanced
            // potionRng but have not inserted their potion rows yet. The
            // classifier requires the very next capture record to hold exactly
            // these simulator identities, so a wrong roll cannot hide here.
            ++v.obtain_race_records;
            capture_race = true;
            std::printf(
                "RACE  seq=%d floor=%d screen=%s cmd='%s': the sim's potion "
                "belt already holds %zu delayed Entropic-Brew obtain%s that "
                "this dump catches mid-ObtainPotionEffect animation; the next "
                "capture record contains exactly those identities\n",
                rec.seq, s.floor, s.screen_type.c_str(),
                rec.action_command.c_str(), rep.size(),
                rep.size() == 1 ? "" : "s");
        } else if (curse_transform_preview_window &&
                   is_transform_preview_rng_advance(
                       diff_field_names(rep), actual.card_rng,
                       expected.card_rng)) {
            ++v.preview_race_records;
            capture_race = true;
            std::printf(
                "RACE  seq=%d floor=%d screen=%s cmd='%s': the capture spent "
                "%d wall-clock cardRng draws in a CURSE transform-confirm "
                "preview; its endpoint is the exact result of that many "
                "CardLibrary.getCurse rolls from the sim stream\n",
                rec.seq, s.floor, s.screen_type.c_str(),
                rec.action_command.c_str(),
                expected.card_rng.counter - actual.card_rng.counter);
            // The UI-only draws still affect later gameplay rolls in the live
            // run. Carry their proven endpoint forward in the REPLAY HARNESS;
            // the headless engine itself remains independent of wall-clock UI.
            rc.run.card_rng = expected.card_rng;
        } else if (!rep.empty() && s.screen_type == "NONE" &&
                   rc.phase == static_cast<uint8_t>(RunPhase::COMBAT_REWARD) &&
                   (rc.combat.flags & kCombatFlagPlayerEscaped) != 0u &&
                   (is_escape_settlement_fields(diff_field_names(rep)) ||
                    escape_counter_reset)) {
            // The Smoke-Bomb escape-settlement race (readout_shapes.hpp): the
            // capture's dump is inside the escape animation, still listing the
            // fight, while the sim settled the escape synchronously on the
            // potion use. A slow animation can yield several consecutive
            // records (STS300133 yields four); every one must independently
            // satisfy the narrow screen/phase/field-set gates, and the first
            // settled capture record stops matching them.
            ++v.escape_race_records;
            capture_race = true;
            std::printf("RACE  seq=%d floor=%d screen=%s cmd='%s': the sim settled a "
                        "Smoke-Bomb escape (victory heal + battle-over assembly) that "
                        "this dump catches mid-escape-animation "
                        "(AbstractPlayer.updateEscapeAnimation -> endBattle, "
                        "AbstractPlayer.java:2281-2292); %zu field%s, all in the "
                        "settlement set%s\n",
                        rec.seq, s.floor, s.screen_type.c_str(),
                        rec.action_command.c_str(), rep.size(),
                        rep.size() == 1 ? "" : "s",
                        escape_counter_reset
                            ? " (proved onVictory relic-counter reset + next-record reconvergence)"
                            : "");
        } else if (!rep.empty()) {
            if (v.diverged_at < 0) {
                v.diverged_at = static_cast<int>(k);
                v.diverged_seq = rec.seq;
                v.diverged_floor = s.floor;
                v.diverged_screen = s.screen_type;
                v.diverged_fields = rep.size();
            }
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

        // --vitals: the index-normalised combat-vitals compare
        // (sts/translate/combat_vitals.hpp; the file header has the contract).
        // Both sides are the state BEFORE this record's command -- the dump by
        // construction, the sim because the command is applied below -- so the
        // comparison is like-for-like in time as well as in shape. A record the
        // run-level compare excused as a capture race, or compared SHIFTED as
        // the double-boss handoff, is skipped and counted: its dump is not the
        // state the rules describe. A record where the sim has already left
        // the fight (the run-level DIFF is what owns that) is skipped and
        // counted separately.
        if (opts.vitals && rec.in_combat) {
            if (capture_race || double_boss_handoff) {
                ++v.vitals_skipped_race;
            } else if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) {
                ++v.vitals_skipped_phase;
            } else {
                const sts::translate::CombatVitals sim_vitals =
                    sts::translate::vitals_from_combat_state(rc.combat);
                const sts::translate::VitalsReport vrep =
                    sts::translate::diff_combat_vitals(rec.vitals, sim_vitals);
                ++v.vitals_records;
                // An unresolved id is named ONCE per file, not dropped: its
                // slot is compared under its raw name and cannot match, so the
                // rows below will carry it too -- this line says why.
                for (const std::string& u : vrep.unknown_ids) {
                    if (v.vitals_unknown_ids.insert(u).second) {
                        std::printf("VITALS seq=%d floor=%d: unresolved id %s -- the "
                                    "registry has no row for it; the slot is compared "
                                    "under its raw name and cannot match\n",
                                    rec.seq, s.floor, u.c_str());
                    }
                }
                if (!vrep.empty()) {
                    const bool first = v.vitals_diverged_records == 0;
                    ++v.vitals_diverged_records;
                    if (first) {
                        v.vitals_first_seq = rec.seq;
                        v.vitals_first_floor = s.floor;
                        v.vitals_first_turn = rec.vitals.turn;
                        v.vitals_first_fields = vrep.size();
                    }
                    if (first || opts.verbose) {
                        std::printf("VDIFF seq=%d floor=%d turn=%d screen=%s cmd='%s' "
                                    "(%zu field%s%s)\n%s\n",
                                    rec.seq, s.floor, rec.vitals.turn,
                                    s.screen_type.c_str(), rec.action_command.c_str(),
                                    vrep.size(), vrep.size() == 1 ? "" : "s",
                                    first ? "; the FIRST vitals divergence" : "",
                                    vrep.to_string().c_str());
                    } else {
                        std::printf("VDIFF seq=%d floor=%d turn=%d cmd='%s' (%zu field%s; "
                                    "--verbose prints the rows)\n",
                                    rec.seq, s.floor, rec.vitals.turn,
                                    rec.action_command.c_str(), vrep.size(),
                                    vrep.size() == 1 ? "" : "s");
                    }
                }
            }
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
            print_semantic_pile_diff(rec.seq, rec.combat, rc.combat);
        }

        // Triage print (the S2.V3 Time Eater stops): the monster power lists,
        // capture beside sim, joined by power id. Not a pass/fail signal --
        // the translator maps only the ids the registry knows -- but a
        // per-record view of a counter the run-level differ never reads.
        if (opts.trace_powers && rec.in_combat &&
            rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            print_monster_power_trace(rec, rc, s);
        }

        // The second diagnosis aid, --combat-summary: the SIM's own in-combat
        // sheet at every in-combat record, in the capture's vocabulary (game
        // ids, hp/block/energy, powers, each monster's move history and
        // intent) so it can be read side by side with the artifact's
        // `combat_state` dump. --combat's field diff is index-normalised and
        // does not translate monster powers, which is exactly what makes the
        // first drifting HIT hard to see in it; this print is what found the
        // play-time damage lock (S2.V3, four captures whose run-level compare
        // stayed clean until the fight ended, because monster HP is not a
        // RunState field). Triage only, never a pass/fail signal.
        if (opts.combat_summary && rec.in_combat &&
            rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            print_combat_summary(rec.seq, rc.combat);
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
                const bool closes =
                    k + 1 >= screens.size() ||
                    screens[k + 1].screen_type != "GRID";
                if (closes) {
                    RunActionMask mask{};
                    legal_actions(rc, mask);
                    if (mask.can_cancel_grid) {
                        step(rc, make_action(ActionVerb::CHOOSE,
                                             kChooseCancelGrid));
                    }
                }
                continue;
            }
            if (m.kind == MapKind::GRID_PICK) {
                toggle_grid_pick(grid, m.grid_index);
            }
            // A two-pick grid has no confirm button: it commits on the pick that
            // fills it, and the only evidence of that is the screen being gone
            // from the next record. Flush on either signal.
            const bool closes =
                k + 1 >= screens.size() || screens[k + 1].screen_type != "GRID";
            if (m.kind != MapKind::GRID_COMMIT && !closes) continue;
            bool committed_curse_transform = false;
            if (rc.phase == static_cast<uint8_t>(RunPhase::EVENT_DIALOG) &&
                rc.event.grid_kind ==
                    static_cast<uint8_t>(EventGridKind::TRANSFORMABLE)) {
                for (const int g : grid.pending) {
                    if (g < 0 ||
                        g >= static_cast<int>(grid.filtered.size())) {
                        continue;
                    }
                    const int deck_index =
                        grid.filtered[static_cast<std::size_t>(g)];
                    if (deck_index < 0 ||
                        deck_index >=
                            static_cast<int>(rc.run.master_deck_count)) {
                        continue;
                    }
                    const CardId id = static_cast<CardId>(
                        rc.run.master_deck[static_cast<std::size_t>(
                            deck_index)].card_id);
                    const CardDef* def = card_def(id);
                    committed_curse_transform =
                        committed_curse_transform ||
                        (def != nullptr && def->type == CardType::CURSE);
                }
            }
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
            if (m.kind == MapKind::GRID_COMMIT &&
                grid.pending.empty() &&
                sim_choice_free_confirmation_grid(rc)) {
                step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
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
            pending_curse_transform_preview = committed_curse_transform;
            grid = GridSession{};
            continue;
        }
        // The map can be up OVER a room the capture has not formally left --
        // that is the whole of the "leaving is deferred to the map choice"
        // convention. Three phases park that way: COMBAT_REWARD, SHOP, and an
        // unopened TREASURE_ROOM chest. Their `proceed` opens a dismissable map
        // overlay capture-side and is NOOPped in the table; the actual node
        // choice is the irreversible exit. Without this the node CHOOSE would
        // land on the still-mounted reward/shop/chest screen rather than move
        // the run.
        if (m.kind == MapKind::LEAVE_ROOM &&
            (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT_REWARD) ||
             rc.phase == static_cast<uint8_t>(RunPhase::SHOP) ||
             rc.phase == static_cast<uint8_t>(RunPhase::TREASURE_ROOM))) {
            step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
        }
        if (m.kind == MapKind::LEAVE_ROOM &&
            rc.phase == static_cast<uint8_t>(RunPhase::NEOW) &&
            rc.neow.screen ==
                static_cast<uint8_t>(NeowScreen::ITEM_REWARD)) {
            step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
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
    int theft_seeded = 0;  // screens whose STOLEN_GOLD amount came from the capture
    int failures = 0;
};

// --- the one input this mode cannot derive: a killed thief's returned gold ---
//
// A KNOWN-BENIGN SHAPE, named here rather than left to surface as an
// unexplained diff. The engine models the row fully: `settle_stolen_gold`
// (run_advance.cpp:329-393) reads `looter_stolen_gold(ms)` off the
// **MonsterState** -- the Looter's steal count lives in `MonsterState.pad0`
// (monster_looter.hpp:92-103) -- and hands the dead thieves' share to
// `assemble_combat_rewards`, which puts a STOLEN_GOLD item AHEAD of every
// battle-over item (combat_rewards.cpp:276-279) where it also counts toward the
// >= 4 potion-suppression threshold.
//
// This mode seeds a translated **RunState** and re-drives nothing, on purpose
// (that independence from combat fidelity is the whole point of the mode), and
// the accumulator is combat state the capture does not carry: CommunicationMod
// publishes no per-monster steal count, so the translated `MonsterState.pad0`
// is 0 no matter what the thief did. Assembling with 0 dropped the row
// entirely, which read as `reward items: [STOLEN_GOLD,GOLD,...] -> [GOLD,...]`
// and then, downstream, as a claim exactly the stolen amount short -- three
// such failures in the G6 campaign, every one of them 60 gold (STS00462 f7,
// STS00683 f5, STS01314 f7).
//
// So the amount is taken from the capture's own reward row, exactly as this
// mode already takes its RunState, its miscRng and its room type from the
// capture. What that seeds is ONE number; what it leaves proved is everything
// downstream of it -- the row's POSITION in the list, its effect on the potion
// threshold, the rest of the assembly's stream draws, and the claim, which is
// where the sim's gold has to land on the capture's to the unit. A screen that
// needed the seed is counted and printed separately so a read-out line never
// implies the theft itself was reproduced.
[[nodiscard]] int32_t captured_stolen_gold(const std::vector<CaptureRewardRow>& rows) {
    int32_t total = 0;
    for (const CaptureRewardRow& r : rows)
        if (r.type == "STOLEN_GOLD") total += r.gold;
    return total;
}

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
        // The one seeded input; see `captured_stolen_gold`. It is already
        // deducted from `rs.gold`, which is what the parameter's contract
        // requires (combat_rewards.hpp:286-291): the game deducts at steal time
        // (DamageAction.stealGold), so the captured pre-battle-over purse this
        // mode seeds from already reflects every steal.
        const int32_t stolen = captured_stolen_gold(open.reward_rows);
        if (stolen > 0) ++v.theft_seeded;
        assemble_combat_rewards(rs, misc, room_type_from_capture(open.room_type),
                                RewardOutcome::KILLED, screen, stolen);

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
            std::printf("]");
            if (stolen > 0)
                std::printf(" (STOLEN_GOLD %d seeded from the capture: the "
                            "thief's accrual is MonsterState, which this mode "
                            "does not re-drive)", stolen);
            std::printf("\n");
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
            neutralize_unattested_boss_chest(expected, actual);
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
// ACTIVATION is a checkpoint of its own because every boss relic mutates state
// before its follow-on screen resolves. All five S1 boss-relic onEquip bodies
// and their grids/reward screens are live; a stop before POST-CHOICE is now an
// upstream state or command-shape divergence, not a deferred Neow body.

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
                // payout handed over. Every S1 boss-relic grid is live, so
                // reaching this stop means the sim diverged before the grid.
                if (rc.neow.screen != static_cast<uint8_t>(NeowScreen::GRID)) {
                    std::string who = "?";
                    if (rc.run.relic_count > 0)
                        who = std::string(sts::registry::relic_game_id(
                            static_cast<RelicId>(
                                rc.run.relics[rc.run.relic_count - 1].relic_id)));
                    v.stop_reason =
                        "the capture opens a grid the blessing did not: " + who +
                        "'s S1 onEquip grid is implemented, so the sim "
                        "diverged earlier";
                    return v;
                }
                open_grid_session(rc, grid);
            }
            if (c[0] == "cancel") {
                grid.pending.clear();
                continue;
            }
            if (c[0] == "choose" && c.size() >= 2) {
                toggle_grid_pick(grid, std::stoi(c[1]));
            }
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
            if (c[0] == "proceed" && grid.pending.empty() &&
                sim_choice_free_confirmation_grid(rc)) {
                step(rc, make_action(ActionVerb::CHOOSE, kChooseProceed));
            }
            if (rc.neow.screen == static_cast<uint8_t>(NeowScreen::GRID)) {
                // The sim still wants picks the capture did not make: the grid
                // shape or an earlier state differs; no S1 Neow onEquip body
                // remains deferred.
                v.stop_reason = "sim's Neow grid outlived the capture's picks "
                                "(selection/card identity or earlier state "
                                "diverged; every S1 Neow grid is implemented)";
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

// `lower`, `ShopPick`/`ShopTarget`, `resolve_shop_choice` and
// `shop_choice_arg_to_index` MOVED to command_map.hpp when `--replay` grew its
// own SHOP_ROOM / SHOP_SCREEN arm:
// both modes resolve a `choose i` on a merchant's shelf the same way (the
// game lists the AFFORDABLE unsold rows by display name, so the index means
// nothing without the choice_list), and two copies of that rule is exactly the
// shape the mapping table was split out to prevent.

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
            neutralize_unattested_boss_chest(e, a);
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
                if (c[0] == "choose" && c.size() >= 2) {
                    toggle_grid_pick(grid, std::stoi(c[1]));
                }
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
            // `state` is the protocol's pure no-op (a forced dump; changes
            // nothing). Scripted captures append them so every post-purchase
            // state lands in an ordinary action record -- the record was
            // already compared above, so the command itself is elided.
            if (c[0] == "state") continue;
            if (screens[j].screen_type != "SHOP_SCREEN" || c[0] != "choose") {
                stop = "seq " + std::to_string(run.records[j].seq) + " cmd '" +
                       run.records[j].action_command +
                       "' is not a merchant action and has no run-layer analogue";
                break;
            }
            const ShopTarget t = resolve_shop_choice(
                screens[j], shop_choice_arg_to_index(screens[j], c),
                screens[j].gold >= 0 ? screens[j].gold : buy.gold);
            const int ordinal = shop_target_ordinal(live, t);
            bool applied = false;
            if (ordinal >= kChooseShopColoredBase &&
                ordinal < kChooseShopColorlessBase) {
                applied = shop_buy_card(
                    buy, live,
                    static_cast<uint8_t>(ordinal - kChooseShopColoredBase),
                    /*colorless=*/false);
            } else if (ordinal >= kChooseShopColorlessBase &&
                       ordinal < kChooseShopRelicBase) {
                applied = shop_buy_card(
                    buy, live,
                    static_cast<uint8_t>(ordinal - kChooseShopColorlessBase),
                    /*colorless=*/true);
            } else if (ordinal >= kChooseShopRelicBase &&
                       ordinal < kChooseShopPotionBase) {
                applied = shop_buy_relic(
                    buy, misc, live,
                    static_cast<uint8_t>(ordinal - kChooseShopRelicBase));
            } else if (ordinal >= kChooseShopPotionBase &&
                       ordinal < kChooseShopPurge) {
                applied = shop_buy_potion(
                    buy, live,
                    static_cast<uint8_t>(ordinal - kChooseShopPotionBase));
            } else if (ordinal == kChooseShopPurge) {
                applied = shop_purge_legal(
                    buy, live);  // opens the grid, spends nothing
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

// --- the chest spot-diff (--treasure) ----------------------------------------
//
// A chest is a pure function of the state the room entry sees, exactly like a
// merchant, so this mode seeds instead of replaying. Per treasure ROOM:
//
//   CONSTRUCTION  seed a RunState from the last record BEFORE the map choice
//                 that entered the room and call `roll_treasure_chest`. That is
//                 TreasureRoom.onPlayerEntry -> getRandomChest -> the chest
//                 constructor, whose only cost is TWO treasureRng wrapper calls
//                 (the size roll, then randomizeReward's one shared contents
//                 roll -- AbstractDungeon.java:499-508, AbstractChest.java:
//                 54-60). Compared against the first in-room record: treasureRng
//                 exactly, every OTHER run stream unmoved, the relic pools
//                 untouched, and the resulting size against the capture's own
//                 `screen_state.chest_type`.
//   OPEN          from the CHEST record's RunState, `open_treasure_chest`. The
//                 whole RunState is diffed against the post-open record -- which
//                 is where the optional gold draw (+1 treasureRng iff the chest
//                 rolled gold, AbstractChest.java:72) and the relic pool's
//                 front-pop are proved -- and the assembled reward rows are
//                 compared against the captured screen.
//   CLAIM         a RunController parked in COMBAT_REWARD with the captured
//                 post-open RunState and the assembled screen, driven through
//                 the capture's own claim commands and diffed whole against
//                 every later in-room record.
//
// THE KEY ROW. Every Act-1 chest open in a capture carries one extra trailing
// SAPPHIRE_KEY row the engine deliberately does not model; the rule for when
// that is expected, and the rule for what claiming it MEANS, both live in
// `readout_shapes.hpp` where they have their own gtest. This file only applies
// them.

// The map symbol of the node a pre-entry record's command stepped ONTO, or ""
// when that record is not a map `choose`. Both `--treasure` and `--event` need
// it, because `room_type` alone cannot tell a room reached from a `T`/`$`/`M`
// node apart from the same room reached through a `?`: the ? path fires
// AbstractRelic.onEnterRoom against the ORIGINAL EventRoom and spends the one
// committed eventRng float before the room content runs at all
// (AbstractDungeon.nextRoomTransition, AbstractDungeon.java:1754-1779), and the
// direct path does neither. STS00052's floor-5 chest is exactly this case --
// `next_nodes[0].symbol == "?"` -- and a read-out that assumed a treasure NODE
// reported the missing eventRng draw as an engine divergence.
[[nodiscard]] std::string entered_node_symbol(const ScreenInfo& s,
                                              const std::string& cmd) {
    if (s.screen_type != "MAP") return {};
    const std::vector<std::string> p = split_ws(cmd);
    if (p.size() < 2 || p[0] != "choose") return {};
    const int i = std::stoi(p[1]);
    if (i < 0 || i >= static_cast<int>(s.map_next_symbol.size())) return {};
    return s.map_next_symbol[static_cast<std::size_t>(i)];
}

[[nodiscard]] const char* event_roll_name(EventRoomResult r) noexcept {
    switch (r) {
        case EventRoomResult::EVENT: return "EVENT";
        case EventRoomResult::MONSTER: return "MONSTER";
        case EventRoomResult::SHOP: return "SHOP";
        case EventRoomResult::TREASURE: return "TREASURE";
        case EventRoomResult::ELITE: return "ELITE";
    }
    return "?";
}

[[nodiscard]] const char* chest_size_name(uint8_t s) noexcept {
    switch (static_cast<ChestSize>(s)) {
        case ChestSize::NONE: return "NONE";
        case ChestSize::SMALL: return "SmallChest";
        case ChestSize::MEDIUM: return "MediumChest";
        case ChestSize::LARGE: return "LargeChest";
    }
    return "?";
}

// Every member named and NO `default:`, so a tier added to the registry is a
// -Wswitch error here rather than a silent "?" (conventions §8). A chest's tier
// is only ever COMMON/UNCOMMON/RARE (`treasure_chest_for_rolls`); the rest exist
// so an out-of-domain descriptor prints as itself instead of as an integer.
[[nodiscard]] const char* relic_tier_name(uint8_t t) noexcept {
    switch (static_cast<RelicTier>(t)) {
        case RelicTier::STARTER: return "STARTER";
        case RelicTier::COMMON: return "COMMON";
        case RelicTier::UNCOMMON: return "UNCOMMON";
        case RelicTier::RARE: return "RARE";
        case RelicTier::BOSS: return "BOSS";
        case RelicTier::SHOP: return "SHOP";
        case RelicTier::SPECIAL: return "SPECIAL";
        case RelicTier::EVENT: return "EVENT";
    }
    return "?";
}

// The engine models no key row, so when a capture claims the KEY the linked
// base relic is abandoned (RewardItem.java:317-322) and BOTH rows leave the
// captured screen. The simulator's screen has only the relic row, and the run
// layer has no verb for "abandon this one item" -- proceed abandons the whole
// screen. Dropping the row here keeps the two index spaces aligned for any
// later claim on the same screen, and it is the tool's job rather than the
// engine's precisely because the key is what caused it.
void drop_reward_row(RewardScreen& s, uint8_t index) noexcept {
    if (index >= s.count) return;
    for (uint8_t i = index; i + 1 < s.count; ++i) s.items[i] = s.items[i + 1];
    s.items[s.count - 1] = RunRewardItem{};
    --s.count;
    s.open_card_item = kNoOpenCardReward;
}

// Does the run hold a N'loth's Mask with a live charge? That is the one thing
// that legitimately deletes the base relic row and its linked key row
// (NlothsMask.java:23-32 -> AbstractRoom.java:549-559). `counter > 0` is the
// relic's own gate.
[[nodiscard]] bool nloths_mask_armed(const RunState& rs) noexcept {
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id ==
                static_cast<uint16_t>(RelicId::NLOTHS_MASK) &&
            rs.relics[i].counter > 0)
            return true;
    }
    return false;
}

struct TreasureVerdict {
    int rooms = 0;
    int construction_clean = 0;
    int opens = 0;
    int open_clean = 0;
    int skips = 0;
    int walk_clean = 0;
    int walk_partial = 0;
    int failures = 0;
};

[[nodiscard]] TreasureVerdict treasure_spot_diff_one(const std::string& path,
                                                     const Options& opts) {
    TreasureVerdict v;
    const sts::translate::TranslatedRun run = sts::translate::translate_file(path);
    const std::vector<ScreenInfo> screens = read_screens(path);
    if (screens.size() != run.records.size())
        throw std::runtime_error("screen/record count mismatch in " + path);

    // `Settings.hasSapphireKey` is run-scoped and starts false
    // (CardCrawlGame.java:473). The only way a capture sets it is claiming a
    // SAPPHIRE_KEY reward row, so track that as the walk goes.
    bool has_key = false;

    for (std::size_t k = 0; k < screens.size(); ++k) {
        if (screens[k].room_type != "TreasureRoom") continue;
        if (k > 0 && screens[k - 1].room_type == "TreasureRoom" &&
            screens[k - 1].floor == screens[k].floor)
            continue;
        if (k == 0) continue;  // no pre-entry record to seed from
        ++v.rooms;
        const int floor = screens[k].floor;

        // 1. CONSTRUCTION, off the pre-entry state.
        RunState rs = run.records[k - 1].run;
        // ++floorNum happens on the transition, BEFORE onPlayerEntry and before
        // the ?-roll (AbstractDungeon.java:1741; trap 7 -- floor++ precedes the
        // floor-stream reseed). The pre-entry record is still on the old floor.
        ++rs.floor;
        const RngStream before = rs.treasure_rng;
        std::vector<std::string> fail;

        // A chest reached through a `?` node runs the ?-room entry FIRST: the
        // onEnterRoom fan-out against the original EventRoom, then the one
        // committed eventRng draw, and only then the recursion into a real
        // TreasureRoom (run_advance.cpp's EventRoomResult::TREASURE arm --
        // "byte-identical to a map treasure node" from that point on).
        const std::string symbol =
            entered_node_symbol(screens[k - 1], run.records[k - 1].action_command);
        const bool via_question = symbol == "?";
        if (via_question) {
            dispatch_on_enter_room_relics(rs, RoomType::Event);
            const EventRoomResult roll =
                event_room_roll(rs, screens[k - 1].room_type == "ShopRoom");
            if (roll != EventRoomResult::TREASURE)
                fail.push_back("the ? node's roll resolved to " +
                               std::string(event_roll_name(roll)) +
                               " but the capture entered a TreasureRoom");
        }
        const TreasureChest chest = roll_treasure_chest(rs);
        const RunState& entered = run.records[k].run;
        auto stream = [&](const char* name, const RngStream& e, const RngStream& a) {
            if (e.s0 == a.s0 && e.s1 == a.s1 && e.counter == a.counter) return;
            fail.push_back(std::string(name) + ": counter " +
                           std::to_string(e.counter) + " -> " +
                           std::to_string(a.counter) +
                           (e.s0 == a.s0 && e.s1 == a.s1 ? "" : " (raw state too)"));
        };
        stream("treasure_rng", entered.treasure_rng, rs.treasure_rng);
        // The construction is treasureRng-only: nothing else may move
        // (treasure_rooms.hpp -- the relic identity is a POOL front-pop at open
        // time, so even relicRng stays put).
        stream("monster_rng", entered.monster_rng, rs.monster_rng);
        stream("event_rng", entered.event_rng, rs.event_rng);
        stream("merchant_rng", entered.merchant_rng, rs.merchant_rng);
        stream("card_rng", entered.card_rng, rs.card_rng);
        stream("relic_rng", entered.relic_rng, rs.relic_rng);
        stream("potion_rng", entered.potion_rng, rs.potion_rng);
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
        // The chest the capture actually shows.
        std::string capture_size;
        for (std::size_t j = k; j < screens.size(); ++j) {
            if (screens[j].floor != floor || screens[j].room_type != "TreasureRoom")
                break;
            if (screens[j].chest_type.empty()) continue;
            capture_size = screens[j].chest_type;
            break;
        }
        if (capture_size.empty()) {
            fail.push_back("the capture never shows a CHEST screen in this room");
        } else if (capture_size != chest_size_name(chest.size)) {
            fail.push_back("chest size: " + capture_size + " -> " +
                           std::string(chest_size_name(chest.size)));
        }

        if (fail.empty()) {
            ++v.construction_clean;
            const std::string route =
                via_question ? std::string("a ? node (eventRng +1 first)")
                             : "a '" + (symbol.empty() ? std::string("unknown")
                                                       : symbol) + "' node";
            std::printf("CHEST    OK   %s floor=%d %s tier=%s gold=%s treasureRng "
                        "%u->%u entered via %s\n",
                        run.seed_string.c_str(), floor, chest_size_name(chest.size),
                        relic_tier_name(chest.relic_tier),
                        chest.has_gold ? "yes" : "no", before.counter,
                        rs.treasure_rng.counter, route.c_str());
        } else {
            ++v.failures;
            std::printf("CHEST    DIFF %s floor=%d\n", run.seed_string.c_str(), floor);
            for (const auto& f : fail) std::printf("    %s\n", f.c_str());
        }

        // 2 + 3. The in-room walk: the open, then the claims.
        RunState cur = run.records[k].run;
        TreasureChest live = chest;
        RewardScreen screen{};
        screen.open_card_item = kNoOpenCardReward;
        bool opened = false;
        int key_index = -1;  // in the CURRENT captured screen's index space
        std::string stop;
        int compared = 0;

        for (std::size_t j = k; j < screens.size(); ++j) {
            if (screens[j].floor != floor || screens[j].room_type != "TreasureRoom")
                break;
            const std::vector<std::string> c = split_ws(run.records[j].action_command);
            if (c.empty()) break;
            if (screens[j].screen_type == "MAP" && c[0] == "choose") break;

            // Compare the whole RunState at every in-room record.
            RunState e = run.records[j].run;
            RunState a = cur;
            neutralize_presentation_only(e);
            neutralize_presentation_only(a);
            e.neow_rng = RngStream{};  // floor-0 only; not carried here
            a.neow_rng = RngStream{};
            const sts::diff::DiffReport rep = sts::diff::diff_run_states(e, a);
            ++compared;
            if (!rep.empty()) {
                ++v.failures;
                std::printf("WALK     DIFF %s floor=%d seq=%d (%zu field%s)\n%s\n",
                            run.seed_string.c_str(), floor, run.records[j].seq,
                            rep.size(), rep.size() == 1 ? "" : "s",
                            rep.to_string().c_str());
                stop = "divergence";
                break;
            }

            // The belt is drawn over a treasure room too (same rationale as the
            // merchant walk's).
            if (c[0] == "potion" && c.size() >= 3 && c[1] == "discard") {
                const int slot = std::stoi(c[2]);
                if (slot < 0 || slot >= kPotionCap ||
                    cur.potions[static_cast<std::size_t>(slot)] ==
                        static_cast<uint16_t>(PotionId::NONE)) {
                    stop = "seq " + std::to_string(run.records[j].seq) +
                           " discards an empty potion slot";
                    break;
                }
                cur.potions[static_cast<std::size_t>(slot)] =
                    static_cast<uint16_t>(PotionId::NONE);
                continue;
            }

            if (screens[j].screen_type == "CHEST") {
                // `proceed` at a chest is the SKIP, and it bounces exactly like
                // a reward screen's (AbstractEvent.openMap leaves the room
                // mounted behind a dismissable map -- the header note). Skipping
                // consumes no RNG and touches no RunState field
                // (run_advance.cpp's TREASURE_ROOM proceed arm), so eliding the
                // bounce is sound and every intervening record is still
                // compared above.
                if (c[0] != "choose") continue;
                if (opened) {
                    stop = "seq " + std::to_string(run.records[j].seq) +
                           ": the capture opens the chest twice";
                    break;
                }
                ++v.opens;
                std::vector<std::string> ofail;
                RunState post = cur;
                if (!treasure_chest_open_legal(post, live)) {
                    ofail.push_back("treasure_chest_open_legal refused the "
                                    "captured chest descriptor");
                } else if (!open_treasure_chest(post, live, screen)) {
                    ofail.push_back("open_treasure_chest refused (capacity "
                                    "preflight)");
                }
                opened = true;

                // The post-open record, and the reward screen it shows.
                std::size_t oj = j + 1;
                if (oj >= screens.size()) {
                    stop = "the artifact ends at the chest open";
                    cur = post;
                    break;
                }
                RunState oe = run.records[oj].run;
                RunState oa = post;
                neutralize_presentation_only(oe);
                neutralize_presentation_only(oa);
                oe.neow_rng = RngStream{};
                oa.neow_rng = RngStream{};
                const sts::diff::DiffReport orep = sts::diff::diff_run_states(oe, oa);
                if (!orep.empty())
                    ofail.push_back("post-open RunState (" +
                                    std::to_string(orep.size()) + " field(s)):\n" +
                                    orep.to_string());

                // The reward rows, with the chest-linked key row accounted for.
                // Taken from the first in-room COMBAT_REWARD screen rather than
                // assumed to be the very next record: `AbstractChest.open` ends
                // with `combatRewardScreen.open()`, but the capture's next dump
                // is whatever screen the policy was looking at.
                std::size_t rj = oj;
                for (; rj < screens.size(); ++rj) {
                    if (screens[rj].floor != floor ||
                        screens[rj].room_type != "TreasureRoom") {
                        rj = screens.size();
                        break;
                    }
                    if (screens[rj].screen_type == "COMBAT_REWARD") break;
                }
                KeyRowContext ctx;
                ctx.chest_open = true;
                ctx.already_has_key = has_key;
                ctx.nloths_mask_fired = nloths_mask_armed(cur);
                KeyRowVerdict kv;
                if (rj >= screens.size()) {
                    ofail.push_back("the capture never shows the reward screen "
                                    "AbstractChest.open opened");
                } else {
                    kv = strip_sapphire_key_row(screens[rj].reward_rows, ctx);
                }
                if (rj >= screens.size()) {
                    // nothing to compare rows against; the ofail above says so
                } else if (!kv.ok) {
                    ofail.push_back("SAPPHIRE_KEY shape: " + kv.problem);
                } else {
                    key_index = kv.key_index;
                    std::vector<std::string> sim_kinds;
                    for (uint8_t i = 0; i < screen.count; ++i)
                        sim_kinds.emplace_back(reward_kind_name(screen.items[i].kind));
                    std::vector<std::string> game_kinds;
                    for (const auto& r : kv.rows) game_kinds.push_back(r.type);
                    if (sim_kinds != game_kinds) {
                        std::string ge, se;
                        for (const auto& x : game_kinds) ge += (ge.empty() ? "" : ",") + x;
                        for (const auto& x : sim_kinds) se += (se.empty() ? "" : ",") + x;
                        ofail.push_back("reward rows: [" + ge + "] -> [" + se + "]");
                    } else {
                        for (std::size_t i = 0; i < kv.rows.size(); ++i) {
                            const RunRewardItem& it = screen.items[i];
                            if (kv.rows[i].type == "GOLD" &&
                                kv.rows[i].gold != it.gold + it.bonus_gold) {
                                ofail.push_back("chest gold: " +
                                                std::to_string(kv.rows[i].gold) + " -> " +
                                                std::to_string(it.gold + it.bonus_gold));
                            }
                            if (kv.rows[i].type == "RELIC") {
                                const std::string sim_id(sts::registry::relic_game_id(
                                    static_cast<RelicId>(it.id)));
                                if (kv.rows[i].relic_id != sim_id)
                                    ofail.push_back("chest relic: " +
                                                    kv.rows[i].relic_id + " -> " + sim_id);
                            }
                        }
                    }
                }

                if (ofail.empty()) {
                    ++v.open_clean;
                    std::printf("OPEN     OK   %s floor=%d seq=%d rows=[",
                                run.seed_string.c_str(), floor, run.records[j].seq);
                    for (uint8_t i = 0; i < screen.count; ++i)
                        std::printf("%s%s", i ? "," : "",
                                    reward_kind_name(screen.items[i].kind));
                    std::printf("] treasureRng %u->%u%s\n", cur.treasure_rng.counter,
                                post.treasure_rng.counter,
                                key_index >= 0
                                    ? "; capture carries the expected trailing "
                                      "SAPPHIRE_KEY row"
                                    : "");
                } else {
                    ++v.failures;
                    std::printf("OPEN     DIFF %s floor=%d seq=%d\n",
                                run.seed_string.c_str(), floor, run.records[j].seq);
                    for (const auto& f : ofail) std::printf("    %s\n", f.c_str());
                }
                cur = post;
                continue;
            }

            if (screens[j].screen_type == "COMBAT_REWARD") {
                if (c[0] != "choose") continue;  // proceed bounces; see above
                // Re-derive the key row from THIS record's own screen: the
                // capture's list shrinks as rows are claimed, so the index space
                // the command uses is the live one.
                KeyRowContext ctx;
                ctx.chest_open = true;
                ctx.already_has_key = has_key;
                ctx.nloths_mask_fired = nloths_mask_armed(cur);
                const KeyRowVerdict kv =
                    strip_sapphire_key_row(screens[j].reward_rows, ctx);
                const int live_key = kv.ok ? kv.key_index : -1;
                const ClaimMapping cm = map_reward_claim(
                    screens[j].reward_rows, live_key,
                    c.size() >= 2 ? std::stoi(c[1]) : -1);
                if (cm.what == ClaimTarget::OUT_OF_RANGE) {
                    stop = "seq " + std::to_string(run.records[j].seq) + " cmd '" +
                           run.records[j].action_command +
                           "' names no captured reward row";
                    break;
                }
                if (cm.what == ClaimTarget::ABANDONS_RELIC) {
                    // The capture took the KEY. RewardItem.java:317-322 marks
                    // the linked base relic isDone/ignoreReward, so the run
                    // never obtains it and neither does the sim; the relic
                    // stays popped from its pool, which the RunState comparison
                    // on the next record proves.
                    has_key = true;
                    for (uint8_t i = 0; i < screen.count; ++i) {
                        if (screen.items[i].kind !=
                            static_cast<uint8_t>(RewardItemKind::RELIC))
                            continue;
                        drop_reward_row(screen, i);
                        break;
                    }
                    std::printf("CLAIM    KEY  %s floor=%d seq=%d: the capture "
                                "claimed the SAPPHIRE_KEY row, abandoning the "
                                "linked base relic (RewardItem.java:317-322)\n",
                                run.seed_string.c_str(), floor, run.records[j].seq);
                    continue;
                }
                RunController rc{};
                rc.run = cur;
                rc.combat = run.records[j].combat;
                rc.phase = static_cast<uint8_t>(RunPhase::COMBAT_REWARD);
                rc.room_type = static_cast<uint8_t>(RoomType::Treasure);
                rc.rewards = screen;
                rc.cur_x = 0;
                step(rc, make_action(ActionVerb::CHOOSE,
                                     static_cast<uint8_t>(cm.sim_index)));
                cur = rc.run;
                screen = rc.rewards;
                continue;
            }

            if (screens[j].screen_type == "MAP") continue;  // `return` dismissal
            if (c[0] == "proceed" || c[0] == "return" || c[0] == "leave") continue;
            stop = "seq " + std::to_string(run.records[j].seq) + " cmd '" +
                   run.records[j].action_command + "' at screen " +
                   screens[j].screen_type + " has no run-layer analogue here";
            break;
        }
        if (!opened) ++v.skips;
        if (stop.empty()) {
            ++v.walk_clean;
            std::printf("WALK     OK   %s floor=%d %d in-room record%s compared, "
                        "chest %s\n", run.seed_string.c_str(), floor, compared,
                        compared == 1 ? "" : "s", opened ? "opened" : "skipped");
        } else {
            ++v.walk_partial;
            std::printf("WALK     PART %s floor=%d %d in-room record%s compared; "
                        "stop: %s\n", run.seed_string.c_str(), floor, compared,
                        compared == 1 ? "" : "s", stop.c_str());
        }
        (void)opts;
    }
    return v;
}

// --- the ?-room selection spot-diff (--event) --------------------------------
//
// A ?-room's resolution is a pure function of the state the room ENTRY sees, so
// this mode seeds like the merchant and the chest do. Per captured ? that
// stayed an event:
//
//   ROLL       `dispatch_on_enter_room_relics(rs, RoomType::Event)` (the
//              onEnterRoom fan-out the game runs against the ORIGINAL
//              EventRoom, AbstractDungeon.java:1755-1757 -- Ssserpent Head's 50
//              gold and Maw Bank's 12 fire even on a ? that becomes something
//              else) followed by
//              `event_room_roll`, which draws THE one committed eventRng float
//              (EventHelper.java:100-187).
//   SELECTION  `generate_event`, whose two selection draws are made on a
//              THROWAWAY copy and discarded, leaving eventRng byte-identical --
//              while the pool-membership removals and the event_flags bit DO
//              commit (event_framework.hpp's RNG contract).
//   ARRIVAL    the whole translated RunState of the first in-room record, and
//              the selected id joined to the capture's own `event_id`.
//
// The comparison is a WHOLE-RunState diff, not a field list, because everything
// the entry moves lives there: eventRng, the three pity floats, the three
// membership bitsets, event_flags, gold, and any relic counter the fan-out
// ticked. What it deliberately does NOT do is drive the dialog -- option flow
// and event grids belong to --replay. The one screen-level check is the ENTRY
// page's option count, and it is advisory: it is reported and counted, never
// folded into the zero-diff verdict, because a body's first page is content the
// selection layer does not own.

// --- the constructor deal (Match and Keep!) ----------------------------------
//
// MOST event bodies spend nothing until the player presses a button, so the
// capture's first in-room record and a sim that has only just SELECTED the
// event agree field for field. Match and Keep does not: its whole twelve-card
// board is dealt in the CONSTRUCTOR (GremlinMatchGame.java:55-61), which the
// game runs at `EventRoom.onPlayerEntry`, before any dialog is shown. By the
// time CommunicationMod dumps the "Continue" page, three streams have already
// moved, and the arrival diff in step 4 was therefore comparing a POST-deal
// capture against a PRE-deal sim -- reporting all six captured sightings as
// `cardRng +5` divergences that were nothing of the kind.
//
// This is a defect in what `--event` models, not a missing mode: the deal
// happens at arrival, which is exactly the moment this mode already owns, and
// steps 0-3 (the node symbol, the ?-roll, the throwaway selection and the
// identity join) are what tell us the sighting IS Match and Keep in the first
// place. So the fix is to run the body's own `on_enter` -- the same entry point
// the run layer uses -- before the arrival diff, and then to compare what the
// deal produced.
//
// THE THREE STREAMS, each read from the Java rather than assumed:
//   cardRng    the three `AbstractDungeon.getCard(rarity)` pool draws (:67-69 /
//              :73-75; one `cardRng.random(size-1)` each via
//              CardGroup.getRandomCard, CardGroup.java:502-506) and EVERY
//              `returnRandomCurse` (:70-71 / :77; CardLibrary.getCurse,
//              CardLibrary.java:1022-1029). Lives in RunState, so step 4's
//              whole-RunState diff compares it once `rs` is post-deal.
//   shuffleRng `returnColorlessCard(UNCOMMON)`'s one `randomLong`
//              (AbstractDungeon.java:1101). ONLY on the `ascensionLevel < 15`
//              branch (:72-78) -- at A20 the ascension branch (:66-71) draws a
//              SECOND curse instead and shuffleRng is never touched, which is
//              why every capture here shows it at counter 0. That untouched
//              state is not a dead check: it is the floor-stream seed itself,
//              so comparing it proves `floor_stream(seed, floor)` below.
//   miscRng    the board shuffle's one `randomLong` (:58).
// Both floor streams live in CombatState, which `diff_run_states` does not
// reach, so they are compared explicitly here.
//
// SEEDING THE FLOOR STREAMS. The capture's own arrival record carries them, but
// using it would make the read-out assume the answer. They are derived instead:
// all five floor-scoped streams are reseeded to `floor_stream(seed, floor)` on
// entering a floor (design §3.4, AbstractDungeon.java:1747-1751; the engine's
// own `reseed_floor_streams`), so the sim computes them and the capture checks
// them.
[[nodiscard]] bool event_deals_at_construction(uint16_t sim_id) noexcept {
    // The only Act-1 body that spends a stream in its constructor. Everything
    // else in the shrine / event / special lists builds its dialog and waits.
    return sim_id == static_cast<uint16_t>(sts::registry::EventId::MATCH_AND_KEEP);
}

[[nodiscard]] bool same_stream(const RngStream& a, const RngStream& b) noexcept {
    return a.s0 == b.s0 && a.s1 == b.s1 && a.counter == b.counter;
}

[[nodiscard]] std::string stream_text(const RngStream& s) {
    std::ostringstream o;
    o << "counter=" << s.counter << " s0=" << static_cast<int64_t>(s.s0)
      << " s1=" << static_cast<int64_t>(s.s1);
    return o.str();
}

// The `N` of a `choose N` command, or -1.
[[nodiscard]] int choose_index(const std::string& cmd) {
    const std::vector<std::string> p = split_ws(cmd);
    if (p.size() < 2 || p[0] != "choose") return -1;
    try {
        return std::stoi(p[1]);
    } catch (const std::exception&) {
        return -1;
    }
}

// The multiset of (card_id, upgrade) `after` holds that `before` does not.
// A multiset difference rather than a tail slice: nothing in this interaction
// removes a card, but a read-out that assumed append-only would silently
// mis-report the day something does.
[[nodiscard]] std::vector<std::pair<uint16_t, uint8_t>> deck_gain(
    const RunState& before, const RunState& after) {
    std::vector<std::pair<uint16_t, uint8_t>> pool;
    for (uint16_t i = 0; i < before.master_deck_count; ++i)
        pool.emplace_back(before.master_deck[i].card_id, before.master_deck[i].upgrade);
    std::vector<std::pair<uint16_t, uint8_t>> gained;
    for (uint16_t i = 0; i < after.master_deck_count; ++i) {
        const std::pair<uint16_t, uint8_t> c{after.master_deck[i].card_id,
                                             after.master_deck[i].upgrade};
        const auto it = std::find(pool.begin(), pool.end(), c);
        if (it != pool.end()) pool.erase(it);
        else gained.push_back(c);
    }
    std::sort(gained.begin(), gained.end());
    return gained;
}

[[nodiscard]] std::string deck_gain_text(
    const std::vector<std::pair<uint16_t, uint8_t>>& g) {
    if (g.empty()) return "nothing";
    std::string s;
    for (const auto& c : g) {
        if (!s.empty()) s += ", ";
        s += sts::registry::card_game_id(static_cast<sts::registry::CardId>(c.first));
        if (c.second != 0) s += "+" + std::to_string(c.second);
    }
    return s;
}

// What the deal read-out found, per sighting.
struct MatchDealReport {
    bool ran = false;       // the sighting was a Match and Keep with a grid walk
    bool ok = false;
    int identity_checks = 0;
    int pair_checks = 0;
    int rounds_compared = 0;  // grid records whose offered set matched the sim's
    int obtained = 0;         // cards the capture's deck gained
    std::string kept;         // those cards, named
    std::vector<std::string> problems;
    std::array<std::string, kMatchBoardSlots> sim_board{};  // by screen position
};

struct EventSighting {
    std::string seed;
    int floor = 0;
    std::string capture_id;
    std::string capture_name;
    uint16_t sim_id = 0;
    bool clean = false;
    bool obtain_race = false;
    bool deal_ok = false;
    std::string problem;
};

struct EventVerdict {
    int sightings = 0;
    int clean = 0;
    int races = 0;
    int failures = 0;
    int options_checked = 0;
    int options_matched = 0;
    int deals_checked = 0;
    int deals_clean = 0;
    int deal_identity_checks = 0;
    int deal_pair_checks = 0;
    int deal_rounds = 0;
    std::vector<EventSighting> rows;
};

// Drive the simulator's Match and Keep from the board it just dealt through the
// capture's own picks, and compare at every step.
//
// WHAT EACH PIECE IS WORTH, so a clean line is not read as more than it is:
//   - the BOARD comparison (`compare_match_deal`) is the acceptance. It pins
//     every screen position the capture ever named, position for position, and
//     every attempt's match/miss as a predicate over two positions -- which
//     reaches positions the capture never named.
//   - the ROUND walk is corroboration: after each pick the simulator's own
//     still-face-down-and-on-board set must be the set the next captured record
//     offered. It re-derives the same facts through the ENGINE's state machine
//     (`match_menu` / `match_choose`) instead of through the board array, so a
//     board that is right while the flip/remove bookkeeping is wrong is caught.
//   - the OBTAINED multiset closes the loop on the identities the capture never
//     labelled at all: a matched pair leaves `cards.group` before it can be
//     named on screen (GremlinMatchGame.java:221-222), and the only witness is
//     the card `ShowCardAndObtainEffect` put in the master deck (:224).
//
// INDEX SPACES. Three of them meet here and none of them is the same:
//   capture `choose N` -> N indexes the COMPACTED, position-sorted offered list
//   screen position    -> what a `cardN` label names
//   board slot         -> `cards.group` index, which is what the sim's
//                         `EventDialogState.board[]` and `match_choose` take
// `mk_board.hpp` owns the position<->slot permutation; `MatchBoardObservation`
// owns the compaction. Nothing here re-derives either.
[[nodiscard]] MatchDealReport read_out_match_deal(
    const sts::translate::TranslatedRun& run, const std::vector<ScreenInfo>& screens,
    std::size_t arrival, int floor, const EventDialogImpl& impl,
    const RunController& dealt) {
    MatchDealReport r;

    for (int p = 0; p < kMatchBoardSlots; ++p)
        r.sim_board[static_cast<std::size_t>(p)] =
            sts::registry::card_game_id(static_cast<sts::registry::CardId>(
                dealt.event.board[match_group_index(p)].card_id));

    // The room's records, and the screen sequence the Java guarantees:
    // INTRO (one button) -> RULE_EXPLANATION (one button) -> PLAY x10 (the
    // twelve-slot grid, five attempts of two picks) -> COMPLETE (one button)
    // (GremlinMatchGame.buttonEffect :246-276, updateMatchGameLogic :179-244).
    //
    // `room_type` alone OVER-COLLECTS: `AbstractEvent.openMap` leaves the
    // room's dialog mounted behind a dismissable map -- the same bounce
    // `map_command` elides for every event -- so the record after the COMPLETE
    // page is a MAP screen still reported as `EventRoom` on the same floor.
    // Only the EVENT screens are this event's pages.
    std::vector<std::size_t> in_room;
    for (std::size_t j = arrival; j < screens.size(); ++j) {
        if (screens[j].floor != floor || screens[j].room_type != "EventRoom") break;
        if (screens[j].screen_type != "EVENT") break;
        in_room.push_back(j);
    }
    if (in_room.size() < 4) {
        r.problems.push_back("the capture holds only " + std::to_string(in_room.size()) +
                             " in-room record(s); a played Match and Keep has an "
                             "INTRO page, a RULE_EXPLANATION page, ten grid picks "
                             "and a COMPLETE page");
        return r;
    }
    const std::size_t last = in_room.size() - 1;
    for (std::size_t t : {std::size_t{0}, std::size_t{1}, last}) {
        if (screens[in_room[t]].option_labels.size() != 1) {
            r.problems.push_back(
                "in-room record " + std::to_string(t) + " offers " +
                std::to_string(screens[in_room[t]].option_labels.size()) +
                " button(s); the INTRO, RULE_EXPLANATION and COMPLETE pages each "
                "offer exactly one");
            return r;
        }
    }

    std::vector<MatchGridRecord> grids;
    for (std::size_t t = 2; t < last; ++t) {
        const ScreenInfo& s = screens[in_room[t]];
        if (s.option_labels.size() < 3) {
            r.problems.push_back("in-room record " + std::to_string(t) + " offers " +
                                 std::to_string(s.option_labels.size()) +
                                 " option(s); a PLAY grid always offers at least "
                                 "three (twelve slots, at most four pairs gone and "
                                 "one face up)");
            return r;
        }
        MatchGridRecord g;
        g.labels = s.option_labels;
        g.choice = choose_index(run.records[in_room[t]].action_command);
        grids.push_back(std::move(g));
    }
    r.ran = true;

    // The deck delta, read one record PAST the room where possible: the last
    // match's `ShowCardAndObtainEffect` is an animation, so a dump taken while
    // it is still running is a card short (the same obtain race step 4 already
    // recognizes). Both readings agree on every capture in the corpus; taking
    // the later one is what makes that a property rather than luck.
    const std::size_t after_room =
        (in_room.back() + 1 < run.records.size()) ? in_room.back() + 1 : in_room.back();
    const std::vector<std::pair<uint16_t, uint8_t>> capture_gain =
        deck_gain(run.records[arrival].run, run.records[after_room].run);
    r.obtained = static_cast<int>(capture_gain.size());
    r.kept = deck_gain_text(capture_gain);

    const MatchBoardObservation obs = decode_match_grid(grids, r.obtained);
    const MatchDealDiff diff = compare_match_deal(obs, r.sim_board);
    r.identity_checks = diff.identity_checks;
    r.pair_checks = diff.pair_checks;
    for (const std::string& p : diff.problems) r.problems.push_back(p);
    if (!obs.ok) return r;

    // The walk. `dealt` is parked on the INTRO page with the board already
    // dealt, so the two dialog pages come first and then the ten picks.
    RunController w = dealt;
    (void)impl.choose(w, w.event, 0);  // INTRO -> RULE_EXPLANATION
    (void)impl.choose(w, w.event, 0);  // RULE_EXPLANATION -> PLAY (placeCards)
    for (std::size_t g = 0; g < grids.size(); ++g) {
        EventDialogMenu menu{};
        impl.build_menu(w, w.event, menu);
        std::vector<int> sim_offered;
        for (int i = 0; i < static_cast<int>(menu.count); ++i)
            if (menu.enabled[static_cast<std::size_t>(i)])
                sim_offered.push_back(match_screen_position(i));
        std::sort(sim_offered.begin(), sim_offered.end());
        if (sim_offered != obs.offered[g]) {
            std::string want;
            for (int p : obs.offered[g]) want += (want.empty() ? "" : ",") + std::to_string(p);
            std::string got;
            for (int p : sim_offered) got += (got.empty() ? "" : ",") + std::to_string(p);
            r.problems.push_back("grid record " + std::to_string(g) +
                                 ": the capture offers screen positions [" + want +
                                 "], the sim offers [" + got + "]");
            break;
        }
        ++r.rounds_compared;
        const int pos = obs.offered[g][static_cast<std::size_t>(grids[g].choice)];
        (void)impl.choose(w, w.event,
                             static_cast<uint8_t>(match_group_index(pos)));
    }

    // The COMPLETE page. The capture reached it after exactly five resolved
    // attempts (`attemptCount` starts at 5 and drops on every resolved pair,
    // match or miss, GremlinMatchGame.java:235-239), so the sim must be off the
    // grid and on a one-button page here -- and pressing it must END the event.
    if (r.rounds_compared == static_cast<int>(grids.size())) {
        EventDialogMenu done{};
        impl.build_menu(w, w.event, done);
        if (done.count != 1)
            r.problems.push_back(
                "after the capture's ten picks the sim still offers " +
                std::to_string(done.count) +
                " option(s); five resolved attempts end the game "
                "(GremlinMatchGame.java:235-239)");
        else if (impl.choose(w, w.event, 0) != EventDialogStatus::FINISHED)
            r.problems.push_back(
                "the sim's COMPLETE page did not finish the event on its one "
                "button, but the capture left the room here");
    }

    const std::vector<std::pair<uint16_t, uint8_t>> sim_gain =
        deck_gain(dealt.run, w.run);
    if (sim_gain != capture_gain)
        r.problems.push_back("the run's deck gained {" + deck_gain_text(capture_gain) +
                             "} in the capture and {" + deck_gain_text(sim_gain) +
                             "} in the sim");

    r.ok = r.problems.empty();
    return r;
}

[[nodiscard]] EventVerdict event_spot_diff_one(const std::string& path,
                                               const Options& opts) {
    EventVerdict v;
    const sts::translate::TranslatedRun run = sts::translate::translate_file(path);
    const std::vector<ScreenInfo> screens = read_screens(path);
    if (screens.size() != run.records.size())
        throw std::runtime_error("screen/record count mismatch in " + path);

    for (std::size_t k = 0; k < screens.size(); ++k) {
        if (screens[k].room_type != "EventRoom") continue;
        if (screens[k].floor == 0) continue;  // Neow -- the --neow mode's subject
        if (k > 0 && screens[k - 1].room_type == "EventRoom" &&
            screens[k - 1].floor == screens[k].floor)
            continue;
        if (k == 0) continue;
        ++v.sightings;

        EventSighting row;
        row.seed = run.seed_string;
        row.floor = screens[k].floor;

        // The capture's own identity for this room, from the first record that
        // actually shows the dialog.
        for (std::size_t j = k; j < screens.size(); ++j) {
            if (screens[j].floor != row.floor || screens[j].room_type != "EventRoom")
                break;
            if (screens[j].event_id.empty()) continue;
            row.capture_id = screens[j].event_id;
            row.capture_name = screens[j].event_name;
            break;
        }

        std::vector<std::string> fail;

        // 0. The node really was a `?`. An EventRoom has no other producer in
        //    Act 1, and checking it keeps the roll below honest about which
        //    branch of nextRoomTransition it is standing in.
        const std::string symbol =
            entered_node_symbol(screens[k - 1], run.records[k - 1].action_command);
        if (!symbol.empty() && symbol != "?")
            fail.push_back("the capture entered this EventRoom from a '" + symbol +
                           "' node, not a '?'");

        // 1. The ?-roll, off the pre-entry state.
        RunState rs = run.records[k - 1].run;
        // ++floorNum first (AbstractDungeon.java:1741; trap 7). This is not
        // cosmetic: `floorNum` is read by EventHelper.roll's eliteSize gate
        // (:123-125) and, more importantly, by getEvent's Dead Adventurer and
        // Mushrooms filter (`floorNum > 6`, AbstractDungeon.java:1949-1982), so
        // an off-by-one floor silently changes the DRAW LIST the pool index
        // addresses.
        ++rs.floor;
        dispatch_on_enter_room_relics(rs, RoomType::Event);
        const bool leaving_shop = screens[k - 1].room_type == "ShopRoom";
        const RngStream before = rs.event_rng;
        const EventRoomResult roll = event_room_roll(rs, leaving_shop);
        if (roll != EventRoomResult::EVENT) {
            fail.push_back("the sim's ?-roll resolved to " +
                           std::string(event_roll_name(roll)) +
                           " but the capture stayed an EventRoom");
        }

        // 2. Selection, on the throwaway stream.
        const RngStream after_roll = rs.event_rng;
        // The pre-entry record's wall clock feeds SecretPortal's getShrine
        // gate (s2-design §5 trap 5 / S2.43) -- see replay_one's write-up.
        // Absent on a pre-anchor capture, which keeps the engine's 0.0f pin.
        const float playtime = run.records[k - 1].has_playtime
                                   ? run.records[k - 1].playtime
                                   : sts::engine::kUnmodelledPlaytimeSeconds;
        const uint16_t sim_id = generate_event(rs, playtime);
        row.sim_id = sim_id;
        if (rs.event_rng.s0 != after_roll.s0 || rs.event_rng.s1 != after_roll.s1 ||
            rs.event_rng.counter != after_roll.counter) {
            fail.push_back("generate_event moved eventRng; the selection draws "
                           "are made on a discarded duplicate "
                           "(AbstractDungeon.java:1865)");
        }

        // 3. The identity join.
        const EventJoin join = join_capture_event(row.capture_id);
        if (!join.problem.empty()) {
            fail.push_back("event join: " + join.problem);
        } else if (join.id != sim_id) {
            fail.push_back(
                "event identity: capture " + row.capture_id + " (EventId " +
                std::to_string(join.id) + ") -> sim " +
                std::string(sim_id == 0
                                ? "NONE"
                                : sts::registry::event_game_id(
                                      static_cast<sts::registry::EventId>(sim_id))) +
                " (EventId " + std::to_string(sim_id) + ")");
        }

        // 3b. THE CONSTRUCTOR DEAL, before the arrival diff, because the
        //     capture's arrival record is already POST-deal. See
        //     `event_deals_at_construction` for the three streams and why the
        //     floor streams are derived rather than copied from the capture.
        const EventDialogImpl* impl =
            (sim_id != 0) ? event_dialog_impl(sim_id) : nullptr;
        RunController rc{};
        bool dealt = false;
        if (fail.empty() && impl != nullptr && event_deals_at_construction(sim_id)) {
            rc.run = rs;
            const RngStream fresh =
                floor_stream(rs.run_seed, static_cast<int32_t>(rs.floor));
            rc.combat.monster_hp_rng = fresh;
            rc.combat.ai_rng = fresh;
            rc.combat.shuffle_rng = fresh;
            rc.combat.card_random_rng = fresh;
            rc.combat.misc_rng = fresh;
            rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
            rc.room_type = static_cast<uint8_t>(RoomType::Event);
            rc.event = EventDialogState{};
            rc.event.event_id = sim_id;
            impl->on_enter(rc, rc.event);
            dealt = true;
            rs = rc.run;  // so the arrival diff below sees a post-deal cardRng

            const CombatState& cap = run.records[k].combat;
            if (!same_stream(rc.combat.misc_rng, cap.misc_rng))
                fail.push_back("miscRng after the board shuffle: capture " +
                               stream_text(cap.misc_rng) + ", sim " +
                               stream_text(rc.combat.misc_rng) +
                               " (GremlinMatchGame.java:58)");
            if (!same_stream(rc.combat.shuffle_rng, cap.shuffle_rng))
                fail.push_back("shuffleRng after the deal: capture " +
                               stream_text(cap.shuffle_rng) + ", sim " +
                               stream_text(rc.combat.shuffle_rng) +
                               " (returnColorlessCard, AbstractDungeon.java:1101 -- "
                               "untouched at ascension >= 15, so this is also the "
                               "floor_stream(seed, floor) seed)");
        }

        // 4. The whole arrival state -- eventRng, pity, the three membership
        //    bitsets, event_flags, gold, relic counters.
        RunState e = run.records[k].run;
        RunState a = rs;
        neutralize_presentation_only(e);
        neutralize_presentation_only(a);
        e.neow_rng = RngStream{};
        a.neow_rng = RngStream{};
        neutralize_unattested_boss_chest(e, a);
        const sts::diff::DiffReport rep = sts::diff::diff_run_states(e, a);
        if (is_obtain_race(rep, /*ahead=*/e, /*behind=*/a)) {
            row.obtain_race = true;
            std::printf("  RACE     %s floor=%d: the capture's deck holds %u card(s) "
                        "the pre-entry record did not -- ShowCardAndObtainEffect "
                        "landed across the transition (the B1.3/B5.2 obtain-race "
                        "capture gap); selection state is otherwise identical\n",
                        run.seed_string.c_str(), row.floor,
                        static_cast<unsigned>(e.master_deck_count -
                                              a.master_deck_count));
        } else if (!rep.empty()) {
            fail.push_back("arrival RunState (" + std::to_string(rep.size()) +
                           " field(s)):\n" + rep.to_string());
        }

        // 5. ADVISORY: the entry page's option count, when the sim has a body.
        //    A body that already dealt at 3b keeps THAT controller -- running
        //    `on_enter` a second time would deal a second board off an
        //    already-advanced cardRng.
        if (fail.empty() && impl != nullptr) {
            if (!dealt) {
                rc = RunController{};
                rc.run = rs;
                rc.combat = run.records[k].combat;
                rc.phase = static_cast<uint8_t>(RunPhase::EVENT_DIALOG);
                rc.room_type = static_cast<uint8_t>(RoomType::Event);
                rc.event = EventDialogState{};
                rc.event.event_id = sim_id;
                impl->on_enter(rc, rc.event);
            }
            EventDialogMenu menu{};
            impl->build_menu(rc, rc.event, menu);
            ++v.options_checked;
            const std::size_t game = screens[k].option_labels.size();
            if (menu.count == game) {
                ++v.options_matched;
            } else {
                std::printf("  OPTIONS  ADV  %s floor=%d %s: capture shows %zu "
                            "button(s), sim's entry menu has %u\n",
                            run.seed_string.c_str(), row.floor,
                            row.capture_id.c_str(), game, menu.count);
            }
        }

        // 6. THE DEAL READ-OUT: the twelve dealt identities against the board
        //    the capture progressively exposes, the five attempts' outcomes, and
        //    the cards the run actually kept. Unlike the option count above this
        //    is NOT advisory -- the deal is what B4.13's spot-check is for.
        if (fail.empty() && dealt) {
            const MatchDealReport deal =
                read_out_match_deal(run, screens, k, row.floor, *impl, rc);
            if (deal.ran) {
                ++v.deals_checked;
                v.deal_identity_checks += deal.identity_checks;
                v.deal_pair_checks += deal.pair_checks;
                v.deal_rounds += deal.rounds_compared;
            }
            if (deal.ok) {
                ++v.deals_clean;
                row.deal_ok = true;
                std::printf("  DEAL     OK   %s floor=%d: %d/12 screen position(s) "
                            "named by the capture and identical, %d attempt "
                            "outcome(s) reproduced, %d grid round(s) walked, kept "
                            "{%s}\n",
                            run.seed_string.c_str(), row.floor, deal.identity_checks,
                            deal.pair_checks, deal.rounds_compared,
                            deal.kept.c_str());
            } else {
                for (const std::string& p : deal.problems)
                    fail.push_back("Match and Keep deal: " + p);
            }
            if (deal.ok && opts.verbose) {
                std::string b;
                for (int p = 0; p < kMatchBoardSlots; ++p)
                    b += (p == 0 ? "" : " | ") + std::to_string(p) + ":" +
                         deal.sim_board[static_cast<std::size_t>(p)];
                std::printf("           board by screen position: %s\n", b.c_str());
            }
        }

        row.clean = fail.empty();
        if (row.clean) {
            if (row.obtain_race) ++v.races;
            else ++v.clean;
            std::printf("EVENT    %s %s floor=%d %-22s (%s) EventId=%u "
                        "eventRng %u->%u\n",
                        row.obtain_race ? "RACE" : "OK  ",
                        run.seed_string.c_str(), row.floor, row.capture_id.c_str(),
                        row.capture_name.c_str(), sim_id, before.counter,
                        rs.event_rng.counter);
        } else {
            ++v.failures;
            row.problem = fail.front();
            std::printf("EVENT    DIFF %s floor=%d %s (%s)\n",
                        run.seed_string.c_str(), row.floor, row.capture_id.c_str(),
                        row.capture_name.c_str());
            for (const auto& f : fail) std::printf("    %s\n", f.c_str());
        }
        v.rows.push_back(std::move(row));
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
        else if (a == "--trace-powers") opts.trace_powers = true;
        else if (a == "--combat-summary") opts.combat_summary = true;
        else if (a == "--vitals") opts.vitals = true;
        else if (a == "--replay") opts.full_replay = true;
        else if (a == "--neow") opts.neow = true;
        else if (a == "--shop") opts.shop = true;
        else if (a == "--treasure") opts.treasure = true;
        else if (a == "--event") opts.event = true;
        else files.push_back(a);
    }
    if (files.empty() ||
        static_cast<int>(opts.full_replay) + static_cast<int>(opts.neow) +
                static_cast<int>(opts.shop) + static_cast<int>(opts.treasure) +
                static_cast<int>(opts.event) > 1) {
        std::fprintf(stderr,
                     "usage: replay_run_diff <run.jsonl> [...] "
                     "[--replay | --neow | --shop | --treasure | --event]\n"
                     "         [--verbose] [--pool-evidence] [--stop-on-diff] [--combat]\n"
                     "         [--trace-powers] [--combat-summary] [--vitals]\n"
                     "  default:    per-reward-screen spot-diff seeded from the capture\n"
                     "  --replay:   whole-run replay from run_begin, diffed per record\n"
                     "  --neow:     floor-0 blessing spot-diff (options / activation / "
                     "post-choice)\n"
                     "  --shop:     merchant spot-diff (stock, prices, sale slot, purchases)\n"
                     "  --treasure: chest spot-diff (size, rewards, streams, the claim walk)\n"
                     "  --event:    ?-room spot-diff (roll, selection, pools, arrival "
                     "state, and\n"
                     "              a constructor-dealing body's board -- Match and "
                     "Keep's twelve\n"
                     "              cards; --verbose prints the dealt board)\n"
                     "  --trace-powers: with --replay, print every in-combat record's\n"
                     "              monster power lists -- capture beside sim, joined by\n"
                     "              power id -- and tag the records where they differ.\n"
                     "              The run-level differ never reads combat state, so a\n"
                     "              power-count drift (Time Warp's card clock) is invisible\n"
                     "              to it until the turn boundary moves; this is the print\n"
                     "              that shows the drift at the record it starts. Triage\n"
                     "              only: the tag also fires on known dispositions (a\n"
                     "              hidden Curl Up, a spent Artifact at 0, Life Link /\n"
                     "              Minion), so read the named power, not the tag count\n"
                     "  --combat:   (with --replay) also print the raw CombatState diff "
                     "per in-combat\n"
                     "              record -- a diagnosis aid, index-sensitive by nature\n"
                     "  --vitals:   (with --replay) compare the index-normalised combat "
                     "vitals per\n"
                     "              in-combat record (turn, player block/energy/powers, "
                     "monster hp/\n"
                     "              block/liveness/powers by slot, pile contents as "
                     "multisets) and\n"
                     "              report the first differing record; the CLEAN/PART "
                     "verdict and exit\n"
                     "              code are unchanged\n"
                     "  the mode flags are mutually exclusive\n");
        return 2;
    }

    if (opts.treasure) {
        int failures = 0;
        TreasureVerdict total{};
        for (const std::string& f : files) {
            std::printf("=== %s\n", f.c_str());
            try {
                const TreasureVerdict v = treasure_spot_diff_one(f, opts);
                total.rooms += v.rooms;
                total.construction_clean += v.construction_clean;
                total.opens += v.opens;
                total.open_clean += v.open_clean;
                total.skips += v.skips;
                total.walk_clean += v.walk_clean;
                total.walk_partial += v.walk_partial;
                total.failures += v.failures;
                if (v.failures != 0) ++failures;
            } catch (const std::exception& e) {
                std::printf("ERROR %s: %s\n", f.c_str(), e.what());
                ++failures;
            }
        }
        std::printf("--- %zu file(s): %d treasure room(s), construction clean %d, "
                    "%d opened (%d clean) / %d skipped, in-room walks clean %d "
                    "(+%d partial), %d divergence(s) ---\n",
                    files.size(), total.rooms, total.construction_clean, total.opens,
                    total.open_clean, total.skips, total.walk_clean,
                    total.walk_partial, total.failures);
        return failures;
    }

    if (opts.event) {
        int failures = 0;
        EventVerdict total{};
        for (const std::string& f : files) {
            try {
                std::printf("=== %s\n", f.c_str());
                const EventVerdict v = event_spot_diff_one(f, opts);
                total.sightings += v.sightings;
                total.clean += v.clean;
                total.races += v.races;
                total.failures += v.failures;
                total.options_checked += v.options_checked;
                total.options_matched += v.options_matched;
                total.deals_checked += v.deals_checked;
                total.deals_clean += v.deals_clean;
                total.deal_identity_checks += v.deal_identity_checks;
                total.deal_pair_checks += v.deal_pair_checks;
                total.deal_rounds += v.deal_rounds;
                for (const EventSighting& r : v.rows) total.rows.push_back(r);
                if (v.failures != 0) ++failures;
            } catch (const std::exception& e) {
                std::printf("=== %s\nERROR %s\n", f.c_str(), e.what());
                ++failures;
            }
        }
        // The per-sighting table, in one block, so a runbook can quote it.
        std::printf("\n--- per-sighting verdict table ---\n");
        std::printf("%-10s %5s  %-24s %-22s %-8s %s\n", "seed", "floor", "event_id",
                    "event_name", "EventId", "verdict");
        for (const EventSighting& r : total.rows) {
            const char* verdict =
                r.clean ? (r.obtain_race ? "zero-diff (obtain race)"
                                         : (r.deal_ok ? "zero-diff (DEAL OK)"
                                                      : "zero-diff"))
                        : "DIFF";
            std::printf("%-10s %5d  %-24s %-22s %-8u %s%s%s\n", r.seed.c_str(), r.floor,
                        r.capture_id.c_str(), r.capture_name.c_str(), r.sim_id,
                        verdict, r.clean ? "" : ": ",
                        r.clean ? "" : r.problem.c_str());
        }
        std::printf("--- %zu file(s): %d sighting(s), %d zero-diff (+%d clean but for "
                    "the known obtain race), %d diverged; entry-page option count "
                    "matched %d of %d advisory check(s) ---\n",
                    files.size(), total.sightings, total.clean, total.races,
                    total.failures, total.options_matched, total.options_checked);
        if (total.deals_checked != 0)
            std::printf("--- constructor deals: %d read out, %d zero-diff; %d screen "
                        "position(s) named by a capture and compared, %d attempt "
                        "outcome(s) reproduced, %d grid round(s) walked ---\n",
                        total.deals_checked, total.deals_clean,
                        total.deal_identity_checks, total.deal_pair_checks,
                        total.deal_rounds);
        return failures;
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
                total.theft_seeded += v.theft_seeded;
                total.failures += v.failures;
                if (v.failures != 0) ++failures;
            } catch (const std::exception& e) {
                std::printf("ERROR %s: %s\n", f.c_str(), e.what());
                ++failures;
            }
        }
        // `theft seeded` is reported beside the clean counts, never folded into
        // them: those screens are clean, but one input of theirs came from the
        // capture rather than from the sim (see `captured_stolen_gold`).
        std::printf("--- %zu file(s): %d reward screen(s), assembly clean %d "
                    "(%d with a capture-seeded STOLEN_GOLD row), claim clean %d "
                    "(+%d library-order-only), %d failing file(s) ---\n",
                    files.size(), total.screens, total.assembly_clean,
                    total.theft_seeded, total.claim_clean,
                    total.claim_library_order_only, failures);
        return failures;
    }

    int failures = 0;
    for (const std::string& f : files) {
        std::printf("=== %s\n", f.c_str());
        try {
            const Verdict v = replay_one(f, opts);
            std::printf("%s %s: %d record%s compared (%d on reward screens), "
                        "%d library-order-only, %d obtain-race, %d escape-race, "
                        "%d preview-race; stop: %s\n",
                        v.clean ? "CLEAN" : "PART ", f.c_str(), v.records_compared,
                        v.records_compared == 1 ? "" : "s",
                        v.reward_records_compared, v.deck_identity_records,
                        v.obtain_race_records, v.escape_race_records,
                        v.preview_race_records,
                        v.stop_reason.c_str());
            if (v.post_victory_ending_records > 0)
                std::printf("      %d post-victory ending record(s) skipped "
                            "(the Spire Heart cinematic -- out of S2 scope)\n",
                            v.post_victory_ending_records);
            // Deliberately NOT spelled `... -race`: the campaign pipeline's
            // strict accounting scrapes every `N <name>-race` field out of this
            // line as a CAPTURE artifact, and this is not one. Nothing lagged
            // capture-side; the simulator collapses a screen the game shows,
            // and the record is compared shifted rather than excused.
            if (v.double_boss_handoff_records > 0)
                std::printf("      %d A20 double-boss handoff record(s) "
                            "compared against the capture's own post-proceed "
                            "record (ProceedButton.java:210-220)\n",
                            v.double_boss_handoff_records);
            // The frontier, always on its own line and never folded into the
            // stop -- see `Verdict`. "no divergence" is said out loud too: a
            // replay that stopped without ever disagreeing is a coverage gap in
            // the harness, and a replay that disagreed is a question for the
            // engine, and the summary should not need a second run to tell them
            // apart.
            if (v.diverged_seq >= 0) {
                // A replay that ran out of artifact ended for no reason of its
                // own, so "downstream" is only worth saying when the harness
                // stopped EARLY -- that is the case a reader mistakes for the
                // frontier.
                const bool stopped_early = v.stop_reason != "run terminal" &&
                                           v.stop_reason != "artifact exhausted";
                std::printf("      first divergence: seq=%d floor=%d screen=%s "
                            "(%zu field%s)%s\n",
                            v.diverged_seq, v.diverged_floor,
                            v.diverged_screen.c_str(), v.diverged_fields,
                            v.diverged_fields == 1 ? "" : "s",
                            stopped_early
                                ? " -- the stop above is DOWNSTREAM of this"
                                : "");
            } else {
                std::printf("      first divergence: none -- every compared "
                            "record was zero-diff\n");
            }
            // The --vitals report: its own verdict word and its own frontier,
            // on lines of their own, never folded into CLEAN/PART or the exit
            // code. (Worded without a `<N> <name>-race` field on purpose: the
            // campaign pipeline scrapes those off the summary line above as
            // capture-artifact counts.)
            if (opts.vitals) {
                std::printf("      vitals: %s -- %d in-combat record%s compared, %d "
                            "differed; %d skipped on tolerated race/handoff records, "
                            "%d skipped with the sim already out of the fight\n",
                            v.vitals_diverged_records == 0 ? "vitals-clean"
                                                           : "vitals-divergent",
                            v.vitals_records, v.vitals_records == 1 ? "" : "s",
                            v.vitals_diverged_records, v.vitals_skipped_race,
                            v.vitals_skipped_phase);
                if (v.vitals_first_seq >= 0) {
                    std::printf("      first vitals divergence: seq=%d floor=%d turn=%d "
                                "(%zu field%s)\n",
                                v.vitals_first_seq, v.vitals_first_floor,
                                v.vitals_first_turn, v.vitals_first_fields,
                                v.vitals_first_fields == 1 ? "" : "s");
                } else {
                    std::printf("      first vitals divergence: none -- every "
                                "vitals-compared record was zero-diff\n");
                }
                if (!v.vitals_unknown_ids.empty()) {
                    std::printf("      %zu unresolved id(s) named above (VITALS lines)\n",
                                v.vitals_unknown_ids.size());
                }
            }
            if (!v.clean) ++failures;
        } catch (const std::exception& e) {
            std::printf("ERROR %s: %s\n", f.c_str(), e.what());
            ++failures;
        }
    }
    std::printf("--- %zu file(s), %d not clean ---\n", files.size(), failures);
    return failures;
}
