#pragma once

// STS-SCRIPT v1 -- the scripted-action-line artifact (S2.V2).
//
// WHAT IT IS. One selected (seed, policy, policy_seed) triple's COMPLETE
// decision sequence, re-derived from the sim and written in a vocabulary a
// LIVE driver can match against a CommunicationMod dump: screen kind plus the
// STABLE IDENTITY of the chosen thing (card game id + upgrade count +
// same-identity ordinal, monster index, reward-row kind + payload id, rest
// option name, event option index in the game's own enabled-only `choose`
// space). The script-following policy binary
// (tools/oracle_bridge/driver/script_policy_cmd.py) replays a script against
// the live game over the STS-POLICY-IO v1 seam; on ANY mismatch between the
// dump's advertised choices and the script's next step it STOPS the run as
// divergent -- a desync is capture evidence for Stage-B triage, never
// something to improvise around. That contract is why identities, not raw
// indices, are the vocabulary: an index mismatch that still names the same
// card is a presentation difference, while a different card at the same index
// is exactly the desync the stop exists to catch.
//
// WHY IT IS EMITTED BY REPLAY. scan_case's pass A records the winning line as
// engine Actions (fuzz::CaseResult::trajectory). Those bits are meaningless to
// a live driver (a CHOOSE arg0 is screen-relative), so the emitter REPLAYS the
// trajectory from run_begin and decodes every action against the state it was
// taken in -- the same direction the oracle replay harness maps live commands
// to sim actions (tools/oracle_bridge/replay/src/command_map.hpp), inverted.
// The replay is checked whole: after the last step the re-driven controller's
// fuzz::hash_controller must equal the scanned row's final_hash, so an
// emitted script is proven to reproduce the exact terminal state the triple
// was selected on (a mismatch aborts emission loudly -- it would mean the
// trajectory and the engine disagree, which is a finding, not a formatting
// problem).
//
// FORMAT. JSON Lines. Line 1 is the header object:
//
//   {"format":"STS-SCRIPT v1","seed":"STS00001","seed_int":N,"ascension":20,
//    "policy":"sim_search","policy_seed":0,"engine_schema":8,
//    "steps":N,"final_hash":"<16 hex>","end_reason":"run_over",
//    "victory":false,"max_floor":N}
//
// then one step object per decision, in order:
//
//   {"i":N,"floor":N,"act":N,"phase":"COMBAT","k":"<kind>", ...}
//
// Step kinds and their identity fields are documented in the planner README
// (the schema's normative home); the Python matcher in script_policy_cmd.py
// implements the live-side join per kind and is unit-tested against recorded
// protocol dumps without launching the game.

#include <cstdint>
#include <string>
#include <vector>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/types.hpp"

namespace sts::planner {

inline constexpr const char* kScriptFormat = "STS-SCRIPT v1";

struct ScriptEmit {
    bool ok = false;
    std::string error;            // set when !ok: which step failed and why
    std::vector<std::string> lines;  // header + one line per decision
};

// Replay `trajectory` from run_begin(seed, ascension) and emit the script.
// Deterministic: byte-identical output for identical inputs.
[[nodiscard]] ScriptEmit emit_script(int64_t seed_value,
                                     const std::string& seed_text,
                                     uint8_t ascension,
                                     const char* policy_name,
                                     uint64_t policy_seed,
                                     const std::vector<engine::Action>& trajectory,
                                     const char* end_reason,
                                     uint64_t final_hash);

// One decision decoded to its STS-SCRIPT step object (no trailing newline).
// Used by emit_script and by seed_scan's --trace-line debug mode.
[[nodiscard]] std::string script_step_json(const engine::RunController& rc,
                                           engine::Action a,
                                           uint32_t index,
                                           std::string& error);

}  // namespace sts::planner
