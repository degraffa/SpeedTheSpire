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
    monster_giant_head.cpp
    monster_gremlin.cpp
    monster_guardian.cpp
    monster_hexaghost.cpp
    monster_slaver.cpp
    monster_fungi_beast.cpp
    monster_looter.cpp
    # S2.21 -- Act-2 city normals I.
    monster_chosen.cpp
    monster_byrd.cpp
    monster_shelled_parasite.cpp
    monster_spheric_guardian.cpp
    # S2.22 -- Act-2 city normals II.
    monster_mugger.cpp
    monster_snake_plant.cpp
    monster_snecko.cpp
    monster_centurion.cpp
    monster_healer.cpp
    monster_book_of_stabbing.cpp
    monster_gremlin_leader.cpp
    monster_taskmaster.cpp
    # S2.25 -- Act-3 beyond normals I
    monster_darkling.cpp
    monster_nemesis.cpp
    monster_orb_walker.cpp
    monster_reptomancer.cpp
    monster_repulsor.cpp
    monster_exploder.cpp
    monster_spiker.cpp
    # S2.26 -- Act-3 "Beyond" normals II.
    monster_spire_growth.cpp
    monster_transient.cpp
    monster_maw.cpp
    monster_writhing_mass.cpp
    # S2.24 -- Act-2 City bosses.
    monster_bronze_automaton.cpp
    monster_bronze_orb.cpp
    monster_champ.cpp
    monster_collector.cpp
    monster_torch_head.cpp
    # S2.32 -- the Masked Bandits event trio.
    monster_bandits.cpp
    # S2.28 -- Act-3 Beyond bosses.
    monster_awakened_one.cpp
    monster_time_eater.cpp
    monster_donu_deca.cpp
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
    knowledge.cpp
    card_play.cpp
    power_hooks.cpp
    # Native power bodies, ONE TU PER POWER, each named for its power (see
    # powers/power_native.hpp). A new native power adds a file here.
    powers/power_anger.cpp
    powers/power_angry.cpp
    powers/power_artifact.cpp
    powers/power_combust.cpp
    powers/power_confusion.cpp
    powers/power_constricted.cpp
    powers/power_corruption.cpp
    powers/power_curiosity.cpp
    powers/power_curl_up.cpp
    powers/power_dark_embrace.cpp
    powers/power_double_tap.cpp
    powers/power_draw_reduction.cpp
    powers/power_duplication.cpp
    # Not a power: the atEndOfRound body Vulnerable / Weak / Frail share, plus
    # their application-time justApplied predicate (powers/power_duration_debuff.hpp).
    powers/power_duration_debuff.cpp
    powers/power_evolve.cpp
    powers/power_explosive.cpp
    powers/power_fading.cpp
    powers/power_fire_breathing.cpp
    powers/power_flight.cpp
    powers/power_flame_barrier.cpp
    powers/power_frail.cpp
    powers/power_hex.cpp
    powers/power_intangible_monster.cpp
    powers/power_lose_dexterity.cpp
    powers/power_lose_strength.cpp
    powers/power_magnetism.cpp
    powers/power_malleable.cpp
    powers/power_painful_stabs.cpp
    powers/power_mayhem.cpp
    powers/power_next_turn_block.cpp
    powers/power_panache.cpp
    powers/power_pen_nib.cpp
    powers/power_plated_armor.cpp
    powers/power_rage.cpp
    powers/power_reactive.cpp
    powers/power_regen.cpp
    powers/power_regenerate_monster.cpp
    powers/power_ritual.cpp
    powers/power_rupture.cpp
    powers/power_sadistic.cpp
    powers/power_sharp_hide.cpp
    powers/power_shifting.cpp
    powers/power_slow.cpp
    powers/power_spore_cloud.cpp
    powers/power_stasis.cpp
    powers/power_the_bomb.cpp
    powers/power_thorns.cpp
    powers/power_time_warp.cpp
    powers/power_vigor.cpp
    powers/power_vulnerable.cpp
    powers/power_weak.cpp
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
    relics/relic_pickup_special.cpp
    potions.cpp
    advance.cpp
    # Combat-reward assembly + claim (combat_rewards.hpp).
    combat_rewards.cpp
    rest_sites.cpp
    # Treasure-room chest construction/opening + chest relic hooks.
    treasure_rooms.cpp
    # The post-boss chest: the three entry-time BOSS-pool pops and the
    # boss-relic screen's pure predicates (boss_chest.hpp).
    boss_chest.cpp
    # Merchant stock generation, pricing, purchases and the purge service.
    shop.cpp
    # ?-room roll + event selection/pool bookkeeping + dialog dispatch.
    event_framework.cpp
    # Native event bodies, grouped by delivery batch.
    events/exordium_events_i.cpp
    events/exordium_events_ii.cpp
    events/shrines.cpp
    events/one_time_specials.cpp
    events/city_events_i.cpp
    events/beyond_events.cpp
    # S2.32 -- City events II (five eventList bodies incl. the two combat
    # embeds) + the five act-gated one-timer bodies.
    events/city_events_ii.cpp
    events/city_one_timers.cpp
    # S3.31 -- the `Spire Heart` dialog: the Act-3 terminal and the Door. Not a
    # pool event (VictoryRoom constructs it), so it dispatches off the reserved
    # kSpireHeartEventId rather than through the generated native-event table.
    events/spire_heart.cpp
    # The floor-0 Neow blessing: category tables, payouts, sub-screens.
    neow.cpp
    run_advance.cpp
    # PublicView -- the T0.1 public-information observation record
    # (public_view.hpp): the pull encoder beside legal_actions().
    public_view.cpp
    # public_hash (public_view.hpp): the XXH3-64 identity of a public
    # information set. Its own TU because it includes xxhash.h with
    # XXH_INLINE_ALL, exactly as state_hash.cpp does.
    public_hash.cpp
    # resample_hidden -- the T0.4 belief sampler (resample.hpp): the plan §2.4
    # per-source table, drawn entirely from sampler-private randomness.
    resample.cpp
    # make_hidden_twin -- the T0.5 leak gate's state generator (twin.hpp): a
    # same-public draw from the resample_hidden belief, plus the PublicView
    # difference diagnostic every twin assertion reports through.
    twin.cpp
)

# PROJECT_SOURCE_DIR, never CMAKE_SOURCE_DIR: this is a PUBLIC include dir, so
# it is exported to every consumer. Under add_subdirectory() from another
# top-level (the training repo embeds the engine this way) CMAKE_SOURCE_DIR is
# the CONSUMER's root, so the old spelling handed each consumer its own
# <consumer>/include -- a silently wrong include path rather than a build error,
# since the engine's own TUs would still find the headers via relative paths.
# Only ./CMakeLists.txt calls project(), so PROJECT_SOURCE_DIR is the engine root
# whether this tree is the top level or embedded. Same rule for every other
# repo-owned path in this build; tools/check_embed_consumer.sh is the guard.
target_include_directories(sts_engine PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(sts_engine PUBLIC cxx_std_20)

# The engine's id enums and card/monster tables are the code-generated registry
# headers (types.hpp/cards.hpp/monster_jaw_worm.hpp re-export them). PUBLIC:
# engine public headers include the generated ones, so every consumer needs the
# generated include dir; the explicit add_dependencies orders codegen before any
# engine TU compiles.
target_link_libraries(sts_engine PUBLIC registry_generated)
add_dependencies(sts_engine registry_gen_headers)

# state_hash.cpp and public_hash.cpp include xxhash.h (XXH_INLINE_ALL, one
# internal-linkage copy each). PRIVATE: the header is an
# implementation detail of the engine; nothing in the public headers exposes it.
target_link_libraries(sts_engine PRIVATE xxhash)

sts_set_warnings(sts_engine)
sts_enable_sanitizers(sts_engine)
