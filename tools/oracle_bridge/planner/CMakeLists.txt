# The capture PLANNER: pre-scan seeds so a capture campaign can be aimed at a
# rare target instead of walking STS%05d and hoping.
#
# Shaped like tools/fuzz and tools/oracle_bridge/replay: a static library that
# holds the substance (the event-name table, the scan, the filter, the two
# output encodings, the aggregate report) plus one thin executable over it. The
# library is what `seed_scan_test` links, so every claim the planner makes --
# including "scanning the same case twice is byte-identical" -- is testable
# without running a scan.
#
# It links fuzz_core rather than restating the run loop: the scan's whole value
# is that its verdict matches what a capture will see, and a forked copy of
# run_begin -> legal_actions -> enumerate_moves -> policy_pick -> advance is a
# copy that drifts away from that. All this directory adds is a
# fuzz::StepObserver over pass A. It also needs the generated registry directly
# (registry/ids.hpp for EventId, registry/event_table.hpp for the static_assert
# that fails the build when events.yaml gains a row), which fuzz_core already
# exposes PUBLIC -- named again here so the dependency is not accidental.
add_library(seed_scan_core STATIC src/seed_scan.cpp)
target_include_directories(seed_scan_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(seed_scan_core PUBLIC fuzz_core registry_generated)
target_compile_features(seed_scan_core PUBLIC cxx_std_20)
add_dependencies(seed_scan_core registry_gen_headers)
sts_set_warnings(seed_scan_core)
sts_enable_sanitizers(seed_scan_core)

add_executable(seed_scan src/main.cpp)
target_link_libraries(seed_scan PRIVATE seed_scan_core)
sts_set_warnings(seed_scan)
sts_enable_sanitizers(seed_scan)
