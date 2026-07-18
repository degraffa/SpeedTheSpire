// B2.1 acceptance suite for the registry codegen (tools/registry_gen/gen.py):
//
//   1. Determinism   -- running the generator twice yields byte-identical output.
//   2. DuplicateId   -- a YAML entry with a reused id fails generation with a
//                        clear error and a non-zero exit.
//   3. Equivalence   -- the generated tables match the hand-written engine tables
//                        (types.hpp enums, cards.hpp CardDef) field-for-field, so
//                        the later skeleton migration can swap the engine onto the
//                        generated headers with zero downstream change.
//   4. GameIds       -- the game_id<->enum string tables round-trip.
//   5. Manifest      -- the row-count manifest matches the seeded content.
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
#include <vector>

#include <gtest/gtest.h>

// Generated headers (build tree, via the registry_generated include dir).
#include "sts/registry/card_table.hpp"
#include "sts/registry/game_ids.hpp"
#include "sts/registry/ids.hpp"
#include "sts/registry/manifest.hpp"

// Hand-written engine tables -- the equivalence oracle.
#include "sts/engine/cards.hpp"
#include "sts/engine/interp.hpp"
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

const std::array<const char*, 4> kGenFiles = {
    "sts/registry/ids.hpp", "sts/registry/card_table.hpp",
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

    // Empty domains: unknown lookups return NONE, NONE maps to "".
    EXPECT_EQ(r::relic_from_game_id("anything"), r::RelicId::NONE);
    EXPECT_TRUE(r::potion_game_id(r::PotionId::NONE).empty());
}

// --- 5. Manifest row counts match the seeded content ------------------------
TEST(RegistryGen, ManifestCounts) {
    namespace m = sts::registry::manifest;
    EXPECT_EQ(m::kCardsCount, 5u);
    EXPECT_EQ(m::kPowersCount, 3u);
    EXPECT_EQ(m::kMonstersCount, 1u);
    EXPECT_EQ(m::kRelicsCount, 0u);
    EXPECT_EQ(m::kPotionsCount, 0u);
    EXPECT_EQ(m::kEventsCount, 0u);
    EXPECT_EQ(m::kEncountersCount, 0u);
    EXPECT_EQ(m::kA20Count, 0u);
    EXPECT_EQ(m::kTotalCount, 9u);
}
