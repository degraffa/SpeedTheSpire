// Ascension (A20) modifier table -- design doc B §6.
//
// Two halves, and the second one is the point of the file.
//
// POSITIVE ROWS. One named test per §6 row that S1 implements, hand-computed
// from the Java cited in registry/a20.yaml: the elite map quota, the run-setup
// max-HP loss, the 90 %-of-max current HP, the starting curse, and the potion
// slot. The run-setup rows are also checked as an ORDER, because three of them
// land inside one method and two of them compose: taking 90 % of the max HP
// before rather than after the max-HP loss gives 72/75 instead of 68/75, and
// both are "the modifiers were applied".
//
// NEGATIVE FREEZES. Design §6 carries a list of ascension modifiers that DO NOT
// EXIST -- campfire heal, potion-drop chance, normal/elite combat gold, card
// reward rarity -- plus two levels that are no-ops in S1 (the upgraded-card
// chance, pinned to 0 by Exordium, and the double boss, gated to the final act).
// Those are frozen precisely because a later reader skimming ascensionLevel
// branches is likely to "find" one and helpfully add it. A negative is harder to
// test than a positive: there is no code to exercise, because the whole claim is
// that no code should exist. So the freeze is enforced in the two places where
// adding one WOULD leave a mark:
//
//   1. the run-setup sweep -- run_begin over every ascension 0..20, asserting
//      that the ONLY fields that move are the four the table says move. Any new
//      run-setup modifier lands in run_begin and trips this.
//   2. the registry -- registry/a20.yaml is the rules-as-data home of this
//      table, so a new modifier is a new row. The manifest tests below pin the
//      row set to exactly one row per ascension level, and pin each written
//      `no-such-modifier:` statement by name. Adding a modifier means deleting a
//      line that says it does not exist, which is a reviewable act rather than
//      an accident.

#include "sts/engine/run_advance.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/combat_rewards.hpp"  // card_upgraded_chance (the A12 arm)
#include "sts/engine/map_rooms.hpp"   // compute_room_quota (the A1 elite quota)
#include "sts/engine/potions.hpp"     // potion_slot_count
#include "sts/engine/relics.hpp"      // RelicId
#include "sts/engine/run_deck.hpp"    // add_card_to_master_deck (the obtain door)
#include "sts/engine/run_state.hpp"
#include "sts/registry/manifest.hpp"  // kA20Count

namespace sts::engine {
namespace {

constexpr int64_t kSeed = 12345;
constexpr int kMaxAscension = 20;

// The Ironclad starting deck as the master deck must hold it, curse aside.
const std::vector<CardId> kStarterDeck = {
    CardId::STRIKE, CardId::STRIKE, CardId::STRIKE, CardId::STRIKE, CardId::STRIKE,
    CardId::DEFEND, CardId::DEFEND, CardId::DEFEND, CardId::DEFEND,
    CardId::BASH,
};

std::vector<CardId> master_deck_of(const RunController& rc) {
    std::vector<CardId> out;
    for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
        out.push_back(static_cast<CardId>(rc.run.master_deck[i].card_id));
    }
    return out;
}

// =============================================================================
// Positive rows
// =============================================================================

// §6 row: "A1 | elite map-count x1.6 | AbstractDungeon.java:571-573". The branch
// multiplies the elite quota only; every other quota is untouched, and the
// multiply happens INSIDE Math.round, not after it.
TEST(A20RunSetup, A1MultipliesOnlyTheEliteRoomQuota) {
    // Act-1 chances (Exordium.initializeLevelSpecificChances, Exordium.java:
    // 95-99): shop .05, rest .12, treasure 0, event .22, elite .08. Expected
    // counts hand-derived offline in float32 with Java's Math.round(float) ==
    // floor(v + 0.5), for four plausible availableRoomCount values.
    struct Case { int count; int elite_base; int elite_asc; };
    for (const Case c : {Case{30, 2, 4}, Case{40, 3, 5}, Case{47, 4, 6},
                         Case{53, 4, 7}}) {
        const RoomQuota base = compute_room_quota(c.count, 0);
        const RoomQuota asc = compute_room_quota(c.count, 1);
        EXPECT_EQ(base.elite, c.elite_base) << "count " << c.count;
        EXPECT_EQ(asc.elite, c.elite_asc) << "count " << c.count;
        // Everything else is unbranched.
        EXPECT_EQ(asc.shop, base.shop) << "count " << c.count;
        EXPECT_EQ(asc.rest, base.rest) << "count " << c.count;
        EXPECT_EQ(asc.treasure, base.treasure) << "count " << c.count;
        EXPECT_EQ(asc.event, base.event) << "count " << c.count;
    }
    // The threshold is >= 1, so ascension 1 already pays it and 20 pays no more.
    EXPECT_EQ(compute_room_quota(47, 1).elite, compute_room_quota(47, 20).elite);
}

// §6 row: "A5 | between-act heal = 75 % of missing HP | AbstractDungeon.java:
// 2582-2586". At RUN START the sheet arrives full (80/80, Ironclad.java:114), so
// missing HP is 0 and the else-branch heal(maxHealth) is equally saturated: both
// branches are no-ops. This test states that -- crossing the threshold at run
// setup must change nothing at all.
TEST(A20RunSetup, A5BetweenActHealIsANoOpAtRunStart) {
    for (uint8_t asc = 0; asc <= 5; ++asc) {
        const RunController rc = run_begin(kSeed, asc);
        EXPECT_EQ(rc.run.hp, kIroncladBaseMaxHp) << "ascension " << int{asc};
        EXPECT_EQ(rc.run.max_hp, kIroncladBaseMaxHp) << "ascension " << int{asc};
    }
}

// §6 row: "A6 | start act at 90 % HP | AbstractDungeon.java:2594-2596". Below the
// threshold the run starts at full; from 6 up, current HP is round(max * 0.9f).
TEST(A20RunSetup, A6StartsTheRunAtNinetyPercentOfMaxHp) {
    EXPECT_EQ(run_begin(kSeed, 5).run.hp, 80);
    EXPECT_EQ(run_begin(kSeed, 5).run.max_hp, 80);

    const RunController rc = run_begin(kSeed, 6);
    EXPECT_EQ(rc.run.max_hp, 80);
    EXPECT_EQ(rc.run.hp, 72);  // round(80 * 0.9f) == round(72.0f)
    EXPECT_EQ(run_setup_hp(6), 72);
}

// THE ORDER TEST. The max-HP loss (AbstractDungeon.java:2591-2593) and the 90 %
// rewrite (:2594-2596) are consecutive statements in one method, in THAT order,
// so from ascension 14 the 90 % is taken of the already-reduced max. Getting the
// order backwards still "applies both modifiers" and still lands on a plausible
// 72/75 -- which is exactly why this needs its own named test rather than being
// folded into the two rows above.
TEST(A20RunSetup, A6RunsAfterA14SoNinetyPercentIsOfTheReducedMax) {
    const RunController rc = run_begin(kSeed, 14);
    EXPECT_EQ(rc.run.max_hp, 75);
    EXPECT_EQ(rc.run.hp, 68);  // round(75 * 0.9f) == round(67.5f) == 68
    EXPECT_NE(rc.run.hp, 72) << "the 90 % was taken of the UNREDUCED max HP";

    // MathUtils.round (MathUtils.java:233-235) is a biased floor of (v + 0.5), so
    // the exact .5 that 90 % of 75 lands on rounds UP. std::round agrees here;
    // truncation would not, and would silently give 67.
    EXPECT_EQ(mathutils_round(67.5f), 68);
    EXPECT_EQ(mathutils_round(72.0f), 72);
    EXPECT_EQ(run_setup_hp(14), 68);
    EXPECT_EQ(run_setup_max_hp(14), 75);
}

// §6 row: "A14 | -5 max HP (Ironclad) | AbstractDungeon.java:2591-2593 +
// Ironclad.java:168-170". The amount is the character's own
// getAscensionMaxHPLoss, not a constant 5 baked into the dungeon.
TEST(A20RunSetup, A14RemovesTheCharactersOwnMaxHpLossFromFourteen) {
    EXPECT_EQ(kIroncladAscensionMaxHpLoss, 5);
    EXPECT_EQ(run_begin(kSeed, 13).run.max_hp, 80);
    EXPECT_EQ(run_begin(kSeed, 14).run.max_hp, 75);
    EXPECT_EQ(run_begin(kSeed, 20).run.max_hp, 75);
    EXPECT_EQ(run_setup_max_hp(13), 80);
    EXPECT_EQ(run_setup_max_hp(14), 80 - kIroncladAscensionMaxHpLoss);
}

// §6 row: "A10 | Ascender's Bane added to deck | AbstractDungeon.java:2597-2600".
// POSITION is the interesting half. AbstractDungeon.<init> calls
// dungeonTransitionSetup (:287) and only then initializeStarterDeck (:295-296),
// so the curse is appended to an EMPTY master deck and the starter cards land
// behind it. addToTop is CardGroup.java:455-457, i.e. an ArrayList append.
TEST(A20RunSetup, A10PutsAscendersBaneAtMasterDeckIndexZero) {
    const RunController below = run_begin(kSeed, 9);
    EXPECT_EQ(below.run.master_deck_count, 10);
    EXPECT_EQ(master_deck_of(below), kStarterDeck);

    const RunController rc = run_begin(kSeed, 10);
    ASSERT_EQ(rc.run.master_deck_count, 11);
    std::vector<CardId> expected = {CardId::ASCENDERS_BANE};
    expected.insert(expected.end(), kStarterDeck.begin(), kStarterDeck.end());
    EXPECT_EQ(master_deck_of(rc), expected)
        << "the curse goes in before the starting deck, so it is index 0";
    EXPECT_EQ(rc.run.master_deck[0].upgrade, 0);
    EXPECT_TRUE(run_setup_has_starting_curse(10));
    EXPECT_FALSE(run_setup_has_starting_curse(9));
}

// The curse walks through add_card_to_master_deck, the sanctioned master-deck
// door, which fires every owned relic's onObtainCard. The game's own call site is
// a raw CardGroup insert that reaches no such hook, so the two agree only while
// the obtain pass is empty at run setup. That is true today for two independent
// reasons -- nothing is equipped yet when run_begin adds the curse, and the
// starting relic has no obtain binding -- and this test pins BOTH, so the day one
// of them stops holding is the day the test goes red rather than the day a run
// silently gains gold.
TEST(A20RunSetup, A10CurseGoesThroughTheDoorButTheObtainPassChangesNothing) {
    const RunController rc = run_begin(kSeed, 20);
    EXPECT_EQ(rc.run.gold, kIroncladBaseGold);   // Ceramic Fish would be +9
    EXPECT_EQ(rc.run.max_hp, 75);                // Darkstone Periapt would be +6
    EXPECT_EQ(rc.run.master_deck[0].upgrade, 0); // an Egg relic would upgrade it

    // ...and directly: the door over a run holding exactly the starting relic.
    RunState rs{};
    rs.gold = kIroncladBaseGold;
    rs.hp = static_cast<int16_t>(75);
    rs.max_hp = static_cast<int16_t>(75);
    rs.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::BURNING_BLOOD), 0};
    rs.relic_count = 1;
    ASSERT_TRUE(add_card_to_master_deck(rs, CardId::ASCENDERS_BANE));
    EXPECT_EQ(rs.master_deck_count, 1);
    EXPECT_EQ(rs.gold, kIroncladBaseGold);
    EXPECT_EQ(rs.max_hp, 75);
    EXPECT_EQ(rs.master_deck[0].upgrade, 0);
}

// §6 row: "A11 | one fewer potion slot | AbstractPlayer.java:211-213", over the
// field default potionSlots = 3 (AbstractPlayer.java:144). Applied in the PLAYER
// constructor, so it is the earliest run-setup modifier of the set.
TEST(A20RunSetup, A11RemovesOnePotionSlotFromEleven) {
    EXPECT_EQ(potion_slot_count(10), 3);
    EXPECT_EQ(potion_slot_count(11), 2);
    EXPECT_EQ(run_begin(kSeed, 10).run.potion_slots, 3);
    EXPECT_EQ(run_begin(kSeed, 11).run.potion_slots, 2);
    EXPECT_EQ(run_begin(kSeed, 20).run.potion_slots, 2);
}

// The whole run-setup block against a REAL live-game capture: the committed G4
// oracle sample (tests/golden/oracle_corpus/skeleton_sample.jsonl, campaign
// b13_on20b, IRONCLAD, ascension 20, seed 1790050543751) reports max_hp 75 and
// current_hp 68 on its floor-0 and floor-1 states. The sim must land there from
// the Java alone.
TEST(A20RunSetup, AscensionTwentySheetMatchesTheLiveOracleCapture) {
    const RunController rc = run_begin(1790050543751LL, 20);
    EXPECT_EQ(rc.run.max_hp, 75);
    EXPECT_EQ(rc.run.hp, 68);
    EXPECT_EQ(rc.run.gold, 99);
    EXPECT_EQ(rc.run.potion_slots, 2);
    EXPECT_EQ(rc.run.master_deck_count, 11);
    EXPECT_EQ(static_cast<CardId>(rc.run.master_deck[0].card_id),
              CardId::ASCENDERS_BANE);
}

// =============================================================================
// Negative freeze 1 -- the run-setup sweep
// =============================================================================

// THE SWEEP. Design §6's run-setup rows are exhaustive: across ascension 0..20
// the ONLY things that may change at run start are max HP (from 14), current HP
// (from 6, and again from 14 through the reduced max), the potion-slot count
// (from 11) and the master deck (from 10). Everything else on the sheet is
// ascension-invariant.
//
// This is the freeze for the §6 negatives that have no S1 mechanism of their own
// -- there is no campfire, no reward assembly and no shop to test, so the place a
// wrongly-added modifier would first show up is here, in the state run_begin
// hands out. It is deliberately written as "nothing else moved" rather than as a
// list of the things that do.
TEST(A20Negative, RunSetupChangesNothingBeyondTheFourFrozenModifiers) {
    const RunController base = run_begin(kSeed, 0);
    for (int a = 0; a <= kMaxAscension; ++a) {
        const uint8_t asc = static_cast<uint8_t>(a);
        const RunController rc = run_begin(kSeed, asc);
        const std::string at = " at ascension " + std::to_string(a);

        // The four that are allowed to move, at their exact thresholds.
        EXPECT_EQ(rc.run.max_hp, a >= 14 ? 75 : 80) << at;
        EXPECT_EQ(rc.run.hp, run_setup_hp(a)) << at;
        EXPECT_EQ(rc.run.potion_slots, a >= 11 ? 2 : 3) << at;
        EXPECT_EQ(rc.run.master_deck_count, a >= 10 ? 11 : 10) << at;

        // Everything else is invariant.
        EXPECT_EQ(rc.run.gold, base.run.gold) << "gold moved" << at;
        EXPECT_EQ(rc.run.act, base.run.act) << at;
        EXPECT_EQ(rc.run.floor, base.run.floor) << at;
        EXPECT_EQ(rc.phase, base.phase) << at;
        // No ascension scaling on the potion-drop ratchet or the card-rarity
        // randomizer, and none on the event pity floats.
        EXPECT_EQ(rc.run.blizzard_potion_mod, base.run.blizzard_potion_mod) << at;
        EXPECT_EQ(rc.run.card_blizz_randomizer, base.run.card_blizz_randomizer) << at;
        EXPECT_EQ(rc.run.event_pity_monster, base.run.event_pity_monster) << at;
        EXPECT_EQ(rc.run.event_pity_shop, base.run.event_pity_shop) << at;
        EXPECT_EQ(rc.run.event_pity_treasure, base.run.event_pity_treasure) << at;
        // The starting relic loadout does not scale either.
        ASSERT_EQ(rc.run.relic_count, 1) << at;
        EXPECT_EQ(rc.run.relics[0].relic_id,
                  static_cast<uint16_t>(RelicId::BURNING_BLOOD)) << at;
        // ...and the starter cards themselves are untouched: only the curse is
        // ever added, never a substitution or an upgrade.
        std::vector<CardId> deck = master_deck_of(rc);
        if (a >= 10) {
            ASSERT_FALSE(deck.empty()) << at;
            EXPECT_EQ(deck.front(), CardId::ASCENDERS_BANE) << at;
            deck.erase(deck.begin());
        }
        EXPECT_EQ(deck, kStarterDeck) << at;
        for (uint16_t i = 0; i < rc.run.master_deck_count; ++i) {
            EXPECT_EQ(rc.run.master_deck[i].upgrade, 0) << at;
        }
    }
}

// =============================================================================
// Negative freeze 2 + manifest -- registry/a20.yaml
// =============================================================================

struct A20Row {
    int id = -1;
    int level = -1;
    std::string scope;
    std::string mechanic;
    std::string provenance;
    std::string notes;
};

std::string a20_yaml_text() {
    const std::string path = std::string(STS_REGISTRY_DIR) + "/a20.yaml";
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "cannot open " << path;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

// A deliberately small line reader, not a YAML parser: every row in a20.yaml is
// `- id: <n>` followed by one-line `  <key>: <value>` pairs, and a test that
// pulled in a YAML library to read a file the codegen already validates would be
// checking the library.
std::vector<A20Row> parse_a20_rows(const std::string& text) {
    std::vector<A20Row> rows;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        if (t.rfind("- id:", 0) == 0) {
            A20Row row;
            row.id = std::stoi(trim(t.substr(5)));
            rows.push_back(row);
            continue;
        }
        if (rows.empty()) {
            continue;
        }
        const std::size_t colon = t.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = t.substr(0, colon);
        const std::string value = trim(t.substr(colon + 1));
        A20Row& row = rows.back();
        if (key == "level") {
            row.level = std::stoi(value);
        } else if (key == "scope") {
            row.scope = value;
        } else if (key == "mechanic") {
            row.mechanic = value;
        } else if (key == "provenance") {
            row.provenance = value;
        } else if (key == "notes") {
            row.notes = value;
        }
    }
    return rows;
}

// MANIFEST COMPLETENESS. Every §6 table row maps onto at least one ascension
// level, and the file carries exactly one row per level 1..20 -- so a §6 row that
// was never transcribed is a missing level, and a modifier somebody invented is
// an extra row. Both fail here.
TEST(A20Manifest, ExactlyOneRowPerAscensionLevelWithIdEqualToLevel) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    ASSERT_EQ(rows.size(), static_cast<std::size_t>(kMaxAscension));
    std::vector<bool> seen(static_cast<std::size_t>(kMaxAscension) + 1, false);
    for (const A20Row& r : rows) {
        EXPECT_EQ(r.id, r.level) << "id must equal level (row id " << r.id << ")";
        ASSERT_GE(r.level, 1);
        ASSERT_LE(r.level, kMaxAscension);
        EXPECT_FALSE(seen[static_cast<std::size_t>(r.level)])
            << "duplicate row for ascension " << r.level;
        seen[static_cast<std::size_t>(r.level)] = true;
    }
    for (int a = 1; a <= kMaxAscension; ++a) {
        EXPECT_TRUE(seen[static_cast<std::size_t>(a)])
            << "no registry row for ascension " << a;
    }
    // The generated manifest agrees with the file the test just read.
    EXPECT_EQ(sts::registry::manifest::kA20Count,
              static_cast<std::size_t>(kMaxAscension));
}

// Every row carries the frozen entry shape: a scope from the §6 vocabulary, a
// mechanic, a provenance citation into the decompiled Java, and a notes field
// whose first word is the row's S1 STATUS. "Implemented, or N/A-for-S1 with its
// reason recorded" is the acceptance bar, so it is machine-checked rather than
// eyeballed.
TEST(A20Manifest, EveryRowCarriesScopeProvenanceAndAnS1Status) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    ASSERT_FALSE(rows.empty());
    for (const A20Row& r : rows) {
        const std::string at = "ascension row " + std::to_string(r.id);
        EXPECT_TRUE(r.scope == "run-setup" || r.scope == "monster" ||
                    r.scope == "economy" || r.scope == "event")
            << at << " has scope '" << r.scope << "' outside the §6 vocabulary";
        EXPECT_FALSE(r.mechanic.empty()) << at;
        EXPECT_NE(r.provenance.find(".java:"), std::string::npos)
            << at << " provenance must cite File.java:line";
        const bool implemented = r.notes.rfind("IMPLEMENTED", 0) == 0;
        const bool na = r.notes.rfind("N/A-FOR-S1", 0) == 0;
        EXPECT_TRUE(implemented || na)
            << at << " notes must open with IMPLEMENTED or N/A-FOR-S1, got: "
            << r.notes.substr(0, 40);
        if (na) {
            // "with reason recorded": the marker alone is not enough.
            EXPECT_GT(r.notes.size(), std::size_t{40}) << at << " gives no reason";
        }
    }
}

// THE WRITTEN NEGATIVES. Design §6: "no ascension scaling exists on campfire
// heal, potion-drop chance, normal/elite combat gold, or card-reward rarity".
// Each is recorded in a20.yaml as a `no-such-modifier:` statement with the line
// that was read to establish it. This test fails if one is deleted -- which is
// what adding the modifier would require -- so the frozen negative cannot be
// quietly reversed.
TEST(A20Negative, TheFrozenNoSuchModifierStatementsAreStillInTheRegistry) {
    const std::string text = a20_yaml_text();
    for (const char* marker : {
             "no-such-modifier: campfire heal",
             "no-such-modifier: potion-drop chance",
             "no-such-modifier: normal-combat gold",
             "no-such-modifier: elite-combat gold",
             "no-such-modifier: card-reward RARITY",
         }) {
        EXPECT_NE(text.find(marker), std::string::npos)
            << "the frozen negative '" << marker << "' was removed from a20.yaml";
    }
    // Their citations must survive with them: a marker with no line to re-read
    // is not a freeze.
    for (const char* cite : {"RestOption.java:25", "AbstractRoom.java:580-607",
                             "AbstractRoom.java:324", "AbstractRoom.java:316",
                             "AbstractDungeon.java:1597-1603"}) {
        EXPECT_NE(text.find(cite), std::string::npos)
            << "negative-result citation " << cite << " was removed";
    }
}

// §6: "A12 (upgraded-card chance) is a no-op in Act 1 (Exordium pins
// cardUpgradedChance = 0.0f, Exordium.java:107)". The row's status flipped to
// IMPLEMENTED when its engine owner landed (S2.12's (act, ascension) keying,
// observable end-to-end since S2.24), but the Act-1 frozen negative it records
// must survive the flip -- the row-20 discipline. Behaviourally, the Act-1 arm
// still returns 0.0f at every ascension.
TEST(A20Negative, A12UpgradedCardChanceStaysANoOpInActOne) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [](const A20Row& r) { return r.level == 12; });
    ASSERT_NE(it, rows.end());
    EXPECT_EQ(it->notes.rfind("IMPLEMENTED", 0), std::size_t{0}) << it->notes;
    EXPECT_NE(it->provenance.find("Exordium.java:107"), std::string::npos);
    EXPECT_NE(it->notes.find("no-such-modifier"), std::string::npos)
        << "the S1 frozen negative must survive the status flip";
    EXPECT_FLOAT_EQ(card_upgraded_chance(1, 12), 0.0f);
    EXPECT_FLOAT_EQ(card_upgraded_chance(1, 20), 0.0f);
}

// §6: "A20's double boss is Act-3-only (ProceedButton.java:102,
// AbstractMonster.java:1059)". The level's only effect is gated behind an
// Act-3 dungeon id, so an Act-1 run at ascension 20 gets nothing from level 20
// beyond the union of 1..19 -- a fact S2.28's IMPLEMENTED route did not move
// (the row's status changed; the Act-1 negative it records must not). The
// sweep above proves the run-setup half of that; this pins the written reason.
TEST(A20Negative, LevelTwentyAddsNothingToAnActOneRun) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [](const A20Row& r) { return r.level == 20; });
    ASSERT_NE(it, rows.end());
    EXPECT_EQ(it->notes.rfind("IMPLEMENTED", 0), std::size_t{0}) << it->notes;
    EXPECT_NE(it->notes.find("A20 still adds NOTHING to an Act-1 run"),
              std::string::npos)
        << "the S1 frozen negative must survive the status flip";
    EXPECT_NE(it->provenance.find("ProceedButton.java:100-104"), std::string::npos);
    EXPECT_NE(it->provenance.find("AbstractMonster.java:1058-1060"),
              std::string::npos);

    // Behaviourally: ascension 19 and 20 produce the same run-setup sheet.
    const RunController a19 = run_begin(kSeed, 19);
    const RunController a20 = run_begin(kSeed, 20);
    EXPECT_EQ(a19.run.hp, a20.run.hp);
    EXPECT_EQ(a19.run.max_hp, a20.run.max_hp);
    EXPECT_EQ(a19.run.potion_slots, a20.run.potion_slots);
    EXPECT_EQ(a19.run.master_deck_count, a20.run.master_deck_count);
}

// =============================================================================
// S2.04 -- S2 status refresh (status text + provenance only, no engine change)
// =============================================================================
//
// s2-tasks.md S2.04: rows A5, A6, A12, A13, A20 gain S2-scoped facts and
// explicit engine-work ownership; a row's STATUS moves only when the owning
// task lands ("rows keep S1 status until their owner lands"). Row 20's owner
// LANDED (S2.28's double-boss route), so its prefix is IMPLEMENTED now; row
// 13 flipped to IMPLEMENTED when S2.24 landed its reward-assembly share (the
// Act-3 draw-and-discard half was S2.28's; the pin moved with the row, the
// same discipline as row 20's flip). Rows 5 and 12 flipped last, at the
// S2-G1 gate sweep (2026-08-09): their owner S2.12 had landed the engine
// share on 2026-08-07 but deferred each row's one-line status edit to a
// successor that never carried it. These tests pin the citations by
// substring, the same discipline the frozen no-such-modifier block already
// uses.
TEST(A20S204, AffectedRowsKeepTheirS1StatusPrefix) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    auto row = [&](int level) -> const A20Row& {
        auto it = std::find_if(rows.begin(), rows.end(),
                                [&](const A20Row& r) { return r.level == level; });
        return *it;
    };
    EXPECT_EQ(row(5).notes.rfind("IMPLEMENTED", 0), std::size_t{0})
        << "S2.12 landed the between-act heal; the gate sweep flipped the row";
    EXPECT_EQ(row(6).notes.rfind("IMPLEMENTED", 0), std::size_t{0});
    EXPECT_EQ(row(12).notes.rfind("IMPLEMENTED", 0), std::size_t{0})
        << "S2.12 keyed the chance per act; the gate sweep flipped the row";
    EXPECT_EQ(row(13).notes.rfind("IMPLEMENTED", 0), std::size_t{0})
        << "S2.24 landed the claimable Act-2 share, closing the row";
    EXPECT_EQ(row(20).notes.rfind("IMPLEMENTED", 0), std::size_t{0})
        << "S2.28 landed the double-boss route";
}

// A5: the between-act heal (AbstractDungeon.java:2582-2586, the same line
// already cited for the run-start no-op) becomes live once Acts 2-3 exist,
// with the engine work owned by S2.12.
TEST(A20S204, A5RecordsTheBetweenActHealBecomesLiveUnderS2) {
    const std::string text = a20_yaml_text();
    const std::vector<A20Row> rows = parse_a20_rows(text);
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [](const A20Row& r) { return r.level == 5; });
    ASSERT_NE(it, rows.end());
    EXPECT_NE(it->notes.find("S2 note (S2.04)"), std::string::npos);
    EXPECT_NE(it->notes.find("owned by S2.12"), std::string::npos);
    EXPECT_NE(it->notes.find("between-act heal becomes live"), std::string::npos);
}

// A6: the run-start guard (AbstractDungeon.java:2590) is one level above the
// A6/A10/A14 ascensionLevel checks, so those three never recur at an act
// transition. Design s2-design.md §4.2 states this explicitly; this pins the
// citation landed in the registry row.
TEST(A20S204, A6RecordsTheRunStartOnlyGuardCitation) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [](const A20Row& r) { return r.level == 6; });
    ASSERT_NE(it, rows.end());
    EXPECT_NE(it->notes.find("AbstractDungeon.java:2590"), std::string::npos);
    EXPECT_NE(it->notes.find("verified run-start-only"), std::string::npos);
}

// A12: the per-act halving values (TheCity.java:84, TheBeyond.java:81) match
// the decompiled source exactly -- 0.25/0.125 in Act 2, 0.5/0.25 in Act 3.
TEST(A20S204, A12RecordsThePerActHalvingCitations) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [](const A20Row& r) { return r.level == 12; });
    ASSERT_NE(it, rows.end());
    EXPECT_NE(it->provenance.find("TheCity.java:84"), std::string::npos);
    EXPECT_NE(it->provenance.find("TheBeyond.java:81"), std::string::npos);
    EXPECT_NE(it->notes.find("0.25->0.125"), std::string::npos);
    EXPECT_NE(it->notes.find("0.5->0.25"), std::string::npos);
}

// A13: two independent S2 facts -- the boss-gold branch fires per
// MonsterRoomBoss instance (so the A20 double boss draws it twice), and Mind
// Bloom's own boss re-fight has a SEPARATE fixed 25/50 branch on the same
// threshold with no miscRng draw (MindBloom.java:73-77).
TEST(A20S204, A13RecordsDoubleBossAndMindBloomCitations) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [](const A20Row& r) { return r.level == 13; });
    ASSERT_NE(it, rows.end());
    EXPECT_NE(it->provenance.find("MindBloom.java:73-77"), std::string::npos);
    EXPECT_NE(it->provenance.find("MonsterRoomBoss.java:27-36"), std::string::npos);
    // The Mind Bloom share flipped from "owned by S2.33" to LANDED when
    // S2.33 delivered the event body; the note must keep the citation trail.
    EXPECT_NE(it->notes.find("LANDED with S2.33"), std::string::npos);
    EXPECT_NE(it->notes.find("owned by S2.24"), std::string::npos);
    EXPECT_NE(it->notes.find("owned by S2.28"), std::string::npos);
}

// A20: the row flipped to IMPLEMENTED when S2.28 landed the route, and the
// note must keep the ownership HISTORY -- the route was originally S2.12's and
// was reassigned in the Wave-3 allocation note, a drift the ledger records
// explicitly so a later reader does not go looking for it in S2.12's Log.
TEST(A20S204, A20RecordsDoubleBossEngineOwnership) {
    const std::vector<A20Row> rows = parse_a20_rows(a20_yaml_text());
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [](const A20Row& r) { return r.level == 20; });
    ASSERT_NE(it, rows.end());
    EXPECT_NE(it->notes.find("originally assigned S2.12"), std::string::npos);
    EXPECT_NE(it->notes.find("REASSIGNED to S2.28"), std::string::npos);
    EXPECT_NE(it->notes.find("finish_combat_after_action"), std::string::npos)
        << "an IMPLEMENTED prefix names its site";
}

}  // namespace
}  // namespace sts::engine
