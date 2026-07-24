# sts_add_test: one gtest executable per test target.
#
# Every suite in tests/ is its own executable -- the task ledgers' acceptance
# lines name specific gtest binaries (`rng_xs128_test`, `differ_test`, ...), so
# suites are never merged into a shared runner. That made tests/CMakeLists.txt a
# stack of near-identical six-line blocks and a contention point for parallel
# work; this helper is the one place that shape lives.
#
#   sts_add_test(<name>
#       [SOURCES   <file>...]   # default: <name>.cpp
#       [LIBS      <target>...] # default: sts_engine (always + GTest::gtest_main)
#       [DEFS      <def>...]    # extra PRIVATE compile definitions
#       [DEPENDS   <target>...] # add_dependencies (e.g. generated headers)
#       [MAKE_DIRS <dir>...]    # dirs created under CMAKE_CURRENT_BINARY_DIR at
#                               #   configure time, for tests that write scratch
#       [GOLDEN]                # define STS_GOLDEN_DIR  -> tests/golden
#       [FIXTURES]              # define STS_FIXTURE_DIR -> tests/fixtures
#   )
#
# GOLDEN/FIXTURES exist because those two directories are the project's two
# distinct oracle kinds and are wanted by name: tests/golden holds JVM-captured
# vectors, tests/fixtures holds hand-derived oracles. Callers that need some
# other directory pass it through DEFS.
#
# Every target gets sts_set_warnings + sts_enable_sanitizers + gtest_discover_tests
# unconditionally; that was already true of all 39 blocks and is not optional.
#
# Discovery runs in PRE_TEST mode: with the CMake default (POST_BUILD) every one
# of the test binaries is executed at *build* time purely to enumerate its test
# names, so a full build pays 39 process launches it does not need. PRE_TEST
# defers that enumeration to `ctest` startup, where it is actually used.
function(sts_add_test name)
    set(options GOLDEN FIXTURES)
    set(multi SOURCES LIBS DEFS DEPENDS MAKE_DIRS)
    cmake_parse_arguments(ARG "${options}" "" "${multi}" ${ARGN})

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "sts_add_test(${name}): unrecognised arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT ARG_SOURCES)
        set(ARG_SOURCES ${name}.cpp)
    endif()
    if(NOT ARG_LIBS)
        set(ARG_LIBS sts_engine)
    endif()

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS} GTest::gtest_main)

    if(ARG_DEPENDS)
        add_dependencies(${name} ${ARG_DEPENDS})
    endif()

    if(ARG_GOLDEN)
        list(APPEND ARG_DEFS STS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
    endif()
    if(ARG_FIXTURES)
        list(APPEND ARG_DEFS STS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    endif()
    if(ARG_DEFS)
        target_compile_definitions(${name} PRIVATE ${ARG_DEFS})
    endif()

    foreach(dir IN LISTS ARG_MAKE_DIRS)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${dir}")
    endforeach()

    sts_set_warnings(${name})
    sts_enable_sanitizers(${name})

    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()
