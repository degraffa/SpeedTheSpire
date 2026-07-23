// translate_cli — B1.5 translator front-end (+ G4 id-tolerance accounting).
//
// Usage:
//   translate_cli <run.jsonl> [<run2.jsonl> ...] [--trace-out <path>]
//                 [--tolerate-unknown-ids]
//
// Translates each campaign JSONL run through sts::translate::translate_file and
// prints per-run disposition stats. On a drift error it prints the loud message
// and moves to the next file (exit code reflects whether every file translated).
// With --trace-out, the FIRST run's combat snapshots are written as a v1 trace.
//
// --tolerate-unknown-ids switches the id join to ACCOUNTING mode (G4): an
// unknown content id (a card/power/monster/relic/potion the skeleton registry
// lacks pre-B3) is tallied per-id and translated to NONE instead of aborting,
// while unknown FIELDS still hard-fail. This is exactly the mode G4's checklist
// item 1 ("zero unknown-FIELD errors; the id tally is expected A20 content")
// needs to run over the real 20-seed A20 campaign. A campaign-wide unknown-id
// tally + aggregate field-disposition totals are printed at the end.
//
// This is the tool used to run the translator against the real §7.3 campaign
// corpus and report how it fares (see the B1.5 / G4 Logs). It is NOT part of the
// WSL gtest CI; the acceptance test is translator_test.

#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <vector>

#include "sts/translate/translate.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> files;
    std::string trace_out;
    sts::translate::TranslateOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--trace-out" && i + 1 < argc) {
            trace_out = argv[++i];
        } else if (a == "--tolerate-unknown-ids") {
            opts.tolerate_unknown_ids = true;
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: translate_cli <run.jsonl> [...] [--trace-out <path>]"
                             " [--tolerate-unknown-ids]\n");
        return 2;
    }

    int failures = 0;
    bool wrote_trace = false;
    std::map<std::string, uint64_t> agg_unknown_ids;  // campaign-wide id tally
    uint64_t agg_unknown_hits = 0;
    for (const std::string& f : files) {
        try {
            sts::translate::TranslatedRun run = sts::translate::translate_file(f, opts);
            const auto& s = run.stats;
            std::printf("OK   %s: seed=%s(%lld) records=%zu combat=%d "
                        "mapped=%llu ignored=%llu oracle=%llu deferred=%llu "
                        "unknown_id_hits=%llu\n",
                        f.c_str(), run.seed_string.c_str(),
                        static_cast<long long>(run.seed), run.records.size(),
                        run.combat_record_count,
                        static_cast<unsigned long long>(s.mapped),
                        static_cast<unsigned long long>(s.ignored),
                        static_cast<unsigned long long>(s.oracle),
                        static_cast<unsigned long long>(s.deferred),
                        static_cast<unsigned long long>(run.unknown_id_hits));
            for (const auto& [id, n] : run.unknown_ids) agg_unknown_ids[id] += n;
            agg_unknown_hits += run.unknown_id_hits;
            if (!trace_out.empty() && !wrote_trace) {
                if (sts::translate::write_combat_trace(trace_out, run)) {
                    std::printf("     wrote combat trace -> %s\n", trace_out.c_str());
                    wrote_trace = true;
                }
            }
        } catch (const sts::translate::TranslateError& e) {
            std::printf("DRIFT %s: %s\n", f.c_str(), e.what());
            ++failures;
        } catch (const std::exception& e) {
            std::printf("ERR  %s: %s\n", f.c_str(), e.what());
            ++failures;
        }
    }
    if (opts.tolerate_unknown_ids) {
        std::printf("--- unknown-id tally (%llu distinct, %llu hits) ---\n",
                    static_cast<unsigned long long>(agg_unknown_ids.size()),
                    static_cast<unsigned long long>(agg_unknown_hits));
        for (const auto& [id, n] : agg_unknown_ids)
            std::printf("  %-40s %llu\n", id.c_str(), static_cast<unsigned long long>(n));
    }
    std::printf("--- %zu file(s), %d drift/error ---\n", files.size(), failures);
    return failures == 0 ? 0 : 1;
}
