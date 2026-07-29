// `seed_scan` -- the capture planner's front end.
//
// Scans (seed range) x (policies) x (policy seeds), writes one machine-readable
// row per combination, and -- given a filter -- writes the qualifying seeds as a
// capture-ready seed list. See sts/planner/seed_scan.hpp for why the qualifying
// rule counts hits across combinations instead of taking the first one.
//
// The output is TWO files on purpose. The results file is the evidence (every
// combination, hit or not, so a later question can be answered without
// rescanning); the seed list is the artifact a campaign consumes, and it holds
// nothing but seed strings and a `#` header recording the scan that produced
// it. A campaign driver reading it must never have to parse a verdict.

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "sts/engine/seed_string.hpp"
#include "sts/planner/seed_scan.hpp"
#include "sts/registry/game_ids.hpp"

namespace {

using sts::planner::Filter;
using sts::planner::Format;
using sts::planner::ScanCase;
using sts::planner::ScanLimits;
using sts::planner::ScanRow;
using sts::planner::ScanSummary;
using sts::planner::Seed;

[[noreturn]] void die(const std::string& msg) {
    std::fprintf(stderr, "seed_scan: %s\n", msg.c_str());
    std::exit(2);
}

void usage() {
    std::fputs(
        "seed_scan -- pre-scan run seeds so a capture campaign can be aimed.\n"
        "\n"
        "SEED SELECTION (exactly one required)\n"
        "  --seeds <BEGIN>-<END>     Inclusive range. Two accepted spellings:\n"
        "                              STS00100-STS05099  counter range: a shared\n"
        "                                non-digit prefix and equal-width digits,\n"
        "                                enumerated the way the capture campaigns\n"
        "                                name seeds (STS%05d).\n"
        "                              1000-2000          raw int64 seed values;\n"
        "                                the seed STRING is derived by encoding.\n"
        "                            `..` also separates (use it for negative ints).\n"
        "  --seed-file <path>        One seed string per line; '#' comments ignored.\n"
        "\n"
        "SCAN SHAPE\n"
        "  --policies <a,b,...>      random, greedy_damage, greedy_block,\n"
        "                            hoard_gold, always_event  (default: random)\n"
        "  --policy-seeds <n,n,...>  default: 0\n"
        "  --ascension <n>           default: 20\n"
        "  --max-actions <n>         per run; default 4000\n"
        "  --revisit-limit <n>       livelock cut-off; default 64\n"
        "\n"
        "OUTPUT\n"
        "  --out <path>              results file (default: stdout)\n"
        "  --format tsv|jsonl        default: tsv\n"
        "  --seed-list <path>        qualifying seeds, one per line, '#' header\n"
        "  --summary <path>          aggregate report (default: stderr)\n"
        "  --progress                per-1000-row progress to stderr\n"
        "\n"
        "FILTERS (a row hits when ALL of these hold)\n"
        "  --need-event <name>       repeatable; enum symbol (MATCH_AND_KEEP) or\n"
        "                            game id (\"Match and Keep!\"), case-insensitive\n"
        "  --need-treasure           a treasure room was entered\n"
        "  --need-boss               the boss room was entered\n"
        "  --min-floor <n>           max floor reached >= n\n"
        "  --need-relic-offered <game id>   repeatable; hits when ANY listed\n"
        "                            relic was OFFERED (a RELIC reward row or a\n"
        "                            merchant shelf slot). Relic clauses are\n"
        "                            any-of WITHIN the clause, and AND with\n"
        "                            every other clause. Names are the exact\n"
        "                            registry game ids (\"Bottled Flame\").\n"
        "  --need-relic-reward-offered <game id>  repeatable; like\n"
        "                            --need-relic-offered but REWARD ROWS only\n"
        "                            (claimable for free; a shelf offer must be\n"
        "                            bought, and the shop lists only rows the\n"
        "                            run can afford)\n"
        "  --need-relic-acquired <game id>  repeatable; ANY listed relic ended\n"
        "                            up owned (the policy claimed/bought it)\n"
        "  --need-shop-after-relic <game id> repeatable; a merchant floor was\n"
        "                            live while ANY listed relic was owned\n"
        "                            (The Courier's restock precondition)\n"
        "  --track-relic <game id>   repeatable; observe a relic (adds the\n"
        "                            relic_obs column) without filtering on it\n"
        "  --min-hit-count <k>       a SEED qualifies when >= k of its scanned\n"
        "                            combinations hit; default 1. Use >= 2: the\n"
        "                            capture runs a different policy, so a target\n"
        "                            found by one combination is not evidence.\n"
        "\n"
        "OTHER\n"
        "  --verify-determinism      scan every case twice and require the two\n"
        "                            rows to be byte-identical (exit 1 if not)\n"
        "  --list-events             print the EventId name table and exit\n"
        "  -h, --help\n",
        stderr);
}

// --- seed range parsing ------------------------------------------------------

bool all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// Split "<prefix><digits>". Returns false when there are no trailing digits.
bool split_counter(std::string_view s, std::string_view& prefix,
                   std::string_view& digits) {
    std::size_t i = s.size();
    while (i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9') --i;
    if (i == s.size()) return false;
    prefix = s.substr(0, i);
    digits = s.substr(i);
    return true;
}

Seed make_seed_from_text(std::string text) {
    Seed s;
    s.value = sts::engine::seed_from_string(text);
    s.text = std::move(text);
    return s;
}

std::vector<Seed> parse_seed_range(const std::string& spec) {
    std::string lo_s;
    std::string hi_s;
    const std::size_t dotdot = spec.find("..");
    if (dotdot != std::string::npos) {
        lo_s = spec.substr(0, dotdot);
        hi_s = spec.substr(dotdot + 2);
    } else {
        const std::size_t dash = spec.find('-', 1);
        if (dash == std::string::npos) {
            die("--seeds needs BEGIN-END (or BEGIN..END): got '" + spec + "'");
        }
        lo_s = spec.substr(0, dash);
        hi_s = spec.substr(dash + 1);
    }
    if (lo_s.empty() || hi_s.empty()) die("--seeds has an empty endpoint");

    std::vector<Seed> out;

    // Raw int64 seed values.
    if ((all_digits(lo_s) || (lo_s[0] == '-' && all_digits(lo_s.substr(1)))) &&
        (all_digits(hi_s) || (hi_s[0] == '-' && all_digits(hi_s.substr(1))))) {
        const long long lo = std::strtoll(lo_s.c_str(), nullptr, 10);
        const long long hi = std::strtoll(hi_s.c_str(), nullptr, 10);
        if (hi < lo) die("--seeds range is empty (END < BEGIN)");
        for (long long v = lo; v <= hi; ++v) {
            Seed s;
            s.value = static_cast<int64_t>(v);
            s.text = sts::engine::seed_to_string(s.value);
            out.push_back(std::move(s));
        }
        return out;
    }

    // Counter range: shared prefix, equal-width zero-padded digits.
    std::string_view lp;
    std::string_view ld;
    std::string_view hp;
    std::string_view hd;
    if (!split_counter(lo_s, lp, ld) || !split_counter(hi_s, hp, hd)) {
        die("--seeds endpoints must end in digits: '" + spec + "'");
    }
    if (lp != hp) {
        die("--seeds endpoints have different prefixes ('" + std::string(lp) +
            "' vs '" + std::string(hp) + "')");
    }
    if (ld.size() != hd.size()) {
        // A width change would silently rename every seed past the rollover
        // (STS09999 -> STS10000 is fine; STS9999-STS10000 is not a range this
        // tool can name unambiguously).
        die("--seeds endpoints have different digit widths; pad them equally");
    }
    const long long lo = std::strtoll(std::string(ld).c_str(), nullptr, 10);
    const long long hi = std::strtoll(std::string(hd).c_str(), nullptr, 10);
    if (hi < lo) die("--seeds range is empty (END < BEGIN)");
    const int width = static_cast<int>(ld.size());
    for (long long n = lo; n <= hi; ++n) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*s%0*lld", static_cast<int>(lp.size()),
                      lp.data(), width, n);
        out.push_back(make_seed_from_text(buf));
    }
    return out;
}

std::vector<Seed> parse_seed_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) die("cannot open --seed-file '" + path + "'");
    std::vector<Seed> out;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                                 line.back() == '\r')) {
            line.pop_back();
        }
        std::size_t b = 0;
        while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
        if (b >= line.size()) continue;
        out.push_back(make_seed_from_text(line.substr(b)));
    }
    return out;
}

std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    std::size_t b = 0;
    while (b <= s.size()) {
        const std::size_t e = s.find(',', b);
        const std::size_t end = (e == std::string::npos) ? s.size() : e;
        if (end > b) out.push_back(s.substr(b, end - b));
        if (e == std::string::npos) break;
        b = e + 1;
    }
    return out;
}

const char* need_value(int argc, char** argv, int& i) {
    if (i + 1 >= argc) die(std::string(argv[i]) + " needs a value");
    return argv[++i];
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<Seed> seeds;
    std::vector<sts::fuzz::PolicyKind> policies;
    std::vector<uint64_t> policy_seeds;
    uint8_t ascension = 20;
    ScanLimits limits;
    Filter filter;
    Format format = Format::TSV;
    std::string out_path;
    std::string seed_list_path;
    std::string summary_path;
    bool progress = false;
    bool verify_determinism = false;

    std::vector<sts::registry::RelicId> track_relics;
    auto parse_relic = [&](const char* flag, const char* name) {
        const sts::registry::RelicId id = sts::registry::relic_from_game_id(name);
        if (id == sts::registry::RelicId::NONE) {
            die(std::string(flag) + ": unknown relic game id '" + name +
                "' (names are the exact registry game ids, e.g. "
                "\"Bottled Flame\", \"The Courier\")");
        }
        bool tracked = false;
        for (sts::registry::RelicId t : track_relics) {
            if (t == id) tracked = true;
        }
        if (!tracked) track_relics.push_back(id);
        return id;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "--list-events") {
            for (const auto& e : sts::planner::event_names()) {
                std::printf("%2d  %-24s %s\n", static_cast<int>(e.id),
                            std::string(e.symbol).c_str(),
                            std::string(e.game_id).c_str());
            }
            return 0;
        } else if (a == "--seeds") {
            for (Seed& s : parse_seed_range(need_value(argc, argv, i))) {
                seeds.push_back(std::move(s));
            }
        } else if (a == "--seed-file") {
            for (Seed& s : parse_seed_file(need_value(argc, argv, i))) {
                seeds.push_back(std::move(s));
            }
        } else if (a == "--policies") {
            for (const std::string& name : split_commas(need_value(argc, argv, i))) {
                sts::fuzz::PolicyKind k{};
                if (!sts::fuzz::policy_from_name(name, k)) {
                    die("unknown policy '" + name + "'");
                }
                policies.push_back(k);
            }
        } else if (a == "--policy-seeds") {
            for (const std::string& v : split_commas(need_value(argc, argv, i))) {
                policy_seeds.push_back(std::strtoull(v.c_str(), nullptr, 10));
            }
        } else if (a == "--ascension") {
            ascension = static_cast<uint8_t>(std::strtoul(need_value(argc, argv, i),
                                                          nullptr, 10));
        } else if (a == "--max-actions") {
            limits.max_actions = static_cast<uint32_t>(
                std::strtoul(need_value(argc, argv, i), nullptr, 10));
        } else if (a == "--revisit-limit") {
            limits.revisit_limit = static_cast<uint32_t>(
                std::strtoul(need_value(argc, argv, i), nullptr, 10));
        } else if (a == "--out") {
            out_path = need_value(argc, argv, i);
        } else if (a == "--format") {
            if (!sts::planner::format_from_name(need_value(argc, argv, i), format)) {
                die("--format must be tsv or jsonl");
            }
        } else if (a == "--seed-list") {
            seed_list_path = need_value(argc, argv, i);
        } else if (a == "--summary") {
            summary_path = need_value(argc, argv, i);
        } else if (a == "--progress") {
            progress = true;
        } else if (a == "--verify-determinism") {
            verify_determinism = true;
        } else if (a == "--need-event") {
            const std::string name = need_value(argc, argv, i);
            sts::registry::EventId id{};
            if (!sts::planner::event_id_from_name(name, id)) {
                die("unknown event '" + name + "' (try --list-events)");
            }
            filter.need_events.push_back(id);
        } else if (a == "--need-relic-offered") {
            filter.need_relic_offered.push_back(
                parse_relic("--need-relic-offered", need_value(argc, argv, i)));
        } else if (a == "--need-relic-reward-offered") {
            filter.need_relic_reward_offered.push_back(parse_relic(
                "--need-relic-reward-offered", need_value(argc, argv, i)));
        } else if (a == "--need-relic-acquired") {
            filter.need_relic_acquired.push_back(
                parse_relic("--need-relic-acquired", need_value(argc, argv, i)));
        } else if (a == "--need-shop-after-relic") {
            filter.need_shop_after_relic.push_back(parse_relic(
                "--need-shop-after-relic", need_value(argc, argv, i)));
        } else if (a == "--track-relic") {
            (void)parse_relic("--track-relic", need_value(argc, argv, i));
        } else if (a == "--need-treasure") {
            filter.need_treasure = true;
        } else if (a == "--need-boss") {
            filter.need_boss = true;
        } else if (a == "--min-floor") {
            filter.min_floor = static_cast<uint32_t>(
                std::strtoul(need_value(argc, argv, i), nullptr, 10));
        } else if (a == "--min-hit-count") {
            filter.min_hit_count = static_cast<uint32_t>(
                std::strtoul(need_value(argc, argv, i), nullptr, 10));
        } else {
            usage();
            die("unrecognised argument '" + a + "'");
        }
    }

    if (seeds.empty()) {
        usage();
        die("no seeds: pass --seeds or --seed-file");
    }
    if (policies.empty()) policies.push_back(sts::fuzz::PolicyKind::RANDOM);
    if (policy_seeds.empty()) policy_seeds.push_back(0);
    if (filter.min_hit_count == 0) filter.min_hit_count = 1;
    if (seed_list_path.empty() && !filter.empty()) {
        std::fputs("seed_scan: note: a filter was given but no --seed-list; the "
                   "qualifying seeds will only be counted.\n",
                   stderr);
    }

    // --- scan ----------------------------------------------------------------

    std::ofstream out_file;
    std::ostream* out = &std::cout;
    if (!out_path.empty()) {
        out_file.open(out_path);
        if (!out_file) die("cannot write --out '" + out_path + "'");
        out = &out_file;
    }
    if (format == Format::TSV) *out << sts::planner::tsv_header() << '\n';

    ScanSummary summary;
    summary.seeds = seeds.size();
    // Insertion order is the scan order, so the seed list comes out in the same
    // order the seeds were given rather than sorted by hash.
    std::vector<std::string> qualifying;
    uint64_t rows_done = 0;
    uint64_t determinism_mismatches = 0;
    const uint64_t total_rows =
        static_cast<uint64_t>(seeds.size()) * policies.size() * policy_seeds.size();

    const auto t0 = std::chrono::steady_clock::now();

    // One seed's rows are held only long enough to apply the seed-level rule;
    // the results file is written streaming, so the scan's memory is O(combos
    // per seed), not O(rows).
    std::vector<ScanRow> seed_rows;
    seed_rows.reserve(policies.size() * policy_seeds.size());

    for (const Seed& seed : seeds) {
        seed_rows.clear();
        for (sts::fuzz::PolicyKind policy : policies) {
            for (uint64_t pseed : policy_seeds) {
                ScanCase c;
                c.seed = seed;
                c.ascension = ascension;
                c.policy = policy;
                c.policy_seed = pseed;

                const ScanRow row = sts::planner::scan_case(c, limits,
                                                            track_relics);
                const std::string text = sts::planner::row_to_text(row, format);
                if (verify_determinism) {
                    const ScanRow again =
                        sts::planner::scan_case(c, limits, track_relics);
                    if (sts::planner::row_to_text(again, format) != text) {
                        ++determinism_mismatches;
                        std::fprintf(stderr,
                                     "seed_scan: DETERMINISM MISMATCH\n  A: %s\n  B: %s\n",
                                     text.c_str(),
                                     sts::planner::row_to_text(again, format).c_str());
                    }
                }
                *out << text << '\n';
                summary.add(row);
                seed_rows.push_back(row);
                ++rows_done;
                if (progress && rows_done % 1000 == 0) {
                    const auto dt = std::chrono::duration<double>(
                                        std::chrono::steady_clock::now() - t0)
                                        .count();
                    std::fprintf(stderr, "  %" PRIu64 "/%" PRIu64 " rows  %.1fs  %.0f rows/s\n",
                                 rows_done, total_rows, dt,
                                 dt > 0 ? static_cast<double>(rows_done) / dt : 0.0);
                }
            }
        }
        if (sts::planner::seed_qualifies(seed_rows, filter)) {
            qualifying.push_back(seed.text);
        }
    }

    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
    out->flush();

    // --- seed list -----------------------------------------------------------

    std::string header;
    {
        header += "# seed_scan candidate list\n";
        header += "# seeds_scanned=" + std::to_string(seeds.size());
        header += " policies=";
        for (std::size_t i = 0; i < policies.size(); ++i) {
            if (i) header += ",";
            header += sts::fuzz::policy_name(policies[i]);
        }
        header += " policy_seeds=";
        for (std::size_t i = 0; i < policy_seeds.size(); ++i) {
            if (i) header += ",";
            header += std::to_string(policy_seeds[i]);
        }
        header += " ascension=" + std::to_string(static_cast<unsigned>(ascension));
        header += " max_actions=" + std::to_string(limits.max_actions) + "\n";
        header += "# filter:";
        for (sts::registry::EventId id : filter.need_events) {
            header += " event=\"" + std::string(sts::planner::event_game_id(id)) + "\"";
        }
        for (sts::registry::RelicId id : filter.need_relic_offered) {
            header += " relic_offered=\"" +
                      std::string(sts::registry::relic_game_id(id)) + "\"";
        }
        for (sts::registry::RelicId id : filter.need_relic_reward_offered) {
            header += " relic_reward_offered=\"" +
                      std::string(sts::registry::relic_game_id(id)) + "\"";
        }
        for (sts::registry::RelicId id : filter.need_relic_acquired) {
            header += " relic_acquired=\"" +
                      std::string(sts::registry::relic_game_id(id)) + "\"";
        }
        for (sts::registry::RelicId id : filter.need_shop_after_relic) {
            header += " shop_after_relic=\"" +
                      std::string(sts::registry::relic_game_id(id)) + "\"";
        }
        if (filter.need_treasure) header += " treasure";
        if (filter.need_boss) header += " boss";
        if (filter.min_floor) header += " min_floor=" + std::to_string(filter.min_floor);
        header += " min_hit_count=" + std::to_string(filter.min_hit_count) + "\n";
        header += "# qualifying=" + std::to_string(qualifying.size()) + " of " +
                  std::to_string(seeds.size()) + " seeds\n";
    }

    if (!seed_list_path.empty()) {
        std::ofstream sl(seed_list_path);
        if (!sl) die("cannot write --seed-list '" + seed_list_path + "'");
        sl << header;
        for (const std::string& s : qualifying) sl << s << '\n';
        sl.flush();
    }

    // --- summary -------------------------------------------------------------

    std::string report = summary.text();
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "wall_clock=%.2fs  rows/s=%.0f  actions/s=%.0f\n", wall,
                      wall > 0 ? static_cast<double>(summary.rows) / wall : 0.0,
                      wall > 0 ? static_cast<double>(summary.actions) / wall : 0.0);
        report += buf;
    }
    report += header;
    if (verify_determinism) {
        report += "determinism_mismatches=" + std::to_string(determinism_mismatches) +
                  "\n";
    }

    if (summary_path.empty()) {
        std::fputs(report.c_str(), stderr);
    } else {
        std::ofstream sf(summary_path);
        if (!sf) die("cannot write --summary '" + summary_path + "'");
        sf << report;
        std::fputs(report.c_str(), stderr);
    }

    if (verify_determinism && determinism_mismatches != 0) return 1;
    return 0;
}
