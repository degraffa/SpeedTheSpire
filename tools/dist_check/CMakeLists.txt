# B5.3 tier-4 distributional verification. This is a campaign tool, not a
# per-commit test: dist_check sweeps at least 10k simulator seeds and applies
# the pre-registered family-wise correction documented in README.md.
add_library(dist_check_stats STATIC
    src/stats.cpp
)
target_include_directories(dist_check_stats PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(dist_check_stats PUBLIC cxx_std_20)
sts_set_warnings(dist_check_stats)
sts_enable_sanitizers(dist_check_stats)

add_executable(dist_check
    src/main.cpp
)
target_link_libraries(dist_check PRIVATE dist_check_stats sts_engine)
add_dependencies(dist_check registry_gen_headers)
sts_set_warnings(dist_check)
sts_enable_sanitizers(dist_check)

# S2.44 tier-4 S2 family (s2-design §6, S2-G2 item 6). The ANALYTIC half is a
# library rather than a translation unit inside the executable so
# tests/dist_check_test.cpp can pin its expectations against hand-derived
# numbers -- a wrong expectation and an engine defect look identical on a
# campaign report line, so the expectations get their own tests.
add_library(dist_check_s2_expect STATIC
    src/s2_expect.cpp
)
target_include_directories(dist_check_s2_expect PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(dist_check_s2_expect PUBLIC sts_engine)
add_dependencies(dist_check_s2_expect registry_gen_headers)
sts_set_warnings(dist_check_s2_expect)
sts_enable_sanitizers(dist_check_s2_expect)

add_executable(dist_check_s2
    src/s2_main.cpp
)
target_link_libraries(dist_check_s2
    PRIVATE dist_check_stats dist_check_s2_expect sts_engine)
add_dependencies(dist_check_s2 registry_gen_headers)
sts_set_warnings(dist_check_s2)
sts_enable_sanitizers(dist_check_s2)

# S3.63 tier-4 S3 family (s3-tasks.md S3.63, s3-design §5 traps 1/9 and the
# Act-4 monster getMove tables). Reuses dist_check_s2_expect's
# front_scan_blocked_law (see s3_main.cpp's header for why that reuse is
# sound) rather than duplicating it -- everything else the family needs is
# either a closed-form 50/50 or derived inline from registry-driven engine
# state, so there is no separate S3 expectation library.
add_executable(dist_check_s3
    src/s3_main.cpp
)
target_link_libraries(dist_check_s3
    PRIVATE dist_check_stats dist_check_s2_expect sts_engine)
add_dependencies(dist_check_s3 registry_gen_headers)
sts_set_warnings(dist_check_s3)
sts_enable_sanitizers(dist_check_s3)

if(TARGET fuzz_core)
    add_executable(dist_check_spot_sim
        src/spot_sim.cpp
    )
    target_link_libraries(dist_check_spot_sim PRIVATE fuzz_core)
    sts_set_warnings(dist_check_spot_sim)
    sts_enable_sanitizers(dist_check_spot_sim)
endif()
