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
#include <bit>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <limits>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "sts/fuzz/coverage.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/repro.hpp"

namespace {

using namespace sts::fuzz;

#ifndef STS_FUZZ_BUILD_ID
#define STS_FUZZ_BUILD_ID "stsfuzz-b51fix2-cardgate1-schema5"
#endif

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
    bool inject_no_progress = false;
};

struct SummaryMeta {
    std::string build_id;
    int64_t seed_start = 0;
    uint64_t seed_count = 0;
    uint32_t reps = 0;
    uint32_t ascension = 0;
    uint32_t policy_mask = 0;
    uint32_t max_actions = 0;
    uint32_t revisit_limit = 0;
    uint64_t verify_repro_every = 0;
    uint32_t fail_on_livelock = 0;
    uint64_t shard = 0;
    uint64_t shard_count = 0;
    uint64_t global_cases = 0;
    uint64_t failures = 0;
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
  --inject-no-progress CASE:STEP
                        restore the pre-step state after one legal action.
  --merge FILE...       merge kv summaries and print the report; runs nothing

exit: 0 clean, 1 failures found, 2 bad usage
)");
}

bool parse_policies(const std::string& csv, std::vector<PolicyKind>& out) {
    uint32_t seen = 0;
    size_t pos = 0;
    while (pos <= csv.size()) {
        const size_t comma = csv.find(',', pos);
        const std::string name =
            csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (name.empty()) {
            std::fprintf(stderr, "--policies contains an empty component\n");
            return false;
        }
        PolicyKind k{};
        if (!policy_from_name(name, k)) {
            std::fprintf(stderr, "unknown policy '%s'\n", name.c_str());
            return false;
        }
        const uint32_t bit = 1u << static_cast<uint8_t>(k);
        if ((seen & bit) != 0) {
            std::fprintf(stderr, "duplicate policy '%s'\n", name.c_str());
            return false;
        }
        seen |= bit;
        out.push_back(k);
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

template <class T>
bool parse_integer(const std::string& text, T& out) {
    if (text.empty()) return false;
    T value{};
    const char* first = text.data();
    const char* last = first + text.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last) return false;
    out = value;
    return true;
}

bool parse_case_step(const std::string& text, uint64_t& case_index,
                     uint32_t& step) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos || text.find(':', colon + 1) != std::string::npos) {
        return false;
    }
    return parse_integer(text.substr(0, colon), case_index) &&
           parse_integer(text.substr(colon + 1), step);
}

uint32_t policy_mask(const std::vector<PolicyKind>& policies) {
    uint32_t mask = 0;
    for (PolicyKind p : policies) mask |= 1u << static_cast<uint8_t>(p);
    return mask;
}

SummaryMeta make_summary_meta(const Options& o, uint64_t global_cases,
                              uint64_t failures) {
    SummaryMeta m;
    m.build_id = STS_FUZZ_BUILD_ID;
    m.seed_start = o.seed_start;
    m.seed_count = o.seeds;
    m.reps = o.reps;
    m.ascension = o.ascension;
    m.policy_mask = policy_mask(o.policies);
    m.max_actions = o.limits.max_actions;
    m.revisit_limit = o.limits.revisit_limit;
    m.verify_repro_every = o.verify_repro_every;
    m.fail_on_livelock = o.limits.fail_on_livelock ? 1u : 0u;
    m.shard = o.shard;
    m.shard_count = o.shard_count;
    m.global_cases = global_cases;
    m.failures = failures;
    return m;
}

std::string summary_text(const SummaryMeta& m, const Coverage& c) {
    std::ostringstream os;
    os << "STSFUZZ_SUMMARY v1\n"
       << "build_id " << m.build_id << "\n"
       << "seed_start " << m.seed_start << "\n"
       << "seed_count " << m.seed_count << "\n"
       << "reps " << m.reps << "\n"
       << "ascension " << m.ascension << "\n"
       << "policy_mask " << m.policy_mask << "\n"
       << "max_actions " << m.max_actions << "\n"
       << "revisit_limit " << m.revisit_limit << "\n"
       << "verify_repro_every " << m.verify_repro_every << "\n"
       << "fail_on_livelock " << m.fail_on_livelock << "\n"
       << "shard " << m.shard << "\n"
       << "shard_count " << m.shard_count << "\n"
       << "global_cases " << m.global_cases << "\n"
       << "failures " << m.failures << "\n";
    os << c.kv();
    return os.str();
}

bool valid_seed_interval(int64_t first, uint64_t count);

bool parse_summary(const std::string& text, SummaryMeta& m, Coverage& c,
                   std::string& error) {
    std::istringstream is(text);
    std::string line;
    if (!std::getline(is, line) || line != "STSFUZZ_SUMMARY v1") {
        error = "missing STSFUZZ_SUMMARY v1 header";
        return false;
    }
    const std::vector<std::string> order = {
        "build_id", "seed_start", "seed_count", "reps", "ascension",
        "policy_mask", "max_actions", "revisit_limit", "verify_repro_every",
        "fail_on_livelock", "shard", "shard_count", "global_cases", "failures"};
    for (const std::string& expected : order) {
        if (!std::getline(is, line)) {
            error = "missing summary metadata key " + expected;
            return false;
        }
        std::istringstream ls(line);
        std::string key;
        std::string value;
        std::string extra;
        if (!(ls >> key >> value) || key != expected || (ls >> extra)) {
            error = "bad summary metadata line for " + expected;
            return false;
        }
        bool ok = true;
        if (key == "build_id") m.build_id = value;
        else if (key == "seed_start") ok = parse_integer(value, m.seed_start);
        else if (key == "seed_count") ok = parse_integer(value, m.seed_count);
        else if (key == "reps") ok = parse_integer(value, m.reps);
        else if (key == "ascension") ok = parse_integer(value, m.ascension);
        else if (key == "policy_mask") ok = parse_integer(value, m.policy_mask);
        else if (key == "max_actions") ok = parse_integer(value, m.max_actions);
        else if (key == "revisit_limit") ok = parse_integer(value, m.revisit_limit);
        else if (key == "verify_repro_every") {
            ok = parse_integer(value, m.verify_repro_every);
        } else if (key == "fail_on_livelock") {
            ok = parse_integer(value, m.fail_on_livelock);
        } else if (key == "shard") ok = parse_integer(value, m.shard);
        else if (key == "shard_count") ok = parse_integer(value, m.shard_count);
        else if (key == "global_cases") ok = parse_integer(value, m.global_cases);
        else if (key == "failures") ok = parse_integer(value, m.failures);
        if (!ok) {
            error = "bad value for summary metadata key " + key;
            return false;
        }
    }
    constexpr uint32_t kValidPolicyMask =
        (1u << static_cast<uint8_t>(PolicyKind::COUNT)) - 1u;
    if (m.build_id.empty() || m.seed_count == 0 || m.reps == 0 ||
        m.ascension > 20 || m.policy_mask == 0 ||
        (m.policy_mask & ~kValidPolicyMask) != 0 || m.max_actions == 0 ||
        m.revisit_limit == 0 || m.fail_on_livelock > 1 ||
        m.shard_count == 0 || m.shard >= m.shard_count ||
        m.global_cases == 0) {
        error = "summary metadata values out of range";
        return false;
    }
    const uint64_t policy_count =
        static_cast<uint64_t>(std::popcount(m.policy_mask));
    if (m.seed_count > UINT64_MAX / policy_count ||
        m.seed_count * policy_count > UINT64_MAX / m.reps ||
        m.global_cases != m.seed_count * policy_count * m.reps) {
        error = "summary global_cases does not match its sweep configuration";
        return false;
    }
    if (!valid_seed_interval(m.seed_start, m.seed_count)) {
        error = "summary seed interval overflows int64";
        return false;
    }
    std::string coverage((std::istreambuf_iterator<char>(is)),
                         std::istreambuf_iterator<char>());
    if (!coverage_from_kv(coverage, c)) {
        error = "malformed or incomplete coverage body";
        return false;
    }
    const uint64_t expected_cases =
        m.global_cases / m.shard_count +
        (m.shard < m.global_cases % m.shard_count ? 1u : 0u);
    if (c.cases != expected_cases) {
        error = "shard case count is incomplete or inconsistent";
        return false;
    }
    if (m.failures > c.cases) {
        error = "summary failure count exceeds shard case count";
        return false;
    }
    return true;
}

bool compatible_summary(const SummaryMeta& a, const SummaryMeta& b) {
    return a.build_id == b.build_id && a.seed_start == b.seed_start &&
           a.seed_count == b.seed_count && a.reps == b.reps &&
           a.ascension == b.ascension && a.policy_mask == b.policy_mask &&
           a.max_actions == b.max_actions &&
           a.revisit_limit == b.revisit_limit &&
           a.verify_repro_every == b.verify_repro_every &&
           a.fail_on_livelock == b.fail_on_livelock &&
           a.shard_count == b.shard_count &&
           a.global_cases == b.global_cases;
}

bool valid_seed_interval(int64_t first, uint64_t count) {
    if (count == 0) return false;
    const uint64_t max_offset =
        static_cast<uint64_t>(INT64_MAX) - static_cast<uint64_t>(first);
    return count - 1 <= max_offset;
}

int64_t seed_at_offset(int64_t first, uint64_t offset) {
    if (offset <= static_cast<uint64_t>(INT64_MAX)) {
        return first + static_cast<int64_t>(offset);
    }
    const int64_t partial = first + INT64_MAX;
    const uint64_t beyond =
        offset - static_cast<uint64_t>(INT64_MAX);
    return partial + static_cast<int64_t>(beyond - 1u) + 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    std::vector<std::string> merge_files;
    bool merge_mode = false;
    bool run_option_seen = false;
    bool policies_seen = false;
    bool injection_seen = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a != "--merge" && a != "--help" && a != "-h") {
            run_option_seen = true;
        }
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--seed-start") {
            if (!parse_integer(next("--seed-start"), o.seed_start)) return 2;
        }
        else if (a == "--seeds") {
            if (!parse_integer(next("--seeds"), o.seeds) || o.seeds == 0) return 2;
        }
        else if (a == "--reps") {
            if (!parse_integer(next("--reps"), o.reps) || o.reps == 0) return 2;
        }
        else if (a == "--ascension") {
            unsigned value = 0;
            if (!parse_integer(next("--ascension"), value) || value > 20) return 2;
            o.ascension = static_cast<uint8_t>(value);
        }
        else if (a == "--policies") {
            if (policies_seen) {
                std::fprintf(stderr, "--policies may appear only once\n");
                return 2;
            }
            policies_seen = true;
            if (!parse_policies(next("--policies"), o.policies)) return 2;
        }
        else if (a == "--max-actions") {
            if (!parse_integer(next("--max-actions"), o.limits.max_actions) ||
                o.limits.max_actions == 0) return 2;
        }
        else if (a == "--revisit-limit") {
            if (!parse_integer(next("--revisit-limit"), o.limits.revisit_limit) ||
                o.limits.revisit_limit == 0) return 2;
        }
        else if (a == "--fail-on-livelock") o.limits.fail_on_livelock = true;
        else if (a == "--threads") {
            if (!parse_integer(next("--threads"), o.threads) || o.threads == 0) return 2;
        }
        else if (a == "--verify-repro-every") {
            if (!parse_integer(next("--verify-repro-every"), o.verify_repro_every)) return 2;
        }
        else if (a == "--out") o.out_dir = next("--out");
        else if (a == "--label") o.label = next("--label");
        else if (a == "--max-failures") {
            if (!parse_integer(next("--max-failures"), o.max_failures) ||
                o.max_failures == 0) return 2;
        }
        else if (a == "--progress-secs") {
            const std::string value = next("--progress-secs");
            char* end = nullptr;
            errno = 0;
            o.progress_secs = std::strtod(value.c_str(), &end);
            if (errno != 0 || end != value.c_str() + value.size() ||
                !std::isfinite(o.progress_secs) || o.progress_secs < 0.0) {
                return 2;
            }
        }
        else if (a == "--quiet") o.quiet = true;
        else if (a == "--shard") {
            const std::string v = next("--shard");
            const size_t slash = v.find('/');
            if (slash == std::string::npos) { std::fprintf(stderr, "--shard wants I/N\n"); return 2; }
            if (!parse_integer(v.substr(0, slash), o.shard) ||
                !parse_integer(v.substr(slash + 1), o.shard_count)) return 2;
            if (o.shard_count == 0 || o.shard >= o.shard_count) { std::fprintf(stderr, "bad --shard\n"); return 2; }
        } else if (a == "--inject-nondeterminism") {
            if (injection_seen) {
                std::fprintf(stderr,
                             "diagnostic injection switches are mutually exclusive\n");
                return 2;
            }
            injection_seen = true;
            const std::string v = next("--inject-nondeterminism");
            if (!parse_case_step(v, o.inject_case, o.inject_step)) {
                std::fprintf(stderr, "--inject-nondeterminism wants CASE:STEP\n"); return 2;
            }
        } else if (a == "--inject-abort") {
            if (injection_seen) {
                std::fprintf(stderr,
                             "diagnostic injection switches are mutually exclusive\n");
                return 2;
            }
            injection_seen = true;
            const std::string v = next("--inject-abort");
            if (!parse_case_step(v, o.inject_case, o.inject_step)) {
                std::fprintf(stderr, "--inject-abort wants CASE:STEP\n"); return 2;
            }
            o.inject_abort = true;
        } else if (a == "--inject-no-progress") {
            if (injection_seen) {
                std::fprintf(stderr,
                             "diagnostic injection switches are mutually exclusive\n");
                return 2;
            }
            injection_seen = true;
            const std::string v = next("--inject-no-progress");
            if (!parse_case_step(v, o.inject_case, o.inject_step)) {
                std::fprintf(stderr, "--inject-no-progress wants CASE:STEP\n"); return 2;
            }
            o.inject_no_progress = true;
        } else if (a == "--merge") {
            if (merge_mode) {
                std::fprintf(stderr, "--merge may appear only once\n");
                return 2;
            }
            merge_mode = true;
            while (i + 1 < argc && argv[i + 1][0] != '-') merge_files.push_back(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown argument '%s'\n", a.c_str());
            usage();
            return 2;
        }
    }

    if (merge_mode) {
        if (run_option_seen) {
            std::fprintf(stderr, "--merge cannot be combined with sweep options\n");
            return 2;
        }
        if (merge_files.empty()) {
            std::fprintf(stderr, "--merge requires at least one summary\n");
            return 2;
        }
        Coverage total;
        SummaryMeta common;
        bool have_common = false;
        std::unordered_set<uint64_t> shards;
        uint64_t total_failures = 0;
        for (const std::string& f : merge_files) {
            std::ifstream is(f, std::ios::binary);
            if (!is) { std::fprintf(stderr, "cannot open %s\n", f.c_str()); return 2; }
            std::string text((std::istreambuf_iterator<char>(is)),
                             std::istreambuf_iterator<char>());
            Coverage c;
            SummaryMeta meta;
            std::string error;
            if (!parse_summary(text, meta, c, error)) {
                std::fprintf(stderr, "malformed summary %s: %s\n", f.c_str(),
                             error.c_str());
                return 2;
            }
            if (!have_common) {
                common = meta;
                have_common = true;
            } else if (!compatible_summary(common, meta)) {
                std::fprintf(stderr, "incompatible summary %s\n", f.c_str());
                return 2;
            }
            if (!shards.insert(meta.shard).second) {
                std::fprintf(stderr, "duplicate/overlapping shard %llu\n",
                             static_cast<unsigned long long>(meta.shard));
                return 2;
            }
            if (meta.failures > UINT64_MAX - total_failures) {
                std::fprintf(stderr, "failure total overflows uint64\n");
                return 2;
            }
            total_failures += meta.failures;
            if (!total.merge_checked(c)) {
                std::fprintf(stderr, "coverage total overflows uint64\n");
                return 2;
            }
        }
        if (shards.size() != common.shard_count) {
            std::fprintf(stderr, "incomplete shard set: got %zu of %llu\n",
                         shards.size(),
                         static_cast<unsigned long long>(common.shard_count));
            return 2;
        }
        for (uint64_t shard = 0; shard < common.shard_count; ++shard) {
            if (!shards.contains(shard)) {
                std::fprintf(stderr, "missing shard %llu\n",
                             static_cast<unsigned long long>(shard));
                return 2;
            }
        }
        std::cout << total.report(0.0);
        std::cout << "failures: " << total_failures << "\n"
                  << "build_id: " << common.build_id << "\n";
        std::cout.flush();
        if (!std::cout) {
            std::fprintf(stderr, "cannot write merged report\n");
            return 2;
        }
        return total_failures == 0 ? 0 : 1;
    }

    if (o.policies.empty()) {
        for (uint8_t i = 0; i < static_cast<uint8_t>(PolicyKind::COUNT); ++i) {
            o.policies.push_back(static_cast<PolicyKind>(i));
        }
    }
    if (std::popcount(policy_mask(o.policies)) !=
        static_cast<int>(o.policies.size())) {
        std::fprintf(stderr, "policy list contains duplicate entries\n");
        return 2;
    }
    if (!o.out_dir.empty() &&
        (!std::filesystem::exists(o.out_dir) ||
         !std::filesystem::is_directory(o.out_dir))) {
        std::fprintf(stderr, "--out is not an existing directory: %s\n",
                     o.out_dir.c_str());
        return 2;
    }
    if (o.seeds > std::numeric_limits<uint64_t>::max() /
                      static_cast<uint64_t>(o.policies.size()) ||
        o.seeds * static_cast<uint64_t>(o.policies.size()) >
            std::numeric_limits<uint64_t>::max() / o.reps) {
        std::fprintf(stderr, "case count overflows uint64\n");
        return 2;
    }
    if (!valid_seed_interval(o.seed_start, o.seeds)) {
        std::fprintf(stderr, "seed interval overflows int64\n");
        return 2;
    }
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
    std::atomic<bool> artifact_error{false};
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
        bool journal_written = false;

        CaseResult res;
        for (uint64_t idx = tid; idx < total_cases; idx += o.threads) {
            if (stop.load(std::memory_order_relaxed)) break;
            if (o.shard_count > 1 && (idx % o.shard_count) != o.shard) continue;

            const uint64_t per_seed = o.policies.size() * o.reps;
            const uint64_t si = idx / per_seed;
            const uint64_t rem = idx % per_seed;
            CaseId id;
            id.run_seed = seed_at_offset(o.seed_start, si);
            id.ascension = o.ascension;
            id.policy = o.policies[rem / o.reps];
            id.policy_seed = policy_seed_for(id.run_seed, id.policy,
                                             static_cast<uint32_t>(rem % o.reps));

            if (!journal.empty()) {
                std::ofstream js(journal, std::ios::trunc);
                if (!js) {
                    artifact_error.store(true);
                    stop.store(true);
                    break;
                }
                js << "# fuzz_soak in-flight case (delete on clean exit)\n";
                js << "case_index " << idx << "\n";
                js << case_id_line(id) << "\n";
                js << "repro: fuzz_repro --seed " << id.run_seed << " --ascension "
                   << static_cast<int>(id.ascension) << " --policy "
                   << policy_name(id.policy) << " --policy-seed " << id.policy_seed
                   << " --max-actions " << o.limits.max_actions << "\n";
                js.flush();
                if (!js) {
                    artifact_error.store(true);
                    stop.store(true);
                    break;
                }
                journal_written = true;
            }

            Inject inj;
            if (idx == o.inject_case) {
                inj.enabled = true;
                inj.abort_process = o.inject_abort;
                inj.no_progress = o.inject_no_progress;
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
                        artifact_error.store(true);
                        stop.store(true);
                    }
                }
                if (n >= o.max_failures) stop.store(true);
            }
        }
        if (journal_written && std::remove(journal.c_str()) != 0) {
            artifact_error.store(true);
        }
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
    if (artifact_error.load()) {
        std::fprintf(stderr, "artifact IO failure\n");
        return 2;
    }

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
        const SummaryMeta meta =
            make_summary_meta(o, total_cases, failures.load());
        std::ofstream ks(kvp, std::ios::binary | std::ios::trunc);
        ks << summary_text(meta, total);
        ks.flush();
        if (!ks) {
            std::fprintf(stderr, "cannot write summary %s\n", kvp.c_str());
            return 2;
        }
        const std::string rp = join_path(
            o.out_dir, o.label + "_report_s" + std::to_string(o.shard) + ".txt");
        std::ofstream rs(rp, std::ios::trunc);
        rs << total.report(elapsed) << "failures: " << failures.load() << "\n";
        rs.flush();
        if (!rs) {
            std::fprintf(stderr, "cannot write report %s\n", rp.c_str());
            return 2;
        }
        std::cout << "summary kv: " << kvp << "\nreport:     " << rp << "\n";
    }
    return failures.load() == 0 ? 0 : 1;
}
