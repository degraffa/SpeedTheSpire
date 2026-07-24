# registry codegen (design doc B §4.3). Runs tools/registry_gen/gen.py at build
# time to turn registry/*.yaml into constexpr headers under the build tree
# (<build>/generated/sts/registry/*.hpp) -- NEVER committed. The engine consumes
# them directly (types.hpp/cards.hpp/monster_jaw_worm.hpp re-export the
# generated enums/tables into sts::engine -- the B2.2 skeleton migration), so
# this directory is added before src/engine and sts_engine links
# registry_generated.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Expose the interpreter to sibling scopes (tests/) so registry_gen_test can spawn
# the same Python for its determinism / duplicate-id checks.
set(STS_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE INTERNAL "Python for registry codegen")

set(REGISTRY_DIR "${CMAKE_SOURCE_DIR}/registry")
set(REGISTRY_GEN_PY "${CMAKE_CURRENT_SOURCE_DIR}/gen.py")
# gen.py is only the CLI; the generator lives in the sibling stsgen package.
# Listed explicitly (not globbed) so adding a module is a deliberate edit, and so
# editing an emitter re-runs generation -- a missing entry here means stale
# headers that no build would notice.
set(REGISTRY_GEN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/__init__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/generate.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/loader.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/steps.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/vocab.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/__init__.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/cards.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/encounters.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/ids.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/meta.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/monsters.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/potions.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/powers.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/stsgen/emit/relics.py")
set(GEN_OUT_ROOT "${CMAKE_BINARY_DIR}/generated")
set(STS_REGISTRY_GEN_PY "${REGISTRY_GEN_PY}" CACHE INTERNAL "registry codegen script")
set(STS_REGISTRY_DIR "${REGISTRY_DIR}" CACHE INTERNAL "registry YAML dir")

# The YAML inputs (explicit, not globbed, so a new domain is a deliberate edit).
set(REGISTRY_YAML
    "${REGISTRY_DIR}/cards.yaml"
    "${REGISTRY_DIR}/powers.yaml"
    "${REGISTRY_DIR}/monsters.yaml"
    "${REGISTRY_DIR}/encounters.yaml"
    "${REGISTRY_DIR}/relics.yaml"
    "${REGISTRY_DIR}/potions.yaml"
    "${REGISTRY_DIR}/events.yaml"
    "${REGISTRY_DIR}/a20.yaml")

set(GEN_HEADERS
    "${GEN_OUT_ROOT}/sts/registry/ids.hpp"
    "${GEN_OUT_ROOT}/sts/registry/card_table.hpp"
    "${GEN_OUT_ROOT}/sts/registry/power_table.hpp"
    "${GEN_OUT_ROOT}/sts/registry/relic_table.hpp"
    "${GEN_OUT_ROOT}/sts/registry/potion_table.hpp"
    "${GEN_OUT_ROOT}/sts/registry/monster_table.hpp"
    "${GEN_OUT_ROOT}/sts/registry/encounter_table.hpp"
    "${GEN_OUT_ROOT}/sts/registry/game_ids.hpp"
    "${GEN_OUT_ROOT}/sts/registry/manifest.hpp")

add_custom_command(
    OUTPUT ${GEN_HEADERS}
    COMMAND ${Python3_EXECUTABLE} "${REGISTRY_GEN_PY}"
            --registry "${REGISTRY_DIR}" --out "${GEN_OUT_ROOT}"
    DEPENDS "${REGISTRY_GEN_PY}" ${REGISTRY_GEN_SOURCES} ${REGISTRY_YAML}
    COMMENT "Generating registry headers from registry/*.yaml"
    VERBATIM)

add_custom_target(registry_gen_headers DEPENDS ${GEN_HEADERS})

# Include-only handle for consumers: add the build-tree generated root to the
# include path so `#include "sts/registry/ids.hpp"` resolves. Consumers must also
# add_dependencies(<target> registry_gen_headers) to order the generation.
add_library(registry_generated INTERFACE)
target_include_directories(registry_generated INTERFACE "${GEN_OUT_ROOT}")
add_dependencies(registry_generated registry_gen_headers)
