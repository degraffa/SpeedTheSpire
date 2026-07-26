// fuzz_repro -- replay ONE fuzz case, standalone.
//
// This is the half of B5.1 that makes the other half worth anything. It links
// the engine, the policy and the reproducer parser; it does NOT link the soak
// driver, does not sweep, does not thread, and does not need any of the soak's
// state. Given either
//
//   * a `STSFUZZ v1` file (hash-mismatch triage), or
//   * a bare case id on the command line (crash triage, from the in-flight
//     journal the soak leaves behind),
//
// it rebuilds the run, steps it, and prints the per-step hash chain. The two
// inputs are cross-checked against each other with `--regen`, which re-derives
// the action list from the case id and compares it to the file's list -- so the
// redundancy in the format is a checked redundancy, not a hopeful one.
//
// Exit codes: 0 replayed clean and matched, 1 a mismatch/failure was
// reproduced, 2 bad usage or an unreadable file.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/repro.hpp"

namespace {

using namespace sts::fuzz;

void usage() {
    std::fprintf(stderr, R"(fuzz_repro -- replay one fuzz case (B5.1)

  fuzz_repro FILE.repro [options]
  fuzz_repro --seed N --policy NAME --policy-seed N [--ascension N] [options]

  --regen           re-derive the action list from the case id and require it to
                    equal the file's list (cross-checks the two reproducer forms)
  --verbose         print every step: action + controller hash
  --max-actions N   run cap when driving from a case id (default 4000)
  --emit PATH       write the replayed trajectory back out as a STSFUZZ v1 file

exit: 0 clean, 1 failure reproduced, 2 usage/IO
)");
}

}  // namespace

int main(int argc, char** argv) {
    std::string file;
    CaseId id;
    bool have_case = false;
    bool regen = false;
    bool verbose = false;
    std::string emit;
    uint32_t max_actions = 4000;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--regen") regen = true;
        else if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--emit") emit = next("--emit");
        else if (a == "--max-actions") max_actions = static_cast<uint32_t>(std::strtoul(next("--max-actions").c_str(), nullptr, 10));
        else if (a == "--seed") { id.run_seed = std::strtoll(next("--seed").c_str(), nullptr, 10); have_case = true; }
        else if (a == "--ascension") id.ascension = static_cast<uint8_t>(std::strtoul(next("--ascension").c_str(), nullptr, 10));
        else if (a == "--policy") {
            const std::string n = next("--policy");
            if (!policy_from_name(n, id.policy)) { std::fprintf(stderr, "unknown policy '%s'\n", n.c_str()); return 2; }
            have_case = true;
        }
        else if (a == "--policy-seed") { id.policy_seed = std::strtoull(next("--policy-seed").c_str(), nullptr, 10); have_case = true; }
        else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "unknown argument '%s'\n", a.c_str()); usage(); return 2; }
        else file = a;
    }

    ReproFile rf;
    bool from_file = false;
    if (!file.empty()) {
        std::string err;
        if (!read_fuzz_repro(file, rf, err)) {
            std::fprintf(stderr, "cannot read reproducer: %s\n", err.c_str());
            return 2;
        }
        id = rf.id;
        from_file = true;
        std::cout << "loaded " << file << ": " << case_id_line(id) << ", "
                  << rf.actions.size() << " actions";
        if (!rf.fail_kind.empty()) {
            std::cout << ", recorded failure '" << rf.fail_kind << "' at step "
                      << rf.fail_step;
        }
        std::cout << "\n";
    } else if (!have_case) {
        usage();
        return 2;
    }

    int rc = 0;

    // --- 1. re-derive the trajectory from the case id -------------------------
    // This is the crash-triage path: four values in, the exact trajectory out.
    RunLimits limits;
    limits.max_actions = from_file ? static_cast<uint32_t>(rf.actions.size()) + 1
                                   : max_actions;
    CaseResult res;
    const bool clean = run_case(id, limits, nullptr, res, /*verify_repro=*/true);
    std::cout << "regenerated from case id: " << res.actions << " actions, end="
              << end_reason_name(res.end_reason) << ", final hash=";
    std::printf("%016llx\n", static_cast<unsigned long long>(res.final_hash));
    {
        // Region breakdown of the run's STARTING state. Two reproducers that
        // disagree here disagree about run_begin, not about any action -- which
        // is worth knowing before reading 300 steps.
        const ControllerHashes p = hash_controller_parts(
            sts::engine::run_begin(id.run_seed, id.ascension));
        std::printf("  floor-0 regions: run=%016llx combat=%016llx lists=%016llx "
                    "rewards=%016llx scalars=%016llx\n",
                    static_cast<unsigned long long>(p.run),
                    static_cast<unsigned long long>(p.combat),
                    static_cast<unsigned long long>(p.lists),
                    static_cast<unsigned long long>(p.rewards),
                    static_cast<unsigned long long>(p.scalars));
    }
    if (!clean) {
        std::cout << triage_text(id, res);
        rc = 1;
    }

    // --- 2. replay the file's literal action list -----------------------------
    if (from_file) {
        std::vector<uint64_t> hashes;
        Failure fail;
        const bool ok = replay_actions(id, rf.actions, hashes, fail);
        std::cout << "literal replay: " << hashes.size() << " steps";
        if (!hashes.empty()) {
            std::printf(", last hash=%016llx",
                        static_cast<unsigned long long>(hashes.back()));
        }
        std::cout << "\n";
        if (!ok) {
            std::cout << "  !! the recorded action at step " << fail.step
                      << " is no longer legal: " << decode_action(
                             sts::engine::Action{fail.action_a})
                      << "\n";
            rc = 1;
        }
        if (verbose) {
            for (size_t i = 0; i < rf.actions.size() && i < hashes.size(); ++i) {
                std::printf("  [%4zu] %-34s %016llx\n", i,
                            decode_action(rf.actions[i]).c_str(),
                            static_cast<unsigned long long>(hashes[i]));
            }
        }
        if (rf.has_hashes && rf.fail_step < hashes.size()) {
            const uint64_t got = hashes[rf.fail_step];
            std::printf("recorded hash_a=%016llx  hash_b=%016llx  replayed=%016llx\n",
                        static_cast<unsigned long long>(rf.hash_a),
                        static_cast<unsigned long long>(rf.hash_b),
                        static_cast<unsigned long long>(got));
            if (got != rf.hash_a && got != rf.hash_b) {
                // Neither branch: the reproducer no longer describes anything
                // this build produces. That is the loudest outcome available,
                // and it has exactly two causes worth checking in this order.
                std::cout << "  !! replay matched NEITHER recorded hash.\n"
                             "     Either the engine changed since the reproducer "
                             "was written (check the commit it came from), or the\n"
                             "     fault depends on something outside the case id "
                             "(process-global state, ASLR, uninitialised memory).\n";
                rc = 1;
            } else {
                std::cout << "  replay took the pass-" << (got == rf.hash_a ? "A" : "B")
                          << " branch of the recorded divergence.\n";
            }
        }

        // --- 3. cross-check the two reproducer forms --------------------------
        if (regen) {
            bool same = res.trajectory.size() >= rf.actions.size();
            if (same) {
                for (size_t i = 0; i < rf.actions.size(); ++i) {
                    if (res.trajectory[i].bits != rf.actions[i].bits) { same = false; break; }
                }
            }
            std::cout << "--regen: case-id-derived prefix "
                      << (same ? "MATCHES" : "DIFFERS FROM")
                      << " the file's action list\n";
            if (!same) rc = 1;
        }
    } else if (verbose) {
        std::vector<uint64_t> hashes;
        Failure fail;
        (void)replay_actions(id, res.trajectory, hashes, fail);
        for (size_t i = 0; i < res.trajectory.size() && i < hashes.size(); ++i) {
            std::printf("  [%4zu] %-34s %016llx\n", i,
                        decode_action(res.trajectory[i]).c_str(),
                        static_cast<unsigned long long>(hashes[i]));
        }
    }

    if (!emit.empty()) {
        ReproFile out;
        out.id = id;
        out.actions = res.trajectory;
        out.final_hash = res.final_hash;
        out.has_hashes = true;
        if (!write_fuzz_repro(emit, out)) {
            std::fprintf(stderr, "cannot write %s\n", emit.c_str());
            return 2;
        }
        std::cout << "wrote " << emit << "\n";
    }

    // --- verdict ---------------------------------------------------------------
    // "clean" is only allowed to mean clean. A file that RECORDS a failure and
    // then replays without one is not a pass: it is a failure that did not
    // recur, which is a different and more annoying situation, and saying
    // "clean" there would be the single most misleading thing this tool could
    // print.
    if (!from_file || rf.fail_kind.empty()) {
        std::cout << (rc == 0 ? "RESULT: clean\n" : "RESULT: failure reproduced\n");
    } else if (rc != 0) {
        std::cout << "RESULT: reproduced -- the recorded '" << rf.fail_kind
                  << "' is live in this build\n";
    } else {
        std::cout << "RESULT: NOT REPRODUCED -- the file records '" << rf.fail_kind
                  << "' at step " << rf.fail_step
                  << ", but this process replayed it without a divergence.\n"
                     "        The trajectory is right (the action list replays and "
                     "matches a recorded branch),\n"
                     "        so the fault is not a pure function of the case id. "
                     "Re-run the soak, or run it under asan.\n";
        rc = 1;
    }
    return rc;
}
