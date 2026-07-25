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
    # Native power bodies, ONE TU PER POWER, each named for its power (see
    # powers/power_native.hpp). A new native power adds a file here.
    powers/power_artifact.cpp
    powers/power_combust.cpp
    powers/power_corruption.cpp
    powers/power_curl_up.cpp
    powers/power_dark_embrace.cpp
    powers/power_evolve.cpp
    powers/power_fire_breathing.cpp
    powers/power_flame_barrier.cpp
    powers/power_frail.cpp
    powers/power_lose_dexterity.cpp
    powers/power_lose_strength.cpp
    powers/power_next_turn_block.cpp
    powers/power_plated_armor.cpp
    powers/power_rage.cpp
    powers/power_regen.cpp
    powers/power_ritual.cpp
    powers/power_rupture.cpp
    powers/power_sadistic.cpp
    powers/power_thorns.cpp
    relic_hooks.cpp
    # Native relic combat bodies, one TU per RelicTier (see
    # relics/relic_native.hpp), matching the relic_pickup_* split below. The
    # RARE / SHOP / BOSS tiers each add a file here when they land.
    relics/relics_starter.cpp
    relics/relics_common.cpp
    relics/relics_uncommon.cpp
    relic_pools.cpp
    # The master deck's obtain-time relic pass; holds the generated
    # STS_REGISTRY_RELIC_ON_OBTAIN_CARD dispatch table.
    run_deck.cpp
    # Native relic PICKUP bodies (canSpawn / onEquip / onObtainCard), one TU per
    # RelicTier (see relics/relic_pickup.hpp). Dispatched from relic_pools.cpp and
    # run_deck.cpp through the generated STS_REGISTRY_RELIC_* macros, so bringing
    # up a tier adds a file here and edits no existing switch.
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
