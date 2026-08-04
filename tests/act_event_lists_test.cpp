// act_event_lists_test.cpp -- S2.02 tier-2: the Act-2/Act-3 event rows and the
// per-act membership / draw-gate metadata every events.yaml row now carries.
//
// WHAT THIS FILE OWNS, and what it deliberately does not.
// registry_gen_test.cpp §7 pins event IDENTITY (dense ids in Java add() order,
// game_id round-trip, duplicate-id rejection) across all five canonical lists.
// This file pins the metadata S2.02 added on top of that identity:
//   * `conditions.pool`  -> EventDef::pool
//   * `conditions.acts`  -> EventDef::act_mask, and event_in_act()
//   * the two per-act eventLists as ORDERED lists, against the Java
//   * the ORDER DIVERGENCE between Exordium's shrineList and the Act-2/3 one
//   * the generator's refusal of every malformed conditions block
// It pins no BODY behaviour: ids 32-51 are identity rows with `implemented`
// absent, and their option trees / A15 branches belong to S2.31-S2.33. The
// per-act list REBUILD and the draw itself belong to S2.13; nothing here calls
// build_event_pool or generate_event, because in this commit those still know
// only Act 1 and saying otherwise would be a test of a plan, not of the engine.
//
// Provenance -- every list and gate below was read in full from
// D:\STS_BG_Mod\SlayTheSpireDecompiled:
//   * Exordium.initializeEventList        Exordium.java:224-235
//   * Exordium.initializeShrineList       Exordium.java:239-245
//   * TheCity.initializeEventList         TheCity.java:185-198
//   * TheCity.initializeShrineList        TheCity.java:211-217
//   * TheBeyond.initializeEventList       TheBeyond.java:179-186
//   * TheBeyond.initializeShrineList      TheBeyond.java:199-205
//   * AbstractDungeon.initializeSpecialOneTimeEventList
//                                         AbstractDungeon.java:1340-1358
//   * AbstractDungeon.getShrine           AbstractDungeon.java:1882-1942
//   * AbstractDungeon.getEvent            AbstractDungeon.java:1944-1990
//   * CardCrawlGame one-time-list carry   CardCrawlGame.java:1102-1119
// Every expected value is hand-carried from those lines, never read back from
// the generator.

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/registry/event_table.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/ids.hpp"
#include "sts/registry/manifest.hpp"

#include "host_shell.hpp"

namespace fs = std::filesystem;
namespace r = sts::registry;

namespace {

const char* kPython = STS_PYTHON_EXECUTABLE;
const char* kGenPy = STS_REGISTRY_GEN_PY;
const char* kRegistryDir = STS_REGISTRY_DIR;
const char* kScratchDir = STS_GEN_SCRATCH;

using sts::testing::run_shell;
using sts::testing::shell_quote;

int run_generator(const std::string& registry_dir, const std::string& out_dir,
                  const std::string& err_file) {
    return run_shell(shell_quote(kPython) + " " + shell_quote(kGenPy) +
                     " --registry " + shell_quote(registry_dir) + " --out " +
                     shell_quote(out_dir) + " 2> " + shell_quote(err_file));
}

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

fs::path clone_registry(const fs::path& scratch, const char* name) {
    const fs::path dst = scratch / name;
    fs::remove_all(dst);
    fs::create_directories(dst);
    for (const auto& e : fs::directory_iterator(kRegistryDir)) {
        if (e.path().extension() == ".yaml") {
            fs::copy_file(e.path(), dst / e.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
    }
    return dst;
}

// Append one otherwise-well-formed event row whose `conditions` block is the
// thing under test, then assert the generator rejects it with `needle`. The id
// is far above the allocated block so the loader's duplicate-id gate (which
// runs FIRST, in validate_common) cannot be what fails: the failure has to come
// from the conditions validator itself.
void expect_conditions_rejected(const char* case_name, const char* conditions,
                                const char* needle) {
    const fs::path scratch = fs::path(kScratchDir);
    fs::create_directories(scratch);
    const fs::path bad_reg = clone_registry(scratch, case_name);
    {
        std::ofstream events(bad_reg / "events.yaml", std::ios::app);
        events << "\n- id: 9001\n  name: SYNTHETIC_BAD_CONDITIONS\n"
                  "  game_id: \"Synthetic Bad Conditions\"\n"
                  "  provenance: \"synthetic row for the S2.02 negative test\"\n"
                  "  native: true\n"
               << conditions << "\n";
    }
    const fs::path out = scratch / (std::string(case_name) + "_out");
    const fs::path err = scratch / (std::string(case_name) + "_err.txt");
    fs::remove_all(out);
    const int status = run_generator(bad_reg.string(), out.string(), err.string());
    EXPECT_NE(status, 0) << "generator should reject " << case_name;
    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("events.yaml"), std::string::npos) << msg;
    EXPECT_NE(msg.find("SYNTHETIC_BAD_CONDITIONS"), std::string::npos) << msg;
    EXPECT_NE(msg.find(needle), std::string::npos) << msg;
}

// --- The hand-carried Java lists ---------------------------------------------

// TheCity.initializeEventList (TheCity.java:186-198), in add() order.
constexpr std::array<std::string_view, 13> kCityEventList = {
    "Addict", "Back to Basics", "Beggar", "Colosseum", "Cursed Tome",
    "Drug Dealer", "Forgotten Altar", "Ghosts", "Masked Bandits", "Nest",
    "The Library", "The Mausoleum", "Vampires",
};

// TheBeyond.initializeEventList (TheBeyond.java:180-186), in add() order.
constexpr std::array<std::string_view, 7> kBeyondEventList = {
    "Falling", "MindBloom", "The Moai Head", "Mysterious Sphere",
    "SensoryStone", "Tomb of Lord Red Mask", "Winding Halls",
};

// Exordium.initializeEventList (Exordium.java:225-235), in add() order.
constexpr std::array<std::string_view, 11> kExordiumEventList = {
    "Big Fish", "The Cleric", "Dead Adventurer", "Golden Idol", "Golden Wing",
    "World of Goop", "Liars Game", "Living Wall", "Mushrooms", "Scrap Ooze",
    "Shining Light",
};

// Exordium.initializeShrineList (Exordium.java:240-245), in add() order.
constexpr std::array<std::string_view, 6> kExordiumShrineList = {
    "Match and Keep!", "Golden Shrine", "Transmorgrifier", "Purifier",
    "Upgrade Shrine", "Wheel of Change",
};

// TheCity.initializeShrineList (TheCity.java:212-217) and
// TheBeyond.initializeShrineList (TheBeyond.java:200-205) -- the SAME six keys
// as Exordium's, in a DIFFERENT order. Both later acts use this one.
constexpr std::array<std::string_view, 6> kCityBeyondShrineList = {
    "Match and Keep!", "Wheel of Change", "Golden Shrine", "Transmorgrifier",
    "Purifier", "Upgrade Shrine",
};

// AbstractDungeon.getShrine's per-key act half (AbstractDungeon.java:1889-1934),
// one row per specialOneTimeEventList entry. A key with no `id.equals` test in
// its case -- or no case at all, falling through to the unconditional
// `tmp.add(e)` at :1935 -- is drawable in every act.
struct SpecialActGate {
    r::EventId id;
    uint8_t act_mask;
    const char* why;
};

constexpr std::array<SpecialActGate, 14> kSpecialActGates = {{
    {r::EventId::ACCURSED_BLACKSMITH, 0x7, "no case; unconditional add (:1935)"},
    {r::EventId::BONFIRE_ELEMENTALS, 0x7, "no case; unconditional add (:1935)"},
    {r::EventId::DESIGNER, 0x6, "TheCity or TheBeyond (:1894-1898)"},
    {r::EventId::DUPLICATOR, 0x6, "TheCity or TheBeyond (:1899-1903)"},
    {r::EventId::FACE_TRADER, 0x3, "TheCity or Exordium (:1904-1908)"},
    {r::EventId::FOUNTAIN_OF_CLEANSING, 0x7, "isCursed only, no act test (:1889-1893)"},
    {r::EventId::KNOWING_SKULL, 0x2, "TheCity only (:1909-1913)"},
    {r::EventId::LAB, 0x7, "no case; unconditional add (:1935)"},
    // The decompiled `!id.equals("TheCity") && !id.equals("TheCity")` is one
    // test written twice, not a two-act disjunction: N'loth is Act 2 only.
    {r::EventId::NLOTH, 0x2, "TheCity only, duplicated test (:1914-1918)"},
    {r::EventId::NOTE_FOR_YOURSELF, 0x7, "no case; unconditional add (:1935)"},
    {r::EventId::SECRET_PORTAL, 0x4, "TheBeyond only (:1929-1933)"},
    {r::EventId::THE_JOUST, 0x2, "TheCity only (:1919-1923)"},
    {r::EventId::WE_MEET_AGAIN, 0x7, "no case; unconditional add (:1935)"},
    {r::EventId::THE_WOMAN_IN_BLUE, 0x7, "gold only, no act test (:1924-1928)"},
}};

}  // namespace

// --- 1. Id pins: the S1 block did not move, the S2 block is where it was
//        granted ---------------------------------------------------------------
//
// Compile-time, because a renumber must be a build failure and not a red test:
// ids are the translator join key and the RunState bit positions.
static_assert(static_cast<int>(r::EventId::NONE) == 0);
static_assert(static_cast<int>(r::EventId::BIG_FISH) == 1);
static_assert(static_cast<int>(r::EventId::THE_CLERIC) == 2);
static_assert(static_cast<int>(r::EventId::DEAD_ADVENTURER) == 3);
static_assert(static_cast<int>(r::EventId::GOLDEN_IDOL) == 4);
static_assert(static_cast<int>(r::EventId::GOLDEN_WING) == 5);
static_assert(static_cast<int>(r::EventId::WORLD_OF_GOOP) == 6);
static_assert(static_cast<int>(r::EventId::LIARS_GAME) == 7);
static_assert(static_cast<int>(r::EventId::LIVING_WALL) == 8);
static_assert(static_cast<int>(r::EventId::MUSHROOMS) == 9);
static_assert(static_cast<int>(r::EventId::SCRAP_OOZE) == 10);
static_assert(static_cast<int>(r::EventId::SHINING_LIGHT) == 11);
static_assert(static_cast<int>(r::EventId::MATCH_AND_KEEP) == 12);
static_assert(static_cast<int>(r::EventId::GOLDEN_SHRINE) == 13);
static_assert(static_cast<int>(r::EventId::TRANSMORGRIFIER) == 14);
static_assert(static_cast<int>(r::EventId::PURIFIER) == 15);
static_assert(static_cast<int>(r::EventId::UPGRADE_SHRINE) == 16);
static_assert(static_cast<int>(r::EventId::WHEEL_OF_CHANGE) == 17);
static_assert(static_cast<int>(r::EventId::ACCURSED_BLACKSMITH) == 18);
static_assert(static_cast<int>(r::EventId::BONFIRE_ELEMENTALS) == 19);
static_assert(static_cast<int>(r::EventId::DESIGNER) == 20);
static_assert(static_cast<int>(r::EventId::DUPLICATOR) == 21);
static_assert(static_cast<int>(r::EventId::FACE_TRADER) == 22);
static_assert(static_cast<int>(r::EventId::FOUNTAIN_OF_CLEANSING) == 23);
static_assert(static_cast<int>(r::EventId::KNOWING_SKULL) == 24);
static_assert(static_cast<int>(r::EventId::LAB) == 25);
static_assert(static_cast<int>(r::EventId::NLOTH) == 26);
static_assert(static_cast<int>(r::EventId::NOTE_FOR_YOURSELF) == 27);
static_assert(static_cast<int>(r::EventId::SECRET_PORTAL) == 28);
static_assert(static_cast<int>(r::EventId::THE_JOUST) == 29);
static_assert(static_cast<int>(r::EventId::WE_MEET_AGAIN) == 30);
static_assert(static_cast<int>(r::EventId::THE_WOMAN_IN_BLUE) == 31);
// The S2.02 block granted by s2-tasks.md: 32-44 Act 2, 45-51 Act 3.
static_assert(static_cast<int>(r::EventId::ADDICT) == 32);
static_assert(static_cast<int>(r::EventId::BACK_TO_BASICS) == 33);
static_assert(static_cast<int>(r::EventId::BEGGAR) == 34);
static_assert(static_cast<int>(r::EventId::COLOSSEUM) == 35);
static_assert(static_cast<int>(r::EventId::CURSED_TOME) == 36);
static_assert(static_cast<int>(r::EventId::DRUG_DEALER) == 37);
static_assert(static_cast<int>(r::EventId::FORGOTTEN_ALTAR) == 38);
static_assert(static_cast<int>(r::EventId::GHOSTS) == 39);
static_assert(static_cast<int>(r::EventId::MASKED_BANDITS) == 40);
static_assert(static_cast<int>(r::EventId::NEST) == 41);
static_assert(static_cast<int>(r::EventId::THE_LIBRARY) == 42);
static_assert(static_cast<int>(r::EventId::THE_MAUSOLEUM) == 43);
static_assert(static_cast<int>(r::EventId::VAMPIRES) == 44);
static_assert(static_cast<int>(r::EventId::FALLING) == 45);
static_assert(static_cast<int>(r::EventId::MIND_BLOOM) == 46);
static_assert(static_cast<int>(r::EventId::THE_MOAI_HEAD) == 47);
static_assert(static_cast<int>(r::EventId::MYSTERIOUS_SPHERE) == 48);
static_assert(static_cast<int>(r::EventId::SENSORY_STONE) == 49);
static_assert(static_cast<int>(r::EventId::TOMB_OF_LORD_RED_MASK) == 50);
static_assert(static_cast<int>(r::EventId::WINDING_HALLS) == 51);
static_assert(r::kEventTable.size() == 51);

// --- 2. The two new eventLists, as ordered lists ------------------------------

TEST(ActEventLists, CityEventListIsThirteenRowsInJavaAddOrder) {
    ASSERT_EQ(kCityEventList.size(), 13u);
    for (std::size_t i = 0; i < kCityEventList.size(); ++i) {
        const r::EventId id = r::event_from_game_id(kCityEventList[i]);
        ASSERT_NE(id, r::EventId::NONE)
            << "TheCity list key not in the registry: " << kCityEventList[i];
        // Position i of TheCity.initializeEventList holds id 32 + i: the Act-2
        // block is dense and in add() order, exactly as the Act-1 blocks are.
        EXPECT_EQ(static_cast<int>(id), 32 + static_cast<int>(i))
            << kCityEventList[i];
        EXPECT_EQ(r::event_game_id(id), kCityEventList[i]);
        const r::EventDef* def = r::event_def(id);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->pool, r::EventPool::EVENT) << kCityEventList[i];
        // Act 2 ONLY -- no Act-2 eventList key appears in Exordium's or
        // TheBeyond's list.
        EXPECT_EQ(def->act_mask, r::kEventActMaskCity) << kCityEventList[i];
        EXPECT_TRUE(r::event_in_act(id, 2));
        EXPECT_FALSE(r::event_in_act(id, 1));
        EXPECT_FALSE(r::event_in_act(id, 3));
        // Identity rows: native metadata, no linked body yet (S2.31/S2.32).
        EXPECT_TRUE(def->native) << kCityEventList[i];
        EXPECT_FALSE(def->implemented) << kCityEventList[i];
        EXPECT_EQ(def->screen_count, 0) << kCityEventList[i];
        EXPECT_EQ(def->a15_change_count, 0) << kCityEventList[i];
    }
}

TEST(ActEventLists, BeyondEventListIsSevenRowsInJavaAddOrder) {
    ASSERT_EQ(kBeyondEventList.size(), 7u);
    for (std::size_t i = 0; i < kBeyondEventList.size(); ++i) {
        const r::EventId id = r::event_from_game_id(kBeyondEventList[i]);
        ASSERT_NE(id, r::EventId::NONE)
            << "TheBeyond list key not in the registry: " << kBeyondEventList[i];
        EXPECT_EQ(static_cast<int>(id), 45 + static_cast<int>(i))
            << kBeyondEventList[i];
        EXPECT_EQ(r::event_game_id(id), kBeyondEventList[i]);
        const r::EventDef* def = r::event_def(id);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->pool, r::EventPool::EVENT) << kBeyondEventList[i];
        EXPECT_EQ(def->act_mask, r::kEventActMaskBeyond) << kBeyondEventList[i];
        EXPECT_TRUE(r::event_in_act(id, 3));
        EXPECT_FALSE(r::event_in_act(id, 1));
        EXPECT_FALSE(r::event_in_act(id, 2));
        EXPECT_TRUE(def->native) << kBeyondEventList[i];
        EXPECT_FALSE(def->implemented) << kBeyondEventList[i];
        EXPECT_EQ(def->screen_count, 0) << kBeyondEventList[i];
        EXPECT_EQ(def->a15_change_count, 0) << kBeyondEventList[i];
    }
}

// The three eventLists partition the EVENT pool: every EVENT-pool row belongs to
// exactly one act, and the three lists' sizes add up to the pool. This is what
// makes `acts` literal membership for this pool rather than a gate.
TEST(ActEventLists, EventPoolRowsBelongToExactlyOneAct) {
    int per_act[4] = {0, 0, 0, 0};
    for (const r::EventDef& row : r::kEventTable) {
        if (row.pool != r::EventPool::EVENT) continue;
        int acts = 0;
        for (int a = 1; a <= 3; ++a) {
            if (r::event_in_act(row.id, a)) {
                ++acts;
                ++per_act[a];
            }
        }
        EXPECT_EQ(acts, 1) << "EVENT-pool row " << r::event_game_id(row.id)
                           << " claims " << acts << " acts";
    }
    EXPECT_EQ(per_act[1], 11) << "Exordium.initializeEventList (Exordium.java:225-235)";
    EXPECT_EQ(per_act[2], 13) << "TheCity.initializeEventList (TheCity.java:186-198)";
    EXPECT_EQ(per_act[3], 7) << "TheBeyond.initializeEventList (TheBeyond.java:180-186)";
    EXPECT_EQ(per_act[1] + per_act[2] + per_act[3], 31);
}

TEST(ActEventLists, ExordiumEventListStillActOneOnly) {
    for (std::size_t i = 0; i < kExordiumEventList.size(); ++i) {
        const r::EventId id = r::event_from_game_id(kExordiumEventList[i]);
        ASSERT_NE(id, r::EventId::NONE) << kExordiumEventList[i];
        EXPECT_EQ(static_cast<int>(id), 1 + static_cast<int>(i));
        const r::EventDef* def = r::event_def(id);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->pool, r::EventPool::EVENT);
        EXPECT_EQ(def->act_mask, r::kEventActMaskExordium) << kExordiumEventList[i];
    }
}

// --- 3. The shrine list: same six keys in all three acts, DIFFERENT order ------

TEST(ActEventLists, ShrineRowsAreDrawableInEveryAct) {
    for (std::string_view key : kExordiumShrineList) {
        const r::EventId id = r::event_from_game_id(key);
        ASSERT_NE(id, r::EventId::NONE) << key;
        const r::EventDef* def = r::event_def(id);
        ASSERT_NE(def, nullptr);
        EXPECT_EQ(def->pool, r::EventPool::SHRINE) << key;
        EXPECT_EQ(def->act_mask,
                  r::kEventActMaskExordium | r::kEventActMaskCity |
                      r::kEventActMaskBeyond)
            << key;
        for (int a = 1; a <= 3; ++a) EXPECT_TRUE(r::event_in_act(id, a)) << key;
    }
    // Exactly six SHRINE-pool rows: the later acts add no new key.
    int shrines = 0;
    for (const r::EventDef& row : r::kEventTable) {
        if (row.pool == r::EventPool::SHRINE) ++shrines;
    }
    EXPECT_EQ(shrines, 6);
}

// THE TRAP S2.02 FOUND. Exordium's shrineList and the Act-2/3 one hold the same
// six keys in DIFFERENT positions: both later acts move "Wheel of Change" from
// position 5 to position 1 and push the middle four down one. Order is
// load-bearing twice -- getShrine seeds `tmp.addAll(shrineList)` in list order
// (AbstractDungeon.java:1884) and then indexes tmp with
// `rng.random(tmp.size() - 1)` (:1937), so the same draw resolves to a
// different shrine in Act 2 than in Act 1; and RunState's shrine membership
// bitset maps bit i to position i of the act's list
// (event_framework.hpp:198-199), a mapping derived from Exordium's order.
//
// This case exists so the divergence cannot be re-forgotten: it fails if
// anyone "tidies" the registry into the Act-2/3 order, and it is the evidence
// S2.13 needs before it reuses the Act-1 bit<->position mapping for Acts 2-3.
TEST(ActEventLists, ShrineListOrderDivergesBetweenActOneAndActsTwoThree) {
    // Same membership, both directions.
    for (std::string_view key : kCityBeyondShrineList) {
        EXPECT_NE(std::find(kExordiumShrineList.begin(),
                            kExordiumShrineList.end(), key),
                  kExordiumShrineList.end())
            << key << " is in the Act-2/3 list but not Exordium's";
    }
    for (std::string_view key : kExordiumShrineList) {
        EXPECT_NE(std::find(kCityBeyondShrineList.begin(),
                            kCityBeyondShrineList.end(), key),
                  kCityBeyondShrineList.end())
            << key << " is in Exordium's list but not the Act-2/3 one";
    }

    // Different ORDER -- asserted positively, key by key, not just as "!=".
    EXPECT_NE(kExordiumShrineList, kCityBeyondShrineList);
    EXPECT_EQ(kExordiumShrineList[5], "Wheel of Change");
    EXPECT_EQ(kCityBeyondShrineList[1], "Wheel of Change");
    EXPECT_EQ(kExordiumShrineList[1], "Golden Shrine");
    EXPECT_EQ(kCityBeyondShrineList[2], "Golden Shrine");

    // The registry's id order is EXORDIUM's, and stays Exordium's: ids are
    // append-only identity and are never renumbered to chase a runtime order.
    for (std::size_t i = 0; i < kExordiumShrineList.size(); ++i) {
        const r::EventId id = r::event_from_game_id(kExordiumShrineList[i]);
        EXPECT_EQ(static_cast<int>(id), 12 + static_cast<int>(i))
            << kExordiumShrineList[i];
    }
    // ... which means the registry order is NOT the Act-2/3 draw order. Spelled
    // out so the failure message names the actual hazard.
    const r::EventId first_registry_after_head =
        r::event_from_game_id(kExordiumShrineList[1]);
    const r::EventId first_city_after_head =
        r::event_from_game_id(kCityBeyondShrineList[1]);
    EXPECT_NE(first_registry_after_head, first_city_after_head)
        << "registry position 1 is Golden Shrine, Act-2/3 position 1 is Wheel "
           "of Change -- a bit<->position mapping cannot be shared";
}

// --- 4. The one-time specials: act gate vs list membership --------------------

TEST(ActEventLists, SpecialOneTimeRowsCarryTheirGetShrineActGate) {
    ASSERT_EQ(kSpecialActGates.size(), 14u);
    for (const SpecialActGate& g : kSpecialActGates) {
        const r::EventDef* def = r::event_def(g.id);
        ASSERT_NE(def, nullptr) << static_cast<int>(g.id);
        EXPECT_EQ(def->pool, r::EventPool::SPECIAL) << g.why;
        EXPECT_EQ(def->act_mask, g.act_mask)
            << r::event_game_id(g.id) << ": " << g.why;
        for (int a = 1; a <= 3; ++a) {
            EXPECT_EQ(r::event_in_act(g.id, a),
                      (g.act_mask & (1u << (a - 1))) != 0u)
                << r::event_game_id(g.id) << " act " << a;
        }
    }
    // Exactly fourteen SPECIAL rows -- the list is built once
    // (AbstractDungeon.java:1340-1358, only call site Exordium.java:54) and
    // carried by reference (CardCrawlGame.java:1102-1119); no act adds to it.
    int specials = 0;
    for (const r::EventDef& row : r::kEventTable) {
        if (row.pool == r::EventPool::SPECIAL) ++specials;
    }
    EXPECT_EQ(specials, 14);
}

// The six rows B4.13 left unimplemented because no Act-1 draw can reach them
// are exactly the rows whose act mask excludes Act 1. That equivalence is the
// reason the S1 registry could leave them bodiless, so it is worth pinning
// rather than re-deriving.
TEST(ActEventLists, ActOneUnreachableSpecialsAreExactlyTheUnimplementedOnes) {
    for (const SpecialActGate& g : kSpecialActGates) {
        const r::EventDef* def = r::event_def(g.id);
        ASSERT_NE(def, nullptr);
        const bool act_one = (g.act_mask & r::kEventActMaskExordium) != 0u;
        EXPECT_EQ(def->implemented, act_one)
            << r::event_game_id(g.id) << ": " << g.why;
    }
}

// --- 5. event_in_act edge cases ----------------------------------------------

TEST(ActEventLists, EventInActRejectsUnknownIdsAndOutOfRangeActs) {
    EXPECT_FALSE(r::event_in_act(r::EventId::NONE, 1));
    EXPECT_FALSE(r::event_in_act(static_cast<r::EventId>(9999), 2));
    for (int act : {-1, 0, 4, 5}) {
        EXPECT_FALSE(r::event_in_act(r::EventId::ADDICT, act)) << act;
        EXPECT_FALSE(r::event_in_act(r::EventId::LAB, act)) << act;
    }
}

// Every row in the domain answers the pool/act question -- no row slipped in
// with a default-constructed mask.
TEST(ActEventLists, EveryRowDeclaresAPoolAndAtLeastOneAct) {
    ASSERT_EQ(r::kEventTable.size(), r::manifest::kEventsCount);
    for (const r::EventDef& row : r::kEventTable) {
        EXPECT_NE(row.pool, r::EventPool::NONE) << static_cast<int>(row.id);
        EXPECT_NE(row.act_mask, 0u) << static_cast<int>(row.id);
        EXPECT_EQ(row.act_mask & ~0x7u, 0u) << static_cast<int>(row.id);
    }
}

// --- 6. The generator refuses a malformed conditions block --------------------

TEST(ActEventLists, GeneratorRejectsMissingConditions) {
    expect_conditions_rejected("s2_cond_missing", "",
                               "needs non-empty conditions metadata");
}

TEST(ActEventLists, GeneratorRejectsUnknownPool) {
    expect_conditions_rejected(
        "s2_cond_pool",
        "  conditions: {pool: TREASURE, acts: [2], draw: ALWAYS}",
        "unknown conditions.pool");
}

TEST(ActEventLists, GeneratorRejectsEmptyActs) {
    expect_conditions_rejected(
        "s2_cond_acts_empty",
        "  conditions: {pool: EVENT, acts: [], draw: ALWAYS}",
        "non-empty conditions.acts");
}

TEST(ActEventLists, GeneratorRejectsOutOfRangeAct) {
    // Act 4 is TheEnding, which is out of S2 scope entirely (s2-design §1): a
    // row claiming it is a scope error, not a typo to be tolerated.
    expect_conditions_rejected(
        "s2_cond_acts_range",
        "  conditions: {pool: EVENT, acts: [4], draw: ALWAYS}",
        "must hold only act numbers");
}

TEST(ActEventLists, GeneratorRejectsUnsortedOrRepeatedActs) {
    expect_conditions_rejected(
        "s2_cond_acts_order",
        "  conditions: {pool: EVENT, acts: [3, 2], draw: ALWAYS}",
        "strictly ascending");
}

TEST(ActEventLists, GeneratorRejectsMissingDraw) {
    expect_conditions_rejected(
        "s2_cond_draw",
        "  conditions: {pool: EVENT, acts: [2]}",
        "conditions.draw gate string");
}
