// T0.5 -- the total-byte classification tripwire (docs/training-plan.md §2.6b).
//
// byte_class.hpp holds the table and the `static_assert`s that fire at BUILD
// time. This file is the other half: the RUNTIME message (which byte range,
// between which members) and -- the part that makes the mechanism trustworthy --
// the NEGATIVE TESTS.
//
// WHY NEGATIVE TESTS ARE THE POINT HERE. A tiling check that is wrong in the
// permissive direction passes silently forever: it would sit green in CI while
// classifying nothing, and the first sign of trouble would be a training run
// that never saw a field. So the same `check_tiling` the real table uses is
// handed, in turn: a struct that grew by a scratch field, a table with a row
// removed, and a table with a row moved backwards. Each must fire, and must
// name the byte range and the neighbouring members.
//
// The growth case uses a REAL added field (a shadow struct with a member after
// `RunController`) rather than an arithmetic "sizeof + 4", so what is exercised
// is the situation the tripwire exists for: somebody added state and did not
// classify it.

#include "sts/engine/byte_class.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/run_advance.hpp"

namespace sts::engine {
namespace {

std::string describe(const TilingFault& f) {
    if (f.ok) return "<no fault>";
    return std::string(f.overlap ? "OVERLAPPING" : "unclassified") + " bytes [" +
           std::to_string(f.from) + ", " + std::to_string(f.to) + ") of " +
           f.struct_name + " between `" + f.before + "` and `" + f.after + "`";
}

const ClassTable* const kAllTables[] = {
    &kRunControllerTable, &kRunStateTable,      &kCombatStateTable,
    &kMonsterListsTable,  &kTreasureChestTable, &kEventDialogTable,
};

// =============================================================================
// The tripwire itself
// =============================================================================

TEST(Tripwire, EveryClassifiedStructIsTiledExactly) {
    for (const ClassTable* t : kAllTables) {
        const TilingFault f = check_tiling(*t, t->total);
        EXPECT_TRUE(f.ok)
            << describe(f)
            << ".\nAdd the missing classification row to "
               "include/sts/engine/byte_class.hpp AND the matching row to "
               "docs/public-view-audit.md, then re-check whether "
               "encode_public_view should carry the new field. A field with no "
               "audit row is twin-invariant -- nothing else in this repo will "
               "notice it is missing from the observation.";
    }
}

TEST(Tripwire, EveryRowHasANonZeroSizeAndALeafClass) {
    for (const ClassTable* t : kAllTables) {
        for (std::size_t i = 0; i < t->row_count; ++i) {
            const ClassRow& r = t->rows[i];
            EXPECT_GT(r.size, 0u)
                << t->struct_name << "." << r.member << " claims no bytes";
            if (r.sub != nullptr) {
                EXPECT_EQ(r.cls, ByteClass::MIXED)
                    << t->struct_name << "." << r.member
                    << " has a sub-table but is not classified MIXED";
                EXPECT_EQ(r.size, r.sub->total)
                    << t->struct_name << "." << r.member
                    << " and its sub-table disagree about the struct's size";
            }
        }
    }
}

TEST(Tripwire, RunSeedIsClassifiedHidden) {
    // plan §2.6b names this one explicitly: "every byte of RunController
    // classified public/hidden/derived (`run_seed` itself: hidden)".
    bool found = false;
    for (std::size_t i = 0; i < kRunStateTable.row_count; ++i) {
        const ClassRow& r = kRunStateTable.rows[i];
        if (std::string(r.member) == "run_seed") {
            found = true;
            EXPECT_EQ(r.cls, ByteClass::HIDDEN)
                << "RunState.run_seed is classified " << byte_class_name(r.cls)
                << "; knowing the run seed is seed-cracking, which the "
                   "evaluation contract bans (plan §1)";
        }
    }
    EXPECT_TRUE(found) << "RunState.run_seed has no classification row";
}

TEST(Tripwire, TheStreamsAndPoolsAreClassifiedHidden) {
    // A spot check that the vocabulary is being used, not just spelled: every
    // RngStream-sized row in RunState/CombatState is a realization.
    int hidden_streams = 0;
    for (const ClassTable* t : {&kRunStateTable, &kCombatStateTable}) {
        for (std::size_t i = 0; i < t->row_count; ++i) {
            const ClassRow& r = t->rows[i];
            const std::string name = r.member;
            if (name.size() > 4 && name.substr(name.size() - 4) == "_rng") {
                EXPECT_EQ(r.cls, ByteClass::HIDDEN)
                    << t->struct_name << "." << name
                    << " is a stream state and must be hidden";
                ++hidden_streams;
            }
        }
    }
    EXPECT_EQ(hidden_streams, 14)
        << "the engine's fourteen RNG streams are the contract's canonical "
           "hidden set (stage-a §3.4); this count moved";
}

// =============================================================================
// The negative tests -- the mechanism proving it can fail
// =============================================================================

// A scratch field added to RunController, exactly as an unclassified engine
// change would add one. Nothing constructs it; only its `sizeof` is used.
struct RunControllerPlusScratch {
    RunController base;
    uint32_t scratch_field;
};

TEST(TripwireNegative, FiresOnAScratchFieldAddedToRunController) {
    static_assert(sizeof(RunControllerPlusScratch) > sizeof(RunController),
                  "the shadow struct must actually be bigger");

    const TilingFault f =
        check_tiling(kRunControllerTable, sizeof(RunControllerPlusScratch));
    ASSERT_FALSE(f.ok)
        << "the tripwire did NOT fire on an added field -- it is not "
           "protecting anything";
    EXPECT_FALSE(f.overlap);
    EXPECT_EQ(f.from, sizeof(RunController))
        << "the reported range must start where the classified bytes end";
    EXPECT_EQ(f.to, sizeof(RunControllerPlusScratch));
    EXPECT_STREQ(f.after, "<end>");
    EXPECT_STREQ(f.before, "stolen_live")
        << "the message must name the last classified member so the reader "
           "knows where the new field landed";
    GTEST_LOG_(INFO) << "negative control fired: " << describe(f);
}

TEST(TripwireNegative, FiresWhenAClassificationRowIsRemoved) {
    // Drop `emerald_y` from a COPY of the real table. That is the shape of "a
    // member exists but nobody classified it".
    std::vector<ClassRow> rows;
    std::size_t dropped = 0;
    for (std::size_t i = 0; i < kRunControllerTable.row_count; ++i) {
        const ClassRow& r = kRunControllerTable.rows[i];
        if (std::string(r.member) == "emerald_y") {
            ++dropped;
            continue;
        }
        rows.push_back(r);
    }
    ASSERT_EQ(dropped, 1u);

    const ClassTable mutated{"RunController(mutated)", sizeof(RunController),
                             rows.data(), rows.size()};
    const TilingFault f = check_tiling(mutated, sizeof(RunController));
    ASSERT_FALSE(f.ok) << "a removed row went unnoticed";
    EXPECT_FALSE(f.overlap);
    EXPECT_EQ(f.to - f.from, sizeof(uint8_t))
        << "the reported gap must be exactly the dropped member's size";
    EXPECT_STREQ(f.before, "emerald_x");
    EXPECT_STREQ(f.after, "pad_emerald");
    GTEST_LOG_(INFO) << "negative control fired: " << describe(f);
}

TEST(TripwireNegative, FiresOnOverlappingRows) {
    // Move a row's offset backwards: two rows now claim the same byte, which is
    // how a copy-pasted row or a mis-sized array reads.
    std::vector<ClassRow> rows(
        kRunControllerTable.rows,
        kRunControllerTable.rows + kRunControllerTable.row_count);
    bool moved = false;
    for (ClassRow& r : rows) {
        if (std::string(r.member) == "elite_cursor") {
            r.offset -= 1;
            moved = true;
            break;
        }
    }
    ASSERT_TRUE(moved);

    const ClassTable mutated{"RunController(mutated)", sizeof(RunController),
                             rows.data(), rows.size()};
    const TilingFault f = check_tiling(mutated, sizeof(RunController));
    ASSERT_FALSE(f.ok) << "an overlapping row went unnoticed";
    EXPECT_TRUE(f.overlap);
    EXPECT_STREQ(f.after, "elite_cursor");
    EXPECT_STREQ(f.before, "monster_cursor");
    GTEST_LOG_(INFO) << "negative control fired: " << describe(f);
}

TEST(TripwireNegative, FiresWhenAPaddingRowIsDeclaredTooSmall) {
    // Padding rows carry LITERAL byte counts precisely so they cannot re-fit
    // themselves around a change. Shrinking one must be caught by the NEXT
    // member's offsetof, not absorbed.
    //
    // This used to shrink one of MonsterLists' declared GAPS. Those are gone --
    // all four became real `pad_*` members when the ThreeActSim determinism
    // failure showed that a gap tiles without ever being written -- so the
    // control now shrinks the PADDING ROW that replaced one of them. The
    // property under test is unchanged: a literal byte count that no longer
    // matches the struct must fail loudly.
    std::vector<ClassRow> rows(
        kMonsterListsTable.rows,
        kMonsterListsTable.rows + kMonsterListsTable.row_count);
    bool shrunk = false;
    for (ClassRow& r : rows) {
        if (r.cls == ByteClass::PADDING && r.size > 1) {
            r.size -= 1;
            shrunk = true;
            break;
        }
    }
    ASSERT_TRUE(shrunk) << "MonsterLists must still declare its alignment "
                           "padding as a member";

    const ClassTable mutated{"MonsterLists(mutated)", sizeof(MonsterLists),
                             rows.data(), rows.size()};
    const TilingFault f = check_tiling(mutated, sizeof(MonsterLists));
    EXPECT_FALSE(f.ok) << "a shrunken padding row was absorbed silently";
    GTEST_LOG_(INFO) << "negative control fired: " << describe(f);
}

// --- The elimination: no DECLARED GAPS in a byte-hashed struct ---------------
//
// Third occurrence of one trap (conventions section 7's rule of two, exceeded):
// RunState 2026-07-28, CombatState/RunController 2026-08-03, and MonsterLists
// here -- an implicit alignment gap inside a struct that is memcmp'd and
// byte-hashed. Each time, value-initialisation left the gap INDETERMINATE while
// every reader assumed zero; each time it passed on Linux, where fresh pages
// read zero, and failed only on Windows.
//
// The tiling tripwire could not catch it, because a DECLARED GAP tiles exactly
// as well as a declared member -- it simply never gets written. So the check has
// to be on the gap's EXISTENCE, not on the arithmetic.
//
// This walks the three byte-hashed tables, sub-tables included, and requires
// every byte to belong to a real MEMBER. A new alignment gap is now a failing
// test that names its own fix, instead of a latent nondeterminism that surfaces
// on one host months later.
void collect_gaps(const ClassTable& t, std::vector<std::string>& out,
                  const std::string& prefix) {
    for (std::size_t i = 0; i < t.row_count; ++i) {
        const ClassRow& r = t.rows[i];
        if (r.offset == kDeclaredGap) {
            out.push_back(prefix + t.struct_name + "::" + r.member + " (" +
                          std::to_string(r.size) + " bytes)");
        }
        if (r.sub != nullptr) {
            collect_gaps(*r.sub, out, prefix + t.struct_name + "::");
        }
    }
}

TEST(Tripwire, NoDeclaredGapsInByteHashedStructs) {
    std::vector<std::string> gaps;
    collect_gaps(kRunStateTable, gaps, "");
    collect_gaps(kCombatStateTable, gaps, "");
    collect_gaps(kRunControllerTable, gaps, "");

    std::string joined;
    for (const std::string& g : gaps) {
        joined += "\n  " + g;
    }
    EXPECT_TRUE(gaps.empty())
        << "these byte ranges belong to no member of a byte-hashed struct:"
        << joined
        << "\n\nA declared GAP tiles but is never WRITTEN, so it is "
           "indeterminate -- value-init initialises members, not padding. It "
           "reads zero on Linux (fresh pages) and garbage on Windows, which is "
           "how this defect class has hidden three times. Fix: declare a "
           "`uint8_t pad_*[N]{}` member in the struct and switch the row to "
           "STS_BC_ROW. That changes no offset and no size.";
}

// Aggregate-initialise a T over storage pre-filled with `fill`. Any byte that
// belongs to no MEMBER keeps `fill`.
//
// `T{}` on an aggregate is AGGREGATE-INITIALISATION, which initialises members
// -- [dcl.init.list]/3 reaches the aggregate bullet before the empty-list
// value-init bullet -- so it does NOT touch padding. That is not a technicality
// here: it is the exact mechanism of the failure this guard exists for.
// `rc.lists = MonsterLists{}` (run_advance.cpp) builds such a temporary on the
// stack and copy-assigns it, and because these structs are trivially copyable
// the assignment is a whole-object memcpy -- so the temporary's INDETERMINATE
// padding is copied straight into the controller that then gets byte-hashed.
//
// Deliberately NOT `make_unique<T>()` and not `new (storage) T`: value-init
// zero-initialises the whole object, padding included, so either would hide the
// very defect under test. The first draft of this check used one and passed
// against the live bug.
template <typename T>
void aggregate_init_over_fill(void* storage, unsigned char fill) {
    std::memset(storage, fill, sizeof(T));
    new (storage) T{};
}

template <typename T>
::testing::AssertionResult EveryByteIsAMember(const char* name) {
    alignas(T) static unsigned char a[sizeof(T)];
    alignas(T) static unsigned char b[sizeof(T)];
    aggregate_init_over_fill<T>(a, 0xAA);
    aggregate_init_over_fill<T>(b, 0x55);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        if (a[i] != b[i]) {
            return ::testing::AssertionFailure()
                   << name << " byte " << i << " belongs to no member: it kept "
                      "its fill (0xAA vs 0x55) through aggregate-initialisation. "
                      "That byte is INDETERMINATE in every real use, and this "
                      "struct is memcmp'd and byte-hashed -- so two runs of the "
                      "same seed can disagree. Declare a `uint8_t pad_*[N]{}` "
                      "member covering it (see "
                      "NoDeclaredGapsInByteHashedStructs).";
        }
    }
    return ::testing::AssertionSuccess();
}

TEST(Tripwire, EveryByteOfAByteHashedStructBelongsToAMember) {
    // The invariant ThreeActSim's determinism check was implicitly relying on,
    // pinned directly, deterministically, and on every host -- where the
    // original failure was intermittent and Windows-only because it depended on
    // whether a reused stack frame happened to be dirty.
    EXPECT_TRUE(EveryByteIsAMember<RunController>("RunController"));
    EXPECT_TRUE(EveryByteIsAMember<RunState>("RunState"));
    EXPECT_TRUE(EveryByteIsAMember<CombatState>("CombatState"));
    EXPECT_TRUE(EveryByteIsAMember<MonsterLists>("MonsterLists"));
}

}  // namespace
}  // namespace sts::engine
