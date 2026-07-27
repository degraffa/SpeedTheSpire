add_library(sts_engine
    version.cpp
    state_hash.cpp
    action_queue.cpp
    monster_jaw_worm.cpp
    monster_cultist.cpp
    monster_gremlin_nob.cpp
    monster_louse.cpp
    monster_sentry.cpp
    monster_slime.cpp
    monster_slime_large.cpp
    monster_slime_boss.cpp
    monster_lagavulin.cpp
    monster_gremlin.cpp
    monster_guardian.cpp
    monster_hexaghost.cpp
    monster_slaver.cpp
    monster_fungi_beast.cpp
    monster_looter.cpp
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
    powers/power_anger.cpp
    powers/power_angry.cpp
    powers/power_artifact.cpp
    powers/power_combust.cpp
    powers/power_confusion.cpp
    powers/power_corruption.cpp
    powers/power_curl_up.cpp
    powers/power_dark_embrace.cpp
    powers/power_double_tap.cpp
    powers/power_evolve.cpp
    powers/power_fire_breathing.cpp
    powers/power_flame_barrier.cpp
    powers/power_frail.cpp
    powers/power_lose_dexterity.cpp
    powers/power_lose_strength.cpp
    powers/power_magnetism.cpp
    powers/power_mayhem.cpp
    powers/power_next_turn_block.cpp
    powers/power_panache.cpp
    powers/power_plated_armor.cpp
    powers/power_rage.cpp
    powers/power_regen.cpp
    powers/power_ritual.cpp
    powers/power_rupture.cpp
    powers/power_sadistic.cpp
    powers/power_sharp_hide.cpp
    powers/power_spore_cloud.cpp
    powers/power_the_bomb.cpp
    powers/power_thorns.cpp
    relic_hooks.cpp
    # Native relic combat bodies, one TU per RelicTier (see
    # relics/relic_native.hpp), matching the relic_pickup_* split below.
    relics/relics_starter.cpp
    relics/relics_common.cpp
    relics/relics_uncommon.cpp
    relics/relics_rare.cpp
    relics/relics_shop.cpp
    relics/relics_boss.cpp
    relics/relics_special.cpp
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
    relics/relic_pickup_rare.cpp
    relics/relic_pickup_shop.cpp
    relics/relic_pickup_boss.cpp
    potions.cpp
    advance.cpp
    # Combat-reward assembly + claim (combat_rewards.hpp).
    combat_rewards.cpp
    rest_sites.cpp
    # Treasure-room chest construction/opening + chest relic hooks.
    treasure_rooms.cpp
    # ?-room roll + event selection/pool bookkeeping + dialog dispatch.
    event_framework.cpp
    # Native event bodies, grouped by delivery batch.
    events/exordium_events_i.cpp
    events/exordium_events_ii.cpp
    events/shrines.cpp
    events/one_time_specials.cpp
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
