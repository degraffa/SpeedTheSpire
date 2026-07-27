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
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "sts/diff/trace.hpp"
#include "sts/engine/action_queue.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/power_hooks.hpp"
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

std::string replace_oracle_array(std::string line, const std::string& key,
                                 const std::string& array_json) {
    const std::string anchor = "\"" + key + "\":[";
    const auto begin = line.find(anchor);
    EXPECT_NE(begin, std::string::npos) << "missing oracle array " << key;
    if (begin == std::string::npos) return line;
    const auto close = line.find(']', begin + anchor.size());
    EXPECT_NE(close, std::string::npos) << "unterminated oracle array " << key;
    if (close == std::string::npos) return line;
    line.replace(begin + key.size() + 3, close - (begin + key.size() + 3) + 1,
                 array_json);
    return line;
}

std::string remove_oracle_array_entry(std::string line, const std::string& key,
                                      const std::string& entry) {
    const std::string anchor = "\"" + key + "\":[";
    const auto begin = line.find(anchor);
    EXPECT_NE(begin, std::string::npos) << "missing oracle array " << key;
    if (begin == std::string::npos) return line;
    const auto close = line.find(']', begin + anchor.size());
    EXPECT_NE(close, std::string::npos) << "unterminated oracle array " << key;
    if (close == std::string::npos) return line;
    const std::string needle = "\"" + entry + "\"";
    const auto pos = line.find(needle, begin + anchor.size());
    EXPECT_NE(pos, std::string::npos) << "missing " << entry << " in " << key;
    if (pos == std::string::npos || pos >= close) return line;
    if (pos + needle.size() < close && line[pos + needle.size()] == ',') {
        line.erase(pos, needle.size() + 1);
    } else if (pos > begin + anchor.size() && line[pos - 1] == ',') {
        line.erase(pos - 1, needle.size() + 1);
    } else {
        line.erase(pos, needle.size());
    }
    return line;
}

// Bit-exact stream compare: JSON emits SIGNED longs; RngStream stores uint64,
// so a negative expected value must be reinterpreted, not sign-extended-wrong.
void expect_stream(const engine::RngStream& s, int32_t counter, int64_t s0, int64_t s1,
                   const char* name) {
    EXPECT_EQ(s.counter, counter) << name << ".counter";
    EXPECT_EQ(s.s0, static_cast<uint64_t>(s0)) << name << ".s0";
    EXPECT_EQ(s.s1, static_cast<uint64_t>(s1)) << name << ".s1";
}

tr::TranslatedRun translate_with_player_power(const std::string& power_json,
                                              const std::string& source) {
    std::vector<std::string> lines = read_lines(sample_path());
    if (lines.size() < 3u) {
        ADD_FAILURE() << "golden corpus lacks combat record";
        return {};
    }
    std::string combat = lines[2];
    const std::string anchor =
        "\"powers\":[{\"id\":\"Vulnerable\",\"name\":\"Vulnerable\",\"amount\":0}]";
    const auto pos = combat.find(anchor);
    if (pos == std::string::npos) {
        ADD_FAILURE() << "player-power anchor missing from golden combat record";
        return {};
    }
    combat.replace(pos, anchor.size(), "\"powers\":[" + power_json + "]");
    return tr::translate_lines({lines[0], combat}, source);
}

uint32_t combust_hp_loss(const engine::CombatState& s) {
    return (s.flags & engine::kCombatFlagCombustHpLossMask) >>
           engine::kCombatFlagCombustHpLossShift;
}

void execute_player_power_opcode(engine::CombatState& s, engine::Opcode opcode,
                                 engine::PowerId id, int32_t amount = 0) {
    engine::ActionQueueItem item{};
    item.opcode = static_cast<uint16_t>(opcode);
    item.src = engine::kActorPlayer;
    item.tgt = engine::kActorPlayer;
    item.amount = amount;
    item.flags = engine::make_apply_power_flags(id);
    engine::execute_opcode(s, item);
}

void drain_actions(engine::CombatState& s) {
    engine::ActionQueueItem item{};
    while (engine::pop_action_front(s, item)) {
        engine::execute_opcode(s, item);
    }
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

    // B4.3 un-deferred, now-representable oracle fields (schema v3). The golden's
    // oracle block carries real values (copied from STS00001) for all of these.
    // Event-pity floats: the game's float literals reproduce bit-for-bit.
    EXPECT_EQ(rl.run.event_pity_monster, 0.1f);
    EXPECT_EQ(rl.run.event_pity_shop, 0.03f);
    EXPECT_EQ(rl.run.event_pity_treasure, 0.02f);
    EXPECT_EQ(cb.run.event_pity_monster, 0.1f);
    // Shop purge cost (base 75, un-ramped).
    EXPECT_EQ(rl.run.purge_cost, 75);
    EXPECT_EQ(cb.run.purge_cost, 75);
    // Potion-slot count = length of the potions array (A20 -> A11 -> 2 slots).
    EXPECT_EQ(rl.run.potion_slots, 2);
    EXPECT_EQ(cb.run.potion_slots, 2);
    // neowRng: absent from these in-dungeon dumps (floor-0/Neow only), so it maps
    // nothing and stays a value-init (zero) stream -- storage present, unset here.
    EXPECT_EQ(rl.run.neow_rng.counter, 0);
    EXPECT_EQ(rl.run.neow_rng.s0, 0u);
    EXPECT_EQ(rl.run.neow_rng.s1, 0u);

    // B4.10 un-deferred the three remaining-event list bitsets. The A20
    // golden carries the complete 11-event / 6-shrine / 13-special lists
    // (NoteForYourself absent), so every canonical bit except NFY is set.
    for (const tr::TranslatedRecord* rec : {&rl, &cb}) {
        EXPECT_EQ(rec->run.event_membership, 0x07FFu);
        EXPECT_EQ(rec->run.shrine_membership, 0x3Fu);
        EXPECT_EQ(rec->run.special_membership,
                  0x3FFFu & ~(1u << engine::kNoteForYourselfBit));
    }

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

// The relicPools un-deferral (§2.5 #8). Storage has existed since schema v3; the
// blocker was a COMPLETE registry/relics.yaml, because join_relic is fail-loud --
// one unregistered game_id in any of the five arrays aborts the whole
// translation. The golden's oracle block carries all five real arrays (copied
// from a live capture), so this test only passes once every tier's rows exist.
//
// The mapping is BY NAME, not by the JSON object's key order (which the oracle
// emits as uncommon/shop/boss/common/rare). Asserting a specific relic in each
// tier's slot is what catches a positional read that would transpose the pools.
TEST(Translator, RelicPoolsLandInTheirTierSlots) {
    tr::TranslatedRun run = tr::translate_file(sample_path());
    ASSERT_EQ(run.records.size(), 2u);
    for (const tr::TranslatedRecord& rec : run.records) {
        const engine::RunState& rs = rec.run;
        // The Ironclad-obtainable pool sizes, per tier index (0=Common..4=Boss).
        EXPECT_EQ(rs.relic_pool_count[0], 33);
        EXPECT_EQ(rs.relic_pool_count[1], 30);
        EXPECT_EQ(rs.relic_pool_count[2], 28);
        EXPECT_EQ(rs.relic_pool_count[3], 17);
        EXPECT_EQ(rs.relic_pool_count[4], 22);
        // Front of each shuffled tier, verbatim from the capture.
        EXPECT_EQ(rs.relic_pools[0][0],
                  static_cast<uint16_t>(engine::RelicId::STRAWBERRY));
        EXPECT_EQ(rs.relic_pools[1][0],
                  static_cast<uint16_t>(engine::RelicId::TOXIC_EGG));
        EXPECT_EQ(rs.relic_pools[2][0],
                  static_cast<uint16_t>(engine::RelicId::MAGIC_FLOWER));
        EXPECT_EQ(rs.relic_pools[3][0],
                  static_cast<uint16_t>(engine::RelicId::ORRERY));
        EXPECT_EQ(rs.relic_pools[4][0],
                  static_cast<uint16_t>(engine::RelicId::EMPTY_CAGE));
        // End of the boss tier: the shop pop takes the END, so both ends matter.
        EXPECT_EQ(rs.relic_pools[4][21],
                  static_cast<uint16_t>(engine::RelicId::SNECKO_EYE));
        EXPECT_EQ(rs.relic_pools[3][16],
                  static_cast<uint16_t>(engine::RelicId::BRIMSTONE));
        // Nothing beyond each tier's count was written.
        for (int t = 0; t < engine::kRelicTierCount; ++t) {
            for (int i = rs.relic_pool_count[t]; i < engine::kRelicPoolCap; ++i) {
                EXPECT_EQ(rs.relic_pools[t][i], 0u) << "tier " << t << " slot " << i;
            }
        }
    }
}

// An unregistered relic id anywhere in relicPools must ABORT the translation, not
// silently drop the entry -- that fail-loud join is exactly why this key waited
// for a complete registry rather than landing tier by tier.
TEST(Translator, UnknownRelicInAPoolIsRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    std::string tampered = lines[1];
    const std::string anchor = "\"boss\":[";
    auto pos = tampered.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    tampered.insert(pos + anchor.size(), "\"Not A Relic\",");
    EXPECT_THROW(tr::translate_lines({lines[0], tampered}, "badpool"),
                 tr::TranslateError);
}

// B4.10: the three oracle remaining-list arrays map by generated EventId to
// the B4.3 membership bitsets. Runtime indices shift after removals, but bit
// meanings do not: each bit is the event's canonical init-list position.
TEST(Translator, EventMembershipListsMapAsCanonicalSubsequences) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    std::string changed = replace_oracle_array(
        lines[1], "eventList",
        R"(["Big Fish","Dead Adventurer","Shining Light"])");
    changed = replace_oracle_array(
        std::move(changed), "shrineList",
        R"(["Golden Shrine","Upgrade Shrine"])");
    changed = replace_oracle_array(
        std::move(changed), "specialOneTimeEventList",
        R"(["Bonfire Elementals","FaceTrader","SecretPortal","The Woman in Blue"])");

    tr::TranslatedRun run =
        tr::translate_lines({lines[0], changed}, "event-membership");
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.records[0].run.event_membership,
              (1u << 0) | (1u << 2) | (1u << 10));
    EXPECT_EQ(run.records[0].run.shrine_membership,
              (1u << 1) | (1u << 4));
    EXPECT_EQ(run.records[0].run.special_membership,
              (1u << 1) | (1u << 4) | (1u << 10) | (1u << 13));
}

TEST(Translator, RemovedEventMembershipDerivesCumulativeFiredFlags) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    std::string changed =
        remove_oracle_array_entry(lines[1], "eventList", "Big Fish");
    changed = remove_oracle_array_entry(
        std::move(changed), "shrineList", "Golden Shrine");
    changed = remove_oracle_array_entry(
        std::move(changed), "specialOneTimeEventList",
        "Bonfire Elementals");

    tr::TranslatedRun run =
        tr::translate_lines({lines[0], changed}, "event-fired-flags");
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.records[0].run.event_flags,
              (1u << (1u - 1u)) | (1u << (13u - 1u)) |
                  (1u << (19u - 1u)));
    // The A20 oracle list never initialized NoteForYourself. Its absence is
    // therefore not evidence that the event fired.
    EXPECT_EQ(run.records[0].run.event_flags & (1u << (27u - 1u)), 0u);
}

TEST(Translator, UnknownEventInMembershipListIsRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string changed = replace_oracle_array(
        lines[1], "eventList", R"(["Big Fish","Not A Real Event"])");
    try {
        (void)tr::translate_lines({lines[0], changed}, "bad-event-id");
        FAIL() << "expected TranslateError for an unknown event id";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("unknown event id"),
                  std::string::npos)
            << e.what();
        EXPECT_NE(std::string(e.what()).find("Not A Real Event"),
                  std::string::npos)
            << e.what();
    }
}

TEST(Translator, TolerantMembershipTalliesUnknownEventWithoutInventingABit) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string changed = replace_oracle_array(
        lines[1], "eventList", R"(["Big Fish","Not A Real Event"])");
    tr::TranslateOptions opts;
    opts.tolerate_unknown_ids = true;
    tr::TranslatedRun run =
        tr::translate_lines({lines[0], changed}, "unknown-event", opts);
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.records[0].run.event_membership, 1u);
    EXPECT_EQ(run.unknown_id_hits, 1u);
    ASSERT_EQ(run.unknown_ids.count("event:Not A Real Event"), 1u);
    EXPECT_EQ(run.unknown_ids.at("event:Not A Real Event"), 1u);
}

TEST(Translator, MembershipListRejectsKnownIdFromTheWrongPool) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string changed =
        replace_oracle_array(lines[1], "shrineList", R"(["Big Fish"])");
    try {
        (void)tr::translate_lines({lines[0], changed}, "wrong-event-pool");
        FAIL() << "expected TranslateError for a wrong-pool event id";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("does not belong"),
                  std::string::npos)
            << e.what();
        EXPECT_NE(std::string(e.what()).find("shrineList"),
                  std::string::npos)
            << e.what();
    }
}

TEST(Translator, MembershipListRejectsDuplicatesAndOrderDrift) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    for (const char* array :
         {R"(["Big Fish","Big Fish"])",
          R"(["The Cleric","Big Fish"])"}) {
        const std::string changed =
            replace_oracle_array(lines[1], "eventList", array);
        try {
            (void)tr::translate_lines({lines[0], changed},
                                      "bad-event-order");
            FAIL() << "expected TranslateError for " << array;
        } catch (const tr::TranslateError& e) {
            EXPECT_NE(std::string(e.what()).find(
                          "not a canonical-order subsequence"),
                      std::string::npos)
                << e.what();
        }
    }
}

// B4.3: when the oracle DOES carry neowRng (floor-0 / Neow dumps), it maps into
// RunState.neow_rng as the 14th stream (§2.5 #2). The golden's in-dungeon dumps
// omit it, so inject one into the run-level record's streams block and confirm
// it lands bit-for-bit (and that an in-dungeon dump WITHOUT it still translates).
TEST(Translator, NeowRngMapsWhenPresent) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    std::string tampered = lines[1];
    const std::string anchor = "\"streams\":{";
    auto pos = tampered.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    // signed longs, as the oracle emits them; distinct from every other stream.
    tampered.insert(pos + anchor.size(),
                    "\"neowRng\":{\"counter\":3,\"s0\":-77,\"s1\":88},");

    tr::TranslatedRun run = tr::translate_lines({lines[0], tampered}, "neow");
    ASSERT_EQ(run.records.size(), 1u);
    expect_stream(run.records[0].run.neow_rng, 3, -77LL, 88LL, "neowRng");
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

// --- B4.5 reward-screen slice: content-validated, still storage-less ---------
// The committed sample never reaches a reward screen, so these tests tamper the
// run-level record into one (the same technique as the refusal tests above).

std::string with_reward_screen(const std::string& line,
                               const std::string& screen_state_json) {
    const std::string anchor = "\"screen_type\":\"NONE\"";
    std::string out = line;
    const auto pos = out.find(anchor);
    EXPECT_NE(pos, std::string::npos);
    out.replace(pos, anchor.size(),
                "\"screen_type\":\"COMBAT_REWARD\",\"screen_state\":" +
                    screen_state_json);
    return out;
}

TEST(Translator, CombatRewardScreenStateValidatesAndJoins) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    const std::string tampered = with_reward_screen(
        lines[1],
        "{\"rewards\":["
        "{\"reward_type\":\"GOLD\",\"gold\":13},"
        "{\"reward_type\":\"POTION\",\"potion\":{\"id\":\"Block Potion\","
        "\"name\":\"Block Potion\",\"can_use\":false,\"can_discard\":true,"
        "\"requires_target\":false}},"
        "{\"reward_type\":\"RELIC\",\"relic\":{\"id\":\"Burning Blood\","
        "\"name\":\"Burning Blood\",\"counter\":-1}},"
        "{\"reward_type\":\"CARD\"}"
        "]}");
    // Known shape + known ids: translates cleanly (content is deliberately
    // storage-less -- the acceptance diffs the post-claim RunState).
    tr::TranslatedRun run = tr::translate_lines({lines[0], tampered}, "reward");
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.records[0].run.gold, 99);  // untouched by screen content
}

TEST(Translator, UnknownRewardTypeRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    const std::string tampered = with_reward_screen(
        lines[1], "{\"rewards\":[{\"reward_type\":\"BANANA\"}]}");
    try {
        (void)tr::translate_lines({lines[0], tampered}, "reward");
        FAIL() << "expected TranslateError for an unknown reward_type";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("unknown reward_type"),
                  std::string::npos)
            << e.what();
        EXPECT_NE(std::string(e.what()).find("BANANA"), std::string::npos)
            << e.what();
    }
}

TEST(Translator, UnknownRewardPotionIdRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    const std::string tampered = with_reward_screen(
        lines[1],
        "{\"rewards\":[{\"reward_type\":\"POTION\","
        "\"potion\":{\"id\":\"TotallyFakePotion\"}}]}");
    try {
        (void)tr::translate_lines({lines[0], tampered}, "reward");
        FAIL() << "expected TranslateError for an unknown reward potion id";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("unknown potion id"),
                  std::string::npos)
            << e.what();
        EXPECT_NE(std::string(e.what()).find("TotallyFakePotion"),
                  std::string::npos)
            << e.what();
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

// --- G4 id-tolerance accounting mode --------------------------------------
//
// G4's checklist item 1 runs the translator over a real A20 campaign that
// carries content ids the skeleton registry lacks (AscendersBane, Burning Blood,
// Cultist, ...) and requires "zero unknown-FIELD errors" while the unknown-ids
// are an EXPECTED, tallied set (not fatal). TranslateOptions::tolerate_unknown_ids
// is that mode: an unknown content id is tallied per-id and joined to NONE; the
// record's remaining fields are STILL field-checked; unknown FIELDS still fail.

TEST(Translator, TolerateUnknownIdsTalliesInsteadOfThrowing) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    // Rename a known deck card to an id the registry does not know (twice, to
    // prove per-id counting): Bash and one Strike_R -> unknown ids.
    std::string tampered = lines[1];
    {
        const std::string a = "\"id\":\"Bash\"";
        auto p = tampered.find(a);
        ASSERT_NE(p, std::string::npos);
        tampered.replace(p, a.size(), "\"id\":\"B3_9_UnknownCard\"");
    }

    tr::TranslateOptions opts;
    opts.tolerate_unknown_ids = true;
    // Strict mode (default) must still throw on the same input.
    EXPECT_THROW((void)tr::translate_lines({lines[0], tampered}, "strict"),
                 tr::TranslateError);

    // Tolerant mode: no throw, the unknown id is tallied, and the record still
    // translates (the other 6 known deck cards land, streams land bit-for-bit).
    tr::TranslatedRun run = tr::translate_lines({lines[0], tampered}, "tolerant", opts);
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.unknown_id_hits, 1u);
    ASSERT_EQ(run.unknown_ids.count("card:B3_9_UnknownCard"), 1u);
    EXPECT_EQ(run.unknown_ids.at("card:B3_9_UnknownCard"), 1u);
    // The tampered card joined to NONE; the rest of the record is intact.
    EXPECT_EQ(run.records[0].run.master_deck_count, 7);
    expect_stream(run.records[0].run.relic_rng, 5, -6368056192266778531LL,
                  -2945499761529171947LL, "relicRng");
}

TEST(Translator, TolerateUnknownIdsStillFailsOnUnknownField) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    // An unknown FIELD must remain fatal EVEN in id-tolerance mode: tolerance
    // loosens only the id join, never the fail-loud field discipline (§2.6).
    std::string tampered = lines[1];
    const std::string anchor = "\"game_state\":{";
    auto pos = tampered.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    tampered.insert(pos + anchor.size(), "\"bogus_field\":1,");

    tr::TranslateOptions opts;
    opts.tolerate_unknown_ids = true;
    try {
        (void)tr::translate_lines({lines[0], tampered}, "tolerant", opts);
        FAIL() << "expected TranslateError for an unknown field even under id tolerance";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("bogus_field"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("unknown field"), std::string::npos) << e.what();
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

// --- B3.7 fix-forward: CombustPower private hpLoss survives oracle import ---

TEST(Translator, CombustBaseAndStackedHpLossImportIntoReservedFlags) {
    struct Case {
        const char* power_json;
        int16_t amount;
        uint32_t hp_loss;
    };
    const Case cases[] = {
        {R"({"id":"Combust","name":"Combust","amount":5,"misc":1})", 5, 1u},
        {R"({"id":"Combust","name":"Combust","amount":12,"misc":2})", 12, 2u},
    };

    for (const Case& tc : cases) {
        SCOPED_TRACE(tc.power_json);
        tr::TranslatedRun run = translate_with_player_power(tc.power_json, "combust-import");
        ASSERT_EQ(run.records.size(), 1u);
        const engine::CombatState& s = run.records[0].combat;
        ASSERT_EQ(s.player_power_count, 1);
        EXPECT_EQ(s.player_powers[0].power_id,
                  static_cast<uint16_t>(engine::PowerId::COMBUST));
        EXPECT_EQ(s.player_powers[0].amount, tc.amount);
        EXPECT_EQ(combust_hp_loss(s), tc.hp_loss);
    }
}

TEST(Translator, ImportedStackedCombustReapplicationAndEndTurnUseImportedHpLoss) {
    tr::TranslatedRun run = translate_with_player_power(
        R"({"id":"Combust","name":"Combust","amount":12,"misc":2})",
        "combust-reapply");
    ASSERT_EQ(run.records.size(), 1u);
    engine::CombatState s = run.records[0].combat;

    execute_player_power_opcode(s, engine::Opcode::APPLY_POWER,
                                engine::PowerId::COMBUST, 7);
    ASSERT_EQ(s.player_power_count, 1);
    EXPECT_EQ(s.player_powers[0].amount, 19);
    EXPECT_EQ(combust_hp_loss(s), 3u);

    engine::dispatch_at_end_of_turn(s);
    ASSERT_EQ(s.action_count, 2);
    drain_actions(s);
    EXPECT_EQ(s.player_hp, 65);      // imported 2, then stackPower ++ -> 3
    EXPECT_EQ(s.monsters[0].hp, 21); // imported 12 + reapplied 7
}

TEST(Translator, ImportedCombustRemoveThenReapplyResetsHpLoss) {
    tr::TranslatedRun run = translate_with_player_power(
        R"({"id":"Combust","name":"Combust","amount":15,"misc":3})",
        "combust-reset");
    ASSERT_EQ(run.records.size(), 1u);
    engine::CombatState s = run.records[0].combat;

    execute_player_power_opcode(s, engine::Opcode::REMOVE_POWER,
                                engine::PowerId::COMBUST);
    EXPECT_EQ(s.player_power_count, 0);
    EXPECT_EQ(combust_hp_loss(s), 0u);

    execute_player_power_opcode(s, engine::Opcode::APPLY_POWER,
                                engine::PowerId::COMBUST, 5);
    ASSERT_EQ(s.player_power_count, 1);
    EXPECT_EQ(s.player_powers[0].amount, 5);
    EXPECT_EQ(combust_hp_loss(s), 1u);

    engine::dispatch_at_end_of_turn(s);
    drain_actions(s);
    EXPECT_EQ(s.player_hp, 67);
    EXPECT_EQ(s.monsters[0].hp, 35);
}

TEST(Translator, CombustHpLossImportFailsLoudlyOnMissingOrInvalidMisc) {
    struct Case {
        const char* power_json;
        const char* expected_error;
    };
    const Case cases[] = {
        {R"({"id":"Combust","name":"Combust","amount":5})", "missing required field"},
        {R"({"id":"Combust","name":"Combust","amount":5,"misc":0})", "must be in [1, 255]"},
        {R"({"id":"Combust","name":"Combust","amount":5,"misc":-1})", "must be in [1, 255]"},
        {R"({"id":"Combust","name":"Combust","amount":5,"misc":256})", "must be in [1, 255]"},
        {R"({"id":"Combust","name":"Combust","amount":5,"misc":"1"})", "expected integer"},
    };

    for (const Case& tc : cases) {
        SCOPED_TRACE(tc.power_json);
        try {
            (void)translate_with_player_power(tc.power_json, "combust-invalid");
            FAIL() << "expected TranslateError for invalid Combust misc/hpLoss";
        } catch (const tr::TranslateError& e) {
            EXPECT_NE(std::string(e.what()).find("combat_state.player.powers[0].misc"),
                      std::string::npos) << e.what();
            EXPECT_NE(std::string(e.what()).find(tc.expected_error),
                      std::string::npos) << e.what();
        }
    }
}

TEST(Translator, NonCombustPowerMiscRemainsDeferred) {
    tr::TranslatedRun baseline = translate_with_player_power(
        R"({"id":"Vulnerable","name":"Vulnerable","amount":0})",
        "non-combust-baseline");
    tr::TranslatedRun with_misc = translate_with_player_power(
        R"({"id":"Vulnerable","name":"Vulnerable","amount":0,"misc":9})",
        "non-combust-misc");
    ASSERT_EQ(with_misc.records.size(), 1u);
    EXPECT_EQ(combust_hp_loss(with_misc.records[0].combat), 0u);
    EXPECT_EQ(with_misc.stats.deferred, baseline.stats.deferred + 1u);
}
}  // namespace
