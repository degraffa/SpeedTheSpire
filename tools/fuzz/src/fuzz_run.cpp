// The replay-twice determinism guard. See fuzz_run.hpp for the design and why
// there are three passes rather than one comparison of two final hashes.

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "sts/fuzz/fuzz_run.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/fuzz/repro.hpp"

namespace sts::fuzz {

using engine::Action;
using engine::ActionVerb;
using engine::RunActionMask;
using engine::RunController;
using engine::RunPhase;
using engine::StepResult;

const char* fail_kind_name(FailKind k) noexcept {
    switch (k) {
        case FailKind::NONE: return "none";
        case FailKind::HASH_MISMATCH: return "hash_mismatch";
        case FailKind::ACTION_MISMATCH: return "action_mismatch";
        case FailKind::LENGTH_MISMATCH: return "length_mismatch";
        case FailKind::REPRO_MISMATCH: return "repro_mismatch";
        case FailKind::NO_LEGAL_MOVES: return "no_legal_moves";
        case FailKind::NO_PROGRESS: return "no_progress";
        case FailKind::COUNT: break;
    }
    return "?";
}

bool fail_kind_from_name(std::string_view name, FailKind& out) noexcept {
    for (uint8_t i = static_cast<uint8_t>(FailKind::HASH_MISMATCH);
         i < static_cast<uint8_t>(FailKind::COUNT); ++i) {
        const auto kind = static_cast<FailKind>(i);
        if (name == fail_kind_name(kind)) {
            out = kind;
            return true;
        }
    }
    return false;
}

// WHY THIS IS NOT `XXH3_64bits(&rc, sizeof(rc))`.
//
// It was, for exactly as long as it took to run the induced-failure test. A raw
// byte hash of RunController is NOT a content hash, because `RunController.lists`
// is a `MonsterLists` of `std::string_view` encounter keys -- and a string_view
// is a POINTER plus a length. The keys point into the generated registry's
// .rodata, so under a PIE/ASLR load their addresses differ on every process
// launch. The symptom was that a trajectory hashed to a different chain in every
// process while being perfectly stable inside one, which is precisely what a
// replay-twice guard must never confuse with a real divergence: identical inside
// a process (so the soak saw zero failures) and different across processes (so
// every emitted reproducer failed to reproduce).
//
// `RunController` is `static_assert`ed trivially copyable and described as a
// POD batch entry, and it is -- but TRIVIALLY COPYABLE IS NOT POSITION
// INDEPENDENT. Anything that hashes, persists, or ships a RunController must go
// through a content view like this one.
//
// So: the two real states go through the engine's own hash_state (which carries
// the value-initialization contract), the transient scalars are hashed by value,
// and the encounter lists are hashed by their CHARACTERS, never their addresses.
namespace {

void update_lists(XXH3_state_t* st, const engine::MonsterLists& l) noexcept {
    auto put_keys = [st](const std::string_view* keys, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const uint32_t len = static_cast<uint32_t>(keys[i].size());
            XXH3_64bits_update(st, &len, sizeof(len));
            if (len != 0) XXH3_64bits_update(st, keys[i].data(), len);
        }
    };
    const uint8_t counts[3] = {l.monster_list_count, l.elite_list_count,
                               l.boss_list_count};
    XXH3_64bits_update(st, counts, sizeof(counts));
    put_keys(l.monster_list.data(), l.monster_list.size());
    put_keys(l.elite_list.data(), l.elite_list.size());
    put_keys(l.boss_list.data(), l.boss_list.size());
}

[[nodiscard]] uint64_t lists_hash(const engine::MonsterLists& l) noexcept {
    XXH3_state_t st;
    XXH3_64bits_reset(&st);
    update_lists(&st, l);
    return static_cast<uint64_t>(XXH3_64bits_digest(&st));
}

[[nodiscard]] uint64_t scalars_hash(const RunController& rc) noexcept {
    const uint8_t s[9] = {rc.phase,
                          rc.cur_x,
                          rc.room_type,
                          rc.combat_outcome,
                          rc.monster_cursor,
                          rc.elite_cursor,
                          rc.boss_cursor,
                          rc.pad1,
                          rc.rest.screen};
    XXH3_state_t st;
    XXH3_64bits_reset(&st);
    XXH3_64bits_update(&st, s, sizeof(s));
    // The event dialog state (B4.10): the selected EventId while parked or in
    // dialog, plus the event-defined screen/scratch fields. POD with explicit
    // padding, so a byte hash is stable.
    XXH3_64bits_update(&st, &rc.event, sizeof(rc.event));
    // The Neow blessing state: the four rolled options, which of its screens is
    // up, and the grid's partial selection. Same POD-with-explicit-padding
    // contract as the event block above. It MUST be hashed for the reason
    // rest.screen is: a Neow press that only opens a sub-screen moves no
    // RunState byte, and a step whose whole-controller hash is unchanged is
    // reported as a NO_PROGRESS failure by the soak.
    XXH3_64bits_update(&st, &rc.neow, sizeof(rc.neow));
    // The live merchant: the whole stock, its prices, which rows are sold, the
    // sale index, which shop screen is up and this visit's purge cost. It MUST
    // be hashed for exactly the reason rest.screen and neow are -- opening the
    // card-removal grid moves no RunState byte, so without this the soak would
    // report that press as a NO_PROGRESS failure. POD with explicit padding, so
    // the byte hash is stable.
    XXH3_64bits_update(&st, &rc.shop, sizeof(rc.shop));
    return static_cast<uint64_t>(XXH3_64bits_digest(&st));
}

}  // namespace

uint64_t hash_controller(const RunController& rc, uint64_t* run_h,
                         uint64_t* combat_h) noexcept {
    XXH3_state_t st;
    XXH3_64bits_reset(&st);
    const uint64_t states[2] = {engine::hash_state(rc.run),
                                engine::hash_state(rc.combat)};
    // Handed back so the per-step chain can carry the region breakdown at no
    // extra cost -- localizing a divergence to RunState vs CombatState vs the
    // transient screen flow is most of the triage.
    if (run_h != nullptr) *run_h = states[0];
    if (combat_h != nullptr) *combat_h = states[1];
    XXH3_64bits_update(&st, states, sizeof(states));
    const uint64_t sc = scalars_hash(rc);
    XXH3_64bits_update(&st, &sc, sizeof(sc));
    XXH3_64bits_update(&st, &rc.rewards, sizeof(rc.rewards));
    XXH3_64bits_update(&st, &rc.treasure_chest,
                       sizeof(rc.treasure_chest));
    update_lists(&st, rc.lists);
    return static_cast<uint64_t>(XXH3_64bits_digest(&st));
}

ControllerHashes hash_controller_parts(const RunController& rc) noexcept {
    ControllerHashes h;
    h.whole = hash_controller(rc);
    h.run = engine::hash_state(rc.run);
    h.combat = engine::hash_state(rc.combat);
    h.lists = lists_hash(rc.lists);
    h.rewards = static_cast<uint64_t>(XXH3_64bits(&rc.rewards, sizeof(rc.rewards)));
    h.treasure = static_cast<uint64_t>(
        XXH3_64bits(&rc.treasure_chest, sizeof(rc.treasure_chest)));
    h.scalars = scalars_hash(rc);
    return h;
}

namespace {

[[nodiscard]] int turn_bucket_of(uint32_t turn) noexcept {
    if (turn <= 4) return turn == 0 ? 0 : static_cast<int>(turn) - 1;  // 1,2,3,4
    if (turn <= 6) return 4;
    if (turn <= 9) return 5;
    if (turn <= 19) return 6;
    return 7;
}

// One execution of a case. `scripted` non-null drives from a literal action
// list instead of the policy (the reproducer path).
struct StepHash {
    uint64_t whole = 0;
    uint64_t run = 0;     // engine::hash_state(RunState)
    uint64_t combat = 0;  // engine::hash_state(CombatState)
};

struct Pass {
    std::vector<Action> actions;
    std::vector<StepHash> hashes;  // controller hash AFTER each step
    EndReason end = EndReason::RUN_OVER;
    uint64_t final_hash = 0;
    Failure failure;
};

void execute(const CaseId& id, const RunLimits& lim, Coverage* cov, Pass& p,
             const std::vector<Action>* scripted, const Inject* inject,
             const StepObserver* observer = nullptr) {
    RunController rc = engine::run_begin(id.run_seed, id.ascension);
    PolicyRng rng(id.policy_seed);

    RunActionMask mask{};
    Move moves[kMoveCap];
    StepResult results[1]{};

    uint8_t prev_phase = 0xFF;
    uint32_t combat_max_turn = 0;
    // Livelock detection over a sliding window of recent controller hashes.
    uint64_t recent[kRevisitWindow]{};
    uint32_t recent_n = 0;
    uint32_t revisits = 0;

    p.actions.reserve(64);
    p.hashes.reserve(64);

    // Coverage for the initial (Neow) state, so a case that never steps still
    // reports the room it was in.
    if (cov != nullptr) {
        ++cov->cases;
    }

    for (uint32_t step = 0;; ++step) {
        const auto phase = static_cast<RunPhase>(rc.phase);

        // Pass-A observation of the PRE-step controller (fuzz_run.hpp
        // StepObserver). Only ever non-null for pass A; see the header for why
        // passes B/C must not call it.
        if (observer != nullptr && observer->fn != nullptr) {
            observer->fn(rc, observer->ctx);
        }

        // --- phase-transition accounting -------------------------------------
        if (cov != nullptr && rc.phase != prev_phase) {
            // Leaving a combat: file the fight's depth. Combats are the unit
            // here (not cases), because "did fights reach later turns" is the
            // coverage question and one case can hold several fights.
            if (prev_phase == static_cast<uint8_t>(RunPhase::COMBAT) &&
                combat_max_turn > 0) {
                ++cov->turn_bucket[turn_bucket_of(combat_max_turn)];
                combat_max_turn = 0;
            }
            if (phase == RunPhase::COMBAT) {
                ++cov->combats_entered;
                combat_max_turn = 0;
                for (int m = 0; m < rc.combat.monster_count; ++m) {
                    cov->monsters_fought.set(rc.combat.monsters[m].monster_id);
                }
            } else if (phase == RunPhase::COMBAT_REWARD) {
                ++cov->reward_screens;
                const auto oc =
                    static_cast<engine::RunCombatOutcome>(rc.combat_outcome);
                if (oc == engine::RunCombatOutcome::KILLED) ++cov->combats_killed;
                if (oc == engine::RunCombatOutcome::SMOKE_BOMB) {
                    ++cov->combats_smoked;
                    ++cov->escapes;
                }
            } else if (phase == RunPhase::RUN_OVER) {
                ++cov->deaths;
            }
            if (rc.room_type < kRoomTypeCount &&
                (phase == RunPhase::COMBAT ||
                 phase == RunPhase::REST_SITE ||
                 phase == RunPhase::TREASURE_ROOM ||
                 phase == RunPhase::EVENT_DIALOG ||
                 phase == RunPhase::ROOM_UNIMPLEMENTED)) {
                ++cov->room_entered[rc.room_type];
            }
            prev_phase = rc.phase;
        }
        if (cov != nullptr && phase == RunPhase::COMBAT) {
            if (rc.combat.turn > combat_max_turn) combat_max_turn = rc.combat.turn;
            if (rc.combat.turn > cov->max_turn) {
                cov->max_turn = rc.combat.turn;
            }
            // Powers are cheap to scan: each actor stores a live count, and a
            // typical actor has 0-3.
            for (int k = 0; k < rc.combat.player_power_count; ++k) {
                cov->powers_applied.set(rc.combat.player_powers[k].power_id);
            }
            for (int m = 0; m < rc.combat.monster_count; ++m) {
                const engine::MonsterState& ms = rc.combat.monsters[m];
                for (int k = 0; k < ms.power_count; ++k) {
                    cov->powers_applied.set(ms.powers[k].power_id);
                }
            }
        }
        // --- terminal phases --------------------------------------------------
        if (phase == RunPhase::RUN_OVER) {
            p.end = EndReason::RUN_OVER;
            break;
        }
        if (phase == RunPhase::ROOM_UNIMPLEMENTED) {
            p.end = EndReason::ROOM_UNIMPLEMENTED;
            if (cov != nullptr && rc.room_type < kRoomTypeCount) {
                ++cov->room_stalled[rc.room_type];
            }
            break;
        }
        if (step >= lim.max_actions) {
            p.end = EndReason::ACTION_CAP;
            break;
        }

        // --- choose the action ------------------------------------------------
        engine::legal_actions(rc, mask);
        const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
        if (n == 0) {
            // An empty mask in a live phase means the run has nowhere to go
            // while claiming not to be terminal. Report, do not paper over.
            p.end = EndReason::NO_LEGAL_MOVES;
            p.failure.kind = FailKind::NO_LEGAL_MOVES;
            p.failure.step = step;
            p.failure.phase = rc.phase;
            break;
        }

        if (cov != nullptr) {
            // "Was this category available?" -- once per step, not once per move,
            // so the denominator is steps and the number is readable.
            bool seen[static_cast<int>(MoveCat::COUNT)]{};
            for (size_t i = 0; i < n; ++i) seen[static_cast<int>(moves[i].cat)] = true;
            for (int c = 0; c < static_cast<int>(MoveCat::COUNT); ++c) {
                if (seen[c]) ++cov->move_legal[c];
            }
        }

        Action chosen{};
        MoveCat chosen_cat = MoveCat::END_TURN;
        if (scripted != nullptr) {
            if (step >= scripted->size()) {
                // The recorded log ran out before the run ended. For pass C
                // that is the honest stopping point, not a failure: pass A
                // stopped here too (cap / terminal), and the comparison of
                // hashes up to here is what matters.
                p.end = EndReason::ACTION_CAP;
                break;
            }
            chosen = (*scripted)[step];
            // The reproducer's action must still be one the engine calls legal.
            bool legal = false;
            for (size_t i = 0; i < n; ++i) {
                if (moves[i].action.bits == chosen.bits) {
                    legal = true;
                    chosen_cat = moves[i].cat;
                    break;
                }
            }
            if (!legal) {
                p.failure.kind = FailKind::NO_LEGAL_MOVES;
                p.failure.step = step;
                p.failure.phase = rc.phase;
                p.failure.action_a = chosen.bits;
                p.end = EndReason::NO_LEGAL_MOVES;
                break;
            }
        } else {
            const size_t pick = policy_pick(id.policy, rc, moves, n, rng);
            chosen = moves[pick].action;
            chosen_cat = moves[pick].cat;
        }

        // --- pre-step coverage that needs the state BEFORE the action ---------
        if (cov != nullptr) {
            ++cov->move_taken[static_cast<int>(chosen_cat)];
            switch (chosen_cat) {
                case MoveCat::PLAY_CARD:
                case MoveCat::PLAY_CARD_TARGET: {
                    const uint8_t slot = engine::action_arg0(chosen);
                    if (slot < rc.combat.hand_count) {
                        cov->cards_played.set(
                            rc.combat.card_pool[rc.combat.hand[slot]].card_id);
                    }
                    break;
                }
                case MoveCat::USE_POTION:
                case MoveCat::USE_POTION_TARGET: {
                    const uint8_t slot = engine::action_arg0(chosen);
                    if (slot < engine::kPotionCap) {
                        cov->potions_held.set(rc.run.potions[slot]);
                    }
                    ++cov->potions_used;
                    break;
                }
                case MoveCat::REWARD_CLAIM: {
                    const uint8_t idx = engine::action_arg0(chosen);
                    if (idx < engine::kRewardItemCap) {
                        const uint8_t k = rc.rewards.items[idx].kind;
                        if (k < kRewardKindCount) ++cov->reward_claimed[k];
                    }
                    break;
                }
                case MoveCat::REWARD_TAKE_CARD: ++cov->cards_taken; break;
                case MoveCat::REWARD_SKIP_CARD: ++cov->cards_skipped; break;
                default: break;
            }
        }

        // --- step -------------------------------------------------------------
        const uint64_t before = hash_controller(rc);
        RunController before_rc{};
        if (inject != nullptr && inject->enabled && inject->no_progress &&
            step == inject->at_step) {
            before_rc = rc;
        }
        {
            std::span<RunController> rs(&rc, 1);
            std::span<const Action> as(&chosen, 1);
            std::span<StepResult> os(results, 1);
            engine::advance(rs, as, os);
        }
        if (inject != nullptr && inject->enabled && step == inject->at_step) {
            if (inject->abort_process) {
                // Deliberate process-level fault injection. Do not catch or
                // translate this: the acceptance property is that the soak's
                // pre-case journal remains actionable when an engine assert,
                // sanitizer finding, SIGSEGV, or abort prevents normal triage.
                std::fflush(nullptr);
                std::abort();
            }
            if (inject->no_progress) {
                rc = before_rc;
            } else {
                // Driver-side injected divergence (fuzz_run.hpp Inject): a real
                // state change the comparator sees exactly as it would see an
                // engine nondeterminism.
                rc.run.gold += 1;
            }
        }
        StepHash sh;
        sh.whole = hash_controller(rc, &sh.run, &sh.combat);
        const uint64_t after = sh.whole;

        p.actions.push_back(chosen);
        p.hashes.push_back(sh);

        // A one-state cycle is never a legitimate transition: it means an
        // action enumerated from legal_actions() was ignored by advance().
        // Report it immediately. Longer cycles (notably reward claim/skip)
        // remain ordinary LIVELOCK accounting below.
        if (after == before) {
            p.end = EndReason::NO_PROGRESS;
            p.failure.kind = FailKind::NO_PROGRESS;
            p.failure.step = step;
            p.failure.phase = rc.phase;
            p.failure.action_a = chosen.bits;
            p.failure.hash_a = before;
            p.failure.hash_b = after;
            break;
        }

        bool seen = false;
        for (uint32_t k = 0; k < recent_n; ++k) {
            if (recent[k] == after) { seen = true; break; }
        }
        recent[step % kRevisitWindow] = after;
        if (recent_n < kRevisitWindow) ++recent_n;
        if (seen) {
            if (++revisits >= lim.revisit_limit) {
                p.end = EndReason::LIVELOCK;
                if (lim.fail_on_livelock) {
                    p.failure.kind = FailKind::NO_PROGRESS;
                    p.failure.step = step;
                    p.failure.phase = rc.phase;
                    p.failure.action_a = chosen.bits;
                }
                break;
            }
        } else {
            revisits = 0;
        }
    }

    // The terminal controller. The loop's own observation happens BEFORE each
    // step, so the states reached by the NO_PROGRESS / LIVELOCK exits (which
    // break after advance()) would otherwise never be seen. This deliberately
    // re-observes the terminal state on the phase-terminal exits, which is why
    // the header requires every observer-derived property to be idempotent.
    if (observer != nullptr && observer->fn != nullptr) {
        observer->fn(rc, observer->ctx);
    }

    p.final_hash = hash_controller(rc);

    if (cov != nullptr) {
        const auto acts = static_cast<uint32_t>(p.actions.size());
        cov->actions += acts;
        if (acts > cov->max_actions_in_case) cov->max_actions_in_case = acts;
        ++cov->end_reason[static_cast<int>(p.end)];
        cov->end_actions[static_cast<int>(p.end)] += acts;
        ++cov->per_policy_cases[static_cast<int>(id.policy)];
        cov->per_policy_actions[static_cast<int>(id.policy)] += acts;
        // A case that ended INSIDE a combat (death / cap) still has a fight to
        // file; the leaving-combat branch above never fired for it.
        if (combat_max_turn > 0) {
            ++cov->turn_bucket[turn_bucket_of(combat_max_turn)];
        }
        const uint32_t fl = rc.run.floor;
        ++cov->floor_bucket[fl < kFloorBuckets ? fl : kFloorBuckets - 1];
        if (fl > cov->max_floor) cov->max_floor = fl;
        for (int i = 0; i < rc.run.relic_count; ++i) {
            cov->relics_owned.set(rc.run.relics[i].relic_id);
        }
        cov->relics_gained += rc.run.relic_count;
        for (int i = 0; i < engine::kPotionCap; ++i) {
            if (rc.run.potions[i] != 0) cov->potions_held.set(rc.run.potions[i]);
        }
    }
}

}  // namespace

bool replay_actions(const CaseId& id, const std::vector<Action>& actions,
                    std::vector<uint64_t>& hashes, Failure& failure) {
    RunLimits lim;
    lim.max_actions = static_cast<uint32_t>(actions.size()) + 1;
    Pass p;
    execute(id, lim, nullptr, p, &actions, nullptr);
    hashes.clear();
    hashes.reserve(p.hashes.size());
    for (const StepHash& h : p.hashes) hashes.push_back(h.whole);
    failure = p.failure;
    return !static_cast<bool>(failure);
}

bool run_case(const CaseId& id, const RunLimits& limits, Coverage* cov, CaseResult& out,
              bool verify_repro, const Inject& inject, const StepObserver& observer) {
    out = CaseResult{};

    // --- pass A: the trajectory of record --------------------------------------
    Pass a;
    execute(id, limits, cov, a, nullptr, nullptr, &observer);
    out.end_reason = a.end;
    out.actions = static_cast<uint32_t>(a.actions.size());
    out.final_hash = a.final_hash;
    out.trajectory = a.actions;
    if (cov != nullptr) {
        ++cov->runs;
        cov->actions_engine += a.actions.size();
    }
    if (a.failure) {
        out.failure = a.failure;
        return false;
    }

    // --- pass B: re-derived from the case id alone ------------------------------
    Pass b;
    execute(id, limits, nullptr, b, nullptr, inject.enabled ? &inject : nullptr);
    if (cov != nullptr) {
        ++cov->runs;
        cov->actions_engine += b.actions.size();
    }

    auto compare = [&out](const Pass& x, const Pass& y, FailKind on_hash) -> bool {
        const size_t n = x.actions.size() < y.actions.size() ? x.actions.size()
                                                             : y.actions.size();
        for (size_t i = 0; i < n; ++i) {
            if (x.actions[i].bits != y.actions[i].bits) {
                out.failure.kind = FailKind::ACTION_MISMATCH;
                out.failure.step = static_cast<uint32_t>(i);
                out.failure.action_a = x.actions[i].bits;
                out.failure.action_b = y.actions[i].bits;
                out.failure.hash_a = i > 0 ? x.hashes[i - 1].whole : 0;
                out.failure.hash_b = i > 0 ? y.hashes[i - 1].whole : 0;
                return false;
            }
            if (x.hashes[i].whole != y.hashes[i].whole) {
                out.failure.kind = on_hash;
                out.failure.step = static_cast<uint32_t>(i);
                out.failure.action_a = x.actions[i].bits;
                out.failure.action_b = y.actions[i].bits;
                out.failure.hash_a = x.hashes[i].whole;
                out.failure.hash_b = y.hashes[i].whole;
                out.failure.run_hash_a = x.hashes[i].run;
                out.failure.run_hash_b = y.hashes[i].run;
                out.failure.combat_hash_a = x.hashes[i].combat;
                out.failure.combat_hash_b = y.hashes[i].combat;
                return false;
            }
        }
        if (x.actions.size() != y.actions.size()) {
            out.failure.kind = FailKind::LENGTH_MISMATCH;
            out.failure.step = static_cast<uint32_t>(n);
            return false;
        }
        if (x.final_hash != y.final_hash) {
            // The spec's literal bar (stage-a §2): final-state hashes compared.
            out.failure.kind = on_hash;
            out.failure.step = static_cast<uint32_t>(n);
            out.failure.hash_a = x.final_hash;
            out.failure.hash_b = y.final_hash;
            return false;
        }
        return true;
    };

    if (b.failure) {
        out.end_reason = b.end;
        out.failure = b.failure;
        return false;
    }
    if (!compare(a, b, FailKind::HASH_MISMATCH)) {
        return false;
    }

    // --- pass C: the reproducer file's own path ---------------------------------
    // Sampled during a clean soak so the triage path is continuously exercised
    // rather than first tested on the day it is needed.
    if (verify_repro) {
        Pass c;
        RunLimits clim = limits;
        clim.max_actions = static_cast<uint32_t>(a.actions.size());
        execute(id, clim, nullptr, c, &a.actions, nullptr);
        if (cov != nullptr) {
            ++cov->runs;
            cov->actions_engine += c.actions.size();
        }
        if (c.failure) {
            out.failure = c.failure;
            out.failure.kind = FailKind::REPRO_MISMATCH;
            return false;
        }
        if (!compare(a, c, FailKind::REPRO_MISMATCH)) {
            return false;
        }
    }
    return true;
}

std::string triage_text(const CaseId& id, const CaseResult& r) {
    std::ostringstream os;
    os << "=== FUZZ FAILURE: " << fail_kind_name(r.failure.kind) << " ===\n";
    os << "case: " << case_id_line(id) << "\n";
    os << "step: " << r.failure.step << " of " << r.actions
       << "   phase=" << static_cast<int>(r.failure.phase) << "\n";
    if (r.failure.kind == FailKind::ACTION_MISMATCH) {
        os << "action A: " << decode_action(Action{r.failure.action_a}) << "\n";
        os << "action B: " << decode_action(Action{r.failure.action_b}) << "\n";
    } else if (r.failure.action_a != 0 || r.failure.kind == FailKind::NO_PROGRESS) {
        os << "action:   " << decode_action(Action{r.failure.action_a}) << "\n";
    }
    if (r.failure.hash_a != r.failure.hash_b) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "hash A:   %016llx\nhash B:   %016llx\n",
                      static_cast<unsigned long long>(r.failure.hash_a),
                      static_cast<unsigned long long>(r.failure.hash_b));
        os << buf;
        // Localize: which region of the controller actually moved.
        const bool run_moved = r.failure.run_hash_a != r.failure.run_hash_b;
        const bool combat_moved = r.failure.combat_hash_a != r.failure.combat_hash_b;
        os << "region:   ";
        if (run_moved && combat_moved) os << "RunState AND CombatState";
        else if (run_moved) os << "RunState only";
        else if (combat_moved) os << "CombatState only";
        else os << "neither engine state -- the transient screen flow "
                   "(phase / cursors / reward screen / encounter lists)";
        os << "\n";
        if (run_moved) {
            std::snprintf(buf, sizeof(buf),
                          "  RunState    A=%016llx B=%016llx\n",
                          static_cast<unsigned long long>(r.failure.run_hash_a),
                          static_cast<unsigned long long>(r.failure.run_hash_b));
            os << buf;
        }
        if (combat_moved) {
            std::snprintf(buf, sizeof(buf),
                          "  CombatState A=%016llx B=%016llx\n",
                          static_cast<unsigned long long>(r.failure.combat_hash_a),
                          static_cast<unsigned long long>(r.failure.combat_hash_b));
            os << buf;
        }
    }
    os << "action prefix (" << (r.failure.step + 1 <= r.trajectory.size()
                                    ? r.failure.step + 1
                                    : r.trajectory.size())
       << " actions):\n";
    const size_t upto = r.failure.step + 1 <= r.trajectory.size()
                            ? r.failure.step + 1
                            : r.trajectory.size();
    for (size_t i = 0; i < upto; ++i) {
        os << "  [" << i << "] " << decode_action(r.trajectory[i]) << "\n";
    }
    return os.str();
}

}  // namespace sts::fuzz
