add_library(sts_engine
    version.cpp
    state_hash.cpp
    action_queue.cpp
    monster_jaw_worm.cpp
    monster_cultist.cpp
    monster_louse.cpp
    monster_slime.cpp
    monster_dispatch.cpp
    encounters.cpp
    interp.cpp
    piles.cpp
    card_play.cpp
    power_hooks.cpp
    relic_hooks.cpp
    relic_pools.cpp
    potions.cpp
    advance.cpp
    run_advance.cpp
)

target_include_directories(sts_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_features(sts_engine PUBLIC cxx_std_20)

# The engine's id enums and card/monster tables are the code-generated registry
# headers (types.hpp/cards.hpp/monster_jaw_worm.hpp re-export them). PUBLIC:
# engine public headers include the generated ones, so every consumer needs the
# generated include dir; the explicit add_dependencies orders codegen before any
# engine TU compiles.
target_link_libraries(sts_engine PUBLIC registry_generated)
add_dependencies(sts_engine registry_gen_headers)

# state_hash.cpp includes xxhash.h (XXH_INLINE_ALL). PRIVATE: the header is an
# implementation detail of the engine; nothing in the public headers exposes it.
target_link_libraries(sts_engine PRIVATE xxhash)

sts_set_warnings(sts_engine)
sts_enable_sanitizers(sts_engine)
