// B1.6 acceptance (Stage B design §3.3): the CommunicationModOracleAdapter
// answers query() / query_run() over a TRANSLATED campaign sample.
//
// This is the end-to-end tie: the committed golden sample
// tests/golden/oracle_corpus/skeleton_sample.jsonl is run through the real B1.5
// translator (translate_file), its per-record RunState/CombatState output is
// packed into a v2 trace container (write_trace_v2 -- the B1.6 mixed format),
// and the adapter is exercised over that file. The sample is exactly two
// records -- one out-of-combat (RUN) dump and one in-combat (COMBAT) dump -- so
// it genuinely exercises BOTH state kinds through the adapter.
//
// Links oracle_translator, which PUBLIC-links diff_harness; the test itself
// never touches nlohmann (the translator keeps it PRIVATE), consistent with the
// §2.6 tools-only grant.

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/diff/oracle.hpp"
#include "sts/diff/trace.hpp"
#include "sts/translate/translate.hpp"

namespace {

using namespace sts;
namespace tr = sts::translate;

std::string sample_path() {
    return std::string(STS_ORACLE_CORPUS_DIR) + "/skeleton_sample.jsonl";
}

// Pack a translated run into a v2 mixed container: one record per translated
// dump, RUN-kind out of combat / COMBAT-kind in combat, action bits 0 (the
// campaign driver leaves sim_action_bits null, so the translator emits 0).
std::vector<diff::TraceRecordV2> pack_v2(const tr::TranslatedRun& run) {
    std::vector<diff::TraceRecordV2> recs;
    recs.reserve(run.records.size());
    for (const tr::TranslatedRecord& rec : run.records) {
        diff::TraceRecordV2 r;
        r.kind = rec.in_combat ? diff::StateKind::COMBAT : diff::StateKind::RUN;
        r.run = rec.run;
        r.combat = rec.combat;
        r.action = 0;
        r.aux = 0;
        recs.push_back(r);
    }
    return recs;
}

TEST(TranslatedCampaignAdapter, ServesBothKindsForPrefixAndRejectsUnknownSeed) {
    tr::TranslatedRun run = tr::translate_file(sample_path());
    ASSERT_EQ(run.records.size(), 2u);

    const std::vector<diff::TraceRecordV2> recs = pack_v2(run);
    // The sample carries exactly one RUN dump and one COMBAT dump.
    int combat_index = -1;
    int run_index = -1;
    for (int i = 0; i < static_cast<int>(recs.size()); ++i) {
        if (recs[i].kind == diff::StateKind::COMBAT) combat_index = i;
        else run_index = i;
    }
    ASSERT_GE(combat_index, 0) << "sample must contain a COMBAT dump";
    ASSERT_GE(run_index, 0) << "sample must contain a RUN dump";

    const std::string path = std::string(STS_TRANSLATOR_SCRATCH) + "/adapter_translated.bin";
    ASSERT_TRUE(diff::write_trace_v2(path, run.seed, recs));

    diff::CommunicationModOracleAdapter oracle;
    ASSERT_TRUE(oracle.load_run_trace(path));
    EXPECT_EQ(oracle.run_count(), 1u);

    // A prefix of k zero-bit actions selects record[k] (the translated action
    // bits are all 0, so a default-constructed Action prefix matches).
    auto zero_prefix = [](int k) { return std::vector<engine::Action>(static_cast<std::size_t>(k)); };

    // COMBAT record served by query(), and byte-equal to the translated combat.
    {
        engine::CombatState out{};
        const std::vector<engine::Action> p = zero_prefix(combat_index);
        ASSERT_TRUE(oracle.query(run.seed, std::span<const engine::Action>(p), out));
        EXPECT_EQ(std::memcmp(&out, &recs[combat_index].combat, sizeof(engine::CombatState)), 0);
        // Wrong-kind: this index is not a RUN record.
        engine::RunState rout{};
        EXPECT_FALSE(oracle.query_run(run.seed, std::span<const engine::Action>(p), rout));
    }

    // RUN record served by query_run(), and byte-equal to the translated run.
    {
        engine::RunState out{};
        const std::vector<engine::Action> p = zero_prefix(run_index);
        ASSERT_TRUE(oracle.query_run(run.seed, std::span<const engine::Action>(p), out));
        EXPECT_EQ(std::memcmp(&out, &recs[run_index].run, sizeof(engine::RunState)), 0);
        // The RunState carries the translated run-scoped streams (verify a couple
        // survive the translate -> v2 -> adapter round-trip, non-zero state).
        EXPECT_NE(out.card_rng.s0, 0u);
        EXPECT_NE(out.map_rng.s0, 0u);
    }

    // Unknown seed -> no answer for either kind.
    {
        engine::CombatState cout{};
        engine::RunState rout{};
        EXPECT_FALSE(oracle.query(run.seed + 1, std::span<const engine::Action>{}, cout));
        EXPECT_FALSE(oracle.query_run(run.seed + 1, std::span<const engine::Action>{}, rout));
    }

    // Prefix past the end of the run -> no answer.
    {
        engine::RunState rout{};
        const std::vector<engine::Action> too_long = zero_prefix(static_cast<int>(recs.size()));
        EXPECT_FALSE(oracle.query_run(run.seed, std::span<const engine::Action>(too_long), rout));
    }
}

}  // namespace
