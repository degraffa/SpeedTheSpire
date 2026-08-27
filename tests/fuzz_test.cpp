// B5.1 acceptance tests for tools/fuzz.
//
// The soak's own claims are the thing under test here, in this order of
// importance:
//
//   1. THE TRIAGE PATH WORKS. A deliberate, injected divergence must be
//      detected, must be reported at the right step, must emit a reproducer,
//      and that reproducer must replay. A triage path that has never been
//      exercised is not a triage path -- so it is exercised here, on every
//      build, rather than first tried on the day something breaks.
//   2. THE REPRODUCER IS SUFFICIENT. The four-value case id alone regenerates
//      the identical trajectory (the crash-triage path), and the literal action
//      list alone replays to the identical hash chain (the file path).
//   3. THE GUARD REALLY GUARDS. Clean cases pass; the comparator is not
//      vacuously true.
//   4. Coverage bookkeeping round-trips through the kv form (shard merging).

#include <gtest/gtest.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sts/engine/action_queue.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/monster_awakened_one.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/fuzz/coverage.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/policy.hpp"
#include "sts/fuzz/repro.hpp"

#include "host_shell.hpp"

using namespace sts::fuzz;
namespace engine = sts::engine;

namespace {

constexpr int64_t kSeed = 12345;

CaseId make_case(PolicyKind k, uint64_t pseed = 0xC0FFEEull) {
    CaseId id;
    id.run_seed = kSeed;
    id.ascension = 20;
    id.policy = k;
    id.policy_seed = pseed;
    return id;
}

RunLimits limits(uint32_t cap = 2000) {
    RunLimits l;
    l.max_actions = cap;
    return l;
}

std::string scratch(const std::string& name) {
    return std::string(STS_FUZZ_SCRATCH) + "/" + name;
}

// Host-shell portability lives in one place -- see tests/host_shell.hpp for the
// cmd.exe quoting rule, the null device, and which bash is safe to invoke.
using sts::testing::shell_quote;
using sts::testing::run_shell;
constexpr const char* kNull = sts::testing::kNullDevice;

std::string read_text(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(is),
                       std::istreambuf_iterator<char>());
}

}  // namespace

// --- 1. the guard actually guards -------------------------------------------

TEST(FuzzGuard, CleanCasePassesForEveryPolicy) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(PolicyKind::COUNT); ++i) {
        const CaseId id = make_case(static_cast<PolicyKind>(i));
        CaseResult r;
        Coverage cov;
        EXPECT_TRUE(run_case(id, limits(), &cov, r, /*verify_repro=*/true))
            << policy_name(id.policy) << ": " << triage_text(id, r);
        EXPECT_EQ(r.failure.kind, FailKind::NONE);
        EXPECT_GT(r.actions, 0u) << policy_name(id.policy) << " took no actions";
        EXPECT_EQ(r.trajectory.size(), r.actions);
    }
}

TEST(FuzzGuard, EveryPolicyIsAPureFunctionOfTheCaseId) {
    // Two independent executions of run_case must agree on the trajectory AND
    // the final hash -- this is the property that makes a four-value case id a
    // complete reproducer.
    for (uint8_t i = 0; i < static_cast<uint8_t>(PolicyKind::COUNT); ++i) {
        const CaseId id = make_case(static_cast<PolicyKind>(i));
        CaseResult a, b;
        ASSERT_TRUE(run_case(id, limits(), nullptr, a, false));
        ASSERT_TRUE(run_case(id, limits(), nullptr, b, false));
        EXPECT_EQ(a.final_hash, b.final_hash) << policy_name(id.policy);
        ASSERT_EQ(a.trajectory.size(), b.trajectory.size());
        for (size_t k = 0; k < a.trajectory.size(); ++k) {
            EXPECT_EQ(a.trajectory[k].bits, b.trajectory[k].bits)
                << policy_name(id.policy) << " step " << k;
        }
    }
}

TEST(FuzzGuard, DifferentPolicySeedsExploreDifferentTrajectories) {
    // If they did not, --reps would be free work and the sweep numbers would be
    // overstating what was explored.
    const CaseId a = make_case(PolicyKind::RANDOM, 1);
    const CaseId b = make_case(PolicyKind::RANDOM, 2);
    CaseResult ra, rb;
    ASSERT_TRUE(run_case(a, limits(), nullptr, ra, false));
    ASSERT_TRUE(run_case(b, limits(), nullptr, rb, false));
    EXPECT_NE(ra.final_hash, rb.final_hash);
}

// --- 2. the injected-divergence triage path ----------------------------------

TEST(FuzzTriage, InjectedDivergenceIsDetectedAtTheRightStep) {
    const CaseId id = make_case(PolicyKind::GREEDY_DAMAGE);
    CaseResult base;
    ASSERT_TRUE(run_case(id, limits(), nullptr, base, false));
    ASSERT_GT(base.actions, 4u) << "need a few steps to inject into";

    const uint32_t at = base.actions / 2;
    Inject inj;
    inj.enabled = true;
    inj.at_step = at;

    CaseResult r;
    EXPECT_FALSE(run_case(id, limits(), nullptr, r, false, inj));
    EXPECT_EQ(r.failure.kind, FailKind::HASH_MISMATCH);
    EXPECT_EQ(r.failure.step, at) << "the guard must name the FIRST divergent step";
    EXPECT_NE(r.failure.hash_a, r.failure.hash_b);
    // The triage block must actually carry the prefix a human replays.
    const std::string text = triage_text(id, r);
    EXPECT_NE(text.find("hash_mismatch"), std::string::npos);
    EXPECT_NE(text.find("seed=12345"), std::string::npos);
    EXPECT_NE(text.find("[0]"), std::string::npos);
}

TEST(FuzzTriage, LegalActionNoProgressFailsImmediately) {
    const CaseId id = make_case(PolicyKind::GREEDY_DAMAGE);
    Inject inject;
    inject.enabled = true;
    inject.no_progress = true;
    inject.at_step = 3;
    CaseResult r;
    EXPECT_FALSE(run_case(id, limits(), nullptr, r, false, inject));
    EXPECT_EQ(r.failure.kind, FailKind::NO_PROGRESS);
    EXPECT_EQ(r.end_reason, EndReason::NO_PROGRESS);
    EXPECT_EQ(r.failure.step, 3u);
    EXPECT_EQ(r.failure.hash_a, r.failure.hash_b);
}

TEST(FuzzTriage, EmittedReproducerFromAnInjectedFailureReplays) {
    // The full path, end to end: injected divergence -> reproducer file ->
    // read back -> replay the literal action list -> the recorded hash at the
    // recorded step is reproduced exactly.
    const CaseId id = make_case(PolicyKind::GREEDY_BLOCK);
    CaseResult base;
    ASSERT_TRUE(run_case(id, limits(), nullptr, base, false));
    ASSERT_GT(base.actions, 4u);

    Inject inj;
    inj.enabled = true;
    inj.at_step = base.actions / 2;
    CaseResult r;
    ASSERT_FALSE(run_case(id, limits(), nullptr, r, false, inj));
    ASSERT_EQ(r.failure.kind, FailKind::HASH_MISMATCH);

    ReproFile rf;
    rf.id = id;
    rf.actions.assign(r.trajectory.begin(),
                      r.trajectory.begin() + static_cast<long>(r.failure.step) + 1);
    rf.fail_kind = fail_kind_name(r.failure.kind);
    rf.fail_step = r.failure.step;
    rf.hash_a = r.failure.hash_a;
    rf.hash_b = r.failure.hash_b;
    rf.final_hash = r.final_hash;
    rf.has_hashes = true;

    const std::string path = scratch("injected.repro");
    ASSERT_TRUE(write_fuzz_repro(path, rf));

    ReproFile back;
    std::string err;
    ASSERT_TRUE(read_fuzz_repro(path, back, err)) << err;
    EXPECT_EQ(back.id.run_seed, id.run_seed);
    EXPECT_EQ(back.id.ascension, id.ascension);
    EXPECT_EQ(back.id.policy, id.policy);
    EXPECT_EQ(back.id.policy_seed, id.policy_seed);
    EXPECT_EQ(back.fail_kind, "hash_mismatch");
    EXPECT_EQ(back.fail_step, r.failure.step);
    ASSERT_EQ(back.actions.size(), rf.actions.size());

    // Replay the literal list with no fuzzer/policy involved.
    std::vector<uint64_t> hashes;
    Failure fail;
    ASSERT_TRUE(replay_actions(back.id, back.actions, hashes, fail))
        << "the emitted reproducer no longer replays";
    ASSERT_EQ(hashes.size(), back.actions.size());
    // hash_a is the UNPERTURBED branch, which is what a clean replay reproduces.
    EXPECT_EQ(hashes[back.fail_step], back.hash_a);
    EXPECT_NE(hashes[back.fail_step], back.hash_b);
    std::remove(path.c_str());
}

TEST(FuzzTriage, DriverWritesActionableReproducerForInjectedMismatch) {
    // Exercise the executable boundary, not merely run_case(): the driver must
    // turn its deliberate mismatch into a named file, exit non-zero, and hand
    // that file to the standalone replay program without hidden state.
    const std::string label = "mismatch_path";
    const std::string child_log = scratch(label + ".log");
    const std::string replay_log = scratch(label + "_replay.log");
    std::remove(child_log.c_str());
    std::remove(replay_log.c_str());
    for (const auto& e : std::filesystem::directory_iterator(STS_FUZZ_SCRATCH)) {
        const std::string name = e.path().filename().string();
        if (name.starts_with(label + "_") && e.path().extension() == ".repro") {
            std::filesystem::remove(e.path());
        }
    }

    const std::string command =
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seed-start 12345 --seeds 1 --policies greedy_damage"
        " --threads 1 --max-actions 2000 --out " +
        shell_quote(STS_FUZZ_SCRATCH) + " --label " + label +
        " --inject-nondeterminism 0:3 --quiet >" + shell_quote(child_log) +
        " 2>&1";
    const int child_rc = run_shell(command);
    EXPECT_NE(child_rc, 0) << "the deliberate mismatch unexpectedly exited clean";
    const std::string driver_text = read_text(child_log);
    EXPECT_NE(driver_text.find("=== FUZZ FAILURE: hash_mismatch ==="),
              std::string::npos);
    EXPECT_NE(driver_text.find("step: 3 of "), std::string::npos);
    EXPECT_NE(driver_text.find("reproducer:"), std::string::npos);
    EXPECT_NE(driver_text.find("failures: 1"), std::string::npos);

    std::vector<std::filesystem::path> repros;
    for (const auto& e : std::filesystem::directory_iterator(STS_FUZZ_SCRATCH)) {
        const std::string name = e.path().filename().string();
        if (name.starts_with(label + "_") && e.path().extension() == ".repro") {
            repros.push_back(e.path());
        }
    }
    ASSERT_EQ(repros.size(), 1u)
        << "driver must emit exactly one minimized mismatch reproducer";

    ReproFile rf;
    std::string error;
    ASSERT_TRUE(read_fuzz_repro(repros[0].string(), rf, error)) << error;
    EXPECT_EQ(rf.fail_kind, "hash_mismatch");
    EXPECT_EQ(rf.fail_step, 3u);
    EXPECT_EQ(rf.actions.size(), 4u)
        << "reproducer should contain only the prefix through the first mismatch";
    std::vector<uint64_t> hashes;
    Failure failure;
    ASSERT_TRUE(replay_actions(rf.id, rf.actions, hashes, failure));
    ASSERT_EQ(hashes.size(), rf.actions.size());
    EXPECT_EQ(hashes[rf.fail_step], rf.hash_a);
    EXPECT_NE(hashes[rf.fail_step], rf.hash_b);

    const std::string replay_command =
        shell_quote(STS_FUZZ_REPRO_BIN) + " " +
        shell_quote(repros[0].string()) + " --regen >" +
        shell_quote(replay_log) + " 2>&1";
    const int replay_rc = run_shell(replay_command);
    // The mismatch was intentionally injected in the driver, not the engine,
    // so a clean standalone process correctly reports NOT REPRODUCED. What
    // matters here is that both independent reproducer forms replay pass A.
    EXPECT_NE(replay_rc, 0);
    const std::string replay_text = read_text(replay_log);
    EXPECT_NE(replay_text.find("case-id-derived prefix MATCHES"),
              std::string::npos);
    EXPECT_NE(replay_text.find("replay took the pass-A branch"),
              std::string::npos);
    EXPECT_NE(replay_text.find("RESULT: NOT REPRODUCED"), std::string::npos);

    std::filesystem::remove(repros[0]);
    std::remove(child_log.c_str());
    std::remove(replay_log.c_str());
}

TEST(FuzzDriver, SeedSweepWritesAMergeableSummary) {
    // A clean executable-level smoke for the ordinary path: expand
    // seed x policy into distinct cases, persist the shard summary, and prove
    // the same driver can consume that summary without running another case.
    const std::string label = "sweep_path";
    const std::string child_log = scratch(label + ".log");
    const std::string merge_log = scratch(label + "_merge.log");
    const std::string kv0 = scratch(label + "_summary_s0.kv");
    const std::string kv1 = scratch(label + "_summary_s1.kv");
    const std::string bad_kv = scratch(label + "_summary_bad.kv");
    const std::string report0 = scratch(label + "_report_s0.txt");
    const std::string report1 = scratch(label + "_report_s1.txt");
    std::remove(child_log.c_str());
    std::remove(merge_log.c_str());
    std::remove(kv0.c_str());
    std::remove(kv1.c_str());
    std::remove(bad_kv.c_str());
    std::remove(report0.c_str());
    std::remove(report1.c_str());

    const std::string command0 =
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seed-start 41 --seeds 2 --policies random,greedy_damage"
        " --threads 2 --max-actions 2000 --verify-repro-every 1 --out " +
        shell_quote(STS_FUZZ_SCRATCH) + " --label " + label +
        " --shard 0/2 --quiet >" + shell_quote(child_log) + " 2>&1";
    const std::string command1 =
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seed-start 41 --seeds 2 --policies random,greedy_damage"
        " --threads 2 --max-actions 2000 --verify-repro-every 1 --out " +
        shell_quote(STS_FUZZ_SCRATCH) + " --label " + label +
        " --shard 1/2 --quiet >>" + shell_quote(child_log) + " 2>&1";
    ASSERT_EQ(run_shell(command0), 0) << read_text(child_log);
    ASSERT_EQ(run_shell(command1), 0) << read_text(child_log);
    EXPECT_NE(read_text(kv0).find("STSFUZZ_SUMMARY v1\n"), std::string::npos);
    EXPECT_NE(read_text(kv1).find("shard 1\n"), std::string::npos);

    const std::string merge_command =
        shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " + shell_quote(kv0) + " " +
        shell_quote(kv1) + " >" + shell_quote(merge_log) + " 2>&1";
    ASSERT_EQ(run_shell(merge_command), 0) << read_text(merge_log);
    const std::string merged = read_text(merge_log);
    EXPECT_NE(merged.find("cases (seed x policy x policy-seed) : 4"),
              std::string::npos);
    EXPECT_NE(merged.find("ACTIONS (counted once per case)"), std::string::npos);
    EXPECT_NE(merged.find("failures: 0"), std::string::npos);

    const std::string duplicate_command =
        shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " + shell_quote(kv0) + " " +
        shell_quote(kv0) + " >" + shell_quote(merge_log) + " 2>&1";
    EXPECT_NE(run_shell(duplicate_command), 0);
    EXPECT_NE(read_text(merge_log).find("duplicate/overlapping shard"),
              std::string::npos);

    const std::string incomplete_command =
        shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " + shell_quote(kv0) +
        " >" + shell_quote(merge_log) + " 2>&1";
    EXPECT_NE(run_shell(incomplete_command), 0);
    EXPECT_NE(read_text(merge_log).find("incomplete shard set"),
              std::string::npos);

    std::string corrupted = read_text(kv0);
    const size_t global_cases = corrupted.find("global_cases 4\n");
    ASSERT_NE(global_cases, std::string::npos);
    corrupted.replace(global_cases, std::strlen("global_cases 4"),
                      "global_cases 5");
    {
        // BINARY, and every other writer in this file too. The summary format
        // is LF-terminated (that is what `fuzz_soak` writes, checked with
        // `cat -A`), and a text-mode ofstream on Windows expands each '\n' to
        // "\r\n". This test's whole point is that ONE field is corrupted; a
        // text-mode write corrupts every LINE as well, the parser then rejects
        // the file for the wrong reason, and the assertion on the specific
        // "global_cases does not match" message fails while the driver is in
        // fact behaving correctly.
        std::ofstream os(bad_kv, std::ios::binary);
        os << corrupted;
    }
    const std::string bad_merge =
        shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " + shell_quote(bad_kv) +
        " >" + shell_quote(merge_log) + " 2>&1";
    EXPECT_NE(run_shell(bad_merge), 0);
    EXPECT_NE(read_text(merge_log).find(
                  "global_cases does not match its sweep configuration"),
              std::string::npos);

    const std::string incompatible_shard =
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seed-start 41 --seeds 2 --policies random,greedy_damage"
        " --threads 2 --max-actions 1999 --verify-repro-every 1 --out " +
        shell_quote(STS_FUZZ_SCRATCH) + " --label " + label +
        " --shard 1/2 --quiet >" + kNull + " 2>&1";
    ASSERT_EQ(run_shell(incompatible_shard), 0);
    const std::string incompatible_merge =
        shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " + shell_quote(kv0) + " " +
        shell_quote(kv1) + " >" + shell_quote(merge_log) + " 2>&1";
    EXPECT_NE(run_shell(incompatible_merge), 0);
    EXPECT_NE(read_text(merge_log).find("incompatible summary"),
              std::string::npos);

    std::remove(child_log.c_str());
    std::remove(merge_log.c_str());
    std::remove(kv0.c_str());
    std::remove(kv1.c_str());
    std::remove(bad_kv.c_str());
    std::remove(report0.c_str());
    std::remove(report1.c_str());
}

TEST(FuzzTriage, AbortLeavesAnActionableInFlightJournal) {
    // Process-level test: a deliberate abort cannot return a CaseResult or
    // write a normal .repro file. The pre-case journal is therefore the whole
    // crash triage contract. Run the actual driver as a child, require a
    // non-zero/signal exit, then use only its journal to regenerate a literal
    // action-list reproducer with the standalone executable.
    const std::string label = "abort_path";
    const std::string journal =
        scratch(label + "_inflight_s0_t0.txt");
    const std::string child_log = scratch(label + ".log");
    const std::string regen_log = scratch(label + "_regen.log");
    const std::string emitted = scratch(label + "_regen.repro");
    std::remove(journal.c_str());
    std::remove(child_log.c_str());
    std::remove(regen_log.c_str());
    std::remove(emitted.c_str());

    const std::string command =
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seed-start 12345 --seeds 1 --policies greedy_damage"
        " --threads 1 --max-actions 2000 --out " +
        shell_quote(STS_FUZZ_SCRATCH) + " --label " + label +
        " --inject-abort 0:3 --quiet >" + shell_quote(child_log) + " 2>&1";
    const int child_rc = run_shell(command);
    EXPECT_NE(child_rc, 0) << "the deliberate abort unexpectedly exited clean";

    const std::string text = read_text(journal);
    ASSERT_FALSE(text.empty())
        << "abort lost the in-flight journal; crash case identity is gone";
    EXPECT_NE(text.find("case_index 0"), std::string::npos);
    EXPECT_NE(text.find("seed=12345 asc=20 policy=greedy_damage pseed="),
              std::string::npos);
    const std::string marker = "repro: fuzz_repro ";
    const size_t pos = text.find(marker);
    ASSERT_NE(pos, std::string::npos)
        << "journal has no copy/paste standalone replay command";
    const size_t end = text.find('\n', pos);
    const std::string replay_args =
        text.substr(pos + marker.size(), end - (pos + marker.size()));

    const std::string replay_command =
        shell_quote(STS_FUZZ_REPRO_BIN) + " " + replay_args + " --emit " +
        shell_quote(emitted) + " >" + shell_quote(regen_log) + " 2>&1";
    const int replay_rc = run_shell(replay_command);
    EXPECT_EQ(replay_rc, 0)
        << "journal case id was not actionable:\n" << read_text(regen_log);

    ReproFile regenerated;
    std::string error;
    ASSERT_TRUE(read_fuzz_repro(emitted, regenerated, error)) << error;
    EXPECT_EQ(regenerated.id.run_seed, 12345);
    EXPECT_EQ(regenerated.id.ascension, 20);
    EXPECT_EQ(regenerated.id.policy, PolicyKind::GREEDY_DAMAGE);
    EXPECT_GT(regenerated.actions.size(), 3u)
        << "regenerated trajectory did not reach the injected abort step";

    std::remove(journal.c_str());
    std::remove(child_log.c_str());
    std::remove(regen_log.c_str());
    std::remove(emitted.c_str());
}

TEST(FuzzTriage, DriverWritesReproducerForImmediateNoProgress) {
    const std::string label = "no_progress_path";
    const std::string child_log = scratch(label + ".log");
    const std::string merge_log = scratch(label + "_merge.log");
    const std::string kv = scratch(label + "_summary_s0.kv");
    const std::string report = scratch(label + "_report_s0.txt");
    std::remove(child_log.c_str());
    std::remove(merge_log.c_str());
    std::remove(kv.c_str());
    std::remove(report.c_str());
    for (const auto& e : std::filesystem::directory_iterator(STS_FUZZ_SCRATCH)) {
        const std::string name = e.path().filename().string();
        if (name.starts_with(label + "_") && e.path().extension() == ".repro") {
            std::filesystem::remove(e.path());
        }
    }
    const std::string command =
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seed-start 12345 --seeds 1 --policies greedy_damage"
        " --threads 1 --max-actions 2000 --out " +
        shell_quote(STS_FUZZ_SCRATCH) + " --label " + label +
        " --inject-no-progress 0:3 --quiet >" + shell_quote(child_log) +
        " 2>&1";
    EXPECT_NE(run_shell(command), 0);
    const std::string text = read_text(child_log);
    EXPECT_NE(text.find("=== FUZZ FAILURE: no_progress ==="), std::string::npos);
    std::vector<std::filesystem::path> repros;
    for (const auto& e : std::filesystem::directory_iterator(STS_FUZZ_SCRATCH)) {
        const std::string name = e.path().filename().string();
        if (name.starts_with(label + "_") && e.path().extension() == ".repro") {
            repros.push_back(e.path());
        }
    }
    ASSERT_EQ(repros.size(), 1u);
    ReproFile rf;
    std::string error;
    ASSERT_TRUE(read_fuzz_repro(repros[0].string(), rf, error)) << error;
    EXPECT_EQ(rf.fail_kind, "no_progress");
    EXPECT_EQ(rf.fail_step, 3u);
    EXPECT_EQ(rf.actions.size(), 4u);
    const std::string merge_command =
        shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " + shell_quote(kv) +
        " >" + shell_quote(merge_log) + " 2>&1";
    EXPECT_NE(run_shell(merge_command), 0);
    EXPECT_NE(read_text(merge_log).find("failures: 1"), std::string::npos);
    std::filesystem::remove(repros[0]);
    std::remove(child_log.c_str());
    std::remove(merge_log.c_str());
    std::remove(kv.c_str());
    std::remove(report.c_str());
}

TEST(FuzzTriage, CorruptingTheActionListIsCaught) {
    // A reproducer whose actions were edited into something the engine no
    // longer considers legal must FAIL loudly, not replay something else.
    const CaseId id = make_case(PolicyKind::RANDOM);
    CaseResult base;
    ASSERT_TRUE(run_case(id, limits(), nullptr, base, false));
    ASSERT_GT(base.actions, 3u);

    std::vector<engine::Action> corrupt = base.trajectory;
    // A PLAY_CARD of hand slot 9 at step 0 (the Neow proceed) cannot be legal.
    corrupt[0] = engine::make_action(engine::ActionVerb::PLAY_CARD, 9, 6);
    std::vector<uint64_t> hashes;
    Failure fail;
    EXPECT_FALSE(replay_actions(id, corrupt, hashes, fail));
    EXPECT_EQ(fail.kind, FailKind::NO_LEGAL_MOVES);
    EXPECT_EQ(fail.step, 0u);
}

TEST(FuzzTriage, ReproducerRejectsAWrongVersionLine) {
    const std::string path = scratch("badversion.repro");
    {
        std::FILE* f = std::fopen(path.c_str(), "w");
        ASSERT_NE(f, nullptr);
        std::fputs("STSREPRO v1\nseed 1\nactions 0\n", f);
        std::fclose(f);
    }
    ReproFile r;
    std::string err;
    EXPECT_FALSE(read_fuzz_repro(path, r, err));
    EXPECT_NE(err.find("bad version line"), std::string::npos);
    std::remove(path.c_str());
}

TEST(FuzzTriage, ReproducerRejectsAnUnknownKey) {
    // A v2 field silently dropped by a v1 reader is a reproducer that stops
    // reproducing; the parser must refuse rather than shrug.
    const std::string path = scratch("unknownkey.repro");
    {
        std::FILE* f = std::fopen(path.c_str(), "w");
        ASSERT_NE(f, nullptr);
        std::fputs("STSFUZZ v1\nseed 1\ndeck_hash 99\nactions 0\n", f);
        std::fclose(f);
    }
    ReproFile r;
    std::string err;
    EXPECT_FALSE(read_fuzz_repro(path, r, err));
    EXPECT_NE(err.find("unknown key"), std::string::npos);
    std::remove(path.c_str());
}

TEST(FuzzTriage, ReproducerRequiresEveryCaseIdentityFieldExactlyOnce) {
    const std::string missing = scratch("missing_case_field.repro");
    {
        std::ofstream os(missing, std::ios::binary);
        os << "STSFUZZ v1\nseed 1\nascension 20\npolicy random\nactions 0\n";
    }
    ReproFile r;
    std::string err;
    EXPECT_FALSE(read_fuzz_repro(missing, r, err));
    EXPECT_NE(err.find("policy_seed"), std::string::npos);

    const std::string duplicate = scratch("duplicate_case_field.repro");
    {
        std::ofstream os(duplicate, std::ios::binary);
        os << "STSFUZZ v1\nseed 1\nseed 2\nascension 20\npolicy random\n"
              "policy_seed 3\nactions 0\n";
    }
    EXPECT_FALSE(read_fuzz_repro(duplicate, r, err));
    EXPECT_NE(err.find("duplicate"), std::string::npos);
    std::remove(missing.c_str());
    std::remove(duplicate.c_str());
}

TEST(FuzzTriage, ReproducerRejectsOverflowAndTrailingActionData) {
    const std::string path = scratch("bad_action.repro");
    {
        std::ofstream os(path, std::ios::binary);
        os << "STSFUZZ v1\nseed 1\nascension 20\npolicy random\n"
              "policy_seed 3\nactions 1\n4294967296 not-a-comment\n";
    }
    ReproFile r;
    std::string err;
    EXPECT_FALSE(read_fuzz_repro(path, r, err));
    std::remove(path.c_str());
}

TEST(FuzzTriage, ReproducerValidatesFailureKindHashesAndStepRange) {
    const std::string path = scratch("bad_failure_record.repro");
    auto write_record = [&](const std::string& body) {
        std::ofstream os(path, std::ios::binary);
        os << "STSFUZZ v1\nseed 1\nascension 20\npolicy random\n"
              "policy_seed 3\n"
           << body;
    };
    ReproFile r;
    std::string err;

    write_record("fail invented\nfail_step 0\n"
                 "hash_a 0000000000000001\nhash_b 0000000000000002\n"
                 "final_hash 0000000000000003\nactions 1\n0\n");
    EXPECT_FALSE(read_fuzz_repro(path, r, err));
    EXPECT_NE(err.find("bad fail kind"), std::string::npos);

    write_record("fail action_mismatch\nfail_step 0\nactions 1\n0\n");
    EXPECT_FALSE(read_fuzz_repro(path, r, err));
    EXPECT_NE(err.find("requires hash_a/hash_b/final_hash"),
              std::string::npos);

    write_record("fail action_mismatch\nfail_step 1\n"
                 "hash_a 0000000000000001\nhash_b 0000000000000002\n"
                 "final_hash 0000000000000003\nactions 1\n0\n");
    EXPECT_FALSE(read_fuzz_repro(path, r, err));
    EXPECT_NE(err.find("fail_step is out of range"), std::string::npos);

    write_record("fail hash_mismatch\nfail_step 1\n"
                 "hash_a 0000000000000001\nhash_b 0000000000000002\n"
                 "final_hash 0000000000000003\nactions 1\n0\n");
    EXPECT_TRUE(read_fuzz_repro(path, r, err)) << err;
    EXPECT_EQ(r.fail_step, 1u);

    // NO_LEGAL_MOVES is discovered before an action is appended. In
    // particular, the first live state may produce a valid zero-action
    // reproducer whose failure step is exactly the end boundary.
    write_record("fail no_legal_moves\nfail_step 0\n"
                 "hash_a 0000000000000001\nhash_b 0000000000000002\n"
                 "final_hash 0000000000000003\nactions 0\n");
    EXPECT_TRUE(read_fuzz_repro(path, r, err)) << err;
    EXPECT_EQ(r.fail_step, 0u);
    EXPECT_TRUE(r.actions.empty());
    std::remove(path.c_str());
}

TEST(FuzzTriage, ReproducerWriterReaderClosesOverEveryFailureKind) {
    ReproFile rf;
    rf.id = make_case(PolicyKind::RANDOM, 9191);
    rf.actions = {
        engine::make_action(engine::ActionVerb::CHOOSE,
                            engine::kChooseProceed),
        engine::make_action(engine::ActionVerb::END_TURN),
    };
    rf.hash_a = 0x1111111111111111ull;
    rf.hash_b = 0x2222222222222222ull;
    rf.final_hash = 0x3333333333333333ull;
    rf.has_hashes = true;

    for (uint8_t raw = static_cast<uint8_t>(FailKind::HASH_MISMATCH);
         raw < static_cast<uint8_t>(FailKind::COUNT); ++raw) {
        const FailKind kind = static_cast<FailKind>(raw);
        rf.fail_kind = fail_kind_name(kind);
        const bool end_boundary =
            kind == FailKind::HASH_MISMATCH ||
            kind == FailKind::LENGTH_MISMATCH ||
            kind == FailKind::REPRO_MISMATCH ||
            kind == FailKind::NO_LEGAL_MOVES;
        rf.fail_step = end_boundary
                           ? static_cast<uint32_t>(rf.actions.size())
                           : 1u;

        const std::string path =
            scratch(std::string("failure_roundtrip_") + rf.fail_kind +
                    ".repro");
        ASSERT_TRUE(write_fuzz_repro(path, rf)) << rf.fail_kind;

        ReproFile back;
        std::string err;
        ASSERT_TRUE(read_fuzz_repro(path, back, err))
            << rf.fail_kind << ": " << err;
        EXPECT_EQ(back.fail_kind, rf.fail_kind);
        EXPECT_EQ(back.fail_step, rf.fail_step);
        EXPECT_TRUE(back.has_hashes);
        EXPECT_EQ(back.hash_a, rf.hash_a);
        EXPECT_EQ(back.hash_b, rf.hash_b);
        EXPECT_EQ(back.final_hash, rf.final_hash);
        ASSERT_EQ(back.actions.size(), rf.actions.size());
        std::remove(path.c_str());
    }
}

TEST(FuzzTriage, ReproducerRoundTripsEveryActionVerb) {
    ReproFile rf;
    rf.id = make_case(PolicyKind::HOARD_GOLD, 777);
    rf.actions = {
        engine::make_action(engine::ActionVerb::CHOOSE, engine::kChooseProceed),
        engine::make_action(engine::ActionVerb::PLAY_CARD, 3, 2),
        engine::make_action(engine::ActionVerb::END_TURN),
        engine::make_action(engine::ActionVerb::USE_POTION, 1, 0),
        engine::make_action(engine::ActionVerb::CHOOSE, engine::kChooseBoss),
        engine::make_action(engine::ActionVerb::CONFIRM),
    };
    const std::string path = scratch("roundtrip.repro");
    ASSERT_TRUE(write_fuzz_repro(path, rf));
    ReproFile back;
    std::string err;
    ASSERT_TRUE(read_fuzz_repro(path, back, err)) << err;
    ASSERT_EQ(back.actions.size(), rf.actions.size());
    for (size_t i = 0; i < rf.actions.size(); ++i) {
        EXPECT_EQ(back.actions[i].bits, rf.actions[i].bits) << i;
    }
    EXPECT_EQ(back.id.policy, PolicyKind::HOARD_GOLD);
    EXPECT_EQ(back.id.policy_seed, 777u);
    std::remove(path.c_str());
}

// --- 3. move enumeration is the engine's own legality, not the fuzzer's -------

TEST(FuzzPolicy, EnumeratedMovesAreAllAcceptedByAdvance) {
    // Every enumerated move must actually change the controller: advance()
    // re-checks legality and no-ops what it rejects, so an enumerated move that
    // leaves the state byte-identical means the mask and advance() disagree.
    const CaseId id = make_case(PolicyKind::RANDOM, 42);
    engine::RunController rc = engine::run_begin(id.run_seed, id.ascension);
    PolicyRng rng(id.policy_seed);
    engine::RunActionMask mask{};
    Move moves[kMoveCap];
    engine::StepResult results[1]{};

    int checked = 0;
    for (int step = 0; step < 300; ++step) {
        const auto phase = static_cast<engine::RunPhase>(rc.phase);
        if (phase == engine::RunPhase::RUN_OVER ||
            phase == engine::RunPhase::ROOM_UNIMPLEMENTED) {
            break;
        }
        engine::legal_actions(rc, mask);
        const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
        ASSERT_GT(n, 0u) << "empty legal set in a live phase at step " << step;

        // Try EVERY enumerated move on a copy; each must mutate the controller.
        for (size_t i = 0; i < n; ++i) {
            engine::RunController probe = rc;
            const uint64_t before = hash_controller(probe);
            std::span<engine::RunController> rs(&probe, 1);
            std::span<const engine::Action> as(&moves[i].action, 1);
            std::span<engine::StepResult> os(results, 1);
            engine::advance(rs, as, os);
            EXPECT_NE(hash_controller(probe), before)
                << "enumerated move " << move_cat_name(moves[i].cat)
                << " was a no-op at step " << step;
            ++checked;
        }

        const size_t pick = policy_pick(id.policy, rc, moves, n, rng);
        std::span<engine::RunController> rs(&rc, 1);
        std::span<const engine::Action> as(&moves[pick].action, 1);
        std::span<engine::StepResult> os(results, 1);
        engine::advance(rs, as, os);
    }
    EXPECT_GT(checked, 20) << "the walk did not get far enough to prove anything";
}

TEST(FuzzPolicy, TargetedPotionIsEnumeratedOnceAndOnlyForLiveTargets) {
    engine::RunController rc = engine::run_begin(123, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::COMBAT);
    rc.run.potion_slots = 1;
    rc.run.potions[0] = static_cast<uint16_t>(engine::PotionId::FEAR_POTION);
    rc.combat.phase =
        static_cast<uint8_t>(engine::CombatPhase::WAITING_ON_USER);
    rc.combat.monster_count = 2;
    rc.combat.monsters[0].hp = 0;
    rc.combat.monsters[1].hp = 20;

    engine::RunActionMask mask{};
    engine::legal_actions(rc, mask);
    Move moves[kMoveCap];
    const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
    int potion_moves = 0;
    for (size_t i = 0; i < n; ++i) {
        if (engine::action_verb(moves[i].action) !=
            engine::ActionVerb::USE_POTION) continue;
        ++potion_moves;
        EXPECT_EQ(moves[i].cat, MoveCat::USE_POTION_TARGET);
        EXPECT_EQ(engine::action_arg0(moves[i].action), 0);
        EXPECT_EQ(engine::action_arg1(moves[i].action), 1);
    }
    EXPECT_EQ(potion_moves, 1);
}

void ExpectLargeChoiceEnumerated(engine::ChoiceKind kind) {
    engine::RunController rc = engine::run_begin(456, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::COMBAT);
    rc.combat.phase =
        static_cast<uint8_t>(engine::CombatPhase::WAITING_ON_USER);
    rc.combat.action_count = 1;
    rc.combat.action_head = 0;
    engine::ActionQueueItem& choose = rc.combat.action_queue[0];
    choose.opcode = static_cast<uint16_t>(engine::Opcode::CHOOSE_CARD);
    choose.amount = 1;
    choose.flags = engine::make_choose_flags(kind, false);
    for (uint8_t i = 0; i < 12; ++i) {
        rc.combat.card_pool[i].card_id =
            static_cast<uint16_t>(engine::CardId::STRIKE);
        if (kind == engine::ChoiceKind::DISCARD_TO_DRAW_TOP) {
            rc.combat.discard[i] = i;
        } else {
            rc.combat.exhaust[i] = i;
        }
    }
    if (kind == engine::ChoiceKind::DISCARD_TO_DRAW_TOP) {
        rc.combat.discard_count = 12;
    } else {
        rc.combat.exhaust_count = 12;
    }

    engine::RunActionMask mask{};
    engine::legal_actions(rc, mask);
    ASSERT_TRUE(mask.combat.choice_pending);
    Move moves[kMoveCap];
    const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
    bool found_11 = false;
    for (size_t i = 0; i < n; ++i) {
        if (moves[i].cat == MoveCat::COMBAT_CHOOSE &&
            engine::action_arg0(moves[i].action) == 11) {
            found_11 = true;
            engine::RunController probe = rc;
            engine::StepResult result{};
            const uint64_t before = hash_controller(probe);
            engine::advance(std::span<engine::RunController>(&probe, 1),
                            std::span<const engine::Action>(&moves[i].action, 1),
                            std::span<engine::StepResult>(&result, 1));
            EXPECT_NE(hash_controller(probe), before);
        }
    }
    EXPECT_TRUE(found_11);
}

TEST(FuzzPolicy, EnumeratesDiscardChoicesBeyondTheHandCapacity) {
    ExpectLargeChoiceEnumerated(engine::ChoiceKind::DISCARD_TO_DRAW_TOP);
}

TEST(FuzzPolicy, EnumeratesExhaustChoicesBeyondTheHandCapacity) {
    ExpectLargeChoiceEnumerated(engine::ChoiceKind::EXHAUST_TO_HAND);
}

// An OPTIONAL (zero-to-N) screen: every toggle plus the confirm button, and the
// confirm is offered from the FIRST step. That is what makes the EMPTY confirm
// -- the move a count-driven screen has no spelling for -- reachable in a soak.
TEST(FuzzPolicy, OptionalChoiceEnumeratesTogglesAndAnImmediateEmptyConfirm) {
    engine::RunController rc = engine::run_begin(4567, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::COMBAT);
    rc.combat.phase =
        static_cast<uint8_t>(engine::CombatPhase::WAITING_ON_USER);
    rc.combat.monster_count = 1;
    rc.combat.monsters[0].monster_id =
        static_cast<uint16_t>(engine::MonsterId::JAW_WORM);
    rc.combat.monsters[0].hp = 40;
    rc.combat.monsters[0].max_hp = 40;
    for (uint8_t i = 0; i < 4; ++i) {
        rc.combat.card_pool[i].card_id =
            static_cast<uint16_t>(engine::CardId::STRIKE);
        rc.combat.hand[i] = i;
    }
    rc.combat.hand_count = 4;
    rc.combat.action_count = 1;
    rc.combat.action_head = 0;
    engine::ActionQueueItem& choose = rc.combat.action_queue[0];
    choose.opcode = static_cast<uint16_t>(engine::Opcode::CHOOSE_CARD);
    choose.amount = 3;
    choose.flags = engine::make_choose_flags(
        engine::ChoiceKind::EXHAUST, /*random=*/false, /*copies=*/1,
        engine::kChoiceNoTypeFilter, /*optional=*/true);

    engine::RunActionMask mask{};
    engine::legal_actions(rc, mask);
    ASSERT_TRUE(mask.combat.choice_pending);
    ASSERT_TRUE(mask.combat.choice_optional);
    ASSERT_TRUE(mask.combat.can_confirm_choice);

    Move moves[kMoveCap];
    const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
    size_t toggles = 0;
    size_t confirms = 0;
    for (size_t i = 0; i < n; ++i) {
        if (moves[i].cat == MoveCat::COMBAT_CHOOSE) {
            ++toggles;
        }
        if (moves[i].cat != MoveCat::CHOICE_CONFIRM) {
            continue;
        }
        ++confirms;
        EXPECT_EQ(engine::action_verb(moves[i].action),
                  engine::ActionVerb::CONFIRM);
        // Taking it right now IS the empty confirm: nothing is selected, so the
        // screen closes having exhausted nothing.
        engine::RunController probe = rc;
        engine::StepResult result{};
        engine::advance(std::span<engine::RunController>(&probe, 1),
                        std::span<const engine::Action>(&moves[i].action, 1),
                        std::span<engine::StepResult>(&result, 1));
        EXPECT_EQ(probe.combat.hand_count, 4) << "no card was exhausted";
        EXPECT_EQ(probe.combat.exhaust_count, 0);
        EXPECT_EQ(probe.combat.action_count, 0) << "the screen closed";
    }
    EXPECT_EQ(toggles, 4u) << "every hand card is a legal pick";
    EXPECT_EQ(confirms, 1u);
    EXPECT_STREQ(move_cat_name(MoveCat::CHOICE_CONFIRM), "choice_confirm");
}

// MoveCat::COUNT sizes the soak's coverage arrays, so an enumerator at or above
// it is an out-of-bounds write, not a cosmetic slip.
TEST(FuzzPolicy, MoveCatCountIsPastEveryEnumerator) {
    // This used to compare COUNT against CHOICE_CONFIRM BY NAME, on the
    // assumption that CHOICE_CONFIRM was the last enumerator. It stopped being
    // last twice over (SHOP, RECALL, then the four boss-chest categories) and
    // nothing noticed, because the assertion stayed true while measuring
    // nothing: a guard meant to catch a stale hand-written COUNT had quietly
    // become a guard that 25 < 32.
    //
    // The fix is to find the real highest enumerator rather than naming one.
    // Every value below COUNT must have a name, and COUNT itself -- and the
    // value past it -- must not, which pins COUNT one past the last NAMED
    // enumerator without this test having to know which that is.
    // move_cat_name's switch is -Wswitch-checked, so a new enumerator with no
    // name is a compile error there and a "?" here.
    ASSERT_GT(static_cast<int>(MoveCat::COUNT), 0);
    for (int i = 0; i < static_cast<int>(MoveCat::COUNT); ++i) {
        EXPECT_STRNE(move_cat_name(static_cast<MoveCat>(i)), "?")
            << "MoveCat " << i << " has no name -- either COUNT reaches past a "
               "gap, or an enumerator landed without a move_cat_name case";
    }
    EXPECT_STREQ(move_cat_name(MoveCat::COUNT), "?")
        << "COUNT itself must not be a named category";
    EXPECT_STREQ(
        move_cat_name(static_cast<MoveCat>(static_cast<int>(MoveCat::COUNT) + 1)),
        "?")
        << "an enumerator ABOVE COUNT is the out-of-bounds write this test "
           "exists to catch";
}

TEST(FuzzPolicy, TreasureRoomEnumeratesDistinctOpenAndSkipActions) {
    engine::RunController rc = engine::run_begin(8080, 20);
    rc.phase =
        static_cast<uint8_t>(engine::RunPhase::TREASURE_ROOM);
    rc.room_type =
        static_cast<uint8_t>(engine::RoomType::Treasure);
    rc.treasure_chest.size =
        static_cast<uint8_t>(engine::ChestSize::SMALL);
    rc.treasure_chest.relic_tier =
        static_cast<uint8_t>(engine::RelicTier::COMMON);

    engine::RunActionMask mask{};
    engine::legal_actions(rc, mask);
    Move moves[kMoveCap];
    const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
    ASSERT_EQ(n, 2u);
    EXPECT_EQ(moves[0].cat, MoveCat::TREASURE_OPEN);
    EXPECT_EQ(engine::action_arg0(moves[0].action),
              engine::kChooseOpenChest);
    EXPECT_EQ(moves[1].cat, MoveCat::TREASURE_SKIP);
    EXPECT_EQ(engine::action_arg0(moves[1].action),
              engine::kChooseProceed);
}

TEST(FuzzPolicy, EventDialogEnumeratesOnlyEnabledOptionsWithItsOwnCategory) {
    engine::RunController rc = engine::run_begin(8081, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::EVENT_DIALOG);
    rc.event.event_id = engine::kSyntheticEventId;
    rc.run.gold = 49;
    const engine::EventDialogImpl* impl =
        engine::event_dialog_impl(rc.event.event_id);
    ASSERT_NE(impl, nullptr);
    impl->on_enter(rc, rc.event);

    engine::RunActionMask mask{};
    engine::legal_actions(rc, mask);
    Move moves[kMoveCap];
    const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
    ASSERT_EQ(n, 3u);
    for (size_t i = 0; i < n; ++i) {
        EXPECT_EQ(moves[i].cat, MoveCat::EVENT_OPTION);
    }
    EXPECT_EQ(engine::action_arg0(moves[0].action), 0);
    EXPECT_EQ(engine::action_arg0(moves[1].action), 2);
    EXPECT_EQ(engine::action_arg0(moves[2].action), 3);
}

TEST(FuzzPolicy, EventGridEnumeratesOnlyLegalMasterDeckCards) {
    engine::RunController rc = engine::run_begin(8082, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::EVENT_DIALOG);
    rc.event.event_id =
        static_cast<uint16_t>(engine::EventId::THE_CLERIC);
    rc.run.master_deck_count = 2;
    rc.run.master_deck[0] =
        engine::CardInstance{
            static_cast<uint16_t>(engine::CardId::ASCENDERS_BANE), 0, 0, 0, 0};
    rc.run.master_deck[1] =
        engine::CardInstance{
            static_cast<uint16_t>(engine::CardId::STRIKE), 0, 0, 0, 0};
    engine::open_event_grid(rc.event, engine::EventGridKind::PURGE);

    engine::RunActionMask mask{};
    engine::legal_actions(rc, mask);
    Move moves[kMoveCap];
    const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
    ASSERT_EQ(n, 1u);
    EXPECT_EQ(moves[0].cat, MoveCat::EVENT_GRID);
    EXPECT_EQ(engine::action_arg0(moves[0].action), 1);
}

TEST(FuzzPolicy, RestGridCancelIsEnumeratedAndReturnsToTheMenu) {
    engine::RunController rc = engine::run_begin(8083, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::REST_SITE);
    rc.rest.screen = static_cast<uint8_t>(engine::RestScreen::SMITH);
    rc.run.master_deck_count = 1;
    rc.run.master_deck[0] =
        engine::CardInstance{
            static_cast<uint16_t>(engine::CardId::STRIKE), 0, 0, 0, 0};

    engine::RunActionMask mask{};
    engine::legal_actions(rc, mask);
    ASSERT_TRUE(mask.can_cancel_grid);
    Move moves[kMoveCap];
    const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
    const Move* cancel = nullptr;
    for (size_t i = 0; i < n; ++i) {
        if (engine::action_arg0(moves[i].action) ==
            engine::kChooseCancelGrid) {
            cancel = &moves[i];
            break;
        }
    }
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(cancel->cat, MoveCat::SMITH_CARD);

    engine::StepResult result{};
    engine::advance(std::span<engine::RunController>(&rc, 1),
                    std::span<const engine::Action>(&cancel->action, 1),
                    std::span<engine::StepResult>(&result, 1));
    EXPECT_EQ(static_cast<engine::RestScreen>(rc.rest.screen),
              engine::RestScreen::MENU);
}

// --- 3b. the controller hash is a CONTENT hash, not a byte hash --------------

TEST(FuzzHash, ControllerHashIgnoresEncounterKeyADDRESSES) {
    // The trap this pins, found by running the induced-failure test rather than
    // by reasoning: RunController embeds std::string_view encounter keys, so a
    // raw byte hash of the struct hashes POINTERS into .rodata. Under ASLR
    // those differ per process, so the guard was stable inside one process and
    // different in the next -- every emitted reproducer "failed to reproduce".
    //
    // Re-point one key at an identical character sequence held somewhere else.
    // The bytes of the struct change; the CONTENT does not; the hash must not.
    engine::RunController rc = engine::run_begin(4242, 20);
    ASSERT_GT(rc.lists.monster_list_count, 0);

    const uint64_t before = hash_controller(rc);
    const std::string copy(rc.lists.monster_list[0]);
    ASSERT_NE(copy.data(), rc.lists.monster_list[0].data())
        << "the test needs a genuinely different address";
    rc.lists.monster_list[0] = std::string_view(copy.data(), copy.size());

    EXPECT_EQ(hash_controller(rc), before)
        << "hash_controller is hashing string_view POINTERS, not their contents";

    // ... and it must still notice a real change to the key.
    const std::string other = copy + "X";
    rc.lists.monster_list[0] = std::string_view(other.data(), other.size());
    EXPECT_NE(hash_controller(rc), before);
}

TEST(FuzzHash, RegionBreakdownAgreesWithTheWholeHash) {
    engine::RunController rc = engine::run_begin(99, 20);
    const ControllerHashes p = hash_controller_parts(rc);
    EXPECT_EQ(p.whole, hash_controller(rc));
    EXPECT_EQ(p.run, engine::hash_state(rc.run));
    EXPECT_EQ(p.combat, engine::hash_state(rc.combat));
    EXPECT_NE(p.lists, 0u);
}

TEST(FuzzHash, ControllerHashIncludesTheTreasureChestDescriptor) {
    engine::RunController rc = engine::run_begin(99, 20);
    const ControllerHashes before = hash_controller_parts(rc);
    rc.treasure_chest.size =
        static_cast<uint8_t>(engine::ChestSize::LARGE);
    rc.treasure_chest.relic_tier =
        static_cast<uint8_t>(engine::RelicTier::RARE);
    rc.treasure_chest.has_gold = 1;
    const ControllerHashes after = hash_controller_parts(rc);
    EXPECT_NE(after.whole, before.whole);
    EXPECT_NE(after.treasure, before.treasure);
    EXPECT_EQ(after.run, before.run);
    EXPECT_EQ(after.combat, before.combat);
    EXPECT_EQ(after.rewards, before.rewards);
    EXPECT_EQ(after.scalars, before.scalars);
}

TEST(FuzzHash, ControllerHashIncludesEveryEventDialogStateField) {
    engine::RunController rc = engine::run_begin(100, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::EVENT_DIALOG);
    rc.event.event_id = engine::kSyntheticEventId;
    const uint64_t before = hash_controller(rc);

    engine::RunController changed = rc;
    changed.event.event_id =
        static_cast<uint16_t>(engine::EventId::BIG_FISH);
    EXPECT_NE(hash_controller(changed), before);

    changed = rc;
    changed.event.screen = 1;
    EXPECT_NE(hash_controller(changed), before);

    changed = rc;
    changed.event.grid_kind =
        static_cast<uint8_t>(engine::EventGridKind::PURGE);
    EXPECT_NE(hash_controller(changed), before);

    changed = rc;
    changed.event.scratch0 = 7;
    EXPECT_NE(hash_controller(changed), before);

    changed = rc;
    changed.event.scratch1 = -3;
    EXPECT_NE(hash_controller(changed), before);

    changed = rc;
    changed.event.scratch2 = 11;
    EXPECT_NE(hash_controller(changed), before);

    changed = rc;
    changed.event.scratch3 = -11;
    EXPECT_NE(hash_controller(changed), before);

    // Match and Keep's board: every slot and every per-slot field, so a
    // divergent deal or a divergent match cannot hash the same.
    for (int i = 0; i < engine::kEventBoardCap; ++i) {
        changed = rc;
        changed.event.board[i].card_id =
            static_cast<uint16_t>(engine::CardId::STRIKE);
        EXPECT_NE(hash_controller(changed), before) << "board slot " << i;

        changed = rc;
        changed.event.board[i].upgrade = 1;
        EXPECT_NE(hash_controller(changed), before) << "board slot " << i;

        changed = rc;
        changed.event.board[i].taken = 1;
        EXPECT_NE(hash_controller(changed), before) << "board slot " << i;
    }
}

// --- 4. coverage bookkeeping --------------------------------------------------

TEST(FuzzCoverage, KvRoundTripsAndMergesByAddition) {
    Coverage a;
    CaseResult r;
    const CaseId id = make_case(PolicyKind::GREEDY_DAMAGE);
    ASSERT_TRUE(run_case(id, limits(), &a, r, true));
    ASSERT_GT(a.actions, 0u);

    Coverage back;
    ASSERT_TRUE(coverage_from_kv(a.kv(), back));
    EXPECT_EQ(back.actions, a.actions);
    EXPECT_EQ(back.cases, a.cases);
    EXPECT_EQ(back.combats_entered, a.combats_entered);
    EXPECT_EQ(back.max_turn, a.max_turn);
    EXPECT_EQ(back.cards_played.count(), a.cards_played.count());

    Coverage sum = a;
    sum.merge(back);
    EXPECT_EQ(sum.actions, a.actions * 2);
    EXPECT_EQ(sum.cards_played.count(), a.cards_played.count());  // union, not sum
    EXPECT_EQ(sum.max_turn, a.max_turn);                          // max, not sum
}

// --- ARCHIVED summaries: read them, and say that you did --------------------
//
// `coverage_from_kv` is strict about the field set, on purpose: for a live
// sweep a missing counter is drift. But summaries written before the
// `victories` counter landed (pre-6d7efc4) do not carry that key and cannot be
// rewritten -- regenerating one means re-running the whole campaign it
// summarises. The tolerant read is opt-in, names what it defaulted, and covers
// EXACTLY the known-vintage key set.
TEST(FuzzCoverage, AnArchivedSummaryMissingVictoriesIsRejectedByTheStrictRead) {
    Coverage a;
    CaseResult r;
    ASSERT_TRUE(run_case(make_case(PolicyKind::GREEDY_DAMAGE), limits(), &a, r, true));
    a.victories = 3;

    // Delete exactly the `victories` line, which is what an old binary's output
    // looks like.
    std::string text = a.kv();
    const std::size_t at = text.find("\nvictories ");
    ASSERT_NE(at, std::string::npos);
    const std::size_t eol = text.find('\n', at + 1);
    ASSERT_NE(eol, std::string::npos);
    const std::string legacy = text.substr(0, at) + text.substr(eol);
    ASSERT_EQ(legacy.find("\nvictories "), std::string::npos);

    Coverage strict;
    EXPECT_FALSE(coverage_from_kv(legacy, strict))
        << "a missing counter must stay loud for the live path";

    Coverage tolerant;
    std::vector<std::string> defaulted;
    ASSERT_TRUE(coverage_from_kv_legacy(legacy, tolerant, defaulted));
    ASSERT_EQ(defaulted.size(), 1u);
    EXPECT_EQ(defaulted[0], "victories");
    EXPECT_EQ(tolerant.victories, 0u) << "defaulted, not measured";
    // Everything the vintage DID carry still round-trips exactly -- the
    // tolerance is a hole of known shape, not a relaxation.
    EXPECT_EQ(tolerant.actions, a.actions);
    EXPECT_EQ(tolerant.cases, a.cases);
    EXPECT_EQ(tolerant.deaths, a.deaths);
    EXPECT_EQ(tolerant.cards_played.count(), a.cards_played.count());
}

TEST(FuzzCoverage, TheLegacyToleranceCoversOnlyItsNamedKeys) {
    Coverage a;
    CaseResult r;
    ASSERT_TRUE(run_case(make_case(PolicyKind::GREEDY_DAMAGE), limits(), &a, r, true));

    // The list is the whole of the tolerance, and it is asserted rather than
    // assumed: a counter quietly added to it would make its absence silent
    // forever after.
    ASSERT_EQ(legacy_optional_kv_keys().size(), 1u);
    EXPECT_EQ(legacy_optional_kv_keys()[0], "victories");

    // A key OUTSIDE the list is still fatal, tolerance or not.
    std::string text = a.kv();
    const std::size_t at = text.find("\ndeaths ");
    ASSERT_NE(at, std::string::npos);
    const std::size_t eol = text.find('\n', at + 1);
    ASSERT_NE(eol, std::string::npos);
    const std::string mangled = text.substr(0, at) + text.substr(eol);

    Coverage out;
    std::vector<std::string> defaulted;
    EXPECT_FALSE(coverage_from_kv(mangled, out));
    EXPECT_FALSE(coverage_from_kv_legacy(mangled, out, defaulted))
        << "tolerance is for history, never for drift";
}

TEST(FuzzCoverage, ReportNamesWhatWasNeverReached) {
    // The report must state absences rather than leaving them to be inferred:
    // a one-case soak cannot have used a potion, and must say so.
    Coverage c;
    CaseResult r;
    ASSERT_TRUE(run_case(make_case(PolicyKind::ALWAYS_EVENT), limits(), &c, r, false));
    const std::string text = c.report(1.0);
    EXPECT_NE(text.find("NEVER REACHED"), std::string::npos);
    if (c.potions_used == 0) {
        EXPECT_NE(text.find("no potion was ever used"), std::string::npos);
    }
    EXPECT_NE(text.find("registry rows seen"), std::string::npos);
}

TEST(FuzzCoverage, ActionsAreCountedOncePerCaseNotOncePerPass) {
    // The acceptance number must be the trajectory length, not the trajectory
    // length times the number of replay passes.
    Coverage c;
    CaseResult r;
    ASSERT_TRUE(run_case(make_case(PolicyKind::GREEDY_DAMAGE), limits(), &c, r, true));
    EXPECT_EQ(c.actions, r.actions);
    EXPECT_GE(c.actions_engine, c.actions * 3);  // A + B + C
    EXPECT_EQ(c.runs, 3u);
}

TEST(FuzzCoverage, CheckedMergeRejectsCounterOverflow) {
    Coverage total;
    total.actions = UINT64_MAX;
    Coverage shard;
    shard.actions = 1;
    EXPECT_FALSE(total.merge_checked(shard));
    EXPECT_EQ(total.actions, UINT64_MAX);
}

// REGRESSION (probe_shop, 2026-07-27): B4.8 moved shop rooms from
// ROOM_UNIMPLEMENTED parking to RunPhase::SHOP, and the soak's room-entry
// accounting was never taught the new phase -- so a 300-seed probe printed
// "shop entered 0" while its own move table showed 506 SHOP moves taken.
// Reachability was never the problem; the COUNTER was. This test distinguishes
// the two explicitly: the first assert proves a shop floor was really reached
// (SHOP moves only ever enumerate inside RunPhase::SHOP), the second that the
// rooms table counted it.
TEST(FuzzCoverage, ShopEntryCountsInTheRoomsTable) {
    Coverage cov;
    for (int64_t seed = 1; seed <= 40; ++seed) {
        CaseId id;
        id.run_seed = seed;
        id.ascension = 20;
        id.policy = PolicyKind::ALWAYS_EVENT;  // scores non-combat rooms at 100
        id.policy_seed = 0xC0FFEEull;
        CaseResult r;
        (void)run_case(id, limits(), &cov, r, false);
    }
    ASSERT_GT(cov.move_legal[static_cast<int>(MoveCat::SHOP)], 0u)
        << "no shop floor was ever reached in the 40-seed sweep -- widen it";
    EXPECT_GT(cov.room_entered[static_cast<int>(engine::RoomType::Shop)], 0u)
        << "shops were shopped in (SHOP moves were legal) but the rooms table "
           "never counted an entry";
}

// The seed-116 always_event probe reproducer, as a named in-tree case: beat
// the Act-1 boss, claim its rewards, press proceed -- and the case must not
// fail no_legal_moves on an empty mask the run layer advertised while claiming
// not to be terminal.
//
// WHERE THAT PRESS LEADS MOVED AT S2.12. It used to be the run's VICTORY
// terminal; it is now the ACT TRANSITION, so this trajectory crosses into Act 2
// and then parks on the first Act-2 room, whose monsters are S2.2x's. The
// property under test is unchanged -- the run ends CLEANLY at a named reason,
// having offered a legal move at every non-terminal step -- and the expected
// reason moves with the boundary. `victories` is 0 across the whole soak until
// S2.28 lands the Act-3 bosses; that residue is recorded in the s2-tasks.md
// S2.12 Log against that dependency, and the counter's coherence with
// run_is_victory() is pinned directly in act_transition_test.cpp instead.
TEST(FuzzGuard, Seed116AlwaysEventCrossesIntoActTwoAndEndsCleanly) {
    CaseId id;
    id.run_seed = 116;
    id.ascension = 20;
    id.policy = PolicyKind::ALWAYS_EVENT;
    id.policy_seed = 12948172379672766026ull;
    Coverage cov;
    CaseResult r;
    uint8_t max_act = 1;
    StepObserver obs;
    obs.ctx = &max_act;
    obs.fn = [](const engine::RunController& rc, void* ctx) noexcept {
        uint8_t& m = *static_cast<uint8_t*>(ctx);
        if (rc.run.act > m) m = rc.run.act;
    };
    EXPECT_TRUE(run_case(id, limits(), &cov, r, /*verify_repro=*/true, Inject{},
                         obs))
        << triage_text(id, r);
    EXPECT_NE(r.end_reason, EndReason::NO_LEGAL_MOVES)
        << "the regression this case is named for: an empty mask in a "
           "non-terminal phase";
    // WHERE THE TRAJECTORY ENDS MOVED AGAIN AT S2.32: the Act-2 ?-room bodies
    // this policy steers into are landing batch by batch (S2.32's ten here,
    // S2.31's eight still parking), so the specific terminal is content-
    // dependent -- today the run fights on into Act 2 and DIES there rather
    // than parking on the first bodiless room. The durable property stays
    // exactly what the case is named for: a clean end at a NAMED reason with
    // a legal move offered at every non-terminal step, having really crossed
    // the act boundary.
    EXPECT_TRUE(r.end_reason == EndReason::ROOM_UNIMPLEMENTED ||
                r.end_reason == EndReason::RUN_OVER)
        << "expected a park on unlanded content or a real terminal, got "
        << static_cast<int>(r.end_reason);
    EXPECT_GE(max_act, 2u) << "the act transition really ran";
    EXPECT_EQ(cov.victories, 0u)
        << "not a win -- the terminal is the Act-3 boss";
}

TEST(FuzzDriver, RejectsZeroWorkMalformedAndPartialCaseCli) {
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seeds garbage --quiet >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seeds 0 --quiet >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --reps 0 --quiet >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seed-start 9223372036854775807 --seeds 2"
                           " --quiet >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seeds 1 --progress-secs nan"
                           " --quiet >" + kNull + " 2>&1"),
              0);
    for (const char* policies :
         {"random,random", ",random", "random,,greedy_damage", "random,"}) {
        EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                               " --seeds 1 --policies " + policies +
                               " --quiet >" + kNull + " 2>&1"),
                  0)
            << policies;
    }
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seeds 1 --policies random"
                           " --policies greedy_damage"
                           " --quiet >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seeds 1 --inject-nondeterminism 0:1"
                           " --inject-abort 0:1"
                           " --quiet >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seeds 1 --inject-no-progress 0:1"
                           " --inject-no-progress 0:2"
                           " --quiet >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_REPRO_BIN) +
                           " --seed 1 --policy random --policy-seed 2"
                           " >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_REPRO_BIN) +
                           " dummy.repro --seed 1 --ascension 20"
                           " --policy random --policy-seed 2"
                           " >" + kNull + " 2>&1"),
              0);
}

TEST(FuzzDriver, RejectsMissingArtifactDirectoryAndMalformedSummary) {
    const std::string absent = scratch("does_not_exist/subdir");
    std::filesystem::remove_all(scratch("does_not_exist"));
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seeds 1 --out " + shell_quote(absent) +
                           " --quiet >" + kNull + " 2>&1"),
              0);

    const std::string malformed = scratch("malformed_summary.kv");
    {
        std::ofstream os(malformed, std::ios::binary);
        os << "STSFUZZ_SUMMARY v1\nbuild_id bad\n";
    }
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " +
                           shell_quote(malformed) +
                           " >" + kNull + " 2>&1"),
              0);
    EXPECT_NE(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) + " --quiet --merge " +
                           shell_quote(malformed) +
                           " >" + kNull + " 2>&1"),
              0);
    const std::string bash = sts::testing::bash_program();
    if (bash.empty()) {
        // Everything above already ran; only the script arm is unrunnable.
        GTEST_SKIP() << "no host bash found (STS_TEST_BASH empty) -- "
                        "tools/fuzz/soak.sh cannot be exercised here";
    }
    EXPECT_NE(run_shell(bash +
                           shell_quote(STS_FUZZ_SOAK_SCRIPT) + " --main-bin " +
                           shell_quote(STS_FUZZ_SOAK_BIN) +
                           " --seed-start 9223372036854775807 --seeds 2"
                           " --dry-run >" + kNull + " 2>&1"),
              0);
    std::remove(malformed.c_str());
}

TEST(FuzzRunner, SanitizerPercentageUsesCeilingDivision) {
    const std::string bash = sts::testing::bash_program();
    if (bash.empty()) {
        GTEST_SKIP() << "no host bash found (STS_TEST_BASH empty) -- "
                        "tools/fuzz/soak.sh cannot be exercised here";
    }
    const std::string log = scratch("asan_ceiling.log");
    const std::string command =
        bash + shell_quote(STS_FUZZ_SOAK_SCRIPT) +
        " --main-bin " + shell_quote(STS_FUZZ_SOAK_BIN) + " --asan-bin " +
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seeds 101 --asan-percent 1 --dry-run >" + shell_quote(log) +
        " 2>&1";
    ASSERT_EQ(run_shell(command), 0) << read_text(log);
    const std::string text = read_text(log);
    EXPECT_NE(text.find("--seeds 2"), std::string::npos) << text;
    EXPECT_NE(text.find("asan: "), std::string::npos) << text;
    EXPECT_NE(text.find("--seed-start 102"), std::string::npos) << text;
    std::remove(log.c_str());
}

// --- 5. per-act coverage, and the boss chest's weights (S2.41) ---------------
//
// The S2-G1 gate soak's claim is "three-act A20 runs". Before S2.41 the report
// could not distinguish a sweep that walked three acts from one that died on
// floor 6 every time, so these tests hold the per-act tables to the invariants
// that make them a witness rather than four more numbers.

TEST(FuzzCoverage, PerActTablesAgreeWithTheActBlindOnesOverASweep) {
    Coverage cov;
    for (int64_t seed = 1; seed <= 40; ++seed) {
        for (uint8_t p = 0; p < static_cast<uint8_t>(PolicyKind::COUNT); ++p) {
            CaseId id;
            id.run_seed = seed;
            id.ascension = 20;
            id.policy = static_cast<PolicyKind>(p);
            id.policy_seed = 0xC0FFEEull;
            CaseResult r;
            (void)run_case(id, limits(), &cov, r, false);
        }
    }
    ASSERT_GT(cov.cases, 0u);

    // Every run begins in act 1 and no run can skip an act, so this is an
    // identity rather than a threshold: if it ever fails, the per-act sampling
    // missed steps, not the runs.
    EXPECT_EQ(cov.act_cases[1], cov.cases);
    EXPECT_GE(cov.max_act, 1u);

    // The split is a PARTITION of the act-blind rooms table: the same events,
    // filed by act. A row that did not sum would mean an entry counted in one
    // table and not the other -- the shape of the shop-entry hole.
    for (int r = 1; r < kRoomTypeCount; ++r) {
        uint64_t summed = 0;
        for (int a = 0; a < kActBuckets; ++a) summed += cov.act_rooms[a][r];
        EXPECT_EQ(summed, cov.room_entered[r]) << "room type " << r;
    }

    for (int a = 2; a < kActBuckets; ++a) {
        EXPECT_LE(cov.act_boss_kills[a], cov.act_boss_fights[a])
            << "act " << a << ": a kill was counted without its fight";
        if (cov.act_boss_fights[a] > 0) {
            EXPECT_GT(cov.act_cases[a], 0u)
                << "act " << a << ": a boss was fought in an act no case entered";
        }
        if (cov.act_cases[a] > 0) {
            EXPECT_GE(cov.max_act, static_cast<uint32_t>(a));
            EXPECT_GT(cov.act_cases[a - 1], 0u)
                << "act " << a << " was entered without its predecessor";
        }
    }
    EXPECT_LE(cov.act_boss_kills[1], cov.act_boss_fights[1]);

    // The two independent probes for the same event. `victories` is
    // run_is_victory() at the terminal; act_boss_kills[3] is the act-3 boss
    // combat leaving COMBAT with KILLED. They cannot disagree.
    EXPECT_EQ(cov.act_boss_kills[engine::kFinalAct], cov.victories);

    // Act 2 is a REACH result and a 200-case sweep is not guaranteed to contain
    // one, so the assertion is on the machinery -- the act-1 boss chest is the
    // only door into act 2 -- rather than on luck.
    if (cov.act_cases[2] > 0) {
        EXPECT_GT(cov.act_boss_kills[1], 0u)
            << "act 2 was entered without an act-1 boss kill";
    }
}

TEST(FuzzCoverage, PerActTablesWitnessTheSeed116ActTwoCrossing) {
    // The same pinned crossing FuzzGuard's seed-116 case uses, read through the
    // per-act tables: what that test asserts with its own StepObserver, the
    // coverage instrument must report on its own.
    CaseId id;
    id.run_seed = 116;
    id.ascension = 20;
    id.policy = PolicyKind::ALWAYS_EVENT;
    id.policy_seed = 12948172379672766026ull;
    Coverage cov;
    CaseResult r;
    ASSERT_TRUE(run_case(id, limits(), &cov, r, false)) << triage_text(id, r);

    EXPECT_EQ(cov.act_cases[1], 1u);
    EXPECT_EQ(cov.act_cases[2], 1u) << "the act transition really ran";
    EXPECT_GE(cov.max_act, 2u);
    EXPECT_GT(cov.act_boss_fights[1], 0u);
    EXPECT_GT(cov.act_boss_kills[1], 0u);
    EXPECT_EQ(cov.act_boss_kills[engine::kFinalAct], cov.victories);
}

TEST(FuzzCoverage, ReportWitnessesActsAndNamesTheUnreachedOnes) {
    Coverage cov;
    CaseResult r;
    ASSERT_TRUE(run_case(make_case(PolicyKind::GREEDY_DAMAGE), limits(), &cov, r,
                         false));
    const std::string text = cov.report(1.0);
    EXPECT_NE(text.find("-- per act ("), std::string::npos);
    EXPECT_NE(text.find("deepest act seen"), std::string::npos);
    // A single Act-1 case must SAY that it never left act 1, in the same
    // NEVER REACHED block that names an unused move category.
    if (cov.act_cases[3] == 0) {
        EXPECT_NE(text.find("act never entered by any case: 3"),
                  std::string::npos)
            << text;
    }
    if (cov.victories == 0) {
        EXPECT_NE(text.find("the run was never WON"), std::string::npos) << text;
    }
}

TEST(FuzzCoverage, PerActCountersRoundTripThroughTheKvForm) {
    // The kv form is what a shard merge carries; a per-act counter that did not
    // survive it would silently understate the campaign total.
    Coverage a;
    a.cases = 1;
    a.act_cases[1] = 7;
    a.act_cases[3] = 2;
    a.act_boss_fights[2] = 5;
    a.act_boss_kills[2] = 4;
    a.act_rooms[3][static_cast<int>(engine::RoomType::Elite)] = 11;
    a.max_act = 3;

    Coverage back;
    ASSERT_TRUE(coverage_from_kv(a.kv(), back));
    EXPECT_EQ(back.act_cases[1], 7u);
    EXPECT_EQ(back.act_cases[3], 2u);
    EXPECT_EQ(back.act_boss_fights[2], 5u);
    EXPECT_EQ(back.act_boss_kills[2], 4u);
    EXPECT_EQ(back.act_rooms[3][static_cast<int>(engine::RoomType::Elite)], 11u);
    EXPECT_EQ(back.max_act, 3u);

    Coverage sum = a;
    sum.merge(back);
    EXPECT_EQ(sum.act_cases[1], 14u);   // additive
    EXPECT_EQ(sum.max_act, 3u);         // max, not sum

    // max_act joins the maxima through the same visitor the other three use, so
    // a summary missing it is drift, not vintage.
    std::string text = a.kv();
    const std::size_t at = text.find("\nmax_act ");
    ASSERT_NE(at, std::string::npos);
    const std::size_t eol = text.find('\n', at + 1);
    ASSERT_NE(eol, std::string::npos);
    const std::string mangled = text.substr(0, at) + text.substr(eol);
    Coverage out;
    std::vector<std::string> defaulted;
    EXPECT_FALSE(coverage_from_kv(mangled, out));
    EXPECT_FALSE(coverage_from_kv_legacy(mangled, out, defaulted));
}

// REGRESSION (S2.41 probe, 2026-08-09): S2.11 spent four MoveCat values on the
// boss chest and enumerated all four moves, but `move_score` never grew an arm
// for them -- so every one fell through its final `return 0` and the whole room
// was a single uniform tie-break for all four heuristics. The 300-seed probe
// read `boss_chest_open` legal twice and TAKEN zero times, with `pick` and
// `skip` never legal at all, which looks exactly like unreachable content and
// was in fact a missing preference. (GCC said so, as a -Wswitch warning; this
// project promotes only the conversion pair to errors, so nothing failed.)
//
// `move_score` is internal, so the preference is pinned where it is observable:
// policy_pick over a synthesized two-move set. Neither category reads the
// controller, so a fresh run_begin is a sufficient context.
TEST(FuzzPolicy, BossChestPreferenceIsScoredRatherThanLeftToTheTieBreak) {
    engine::RunController rc = engine::run_begin(7, 20);
    const auto pick_between = [&](PolicyKind kind, MoveCat lo, MoveCat hi,
                                  uint64_t pseed) {
        Move moves[2];
        moves[0].cat = lo;
        moves[0].action = engine::Action{1};
        moves[1].cat = hi;
        moves[1].action = engine::Action{2};
        PolicyRng rng(pseed);
        return moves[policy_pick(kind, rc, moves, 2, rng)].cat;
    };

    for (uint64_t pseed = 1; pseed <= 8; ++pseed) {
        for (PolicyKind kind : {PolicyKind::GREEDY_DAMAGE, PolicyKind::GREEDY_BLOCK,
                                PolicyKind::ALWAYS_EVENT}) {
            // The depth policies OPEN the chest rather than walking past it.
            EXPECT_EQ(pick_between(kind, MoveCat::BOSS_CHEST_PROCEED,
                                   MoveCat::BOSS_CHEST_OPEN, pseed),
                      MoveCat::BOSS_CHEST_OPEN)
                << policy_name(kind);
        }
        // hoard_gold walks past WITHOUT opening -- trap 3's live case, where the
        // three relics burn at room entry and the chest is never touched.
        EXPECT_EQ(pick_between(PolicyKind::HOARD_GOLD, MoveCat::BOSS_CHEST_PROCEED,
                               MoveCat::BOSS_CHEST_OPEN, pseed),
                  MoveCat::BOSS_CHEST_PROCEED);
        // Once open, every E0 heuristic but one PICKS. Skip is a reversible
        // screen close that re-advertises `open`, so a policy scoring it ABOVE
        // pick would sit in the open/skip 2-cycle (s242-deep-reach §7). The
        // loop is bounded at SIM_SEARCH (S2.V2): the sim-consulting kinds read
        // the REAL rc.run.boss_chest state, which this synthetic two-move
        // harness does not stage -- their chest behaviour is pinned on a
        // staged chest below.
        for (uint8_t p = 1; p < static_cast<uint8_t>(PolicyKind::SIM_SEARCH);
             ++p) {
            const auto kind = static_cast<PolicyKind>(p);
            if (kind == PolicyKind::GREEDY_BLOCK) continue;
            EXPECT_EQ(pick_between(kind, MoveCat::BOSS_CHEST_SKIP,
                                   MoveCat::BOSS_CHEST_PICK, pseed),
                      MoveCat::BOSS_CHEST_PICK)
                << policy_name(kind);
        }
    }

    // S2.V2: the sim-consulting cohort pair, on a STAGED RELIC_SELECT chest
    // (three takeable offers). The TAKE identity picks; the SKIP identity --
    // the sim-side mirror of policy_bossrelic_skip.json -- skips; and after a
    // skip (`seen` latched) NEITHER identity re-opens, which is the state
    // that breaks the open/skip 2-cycle for a deterministic argmax.
    {
        // REAL actions, not the synthetic bits the E0 harness uses: the sim
        // policies carry a run-layer no-op guard that proves the winning
        // candidate MUTATES the controller on a snapshot, so a fake action
        // the engine ignores is (correctly) demoted. The staged chest is a
        // legal RELIC_SELECT with three takeable offers.
        const auto pick_real = [&](PolicyKind kind, MoveCat lo_cat,
                                   engine::Action lo, MoveCat hi_cat,
                                   engine::Action hi, uint64_t pseed) {
            Move moves[2];
            moves[0].cat = lo_cat;
            moves[0].action = lo;
            moves[1].cat = hi_cat;
            moves[1].action = hi;
            PolicyRng rng(pseed);
            return moves[policy_pick(kind, rc, moves, 2, rng)].cat;
        };
        const engine::Action pick_a =
            engine::make_action(engine::ActionVerb::CHOOSE, 0);
        const engine::Action skip_a = engine::make_action(
            engine::ActionVerb::CHOOSE, engine::kChooseCancelGrid);
        const engine::Action open_a = engine::make_action(
            engine::ActionVerb::CHOOSE, engine::kChooseOpenChest);
        const engine::Action proceed_a = engine::make_action(
            engine::ActionVerb::CHOOSE, engine::kChooseProceed);
        rc.phase = static_cast<uint8_t>(engine::RunPhase::BOSS_TREASURE);
        rc.room_type = static_cast<uint8_t>(engine::RoomType::TreasureBoss);
        rc.run.boss_chest.seen = 1;
        rc.run.boss_chest.relics[0] =
            static_cast<uint16_t>(engine::RelicId::BLACK_STAR);
        rc.run.boss_chest.relics[1] =
            static_cast<uint16_t>(engine::RelicId::COFFEE_DRIPPER);
        rc.run.boss_chest.relics[2] =
            static_cast<uint16_t>(engine::RelicId::PHILOSOPHERS_STONE);
        for (uint64_t pseed = 1; pseed <= 8; ++pseed) {
            rc.run.boss_chest.screen =
                static_cast<uint8_t>(engine::BossChestScreen::RELIC_SELECT);
            EXPECT_EQ(pick_real(PolicyKind::SIM_SEARCH,
                                MoveCat::BOSS_CHEST_SKIP, skip_a,
                                MoveCat::BOSS_CHEST_PICK, pick_a, pseed),
                      MoveCat::BOSS_CHEST_PICK);
            EXPECT_EQ(pick_real(PolicyKind::SIM_SEARCH_SKIP,
                                MoveCat::BOSS_CHEST_SKIP, skip_a,
                                MoveCat::BOSS_CHEST_PICK, pick_a, pseed),
                      MoveCat::BOSS_CHEST_SKIP);
            // seen == 1: leaving beats re-opening, in both identities --
            // the state that breaks the open/skip 2-cycle.
            rc.run.boss_chest.screen =
                static_cast<uint8_t>(engine::BossChestScreen::CLOSED);
            EXPECT_EQ(pick_real(PolicyKind::SIM_SEARCH,
                                MoveCat::BOSS_CHEST_OPEN, open_a,
                                MoveCat::BOSS_CHEST_PROCEED, proceed_a,
                                pseed),
                      MoveCat::BOSS_CHEST_PROCEED);
            EXPECT_EQ(pick_real(PolicyKind::SIM_SEARCH_SKIP,
                                MoveCat::BOSS_CHEST_OPEN, open_a,
                                MoveCat::BOSS_CHEST_PROCEED, proceed_a,
                                pseed),
                      MoveCat::BOSS_CHEST_PROCEED);
        }
        // Restore the pristine controller for the blocks below.
        rc = engine::run_begin(7, 20);
    }

    // greedy_block scores skip EQUAL to pick, which is what makes
    // BOSS_CHEST_SKIP reachable at soak scale at all: the chests a soak reaches
    // belong to the policies that always pick, so without one policy on the
    // tie-break the category reads "never taken" forever. Equal, never above --
    // above is the 2-cycle.
    {
        bool block_skipped = false;
        bool block_picked = false;
        for (uint64_t pseed = 1; pseed <= 32; ++pseed) {
            const MoveCat got =
                pick_between(PolicyKind::GREEDY_BLOCK, MoveCat::BOSS_CHEST_SKIP,
                             MoveCat::BOSS_CHEST_PICK, pseed);
            block_skipped |= got == MoveCat::BOSS_CHEST_SKIP;
            block_picked |= got == MoveCat::BOSS_CHEST_PICK;
        }
        EXPECT_TRUE(block_skipped && block_picked);
    }

    // ... and `random` reaches both sides of that choice, which is what keeps
    // BOSS_CHEST_SKIP from being permanently unreachable in a soak.
    bool saw_skip = false;
    bool saw_pick = false;
    for (uint64_t pseed = 1; pseed <= 32; ++pseed) {
        const MoveCat got = pick_between(PolicyKind::RANDOM, MoveCat::BOSS_CHEST_SKIP,
                                         MoveCat::BOSS_CHEST_PICK, pseed);
        saw_skip |= got == MoveCat::BOSS_CHEST_SKIP;
        saw_pick |= got == MoveCat::BOSS_CHEST_PICK;
    }
    EXPECT_TRUE(saw_skip && saw_pick);
}

// --- 6. sharding and resume, over three-act volume (S2.41) -------------------
//
// The B5.1 acceptance says "shard/resume paths proven". The soak has no
// checkpoint file and does not want one: a SHARD is the restartable unit, and
// what has to be true is (a) the shards PARTITION the sweep -- their merged
// report equals the unsharded one exactly, not approximately -- and (b) a shard
// re-run after an interruption reproduces itself BYTE FOR BYTE, including under
// a different worker-thread count, so a resumed sweep cannot double-count or
// lose a case. Both are stated here as file comparisons rather than as a
// handful of spot-checked counters.

TEST(FuzzDriver, ShardedSweepMergesToExactlyTheUnshardedSweep) {
    const std::string whole_label = "shard_whole";
    const std::string split_label = "shard_split";
    const std::string log = scratch("shard_equiv.log");
    const std::string whole_report = scratch("shard_whole_merged.txt");
    const std::string split_report = scratch("shard_split_merged.txt");
    const std::string sweep =
        " --seed-start 501 --seeds 4 --policies random,greedy_damage,always_event"
        " --max-actions 1500 --verify-repro-every 3 --out " +
        shell_quote(STS_FUZZ_SCRATCH);

    // One process, the whole sweep.
    ASSERT_EQ(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) + sweep + " --label " +
                        whole_label + " --threads 1 --shard 0/1 --quiet >" +
                        shell_quote(log) + " 2>&1"),
              0)
        << read_text(log);
    // The same sweep cut three ways, each a separate process, and two of them
    // with a different thread count -- a shard's identity must not depend on
    // how many workers happened to be free when it ran.
    for (int i = 0; i < 3; ++i) {
        const std::string threads = i == 1 ? " --threads 3" : " --threads 2";
        ASSERT_EQ(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) + sweep + " --label " +
                            split_label + threads + " --shard " +
                            std::to_string(i) + "/3 --quiet >>" +
                            shell_quote(log) + " 2>&1"),
                  0)
            << read_text(log);
    }

    const std::string whole_kv = scratch(whole_label + "_summary_s0.kv");
    ASSERT_EQ(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " +
                        shell_quote(whole_kv) + " >" +
                        shell_quote(whole_report) + " 2>&1"),
              0)
        << read_text(whole_report);
    std::string merge = shell_quote(STS_FUZZ_SOAK_BIN) + " --merge";
    for (int i = 0; i < 3; ++i) {
        merge += " " + shell_quote(scratch(split_label + "_summary_s" +
                                           std::to_string(i) + ".kv"));
    }
    ASSERT_EQ(run_shell(merge + " >" + shell_quote(split_report) + " 2>&1"), 0)
        << read_text(split_report);

    // Both merge paths call report(0.0), so there is no elapsed line and no
    // wall-clock term anywhere in either text: this is an EXACT comparison.
    const std::string whole_text = read_text(whole_report);
    const std::string split_text = read_text(split_report);
    ASSERT_FALSE(whole_text.empty());
    EXPECT_EQ(whole_text, split_text)
        << "a 3-way shard split did not reproduce the unsharded sweep";
    // ... and it is not vacuously equal on an empty sweep.
    EXPECT_NE(whole_text.find("cases (seed x policy x policy-seed) : 12"),
              std::string::npos)
        << whole_text.substr(0, 200);
    EXPECT_NE(whole_text.find("-- per act ("), std::string::npos);

    for (const std::string& p : {log, whole_report, split_report, whole_kv}) {
        std::remove(p.c_str());
    }
    for (int i = 0; i < 3; ++i) {
        std::remove(scratch(split_label + "_summary_s" + std::to_string(i) + ".kv")
                        .c_str());
        std::remove(scratch(split_label + "_report_s" + std::to_string(i) + ".txt")
                        .c_str());
    }
    std::remove(scratch(whole_label + "_report_s0.txt").c_str());
}

TEST(FuzzDriver, AnInterruptedShardResumesToAByteIdenticalSummary) {
    // "Resume" here means: re-run the shard. The property that makes that safe
    // is that a shard is a pure function of (sweep options, shard index) -- not
    // of the thread count, not of what the other shards did, and not of what a
    // previous attempt left on disk.
    const std::string label = "shard_resume";
    const std::string log = scratch("shard_resume.log");
    const std::string kv = scratch(label + "_summary_s1.kv");
    const std::string sweep =
        " --seed-start 733 --seeds 3 --policies greedy_block,always_event"
        " --max-actions 1500 --out " + shell_quote(STS_FUZZ_SCRATCH) +
        " --label " + label + " --shard 1/3 --quiet";

    ASSERT_EQ(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) + sweep + " --threads 1 >" +
                        shell_quote(log) + " 2>&1"),
              0)
        << read_text(log);
    const std::string first = read_text(kv);
    ASSERT_NE(first.find("STSFUZZ_SUMMARY v1\n"), std::string::npos);
    ASSERT_NE(first.find("shard 1\n"), std::string::npos);

    // The interruption: the artifact is gone and the shard runs again, this
    // time with more workers.
    std::remove(kv.c_str());
    ASSERT_EQ(run_shell(shell_quote(STS_FUZZ_SOAK_BIN) + sweep + " --threads 2 >>" +
                        shell_quote(log) + " 2>&1"),
              0)
        << read_text(log);
    EXPECT_EQ(read_text(kv), first)
        << "a re-run shard did not reproduce itself byte for byte";

    // A clean shard leaves no in-flight journal behind -- that file is the
    // crash reproducer, and a stale one would be read as a dead worker.
    EXPECT_FALSE(std::filesystem::exists(
        scratch(label + "_inflight_s1_t0.txt")));

    std::remove(log.c_str());
    std::remove(kv.c_str());
    std::remove(scratch(label + "_report_s1.txt").c_str());
}

// --- 8. the Curiosity hold (S2.V2's Awakened One discharge, 2026-08-27) ------
//
// PolicyKind::SIM_SEARCH_HOLD is SIM_SEARCH plus exactly one rule: while a live
// monster owns PowerId::CURIOSITY, a POWER-card play is priced at what the tax
// buys the boss. The rule was MEASURED HARMFUL (5 Awakened One kills against
// SIM_SEARCH's 22 over the same 1,929-fight grid --
// docs/verification/s2v2-sim-reach.md section 6), which is exactly why the
// tests below pin two different things: that the rule really is the mechanic it
// names, and that SIM_SEARCH cannot feel it.
//
// GROUND TRUTH, cited where it is used:
//   CuriosityPower.java:42-47  onUseCard -- a POWER play grants the OWNER
//                              `amount` Strength, and nothing else does.
//   AwakenedOne.java:146,149   the ONLY applier: CuriosityPower(this, 2) at
//                              ascension >= 19, (this, 1) below it. The owner
//                              is the boss, so the encounter's two Cultists
//                              (encounters.yaml id 58) never carry it.
//   AwakenedOne.java:302-308   the Rebirth purge removes Curiosity by name, so
//                              phase 2 does not tax at all.
//   AwakenedOne.java:89        SS_AMT = 4, the widest multi-hit either phase
//                              has, which is the per-turn multiplier on one
//                              stack of Strength.

namespace {

// The Awakened One's board as a real fight starts it: the boss alone, its
// pre-battle grants resolved. (The Cultists are omitted deliberately -- part of
// the point below is that they contribute nothing to the tax.)
engine::CombatState MakeAwakenedBoard() {
    engine::CombatState s{};
    s.player_hp = 60;
    s.player_max_hp = 80;
    s.player_energy = 3;
    s.monster_count = 1;
    s.monster_hp_rng = engine::from_seed(3);
    s.ai_rng = engine::from_seed(3);
    s.card_random_rng = engine::from_seed(3);
    engine::awakened_one_init(s, 0);
    engine::awakened_one_use_pre_battle_action(s, 0);
    while (s.action_count > 0) {
        const engine::ActionQueueItem it = s.action_queue[s.action_head];
        s.action_head = static_cast<uint8_t>((s.action_head + 1) %
                                             engine::kActionQueueCap);
        --s.action_count;
        engine::execute_opcode(s, it);
    }
    return s;
}

// Put `id` in hand slot `slot`, backed by its own card-pool row.
void PutInHand(engine::CombatState& s, uint8_t slot, engine::CardId id) {
    const auto pool = static_cast<engine::CardPoolIndex>(slot);
    s.card_pool[pool].card_id = static_cast<uint16_t>(id);
    s.card_pool[pool].upgrade = 0;
    s.hand[slot] = pool;
    if (slot >= s.hand_count) s.hand_count = static_cast<uint8_t>(slot + 1);
}

Move PlayMove(uint8_t hand_slot) {
    Move m;
    m.cat = MoveCat::PLAY_CARD;
    m.action = engine::make_action(engine::ActionVerb::PLAY_CARD, hand_slot);
    return m;
}

}  // namespace

TEST(SimSearchCuriosityHold, TaxIsTheLiveCuriosityStackAndDiesWithTheRebirth) {
    engine::CombatState s = MakeAwakenedBoard();
    // The A20 amount is 2 (AwakenedOne.java:144-146). The tax is READ off the
    // board, not assumed, so a change to that grant moves the rule with it.
    EXPECT_EQ(sim_search_curiosity_tax(s), 2);

    // The Rebirth purge names Curiosity (AwakenedOne.java:302-308): phase 2
    // keeps the Strength phase 1 bought but stops taxing, so the rule must
    // switch itself off exactly there.
    engine::ActionQueueItem hit{};
    hit.opcode = static_cast<uint16_t>(engine::Opcode::DAMAGE);
    hit.src = engine::kActorPlayer;
    hit.tgt = 0;
    hit.amount = 1000;
    hit.flags = engine::make_damage_flags(engine::DamageType::NORMAL);
    engine::execute_opcode(s, hit);
    ASSERT_LE(s.monsters[0].hp, 0) << "the half-death did not land";
    EXPECT_EQ(sim_search_curiosity_tax(s), 0)
        << "Curiosity is purged at the Rebirth (AwakenedOne.java:306)";

    // And an empty board -- the shape every other combat in the game presents
    // to this criterion -- never had it.
    const engine::CombatState none{};
    EXPECT_EQ(sim_search_curiosity_tax(none), 0);
}

TEST(SimSearchCuriosityHold, PricesPowerPlaysOnlyAndOnlyWhileTheTaxIsLive) {
    engine::RunController rc = engine::run_begin(7, 20);
    rc.phase = static_cast<uint8_t>(engine::RunPhase::COMBAT);
    rc.room_type = static_cast<uint8_t>(engine::RoomType::Boss);
    rc.combat = MakeAwakenedBoard();
    PutInHand(rc.combat, 0, engine::CardId::INFLAME);  // POWER
    PutInHand(rc.combat, 1, engine::CardId::STRIKE);   // ATTACK

    // tax(2) * SS_AMT(4) * kRolloutTurnCap(20) * kEvalPlayerHp(300).
    constexpr int64_t kExpected = 2 * 4 * 20 * 300;
    EXPECT_EQ(sim_search_curiosity_penalty(rc, PlayMove(0)), kExpected);
    // Only the card TYPE the Java tests (CuriosityPower.java:43) is charged.
    EXPECT_EQ(sim_search_curiosity_penalty(rc, PlayMove(1)), 0);
    Move end;
    end.cat = MoveCat::END_TURN;
    end.action = engine::make_action(engine::ActionVerb::END_TURN);
    EXPECT_EQ(sim_search_curiosity_penalty(rc, end), 0);

    // Outside combat there is no board to read, so there is no price.
    engine::RunController out = rc;
    out.phase = static_cast<uint8_t>(engine::RunPhase::MAP_CHOICE);
    EXPECT_EQ(sim_search_curiosity_penalty(out, PlayMove(0)), 0);

    // The same hand against a board with no Curiosity on it: the identical
    // POWER play is free. That is the non-interference claim, at the price.
    engine::RunController plain = rc;
    plain.combat.monsters[0].power_count = 0;
    EXPECT_EQ(sim_search_curiosity_penalty(plain, PlayMove(0)), 0);
}

TEST(SimSearchCuriosityHold, NeverFiresOutsideAnAwakenedOneFight) {
    // The structural half of non-interference: nothing in the registry applies
    // PowerId::CURIOSITY except AwakenedOne.usePreBattleAction, so on a line
    // that never meets that boss the tax is zero at EVERY combat decision --
    // and a zero tax cannot move a score. Walked over real SIM_SEARCH lines
    // rather than argued from the registry, because the claim is about what the
    // policy sees.
    for (int64_t seed : {11, 12, 13}) {
        engine::RunController rc = engine::run_begin(seed, 20);
        PolicyRng rng(0xC0FFEEull + static_cast<uint64_t>(seed));
        int combat_decisions = 0;
        for (int step = 0; step < 300; ++step) {
            if (rc.phase == static_cast<uint8_t>(engine::RunPhase::RUN_OVER)) {
                break;
            }
            engine::RunActionMask mask;
            engine::legal_actions(rc, mask);
            Move moves[kMoveCap];
            const size_t n = enumerate_moves(rc, mask, moves, kMoveCap);
            if (n == 0) break;
            if (rc.phase == static_cast<uint8_t>(engine::RunPhase::COMBAT)) {
                ASSERT_EQ(sim_search_curiosity_tax(rc.combat), 0)
                    << "seed " << seed << " step " << step;
                ++combat_decisions;
            }
            const size_t i =
                policy_pick(PolicyKind::SIM_SEARCH, rc, moves, n, rng);
            engine::Action a = moves[i].action;
            engine::StepResult res{};
            engine::advance({&rc, 1}, {&a, 1}, {&res, 1});
        }
        EXPECT_GT(combat_decisions, 0)
            << "seed " << seed << " never reached a combat -- vacuous";
    }
}

TEST(SimSearchCuriosityHold, LeavesSimSearchTrajectoriesIdentical) {
    // The behavioural half: on a line that never meets Curiosity the two kinds
    // are the SAME policy, step for step. They are allowed to diverge only
    // inside an Awakened One phase 1, which no line this shallow reaches.
    for (int64_t seed : {11, 12, 13}) {
        CaseId plain;
        plain.run_seed = seed;
        plain.ascension = 20;
        plain.policy = PolicyKind::SIM_SEARCH;
        plain.policy_seed = 0xC0FFEEull;
        CaseId held = plain;
        held.policy = PolicyKind::SIM_SEARCH_HOLD;

        CaseResult a, b;
        ASSERT_TRUE(run_case(plain, limits(600), nullptr, a, false));
        ASSERT_TRUE(run_case(held, limits(600), nullptr, b, false));
        EXPECT_EQ(a.final_hash, b.final_hash) << "seed " << seed;
        ASSERT_EQ(a.trajectory.size(), b.trajectory.size()) << "seed " << seed;
        for (size_t k = 0; k < a.trajectory.size(); ++k) {
            ASSERT_EQ(a.trajectory[k].bits, b.trajectory[k].bits)
                << "seed " << seed << " diverged at step " << k;
        }
    }
}
