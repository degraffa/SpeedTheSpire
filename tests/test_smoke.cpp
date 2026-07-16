#include "sts/engine/version.hpp"

#include <type_traits>

#include <gtest/gtest.h>

TEST(Smoke, VersionStringMatchesConstant) {
    EXPECT_EQ(sts::engine::VersionString(), "0.1.0");
}

TEST(Smoke, VersionStructIsTriviallyCopyable) {
    // State structs must be memcpy-able for snapshot/restore (InitialPlan.md A.3).
    static_assert(std::is_trivially_copyable_v<sts::engine::Version>);
    SUCCEED();
}
