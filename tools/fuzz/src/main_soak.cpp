// fuzz_soak -- the sim-side self-replay fuzz driver (ledger B5.1, design
// §7.1(2)).
//
// SWEEP SHAPE. The unit of work is a CASE: (run seed, ascension, policy,
// policy seed). Seeds sweep sequentially (design §7.2's "sequential sweep for
// coverage"), every policy runs on every seed, and `--reps` gives each pair
// several independent policy seeds so one map layout is explored more than
// once. Each case is executed at least twice and its state hashes compared
// (stage-a §2); the ACTION COUNT reported for acceptance is counted once per
// case, from the pass of record, so it is not inflated by the replay.
//
// CRASH TRIAGE. Before a case is stepped, its identity is written to an
// in-flight journal (one per worker thread) and flushed. An assert, a segv or
// an ASan abort therefore leaves on disk the four values that regenerate the
// exact trajectory -- see case_id.hpp for why four values are sufficient. The
// soak script prints those journals when a worker dies.
//
// HASH-MISMATCH TRIAGE. A divergence emits a `STSFUZZ v1` reproducer carrying
// both the case id and the literal action prefix, plus a decoded triage block
// on stderr. `fuzz_repro <file>` replays it with no fuzzer involved.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sts/fuzz/coverage.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/repro.hpp"

namespace {

using namespace sts::fuzz;

struct Options {
    int64_t seed_start = 1;
    uint64_t seeds = 1000;
    uint32_t reps = 1;
    uint8_t ascension = 20;
    std::vector<PolicyKind> policies;
    RunLimits limits;
    unsigned threads = 1;
    uint64_t verify_repro_every = 256;
    std::string out_dir;
    std::string label = "soak";
    uint64_t shard = 0;
    uint64_t shard_count = 1;
    uint64_t max_failures = 20;
    bool quiet = false;
    double progress_secs = 15.0;
    // Fault injection, for exercising the triage path on demand. Injects a
    // driver-side state perturbation into the replay pass of ONE case.
    uint64_t inject_case = UINT64_MAX;
    uint32_t inject_step = 0;
    bool inject_abort = false;
};

void usage() {
    std::fprintf(stderr, R"(fuzz_soak -- sim-side self-replay fuzz soak (B5.1)

  --seed-start N        first run seed (default 1)
  --seeds N             number of run seeds to sweep (default 1000)
  --reps N              policy seeds per (seed, policy) pair (default 1)
  --ascension N         ascension level (default 20)
  --policies a,b,...    subset of: random greedy_damage greedy_block hoard_gold
                        always_event   (default: all)
  --max-actions N       per-run action cap (default 4000)
  --revisit-limit N     consecutive state revisits that end a run as LIVELOCK
                        (default 64; see coverage.hpp for why this is not a
                        failure by default)
  --fail-on-livelock    promote LIVELOCK to a failure (writes a reproducer)
  --threads N           worker threads (default 1)
  --verify-repro-every N  run the literal-action-log replay pass on every Nth
                        case, so the reproducer path is continuously exercised
                        (default 256; 0 disables)
  --out DIR             write summary / reproducers / in-flight journals here
  --label NAME          filename prefix under --out (default 'soak')
  --shard I/N           run only cases with (index %% N) == I
  --max-failures N      stop after N failures (default 20)
  --progress-secs S     progress line interval, 0 to silence (default 15)
  --quiet               summary only
  --inject-nondeterminism CASE:STEP
                        perturb the replay pass of case CASE at step STEP.
                        A deliberate, driver-side fault used to prove the
                        mismatch -> reproducer -> fuzz_repro path works.
  --inject-abort CASE:STEP
                        abort the process in case CASE at replay step STEP.
                        Proves an assert/crash leaves an actionable in-flight
                        journal that fuzz_repro can regenerate.
  --merge FILE...       merge kv summaries and print the report; runs nothing

exit: 0 clean, 1 failures found, 2 bad usage
)");
}

bool parse_policies(const std::string& csv, std::vector<PolicyKind>& out) {
    size_t pos = 0;
    while (pos <= csv.size()) {
        const size_t comma = csv.find(',', pos);
        const std::string name =
            csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!name.empty()) {
            PolicyKind k{};
            if (!policy_from_name(name, k)) {
                std::fprintf(stderr, "unknown policy '%s'\n", name.c_str());
                return false;
            }
            out.push_back(k);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return !out.empty();
}

// The policy seed for (seed, policy, rep). A splitmix64 finalizer over the
// three, so reps are decorrelated and the value is reconstructible by hand from
// the case id printed in any report.
uint64_t policy_seed_for(int64_t seed, PolicyKind pol, uint32_t rep) {
    uint64_t z = static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ull;
    z ^= (static_cast<uint64_t>(pol) + 1) * 0xD1B54A32D192ED03ull;
    z ^= (static_cast<uint64_t>(rep) + 1) * 0xA24BAED4963EE407ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char last = dir[dir.size() - 1];
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    std::vector<std::string> merge_files;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--seed-start") o.seed_start = std::strtoll(next("--seed-start").c_str(), nullptr, 10);
        else if (a == "--seeds") o.seeds = std::strtoull(next("--seeds").c_str(), nullptr, 10);
        else if (a == "--reps") o.reps = static_cast<uint32_t>(std::strtoul(next("--reps").c_str(), nullptr, 10));
        else if (a == "--ascension") o.ascension = static_cast<uint8_t>(std::strtoul(next("--ascension").c_str(), nullptr, 10));
        else if (a == "--policies") { if (!parse_policies(next("--policies"), o.policies)) return 2; }
        else if (a == "--max-actions") o.limits.max_actions = static_cast<uint32_t>(std::strtoul(next("--max-actions").c_str(), nullptr, 10));
        else if (a == "--revisit-limit") o.limits.revisit_limit = static_cast<uint32_t>(std::strtoul(next("--revisit-limit").c_str(), nullptr, 10));
        else if (a == "--fail-on-livelock") o.limits.fail_on_livelock = true;
        else if (a == "--threads") o.threads = static_cast<unsigned>(std::strtoul(next("--threads").c_str(), nullptr, 10));
        else if (a == "--verify-repro-every") o.verify_repro_every = std::strtoull(next("--verify-repro-every").c_str(), nullptr, 10);
        else if (a == "--out") o.out_dir = next("--out");
        else if (a == "--label") o.label = next("--label");
        else if (a == "--max-failures") o.max_failures = std::strtoull(next("--max-failures").c_str(), nullptr, 10);
        else if (a == "--progress-secs") o.progress_secs = std::strtod(next("--progress-secs").c_str(), nullptr);
        else if (a == "--quiet") o.quiet = true;
        else if (a == "--shard") {
            const std::string v = next("--shard");
            const size_t slash = v.find('/');
            if (slash == std::string::npos) { std::fprintf(stderr, "--shard wants I/N\n"); return 2; }
            o.shard = std::strtoull(v.substr(0, slash).c_str(), nullptr, 10);
            o.shard_count = std::strtoull(v.substr(slash + 1).c_str(), nullptr, 10);
            if (o.shard_count == 0 || o.shard >= o.shard_count) { std::fprintf(stderr, "bad --shard\n"); return 2; }
        } else if (a == "--inject-nondeterminism") {
            const std::string v = next("--inject-nondeterminism");
            const size_t colon = v.find(':');
            if (colon == std::string::npos) { std::fprintf(stderr, "--inject-nondeterminism wants CASE:STEP\n"); return 2; }
            o.inject_case = std::strtoull(v.substr(0, colon).c_str(), nullptr, 10);
            o.inject_step = static_cast<uint32_t>(std::strtoul(v.substr(colon + 1).c_str(), nullptr, 10));
        } else if (a == "--inject-abort") {
            const std::string v = next("--inject-abort");
            const size_t colon = v.find(':');
            if (colon == std::string::npos) { std::fprintf(stderr, "--inject-abort wants CASE:STEP\n"); return 2; }
            o.inject_case = std::strtoull(v.substr(0, colon).c_str(), nullptr, 10);
            o.inject_step = static_cast<uint32_t>(std::strtoul(v.substr(colon + 1).c_str(), nullptr, 10));
            o.inject_abort = true;
        } else if (a == "--merge") {
            while (i + 1 < argc && argv[i + 1][0] != '-') merge_files.push_back(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", a.c_str());
            usage();
            return 2;
        }
    }

    if (!merge_files.empty()) {
        Coverage total;
        for (const std::string& f : merge_files) {
            std::ifstream is(f, std::ios::binary);
            if (!is) { std::fprintf(stderr, "cannot open %s\n", f.c_str()); return 2; }
            std::string text((std::istreambuf_iterator<char>(is)),
                             std::istreambuf_iterator<char>());
            Coverage c;
            if (!coverage_from_kv(text, c)) {
                std::fprintf(stderr, "malformed summary %s\n", f.c_str());
                return 2;
            }
            total.merge(c);
        }
        std::cout << total.report(0.0);
        return 0;
    }

    if (o.policies.empty()) {
        for (uint8_t i = 0; i < static_cast<uint8_t>(PolicyKind::COUNT); ++i) {
            o.policies.push_back(static_cast<PolicyKind>(i));
        }
    }
    if (o.threads == 0) o.threads = 1;

    const uint64_t total_cases =
        o.seeds * static_cast<uint64_t>(o.policies.size()) * o.reps;

    // --- shared state ---------------------------------------------------------
    std::vector<Coverage> per_thread(o.threads);
    std::mutex io_mu;
    std::atomic<uint64_t> failures{0};
    std::atomic<uint64_t> done{0};
    // Coverage stays thread-local and non-atomic for cheap per-case accounting.
    // Publish only the scalar the progress thread needs; reading per_thread
    // while workers mutate it would make the determinism checker itself racy.
    std::atomic<uint64_t> counted_actions{0};
    std::atomic<bool> stop{false};
    // Sharding skips cases, so `done` never reaches total_cases in a shard;
    // the progress loop watches finished WORKERS instead.
    std::atomic<unsigned> finished{0};
    const auto t0 = std::chrono::steady_clock::now();

    auto worker = [&](unsigned tid) {
        Coverage& cov = per_thread[tid];
        // The in-flight journal: written and flushed BEFORE each case, so an
        // abort leaves the case identity behind. Deleted at the end of a clean
        // shard.
        const std::string journal =
            o.out_dir.empty()
                ? std::string()
                : join_path(o.out_dir, o.label + "_inflight_s" + std::to_string(o.shard) +
                                           "_t" + std::to_string(tid) + ".txt");

        CaseResult res;
        for (uint64_t idx = tid; idx < total_cases; idx += o.threads) {
            if (stop.load(std::memory_order_relaxed)) break;
            if (o.shard_count > 1 && (idx % o.shard_count) != o.shard) continue;

            const uint64_t per_seed = o.policies.size() * o.reps;
            const uint64_t si = idx / per_seed;
            const uint64_t rem = idx % per_seed;
            CaseId id;
            id.run_seed = o.seed_start + static_cast<int64_t>(si);
            id.ascension = o.ascension;
            id.policy = o.policies[rem / o.reps];
            id.policy_seed = policy_seed_for(id.run_seed, id.policy,
                                             static_cast<uint32_t>(rem % o.reps));

            if (!journal.empty()) {
                std::ofstream js(journal, std::ios::trunc);
                js << "# fuzz_soak in-flight case (delete on clean exit)\n";
                js << "case_index " << idx << "\n";
                js << case_id_line(id) << "\n";
                js << "repro: fuzz_repro --seed " << id.run_seed << " --ascension "
                   << static_cast<int>(id.ascension) << " --policy "
                   << policy_name(id.policy) << " --policy-seed " << id.policy_seed
                   << " --max-actions " << o.limits.max_actions << "\n";
                js.flush();
            }

            Inject inj;
            if (idx == o.inject_case) {
                inj.enabled = true;
                inj.abort_process = o.inject_abort;
                inj.at_step = o.inject_step;
            }
            const bool verify = o.verify_repro_every != 0 && (idx % o.verify_repro_every) == 0;
            const bool ok = run_case(id, o.limits, &cov, res, verify, inj);
            counted_actions.fetch_add(res.actions, std::memory_order_relaxed);
            done.fetch_add(1, std::memory_order_relaxed);

            if (!ok) {
                const uint64_t n = failures.fetch_add(1) + 1;
                std::lock_guard<std::mutex> lk(io_mu);
                std::cerr << "\n" << triage_text(id, res);
                if (!o.out_dir.empty()) {
                    ReproFile rf;
                    rf.id = id;
                    // The prefix up to and including the divergent step: the
                    // minimal input that replays the failure (diff harness
                    // §8's rule, applied at the run level).
                    const size_t upto =
                        res.failure.step + 1 <= res.trajectory.size()
                            ? static_cast<size_t>(res.failure.step) + 1
                            : res.trajectory.size();
                    rf.actions.assign(res.trajectory.begin(),
                                      res.trajectory.begin() + static_cast<long>(upto));
                    rf.fail_kind = fail_kind_name(res.failure.kind);
                    rf.fail_step = res.failure.step;
                    rf.hash_a = res.failure.hash_a;
                    rf.hash_b = res.failure.hash_b;
                    rf.final_hash = res.final_hash;
                    rf.has_hashes = true;
                    const std::string path = join_path(
                        o.out_dir,
                        o.label + "_" + fail_kind_name(res.failure.kind) + "_seed" +
                            std::to_string(id.run_seed) + "_" + policy_name(id.policy) +
                            "_p" + std::to_string(id.policy_seed) + ".repro");
                    if (write_fuzz_repro(path, rf)) {
                        std::cerr << "reproducer: " << path << "\n";
                        std::cerr << "replay it:  fuzz_repro " << path << "\n";
                    } else {
                        std::cerr << "!! could not write reproducer to " << path << "\n";
                    }
                }
                if (n >= o.max_failures) stop.store(true);
            }
        }
        if (!journal.empty()) std::remove(journal.c_str());
        finished.fetch_add(1, std::memory_order_release);
    };

    std::vector<std::thread> pool;
    pool.reserve(o.threads);
    for (unsigned t = 0; t < o.threads; ++t) pool.emplace_back(worker, t);

    if (!o.quiet && o.progress_secs > 0.0) {
        // Progress from the main thread while the pool runs.
        while (finished.load(std::memory_order_acquire) < o.threads) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(o.progress_secs * 1000)));
            const auto now = std::chrono::steady_clock::now();
            const double el = std::chrono::duration<double>(now - t0).count();
            const uint64_t acts = counted_actions.load(std::memory_order_relaxed);
            std::lock_guard<std::mutex> lk(io_mu);
            std::fprintf(stderr,
                         "[%6.0fs] cases %llu/%llu  actions %llu  (%.0f act/s)  failures %llu\n",
                         el, static_cast<unsigned long long>(done.load()),
                         static_cast<unsigned long long>(total_cases),
                         static_cast<unsigned long long>(acts),
                         el > 0 ? static_cast<double>(acts) / el : 0.0,
                         static_cast<unsigned long long>(failures.load()));
        }
    }
    for (std::thread& t : pool) t.join();

    Coverage total;
    for (const Coverage& c : per_thread) total.merge(c);
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::cout << total.report(elapsed);
    std::cout << "failures: " << failures.load() << "\n";
    std::cout << "command: ";
    for (int i = 0; i < argc; ++i) std::cout << argv[i] << (i + 1 < argc ? " " : "\n");

    if (!o.out_dir.empty()) {
        const std::string kvp = join_path(
            o.out_dir, o.label + "_summary_s" + std::to_string(o.shard) + ".kv");
        std::ofstream ks(kvp, std::ios::trunc);
        ks << total.kv();
        const std::string rp = join_path(
            o.out_dir, o.label + "_report_s" + std::to_string(o.shard) + ".txt");
        std::ofstream rs(rp, std::ios::trunc);
        rs << total.report(elapsed) << "failures: " << failures.load() << "\n";
        std::cout << "summary kv: " << kvp << "\nreport:     " << rp << "\n";
    }
    return failures.load() == 0 ? 0 : 1;
}
