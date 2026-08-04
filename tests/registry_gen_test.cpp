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
//   7. Events        -- the metadata-only events.yaml rows reproduce the five
//                        canonical dungeon lists in Java insertion order, their
//                        game_id join keys round-trip, an unknown id is rejected
//                        in both directions, and a reused event id fails
//                        generation with a clear error. The per-act membership
//                        and draw-gate metadata added by S2.02, and the
//                        conditions-schema negative tests, live in
//                        tests/act_event_lists_test.cpp.
//
// "Generated headers compile standalone" is proven by registry_gen_standalone.cpp
// (this TU additionally includes the engine headers to run the equivalence checks).
//
// The generator is invoked out-of-process via the same Python interpreter CMake
// found; paths arrive as compile definitions.

#include <algorithm>
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
#include "sts/registry/event_table.hpp"
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

#include "host_shell.hpp"

namespace fs = std::filesystem;

namespace {

const char* kPython = STS_PYTHON_EXECUTABLE;
const char* kGenPy = STS_REGISTRY_GEN_PY;
const char* kRegistryDir = STS_REGISTRY_DIR;
const char* kScratchDir = STS_GEN_SCRATCH;

// Host-shell portability -- including the cmd.exe rule that once made all 9
// RegistryGen cases fail on Windows -- lives in tests/host_shell.hpp.
using sts::testing::run_shell;
using sts::testing::shell_quote;

// Run the generator against `registry_dir`, writing headers under `out_dir` and
// stderr into `err_file`. Returns the process exit status.
int run_generator(const std::string& registry_dir, const std::string& out_dir,
                  const std::string& err_file) {
    return run_shell(shell_quote(kPython) + " " + shell_quote(kGenPy) +
                     " --registry " + shell_quote(registry_dir) + " --out " +
                     shell_quote(out_dir) + " 2> " + shell_quote(err_file));
}

std::vector<unsigned char> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Copy the real registry into a fresh scratch directory so a test can append a
// deliberately-bad row without touching registry/*.yaml.
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

const std::array<const char*, 9> kGenFiles = {
    "sts/registry/ids.hpp", "sts/registry/card_table.hpp",
    "sts/registry/power_table.hpp", "sts/registry/relic_table.hpp",
    "sts/registry/potion_table.hpp",
    "sts/registry/monster_table.hpp",
    // S2.02: event_table.hpp now carries per-row pool/act_mask columns, so it
    // is a content table like the rest and belongs under the determinism
    // guard rather than being covered only indirectly via ids.hpp.
    "sts/registry/event_table.hpp",
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
        relics << "\n- id: 200\n  name: DUPLICATE_POOL_SLOT\n"
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

// The `observability:` field (training-plan §2.3) must be parsed FAIL-LOUD:
// the domain loader ignores unknown row KEYS silently, so an unknown transform
// VALUE has to die in parse_observability rather than silently drop the row
// from the generated membership table (the parse_pickup convention).
TEST(RegistryGen, UnknownObservabilityValueFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_observability_registry");
    {
        std::ofstream relics(bad_reg / "relics.yaml", std::ios::app);
        relics << "\n- id: 200\n  name: BOGUS_OBSERVER\n"
                  "  game_id: \"Bogus Observer\"\n  tier: SPECIAL\n"
                  "  observability: TELEPATHY\n"
                  "  provenance: \"synthetic unknown-observability negative test\"\n";
    }

    const fs::path out = scratch / "bad_observability_out";
    const fs::path err = scratch / "bad_observability_err.txt";
    const int status = run_generator(bad_reg.string(), out.string(), err.string());
    EXPECT_NE(status, 0) << "generator should fail on an unknown observability value";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("unknown observability"), std::string::npos) << msg;
    EXPECT_NE(msg.find("TELEPATHY"), std::string::npos) << msg;
    EXPECT_NE(msg.find("BOGUS_OBSERVER"), std::string::npos) << msg;
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
        EXPECT_EQ(static_cast<int>(g->target_kind),
                  static_cast<int>(e->target_kind))
            << "target_kind, id " << id;
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

    const r::CardDef* strike = r::card_def(r::CardId::STRIKE);
    const r::CardDef* spot = r::card_def(r::CardId::SPOT_WEAKNESS);
    ASSERT_NE(strike, nullptr);
    ASSERT_NE(spot, nullptr);
    EXPECT_EQ(strike->target_kind, r::CardTargetKind::ENEMY);
    EXPECT_EQ(spot->target_kind, r::CardTargetKind::SELF_AND_ENEMY);
    EXPECT_NE(strike->target_kind, spot->target_kind)
        << "needs_target alone must not collapse the dequeue split";
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
    // No Block's join key is the POWER_ID literal, not the display name
    // (NoBlockPower.java:19). The row first landed with "No Block" and the
    // first capture whose player carried the power (a Neow Panic Button,
    // wave2cap_bottle_* STS03364) failed translation on it -- this pin is
    // that reproducer, promoted.
    EXPECT_EQ(r::power_game_id(r::PowerId::NO_BLOCK), "NoBlockPower");
    EXPECT_EQ(r::power_from_game_id("NoBlockPower"), r::PowerId::NO_BLOCK);
    EXPECT_EQ(r::power_from_game_id("No Block"), r::PowerId::NONE);
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

    // Events (B4.10 checkpoint): the game's event id strings round-trip. These
    // are the strings the three dungeon lists are built from, so they are the
    // translator's join key -- including the four whose id string is NOT the
    // display name (FaceTrader, NoteForYourself, SecretPortal, WeMeetAgain) and
    // the two the game itself spells oddly ("Transmorgrifier" is misspelled in
    // Exordium.java:242 and in Transmogrifier.ID; "Liars Game" has no
    // apostrophe).
    EXPECT_EQ(r::event_game_id(r::EventId::BIG_FISH), "Big Fish");
    EXPECT_EQ(r::event_game_id(r::EventId::TRANSMORGRIFIER), "Transmorgrifier");
    EXPECT_EQ(r::event_game_id(r::EventId::LIARS_GAME), "Liars Game");
    EXPECT_EQ(r::event_game_id(r::EventId::NLOTH), "N'loth");
    EXPECT_EQ(r::event_from_game_id("Match and Keep!"),
              r::EventId::MATCH_AND_KEEP);
    EXPECT_EQ(r::event_from_game_id("FaceTrader"), r::EventId::FACE_TRADER);
    EXPECT_EQ(r::event_from_game_id("NoteForYourself"),
              r::EventId::NOTE_FOR_YOURSELF);
    EXPECT_EQ(r::event_from_game_id("SecretPortal"), r::EventId::SECRET_PORTAL);
    EXPECT_EQ(r::event_from_game_id("WeMeetAgain"), r::EventId::WE_MEET_AGAIN);

    // Unknown-id rejection, both directions. A game_id the registry does not
    // carry resolves to NONE rather than to whatever row happens to sort first,
    // and an EventId outside the generated rows yields the empty string rather
    // than reading off the end of the switch.
    EXPECT_EQ(r::event_from_game_id("nope"), r::EventId::NONE);
    EXPECT_EQ(r::event_from_game_id(""), r::EventId::NONE);
    EXPECT_EQ(r::event_from_game_id("Big  Fish"), r::EventId::NONE)
        << "join key is exact, not whitespace-normalised";
    EXPECT_EQ(r::event_from_game_id("big fish"), r::EventId::NONE)
        << "join key is case-sensitive";
    EXPECT_TRUE(r::event_game_id(r::EventId::NONE).empty());
    EXPECT_TRUE(r::event_game_id(static_cast<r::EventId>(9999)).empty());
}

// --- B3.24 relic table: tier + hook bindings match the registry --------------
TEST(RegistryGen, RelicTableMatchesRegistry) {
    namespace r = sts::registry;
    EXPECT_EQ(r::manifest::kRelicsCount, 150u);  // + the 28 rares, 17 shop
                                                 // relics and Odd Mushroom,
                                                 // + the 22-relic BOSS pool and
                                                 // the 9 Act-1 event SPECIALs,
                                                 // + the 8 Act-2/3 SPECIALs

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
    EXPECT_EQ(r::kRelicDefs.size(), 150u);

    // --- B3.25 uncommon rows ------------------------------------------------
    // Mercury Hourglass (data): atTurnStart DAMAGE 3 to ALL enemies, THORNS-typed
    // (extra == DamageType::THORNS == 1, the make_damage_flags packing).
    const r::RelicDef* mh = r::relic_def(r::RelicId::MERCURY_HOURGLASS);
    ASSERT_NE(mh, nullptr);
    EXPECT_EQ(mh->tier, r::RelicTier::UNCOMMON);
    EXPECT_FALSE(mh->native);
    const auto* mb = mh->hook_binding(r::RelicHook::AT_TURN_START);
    ASSERT_NE(mb, nullptr);
    ASSERT_EQ(mb->step_count, 1);
    EXPECT_EQ(mb->steps[0].op, r::Opcode::DAMAGE);
    EXPECT_EQ(mb->steps[0].target, r::StepTarget::ALL_ENEMY);
    EXPECT_EQ(mb->steps[0].amount, 3);
    EXPECT_EQ(mb->steps[0].extra,
              sts::engine::make_damage_flags(sts::engine::DamageType::THORNS));

    // Sundial: native on_shuffle (the new B3.25 hook), onEquip counter 0.
    const r::RelicDef* sun = r::relic_def(r::RelicId::SUNDIAL);
    ASSERT_NE(sun, nullptr);
    EXPECT_TRUE(sun->native);
    EXPECT_NE(sun->hook_binding(r::RelicHook::ON_SHUFFLE), nullptr);
    EXPECT_EQ(sun->initial_counter, 0);

    // Gremlin Horn: native on_monster_death (the other new B3.25 hook).
    EXPECT_NE(r::relic_def(r::RelicId::GREMLIN_HORN)
                  ->hook_binding(r::RelicHook::ON_MONSTER_DEATH),
              nullptr);

    // Paper Phrog / Strike Dummy / Meat on the Bone: bespoke-site rows, no hook
    // bindings (pipeline / queue-time / pre-victory bodies).
    EXPECT_EQ(r::relic_def(r::RelicId::PAPER_PHROG)->hook_count, 0);
    EXPECT_EQ(r::relic_def(r::RelicId::STRIKE_DUMMY)->hook_count, 0);
    EXPECT_EQ(r::relic_def(r::RelicId::MEAT_ON_THE_BONE)->hook_count, 0);

    // Derived canonical pre-shuffle pool_order endpoints (three-seed shuffle
    // inversion; relic_pools_test pins the full shuffled orders).
    EXPECT_EQ(r::relic_def(r::RelicId::BOTTLED_TORNADO)->pool_order, 0);
    EXPECT_EQ(r::relic_def(r::RelicId::PAPER_PHROG)->pool_order, 29);
    EXPECT_EQ(r::relic_def(r::RelicId::MATRYOSHKA)->initial_counter, 2);
}

// --- 5. Manifest row counts match the seeded content ------------------------
TEST(RegistryGen, ManifestCounts) {
    namespace m = sts::registry::manifest;
    EXPECT_EQ(m::kCardsCount, 132u);  // B3.7: prior 67 + 8 red uncommon POWER cards
                                      // + the 16 red RARE cards (ids 76-91)
                                      // + the 20 colorless UNCOMMONs at ids
                                      // 92-111: 18 landed first, and B3.10c's
                                      // two (101 Forethought, 109 Purity)
                                      // filled the reserved interior ids once
                                      // the optional zero-to-N hand selection
                                      // existed to express them. The block is
                                      // now complete and gapless -- the note
                                      // below still applies to powers/monsters.
                                      // + B3.11 stage A's 4 colorless RARE rows
                                      // (112 Apotheosis, 116 Master of
                                      // Strategy, 120 Sadistic Nature, 124
                                      // Thinking Ahead)
                                      // + B3.11 stage B's 3 (121 Secret
                                      // Technique, 122 Secret Weapon, 126
                                      // Violence)
                                      // + B3.11 stage C's 5 (113 Chrysalis,
                                      // 115 Magnetism, 117 Mayhem, 118
                                      // Metamorphosis, 125 Transmutation); ids
                                      // 114/119/123 were reserved gaps owned by
                                      // the last B3.11 stage, so the row count
                                      // added 4 then 3 then 5, not 15
                                      // + B3.11 stage D's 3, which FILL exactly
                                      // those three gaps (114 Hand of Greed, 119
                                      // Panache, 123 The Bomb). With them the
                                      // colorless RARE block 112-126 is complete
                                      // and holds no gap at all.
                                      // + Wave-C track 2's Curse of the Bell
                                      // (127), Calling Bell's SPECIAL curse;
                                      // 128 was that track's published reserve
                                      // and is now issued.
                                      // + S2.03's 5 Act-2/3 SPECIAL cards (128
                                      // Apparition, 129 Bite, 130 J.A.X., 131
                                      // Ritual Dagger, 132 Necronomicurse); the
                                      // S2 block's 133 stays the reserve. All
                                      // five are CardRarity.SPECIAL, so none
                                      // joins any generated pool.
    // Counts are ROW counts, not max ids: ids are append-only and may be sparse,
    // so a reserved-but-unused id (powers 47, monsters 14) contributes no row.
    EXPECT_EQ(m::kPowersCount, 53u);  // + REGENERATE_MONSTER (91), the emerald
                                      // elite entry roll's arm 3, and
                                      // DUPLICATION (92), the potion's power.
                                      // B3.7 appends Evolve (26) + Fire Breathing (27);
                                      // Anger (33) is the Gremlin Nob's Bellow power.
                                      // Lagavulin adds none -- its Metallicize is the
                                      // pre-existing id 5 row.
                                      // + B3.26's Buffer (28) and Intangible (29),
                                      // both applied by a rare relic, not by a card
                                      // + B3.16's Angry (40), the Gremlin Warrior's
                                      // pre-battle power; 30-39 are other batches
                                      // + B3.21's Mode Shift (45) + Sharp Hide (46)
                                      // + the six red-rare card powers (48-53);
                                      // 47 stays a permanent gap, and Corruption
                                      // needed no new row (it is the id 11 the
                                      // hook framework already registered)
                                      // + B3.27's Confusion (59), applied by the
                                      // Snecko Eye boss relic, not by a card;
                                      // 54-58 are another batch's gap
                                      // + B3.15's Entangle (73) and Spore Cloud
                                      // (74), both monster-applied: the Red
                                      // Slaver's net and the Fungi Beast's
                                      // on-death release; 60-72 are other
                                      // batches' block
                                      // + the Looter's Thievery (75), a pure
                                      // marker (its only override is
                                      // updateDescription); 76 is that batch's
                                      // published reserve and stays unissued
                                      // + B3.10a's No Block (77), Panic Button's
                                      // debuff and the only power in the game
                                      // that overrides modifyBlockLast
                                      // + B3.10b's Shackled (78), Dark Shackles'
                                      // end-of-turn Strength restoration
                                      // + B3.11 stage C's Mayhem (81) and
                                      // Magnetism (82), the two colorless-RARE
                                      // POWER cards' start-of-turn generators;
                                      // 79-80 stay another stage's gap
                                      // + B3.11 stage D's Panache (83) and The
                                      // Bomb (84), the first two rows to use the
                                      // schema-6 PowerSlot.counter and (for The
                                      // Bomb) the registry's first `instanced`
                                      // row; 85-86 are that batch's published
                                      // RESERVE and stay unissued
                                      // + the Wave-C relic-tail stage's Vigor
                                      // (87, Akabeko) and Pen Nib (88), the two
                                      // relic-granted atDamageGive scalers; 89-90
                                      // were that stage's block and are RELEASED
                                      // unspent
                                      // + the discovery/duplication stage's
                                      // Duplication (92), the Duplication
                                      // Potion's any-card-type replay; 91 is a
                                      // sibling stage's allocation, untouched
    EXPECT_EQ(m::kMonstersCount, 25u); // + B3.14 four small/medium slimes
                                       // + B3.17 two large + B3.20 Slime Boss
                                       // + Gremlin Nob (12), Sentry (13),
                                       // Lagavulin (15)
                                       // + B3.16 five gremlins (ids 16-20)
                                       // + B3.21 The Guardian (id 21)
                                       // + B3.22 Hexaghost (id 22); powers
                                       // unchanged -- it applies only the
                                       // pre-existing Strength row
                                       // + B3.15's Blue Slaver (23), Red Slaver
                                       // (24) and Fungi Beast (25)
                                       // + the Looter (26), landed together
                                       // with the escape liveness predicate --
                                       // see the block comments in
                                       // registry/monsters.yaml
    EXPECT_EQ(m::kRelicsCount, 150u);  // 65 + B3.26's 28 rare + 17 shop + Odd Mushroom
                                       // + B3.27's 22 boss + 9 Act-1 event specials
                                       // + S2.03's 8 Act-2/3 event specials
                                       // (143-150). With them the registered
                                       // SPECIAL tier is closed at 19 rows.
    EXPECT_EQ(m::kPotionsCount, 33u);
    EXPECT_EQ(m::kEventsCount, 51u);  // the five canonical dungeon lists:
                                      // Exordium.initializeEventList's 11 (ids
                                      // 1-11) + initializeShrineList's 6 (12-17)
                                      // + AbstractDungeon.initializeSpecialOne-
                                      // TimeEventList's 14 (18-31) + S2.02's
                                      // TheCity.initializeEventList 13 (32-44)
                                      // and TheBeyond.initializeEventList 7
                                      // (45-51). Metadata-only rows: B4.10 owns
                                      // Act-1 list membership and selection,
                                      // B4.11-B4.13 the Act-1 bodies, S2.13 the
                                      // per-act rebuild, S2.31-S2.33 the
                                      // Act-2/3 bodies.
    EXPECT_EQ(m::kEncountersCount, 61u);  // Act 1 (B3.12): 20 generated-list
                                          // encounters plus Mushrooms' fixed
                                          // event group. + S2.01's Act 2 (22:
                                          // 5 weak / 8 strong / 3 elite /
                                          // 3 boss / 3 event) and Act 3 (18:
                                          // 3 / 8 / 3 / 3 / 1). "3 Darklings"
                                          // is TWO rows -- TheBeyond lists it
                                          // in both its weak and strong pool.
    EXPECT_EQ(m::kA20Count, 20u);     // B4.15: one row per ascension level 1..20
    // DERIVED, and therefore a count-guard site of BOTH the kCardsCount and the
    // kPowersCount families even though it names neither: any batch that moves
    // either constant has to move this sum too.
    EXPECT_EQ(m::kTotalCount, 525u);  // 132 + 53 + 25 + 150 + 33 + 51 + 61 + 20
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
// The skeleton cards carry no flags (generated flags 0), and each carries a
// DISTINCT upgraded program whose numbers are hand-derived from the decompiled
// Java upgrade() bodies (G6 campaign §8.0: the missing-block default silently
// made Strike+ deal 6). The generated kCardFlag* constants match the engine's
// CardFlag (pinned by the cards.hpp static_assert; re-checked here as a value
// equality for good measure).
TEST(RegistryGen, CardFlagsAndUpgradedProgramsForSkeleton) {
    namespace r = sts::registry;

    EXPECT_EQ(r::kCardFlagExhaust,
              sts::engine::card_flag_bit(sts::engine::CardFlag::EXHAUST));
    EXPECT_EQ(r::kCardFlagUnplayable,
              sts::engine::card_flag_bit(sts::engine::CardFlag::UNPLAYABLE));
    EXPECT_EQ(r::kCardFlagXcost,
              sts::engine::card_flag_bit(sts::engine::CardFlag::XCOST));

    // {id, base amounts, upgraded amounts} per step, hand-derived from the Java:
    //   1 STRIKE        [DMG 6]        -> [DMG 9]        Strike_Red.java:57-62
    //   2 DEFEND        [BLK 5]        -> [BLK 8]        Defend_Red.java:43-48
    //   3 BASH          [DMG 8, VUL 2] -> [DMG 10, VUL 3] Bash.java:54-60
    //   4 SHRUG_IT_OFF  [BLK 8, DRW 1] -> [BLK 11, DRW 1] ShrugItOff.java:43-48
    //   5 POMMEL_STRIKE [DMG 9, DRW 1] -> [DMG 10, DRW 2] PommelStrike.java:44-52
    struct Expect {
        int id;
        std::array<int32_t, 2> base;
        std::array<int32_t, 2> up;
        int steps;
    };
    const std::array<Expect, 5> table{{
        {1, {6, 0}, {9, 0}, 1},
        {2, {5, 0}, {8, 0}, 1},
        {3, {8, 2}, {10, 3}, 2},
        {4, {8, 1}, {11, 1}, 2},
        {5, {9, 1}, {10, 2}, 2},
    }};
    for (const Expect& e : table) {
        const r::CardDef* g = r::card_def(static_cast<r::CardId>(e.id));
        ASSERT_NE(g, nullptr) << "id " << e.id;
        EXPECT_EQ(g->flags, 0u) << "skeleton card " << e.id << " has no flags";
        // None of the five upgrades touch cost or flags (no upgradeBaseCost in
        // any of the five upgrade() bodies).
        EXPECT_EQ(g->upgraded_cost, g->base_cost) << "id " << e.id;
        EXPECT_EQ(g->upgraded_flags, g->flags) << "id " << e.id;
        ASSERT_EQ(g->step_count, e.steps) << "id " << e.id;
        ASSERT_EQ(g->upgraded_step_count, e.steps) << "id " << e.id;
        for (int s = 0; s < e.steps; ++s) {
            const auto si = static_cast<std::size_t>(s);
            // Same opcode sequence and APPLY_POWER packing, upgraded amounts.
            EXPECT_EQ(static_cast<int>(g->upgraded_steps[si].op),
                      static_cast<int>(g->steps[si].op))
                << "id " << e.id << " step " << s;
            EXPECT_EQ(g->upgraded_steps[si].extra, g->steps[si].extra)
                << "id " << e.id << " step " << s;
            EXPECT_EQ(g->steps[si].amount, e.base[si])
                << "id " << e.id << " step " << s;
            EXPECT_EQ(g->upgraded_steps[si].amount, e.up[si])
                << "id " << e.id << " step " << s;
        }
    }

    // STATUS/CURSE rows have no upgraded form (AbstractCard.canUpgrade,
    // AbstractCard.java:672-680, refuses both types); a row that authors no
    // `upgraded:` block still emits upgraded == base, and that default remains
    // pinned here on WOUND (id 24).
    {
        const r::CardDef* g = r::card_def(static_cast<r::CardId>(24));
        ASSERT_NE(g, nullptr);
        ASSERT_EQ(g->type, r::CardType::STATUS);
        EXPECT_EQ(g->upgraded_cost, g->base_cost);
        EXPECT_EQ(g->upgraded_flags, g->flags);
        ASSERT_EQ(g->upgraded_step_count, g->step_count);
        for (int s = 0; s < r::kMaxCardSteps; ++s) {
            const auto si = static_cast<std::size_t>(s);
            EXPECT_EQ(static_cast<int>(g->upgraded_steps[si].op),
                      static_cast<int>(g->steps[si].op)) << "step " << s;
            EXPECT_EQ(g->upgraded_steps[si].amount, g->steps[si].amount)
                << "step " << s;
        }
    }
}

// The guard behind the table above: a row for a card the game CAN upgrade
// (every type except STATUS and CURSE -- AbstractCard.canUpgrade,
// AbstractCard.java:672-680) that authors no `upgraded:` block must FAIL
// generation, not silently emit upgraded == base. That silent default is
// exactly how the G6 §8.0 Strike+/Defend+ divergence shipped.
TEST(RegistryGen, UpgradableCardWithoutUpgradedBlockFailsLoudly) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_no_upgrade_registry");
    {
        std::ofstream cards(bad_reg / "cards.yaml", std::ios::app);
        cards << "\n- id: 902\n  name: SYNTH_NO_UPGRADE\n"
                 "  game_id: \"SynthNoUpgrade\"\n"
                 "  color: RED\n  rarity: COMMON\n"
                 "  type: ATTACK\n  cost: 1\n  target: ENEMY\n  flags: []\n"
                 "  provenance: \"synthetic missing-upgraded negative test\"\n"
                 "  effects:\n"
                 "    - {op: DAMAGE, target: CARD_TARGET, amount: 5}\n";
    }

    const fs::path out = scratch / "bad_no_upgrade_out";
    const fs::path err = scratch / "bad_no_upgrade_err.txt";
    fs::remove_all(out);
    EXPECT_NE(run_generator(bad_reg.string(), out.string(), err.string()), 0)
        << "generator must reject an upgradable card with no `upgraded:` block";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("cards.yaml"), std::string::npos) << msg;
    EXPECT_NE(msg.find("SYNTH_NO_UPGRADE"), std::string::npos) << msg;
    EXPECT_NE(msg.find("upgraded"), std::string::npos) << msg;
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
        // X-cost + exhaust; base DAMAGE 7 (all enemies), upgraded DAMAGE 99.
        // The id must not collide with a real cards.yaml row, and the loader
        // rejects a duplicate outright. It was 100 and picked as "past the real
        // card ids 1-10" -- which stopped being true the moment the card roster
        // grew past 100 rows, exactly the rot conventions §8 warns about. 901 is
        // in the same deliberately-far band the other synthetic rows in this file
        // use (900), chosen so no content batch will ever reach it.
        std::ofstream cards(reg / "cards.yaml", std::ios::app);
        cards << "\n- id: 901\n  name: SYNTH_XCOST\n  game_id: \"SynthX\"\n"
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

// --- 7. Per-domain op allowlist: no silent mispack --------------------------
//
// Before the step-parser unification each domain carried its own copy of the
// effect-step parser, and an op that had no `extra`-packing branch in THAT copy
// still passed the OPCODES membership check -- so a relic or potion author who
// wrote CHOOSE_CARD got `extra = 0` silently, and a MAKE_CARD outside a card
// program got a CardPile that no queue helper splits out. In a project whose
// premise is bit-exactness a mispack must be LOUD. These tests pin that: an op
// the calling domain cannot queue fails generation, naming the domain, the row
// and the op.

TEST(RegistryGen, UnsupportedOpInRelicDomainFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_relic_op_registry");
    {
        // CHOOSE_CARD is completed from the played card's instance at queue time
        // (card_play.cpp stamps the source pool index into `tgt` for the
        // discard-source kind); relic_hooks.cpp has no such context, so a relic
        // may not author it. Pre-fix this row generated `extra = 0` and no error.
        std::ofstream relics(bad_reg / "relics.yaml", std::ios::app);
        relics << "\n- id: 900\n  name: SYNTH_BAD_OP_RELIC\n"
                  "  game_id: \"Synth Bad Op Relic\"\n  tier: EVENT\n"
                  "  hooks:\n    at_battle_start:\n"
                  "      - {op: CHOOSE_CARD, target: SELF, amount: 1, "
                  "choose: exhaust}\n"
                  "  provenance: \"synthetic unsupported-op negative test\"\n";
    }

    const fs::path out = scratch / "bad_relic_op_out";
    const fs::path err = scratch / "bad_relic_op_err.txt";
    fs::remove_all(out);
    EXPECT_NE(run_generator(bad_reg.string(), out.string(), err.string()), 0)
        << "generator should reject CHOOSE_CARD in a relic hook program";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("relics.yaml"), std::string::npos) << msg;       // domain
    EXPECT_NE(msg.find("SYNTH_BAD_OP_RELIC"), std::string::npos) << msg;  // row
    EXPECT_NE(msg.find("CHOOSE_CARD"), std::string::npos) << msg;       // op
    EXPECT_NE(msg.find("does not support"), std::string::npos) << msg;
}

TEST(RegistryGen, UnsupportedOpInPotionDomainFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_potion_op_registry");
    {
        // MAKE_CARD packs the destination CardPile into extra bits 16-23; only
        // card_play.cpp / monster_dispatch.cpp split it back into the queue
        // item's `src`, so a potion authoring it would create the card into
        // pile 0 (HAND) regardless of what the row asked for.
        std::ofstream potions(bad_reg / "potions.yaml", std::ios::app);
        potions << "\n- id: 900\n  name: SYNTH_BAD_OP_POTION\n"
                   "  game_id: \"Synth Bad Op Potion\"\n  rarity: COMMON\n"
                   "  potency: 1\n"
                   "  effects:\n"
                   "    - {op: MAKE_CARD, target: SELF, amount: 1, "
                   "card: WOUND, pile: DISCARD}\n"
                   "  provenance: \"synthetic unsupported-op negative test\"\n";
    }

    const fs::path out = scratch / "bad_potion_op_out";
    const fs::path err = scratch / "bad_potion_op_err.txt";
    fs::remove_all(out);
    EXPECT_NE(run_generator(bad_reg.string(), out.string(), err.string()), 0)
        << "generator should reject MAKE_CARD in a potion effect program";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("potions.yaml"), std::string::npos) << msg;
    EXPECT_NE(msg.find("SYNTH_BAD_OP_POTION"), std::string::npos) << msg;
    EXPECT_NE(msg.find("MAKE_CARD"), std::string::npos) << msg;
    EXPECT_NE(msg.find("does not support"), std::string::npos) << msg;
}

TEST(RegistryGen, EngineEmittedOpInCardDomainFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_card_op_registry");
    {
        // SUICIDE is emitted natively by the split framework (monster_slime_
        // large.cpp / monster_slime_boss.cpp) and is pinned in the Opcode enum
        // only for the cards.hpp drift check -- never authorable.
        std::ofstream cards(bad_reg / "cards.yaml", std::ios::app);
        cards << "\n- id: 900\n  name: SYNTH_BAD_OP_CARD\n"
                 "  game_id: \"SynthBadOp\"\n  type: ATTACK\n  cost: 1\n"
                 "  target: ENEMY\n"
                 "  provenance: \"synthetic unsupported-op negative test\"\n"
                 "  effects:\n"
                 "    - {op: SUICIDE, target: CARD_TARGET, amount: 0}\n";
    }

    const fs::path out = scratch / "bad_card_op_out";
    const fs::path err = scratch / "bad_card_op_err.txt";
    fs::remove_all(out);
    EXPECT_NE(run_generator(bad_reg.string(), out.string(), err.string()), 0)
        << "generator should reject the natively-emitted SUICIDE op in a card";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("cards.yaml"), std::string::npos) << msg;
    EXPECT_NE(msg.find("SYNTH_BAD_OP_CARD"), std::string::npos) << msg;
    EXPECT_NE(msg.find("SUICIDE"), std::string::npos) << msg;
    EXPECT_NE(msg.find("does not support"), std::string::npos) << msg;
}

TEST(RegistryGen, NativeCardKeyFailsLoudly) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_native_card_registry");
    {
        std::ofstream cards(bad_reg / "cards.yaml", std::ios::app);
        cards << "\n- id: 900\n  name: SYNTH_NATIVE_CARD\n"
                 "  game_id: \"SynthNativeCard\"\n"
                 "  color: COLORLESS\n  rarity: UNCOMMON\n"
                 "  type: SKILL\n  cost: 0\n  target: SELF\n"
                 "  flags: []\n  native: true\n"
                 "  provenance: \"synthetic native-card negative test\"\n"
                 "  effects: []\n";
    }

    const fs::path out = scratch / "bad_native_card_out";
    const fs::path err = scratch / "bad_native_card_err.txt";
    fs::remove_all(out);
    EXPECT_NE(run_generator(bad_reg.string(), out.string(), err.string()), 0)
        << "cards have no native dispatch and must reject the key";
    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("cards.yaml"), std::string::npos) << msg;
    EXPECT_NE(msg.find("SYNTH_NATIVE_CARD"), std::string::npos) << msg;
    EXPECT_NE(msg.find("unsupported key 'native'"), std::string::npos) << msg;
}

// The unification's other half: the packings that used to exist in only ONE
// domain's copy now apply everywhere the op is allowed. A power hook DAMAGE
// step's damage_type was ignored before (extra = 0 == NORMAL) even though the
// card and relic copies honoured it; a relic REMOVE_POWER got no PowerId.
TEST(RegistryGen, SharedPackingsApplyAcrossDomains) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path reg = clone_registry(scratch, "shared_packing_registry");
    {
        std::ofstream powers(reg / "powers.yaml", std::ios::app);
        powers << "\n- id: 900\n  name: SYNTH_THORNS_POWER\n"
                  "  game_id: \"SynthThornsPower\"\n  type: BUFF\n"
                  "  hooks:\n    at_end_of_turn:\n"
                  "      - {op: DAMAGE, target: SELF, amount: 3, "
                  "damage_type: THORNS}\n"
                  "  provenance: \"synthetic cross-domain packing test\"\n";
    }
    {
        std::ofstream relics(reg / "relics.yaml", std::ios::app);
        relics << "\n- id: 900\n  name: SYNTH_REMOVE_POWER_RELIC\n"
                  "  game_id: \"Synth Remove Power Relic\"\n  tier: EVENT\n"
                  "  hooks:\n    at_turn_start:\n"
                  "      - {op: REMOVE_POWER, target: SELF, amount: 0, "
                  "power: VULNERABLE}\n"
                  "  provenance: \"synthetic cross-domain packing test\"\n";
    }

    const fs::path out = scratch / "shared_packing_out";
    const fs::path err = scratch / "shared_packing_err.txt";
    fs::remove_all(out);
    ASSERT_EQ(run_generator(reg.string(), out.string(), err.string()), 0)
        << read_text(err);

    // THORNS == DamageType 1 (interp.hpp make_damage_flags), packed into extra.
    const std::string ptxt = read_text(out / "sts/registry/power_table.hpp");
    const auto ppos = ptxt.find("kSynthThornsPowerPower");
    ASSERT_NE(ppos, std::string::npos) << "synthetic power literal missing";
    const std::string plit = ptxt.substr(ppos, 800);
    EXPECT_NE(plit.find("{Opcode::DAMAGE, 3, 1, StepTarget::SELF}"),
              std::string::npos)
        << "power DAMAGE damage_type must pack like the card/relic copies: "
        << plit;

    // REMOVE_POWER packs the PowerId in extra's low 16 bits, exactly as
    // APPLY_POWER does (op_remove_power reads the same packing).
    const std::string rtxt = read_text(out / "sts/registry/relic_table.hpp");
    const auto rpos = rtxt.find("kSynthRemovePowerRelicRelic");
    ASSERT_NE(rpos, std::string::npos) << "synthetic relic literal missing";
    const std::string rlit = rtxt.substr(rpos, 800);
    EXPECT_NE(rlit.find("Opcode::REMOVE_POWER"), std::string::npos) << rlit;
    EXPECT_EQ(rlit.find("{Opcode::REMOVE_POWER, 0, 0,"), std::string::npos)
        << "relic REMOVE_POWER must carry a PowerId, not extra = 0: " << rlit;
}

// --- 7. Native dispatch lists ------------------------------------------------
//
// The native power/relic dispatch tables used to be hand-written switches in
// power_hooks.cpp / relic_hooks.cpp. They are now generated: every registry row
// marked `native: true` contributes one X(<Id>, <handler>) entry to
// STS_REGISTRY_NATIVE_POWERS / STS_REGISTRY_NATIVE_RELICS, and the hook TUs
// expand that list into both the extern declarations and the id -> handler
// switch. These tests pin the two properties the engine relies on:
//
//   * COVERAGE  -- the list is exactly the set of `native: true` rows. Not a
//                  subset (a missing row would be a silently dead power/relic)
//                  and not a superset (a non-native row must never route to the
//                  escape hatch).
//   * NAMING    -- the handler symbol is `<domain>_native_<lowercased row name>`.
//                  The convention is load-bearing: the generated declaration is
//                  what odr-uses the body, so a body defined under any other
//                  name fails to link.
//
// (The link-error property itself is a build-level property, not observable from
// inside a test binary that has already linked; it is exercised by deliberately
// marking a row native with no body and confirming the undefined reference.)

namespace {

std::string ascii_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
    }
    return out;
}

// Every enum value a scoped id enum can hold is far below this; power/relic ids
// are small and dense, so a linear probe over the id space enumerates the rows.
constexpr int kIdProbeLimit = 1024;

}  // namespace

TEST(RegistryGen, NativePowerDispatchListCoversExactlyNativeRows) {
    std::vector<int> from_macro;
#define STS_TEST_NATIVE_POWER(ID, FN) \
    from_macro.push_back(static_cast<int>(sts::registry::PowerId::ID));
    STS_REGISTRY_NATIVE_POWERS(STS_TEST_NATIVE_POWER)
#undef STS_TEST_NATIVE_POWER

    std::vector<int> from_table;
    for (int i = 0; i < kIdProbeLimit; ++i) {
        const sts::registry::PowerDef* d =
            sts::registry::power_def(static_cast<sts::registry::PowerId>(i));
        if (d != nullptr && d->native) { from_table.push_back(i); }
    }
    ASSERT_FALSE(from_table.empty()) << "no native power rows -- probe is wrong";

    std::sort(from_macro.begin(), from_macro.end());
    std::sort(from_table.begin(), from_table.end());
    EXPECT_EQ(from_macro, from_table)
        << "STS_REGISTRY_NATIVE_POWERS must list exactly the `native: true` "
           "powers.yaml rows";
}

TEST(RegistryGen, PowerPrioritiesMirrorTheJavaCtors) {
    // AbstractPower.priority defaults to 5 (AbstractPower.java:66); a ctor
    // overrides it. The registry mirrors every override because
    // op_apply_power's ApplyPowerAction re-sort (ApplyPowerAction.java:167,
    // AbstractPower.compareTo :366-368) orders the live power list by it.
    // The complete override set for registered powers, from a full grep of
    // `this.priority` across the decompiled powers directory: Weak 99
    // (WeakPower.java:40), Frail 10 (FrailPower.java:29), IntangiblePlayer 75
    // (IntangiblePlayerPower.java:31), Confusion 0 (ConfusionPower.java:30),
    // PenNib 6 (PenNibPower.java:36).
    namespace r = sts::registry;
    EXPECT_EQ(r::power_def(r::PowerId::WEAK)->priority, 99);
    EXPECT_EQ(r::power_def(r::PowerId::FRAIL)->priority, 10);
    EXPECT_EQ(r::power_def(r::PowerId::INTANGIBLE)->priority, 75);
    EXPECT_EQ(r::power_def(r::PowerId::CONFUSION)->priority, 0);
    EXPECT_EQ(r::power_def(r::PowerId::PEN_NIB)->priority, 6);
    // Vigor's ctor sets NO priority (VigorPower.java:30-38), so it takes the
    // default and belongs in the sweep below rather than in the list above --
    // which is exactly why ((base + Str + Vigor) * 2) is the game's grouping.
    EXPECT_EQ(r::power_def(r::PowerId::VIGOR)->priority,
              r::kDefaultPowerPriority);
    // Every other row carries the default.
    for (int i = 0; i < kIdProbeLimit; ++i) {
        const auto id = static_cast<r::PowerId>(i);
        if (id == r::PowerId::WEAK || id == r::PowerId::FRAIL ||
            id == r::PowerId::INTANGIBLE || id == r::PowerId::CONFUSION ||
            id == r::PowerId::PEN_NIB) {
            continue;
        }
        const r::PowerDef* d = r::power_def(id);
        if (d != nullptr) {
            EXPECT_EQ(d->priority, r::kDefaultPowerPriority)
                << "PowerId " << i
                << " carries a non-default priority the Java does not";
        }
    }
}

TEST(RegistryGen, NativeRelicDispatchListCoversExactlyNativeRows) {
    std::vector<int> from_macro;
#define STS_TEST_NATIVE_RELIC(ID, FN) \
    from_macro.push_back(static_cast<int>(sts::registry::RelicId::ID));
    STS_REGISTRY_NATIVE_RELICS(STS_TEST_NATIVE_RELIC)
#undef STS_TEST_NATIVE_RELIC

    std::vector<int> from_table;
    for (const sts::registry::RelicDef* d : sts::registry::kRelicDefs) {
        if (d->native) { from_table.push_back(static_cast<int>(d->id)); }
    }
    ASSERT_FALSE(from_table.empty()) << "no native relic rows -- probe is wrong";

    std::sort(from_macro.begin(), from_macro.end());
    std::sort(from_table.begin(), from_table.end());
    EXPECT_EQ(from_macro, from_table)
        << "STS_REGISTRY_NATIVE_RELICS must list exactly the `native: true` "
           "relics.yaml rows";
}

TEST(RegistryGen, NativeDispatchHandlerNamesFollowTheRowNameConvention) {
    // PowerId::COMBUST -> power_native_combust; RelicId::BLUE_CANDLE ->
    // relic_native_blue_candle. Stringize both halves of each X() entry and
    // check the derivation, so a generator change that renames handlers is
    // caught here rather than as an undefined reference in the engine link.
#define STS_TEST_POWER_NAME(ID, FN) \
    EXPECT_EQ(std::string(#FN), "power_native_" + ascii_lower(#ID));
    STS_REGISTRY_NATIVE_POWERS(STS_TEST_POWER_NAME)
#undef STS_TEST_POWER_NAME
#define STS_TEST_RELIC_NAME(ID, FN) \
    EXPECT_EQ(std::string(#FN), "relic_native_" + ascii_lower(#ID));
    STS_REGISTRY_NATIVE_RELICS(STS_TEST_RELIC_NAME)
#undef STS_TEST_RELIC_NAME
}

// --- 8. B4.10/B4.11 event registry: identities + native-body metadata --------
//
// events.yaml is metadata-only (`native: true` on every row): B4.10 owns Act-1
// list membership and selection, B4.11-B4.13 the Act-1 bodies, S2.13 the
// per-act list rebuild and S2.31-S2.33 the Act-2/3 bodies. So the only thing
// there IS to pin is the data -- that the ids reproduce the five canonical
// dungeon lists in Java insertion order, that the game_id join keys round-trip,
// and that this domain refuses a reused id like every other.
//
// Determinism is deliberately NOT re-tested per-domain here. Deterministic-
// ByteIdentical above already generates the real registry twice and byte-
// compares ids.hpp, game_ids.hpp and event_table.hpp -- every header the events
// domain contributes to. A per-domain determinism case would be a second copy
// of that one shared test, which is not the shape the other seven domains use.
//
// Every expected value below is hand-carried from the cited Java, NOT read back
// from the generator.

namespace {

struct EventRow {
    int id;
    sts::registry::EventId sym;
    const char* game_id;
};

// The three canonical lists, in add() order:
//   Exordium.initializeEventList          (Exordium.java:223-236)        -- 11
//   Exordium.initializeShrineList         (Exordium.java:238-246)        --  6
//   AbstractDungeon.initializeSpecialOneTimeEventList
//                                         (AbstractDungeon.java:1340-1358) -- 14
//
// The special list is 14 entries counting NoteForYourself, whose add at
// :1351-1353 is guarded by isNoteForYourselfAvailable (:1360-1378). The guard
// wraps the add ITSELF, so there is no placeholder slot: when NFY is absent the
// runtime list is length 13 and SecretPortal sits at runtime index 9, not 10.
// The registry id is identity only and never moves -- which is precisely why
// the conditional entry can hold a dense id here.
// S2.02 appended two more canonical lists after the special block:
//   TheCity.initializeEventList          (TheCity.java:185-198)          -- 13
//   TheBeyond.initializeEventList        (TheBeyond.java:179-186)        --  7
// Their ids stay dense and ascending in add() order for the same reason the
// first three blocks do. The per-act membership/gate METADATA those rows carry
// is pinned in tests/act_event_lists_test.cpp; this table pins identity only.
constexpr std::array<EventRow, 51> kCanonicalEvents = {{
    // -- Exordium.initializeEventList, positions 0-10 --
    {1, sts::registry::EventId::BIG_FISH, "Big Fish"},
    {2, sts::registry::EventId::THE_CLERIC, "The Cleric"},
    {3, sts::registry::EventId::DEAD_ADVENTURER, "Dead Adventurer"},
    {4, sts::registry::EventId::GOLDEN_IDOL, "Golden Idol"},
    {5, sts::registry::EventId::GOLDEN_WING, "Golden Wing"},
    {6, sts::registry::EventId::WORLD_OF_GOOP, "World of Goop"},
    {7, sts::registry::EventId::LIARS_GAME, "Liars Game"},
    {8, sts::registry::EventId::LIVING_WALL, "Living Wall"},
    {9, sts::registry::EventId::MUSHROOMS, "Mushrooms"},
    {10, sts::registry::EventId::SCRAP_OOZE, "Scrap Ooze"},
    {11, sts::registry::EventId::SHINING_LIGHT, "Shining Light"},
    // -- Exordium.initializeShrineList, positions 0-5 --
    {12, sts::registry::EventId::MATCH_AND_KEEP, "Match and Keep!"},
    {13, sts::registry::EventId::GOLDEN_SHRINE, "Golden Shrine"},
    {14, sts::registry::EventId::TRANSMORGRIFIER, "Transmorgrifier"},
    {15, sts::registry::EventId::PURIFIER, "Purifier"},
    {16, sts::registry::EventId::UPGRADE_SHRINE, "Upgrade Shrine"},
    {17, sts::registry::EventId::WHEEL_OF_CHANGE, "Wheel of Change"},
    // -- AbstractDungeon.initializeSpecialOneTimeEventList, positions 0-13
    //    (NFY-present numbering; see the note above) --
    {18, sts::registry::EventId::ACCURSED_BLACKSMITH, "Accursed Blacksmith"},
    {19, sts::registry::EventId::BONFIRE_ELEMENTALS, "Bonfire Elementals"},
    {20, sts::registry::EventId::DESIGNER, "Designer"},
    {21, sts::registry::EventId::DUPLICATOR, "Duplicator"},
    {22, sts::registry::EventId::FACE_TRADER, "FaceTrader"},
    {23, sts::registry::EventId::FOUNTAIN_OF_CLEANSING, "Fountain of Cleansing"},
    {24, sts::registry::EventId::KNOWING_SKULL, "Knowing Skull"},
    {25, sts::registry::EventId::LAB, "Lab"},
    {26, sts::registry::EventId::NLOTH, "N'loth"},
    {27, sts::registry::EventId::NOTE_FOR_YOURSELF, "NoteForYourself"},
    {28, sts::registry::EventId::SECRET_PORTAL, "SecretPortal"},
    {29, sts::registry::EventId::THE_JOUST, "The Joust"},
    {30, sts::registry::EventId::WE_MEET_AGAIN, "WeMeetAgain"},
    {31, sts::registry::EventId::THE_WOMAN_IN_BLUE, "The Woman in Blue"},
    // -- TheCity.initializeEventList, positions 0-12 --
    {32, sts::registry::EventId::ADDICT, "Addict"},
    {33, sts::registry::EventId::BACK_TO_BASICS, "Back to Basics"},
    {34, sts::registry::EventId::BEGGAR, "Beggar"},
    {35, sts::registry::EventId::COLOSSEUM, "Colosseum"},
    {36, sts::registry::EventId::CURSED_TOME, "Cursed Tome"},
    {37, sts::registry::EventId::DRUG_DEALER, "Drug Dealer"},
    {38, sts::registry::EventId::FORGOTTEN_ALTAR, "Forgotten Altar"},
    {39, sts::registry::EventId::GHOSTS, "Ghosts"},
    {40, sts::registry::EventId::MASKED_BANDITS, "Masked Bandits"},
    {41, sts::registry::EventId::NEST, "Nest"},
    {42, sts::registry::EventId::THE_LIBRARY, "The Library"},
    {43, sts::registry::EventId::THE_MAUSOLEUM, "The Mausoleum"},
    {44, sts::registry::EventId::VAMPIRES, "Vampires"},
    // -- TheBeyond.initializeEventList, positions 0-6 --
    {45, sts::registry::EventId::FALLING, "Falling"},
    {46, sts::registry::EventId::MIND_BLOOM, "MindBloom"},
    {47, sts::registry::EventId::THE_MOAI_HEAD, "The Moai Head"},
    {48, sts::registry::EventId::MYSTERIOUS_SPHERE, "Mysterious Sphere"},
    {49, sts::registry::EventId::SENSORY_STONE, "SensoryStone"},
    {50, sts::registry::EventId::TOMB_OF_LORD_RED_MASK, "Tomb of Lord Red Mask"},
    {51, sts::registry::EventId::WINDING_HALLS, "Winding Halls"},
}};

}  // namespace

TEST(RegistryGen, EventIdsFollowCanonicalJavaListOrder) {
    namespace r = sts::registry;
    ASSERT_EQ(kCanonicalEvents.size(), r::manifest::kEventsCount)
        << "a row landed in events.yaml without being added to this table";

    std::vector<int> ids;
    for (std::size_t i = 0; i < kCanonicalEvents.size(); ++i) {
        const EventRow& row = kCanonicalEvents[i];
        // Canonical order: the ids run 1..31 dense and ascending in exactly the
        // order the three Java lists add their entries, so canonical position i
        // holds id i+1. A row inserted mid-file, or a gap, breaks this.
        EXPECT_EQ(row.id, static_cast<int>(i) + 1)
            << "canonical position " << i << " (" << row.game_id << ")";
        EXPECT_EQ(static_cast<int>(row.sym), row.id)
            << "generated EventId for " << row.game_id;
        EXPECT_EQ(r::event_game_id(row.sym), row.game_id) << "id " << row.id;
        EXPECT_EQ(r::event_from_game_id(row.game_id), row.sym) << "id " << row.id;
        ids.push_back(row.id);
    }

    // Id uniqueness, read off the GENERATED enum rather than off the YAML.
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end())
        << "EventId values must be unique";
    EXPECT_EQ(ids.front(), 1) << "0 is reserved for EventId::NONE";
    EXPECT_EQ(ids.back(), 51);

    // Block boundaries, so a row appended to the wrong list is caught even when
    // the overall sequence stays dense.
    EXPECT_EQ(static_cast<int>(r::EventId::SHINING_LIGHT), 11)
        << "last initializeEventList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::MATCH_AND_KEEP), 12)
        << "first initializeShrineList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::WHEEL_OF_CHANGE), 17)
        << "last initializeShrineList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::ACCURSED_BLACKSMITH), 18)
        << "first initializeSpecialOneTimeEventList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::THE_WOMAN_IN_BLUE), 31)
        << "last initializeSpecialOneTimeEventList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::ADDICT), 32)
        << "first TheCity.initializeEventList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::VAMPIRES), 44)
        << "last TheCity.initializeEventList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::FALLING), 45)
        << "first TheBeyond.initializeEventList entry";
    EXPECT_EQ(static_cast<int>(r::EventId::WINDING_HALLS), 51)
        << "last TheBeyond.initializeEventList entry";

    // NoteForYourself holds a real, dense id between N'loth and SecretPortal
    // even though its runtime slot is conditional: the conditionality lives in
    // the list build (AbstractDungeon.java:1351-1353), never in the numbering.
    EXPECT_EQ(static_cast<int>(r::EventId::NLOTH), 26);
    EXPECT_EQ(static_cast<int>(r::EventId::NOTE_FOR_YOURSELF), 27);
    EXPECT_EQ(static_cast<int>(r::EventId::SECRET_PORTAL), 28);

    // game_id uniqueness: the translator joins on this string, so two rows
    // sharing one would silently make the join ambiguous -- event_from_game_id
    // is a linear if-chain and would return whichever row the generator emitted
    // first.
    std::vector<std::string> gids;
    gids.reserve(kCanonicalEvents.size());
    for (const EventRow& row : kCanonicalEvents) { gids.emplace_back(row.game_id); }
    std::sort(gids.begin(), gids.end());
    EXPECT_EQ(std::adjacent_find(gids.begin(), gids.end()), gids.end())
        << "game_id strings must be unique -- they are the translator join key";
}

// Scope: the S1 block, ids 1-31. S2.02's ids 32-51 are identity rows with no
// body (implemented false, zero screens) and are pinned in
// tests/act_event_lists_test.cpp instead, so this case keeps saying exactly
// what it said about the Act-1 rows.
TEST(RegistryGen, ExordiumEventsCarryAuditedNativeBodyMetadata) {
    namespace r = sts::registry;
    // The only unimplemented rows are the six one-time specials whose
    // getShrine gates cannot be met in Act 1 (AbstractDungeon.java:1894-1933):
    // Designer, Duplicator, Knowing Skull, N'loth, SecretPortal, The Joust.
    const auto act_gated_out = [](uint16_t raw) {
        return raw == 20 || raw == 21 || raw == 24 || raw == 26 ||
               raw == 28 || raw == 29;
    };
    for (uint16_t raw = 1; raw <= 31; ++raw) {
        const auto id = static_cast<r::EventId>(raw);
        const r::EventDef* def = r::event_def(id);
        ASSERT_NE(def, nullptr) << raw;
        EXPECT_TRUE(def->native) << raw;
        EXPECT_EQ(def->implemented, !act_gated_out(raw)) << raw;
        if (!act_gated_out(raw)) {
            // Lab is the one single-screen dialog: its only button opens the
            // potion reward screen and the event is over (Lab.java:46-61).
            EXPECT_GE(def->screen_count,
                      raw == static_cast<uint16_t>(r::EventId::LAB) ? 1 : 2)
                << raw;
        } else {
            EXPECT_EQ(def->screen_count, 0) << raw;
            EXPECT_EQ(def->a15_change_count, 0) << raw;
        }
    }
    EXPECT_EQ(r::event_def(r::EventId::MATCH_AND_KEEP)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::GOLDEN_SHRINE)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::TRANSMORGRIFIER)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::PURIFIER)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::UPGRADE_SHRINE)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::WHEEL_OF_CHANGE)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::ACCURSED_BLACKSMITH)->a15_change_count,
              0);
    EXPECT_EQ(r::event_def(r::EventId::BONFIRE_ELEMENTALS)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::FACE_TRADER)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::FOUNTAIN_OF_CLEANSING)->a15_change_count,
              0);
    EXPECT_EQ(r::event_def(r::EventId::LAB)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::NOTE_FOR_YOURSELF)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::WE_MEET_AGAIN)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::THE_WOMAN_IN_BLUE)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::BIG_FISH)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::THE_CLERIC)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::DEAD_ADVENTURER)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::GOLDEN_IDOL)->a15_change_count, 2);
    EXPECT_EQ(r::event_def(r::EventId::GOLDEN_WING)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::WORLD_OF_GOOP)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::LIARS_GAME)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::LIVING_WALL)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::MUSHROOMS)->a15_change_count, 0);
    EXPECT_EQ(r::event_def(r::EventId::SCRAP_OOZE)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(r::EventId::SHINING_LIGHT)->a15_change_count, 1);
    EXPECT_EQ(r::event_def(static_cast<r::EventId>(0xFFFF)), nullptr);
}

TEST(RegistryGen, DuplicateEventIdFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_event_registry");
    {
        // Reuse id 1 (BIG_FISH). The row is otherwise well-formed -- the
        // mandatory game_id and provenance are both present -- so the loader
        // reaches the duplicate-id gate rather than tripping an earlier
        // missing-field check and passing for the wrong reason.
        std::ofstream events(bad_reg / "events.yaml", std::ios::app);
        events << "\n- id: 1\n  name: DUPLICATE_BIG_FISH\n"
                  "  game_id: \"Dup Big Fish\"\n"
                  "  provenance: \"synthetic duplicate for the negative test\"\n"
                  "  native: true\n";
    }

    const fs::path out = scratch / "bad_event_out";
    const fs::path err = scratch / "bad_event_err.txt";
    fs::remove_all(out);
    const int status = run_generator(bad_reg.string(), out.string(), err.string());
    EXPECT_NE(status, 0) << "generator should fail on a duplicate event id";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("events.yaml"), std::string::npos) << msg;  // the domain
    EXPECT_NE(msg.find("duplicate"), std::string::npos) << msg;
    EXPECT_NE(msg.find("DUPLICATE_BIG_FISH"), std::string::npos) << msg;  // new row
    EXPECT_NE(msg.find("BIG_FISH"), std::string::npos) << msg;  // what it collides with
}

// --- S2.01: the composition-program grammar must fail loudly ----------------
// The Acts 2-3 rows needed no new CompOp -- spawnShapes' shared 6-slot 3-or-4
// draw is exactly the existing POOL node, spawnGremlin is a count-1 POOL per
// call (it rebuilds its pool, so the two draws are with replacement), and
// getAncientShape is a count-1 POOL over the three shapes in index order. That
// makes the "unknown node kind" gate the thing standing between a typo and a
// silently-dropped miscRng draw, so it gets its own negative test.

TEST(RegistryGen, UnknownCompositionNodeKindFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_comp_node_registry");
    {
        std::ofstream encs(bad_reg / "encounters.yaml", std::ios::app);
        encs << "\n- id: 900\n  name: SYNTH_BAD_NODE\n"
                "  game_id: \"Synth Bad Node\"\n  act: 3\n  pool: EVENT\n"
                "  weight: 0.0\n  program:\n"
                "    - {summon: \"Darkling\"}\n"
                "  provenance: \"synthetic unknown-node negative test\"\n";
    }

    const fs::path out = scratch / "bad_comp_node_out";
    const fs::path err = scratch / "bad_comp_node_err.txt";
    fs::remove_all(out);
    EXPECT_NE(run_generator(bad_reg.string(), out.string(), err.string()), 0)
        << "generator should reject an unknown composition node kind";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("encounters.yaml"), std::string::npos) << msg;   // domain
    EXPECT_NE(msg.find("Synth Bad Node"), std::string::npos) << msg;    // row
    EXPECT_NE(msg.find("summon"), std::string::npos) << msg;            // the node
    EXPECT_NE(msg.find("unknown program node kind"), std::string::npos) << msg;
}

// The exclusion cross-check gained ONE escape hatch in S2.01 -- a row may
// exclude its own key, which is how TheBeyond.java:135-138's inert "Orb Walker"
// self-exclusion is modeled. Everything else must still fail: a key that names
// no strong encounter in the row's act is a typo, and a silently-dropped
// exclusion changes the first-strong rejection loop's draw count.
TEST(RegistryGen, UnknownEncounterExclusionKeyFailsWithClearError) {
    const fs::path scratch = fs::path(kScratchDir);
    const fs::path bad_reg = clone_registry(scratch, "bad_exclusion_registry");
    {
        std::ofstream encs(bad_reg / "encounters.yaml", std::ios::app);
        encs << "\n- id: 901\n  name: SYNTH_BAD_EXCLUSION\n"
                "  game_id: \"Synth Bad Exclusion\"\n  act: 2\n  pool: WEAK\n"
                "  weight: 2.0\n  excludes: [\"Sentry and Spear\"]\n"
                "  program:\n    - {emit: \"Chosen\"}\n"
                "  provenance: \"synthetic bad-exclusion negative test\"\n";
    }

    const fs::path out = scratch / "bad_exclusion_out";
    const fs::path err = scratch / "bad_exclusion_err.txt";
    fs::remove_all(out);
    EXPECT_NE(run_generator(bad_reg.string(), out.string(), err.string()), 0)
        << "generator should reject an exclusion naming no act-2 strong key";

    const std::string msg = read_text(err);
    EXPECT_NE(msg.find("error:"), std::string::npos) << msg;
    EXPECT_NE(msg.find("encounters.yaml"), std::string::npos) << msg;
    EXPECT_NE(msg.find("Sentry and Spear"), std::string::npos) << msg;
    EXPECT_NE(msg.find("inert self-exclusion"), std::string::npos) << msg;
}
