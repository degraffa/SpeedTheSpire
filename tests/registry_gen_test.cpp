// B2.1/B2.2 acceptance suite for the registry codegen (tools/registry_gen/gen.py):
//
//   1. Determinism   -- running the generator twice yields byte-identical output.
//   2. DuplicateId   -- a YAML entry with a reused id fails generation with a
//                        clear error and a non-zero exit.
//   3. Equivalence   -- the tables the engine re-exports (types.hpp enums,
//                        cards.hpp CardDef) are the generated ones, field-for-
//                        field and (post-migration) entity-for-entity: sts::engine
//                        aliases sts::registry, no hand copy exists.
//   4. GameIds       -- the game_id<->enum string tables round-trip.
//   5. Manifest      -- the row-count manifest matches the seeded content.
//   6. MonsterTable  -- (B2.2) the generated Jaw Worm stat/move table matches the
//                        hand-derived JawWorm.java ascension columns, including
//                        tier-threshold resolution at the branch boundaries; a
//                        duplicate move_id fails generation with a clear error.
//
// "Generated headers compile standalone" is proven by registry_gen_standalone.cpp
// (this TU additionally includes the engine headers to run the equivalence checks).
//
// The generator is invoked out-of-process via the same Python interpreter CMake
// found; paths arrive as compile definitions.

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

// Generated headers (build tree, via the registry_generated include dir).
#include "sts/registry/card_table.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/ids.hpp"
#include "sts/registry/manifest.hpp"
#include "sts/registry/monster_table.hpp"
#include "sts/registry/power_table.hpp"
#include "sts/registry/relic_table.hpp"

// Engine headers -- post-migration these re-export the generated tables; the
// equivalence tests double as the proof that the re-exports are the same
// entities (no dual system).
#include "sts/engine/cards.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/monster_jaw_worm.hpp"
#include "sts/engine/types.hpp"

namespace fs = std::filesystem;

namespace {

const char* kPython = STS_PYTHON_EXECUTABLE;
const char* kGenPy = STS_REGISTRY_GEN_PY;
const char* kRegistryDir = STS_REGISTRY_DIR;
const char* kScratchDir = STS_GEN_SCRATCH;

std::string quote(const std::string& s) { return "\"" + s + "\""; }

// Run the generator against `registry_dir`, writing headers under `out_dir` and
// stderr into `err_file`. Returns the process exit status.
int run_generator(const std::string& registry_dir, const std::string& out_dir,
                  const std::string& err_file) {
    const std::string cmd = quote(kPython) + " " + quote(kGenPy) +
                            " --registry " + quote(registry_dir) + " --out " +
                            quote(out_dir) + " 2> " + quote(err_file);
    return std::system(cmd.c_str());
}

std::vector<unsigned char> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

const std::array<const char*, 8> kGenFiles = {
    "sts/registry/ids.hpp", "sts/registry/card_table.hpp",
    "sts/registry/power_table.hpp", "sts/registry/relic_table.hpp",
    "sts/registry/potion_table.hpp",
    "sts/registry/monster_table.hpp",
    "sts/registry/game_ids.hpp", "sts/registry/manifest.hpp"};

}  // namespace

// --- 1. Determinism ---------------------------------------------------------
TEST(RegistryGen, DeterministicByteIdentical) {
    const fs::path scratch = fs::path(kScratchDir);
    fs::create_directories(scratch);
    const fs::path out_a = scratch / "det_a";
    const fs::path out_b = scratch / "det_b";
    fs::remove_all(out_a);
    fs::remove_all(out_b);

    const fs::path err = scratch / "det_err.txt";
    ASSERT_EQ(run_generator(kRegistryDir, out_a.string(), err.string()), 0)
        << read_text(err);
    ASSERT_EQ(run_generator(kRegistryDir, out_b.string(), err.string()), 0)
        << read_text(err);

    for (const char* rel : kGenFiles) {
        const auto a = read_bytes(out_a / rel);
        const auto b = read_bytes(out_b / rel);
        ASSERT_FALSE(a.empty()) << "missing generated file: " << rel;
        EXPECT_EQ(a, b) << "non-deterministic output for " << rel;
    }
}

// --- 2. Duplicate id is rejected with a clear error -------------------------
TEST(RegistryGen, DuplicateIdFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = scratch / "bad_registry";
    fs::remove_all(bad_reg);
    fs::create_directories(bad_reg);

    // Copy the real registry, then append a card reusing id 1 (STRIKE's id).
    for (const auto& e : fs::directory_iterator(kRegistryDir)) {
        if (e.path().extension() == ".yaml") {
            fs::copy_file(e.path(), bad_reg / e.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
    }
    {
        std::ofstream cards(bad_reg / "cards.yaml", std::ios::app);
        cards << "\n- id: 1\n  name: DUPLICATE_STRIKE\n  game_id: \"Dup\"\n"
                 "  type: ATTACK\n  cost: 1\n  target: ENEMY\n"
                 "  provenance: \"synthetic duplicate for the negative test\"\n"
                 "  effects:\n    - {op: DAMAGE, target: CARD_TARGET, amount: 6}\n";
    }

    const fs::path out = scratch / "bad_out";
    const fs::path err = scratch / "bad_err.txt";
    const int status = run_generator(bad_reg.string(), out.string(), err.string());
    EXPECT_NE(status, 0) << "generator should fail on a duplicate id";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("duplicate"), std::string::npos) << msg;
    EXPECT_NE(msg.find('1'), std::string::npos) << msg;  // the offending id
}

// B4.6 pool ordering is registry data, so invalid orders must fail at generation
// time instead of producing a sparse/ambiguous runtime pool.
TEST(RegistryGen, DuplicateRelicPoolOrderFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = scratch / "bad_pool_order_registry";
    fs::remove_all(bad_reg);
    fs::create_directories(bad_reg);

    for (const auto& e : fs::directory_iterator(kRegistryDir)) {
        if (e.path().extension() == ".yaml") {
            fs::copy_file(e.path(), bad_reg / e.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
    }
    {
        std::ofstream relics(bad_reg / "relics.yaml", std::ios::app);
        relics << "\n- id: 36\n  name: DUPLICATE_POOL_SLOT\n"
                  "  game_id: \"Duplicate Pool Slot\"\n  tier: COMMON\n"
                  "  pool_order: 0\n"
                  "  provenance: \"synthetic duplicate for the negative test\"\n";
    }

    const fs::path out = scratch / "bad_pool_order_out";
    const fs::path err = scratch / "bad_pool_order_err.txt";
    const int status = run_generator(bad_reg.string(), out.string(), err.string());
    EXPECT_NE(status, 0) << "generator should fail on duplicate pool_order";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("duplicate COMMON pool_order 0"), std::string::npos) << msg;
}

// --- 3. Generated enums match the engine's hand enums exactly ---------------
TEST(RegistryGen, EnumIdsMatchEngine) {
    using sts::engine::CardId;
    using sts::engine::MonsterId;
    using sts::engine::PowerId;
    namespace r = sts::registry;

    EXPECT_EQ(static_cast<int>(r::CardId::STRIKE), static_cast<int>(CardId::STRIKE));
    EXPECT_EQ(static_cast<int>(r::CardId::DEFEND), static_cast<int>(CardId::DEFEND));
    EXPECT_EQ(static_cast<int>(r::CardId::BASH), static_cast<int>(CardId::BASH));
    EXPECT_EQ(static_cast<int>(r::CardId::SHRUG_IT_OFF),
              static_cast<int>(CardId::SHRUG_IT_OFF));
    EXPECT_EQ(static_cast<int>(r::CardId::POMMEL_STRIKE),
              static_cast<int>(CardId::POMMEL_STRIKE));

    EXPECT_EQ(static_cast<int>(r::PowerId::STRENGTH), static_cast<int>(PowerId::STRENGTH));
    EXPECT_EQ(static_cast<int>(r::PowerId::VULNERABLE),
              static_cast<int>(PowerId::VULNERABLE));
    EXPECT_EQ(static_cast<int>(r::PowerId::WEAK), static_cast<int>(PowerId::WEAK));
    EXPECT_EQ(static_cast<uint16_t>(PowerId::CURL_UP), 20u);
    EXPECT_EQ(static_cast<uint16_t>(PowerId::FRAIL), 21u);

    EXPECT_EQ(static_cast<int>(r::MonsterId::JAW_WORM),
              static_cast<int>(MonsterId::JAW_WORM));
}

// --- 3b. Generated CardDef table matches cards.hpp field-for-field ----------
TEST(RegistryGen, CardTableMatchesEngine) {
    namespace r = sts::registry;

    // Identical step budget and layout size -> a raw-bytes swap is safe (B2.2).
    static_assert(r::kMaxCardSteps == sts::engine::kMaxCardSteps,
                  "kMaxCardSteps must match the engine's");
    static_assert(sizeof(r::CardDef) == sizeof(sts::engine::CardDef),
                  "generated CardDef layout must match the engine's");

    for (int id = 1; id <= 5; ++id) {
        const auto eid = static_cast<sts::engine::CardId>(id);
        const auto rid = static_cast<r::CardId>(id);
        const sts::engine::CardDef* e = sts::engine::card_def(eid);
        const r::CardDef* g = r::card_def(rid);
        ASSERT_NE(e, nullptr) << "engine card_def null for id " << id;
        ASSERT_NE(g, nullptr) << "generated card_def null for id " << id;

        EXPECT_EQ(static_cast<int>(g->id), static_cast<int>(e->id)) << "id " << id;
        EXPECT_EQ(g->base_cost, e->base_cost) << "cost, id " << id;
        EXPECT_EQ(static_cast<int>(g->type), static_cast<int>(e->type))
            << "type, id " << id;
        EXPECT_EQ(g->needs_target, e->needs_target) << "needs_target, id " << id;
        EXPECT_EQ(g->random_target, e->random_target) << "random_target, id " << id;
        EXPECT_EQ(g->step_count, e->step_count) << "step_count, id " << id;

        for (int s = 0; s < sts::engine::kMaxCardSteps; ++s) {
            EXPECT_EQ(static_cast<int>(g->steps[static_cast<std::size_t>(s)].op),
                      static_cast<int>(e->steps[static_cast<std::size_t>(s)].op))
                << "op, id " << id << " step " << s;
            EXPECT_EQ(g->steps[static_cast<std::size_t>(s)].amount,
                      e->steps[static_cast<std::size_t>(s)].amount)
                << "amount, id " << id << " step " << s;
            EXPECT_EQ(g->steps[static_cast<std::size_t>(s)].extra,
                      e->steps[static_cast<std::size_t>(s)].extra)
                << "extra, id " << id << " step " << s;
            EXPECT_EQ(static_cast<int>(g->steps[static_cast<std::size_t>(s)].target),
                      static_cast<int>(e->steps[static_cast<std::size_t>(s)].target))
                << "target, id " << id << " step " << s;
        }
    }

    // The APPLY_POWER packing must equal the engine's make_apply_power_flags:
    // Bash step 1 carries VULNERABLE in `extra`.
    const r::CardDef* bash = r::card_def(r::CardId::BASH);
    ASSERT_NE(bash, nullptr);
    EXPECT_EQ(bash->steps[1].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::VULNERABLE));
}

// --- 4. game_id string tables round-trip ------------------------------------
TEST(RegistryGen, GameIdTablesRoundTrip) {
    namespace r = sts::registry;

    EXPECT_EQ(r::card_game_id(r::CardId::STRIKE), "Strike_R");
    EXPECT_EQ(r::card_game_id(r::CardId::SHRUG_IT_OFF), "Shrug It Off");
    EXPECT_EQ(r::card_from_game_id("Bash"), r::CardId::BASH);
    EXPECT_EQ(r::card_from_game_id("Pommel Strike"), r::CardId::POMMEL_STRIKE);
    EXPECT_EQ(r::card_from_game_id("nope"), r::CardId::NONE);
    EXPECT_TRUE(r::card_game_id(r::CardId::NONE).empty());

    EXPECT_EQ(r::power_game_id(r::PowerId::WEAK), "Weakened");
    EXPECT_EQ(r::power_from_game_id("Vulnerable"), r::PowerId::VULNERABLE);
    EXPECT_EQ(r::monster_game_id(r::MonsterId::JAW_WORM), "JawWorm");
    EXPECT_EQ(r::monster_from_game_id("JawWorm"), r::MonsterId::JAW_WORM);

    // Relics (B3.24): the game's relicId strings round-trip, including ids whose
    // string differs from the display name (Boot, CeramicFish).
    EXPECT_EQ(r::relic_game_id(r::RelicId::BURNING_BLOOD), "Burning Blood");
    EXPECT_EQ(r::relic_from_game_id("Burning Blood"), r::RelicId::BURNING_BLOOD);
    EXPECT_EQ(r::relic_from_game_id("Bag of Marbles"), r::RelicId::BAG_OF_MARBLES);
    EXPECT_EQ(r::relic_game_id(r::RelicId::BOOT), "Boot");
    EXPECT_EQ(r::relic_from_game_id("CeramicFish"), r::RelicId::CERAMIC_FISH);
    EXPECT_EQ(r::relic_from_game_id("Circlet"), r::RelicId::CIRCLET);
    EXPECT_EQ(r::relic_from_game_id("anything"), r::RelicId::NONE);
    EXPECT_TRUE(r::potion_game_id(r::PotionId::NONE).empty());
}

// --- B3.24 relic table: tier + hook bindings match the registry --------------
TEST(RegistryGen, RelicTableMatchesRegistry) {
    namespace r = sts::registry;
    EXPECT_EQ(r::manifest::kRelicsCount, 35u);

    // Burning Blood (starter, native on_victory).
    const r::RelicDef* bb = r::relic_def(r::RelicId::BURNING_BLOOD);
    ASSERT_NE(bb, nullptr);
    EXPECT_EQ(bb->tier, r::RelicTier::STARTER);
    EXPECT_TRUE(bb->native);
    ASSERT_EQ(bb->hook_count, 1);
    EXPECT_EQ(bb->hooks[0].hook, r::RelicHook::ON_VICTORY);
    EXPECT_EQ(bb->hooks[0].step_count, 0) << "native relic lists an empty program";

    // Vajra (data): atBattleStart APPLY_POWER Strength 1 on self; extra packs the
    // PowerId exactly like the card table's make_apply_power_flags.
    const r::RelicDef* vajra = r::relic_def(r::RelicId::VAJRA);
    ASSERT_NE(vajra, nullptr);
    EXPECT_EQ(vajra->tier, r::RelicTier::COMMON);
    EXPECT_FALSE(vajra->native);
    ASSERT_EQ(vajra->hook_count, 1);
    const auto* vb = vajra->hook_binding(r::RelicHook::AT_BATTLE_START);
    ASSERT_NE(vb, nullptr);
    ASSERT_EQ(vb->step_count, 1);
    EXPECT_EQ(vb->steps[0].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(vb->steps[0].target, r::StepTarget::SELF);
    EXPECT_EQ(vb->steps[0].amount, 1);
    EXPECT_EQ(vb->steps[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::STRENGTH));

    // A non-combat relic carries no hook bindings.
    EXPECT_EQ(r::relic_def(r::RelicId::WHETSTONE)->hook_count, 0);
    EXPECT_EQ(r::relic_def(r::RelicId::WHETSTONE)->pool_order, 0);
    EXPECT_EQ(r::relic_def(r::RelicId::RED_SKULL)->pool_order, 32);
    EXPECT_EQ(r::relic_def(r::RelicId::CIRCLET)->pool_order, -1);
    EXPECT_EQ(r::relic_def(r::RelicId::CIRCLET)->initial_counter, 1);
    EXPECT_EQ(r::kRelicDefs.size(), 35u);
}

// --- 5. Manifest row counts match the seeded content ------------------------
TEST(RegistryGen, ManifestCounts) {
    namespace m = sts::registry::manifest;
    EXPECT_EQ(m::kCardsCount, 50u);   // B3.5: prior 39 + 11 red uncommon attacks
    EXPECT_EQ(m::kPowersCount, 22u);  // 19 + B3.13 Curl Up + B3.9 Frail + B3.17 Split
    EXPECT_EQ(m::kMonstersCount, 10u); // + B3.14 four small/medium slimes
                                       // + B3.17 two large slimes
    EXPECT_EQ(m::kRelicsCount, 35u);  // + B4.6 Circlet fallback
    EXPECT_EQ(m::kPotionsCount, 33u);
    EXPECT_EQ(m::kEventsCount, 0u);
    EXPECT_EQ(m::kEncountersCount, 20u);  // B3.12: Act-1 Exordium framework (4 weak +
                                          // 10 strong + 3 elite + 3 boss)
    EXPECT_EQ(m::kA20Count, 0u);
    EXPECT_EQ(m::kTotalCount, 170u);  // integrated through B3.5/B3.14/B4.6/B3.17
}

// --- 6. B2.2 skeleton migration: no dual system ------------------------------
// The engine's re-exports are the SAME entities as the generated ones -- the
// hand tables are gone (G5's "no dual system"), not merely value-equal.
TEST(RegistryGen, EngineReExportsGeneratedTables) {
    static_assert(std::is_same_v<sts::engine::CardId, sts::registry::CardId>);
    static_assert(std::is_same_v<sts::engine::PowerId, sts::registry::PowerId>);
    static_assert(
        std::is_same_v<sts::engine::MonsterId, sts::registry::MonsterId>);
    static_assert(std::is_same_v<sts::engine::RelicId, sts::registry::RelicId>);
    static_assert(std::is_same_v<sts::engine::CardDef, sts::registry::CardDef>);

    EXPECT_EQ(&sts::engine::kStrike, &sts::registry::kStrike);
    EXPECT_EQ(&sts::engine::kBash, &sts::registry::kBash);
    EXPECT_EQ(sts::engine::card_def(sts::engine::CardId::POMMEL_STRIKE),
              sts::registry::card_def(sts::registry::CardId::POMMEL_STRIKE));

    // The engine's Jaw Worm HP constants resolve from the generated table's a7
    // column at the skeleton's fixed A20.
    EXPECT_EQ(sts::engine::kJawWormHpMin,
              sts::registry::kJawWorm.hp_min(sts::engine::kSkeletonAscension));
    EXPECT_EQ(sts::engine::kJawWormHpMax,
              sts::registry::kJawWorm.hp_max(sts::engine::kSkeletonAscension));
}

// --- 6b. B2.2 monster table vs. hand-derived JawWorm.java columns ------------
// Every expected number below is hand-carried from the cited ascension branch,
// NOT read back from the generator: HP setHp(40,44) base / setHp(42,46) A7+
// (JawWorm.java:81-84); A17 branch bellowStr 5 / bellowBlock 9 / chompDmg 12
// (:86-91); A2 branch bellowStr 4 / bellowBlock 6 / chompDmg 12 (:92-97); base
// branch bellowStr 3 / bellowBlock 6 / chompDmg 11 (:98-103); thrashDmg 7 /
// thrashBlock 5 in all branches; move ids CHOMP=1/BELLOW=2/THRASH=3 (:65-67).
TEST(RegistryGen, MonsterTableMatchesJava) {
    namespace r = sts::registry;
    const r::MonsterDef& jw = r::kJawWorm;

    EXPECT_EQ(static_cast<int>(jw.id), 1);
    EXPECT_EQ(r::monster_def(r::MonsterId::JAW_WORM), &jw);
    EXPECT_EQ(r::monster_def(r::MonsterId::NONE), nullptr);
    EXPECT_TRUE(jw.ai_native);

    // HP tiers: base 40-44, A7+ 42-46, resolved across the boundary.
    EXPECT_EQ(jw.hp_min(0), 40);
    EXPECT_EQ(jw.hp_max(0), 44);
    EXPECT_EQ(jw.hp_min(6), 40);
    EXPECT_EQ(jw.hp_max(6), 44);
    EXPECT_EQ(jw.hp_min(7), 42);
    EXPECT_EQ(jw.hp_max(7), 46);
    EXPECT_EQ(jw.hp_min(20), 42);
    EXPECT_EQ(jw.hp_max(20), 46);

    // Moves are looked up by the game's byte move id, never 0.
    ASSERT_EQ(jw.move_count, 3);
    EXPECT_EQ(jw.move(0), nullptr);
    EXPECT_EQ(jw.move(4), nullptr);

    // CHOMP (1): one DAMAGE step on the player; 11 base, 12 from A2.
    const r::MonsterMove* chomp = jw.move(r::kJawWormMoveChomp);
    ASSERT_NE(chomp, nullptr);
    EXPECT_EQ(chomp->move_id, 1);
    EXPECT_EQ(chomp->intent, r::MonsterIntent::ATTACK);
    ASSERT_EQ(chomp->effect_count, 1);
    EXPECT_EQ(chomp->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(chomp->effects[0].target, r::MonsterMoveTarget::PLAYER);
    EXPECT_EQ(chomp->effects[0].amount.at(0), 11);
    EXPECT_EQ(chomp->effects[0].amount.at(1), 11);
    EXPECT_EQ(chomp->effects[0].amount.at(2), 12);
    EXPECT_EQ(chomp->effects[0].amount.at(20), 12);

    // BELLOW (2): APPLY_POWER Strength then BLOCK, both on self (takeTurn
    // addToBottom order, JawWorm.java:135-136). Strength 3/4@A2/5@A17;
    // block 6 base, 9 from A17.
    const r::MonsterMove* bellow = jw.move(r::kJawWormMoveBellow);
    ASSERT_NE(bellow, nullptr);
    EXPECT_EQ(bellow->move_id, 2);
    EXPECT_EQ(bellow->intent, r::MonsterIntent::DEFEND_BUFF);
    ASSERT_EQ(bellow->effect_count, 2);
    EXPECT_EQ(bellow->effects[0].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(bellow->effects[0].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(bellow->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::STRENGTH));
    EXPECT_EQ(bellow->effects[0].amount.at(1), 3);
    EXPECT_EQ(bellow->effects[0].amount.at(2), 4);
    EXPECT_EQ(bellow->effects[0].amount.at(16), 4);
    EXPECT_EQ(bellow->effects[0].amount.at(17), 5);
    EXPECT_EQ(bellow->effects[1].op, r::Opcode::BLOCK);
    EXPECT_EQ(bellow->effects[1].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(bellow->effects[1].amount.at(16), 6);
    EXPECT_EQ(bellow->effects[1].amount.at(17), 9);

    // THRASH (3): DAMAGE 7 on the player then BLOCK 5 on self (JawWorm.java:
    // 141-142), ascension-flat.
    const r::MonsterMove* thrash = jw.move(r::kJawWormMoveThrash);
    ASSERT_NE(thrash, nullptr);
    EXPECT_EQ(thrash->move_id, 3);
    EXPECT_EQ(thrash->intent, r::MonsterIntent::ATTACK_DEFEND);
    ASSERT_EQ(thrash->effect_count, 2);
    EXPECT_EQ(thrash->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(thrash->effects[0].target, r::MonsterMoveTarget::PLAYER);
    EXPECT_EQ(thrash->effects[0].amount.at(0), 7);
    EXPECT_EQ(thrash->effects[0].amount.at(20), 7);
    EXPECT_EQ(thrash->effects[1].op, r::Opcode::BLOCK);
    EXPECT_EQ(thrash->effects[1].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(thrash->effects[1].amount.at(0), 5);
    EXPECT_EQ(thrash->effects[1].amount.at(20), 5);

    // The generated intent values match the engine's fixture-pinned bytes.
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::ATTACK), 1);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::DEFEND_BUFF), 2);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::ATTACK_DEFEND), 3);
}

// --- 6c. B3.13 Cultist/louse tables vs. hand-derived Java columns -----------

TEST(RegistryGen, CultistTableMatchesJava) {
    namespace r = sts::registry;
    const r::MonsterDef& c = r::kCultist;
    EXPECT_EQ(static_cast<int>(c.id), 2);
    EXPECT_EQ(r::monster_def(r::MonsterId::CULTIST), &c);
    EXPECT_TRUE(c.ai_native);
    EXPECT_EQ(c.roll_count, 0);

    // Cultist.java:59-62: HP 48-54 base, 50-56 from A7.
    EXPECT_EQ(c.hp_min(6), 48);
    EXPECT_EQ(c.hp_max(6), 54);
    EXPECT_EQ(c.hp_min(7), 50);
    EXPECT_EQ(c.hp_max(20), 56);

    const r::MonsterMove* strike = c.move(r::kCultistMoveDarkStrike);
    ASSERT_NE(strike, nullptr);
    EXPECT_EQ(strike->intent, r::MonsterIntent::ATTACK);
    ASSERT_EQ(strike->effect_count, 1);
    EXPECT_EQ(strike->effects[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(strike->effects[0].target, r::MonsterMoveTarget::PLAYER);
    EXPECT_EQ(strike->effects[0].amount.at(0), 6);
    EXPECT_EQ(strike->effects[0].amount.at(20), 6);

    // ritualAmount 3 base / 4 at A2; Incantation adds one more at A17.
    const r::MonsterMove* inc = c.move(r::kCultistMoveIncantation);
    ASSERT_NE(inc, nullptr);
    EXPECT_EQ(inc->intent, r::MonsterIntent::BUFF);
    ASSERT_EQ(inc->effect_count, 1);
    EXPECT_EQ(inc->effects[0].op, r::Opcode::APPLY_POWER);
    EXPECT_EQ(inc->effects[0].target, r::MonsterMoveTarget::SELF);
    EXPECT_EQ(inc->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::RITUAL));
    EXPECT_EQ(inc->effects[0].amount.at(1), 3);
    EXPECT_EQ(inc->effects[0].amount.at(2), 4);
    EXPECT_EQ(inc->effects[0].amount.at(16), 4);
    EXPECT_EQ(inc->effects[0].amount.at(17), 5);
}

TEST(RegistryGen, LouseTablesMatchJava) {
    namespace r = sts::registry;
    const auto check_rolls = [](const r::MonsterDef& l) {
        ASSERT_EQ(l.roll_count, 2);
        const r::MonsterRollDef* bite = l.roll(0);
        const r::MonsterRollDef* curl = l.roll(1);
        ASSERT_NE(bite, nullptr);
        ASSERT_NE(curl, nullptr);
        EXPECT_EQ(l.roll(2), nullptr);
        EXPECT_EQ(bite->stream, r::MonsterRollStream::MONSTER_HP);
        EXPECT_EQ(bite->timing,
                  r::MonsterRollTiming::CONSTRUCTOR_AFTER_HP);
        EXPECT_EQ(bite->min(1), 5);
        EXPECT_EQ(bite->max(1), 7);
        EXPECT_EQ(bite->min(2), 6);
        EXPECT_EQ(bite->max(20), 8);
        EXPECT_EQ(curl->stream, r::MonsterRollStream::MONSTER_HP);
        EXPECT_EQ(curl->timing, r::MonsterRollTiming::PRE_BATTLE);
        EXPECT_EQ(curl->min(6), 3);
        EXPECT_EQ(curl->max(6), 7);
        EXPECT_EQ(curl->min(7), 4);
        EXPECT_EQ(curl->max(16), 8);
        EXPECT_EQ(curl->min(17), 9);
        EXPECT_EQ(curl->max(20), 12);
    };

    const r::MonsterDef& red = r::kLouseNormal;
    EXPECT_EQ(static_cast<int>(red.id), 3);
    EXPECT_EQ(red.hp_min(6), 10);
    EXPECT_EQ(red.hp_max(6), 15);
    EXPECT_EQ(red.hp_min(7), 11);
    EXPECT_EQ(red.hp_max(20), 16);
    EXPECT_EQ(r::kLouseNormalRollBiteDamage, 0);
    EXPECT_EQ(r::kLouseNormalRollCurlUp, 1);
    check_rolls(red);

    const r::MonsterMove* red_bite = red.move(r::kLouseNormalMoveBite);
    const r::MonsterMove* strengthen = red.move(r::kLouseNormalMoveStrengthen);
    ASSERT_NE(red_bite, nullptr);
    ASSERT_NE(strengthen, nullptr);
    EXPECT_EQ(red_bite->intent, r::MonsterIntent::ATTACK);
    EXPECT_EQ(red_bite->effects[0].amount.at(20), 0)
        << "native turn substitutes the per-instance registry roll";
    EXPECT_EQ(strengthen->intent, r::MonsterIntent::BUFF);
    EXPECT_EQ(strengthen->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::STRENGTH));
    EXPECT_EQ(strengthen->effects[0].amount.at(16), 3);
    EXPECT_EQ(strengthen->effects[0].amount.at(17), 4);

    const r::MonsterDef& green = r::kLouseDefensive;
    EXPECT_EQ(static_cast<int>(green.id), 4);
    EXPECT_EQ(green.hp_min(6), 11);
    EXPECT_EQ(green.hp_max(6), 17);
    EXPECT_EQ(green.hp_min(7), 12);
    EXPECT_EQ(green.hp_max(20), 18);
    EXPECT_EQ(r::kLouseDefensiveRollBiteDamage, 0);
    EXPECT_EQ(r::kLouseDefensiveRollCurlUp, 1);
    check_rolls(green);

    const r::MonsterMove* weaken = green.move(r::kLouseDefensiveMoveWeaken);
    ASSERT_NE(weaken, nullptr);
    EXPECT_EQ(weaken->intent, r::MonsterIntent::DEBUFF);
    EXPECT_EQ(weaken->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::WEAK));
    EXPECT_EQ(weaken->effects[0].amount.at(0), 2);
    EXPECT_EQ(weaken->effects[0].amount.at(20), 2);

    const r::PowerDef* curl_power = r::power_def(r::PowerId::CURL_UP);
    ASSERT_NE(curl_power, nullptr);
    EXPECT_TRUE(curl_power->native);
    EXPECT_NE(curl_power->hook_binding(r::Hook::ON_ATTACKED), nullptr);

    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::BUFF), 4);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::DEBUFF), 5);
}

TEST(RegistryGen, SmallAndMediumSlimeTablesMatchJava) {
    namespace r = sts::registry;
    const auto hp = [](const r::MonsterDef& d, int blo, int bhi, int a7lo,
                       int a7hi) {
        EXPECT_EQ(d.hp_min(6), blo);
        EXPECT_EQ(d.hp_max(6), bhi);
        EXPECT_EQ(d.hp_min(7), a7lo);
        EXPECT_EQ(d.hp_max(20), a7hi);
        EXPECT_TRUE(d.ai_native);
        EXPECT_EQ(d.roll_count, 0);
    };
    hp(r::kSpikeSlimeSmall, 10, 14, 11, 15);
    hp(r::kSpikeSlimeMedium, 28, 32, 29, 34);
    hp(r::kAcidSlimeSmall, 8, 12, 9, 13);
    hp(r::kAcidSlimeMedium, 28, 32, 29, 34);

    EXPECT_EQ(static_cast<int>(r::MonsterId::SPIKE_SLIME_SMALL), 5);
    EXPECT_EQ(static_cast<int>(r::MonsterId::SPIKE_SLIME_MEDIUM), 6);
    EXPECT_EQ(static_cast<int>(r::MonsterId::ACID_SLIME_SMALL), 7);
    EXPECT_EQ(static_cast<int>(r::MonsterId::ACID_SLIME_MEDIUM), 8);
    EXPECT_EQ(r::monster_from_game_id("SpikeSlime_S"),
              r::MonsterId::SPIKE_SLIME_SMALL);
    EXPECT_EQ(r::monster_from_game_id("AcidSlime_M"),
              r::MonsterId::ACID_SLIME_MEDIUM);

    const r::MonsterMove* spike_s =
        r::kSpikeSlimeSmall.move(r::kSpikeSlimeSmallMoveTackle);
    ASSERT_NE(spike_s, nullptr);
    EXPECT_EQ(spike_s->effects[0].amount.at(1), 5);
    EXPECT_EQ(spike_s->effects[0].amount.at(2), 6);

    const r::MonsterMove* spike_tackle =
        r::kSpikeSlimeMedium.move(r::kSpikeSlimeMediumMoveFlameTackle);
    ASSERT_NE(spike_tackle, nullptr);
    EXPECT_EQ(spike_tackle->intent, r::MonsterIntent::ATTACK_DEBUFF);
    ASSERT_EQ(spike_tackle->effect_count, 2);
    EXPECT_EQ(spike_tackle->effects[0].amount.at(1), 8);
    EXPECT_EQ(spike_tackle->effects[0].amount.at(2), 10);
    EXPECT_EQ(spike_tackle->effects[1].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(spike_tackle->effects[1].extra & 0xFFFFu,
              static_cast<uint32_t>(r::CardId::SLIMED));
    EXPECT_EQ((spike_tackle->effects[1].extra >> 16) & 0xFFu, 2u)
        << "CardPile::DISCARD";
    const r::MonsterMove* frail =
        r::kSpikeSlimeMedium.move(r::kSpikeSlimeMediumMoveFrailLick);
    ASSERT_NE(frail, nullptr);
    EXPECT_EQ(frail->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::FRAIL));

    const r::MonsterMove* acid_s =
        r::kAcidSlimeSmall.move(r::kAcidSlimeSmallMoveTackle);
    ASSERT_NE(acid_s, nullptr);
    EXPECT_EQ(acid_s->effects[0].amount.at(1), 3);
    EXPECT_EQ(acid_s->effects[0].amount.at(2), 4);
    const r::MonsterMove* acid_m_wound =
        r::kAcidSlimeMedium.move(r::kAcidSlimeMediumMoveWoundTackle);
    ASSERT_NE(acid_m_wound, nullptr);
    EXPECT_EQ(acid_m_wound->effects[0].amount.at(1), 7);
    EXPECT_EQ(acid_m_wound->effects[0].amount.at(2), 8);
    EXPECT_EQ(acid_m_wound->effects[1].op, r::Opcode::MAKE_CARD);
    const r::MonsterMove* acid_m_tackle =
        r::kAcidSlimeMedium.move(r::kAcidSlimeMediumMoveTackle);
    ASSERT_NE(acid_m_tackle, nullptr);
    EXPECT_EQ(acid_m_tackle->effects[0].amount.at(1), 10);
    EXPECT_EQ(acid_m_tackle->effects[0].amount.at(2), 12);
    const r::MonsterMove* acid_m_lick =
        r::kAcidSlimeMedium.move(r::kAcidSlimeMediumMoveLick);
    ASSERT_NE(acid_m_lick, nullptr);
    EXPECT_EQ(acid_m_lick->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::WEAK));

    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::ATTACK_DEBUFF), 6);
}

TEST(RegistryGen, LargeSlimeTablesMatchJava) {
    namespace r = sts::registry;
    // Appended ids after 8 (append-only, design doc §4.4); UNKNOWN intent and
    // the Split marker power are pinned appends too.
    EXPECT_EQ(static_cast<int>(r::MonsterId::SPIKE_SLIME_LARGE), 9);
    EXPECT_EQ(static_cast<int>(r::MonsterId::ACID_SLIME_LARGE), 10);
    EXPECT_EQ(static_cast<uint8_t>(r::MonsterIntent::UNKNOWN), 7);
    EXPECT_EQ(static_cast<int>(r::PowerId::SPLIT), 22);
    EXPECT_EQ(r::monster_from_game_id("SpikeSlime_L"),
              r::MonsterId::SPIKE_SLIME_LARGE);
    EXPECT_EQ(r::monster_from_game_id("AcidSlime_L"),
              r::MonsterId::ACID_SLIME_LARGE);
    EXPECT_EQ(r::power_game_id(r::PowerId::SPLIT), "Split");

    // HP columns (SpikeSlime_L.java:71-73 / AcidSlime_L.java:76-78).
    EXPECT_EQ(r::kSpikeSlimeLarge.hp_min(6), 64);
    EXPECT_EQ(r::kSpikeSlimeLarge.hp_max(6), 70);
    EXPECT_EQ(r::kSpikeSlimeLarge.hp_min(7), 67);
    EXPECT_EQ(r::kSpikeSlimeLarge.hp_max(20), 73);
    EXPECT_EQ(r::kAcidSlimeLarge.hp_min(6), 65);
    EXPECT_EQ(r::kAcidSlimeLarge.hp_max(6), 69);
    EXPECT_EQ(r::kAcidSlimeLarge.hp_min(7), 68);
    EXPECT_EQ(r::kAcidSlimeLarge.hp_max(20), 72);
    EXPECT_TRUE(r::kSpikeSlimeLarge.ai_native);
    EXPECT_TRUE(r::kAcidSlimeLarge.ai_native);
    EXPECT_EQ(r::kSpikeSlimeLarge.roll_count, 0);
    EXPECT_EQ(r::kAcidSlimeLarge.roll_count, 0);

    // Spike L FLAME_TACKLE: 16/A2 18 + 2 Slimed to discard (SpikeSlime_L.java:
    // 82-86,108-112); FRAIL_LICK 2/A17 3 (:100-106); SPLIT telegraph UNKNOWN.
    const r::MonsterMove* spike_tackle =
        r::kSpikeSlimeLarge.move(r::kSpikeSlimeLargeMoveFlameTackle);
    ASSERT_NE(spike_tackle, nullptr);
    EXPECT_EQ(spike_tackle->intent, r::MonsterIntent::ATTACK_DEBUFF);
    ASSERT_EQ(spike_tackle->effect_count, 2);
    EXPECT_EQ(spike_tackle->effects[0].amount.at(1), 16);
    EXPECT_EQ(spike_tackle->effects[0].amount.at(2), 18);
    EXPECT_EQ(spike_tackle->effects[1].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(spike_tackle->effects[1].amount.at(20), 2) << "WOUND_COUNT";
    EXPECT_EQ(spike_tackle->effects[1].extra & 0xFFFFu,
              static_cast<uint32_t>(r::CardId::SLIMED));
    EXPECT_EQ((spike_tackle->effects[1].extra >> 16) & 0xFFu, 2u)
        << "CardPile::DISCARD";
    const r::MonsterMove* spike_frail =
        r::kSpikeSlimeLarge.move(r::kSpikeSlimeLargeMoveFrailLick);
    ASSERT_NE(spike_frail, nullptr);
    EXPECT_EQ(spike_frail->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::FRAIL));
    EXPECT_EQ(spike_frail->effects[0].amount.at(16), 2);
    EXPECT_EQ(spike_frail->effects[0].amount.at(17), 3);
    const r::MonsterMove* spike_split =
        r::kSpikeSlimeLarge.move(r::kSpikeSlimeLargeMoveSplit);
    ASSERT_NE(spike_split, nullptr);
    EXPECT_EQ(spike_split->move_id, 3);
    EXPECT_EQ(spike_split->intent, r::MonsterIntent::UNKNOWN);

    // Acid L WOUND_TACKLE 11/A2 12 + 2 Slimed; TACKLE 16/A2 18; LICK Weak 2;
    // SPLIT telegraph UNKNOWN (AcidSlime_L.java:88-92,107-138).
    const r::MonsterMove* acid_wound =
        r::kAcidSlimeLarge.move(r::kAcidSlimeLargeMoveWoundTackle);
    ASSERT_NE(acid_wound, nullptr);
    EXPECT_EQ(acid_wound->effects[0].amount.at(1), 11);
    EXPECT_EQ(acid_wound->effects[0].amount.at(2), 12);
    EXPECT_EQ(acid_wound->effects[1].op, r::Opcode::MAKE_CARD);
    EXPECT_EQ(acid_wound->effects[1].amount.at(20), 2);
    const r::MonsterMove* acid_tackle =
        r::kAcidSlimeLarge.move(r::kAcidSlimeLargeMoveTackle);
    ASSERT_NE(acid_tackle, nullptr);
    EXPECT_EQ(acid_tackle->effects[0].amount.at(1), 16);
    EXPECT_EQ(acid_tackle->effects[0].amount.at(2), 18);
    const r::MonsterMove* acid_lick =
        r::kAcidSlimeLarge.move(r::kAcidSlimeLargeMoveLick);
    ASSERT_NE(acid_lick, nullptr);
    EXPECT_EQ(acid_lick->effects[0].extra,
              sts::engine::make_apply_power_flags(sts::engine::PowerId::WEAK));
    EXPECT_EQ(acid_lick->effects[0].amount.at(20), 2) << "WEAK_TURNS";
    const r::MonsterMove* acid_split =
        r::kAcidSlimeLarge.move(r::kAcidSlimeLargeMoveSplit);
    ASSERT_NE(acid_split, nullptr);
    EXPECT_EQ(acid_split->move_id, 3);
    EXPECT_EQ(acid_split->intent, r::MonsterIntent::UNKNOWN);

    // The B3.17 split-framework opcodes are pinned appends from 25.
    EXPECT_EQ(static_cast<uint16_t>(r::Opcode::CANNOT_LOSE), 25);
    EXPECT_EQ(static_cast<uint16_t>(r::Opcode::CAN_LOSE), 26);
    EXPECT_EQ(static_cast<uint16_t>(r::Opcode::SUICIDE), 27);
    EXPECT_EQ(static_cast<uint16_t>(r::Opcode::SPAWN_MONSTER), 28);
    EXPECT_EQ(static_cast<uint16_t>(r::Opcode::SET_MOVE), 29);
}

// --- 6d. Duplicate move_id is rejected with a clear error --------------------
TEST(RegistryGen, DuplicateMoveIdFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = scratch / "bad_registry_moves";
    fs::remove_all(bad_reg);
    fs::create_directories(bad_reg);

    // Copy the real registry, then append a monster whose two moves share a
    // move_id.
    for (const auto& e : fs::directory_iterator(kRegistryDir)) {
        if (e.path().extension() == ".yaml") {
            fs::copy_file(e.path(), bad_reg / e.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
    }
    {
        std::ofstream monsters(bad_reg / "monsters.yaml", std::ios::app);
        // id 99: unused (real ids 1..8 include B3.14 slimes), so the
        // parser reaches the duplicate-move_id check rather than an id collision.
        monsters << "\n- id: 99\n  name: BAD_MOVES\n  game_id: \"BadMoves\"\n"
                    "  provenance: \"synthetic duplicate-move_id negative test\"\n"
                    "  hp:\n    base: {min: 10, max: 12}\n"
                    "  moves:\n"
                    "    - name: FIRST\n      move_id: 1\n      intent: ATTACK\n"
                    "      effects:\n"
                    "        - {op: DAMAGE, target: PLAYER, amount: 3}\n"
                    "    - name: SECOND\n      move_id: 1\n      intent: ATTACK\n"
                    "      effects:\n"
                    "        - {op: DAMAGE, target: PLAYER, amount: 4}\n"
                    "  ai: native\n";
    }

    const fs::path out = scratch / "bad_moves_out";
    const fs::path err = scratch / "bad_moves_err.txt";
    const int status = run_generator(bad_reg.string(), out.string(), err.string());
    EXPECT_NE(status, 0) << "generator should fail on a duplicate move_id";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("duplicate move_id"), std::string::npos) << msg;
    EXPECT_NE(msg.find("BAD_MOVES"), std::string::npos) << msg;
}

// --- 7. B3.1 card-flag + two-row (upgrade) codegen --------------------------
// The skeleton cards carry no flags and no distinct `upgraded:` block, so the
// generated flags are 0 and the upgraded program mirrors the base program
// byte-for-byte (the safe default). The generated kCardFlag* constants match the
// engine's CardFlag (pinned by the cards.hpp static_assert; re-checked here as a
// value equality for good measure).
TEST(RegistryGen, CardFlagsAndUpgradeDefaultsForSkeleton) {
    namespace r = sts::registry;

    EXPECT_EQ(r::kCardFlagExhaust,
              sts::engine::card_flag_bit(sts::engine::CardFlag::EXHAUST));
    EXPECT_EQ(r::kCardFlagUnplayable,
              sts::engine::card_flag_bit(sts::engine::CardFlag::UNPLAYABLE));
    EXPECT_EQ(r::kCardFlagXcost,
              sts::engine::card_flag_bit(sts::engine::CardFlag::XCOST));

    for (int id = 1; id <= 5; ++id) {
        const r::CardDef* g = r::card_def(static_cast<r::CardId>(id));
        ASSERT_NE(g, nullptr) << "id " << id;
        EXPECT_EQ(g->flags, 0u) << "skeleton card " << id << " has no flags";
        // No `upgraded:` in the YAML -> upgraded mirrors base.
        EXPECT_EQ(g->upgraded_cost, g->base_cost) << "id " << id;
        EXPECT_EQ(g->upgraded_flags, g->flags) << "id " << id;
        ASSERT_EQ(g->upgraded_step_count, g->step_count) << "id " << id;
        for (int s = 0; s < r::kMaxCardSteps; ++s) {
            const auto si = static_cast<std::size_t>(s);
            EXPECT_EQ(static_cast<int>(g->upgraded_steps[si].op),
                      static_cast<int>(g->steps[si].op)) << "id " << id << " step " << s;
            EXPECT_EQ(g->upgraded_steps[si].amount, g->steps[si].amount)
                << "id " << id << " step " << s;
            EXPECT_EQ(g->upgraded_steps[si].extra, g->steps[si].extra)
                << "id " << id << " step " << s;
        }
    }
}

// A card WITH an `upgraded:` block (and a `flags:`/X-cost `cost:`) emits a
// DISTINCT upgraded program + the expected flag bits -- end-to-end through the
// generator. Writes a synthetic registry (real files + one appended card),
// generates, and reads the resulting literal back.
TEST(RegistryGen, UpgradedBlockAndFlagsEmitDistinctRow) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path reg = scratch / "upgrade_registry";
    fs::remove_all(reg);
    fs::create_directories(reg);
    for (const auto& e : fs::directory_iterator(kRegistryDir)) {
        if (e.path().extension() == ".yaml") {
            fs::copy_file(e.path(), reg / e.path().filename(),
                          fs::copy_options::overwrite_existing);
        }
    }
    {
        // id 100: X-cost + exhaust; base DAMAGE 7 (all enemies), upgraded DAMAGE 99.
        // (Past the real card ids 1-10 so it never collides -- B3.4 filled 6-10.)
        std::ofstream cards(reg / "cards.yaml", std::ios::app);
        cards << "\n- id: 100\n  name: SYNTH_XCOST\n  game_id: \"SynthX\"\n"
                 "  type: ATTACK\n  cost: -1\n  target: ALL_ENEMY\n"
                 "  flags: [exhaust]\n"
                 "  provenance: \"synthetic B3.1 upgrade/flags codegen test\"\n"
                 "  effects:\n"
                 "    - {op: DAMAGE, target: ALL_ENEMY, amount: 7}\n"
                 "  upgraded:\n"
                 "    - {op: DAMAGE, target: ALL_ENEMY, amount: 99}\n";
    }

    const fs::path out = scratch / "upgrade_out";
    const fs::path err = scratch / "upgrade_err.txt";
    fs::remove_all(out);
    ASSERT_EQ(run_generator(reg.string(), out.string(), err.string()), 0)
        << read_text(err);

    const std::string hpp = read_text(out / "sts/registry/card_table.hpp");
    // The distinct upgraded amount appears (proving two rows were emitted).
    EXPECT_NE(hpp.find("99"), std::string::npos) << "upgraded amount not emitted";
    // The synthetic card's literal carries the X-cost (32) | exhaust (1) == 33
    // flag word and the ALL_ENEMY step target.
    const auto pos = hpp.find("kSynthXcost");
    ASSERT_NE(pos, std::string::npos) << "synthetic card literal missing";
    const std::string lit = hpp.substr(pos, 400);
    EXPECT_NE(lit.find("StepTarget::ALL_ENEMY"), std::string::npos) << lit;
    EXPECT_NE(lit.find(", 33, "), std::string::npos)
        << "expected flags word 33 (xcost|exhaust) in: " << lit;
}
