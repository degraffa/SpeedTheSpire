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

add_executable(seed_string_test
    seed_string_test.cpp
)

target_link_libraries(seed_string_test PRIVATE sts_engine GTest::gtest_main)
sts_set_warnings(seed_string_test)
sts_enable_sanitizers(seed_string_test)
target_compile_definitions(seed_string_test PRIVATE STS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")

gtest_discover_tests(seed_string_test)
