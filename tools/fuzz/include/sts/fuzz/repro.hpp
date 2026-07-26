#pragma once

// THE REPRODUCER FILE -- `STSFUZZ v1`.
//
// The whole value of a fuzz soak is concentrated here. A reproducer must let
// someone re-run exactly one failure WITHOUT the fuzzer, so this file carries
// both of the two independent ways to get back to it:
//
//   * the CASE ID (seed / ascension / policy / policy_seed) -- enough on its
//     own, because the engine and the policy are both pure functions of it.
//     This is the form that survives an abort: the soak writes it to an
//     in-flight journal BEFORE stepping a case, so a SIGSEGV/assert leaves the
//     identity on disk even though nothing got to write an action log.
//
//   * the LITERAL ACTION LIST -- replayable by `fuzz_repro`, which links the
//     engine and this parser and nothing else. No policy code, no fuzz driver.
//     `fuzz_repro --regen` re-derives the list from the case id and asserts the
//     two agree, so the redundancy is a checked one.
//
// Deliberately modelled on tools/diff_harness's STSREPRO v1 (same plain-text,
// hand-editable, `# decode` trailer shape) but a DISTINCT format: this one is
// run-level (ascension, policy, policy_seed) where STSREPRO is combat-level
// with a fixed skeleton deck. Sharing a version line between two formats with
// different bodies is how a parser starts silently accepting the wrong file.
//
// FORMAT
//
//   STSFUZZ v1
//   seed <int64 decimal>
//   ascension <decimal>
//   policy <name>
//   policy_seed <uint64 decimal>
//   fail <kind>                 # optional -- what the soak observed
//   fail_step <decimal>         # optional
//   hash_a <16 hex digits>      # optional -- pass-A controller hash at fail_step
//   hash_b <16 hex digits>      # optional
//   final_hash <16 hex digits>  # optional -- pass-A final controller hash
//   actions <count decimal>
//   <Action.bits decimal>   # <decoded verb + args>
//   ...
//
// The parser reads only the leading token of each action line; blank lines and
// '#' lines are ignored. An unknown key is an ERROR, not a shrug -- a v2 field
// silently dropped by a v1 reader is how a reproducer stops reproducing.

#include <cstdint>
#include <string>
#include <vector>

#include "sts/engine/types.hpp"
#include "sts/fuzz/case_id.hpp"

namespace sts::fuzz {

struct ReproFile {
    CaseId id;
    std::vector<engine::Action> actions;
    std::string fail_kind;   // empty when the file is not a failure record
    uint32_t fail_step = 0;
    uint64_t hash_a = 0;
    uint64_t hash_b = 0;
    uint64_t final_hash = 0;
    bool has_hashes = false;
};

[[nodiscard]] bool write_fuzz_repro(const std::string& path, const ReproFile& r);
[[nodiscard]] bool read_fuzz_repro(const std::string& path, ReproFile& out,
                                   std::string& error);

// One decoded action, e.g. "PLAY_CARD hand=2 target=1".
[[nodiscard]] std::string decode_action(engine::Action a);

}  // namespace sts::fuzz
