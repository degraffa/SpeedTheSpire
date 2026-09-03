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

#include "sts/diff/differ.hpp"  // both differ directions over the v8 storage
#include "sts/diff/trace.hpp"
#include "sts/engine/action_queue.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/power_hooks.hpp"
#include "sts/engine/run_deck.hpp"  // the master-deck bottle bits
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

// B5.2: encounterLists is live controller-oracle data, not a RunState field.
// The translator must accept it under the ORACLE disposition while still
// validating all three arrays and refusing nested shape drift.
TEST(Translator, EncounterListsAreValidatedOracleControllerState) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string field =
        R"("encounterLists":{"monster":["Jaw Worm"],"elite":["Gremlin Nob"],"boss":["The Guardian"]},)";
    const auto pos = lines[1].find("\"relicPools\":");
    ASSERT_NE(pos, std::string::npos);
    lines[1].insert(pos, field);
    EXPECT_NO_THROW(
        (void)tr::translate_lines({lines[0], lines[1]}, "encounter-lists"));

    std::string drifted = lines[1];
    const auto lists_end = drifted.find("},\"relicPools\":");
    ASSERT_NE(lists_end, std::string::npos);
    drifted.insert(lists_end, R"(,"unexpected":[])");
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], drifted},
                                  "encounter-lists-drift"),
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

// --- S2.13: the pool parsers are act-aware ------------------------------------
//
// Two orders are in play and only one of them is act-dependent. The membership
// BIT is always `id - first_id` (registry order), so `shrine_membership` stays
// byte-comparable across an act crossing for the differ and PublicView. The
// LIST ORDER a dump arrives in is the act's own: Exordium ends with Wheel of
// Change (Exordium.java:238-246) while TheCity and TheBeyond -- byte-identical
// to each other -- put it second (TheCity.java:210-218 ==
// TheBeyond.java:198-206). Before S2.13 the subsequence check compared bit
// indices, so an Act-2 dump would have been rejected as "not a canonical-order
// subsequence"; it now compares POSITIONS in the act's order table.

// Retarget an Act-1 capture at another act: `act` appears at the stock top
// level and again in the oracle anchors, and the two are cross-checked.
std::string retarget_act(std::string line, int act) {
    const std::string from = "\"act\":1";
    const std::string to = "\"act\":" + std::to_string(act);
    for (std::size_t pos = line.find(from); pos != std::string::npos;
         pos = line.find(from, pos + to.size())) {
        line.replace(pos, from.size(), to);
    }
    return line;
}

TEST(Translator, ActTwoPoolsParseWithTheCityWidthsAndOrder) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    std::string changed = retarget_act(lines[1], 2);
    // TheCity's eventList, minus Beggar (id 34) -- so exactly one Act-2 fire.
    changed = replace_oracle_array(
        std::move(changed), "eventList",
        R"(["Addict","Back to Basics","Colosseum","Cursed Tome","Drug Dealer",)"
        R"("Forgotten Altar","Ghosts","Masked Bandits","Nest","The Library",)"
        R"("The Mausoleum","Vampires"])");
    // TheCity's shrineList ORDER -- Wheel of Change SECOND -- minus Purifier.
    changed = replace_oracle_array(
        std::move(changed), "shrineList",
        R"(["Match and Keep!","Wheel of Change","Golden Shrine",)"
        R"("Transmorgrifier","Upgrade Shrine"])");

    tr::TranslatedRun run = tr::translate_lines({lines[0], changed}, "act-two");
    ASSERT_EQ(run.records.size(), 1u);
    const auto& rs = run.records[0].run;

    // eventList: 13 bits wide, bit i == id (32 + i); Beggar is bit 2.
    EXPECT_EQ(rs.event_membership,
              static_cast<uint16_t>(((1u << 13) - 1u) & ~(1u << 2)));
    // shrineList: bit i == id (12 + i) REGARDLESS of the arrival order, so
    // Purifier (id 15) is bit 3 -- not the position it occupied in the dump.
    EXPECT_EQ(rs.shrine_membership, static_cast<uint8_t>(0x3Fu & ~(1u << 3)));

    // The FIRED derivation routes Beggar (id 34) to the HI word -- the old
    // `<< (first_id - 1)` block shift would have been UB at first_id 32.
    EXPECT_EQ(rs.event_flags_hi, 1u << (34u - 32u)) << "Beggar (id 34)";
    // Shrine ids are act-independent, so Purifier stays a lo-word id.
    EXPECT_EQ(rs.event_flags & (1u << (15u - 1u)), 1u << (15u - 1u))
        << "Purifier (id 15)";
}

TEST(Translator, AnExordiumOrderedShrineListIsRefusedInActTwo) {
    // The negative that gives the order table its teeth: the SAME six keys in
    // Exordium's order are not a subsequence of TheCity's order, and silently
    // accepting them would put a captured state's draw index out of step with
    // the simulator's.
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    std::string changed = retarget_act(lines[1], 2);
    changed = replace_oracle_array(
        std::move(changed), "eventList",
        R"(["Addict","Back to Basics","Beggar","Colosseum","Cursed Tome",)"
        R"("Drug Dealer","Forgotten Altar","Ghosts","Masked Bandits","Nest",)"
        R"("The Library","The Mausoleum","Vampires"])");
    // shrineList is left in Exordium's order -- Wheel of Change LAST.
    EXPECT_THROW(tr::translate_lines({lines[0], changed}, "act-two-bad-order"),
                 tr::TranslateError);
}

TEST(Translator, AnActOneEventListIsRefusedInActTwo) {
    // The width half of the same guard: Exordium's ids sit outside TheCity's
    // block, so an act/list mismatch is a named refusal rather than a bitset
    // whose bits quietly mean the wrong events.
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string changed = retarget_act(lines[1], 2);
    EXPECT_THROW(
        tr::translate_lines({lines[0], changed}, "act-two-act-one-list"),
        tr::TranslateError);
}

TEST(Translator, PostVictoryEndingTailIsTranslatedAndCounted) {
    // S2.43 found this shape: an A20 double-boss VICTORY walks the "Spire
    // Heart" dialog before its terminal (s2v2_db47_b, STS128113/ps47, records
    // 667-671). S2.43 SKIPPED that tail because the id had no recognition and
    // would have aborted the run; S3.21 gave it one (a non-pool sentinel, like
    // Neow's), so the records are now TRANSLATED like any other and the count
    // survives as a tally the replay differ reports against.
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 4u);
    // The sample's action records carry no screen_state key at all
    // (compact separators); insert one at the head of game_state.
    std::string ending = lines[1];
    const std::string anchor = "\"game_state\":{";
    const auto pos = ending.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    ending.insert(pos + anchor.size(),
                  "\"screen_state\":{\"event_id\":\"Spire Heart\"},");
    const std::string st = "\"screen_type\":\"NONE\"";
    const auto tpos = ending.find(st);
    ASSERT_NE(tpos, std::string::npos);
    ending.replace(tpos, st.size(), "\"screen_type\":\"EVENT\"");
    std::string victory = lines[3];
    const auto opos = victory.find("\"outcome\":\"death\"");
    ASSERT_NE(opos, std::string::npos);
    victory.replace(opos, std::strlen("\"outcome\":\"death\""),
                    "\"outcome\":\"victory\"");
    const tr::TranslatedRun run = tr::translate_lines(
        {lines[0], lines[1], ending, ending, victory}, "victory-tail");
    EXPECT_EQ(run.records.size(), 3u);  // the ordinary record AND the tail
    EXPECT_EQ(run.post_victory_ending_records, 2);
    EXPECT_EQ(run.first_post_victory_ending_record, 1);
}

TEST(Translator, AnUnregisteredEventIdStillAborts) {
    // The recognition S3.21 added is for exactly two named non-pool ids (Neow
    // and Spire Heart) and is not a general amnesty: any other event id the
    // registry does not know is still schema drift, in a victory artifact as
    // much as anywhere else. This is the guard that keeps the tail's
    // recognition from becoming a hole a real Act-4 event could fall through.
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 4u);
    std::string ending = lines[1];
    const std::string anchor = "\"game_state\":{";
    const auto pos = ending.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    ending.insert(pos + anchor.size(),
                  "\"screen_state\":{\"event_id\":\"Not An Event\"},");
    const std::string st = "\"screen_type\":\"NONE\"";
    const auto tpos = ending.find(st);
    ASSERT_NE(tpos, std::string::npos);
    ending.replace(tpos, st.size(), "\"screen_type\":\"EVENT\"");
    std::string victory = lines[3];
    const auto opos = victory.find("\"outcome\":\"death\"");
    ASSERT_NE(opos, std::string::npos);
    victory.replace(opos, std::strlen("\"outcome\":\"death\""),
                    "\"outcome\":\"victory\"");
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], ending, victory},
                                  "unregistered-event-id"),
        tr::TranslateError);
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

std::string with_shop_screen(const std::string& line,
                             const std::string& screen_state_json) {
    const std::string anchor = "\"screen_type\":\"NONE\"";
    std::string out = line;
    const auto pos = out.find(anchor);
    EXPECT_NE(pos, std::string::npos);
    out.replace(pos, anchor.size(),
                "\"screen_type\":\"SHOP_SCREEN\",\"screen_state\":" +
                    screen_state_json);
    return out;
}

std::string with_event_screen(const std::string& line,
                              const std::string& screen_state_json) {
    const std::string anchor = "\"screen_type\":\"NONE\"";
    std::string out = line;
    const auto pos = out.find(anchor);
    EXPECT_NE(pos, std::string::npos);
    out.replace(pos, anchor.size(),
                "\"screen_type\":\"EVENT\",\"screen_state\":" +
                    screen_state_json);
    return out;
}

std::string with_boss_reward_screen(const std::string& line,
                                    const std::string& screen_state_json) {
    const std::string anchor = "\"screen_type\":\"NONE\"";
    std::string out = line;
    const auto pos = out.find(anchor);
    EXPECT_NE(pos, std::string::npos);
    out.replace(pos, anchor.size(),
                "\"screen_type\":\"BOSS_REWARD\",\"screen_state\":" +
                    screen_state_json);
    return out;
}

// --- BOSS_REWARD.screen_state.relics: S2.42 promoted I -> deferred; S2.47 ----
// --- landed the storage (RunState.boss_chest, schema v8) and the emit --------
//
// PROTOCOL.md 3.8 dispositioned this `I (S2 scope)` on the grounds that the run
// terminated at the act-1 boss combat reward, before the chest. Capture driver
// b1.7.0 plays through the chest, and an `I` field is never diffed -- so design
// 6's S2-G2 item 2 (a ZERO-DIFF boss-relic pick) was unachievable while the row
// said `I`. S2.42 fixed the classification; S2.47 discharged the deferral row's
// storage half. These tests pin the whole chain: the emit into the new storage,
// both differ directions over it, and the fail-loud registry join that must
// survive the emit.

TEST(Translator, BossRewardRelicsAreEmittedIntoBossChestStorage) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);

    // Control: a record with no BOSS_REWARD screen leaves the schema-v8 storage
    // value-init zero -- the "unattested" state the replay differ gates on.
    const tr::TranslatedRun baseline = tr::translate_lines(
        {lines[0], lines[1]}, "boss-reward-baseline");
    ASSERT_EQ(baseline.records.size(), 1u);
    {
        const engine::BossChestState zero{};
        EXPECT_EQ(std::memcmp(&baseline.records[0].run.boss_chest, &zero,
                              sizeof zero),
                  0)
            << "a non-BOSS_REWARD dump must attest nothing";
    }

    // The emit: three offers land in pop order with the reveal bits a live
    // BOSS_REWARD screen implies (the screen up == the chest was opened and no
    // pick has happened yet, BossRelicSelectScreen.java:353/:101-108).
    const tr::TranslatedRun populated = tr::translate_lines(
        {lines[0], with_boss_reward_screen(
                       lines[1],
                       "{\"relics\":[{\"id\":\"Astrolabe\","
                       "\"name\":\"Astrolabe\",\"counter\":-1},"
                       "{\"id\":\"Sozu\",\"name\":\"Sozu\",\"counter\":-1},"
                       "{\"id\":\"Runic Dome\",\"name\":\"Runic Dome\","
                       "\"counter\":-1}]}")},
        "boss-reward");
    ASSERT_EQ(populated.records.size(), 1u);
    const engine::BossChestState& chest = populated.records[0].run.boss_chest;
    EXPECT_EQ(chest.relics[0], static_cast<uint16_t>(engine::RelicId::ASTROLABE));
    EXPECT_EQ(chest.relics[1], static_cast<uint16_t>(engine::RelicId::SOZU));
    EXPECT_EQ(chest.relics[2],
              static_cast<uint16_t>(engine::RelicId::RUNIC_DOME));
    EXPECT_EQ(chest.screen,
              static_cast<uint8_t>(engine::BossChestScreen::RELIC_SELECT));
    EXPECT_EQ(chest.seen, 1);
    EXPECT_EQ(chest.chose_relic, 0);

    // The disposition: the key is MAPPED now, not deferred and not ignored.
    // The control is a BOSS_REWARD screen with NO `relics` key, so the only
    // delta between it and `populated` is the key itself (the `screen_state`
    // container's own deferral is present in both and absent from `baseline`,
    // which is why `baseline` is not the right control here).
    const tr::TranslatedRun no_key = tr::translate_lines(
        {lines[0], with_boss_reward_screen(lines[1], "{}")},
        "boss-reward-nokey");
    EXPECT_EQ(populated.stats.deferred, no_key.stats.deferred)
        << "the `relics` key no longer defers -- it has storage";
    EXPECT_EQ(populated.stats.ignored, no_key.stats.ignored + 3u)
        << "each relic object still ignores only its localized `name`";
    EXPECT_GT(populated.stats.mapped, no_key.stats.mapped);
}

TEST(Translator, BossRewardOffersRoundTripThroughTheDifferBothWays) {
    // The S2.47 acceptance, both directions. MATCH: a translated BOSS_REWARD
    // record diffed against a RunState carrying the same three offers is clean.
    // MISMATCH: one substituted offer REDs, and the report NAMES the field.
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const tr::TranslatedRun run = tr::translate_lines(
        {lines[0], with_boss_reward_screen(
                       lines[1],
                       "{\"relics\":[{\"id\":\"Astrolabe\","
                       "\"name\":\"Astrolabe\",\"counter\":-1},"
                       "{\"id\":\"Sozu\",\"name\":\"Sozu\",\"counter\":-1},"
                       "{\"id\":\"Runic Dome\",\"name\":\"Runic Dome\","
                       "\"counter\":-1}]}")},
        "boss-reward-diff");
    ASSERT_EQ(run.records.size(), 1u);
    const engine::RunState& expected = run.records[0].run;

    engine::RunState actual = expected;  // the sim agreeing with the capture
    EXPECT_TRUE(sts::diff::diff_run_states(expected, actual).empty())
        << "matching offers must produce no diff";

    actual.boss_chest.relics[1] =
        static_cast<uint16_t>(engine::RelicId::CURSED_KEY);
    const sts::diff::DiffReport rep =
        sts::diff::diff_run_states(expected, actual);
    ASSERT_FALSE(rep.empty()) << "a mismatched offer must RED";
    EXPECT_TRUE(rep.mentions("boss_chest.relics[1]")) << rep.to_string();
    EXPECT_NE(rep.to_string().find("Sozu"), std::string::npos)
        << "the repr should name the relic (mentions() searches field names, "
           "so the game-id is looked for in the rendered report): "
        << rep.to_string();
    EXPECT_EQ(rep.size(), 1u)
        << "exactly the substituted offer, nothing else: " << rep.to_string();
}

TEST(Translator, BossRewardRelicsStillJoinTheRegistryAndFailLoud) {
    // The half S2.42 pinned and the emit must not regress: an unknown boss
    // relic on this screen is schema drift, not a shrug. The tampered list
    // keeps the legal count of three so what trips is the JOIN, not the count
    // check below.
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string tampered = with_boss_reward_screen(
        lines[1],
        "{\"relics\":[{\"id\":\"Astrolabe\",\"name\":\"Astrolabe\","
        "\"counter\":-1},"
        "{\"id\":\"TotallyFakeBossRelic\","
        "\"name\":\"TotallyFakeBossRelic\",\"counter\":-1},"
        "{\"id\":\"Sozu\",\"name\":\"Sozu\",\"counter\":-1}]}");
    try {
        (void)tr::translate_lines({lines[0], tampered}, "boss-reward-bogus");
        FAIL() << "expected TranslateError for an unknown boss relic id";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("TotallyFakeBossRelic"),
                  std::string::npos)
            << e.what();
    }
}

TEST(Translator, BossRewardRelicsRejectAnyCountButThree) {
    // BossChest offers exactly three (BossChest.java:37) and the screen re-adds
    // from that same list on every open (BossRelicSelectScreen.open:342-373),
    // so a shorter or longer list is schema drift and must abort loudly rather
    // than half-fill the storage.
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    for (const char* screen :
         {"{\"relics\":[]}",
          "{\"relics\":[{\"id\":\"Astrolabe\",\"name\":\"Astrolabe\","
          "\"counter\":-1}]}"}) {
        try {
            (void)tr::translate_lines(
                {lines[0], with_boss_reward_screen(lines[1], screen)},
                "boss-reward-count");
            FAIL() << "expected TranslateError for a non-3 offer count";
        } catch (const tr::TranslateError& e) {
            EXPECT_NE(std::string(e.what()).find("exactly"), std::string::npos)
                << e.what();
        }
    }
}

TEST(Translator, EventScreenStateValidatesKnownIdsAndOptionTypes) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string tampered = with_event_screen(
        lines[1],
        "{\"event_id\":\"Big Fish\",\"event_name\":\"Big Fish\","
        "\"body_text\":\"Choose\",\"options\":["
        "{\"text\":\"Banana\",\"label\":\"Heal\",\"disabled\":false,"
        "\"choice_index\":0}]}");
    tr::TranslatedRun run =
        tr::translate_lines({lines[0], tampered}, "event-screen");
    ASSERT_EQ(run.records.size(), 1u);
}

TEST(Translator, EventScreenStateRejectsUnknownIdsAndBadOptionTypes) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string unknown = with_event_screen(
        lines[1],
        "{\"event_id\":\"Totally Fake Event\",\"options\":[]}");
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], unknown}, "event-screen"),
        tr::TranslateError);

    const std::string bad_bool = with_event_screen(
        lines[1],
        "{\"event_id\":\"Big Fish\",\"options\":[{\"disabled\":1,"
        "\"choice_index\":\"zero\"}]}");
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], bad_bool}, "event-screen"),
        tr::TranslateError);
}

// The Neow slice. NeowRoom reports as an EVENT screen carrying a hard-coded
// "Neow Event" id (GameStateConverter.getEventState :343-355) that is
// deliberately NOT an events.yaml row: Neow belongs to no act's event / shrine
// / special pool, and minting an EventId for it would put a non-pool entry into
// the three membership bitsets that pool ids index. The id is therefore
// recognised as a sentinel while the option list still gets the ordinary
// EVENT-screen treatment, and the slice stays storage-less like the reward
// slices above.
TEST(Translator, NeowScreenStateIsAcceptedWithoutAnEventRegistryRow) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string tampered = with_event_screen(
        lines[1],
        "{\"event_id\":\"Neow Event\",\"event_name\":\"Neow\","
        "\"body_text\":\"\",\"options\":["
        "{\"text\":\"Transform a card\",\"label\":\"Transform a card\","
        "\"disabled\":false,\"choice_index\":0},"
        "{\"text\":\"Random common relic\",\"label\":\"Random common relic\","
        "\"disabled\":false,\"choice_index\":1},"
        "{\"text\":\"250 gold\",\"label\":\"250 gold\",\"disabled\":false,"
        "\"choice_index\":2},"
        "{\"text\":\"Boss relic swap\",\"label\":\"Boss relic swap\","
        "\"disabled\":false,\"choice_index\":3}]}");
    tr::TranslatedRun run =
        tr::translate_lines({lines[0], tampered}, "neow-screen");
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.records[0].run.gold, 99);  // screen content stores nothing
}

// The sentinel is exact: a near-miss is still an unknown event id, so a
// CommunicationMod rename cannot slip through as "probably Neow".
TEST(Translator, NeowSentinelDoesNotWhitelistNeighbouringIds) {
    std::vector<std::string> lines = read_lines(sample_path());
    const std::string near_miss = with_event_screen(
        lines[1], "{\"event_id\":\"Neow\",\"options\":[]}");
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], near_miss}, "neow-screen"),
        tr::TranslateError);
}

// Under the sentinel the option list keeps every type check.
TEST(Translator, NeowScreenStillTypeChecksItsOptions) {
    std::vector<std::string> lines = read_lines(sample_path());
    const std::string bad = with_event_screen(
        lines[1],
        "{\"event_id\":\"Neow Event\",\"options\":[{\"disabled\":1,"
        "\"choice_index\":\"zero\"}]}");
    EXPECT_THROW((void)tr::translate_lines({lines[0], bad}, "neow-screen"),
                 tr::TranslateError);
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

// The SHOP_SCREEN slice: registry-joined, type-checked, storage-less. Same
// contract as the reward slice above -- a merchant is derived state the game
// rebuilds from (seed, merchantRng.counter), so nothing here lands in RunState.
TEST(Translator, ShopScreenStateValidatesAndJoins) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    const std::string shop =
        "{\"cards\":[{\"id\":\"Iron Wave\",\"name\":\"Iron Wave\","
        "\"type\":\"ATTACK\",\"rarity\":\"COMMON\",\"upgrades\":0,\"cost\":1,"
        "\"has_target\":true,\"exhausts\":false,\"ethereal\":false,"
        "\"uuid\":\"x\",\"price\":59}],"
        "\"relics\":[{\"id\":\"Blood Vial\",\"name\":\"Blood Vial\","
        "\"counter\":-1,\"price\":172}],"
        "\"potions\":[{\"id\":\"Strength Potion\",\"name\":\"Strength Potion\","
        "\"can_use\":false,\"can_discard\":true,\"requires_target\":false,"
        "\"price\":54}],"
        "\"purge_available\":true,\"purge_cost\":75}";
    tr::TranslatedRun run =
        tr::translate_lines({lines[0], with_shop_screen(lines[1], shop)}, "shop");
    ASSERT_EQ(run.records.size(), 1u);
    EXPECT_EQ(run.records[0].run.gold, 99);  // untouched by screen content
}

TEST(Translator, UnknownShopPotionIdRefused) {
    std::vector<std::string> lines = read_lines(sample_path());
    const std::string shop =
        "{\"potions\":[{\"id\":\"TotallyFakePotion\"}],"
        "\"purge_available\":true,\"purge_cost\":75}";
    try {
        (void)tr::translate_lines({lines[0], with_shop_screen(lines[1], shop)},
                                  "shop");
        FAIL() << "expected TranslateError for an unknown shop potion id";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("TotallyFakePotion"),
                  std::string::npos)
            << e.what();
    }
}

TEST(Translator, ShopPurgeCostAndPricesAreTypeChecked) {
    std::vector<std::string> lines = read_lines(sample_path());
    const std::string bad_cost =
        "{\"purge_available\":true,\"purge_cost\":\"75\"}";
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], with_shop_screen(lines[1], bad_cost)},
                                  "shop"),
        tr::TranslateError);
    const std::string bad_flag =
        "{\"purge_available\":1,\"purge_cost\":75}";
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], with_shop_screen(lines[1], bad_flag)},
                                  "shop"),
        tr::TranslateError);
    const std::string bad_price =
        "{\"relics\":[{\"id\":\"Blood Vial\",\"counter\":-1,\"price\":\"172\"}],"
        "\"purge_available\":true,\"purge_cost\":75}";
    EXPECT_THROW(
        (void)tr::translate_lines({lines[0], with_shop_screen(lines[1], bad_price)},
                                  "shop"),
        tr::TranslateError);
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

TEST(Translator, PlaytimeIsDispositionedOracleNotUnknown) {
    // s2-design section 5 trap 5: the redeployed fork records
    // CardCrawlGame.playtime in the oracle block so a violated SecretPortal
    // >= 800s pin is detectable. The field is capture-side evidence, never
    // RunState -- the disposition is `oracle`, and a capture carrying it must
    // translate cleanly (the FieldReader is fail-loud, so without the
    // disposition this insert would abort as schema drift). Absence stays
    // legal too: every other test in this file runs the pre-redeploy sample.
    std::vector<std::string> lines = read_lines(sample_path());
    std::string tampered = lines[1];
    const std::string anchor = "\"oracle\":{";
    auto pos = tampered.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    tampered.insert(pos + anchor.size(), "\"playtime\":812.53125,");
    EXPECT_NO_THROW((void)tr::translate_lines({lines[0], tampered}, "playtime"));
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

// --- act_boss joins through the ENCOUNTER registry (B1.5/B4.3 deferral) -----
//
// `AbstractDungeon.bossKey` is the same string the encounter registry keys on
// ("The Guardian" / "Hexaghost" / "Slime Boss" -- Exordium.initializeBoss,
// MonsterHelper.getEncounter), so the join is the registry's own lookup rather
// than a table of spellings, and an unknown key is schema drift like any other
// id join. The stored value is the EncounterId because that is the space the
// run layer speaks: its `boss_list[]` holds encounter game_ids.
TEST(Translator, ActBossJoinsThroughTheEncounterRegistry) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string anchor = "\"game_state\":{";
    auto with_boss = [&](const std::string& key) {
        std::string out = lines[1];
        const auto pos = out.find(anchor);
        EXPECT_NE(pos, std::string::npos);
        out.insert(pos + anchor.size(), "\"act_boss\":\"" + key + "\",");
        return out;
    };

    for (const auto& [key, id] : std::vector<std::pair<std::string, int>>{
             {"The Guardian", 18}, {"Hexaghost", 19}, {"Slime Boss", 20}}) {
        tr::TranslatedRun run = tr::translate_lines({lines[0], with_boss(key)}, key);
        ASSERT_EQ(run.records.size(), 1u) << key;
        // The sample is act 1, so the value lands in boss_ids[0].
        EXPECT_EQ(run.records[0].run.boss_ids[0], id) << key;
        for (std::size_t i = 1; i < engine::kBossIdCap; ++i)
            EXPECT_EQ(run.records[0].run.boss_ids[i], 0) << key << " slot " << i;
    }
}

// A key the registry does not carry -- or one that is a real encounter but not
// a BOSS row -- aborts rather than silently writing 0, which is the same
// fail-loud discipline every other id join here has.
TEST(Translator, AnActBossTheRegistryDoesNotCarryAborts) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 2u);
    const std::string anchor = "\"game_state\":{";
    auto with_boss = [&](const std::string& key) {
        std::string out = lines[1];
        const auto pos = out.find(anchor);
        EXPECT_NE(pos, std::string::npos);
        out.insert(pos + anchor.size(), "\"act_boss\":\"" + key + "\",");
        return out;
    };
    EXPECT_THROW((void)tr::translate_lines({lines[0], with_boss("Not A Boss")}, "bad"),
                 tr::TranslateError);
    // "Cultist" IS an encounter row -- a WEAK one. The pool check is what makes
    // the difference, and it is checked rather than assumed.
    EXPECT_THROW((void)tr::translate_lines({lines[0], with_boss("Cultist")}, "weak"),
                 tr::TranslateError);
}

// --- monster_move_history: the LAST three, most-recent-first ---------------
//
// The fork emits the full `AbstractMonster.moveHistory` (up to 14 entries in
// the campaign corpus, where stock's own JSON gives 2); the schema keeps three.
// `moveHistory` is APPENDED, so the newest move is the last element and the
// schema is most-recent-first -- reading the head would store a monster's
// opening moves as if they were its latest, a wrong-but-plausible value no
// differ can flag. The committed sample carries [1, 2, 1] for its Jaw Worm,
// which is deliberately not a palindrome-free case on its own, so the test also
// drives a longer history where head and tail cannot be confused.
TEST(Translator, MonsterMoveHistoryTakesTheNewestThreeNewestFirst) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);

    tr::TranslatedRun run = tr::translate_file(sample_path());
    ASSERT_GE(run.records.size(), 2u);
    const engine::CombatState& cs = run.records[1].combat;
    ASSERT_GE(cs.monster_count, 1);
    EXPECT_EQ(cs.monsters[0].move_history[0], 1);  // last element of [1,2,1]
    EXPECT_EQ(cs.monsters[0].move_history[1], 2);
    EXPECT_EQ(cs.monsters[0].move_history[2], 1);

    // A history longer than the schema's three: only the tail lands, and the
    // head (3, 4, 5) must not appear anywhere.
    std::string longer = lines[2];
    const std::string from = "\"move_history\": [1, 2, 1]";
    const std::string from_compact = "\"move_history\":[1,2,1]";
    const std::string to = "\"move_history\":[3,4,5,7,8,9]";
    if (longer.find(from) != std::string::npos) {
        longer.replace(longer.find(from), from.size(), to);
    } else {
        ASSERT_NE(longer.find(from_compact), std::string::npos) << longer.substr(0, 200);
        longer.replace(longer.find(from_compact), from_compact.size(), to);
    }
    tr::TranslatedRun run2 = tr::translate_lines({lines[0], longer}, "longer");
    ASSERT_EQ(run2.records.size(), 1u);
    const engine::CombatState& cs2 = run2.records[0].combat;
    ASSERT_GE(cs2.monster_count, 1);
    EXPECT_EQ(cs2.monsters[0].move_history[0], 9);
    EXPECT_EQ(cs2.monsters[0].move_history[1], 8);
    EXPECT_EQ(cs2.monsters[0].move_history[2], 7);
}

// The positional join is the fork's contract, and it is CHECKED: a list whose
// ids do not line up with the combat block's monsters would attribute one
// monster's history to another, silently.
TEST(Translator, AMisalignedMonsterMoveHistoryAborts) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    std::string bad = lines[2];
    const std::string from = "\"id\": \"JawWorm\", \"move_history\"";
    const std::string from_compact = "\"id\":\"JawWorm\",\"move_history\"";
    const std::string to = "\"id\":\"Cultist\",\"move_history\"";
    if (bad.find(from) != std::string::npos) {
        bad.replace(bad.find(from), from.size(), to);
    } else {
        ASSERT_NE(bad.find(from_compact), std::string::npos);
        bad.replace(bad.find(from_compact), from_compact.size(), to);
    }
    EXPECT_THROW((void)tr::translate_lines({lines[0], bad}, "misaligned"),
                 tr::TranslateError);
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

// --- B3.11 stage D: the two `counter`-carrying powers import their second
// --- number, and The Bomb's per-instance oracle id joins ---------------------
//
// GameStateConverter emits a power's `damage` by REFLECTION over the field name
// (convertCreaturePowersToJson, GameStateConverter.java:895-903), so it is
// present for exactly the powers that declare a private `damage` -- which is
// exactly the set that maps onto PowerSlot.counter.

TEST(Translator, PanacheImportsCountdownAndDamageCounter) {
    tr::TranslatedRun run = translate_with_player_power(
        R"({"id":"Panache","name":"Panache","amount":3,"damage":24})",
        "panache-import");
    ASSERT_EQ(run.records.size(), 1u);
    const engine::CombatState& s = run.records[0].combat;
    ASSERT_EQ(s.player_power_count, 1);
    EXPECT_EQ(s.player_powers[0].power_id,
              static_cast<uint16_t>(engine::PowerId::PANACHE));
    EXPECT_EQ(s.player_powers[0].amount, 3) << "the oracle-visible countdown";
    EXPECT_EQ(s.player_powers[0].counter, 24) << "the private damage";
}

// TheBombPower's ID is "TheBomb" + an ever-increasing static offset
// (TheBombPower.java:31-32), so the oracle reports a DIFFERENT id string per
// instance and the registry's exact game_id table cannot hold them.
TEST(Translator, TheBombPerInstanceOracleIdsAllJoinToOnePowerId) {
    for (const char* json : {
             R"({"id":"TheBomb0","name":"Bomb","amount":3,"damage":40})",
             R"({"id":"TheBomb1","name":"Bomb","amount":2,"damage":50})",
             R"({"id":"TheBomb17","name":"Bomb","amount":1,"damage":40})",
             R"({"id":"TheBomb","name":"Bomb","amount":3,"damage":40})",
         }) {
        SCOPED_TRACE(json);
        tr::TranslatedRun run = translate_with_player_power(json, "bomb-import");
        ASSERT_EQ(run.records.size(), 1u);
        const engine::CombatState& s = run.records[0].combat;
        ASSERT_EQ(s.player_power_count, 1);
        EXPECT_EQ(s.player_powers[0].power_id,
                  static_cast<uint16_t>(engine::PowerId::THE_BOMB));
    }
}

TEST(Translator, TheBombImportsFuseAndDamagePerInstance) {
    tr::TranslatedRun run = translate_with_player_power(
        R"({"id":"TheBomb2","name":"Bomb","amount":2,"damage":50})",
        "bomb-fields");
    ASSERT_EQ(run.records.size(), 1u);
    const engine::CombatState& s = run.records[0].combat;
    ASSERT_EQ(s.player_power_count, 1);
    EXPECT_EQ(s.player_powers[0].amount, 2) << "the fuse in turns";
    EXPECT_EQ(s.player_powers[0].counter, 50) << "this instance's damage";
}

// The suffix normalization is DELIBERATELY narrow: only digits, and only after
// the exact lookup has failed. A near-miss must still fail loud rather than be
// swallowed into The Bomb.
TEST(Translator, TheBombPrefixNormalizationDoesNotSwallowNearMisses) {
    for (const char* json : {
             R"({"id":"TheBombX","name":"x","amount":1})",
             R"({"id":"TheBomb1a","name":"x","amount":1})",
             R"({"id":"TheBombardier","name":"x","amount":1})",
         }) {
        SCOPED_TRACE(json);
        EXPECT_THROW(
            translate_with_player_power(json, "bomb-near-miss"),
            tr::TranslateError);
    }
}

// --- HAND_SELECT: the optional-selection screen, mapped rather than deferred --
//
// getHandSelectState (GameStateConverter.java:538-557) splits the cards in two:
// `hand` is what is left of p.hand and `selected` is the picks, in pick order.
// The sim keeps both as one array with the picks as the hand's trailing suffix,
// so the translation is a concatenation plus a count -- and every shape it
// cannot model refuses loudly instead of guessing.

// The combat record's own hand array, verbatim. Card objects contain no nested
// arrays, so the first ']' closes it.
std::string hand_array_of(const std::string& line) {
    const std::string key = "\"hand\":[";
    const auto open = line.find(key);
    EXPECT_NE(open, std::string::npos);
    const auto close = line.find(']', open);
    EXPECT_NE(close, std::string::npos);
    const auto start = open + key.size() - 1;
    return line.substr(start, close + 1 - start);
}

std::string with_hand_select_screen(const std::string& line,
                                    const std::string& selected_json,
                                    const std::string& max_cards,
                                    const std::string& can_pick_zero,
                                    const std::string& hand_override = "") {
    const std::string anchor = "\"screen_type\":\"NONE\"";
    std::string out = line;
    const auto pos = out.find(anchor);
    EXPECT_NE(pos, std::string::npos);
    const std::string hand =
        hand_override.empty() ? hand_array_of(line) : hand_override;
    out.replace(pos, anchor.size(),
                "\"screen_type\":\"HAND_SELECT\",\"screen_state\":{\"hand\":" +
                    hand + ",\"selected\":" + selected_json +
                    ",\"max_cards\":" + max_cards +
                    ",\"can_pick_zero\":" + can_pick_zero + "}");
    return out;
}

const char* kOnePick =
    R"([{"id":"Bash","name":"Bash","uuid":"0","cost":2,"upgrades":0,)"
    R"("type":"ATTACK","rarity":"BASIC","has_target":true,"exhausts":false,)"
    R"("ethereal":false,"is_playable":true}])";

TEST(Translator, HandSelectOptionalScreenConcatenatesPicksOntoTheHand) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    tr::TranslatedRun base =
        tr::translate_lines({lines[0], lines[2]}, "hand-select-base");
    ASSERT_EQ(base.records.size(), 1u);
    const uint8_t hand_before = base.records[0].combat.hand_count;
    ASSERT_GT(hand_before, 0);

    const std::string tampered =
        with_hand_select_screen(lines[2], kOnePick, "3", "true");
    tr::TranslatedRun run =
        tr::translate_lines({lines[0], tampered}, "hand-select");
    ASSERT_EQ(run.records.size(), 1u);
    const engine::CombatState& s = run.records[0].combat;

    // The pick is the LAST hand entry, and the open choice says how many are.
    EXPECT_EQ(s.hand_count, static_cast<uint8_t>(hand_before + 1));
    EXPECT_EQ(s.card_pool[s.hand[s.hand_count - 1]].card_id,
              static_cast<uint16_t>(engine::CardId::BASH));
    ASSERT_EQ(s.action_count, 1);
    const engine::ActionQueueItem& open = s.action_queue[s.action_head];
    EXPECT_EQ(open.opcode, static_cast<uint16_t>(engine::Opcode::CHOOSE_CARD));
    EXPECT_EQ(open.amount, 3);
    EXPECT_TRUE(engine::choose_is_optional(open.flags));
    EXPECT_EQ(engine::choose_selected_count(open.flags), 1);
}

TEST(Translator, HandSelectMandatoryScreenTranslatesOnlyWithNothingHeldAside) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);
    // can_pick_zero false + an empty selection is the ordinary mandatory block.
    const std::string clean =
        with_hand_select_screen(lines[2], "[]", "1", "false");
    tr::TranslatedRun run =
        tr::translate_lines({lines[0], clean}, "hand-select-mandatory");
    ASSERT_EQ(run.records.size(), 1u);
    const engine::CombatState& s = run.records[0].combat;
    ASSERT_EQ(s.action_count, 1);
    const engine::ActionQueueItem& open = s.action_queue[s.action_head];
    EXPECT_FALSE(engine::choose_is_optional(open.flags));
    EXPECT_EQ(engine::choose_selected_count(open.flags), 0);

    // A mandatory screen MID-selection has no sim counterpart: the sim applies
    // each mandatory pick immediately, so those cards are already in their
    // destination pile. Refuse rather than put them back in the hand.
    const std::string held =
        with_hand_select_screen(lines[2], kOnePick, "2", "false");
    try {
        (void)tr::translate_lines({lines[0], held}, "hand-select-held");
        FAIL() << "expected a refusal";
    } catch (const tr::TranslateError& e) {
        EXPECT_NE(std::string(e.what()).find("MANDATORY"), std::string::npos)
            << e.what();
    }
}

TEST(Translator, HandSelectRefusesShapesItCannotModel) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);

    // More picked than the screen allows.
    EXPECT_THROW((void)tr::translate_lines(
                     {lines[0],
                      with_hand_select_screen(lines[2], kOnePick, "0", "true")},
                     "hs-overpick"),
                 tr::TranslateError);

    // The screen's `hand` disagreeing with combat_state.hand is protocol drift:
    // both are p.hand.group in the same converter run.
    EXPECT_THROW((void)tr::translate_lines(
                     {lines[0],
                      with_hand_select_screen(lines[2], "[]", "1", "true", "[]")},
                     "hs-hand-drift"),
                 tr::TranslateError);

    // Wrong JSON types still fail loud, as everywhere else.
    EXPECT_THROW((void)tr::translate_lines(
                     {lines[0],
                      with_hand_select_screen(lines[2], "[]", "1", "\"yes\"")},
                     "hs-bad-bool"),
                 tr::TranslateError);

    // A missing key is drift, not a default.
    std::string missing = lines[2];
    const std::string anchor = "\"screen_type\":\"NONE\"";
    const auto pos = missing.find(anchor);
    ASSERT_NE(pos, std::string::npos);
    missing.replace(
        pos, anchor.size(),
        "\"screen_type\":\"HAND_SELECT\",\"screen_state\":{\"hand\":" +
            hand_array_of(lines[2]) + ",\"selected\":[],\"max_cards\":1}");
    EXPECT_THROW((void)tr::translate_lines({lines[0], missing}, "hs-missing"),
                 tr::TranslateError);
}

// --- The fork's Bottled trio booleans (PROTOCOL §3.13 fork addition) --------

TEST(Translator, BottleFlagsMapOnTheDeckWalkOnly) {
    std::vector<std::string> lines = read_lines(sample_path());
    ASSERT_GE(lines.size(), 3u);

    // Run-level record: bottle the first deck Strike. The key lands as the
    // master-deck bottle bit (engine run_deck.hpp). Absence -- every capture
    // made before the fork addition, this golden included -- means 0, which
    // the untouched neighbour row pins.
    {
        std::string runline = lines[1];
        const std::string anchor = "\"deck\":[{\"id\":\"Strike_R\",";
        const auto pos = runline.find(anchor);
        ASSERT_NE(pos, std::string::npos) << "deck anchor missing from golden";
        runline.insert(pos + anchor.size(), "\"in_bottle_flame\":true,");
        tr::TranslatedRun run =
            tr::translate_lines({lines[0], runline}, "bottle-deck");
        ASSERT_EQ(run.records.size(), 1u);
        EXPECT_EQ(run.records[0].run.master_deck[0].flags,
                  engine::kMasterCardInBottleFlame);
        EXPECT_EQ(run.records[0].run.master_deck[1].flags, 0);
    }

    // Combat record: the same key on a combat pile card is consumed and
    // DROPPED -- combat flags are registry-derived CardFlags (bit 0 there is
    // EXHAUST), so the bottle bit must NOT leak in.
    {
        std::string combat = lines[2];
        const std::string anchor = "\"draw_pile\":[{\"id\":\"Strike_R\",";
        const auto pos = combat.find(anchor);
        ASSERT_NE(pos, std::string::npos) << "draw anchor missing from golden";
        combat.insert(pos + anchor.size(), "\"in_bottle_flame\":true,");
        tr::TranslatedRun run =
            tr::translate_lines({lines[0], combat}, "bottle-combat");
        ASSERT_EQ(run.records.size(), 1u);
        ASSERT_TRUE(run.records[0].in_combat);
        for (int i = 0; i < engine::kCardPoolCap; ++i) {
            EXPECT_EQ(run.records[0].combat.card_pool[i].flags &
                          engine::kMasterCardBottleMask,
                      0u)
                << "combat card_pool[" << i << "]";
        }
    }
}

}  // namespace
