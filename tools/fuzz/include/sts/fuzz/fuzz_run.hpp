#pragma once

// THE REPLAY-TWICE DETERMINISM GUARD (stage-a design §2, stage-b design
// §7.1(2)) and its triage output.
//
// stage-a §2 bought back Rust's memory-safety economics with two controls: an
// asan/ubsan preset, and "the fuzz farm replays every run twice and compares
// final state hashes; a silent stray write that survives asan shows up as a
// replay mismatch". This is that second control.
//
// WHAT IS COMPARED, AND WHY MORE THAN THE FINAL HASH. The spec's bar is the
// FINAL-state hash. This implementation keeps a per-step hash CHAIN as well,
// because a final-hash-only comparison answers "something went wrong somewhere
// in 400 steps" -- which is a bug report nobody can act on. The chain costs one
// XXH3 over ~6 KB per step (measured cost is a few percent of the step itself)
// and turns the answer into "step 137, and here are the 137 actions that get
// you there".
//
// THREE PASSES, EACH PROVING A DIFFERENT THING:
//   A  policy-driven, recorded          -- the trajectory of record
//   B  policy-driven, re-derived        -- proves the ENGINE and the POLICY are
//                                          both functions of the case id alone
//   C  driven by A's literal action log -- proves the REPRODUCER FILE replays
//                                          (sampled; always on failure)
//
// Pass C is the one that keeps the triage path honest. A reproducer format that
// is only exercised when something breaks is a format that has never been
// tested; running it on a sampled fraction of every soak means the emitted file
// is known to replay before anyone needs it to.
//
// WHAT COUNTS AS A FINDING. Beyond hash/action divergence, two shapes are
// reported rather than swallowed: an empty legal-action set in a non-terminal
// phase (NO_LEGAL_MOVES), and a legal action that repeatedly leaves the whole
// controller byte-identical (NO_PROGRESS). Both mean the mask and advance()
// disagree about what an action does, which is exactly the class of bug this
// tool exists to surface -- see the advance() gate's memory-safety note.

#include <cstdint>
#include <string>
#include <vector>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"
#include "sts/fuzz/case_id.hpp"
#include "sts/fuzz/coverage.hpp"

namespace sts::fuzz {

enum class FailKind : uint8_t {
    NONE = 0,
    HASH_MISMATCH = 1,     // pass B reached a different state at the same step
    ACTION_MISMATCH = 2,   // pass B's policy chose a different action (mask drift)
    LENGTH_MISMATCH = 3,   // pass B ran a different number of steps
    REPRO_MISMATCH = 4,    // pass C (literal action log) did not match pass A
    NO_LEGAL_MOVES = 5,    // empty mask in a non-terminal phase
    NO_PROGRESS = 6,       // legal actions stopped mutating the controller
    COUNT = 7,
};

[[nodiscard]] const char* fail_kind_name(FailKind k) noexcept;

struct Failure {
    FailKind kind = FailKind::NONE;
    uint32_t step = 0;         // 0-based index of the first divergent step
    uint64_t hash_a = 0;       // whole-RunController hash, pass A
    uint64_t hash_b = 0;       // ... pass B/C
    uint64_t run_hash_a = 0;   // hash_state(RunState)  -- localizes the divergence
    uint64_t run_hash_b = 0;
    uint64_t combat_hash_a = 0;  // hash_state(CombatState)
    uint64_t combat_hash_b = 0;
    uint32_t action_a = 0;     // Action.bits at the divergent step
    uint32_t action_b = 0;
    uint8_t phase = 0;         // RunPhase at the divergent step

    [[nodiscard]] explicit operator bool() const noexcept {
        return kind != FailKind::NONE;
    }
};

struct RunLimits {
    uint32_t max_actions = 4000;    // per run; a stalemate combat is not a bug
    // Livelock detection. The last `kRevisitWindow` controller hashes are kept;
    // a step whose result was already in that window counts as a revisit, and
    // `revisit_limit` consecutive revisits ends the run as LIVELOCK. This
    // generalizes a zero-delta detector (which only catches 1-cycles) to the
    // 2-cycle the reward screen legitimately admits -- see coverage.hpp.
    uint32_t revisit_limit = 64;
    bool fail_on_livelock = false;  // promote LIVELOCK to a reported failure
};

inline constexpr uint32_t kRevisitWindow = 64;

// Driver-side fault injection. This exists so the triage path can be exercised
// on demand -- by the unit test and by anyone auditing the tool -- WITHOUT
// patching the engine. It perturbs pass B only, after the given step, which the
// comparator cannot distinguish from a genuine engine nondeterminism.
struct Inject {
    bool enabled = false;
    // Abort the whole process instead of perturbing pass B. This exercises the
    // only triage path available when an engine assert/ASan finding prevents
    // normal unwinding: the already-flushed in-flight journal.
    bool abort_process = false;
    uint32_t at_step = 0;
};

struct CaseResult {
    EndReason end_reason = EndReason::RUN_OVER;
    uint32_t actions = 0;
    uint64_t final_hash = 0;
    Failure failure;
    std::vector<engine::Action> trajectory;  // pass A, complete
};

// Execute one case: pass A (recorded, coverage-counted), pass B (re-derived),
// and -- when `verify_repro` -- pass C over pass A's literal action log.
// Returns true when the case is clean. `cov` may be null.
bool run_case(const CaseId& id, const RunLimits& limits, Coverage* cov, CaseResult& out,
              bool verify_repro, const Inject& inject = Inject{});

// Replay a LITERAL action list against a fresh run_begin(seed, ascension) --
// the standalone reproducer path, with no policy involved. Every action is
// checked against the engine's own legal_actions() before it is applied; an
// action the mask rejects fills `failure` with NO_LEGAL_MOVES at that step
// (a reproducer that no longer replays is itself information).
// Fills `hashes` with the per-step whole-controller hash chain.
bool replay_actions(const CaseId& id, const std::vector<engine::Action>& actions,
                    std::vector<uint64_t>& hashes, Failure& failure);

// Whole-RunController CONTENT hash: the two engine states through
// engine::hash_state, plus the transient screen-flow fields (phase, cursors,
// reward screen, encounter lists) that RunState/CombatState do not cover.
//
// It is emphatically NOT a byte hash of the struct. RunController embeds
// `std::string_view` encounter keys, so its bytes contain POINTERS whose values
// move with ASLR -- a byte hash is stable within one process and different in
// the next, which is the one failure mode a determinism guard must not have.
// The full story, and how it was found, is at the definition in fuzz_run.cpp.
// `run_h`/`combat_h` (optional) receive the two engine::hash_state values the
// combined hash is built from -- free, since they are computed anyway.
uint64_t hash_controller(const engine::RunController& rc, uint64_t* run_h,
                         uint64_t* combat_h) noexcept;

[[nodiscard]] inline uint64_t hash_controller(const engine::RunController& rc) noexcept {
    return hash_controller(rc, nullptr, nullptr);
}

// The same content, split by region. Triage only: when `whole` diverges, this
// says WHICH part of the controller moved, which is the difference between
// "state 137 differs" and "the reward screen differs at state 137".
struct ControllerHashes {
    uint64_t whole = 0;
    uint64_t run = 0;       // engine::hash_state(RunState)
    uint64_t combat = 0;    // engine::hash_state(CombatState)
    uint64_t lists = 0;     // generated encounter lists
    uint64_t rewards = 0;   // live reward screen
    uint64_t scalars = 0;   // phase / cur_x / room_type / outcome / cursors
};

[[nodiscard]] ControllerHashes hash_controller_parts(
    const engine::RunController& rc) noexcept;

// Human-readable triage block for a failed case: the case id, the failure,
// both hashes, and the decoded action prefix. This is what gets printed to the
// soak log next to the path of the emitted reproducer file.
[[nodiscard]] std::string triage_text(const CaseId& id, const CaseResult& r);

}  // namespace sts::fuzz
