// Tests for the capture planner (tools/oracle_bridge/planner).
//
// This tests the TOOL, not the engine. Four things it must not get wrong, each
// of which would silently produce a WRONG capture seed list rather than an
// error -- which is the whole hazard of a planner:
//
//   1. The EventId <-> name table. `--need-event "Match and Keep!"` resolving
//      to the wrong id yields a list of seeds for some other shrine, and every
//      downstream artifact looks fine.
//   2. The event_flags bit layout. event_framework.cpp:392 writes bit (id-1);
//      an off-by-one here reports the neighbouring event.
//   3. Determinism. A ScanRow must be a pure function of its ScanCase. If it
//      is not, the list is unreproducible and the whole premise -- that a scan
//      predicts what a capture will meet -- is void.
//   4. The hit-count rule. A seed qualifying on ONE combination is the
//      near-worthless case the rule exists to exclude.

#include "sts/planner/seed_scan.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/seed_string.hpp"
#include "sts/registry/event_table.hpp"
#include "sts/registry/game_ids.hpp"

namespace {

using sts::fuzz::PolicyKind;
using sts::planner::Filter;
using sts::planner::Format;
using sts::planner::RelicObs;
using sts::planner::ScanCase;
using sts::planner::ScanLimits;
using sts::planner::ScanRow;
using sts::registry::EventId;
using sts::registry::RelicId;

sts::planner::Seed MakeSeed(const std::string& text) {
    sts::planner::Seed s;
    s.text = text;
    s.value = sts::engine::seed_from_string(text);
    return s;
}

ScanCase MakeCase(const std::string& seed, PolicyKind policy, uint64_t pseed) {
    ScanCase c;
    c.seed = MakeSeed(seed);
    c.ascension = 20;
    c.policy = policy;
    c.policy_seed = pseed;
    return c;
}

// A scan cheap enough to run inside a unit test: the tests below only need
// runs that actually go somewhere, not runs that go all the way.
ScanLimits SmallLimits() {
    ScanLimits l;
    l.max_actions = 600;
    return l;
}

int CountTabs(const std::string& s) {
    int n = 0;
    for (char c : s) {
        if (c == '\t') ++n;
    }
    return n;
}

}  // namespace

// --- 1. the name table -------------------------------------------------------

TEST(SeedScanEventNames, CoversEveryGeneratedEventRow) {
    // The hand table must have exactly one row per generated event, and the
    // ids must be the canonical dense run of the five dungeon lists:
    // 11 Exordium event-list + 6 shrine-list + 14 special (the 1..31 layout of
    // event_framework.hpp:164-169) + S2.02's 13 TheCity event-list (32-44) and
    // 7 TheBeyond event-list (45-51) = 51.
    const auto& names = sts::planner::event_names();
    EXPECT_EQ(names.size(), sts::registry::kEventTable.size());
    ASSERT_EQ(names.size(), 51u);

    for (std::size_t i = 0; i < names.size(); ++i) {
        SCOPED_TRACE(std::string(names[i].symbol));
        // Ids are dense and ascending, which is what makes the bit layout a
        // shift rather than a lookup.
        EXPECT_EQ(static_cast<int>(names[i].id), static_cast<int>(i) + 1);
        EXPECT_FALSE(names[i].symbol.empty());
        EXPECT_FALSE(names[i].game_id.empty());
        // Every id the table claims must exist in the generated metadata.
        EXPECT_NE(sts::registry::event_def(names[i].id), nullptr);
    }

    // The five canonical list boundaries (event_framework.hpp:164-169 for the
    // first three; TheCity.java:186-198 / TheBeyond.java:180-186 for the rest).
    EXPECT_EQ(static_cast<int>(EventId::BIG_FISH), 1);
    EXPECT_EQ(static_cast<int>(EventId::MATCH_AND_KEEP), 12);
    EXPECT_EQ(static_cast<int>(EventId::ACCURSED_BLACKSMITH), 18);
    EXPECT_EQ(static_cast<int>(EventId::THE_WOMAN_IN_BLUE), 31);
    EXPECT_EQ(static_cast<int>(EventId::ADDICT), 32);
    EXPECT_EQ(static_cast<int>(EventId::VAMPIRES), 44);
    EXPECT_EQ(static_cast<int>(EventId::FALLING), 45);
    EXPECT_EQ(static_cast<int>(EventId::WINDING_HALLS), 51);
}

TEST(SeedScanEventNames, ResolvesBothSpellingsCaseInsensitively) {
    EventId id{};

    // The game id (registry/events.yaml `game_id`) -- what the capture
    // artifacts and the translator join key carry.
    ASSERT_TRUE(sts::planner::event_id_from_name("Match and Keep!", id));
    EXPECT_EQ(id, EventId::MATCH_AND_KEEP);
    // The enum symbol -- what someone reading engine code will type.
    ASSERT_TRUE(sts::planner::event_id_from_name("MATCH_AND_KEEP", id));
    EXPECT_EQ(id, EventId::MATCH_AND_KEEP);
    ASSERT_TRUE(sts::planner::event_id_from_name("match and keep!", id));
    EXPECT_EQ(id, EventId::MATCH_AND_KEEP);

    // A name that is a PREFIX of a real one must not match -- "Golden" is
    // shared by Golden Idol, Golden Wing and Golden Shrine, so a prefix match
    // here would silently pick one of three.
    EXPECT_FALSE(sts::planner::event_id_from_name("Golden", id));
    EXPECT_FALSE(sts::planner::event_id_from_name("", id));
    EXPECT_FALSE(sts::planner::event_id_from_name("Neow", id));

    EXPECT_EQ(sts::planner::event_game_id(EventId::MATCH_AND_KEEP), "Match and Keep!");
    EXPECT_EQ(sts::planner::event_game_id(EventId::NLOTH), "N'loth");
    EXPECT_EQ(sts::planner::event_game_id(EventId::NONE), "");
}

// --- 2. the flag layout ------------------------------------------------------

TEST(SeedScanEventFlags, BitIsIdMinusOne) {
    // src/engine/event_framework.cpp:392 -- `rs.event_flags |= 1u << (id - 1)`.
    // RunState::event_flags is a uint32_t, so only ids 1..31 have a bit; the
    // loop is bounded by the WORD, not by the enum, and `1u << 31` upward would
    // itself be UB in this test.
    const auto has_bit = [](EventId id) {
        return static_cast<uint32_t>(id) >= 1u && static_cast<uint32_t>(id) <= 31u;
    };
    for (const auto& e : sts::planner::event_names()) {
        if (!has_bit(e.id)) continue;
        SCOPED_TRACE(std::string(e.symbol));
        const uint32_t bit = 1u << (static_cast<uint32_t>(e.id) - 1u);
        EXPECT_TRUE(sts::planner::event_flag_set(bit, e.id));
        // ... and only that id.
        for (const auto& other : sts::planner::event_names()) {
            if (other.id == e.id) continue;
            EXPECT_FALSE(sts::planner::event_flag_set(bit, other.id))
                << "bit for " << e.symbol << " also read as " << other.symbol;
        }
    }
    // EventId::NONE never fires and has no bit; a shift by -1 would be UB.
    EXPECT_FALSE(sts::planner::event_flag_set(0xFFFFFFFFu, EventId::NONE));
    // S2.02's Act-2/3 ids 32..51 are past the end of the word. They read false
    // for EVERY flags value -- including all-ones -- rather than shifting out
    // of range. That is a live guard, not a formality: S2.13 makes those ids
    // drawable and has to widen the storage (or split it per act) first.
    for (const auto& e : sts::planner::event_names()) {
        if (has_bit(e.id)) continue;
        SCOPED_TRACE(std::string(e.symbol));
        EXPECT_GT(static_cast<uint32_t>(e.id), 31u);
        EXPECT_FALSE(sts::planner::event_flag_set(0xFFFFFFFFu, e.id));
        EXPECT_FALSE(sts::planner::event_flag_set(0u, e.id));
    }
}

TEST(SeedScanEventFlags, DecodesInAscendingIdOrder) {
    const uint32_t flags = (1u << (12 - 1)) |  // Match and Keep!
                           (1u << (1 - 1)) |   // Big Fish
                           (1u << (31 - 1));   // The Woman in Blue
    const std::vector<EventId> ids = sts::planner::decode_event_flags(flags);
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], EventId::BIG_FISH);
    EXPECT_EQ(ids[1], EventId::MATCH_AND_KEEP);
    EXPECT_EQ(ids[2], EventId::THE_WOMAN_IN_BLUE);

    EXPECT_EQ(sts::planner::event_flags_text(flags),
              "Big Fish|Match and Keep!|The Woman in Blue");
    EXPECT_EQ(sts::planner::event_flags_text(0), "");
    EXPECT_TRUE(sts::planner::decode_event_flags(0).empty());
}

TEST(SeedScanEventFlags, SeparatorNeverOccursInsideAName) {
    // The `events` column joins on '|'. Every other plausible separator
    // (space, comma, tab) occurs inside a real game id, which would make the
    // column unparseable; assert the chosen one does not.
    for (const auto& e : sts::planner::event_names()) {
        EXPECT_EQ(e.game_id.find('|'), std::string_view::npos) << e.game_id;
        EXPECT_EQ(e.game_id.find('\t'), std::string_view::npos) << e.game_id;
    }
}

// --- 3. determinism ----------------------------------------------------------

TEST(SeedScanDeterminism, SameCaseGivesByteIdenticalRows) {
    // The guard the whole tool rests on. The policies draw only from
    // fuzz::PolicyRng, a private splitmix64 seeded from policy_seed
    // (tools/fuzz/include/sts/fuzz/policy.hpp:38-62), and the engine is a
    // function of its seed -- so a row must be a pure function of its case.
    // Comparing the SERIALIZED rows (not selected fields) is what makes this
    // the same claim the CLI's --verify-determinism makes.
    const std::string seeds[] = {"STS00100", "STS00101", "STS00102"};
    const PolicyKind policies[] = {PolicyKind::RANDOM, PolicyKind::GREEDY_DAMAGE};

    for (const std::string& seed : seeds) {
        for (PolicyKind p : policies) {
            for (uint64_t pseed : {0ull, 7ull}) {
                const ScanCase c = MakeCase(seed, p, pseed);
                SCOPED_TRACE(seed + " policy=" + sts::fuzz::policy_name(p) +
                             " pseed=" + std::to_string(pseed));
                const ScanRow a = sts::planner::scan_case(c, SmallLimits());
                const ScanRow b = sts::planner::scan_case(c, SmallLimits());
                EXPECT_EQ(sts::planner::row_to_tsv(a), sts::planner::row_to_tsv(b));
                EXPECT_EQ(sts::planner::row_to_jsonl(a), sts::planner::row_to_jsonl(b));
            }
        }
    }
}

TEST(SeedScanDeterminism, ScanIsNotVacuous) {
    // A guard against the above passing because every row is empty: the scan
    // must actually drive runs, and different cases must produce different
    // rows. Without this, a scan_case that returned a default ScanRow would
    // be perfectly "deterministic".
    const ScanRow a = sts::planner::scan_case(
        MakeCase("STS00100", PolicyKind::RANDOM, 0), SmallLimits());
    EXPECT_GT(a.actions, 0u);
    EXPECT_EQ(a.seed.value, sts::engine::seed_from_string("STS00100"));
    EXPECT_EQ(a.fail_kind, "none") << "a scanned case reported a fuzz finding";

    bool any_difference = false;
    for (uint64_t pseed = 1; pseed <= 8 && !any_difference; ++pseed) {
        const ScanRow b = sts::planner::scan_case(
            MakeCase("STS00100", PolicyKind::RANDOM, pseed), SmallLimits());
        if (sts::planner::row_to_tsv(b) != sts::planner::row_to_tsv(a)) {
            any_difference = true;
        }
    }
    EXPECT_TRUE(any_difference)
        << "no policy seed changed the outcome -- the scan is not exploring";
}

TEST(SeedScanDeterminism, SeedTextAndValueAgree) {
    // The bridge join key: sim_seed_int64 == seed_to_long(game_seed_string)
    // (campaign_driver.py:199-206). A row carrying a mismatched pair would
    // send a capture at a different run than the one that was scanned.
    const ScanRow r = sts::planner::scan_case(
        MakeCase("STS12345", PolicyKind::RANDOM, 0), SmallLimits());
    EXPECT_EQ(r.seed.text, "STS12345");
    EXPECT_EQ(r.seed.value, 1790052133945);
}

// --- 4. filtering ------------------------------------------------------------

namespace {

ScanRow FakeRow(uint32_t flags, bool treasure, bool boss, uint32_t floor) {
    ScanRow r;
    r.seed = MakeSeed("STS00001");
    r.event_flags = flags;
    r.treasure_entered = treasure;
    r.boss_reached = boss;
    r.max_floor = floor;
    return r;
}

constexpr uint32_t kMatchAndKeepBit = 1u << (12 - 1);
constexpr uint32_t kGoldenShrineBit = 1u << (13 - 1);

}  // namespace

TEST(SeedScanFilter, RowHitsRequiresEveryClause) {
    Filter f;
    f.need_events = {EventId::MATCH_AND_KEEP};
    f.need_treasure = true;
    f.min_floor = 5;

    EXPECT_TRUE(sts::planner::row_hits(FakeRow(kMatchAndKeepBit, true, false, 5), f));
    EXPECT_TRUE(sts::planner::row_hits(FakeRow(kMatchAndKeepBit, true, false, 9), f));
    // Each clause alone must be able to reject.
    EXPECT_FALSE(sts::planner::row_hits(FakeRow(kGoldenShrineBit, true, false, 9), f));
    EXPECT_FALSE(sts::planner::row_hits(FakeRow(kMatchAndKeepBit, false, false, 9), f));
    EXPECT_FALSE(sts::planner::row_hits(FakeRow(kMatchAndKeepBit, true, false, 4), f));

    // Multiple events are an AND within one run, not an OR across runs.
    Filter both;
    both.need_events = {EventId::MATCH_AND_KEEP, EventId::GOLDEN_SHRINE};
    EXPECT_FALSE(sts::planner::row_hits(FakeRow(kMatchAndKeepBit, false, false, 0), both));
    EXPECT_TRUE(sts::planner::row_hits(
        FakeRow(kMatchAndKeepBit | kGoldenShrineBit, false, false, 0), both));

    // An empty filter matches everything, and says so.
    Filter none;
    EXPECT_TRUE(none.empty());
    EXPECT_TRUE(sts::planner::row_hits(FakeRow(0, false, false, 0), none));
    EXPECT_FALSE(f.empty());
}

TEST(SeedScanFilter, MinHitCountExcludesSingleHitSeeds) {
    // The rule that makes a candidate list worth having: the capture runs a
    // DIFFERENT policy from the scan, so one combination finding the target is
    // "reachable", not "will be reached".
    const std::vector<ScanRow> one_hit = {
        FakeRow(kMatchAndKeepBit, false, false, 6),
        FakeRow(0, false, false, 6),
        FakeRow(0, false, false, 6),
    };
    const std::vector<ScanRow> three_hits = {
        FakeRow(kMatchAndKeepBit, false, false, 6),
        FakeRow(kMatchAndKeepBit, false, false, 6),
        FakeRow(kMatchAndKeepBit, false, false, 6),
    };

    Filter f;
    f.need_events = {EventId::MATCH_AND_KEEP};

    EXPECT_EQ(sts::planner::count_hits(one_hit, f), 1u);
    EXPECT_EQ(sts::planner::count_hits(three_hits, f), 3u);

    f.min_hit_count = 1;
    EXPECT_TRUE(sts::planner::seed_qualifies(one_hit, f));
    f.min_hit_count = 2;
    EXPECT_FALSE(sts::planner::seed_qualifies(one_hit, f));
    EXPECT_TRUE(sts::planner::seed_qualifies(three_hits, f));
    f.min_hit_count = 4;
    EXPECT_FALSE(sts::planner::seed_qualifies(three_hits, f));

    // min_hit_count 0 must not qualify a seed with no rows at all.
    f.min_hit_count = 0;
    EXPECT_FALSE(sts::planner::seed_qualifies({}, f));
}

// --- relic targeting ---------------------------------------------------------

namespace {

RelicObs MakeObs(RelicId id, bool offered, bool acquired, bool shop,
                 bool reward_offered = false) {
    RelicObs o;
    o.id = id;
    o.offered = offered;
    o.reward_offered = reward_offered;
    o.acquired = acquired;
    o.shop_while_owned = shop;
    return o;
}

}  // namespace

TEST(SeedScanRelicFilter, ClausesAreAnyOfWithinAndAndAcross) {
    // The motivating query: "ANY of the three Bottled relics was offered".
    // need_events is all-of; the relic clauses must NOT be, or the bottle
    // query could never be satisfied by a real run.
    ScanRow flame_offered = FakeRow(0, false, false, 6);
    flame_offered.relic_obs = {
        MakeObs(RelicId::BOTTLED_FLAME, true, false, false),
        MakeObs(RelicId::BOTTLED_LIGHTNING, false, false, false),
        MakeObs(RelicId::BOTTLED_TORNADO, false, false, false),
    };

    Filter any_bottle;
    any_bottle.need_relic_offered = {RelicId::BOTTLED_FLAME,
                                     RelicId::BOTTLED_LIGHTNING,
                                     RelicId::BOTTLED_TORNADO};
    EXPECT_TRUE(sts::planner::row_hits(flame_offered, any_bottle));

    // Offered is not acquired: the same row must fail an acquired clause.
    Filter any_bottle_acquired;
    any_bottle_acquired.need_relic_acquired = any_bottle.need_relic_offered;
    EXPECT_FALSE(sts::planner::row_hits(flame_offered, any_bottle_acquired));
    EXPECT_FALSE(any_bottle.empty());
    EXPECT_FALSE(any_bottle_acquired.empty());

    // A shelf-only offer is not a REWARD-ROW offer -- the affordability
    // distinction the first bottle scan missed (a shelf bottle against ~130
    // gold is unclaimable; a reward-row bottle is free).
    Filter reward_only;
    reward_only.need_relic_reward_offered = any_bottle.need_relic_offered;
    EXPECT_FALSE(sts::planner::row_hits(flame_offered, reward_only))
        << "offered=1 with reward_offered=0 is a shelf offer";
    EXPECT_FALSE(reward_only.empty());
    ScanRow flame_reward = flame_offered;
    flame_reward.relic_obs[0] = MakeObs(RelicId::BOTTLED_FLAME, true, false,
                                        false, /*reward_offered=*/true);
    EXPECT_TRUE(sts::planner::row_hits(flame_reward, reward_only));

    // Clauses AND with each other and with the non-relic clauses.
    ScanRow courier_shop = FakeRow(0, false, false, 8);
    courier_shop.relic_obs = {MakeObs(RelicId::THE_COURIER, true, true, true)};
    Filter courier;
    courier.need_relic_acquired = {RelicId::THE_COURIER};
    courier.need_shop_after_relic = {RelicId::THE_COURIER};
    EXPECT_TRUE(sts::planner::row_hits(courier_shop, courier));
    courier.min_floor = 9;
    EXPECT_FALSE(sts::planner::row_hits(courier_shop, courier));
    courier.min_floor = 0;
    ScanRow courier_no_shop = courier_shop;
    courier_no_shop.relic_obs = {
        MakeObs(RelicId::THE_COURIER, true, true, false)};
    EXPECT_FALSE(sts::planner::row_hits(courier_no_shop, courier));
}

TEST(SeedScanRelicFilter, UntrackedRelicNeverHits) {
    // A row cannot testify about a relic it did not watch. Silently passing
    // here would qualify every seed of a scan whose --track/--need lists
    // disagreed -- the planner's canonical silent-wrong-list hazard.
    ScanRow row = FakeRow(0, false, false, 6);
    row.relic_obs = {MakeObs(RelicId::BOTTLED_FLAME, true, true, true)};
    Filter f;
    f.need_relic_offered = {RelicId::THE_COURIER};
    EXPECT_FALSE(sts::planner::row_hits(row, f));
    // Empty relic clauses constrain nothing.
    Filter none;
    EXPECT_TRUE(none.empty());
    EXPECT_TRUE(sts::planner::row_hits(row, none));
}

TEST(SeedScanRelicObs, StarterRelicIsAcquiredFromFloorZeroAndNeverOffered) {
    // A live-path pin that needs no lottery: Burning Blood is the Ironclad's
    // starter, so it is OWNED from run_begin and can never sit on a reward
    // row or a merchant shelf. The Courier, conversely, is essentially never
    // acquired by a 600-action random run -- both directions guard against
    // the observer latching the wrong field.
    const std::vector<RelicId> targets = {RelicId::BURNING_BLOOD,
                                          RelicId::THE_COURIER};
    const ScanRow r = sts::planner::scan_case(
        MakeCase("STS00100", PolicyKind::RANDOM, 0), SmallLimits(), targets);
    ASSERT_EQ(r.relic_obs.size(), 2u);
    EXPECT_EQ(r.relic_obs[0].id, RelicId::BURNING_BLOOD);
    EXPECT_TRUE(r.relic_obs[0].acquired);
    EXPECT_FALSE(r.relic_obs[0].offered);
    EXPECT_EQ(r.relic_obs[1].id, RelicId::THE_COURIER);

    // Determinism must hold with targets attached (the serialized comparison,
    // same claim as the CLI's --verify-determinism).
    const ScanRow again = sts::planner::scan_case(
        MakeCase("STS00100", PolicyKind::RANDOM, 0), SmallLimits(), targets);
    EXPECT_EQ(sts::planner::row_to_tsv(r), sts::planner::row_to_tsv(again));
    EXPECT_EQ(sts::planner::row_to_jsonl(r), sts::planner::row_to_jsonl(again));

    // And the untracked row must serialize exactly as before targets existed.
    const ScanRow untracked = sts::planner::scan_case(
        MakeCase("STS00100", PolicyKind::RANDOM, 0), SmallLimits());
    EXPECT_TRUE(untracked.relic_obs.empty());
}

// --- output ------------------------------------------------------------------

TEST(SeedScanOutput, TsvRowMatchesHeaderWidth) {
    const ScanRow r = sts::planner::scan_case(
        MakeCase("STS00100", PolicyKind::RANDOM, 0), SmallLimits());
    const std::string header(sts::planner::tsv_header());
    const std::string row = sts::planner::row_to_tsv(r);
    EXPECT_EQ(CountTabs(header), CountTabs(row))
        << "header: " << header << "\nrow:    " << row;
    EXPECT_EQ(row.find('\n'), std::string::npos);

    // A row whose events column is populated must still have the same width --
    // the '|' join is what guarantees that, and this is where it is proven.
    ScanRow with_events = r;
    with_events.event_flags = kMatchAndKeepBit | kGoldenShrineBit;
    const std::string wide = sts::planner::row_to_tsv(with_events);
    EXPECT_EQ(CountTabs(header), CountTabs(wide)) << wide;
    EXPECT_NE(wide.find("Match and Keep!|Golden Shrine"), std::string::npos);

    // The relic_obs column joins on '|' too, and must not change the width.
    ScanRow with_relics = r;
    with_relics.relic_obs = {
        MakeObs(RelicId::BOTTLED_FLAME, true, true, false),
        MakeObs(RelicId::THE_COURIER, false, false, false),
    };
    const std::string relic_row = sts::planner::row_to_tsv(with_relics);
    EXPECT_EQ(CountTabs(header), CountTabs(relic_row)) << relic_row;
    EXPECT_NE(relic_row.find("Bottled Flame=1010|The Courier=0000"),
              std::string::npos)
        << relic_row;
}

TEST(SeedScanOutput, JsonlEscapesAndSelects) {
    EXPECT_EQ(sts::planner::json_escape("a\"b\\c"), "a\\\"b\\\\c");
    EXPECT_EQ(sts::planner::json_escape("a\tb"), "a\\tb");
    EXPECT_EQ(sts::planner::json_escape("N'loth"), "N'loth");

    ScanRow r = FakeRow(kMatchAndKeepBit, true, false, 6);
    r.relic_obs = {MakeObs(RelicId::THE_COURIER, true, false, false)};
    const std::string j = sts::planner::row_to_jsonl(r);
    EXPECT_EQ(j.front(), '{');
    EXPECT_EQ(j.back(), '}');
    EXPECT_NE(j.find("\"seed\":\"STS00001\""), std::string::npos) << j;
    EXPECT_NE(j.find("\"treasure\":true"), std::string::npos) << j;
    EXPECT_NE(j.find("\"boss\":false"), std::string::npos) << j;
    EXPECT_NE(j.find("\"events\":[\"Match and Keep!\"]"), std::string::npos) << j;
    EXPECT_NE(j.find("\"relic_obs\":[{\"relic\":\"The Courier\","
                     "\"offered\":true,\"reward_offered\":false,"
                     "\"acquired\":false,\"shop_while_owned\":false}]"),
              std::string::npos)
        << j;
    EXPECT_EQ(j.find('\n'), std::string::npos);

    Format f{};
    EXPECT_TRUE(sts::planner::format_from_name("jsonl", f));
    EXPECT_EQ(f, Format::JSONL);
    EXPECT_TRUE(sts::planner::format_from_name("TSV", f));
    EXPECT_EQ(f, Format::TSV);
    EXPECT_FALSE(sts::planner::format_from_name("csv", f));
}

TEST(SeedScanOutput, SummaryBucketsFloorsAndEvents) {
    sts::planner::ScanSummary s;
    s.seeds = 2;
    s.add(FakeRow(kMatchAndKeepBit, true, false, 0));
    s.add(FakeRow(kMatchAndKeepBit, false, true, 3));
    s.add(FakeRow(0, false, false, 99));  // clamps into the last bucket

    EXPECT_EQ(s.rows, 3u);
    EXPECT_EQ(s.treasure_rows, 1u);
    EXPECT_EQ(s.boss_rows, 1u);
    EXPECT_EQ(s.max_floor, 99u);
    EXPECT_EQ(s.floor_hist[0], 1u);
    EXPECT_EQ(s.floor_hist[3], 1u);
    EXPECT_EQ(s.floor_hist[sts::planner::kFloorHistogramBuckets - 1], 1u);
    EXPECT_EQ(s.event_rows[static_cast<int>(EventId::MATCH_AND_KEEP)], 2u);
    EXPECT_EQ(s.event_rows[static_cast<int>(EventId::GOLDEN_SHRINE)], 0u);

    const std::string text = s.text();
    EXPECT_NE(text.find("Match and Keep!: 2"), std::string::npos) << text;
    EXPECT_NE(text.find("treasure_entered: 1/3"), std::string::npos) << text;
}
