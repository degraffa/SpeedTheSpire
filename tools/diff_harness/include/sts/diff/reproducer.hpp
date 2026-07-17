#pragma once

// Reproducer emitter (design doc §8: "auto-saved (seed, action-prefix)
// reproducer ... reproducer file replays to the same diff"). When the diff
// harness finds a divergence at step k of a fight, it saves the minimal input
// needed to replay exactly that divergence: the run seed and the action prefix
// up to and including the diverging step. A human (or a future automated
// bisector) loads it, replays combat_begin + advance over the prefix, and hits
// the identical DiffReport.
//
// FILE FORMAT (plain text, human-readable and hand-editable -- a reproducer is
// something a person opens):
//
//   STSREPRO v1
//   seed <int64 decimal>
//   actions <count decimal>
//   <bits decimal>   # <decoded verb + args>
//   <bits decimal>   # ...
//   ...
//
// The `# ...` trailer on each action line is a decode for the reader's benefit;
// the parser reads only the leading integer (Action.bits) and ignores the rest
// of the line. Blank lines and lines beginning with '#' are ignored.
//
// The deck and floor are NOT stored: for the M1 skeleton they are fixed (the
// §9 Ironclad-vs-Jaw-Worm deck at replay.hpp's kSkeletonFloor). Stage B, when
// runs vary the deck, extends this format additively (a `deck ...` line);
// v1 readers of a v2 file would fail the version line cleanly.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sts/engine/types.hpp"

namespace sts::diff {

// Write a reproducer capturing (seed, action_prefix). Returns false on I/O
// failure.
[[nodiscard]] bool write_reproducer(const std::string& path, int64_t seed,
                                    std::span<const engine::Action> action_prefix);

// Read a reproducer written by write_reproducer. On success fills `seed_out` and
// `actions_out`. Returns false on I/O failure, a bad/unknown version line, or a
// malformed body (never a partial/garbage load).
[[nodiscard]] bool read_reproducer(const std::string& path, int64_t& seed_out,
                                   std::vector<engine::Action>& actions_out);

}  // namespace sts::diff
