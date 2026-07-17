include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(sts_engine_tests
    test_smoke.cpp
)

target_link_libraries(sts_engine_tests PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(sts_engine_tests)
sts_enable_sanitizers(sts_engine_tests)

include(GoogleTest)
gtest_discover_tests(sts_engine_tests)

# A1.1: RandomXS128 (xorshift128+) bit-exactness suite. Separate executable
# target per the Stage A task ledger's acceptance line (gtest
# `rng_xs128_test`), not appended to sts_engine_tests.
add_executable(rng_xs128_test
    rng_xs128_test.cpp
)

target_link_libraries(rng_xs128_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(rng_xs128_test PRIVATE STS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
sts_set_warnings(rng_xs128_test)
sts_enable_sanitizers(rng_xs128_test)

gtest_discover_tests(rng_xs128_test)

# A1.2: JDK LCG + Collections.shuffle bit-exactness suite.
add_executable(rng_jdk_test
    rng_jdk_test.cpp
)

target_link_libraries(rng_jdk_test PRIVATE sts_engine GTest::gtest_main)
target_compile_definitions(rng_jdk_test PRIVATE STS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
sts_set_warnings(rng_jdk_test)
sts_enable_sanitizers(rng_jdk_test)

gtest_discover_tests(rng_jdk_test)
