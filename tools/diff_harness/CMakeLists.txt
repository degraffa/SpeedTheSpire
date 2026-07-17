# diff harness -- trace format, field-by-field CombatState differ,
# reproducer emitter, and the fixture-file oracle adapter (design doc §8).
#
# Unlike tools/golden_capture (a Windows-host-only JVM tool with no CMake
# integration), this IS a normal C++ target in the WSL/CMake pipeline: a static
# library consumed by the differ_test gtest binary now, and available to a future
# CLI or the fixture generator. It links sts_engine (PUBLIC, so its own headers
# and sts_engine's transitively reach consumers) and exposes its public headers
# under include/sts/diff/.
add_library(diff_harness STATIC
    src/trace.cpp
    src/differ.cpp
    src/reproducer.cpp
    src/oracle.cpp
    src/replay.cpp
)

target_include_directories(diff_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(diff_harness PUBLIC sts_engine)
target_compile_features(diff_harness PUBLIC cxx_std_20)

sts_set_warnings(diff_harness)
sts_enable_sanitizers(diff_harness)
