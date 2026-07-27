# The run-level replay harness: re-drive the simulator through a campaign
# artifact's command sequence and diff its RunState against the artifact's at
# every record (see src/main.cpp for the mapping table and its honest limits).
#
# It links oracle_translator for the typed translation (which transitively
# brings diff_harness -> sts_engine and the generated registry) and nlohmann
# PRIVATE for the one extra presentation-field pass the translator does not
# expose. Same tools-only JSON grant as the translator directory next door;
# this target is added after it, so the nlohmann_json INTERFACE target exists.
add_executable(replay_run_diff src/main.cpp)
target_link_libraries(replay_run_diff PRIVATE oracle_translator nlohmann_json)
add_dependencies(replay_run_diff registry_gen_headers)
sts_set_warnings(replay_run_diff)
sts_enable_sanitizers(replay_run_diff)
