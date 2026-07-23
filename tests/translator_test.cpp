// B1.5 translator acceptance (Stage B design §2.6; ledger docs/stage-b-tasks.md
// B1.5 block). Runs on the committed curated golden sample
// tests/golden/oracle_corpus/skeleton_sample.jsonl.
//
// The sample is hand-curated to skeleton scope (only registry-known content:
// Strike_R/Defend_R/Bash/Shrug It Off/Pommel Strike, JawWorm, Strength/Vulnerable),
// but its `oracle` block (the 14 RNG streams + pity) is copied VERBATIM from a
// real driver artifact (campaign b14_accept2, seed STS00001) so the bit-for-bit
// assertions run against genuine, sign-varied 64-bit stream state (e.g. cardRng.s0
// is negative). Provenance is recorded in the B1.5 Log.
//
// Acceptance covered:
//   1. every §2.5 oracle field WITH schema storage lands bit-for-bit (7 run
//      streams + mapRng -> RunState; 5 floor streams -> CombatState;
//      cardBlizzRandomizer / blizzardPotionMod -> RunState), signed longs
//      preserved;
//   2. an artifact with an unknown field is refused (and an unknown content id);
//   3. round-trip stability: translate twice -> identical structs and an
//      identical emitted v1 combat trace.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/diff/trace.hpp"
#include "sts/translate/translate.hpp"

namespace {

using namespace sts;
namespace tr = sts::translate;

constexpr int64_t kSeed = 1790050543751LL;

std::string sample_path() {
    return std::string(STS_ORACLE_CORPUS_DIR) + "/skeleton_sample.jsonl";
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(is)) << "cannot open " << path;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Bit-exact stream compare: JSON emits SIGNED longs; RngStream stores uint64,
// so a negative expected value must be reinterpreted, not sign-extended-wrong.
void expect_stream(const engine::RngStream& s, int32_t counter, int64_t s0, int64_t s1,
                   const char* name) {
    EXPECT_EQ(s.counter, counter) << name << ".counter";
    EXPECT_EQ(s.s0, static_cast<uint64_t>(s0)) << name << ".s0";
    EXPECT_EQ(s.s1, static_cast<uint64_t>(s1)) << name << ".s1";
}

// --- Acceptance 1: oracle fields bit-for-bit ------------------------------

TEST(Translator, OracleFieldsLandBitForBit) {
    tr::TranslatedRun run = tr::translate_file(sample_path());
    ASSERT_EQ(run.seed, kSeed);
    ASSERT_EQ(run.records.size(), 2u);

    const tr::TranslatedRecord& rl = run.records[0];  // run-level (Neow, floor 0)
    const tr::TranslatedRecord& cb = run.records[1];  // combat (floor 1)
    ASSERT_FALSE(rl.in_combat);
    ASSERT_TRUE(cb.in_combat);

    // run-scoped streams + mapRng -> RunState (values copied from STS00001).
    expect_stream(rl.run.monster_rng, 41, 3388898780908912053LL, -2195227397617715518LL, "monsterRng");
    expect_stream(rl.run.card_rng, 0, -6619158040114265405LL, 4177985537405174798LL, "cardRng");
    expect_stream(rl.run.relic_rng, 5, -6368056192266778531LL, -2945499761529171947LL, "relicRng");
    expect_stream(rl.run.map_rng, 94, 8756960311115476284LL, 8714461748465431467LL, "mapRng");
    // relicRng.counter == 5 at the first in-dungeon dump: the 5 init pool
    // shuffles (design §2.5 / B1.2 verified). A cross-check the translator carries
    // for free.
    EXPECT_EQ(rl.run.relic_rng.counter, 5);

    // pity fields with storage.
    EXPECT_EQ(rl.run.card_blizz_randomizer, 5);
    EXPECT_EQ(rl.run.blizzard_potion_mod, 0);

    // floor-scoped streams -> CombatState (from the combat record's oracle).
    expect_stream(cb.combat.monster_hp_rng, 1, -5471394293180523395LL, 630273432087629641LL, "monsterHpRng");
    expect_stream(cb.combat.shuffle_rng, 1, -5471394293180523395LL, 630273432087629641LL, "shuffleRng");
    expect_stream(cb.combat.card_random_rng, 0, -3325542346638085447LL, -5471394293180523395LL, "cardRandomRng");

    // run seed lands in RunState.run_seed for both records; anchors cross-checked.
    EXPECT_EQ(rl.run.run_seed, kSeed);
    EXPECT_EQ(cb.run.run_seed, kSeed);

    // combat schema fields translated (skeleton content).
    ASSERT_EQ(cb.combat.monster_count, 1);
    EXPECT_EQ(cb.combat.monsters[0].monster_id,
              static_cast<uint16_t>(engine::MonsterId::JAW_WORM));
    EXPECT_EQ(cb.combat.monsters[0].hp, 40);
    EXPECT_EQ(cb.combat.player_hp, 68);
    EXPECT_EQ(cb.combat.player_energy, 3);
    EXPECT_EQ(cb.combat.hand_count, 3);
    EXPECT_EQ(cb.combat.turn, 2);

    // master deck translated (7 skeleton cards).
    EXPECT_EQ(rl.run.master_deck_count, 7);
    EXPECT_EQ(rl.run.master_deck[0].card_id, static_cast<uint16_t>(engine::CardId::STRIKE));

    // all four dispositions were exercised (mapped/ignored/oracle/deferred).
    EXPECT_GT(run.stats.mapped, 0u);
    EXPECT_GT(run.stats.ignored, 0u);
    EXPECT_GT(run.stats.oracle, 0u);
    EXPECT_GT(run.stats.deferred, 0u);
}

// --- Acceptance 2: fail loudly --------------------------------------------

TEST(Translator, UnknownFieldRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    // Inject a field that is on no list into game_state of the run-level record.
    std::string tampered = lines[1];
    const std::string anchor = "\"game_state\":{";
    auto pos = tampered.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    tampered.insert(pos + anchor.size(), "\"bogus_field\":1,");

    try {
        (void)tr::translate_lines({lines[0], tampered}, "tamper");
        FAIL() << "expected TranslateError for an unknown field";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("bogus_field"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("unknown field"), std::string::npos) << e.what();
    }
}

TEST(Translator, UnknownContentIdRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    // Rename a known deck card to an id the registry does not know.
    std::string tampered = lines[1];
    const std::string anchor = "\"id\":\"Bash\"";
    auto pos = tampered.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    tampered.replace(pos, anchor.size(), "\"id\":\"TotallyFakeCard\"");

    try {
        (void)tr::translate_lines({lines[0], tampered}, "tamper");
        FAIL() << "expected TranslateError for an unknown content id";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("unknown card id"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("TotallyFakeCard"), std::string::npos) << e.what();
    }
}

TEST(Translator, UnknownStreamNameRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    std::string tampered = lines[1];
    // Add a 15th, unknown stream inside oracle.streams.
    const std::string anchor = "\"streams\":{";
    auto pos = tampered.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    tampered.insert(pos + anchor.size(), "\"ghostRng\":{\"counter\":0,\"s0\":0,\"s1\":0},");
    try {
        (void)tr::translate_lines({lines[0], tampered}, "tamper");
        FAIL() << "expected TranslateError for an unknown stream name";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("ghostRng"), std::string::npos) << e.what();
    }
}

TEST(Translator, AnchorMismatchRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    std::string tampered = lines[1];
    // Corrupt the oracle seed echo so it disagrees with the stock top-level seed.
    const std::string anchor = "\"oracle\":{\"cardBlizzRandomizer\"";
    // The oracle block starts with its own "seed"; find oracle then its seed.
    auto opos = tampered.find("\"oracle\":{");
    ASSERT_NE(opos, std::string::npos);
    auto spos = tampered.find("\"seed\":", opos);
    ASSERT_NE(spos, std::string::npos);
    tampered.replace(spos, std::string("\"seed\":1790050543751").size(),
                     "\"seed\":123456789");
    (void)anchor;
    try {
        (void)tr::translate_lines({lines[0], tampered}, "tamper");
        FAIL() << "expected TranslateError for an oracle-anchor mismatch";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("anchor mismatch"), std::string::npos) << e.what();
    }
}

// --- Acceptance 3: round-trip stability -----------------------------------

TEST(Translator, RoundTripDeterministic) {
    tr::TranslatedRun a = tr::translate_file(sample_path());
    tr::TranslatedRun b = tr::translate_file(sample_path());
    ASSERT_EQ(a.records.size(), b.records.size());
    for (std::size_t i = 0; i < a.records.size(); ++i) {
        EXPECT_EQ(a.records[i].in_combat, b.records[i].in_combat);
        EXPECT_EQ(0, std::memcmp(&a.records[i].run, &b.records[i].run, sizeof(engine::RunState)))
            << "RunState differs at record " << i;
        EXPECT_EQ(0, std::memcmp(&a.records[i].combat, &b.records[i].combat, sizeof(engine::CombatState)))
            << "CombatState differs at record " << i;
    }
    EXPECT_EQ(a.stats.mapped, b.stats.mapped);
    EXPECT_EQ(a.stats.deferred, b.stats.deferred);

    // Emitted v1 combat trace is byte-identical across runs.
    const std::string p1 = std::string(STS_TRANSLATOR_SCRATCH) + "/rt_a.trace";
    const std::string p2 = std::string(STS_TRANSLATOR_SCRATCH) + "/rt_b.trace";
    ASSERT_TRUE(tr::write_combat_trace(p1, a));
    ASSERT_TRUE(tr::write_combat_trace(p2, b));
    std::ifstream f1(p1, std::ios::binary), f2(p2, std::ios::binary);
    std::string b1((std::istreambuf_iterator<char>(f1)), std::istreambuf_iterator<char>());
    std::string b2((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(b1.empty());
    EXPECT_EQ(b1, b2);
}

// The emitted combat trace is a real v1 trace the diff harness reads back, and
// the floor streams survive the round-trip (ties the "trace files" deliverable
// to the oracle bit-for-bit requirement for the floor-scoped streams).
TEST(Translator, CombatTraceReadsBackWithFloorStreams) {
    tr::TranslatedRun run = tr::translate_file(sample_path());
    const std::string p = std::string(STS_TRANSLATOR_SCRATCH) + "/rb.trace";
    ASSERT_TRUE(tr::write_combat_trace(p, run));

    diff::TraceHeader h{};
    std::vector<diff::TraceRecord> recs;
    ASSERT_TRUE(diff::read_trace(p, h, recs));
    EXPECT_EQ(h.seed, kSeed);
    ASSERT_EQ(recs.size(), 1u);  // one combat record in the sample
    expect_stream(recs[0].state.monster_hp_rng, 1, -5471394293180523395LL, 630273432087629641LL, "monsterHpRng");
    expect_stream(recs[0].state.card_random_rng, 0, -3325542346638085447LL, -5471394293180523395LL, "cardRandomRng");
    EXPECT_EQ(recs[0].state.monsters[0].monster_id,
              static_cast<uint16_t>(engine::MonsterId::JAW_WORM));
}

// --- Regression (B1.3 / design §11 v0.1.2): DEBUG intent anchors on move_id ---
//
// A stripped capture can carry intent=="DEBUG" and move_adjusted_damage==-1 on a
// LIVING monster whose semantic move_id is intact (18/20 B1.3 seeds; stock hits
// it too). Both are display-derived (PROTOCOL §3.12 disposition D); the translator
// must NOT treat the display enum as the move source — it anchors on move_id and
// must yield the SAME CombatState as the refreshed-intent dump. This is the
// regression G4's 20-seed translation run depends on.
TEST(Translator, DebugIntentAnchorsOnMoveId) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    const std::string& header = lines[0];
    const std::string& combat = lines[2];  // the floor-1 combat action record

    // Refreshed-intent baseline: the sample as-is (JawWorm intent ATTACK, move_id 1,
    // move_adjusted_damage 11) on a living monster (current_hp 40, is_gone false).
    ASSERT_NE(combat.find("\"intent\":\"ATTACK\""), std::string::npos);
    ASSERT_NE(combat.find("\"move_id\":1"), std::string::npos);
    ASSERT_NE(combat.find("\"move_adjusted_damage\":11"), std::string::npos);

    // Display-derived DEBUG variant: same move_id, banner not yet refreshed.
    std::string debug = combat;
    {
        const std::string i_from = "\"intent\":\"ATTACK\"", i_to = "\"intent\":\"DEBUG\"";
        const std::string d_from = "\"move_adjusted_damage\":11", d_to = "\"move_adjusted_damage\":-1";
        debug.replace(debug.find(i_from), i_from.size(), i_to);
        debug.replace(debug.find(d_from), d_from.size(), d_to);
    }

    tr::TranslatedRun ref = tr::translate_lines({header, combat}, "ref");
    tr::TranslatedRun dbg = tr::translate_lines({header, debug}, "debug");

    // Both translate successfully to one living-monster combat record.
    ASSERT_EQ(ref.records.size(), 1u);
    ASSERT_EQ(dbg.records.size(), 1u);
    ASSERT_TRUE(ref.records[0].in_combat);
    ASSERT_TRUE(dbg.records[0].in_combat);
    ASSERT_EQ(ref.records[0].combat.monster_count, 1);
    ASSERT_EQ(dbg.records[0].combat.monster_count, 1);

    const engine::MonsterState& mr = ref.records[0].combat.monsters[0];
    const engine::MonsterState& md = dbg.records[0].combat.monsters[0];

    // The move/intent representation is derived from move_id (== 1), identically.
    EXPECT_EQ(mr.intent, 1);
    EXPECT_EQ(md.intent, mr.intent);
    EXPECT_EQ(md.monster_id, static_cast<uint16_t>(engine::MonsterId::JAW_WORM));
    EXPECT_EQ(md.hp, 40);  // living monster

    // The DEBUG display strings do not perturb the semantic MonsterState at all...
    EXPECT_EQ(0, std::memcmp(&mr, &md, sizeof(engine::MonsterState)))
        << "DEBUG-intent monster must translate identically to refreshed-intent";
    // ...and the whole CombatState is byte-identical.
    EXPECT_EQ(0, std::memcmp(&ref.records[0].combat, &dbg.records[0].combat,
                             sizeof(engine::CombatState)))
        << "display-derived intent/move_adjusted_damage must not affect CombatState";
}

}  // namespace
