add_library(sts_engine
    version.cpp
    state_hash.cpp
    action_queue.cpp
    monster_jaw_worm.cpp
    monster_cultist.cpp
    monster_louse.cpp
    monster_slime.cpp
    monster_slime_large.cpp
    monster_slime_boss.cpp
    monster_dispatch.cpp
    encounters.cpp
    interp.cpp
    # Opcode bodies, one TU per behavioural domain (see interp/interp_ops.hpp).
    # interp.cpp keeps only the dynamic-target fan-out + the execute_opcode
    # switch; a new opcode adds its body to the domain TU that owns it.
    interp/interp_damage.cpp
    interp/interp_block.cpp
    interp/interp_powers.cpp
    interp/interp_cards.cpp
    piles.cpp
    card_play.cpp
    power_hooks.cpp
    # Native power bodies, one TU per registry/powers.yaml batch (see
    # powers/power_native.hpp). A new power batch adds a file here.
    powers/powers_b3_2.cpp
    powers/powers_b3_4.cpp
    powers/powers_potion_support.cpp
    powers/powers_b3_6.cpp
    powers/powers_b3_7.cpp
    powers/powers_b3_9.cpp
    powers/powers_b3_25.cpp
    relic_hooks.cpp
    # Native relic bodies, one TU per relic tier/batch (see
    # relics/relic_native.hpp). B3.26 (rares+shop) / B3.27 (boss+specials) each
    # add a file here.
    relics/relics_b3_24.cpp
    relics/relics_b3_25.cpp
    relic_pools.cpp
    # Native relic PICKUP bodies (canSpawn / onEquip), one TU per RelicTier (see
    # relics/relic_pickup.hpp). Dispatched from relic_pools.cpp through the
    # generated STS_REGISTRY_RELIC_CAN_SPAWN / _ON_EQUIP macros, so bringing up a
    # tier adds a file here and edits no existing switch.
    relics/relic_pickup_common.cpp
    relics/relic_pickup_uncommon.cpp
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
