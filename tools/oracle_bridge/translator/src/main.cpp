// translate_cli — B1.5 translator front-end.
//
// Usage:
//   translate_cli <run.jsonl> [<run2.jsonl> ...] [--trace-out <path>]
//
// Translates each campaign JSONL run through sts::translate::translate_file and
// prints per-run disposition stats. On a drift error it prints the loud message
// and moves to the next file (exit code reflects whether every file translated).
// With --trace-out, the FIRST run's combat snapshots are written as a v1 trace.
//
// This is the tool used to run the translator against the real §7.3 campaign
// corpus and report how it fares (see the B1.5 Log). It is NOT part of the WSL
// gtest CI; the acceptance test is translator_test.

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "sts/translate/translate.hpp"

int main(int argc, char** argv) {
    std::vector<std::string> files;
    std::string trace_out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--trace-out" && i + 1 < argc) {
            trace_out = argv[++i];
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: translate_cli <run.jsonl> [...] [--trace-out <path>]\n");
        return 2;
    }

    int failures = 0;
    bool wrote_trace = false;
    for (const std::string& f : files) {
        try {
            sts::translate::TranslatedRun run = sts::translate::translate_file(f);
            const auto& s = run.stats;
            std::printf("OK   %s: seed=%s(%lld) records=%zu combat=%d "
                        "mapped=%llu ignored=%llu oracle=%llu deferred=%llu\n",
                        f.c_str(), run.seed_string.c_str(),
                        static_cast<long long>(run.seed), run.records.size(),
                        run.combat_record_count,
                        static_cast<unsigned long long>(s.mapped),
                        static_cast<unsigned long long>(s.ignored),
                        static_cast<unsigned long long>(s.oracle),
                        static_cast<unsigned long long>(s.deferred));
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
    std::printf("--- %zu file(s), %d drift/error ---\n", files.size(), failures);
    return failures == 0 ? 0 : 1;
}
