#pragma once

// The FUZZ CASE IDENTITY -- the four values that completely determine one fuzz
// run, and therefore the smallest thing that can be called a reproducer.
//
// WHY THIS IS THE CENTRE OF THE TOOL. A fuzzer that finds a crash but cannot
// say which run crashed is a random number generator. Everything here is
// arranged so that (run_seed, ascension, policy, policy_seed) regenerates a
// byte-identical trajectory: the engine is deterministic given its seed, and
// the POLICY is deterministic given `policy_seed` (its RNG is a private
// splitmix64 -- sts::fuzz::PolicyRng -- that never touches an engine stream).
// So a crash needs no action log at all: the four-tuple is enough, and the
// soak writes it to an in-flight journal BEFORE stepping the case so an
// abort/segv leaves the identity on disk.
//
// The literal action list is still recorded and emitted for hash-mismatch
// triage, because it makes the reproducer replayable by something that does not
// link the policy code at all (see repro.hpp). The two forms are cross-checked
// against each other -- `fuzz_repro --regen` regenerates the actions from the
// case id and asserts they equal the recorded list.

#include <cstdint>
#include <string>

#include "sts/fuzz/policy.hpp"

namespace sts::fuzz {

struct CaseId {
    int64_t run_seed = 0;
    uint8_t ascension = 20;
    PolicyKind policy = PolicyKind::RANDOM;
    uint64_t policy_seed = 0;
};

// One-line human/grep-friendly spelling, e.g.
//   seed=123 asc=20 policy=greedy_damage pseed=456
[[nodiscard]] std::string case_id_line(const CaseId& c);

}  // namespace sts::fuzz
