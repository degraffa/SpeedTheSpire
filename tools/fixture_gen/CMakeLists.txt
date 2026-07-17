# combat-fixture generator -- the INDEPENDENT reference simulator that
# writes the expected per-action state traces (tests/golden/combat_fixtures/*.trace)
# for the ~20 scripted Jaw Worm fights. It re-derives every state from first
# principles (design-doc formulas + the Jaw Worm fixture + tier-1 RNG
# oracles), never from the production gameplay engine -- see the file header for
# the independence contract. Checked in alongside the derivation notes; run
# manually to regenerate the traces (like tools/golden_capture).
add_executable(gen_combat_fixtures
    gen_combat_fixtures.cpp
)

target_link_libraries(gen_combat_fixtures PRIVATE diff_harness)
target_compile_definitions(gen_combat_fixtures PRIVATE
    STS_JAW_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    STS_FIXTURE_OUT_DIR="${CMAKE_SOURCE_DIR}/tests/golden/combat_fixtures")
sts_set_warnings(gen_combat_fixtures)
sts_enable_sanitizers(gen_combat_fixtures)
