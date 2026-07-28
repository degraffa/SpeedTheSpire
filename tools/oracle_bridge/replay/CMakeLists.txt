# The run-level replay harness: re-drive the simulator through a campaign
# artifact's command sequence and diff its RunState against the artifact's at
# every record (see src/main.cpp for the mapping table and its honest limits).
#
# It links oracle_translator for the typed translation (which transitively
# brings diff_harness -> sts_engine and the generated registry) and nlohmann
# PRIVATE for the one extra presentation-field pass the translator does not
# expose. Same tools-only JSON grant as the translator directory next door;
# this target is added after it, so the nlohmann_json INTERFACE target exists.
#
# The command mapping itself is an INTERFACE target rather than part of the
# executable: it is the piece a captured run can be replayed WRONG by, and
# `replay_command_map_test` (tests/) links this to exercise the table without
# the JSON layer or an artifact. It carries no JSON dependency by design --
# `ScreenInfo` is plain data that `main.cpp` fills.
add_library(replay_command_map INTERFACE)
target_include_directories(replay_command_map INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(replay_command_map INTERFACE sts_engine)

# The two SHAPE decisions the --treasure and --event read-outs make before any
# comparison runs (src/readout_shapes.hpp): which captured reward rows a chest
# open may carry that the engine deliberately does not model, and the
# capture-id -> EventId join. Published as its own INTERFACE target for exactly
# the reason the command table is -- both are places a read-out can quietly call
# a real divergence benign, so `replay_readout_shapes_test` exercises them with
# no artifact and no JSON layer. `command_map.hpp` includes the header for
# ScreenInfo's reward rows, so the command-map target links it too and its own
# test keeps compiling unchanged.
add_library(replay_readout_shapes INTERFACE)
target_include_directories(replay_readout_shapes INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(replay_readout_shapes INTERFACE sts_engine)
target_link_libraries(replay_command_map INTERFACE replay_readout_shapes)

add_executable(replay_run_diff src/main.cpp)
target_link_libraries(replay_run_diff
    PRIVATE oracle_translator nlohmann_json replay_command_map)
add_dependencies(replay_run_diff registry_gen_headers)
sts_set_warnings(replay_run_diff)
sts_enable_sanitizers(replay_run_diff)
