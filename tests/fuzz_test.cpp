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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sts/engine/run_advance.hpp"
#include "sts/engine/state_hash.hpp"
#include "sts/fuzz/coverage.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/policy.hpp"
#include "sts/fuzz/repro.hpp"

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

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

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
    const int child_rc = std::system(command.c_str());
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
    const int replay_rc = std::system(replay_command.c_str());
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
    const std::string kv = scratch(label + "_summary_s0.kv");
    const std::string report = scratch(label + "_report_s0.txt");
    std::remove(child_log.c_str());
    std::remove(merge_log.c_str());
    std::remove(kv.c_str());
    std::remove(report.c_str());

    const std::string command =
        shell_quote(STS_FUZZ_SOAK_BIN) +
        " --seed-start 41 --seeds 2 --policies random,greedy_damage"
        " --threads 2 --max-actions 2000 --verify-repro-every 1 --out " +
        shell_quote(STS_FUZZ_SCRATCH) + " --label " + label +
        " --quiet >" + shell_quote(child_log) + " 2>&1";
    ASSERT_EQ(std::system(command.c_str()), 0) << read_text(child_log);

    Coverage persisted;
    ASSERT_TRUE(coverage_from_kv(read_text(kv), persisted));
    EXPECT_EQ(persisted.cases, 4u);
    EXPECT_GE(persisted.runs, 12u)
        << "every case should execute passes A, B, and sampled replay C";
    EXPECT_GT(persisted.actions, 0u);
    EXPECT_EQ(persisted.per_policy_cases[static_cast<int>(PolicyKind::RANDOM)], 2u);
    EXPECT_EQ(
        persisted.per_policy_cases[static_cast<int>(PolicyKind::GREEDY_DAMAGE)], 2u);

    const std::string merge_command =
        shell_quote(STS_FUZZ_SOAK_BIN) + " --merge " + shell_quote(kv) + " >" +
        shell_quote(merge_log) + " 2>&1";
    ASSERT_EQ(std::system(merge_command.c_str()), 0) << read_text(merge_log);
    const std::string merged = read_text(merge_log);
    EXPECT_NE(merged.find("cases (seed x policy x policy-seed) : 4"),
              std::string::npos);
    EXPECT_NE(merged.find("ACTIONS (counted once per case)"), std::string::npos);

    std::remove(child_log.c_str());
    std::remove(merge_log.c_str());
    std::remove(kv.c_str());
    std::remove(report.c_str());
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
    const int child_rc = std::system(command.c_str());
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
    const int replay_rc = std::system(replay_command.c_str());
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

TEST(FuzzTriage, ReproducerRoundTripsEveryActionVerb) {
    ReproFile rf;
    rf.id = make_case(PolicyKind::HOARD_GOLD, 777);
    rf.actions = {
        engine::make_action(engine::ActionVerb::CHOOSE, engine::kChooseProceed),
        engine::make_action(engine::ActionVerb::PLAY_CARD, 3, 2),
        engine::make_action(engine::ActionVerb::END_TURN),
        engine::make_action(engine::ActionVerb::USE_POTION, 1, 0),
        engine::make_action(engine::ActionVerb::CHOOSE, engine::kChooseBoss),
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
