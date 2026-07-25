# registry codegen (design doc B §4.3). Runs tools/registry_gen/gen.py at build
# time to turn registry/*.yaml into constexpr headers under the build tree
# (<build>/generated/sts/registry/*.hpp) -- NEVER committed. The engine consumes
# them directly (types.hpp/cards.hpp/monster_jaw_worm.hpp re-export the
# generated enums/tables into sts::engine -- the B2.2 skeleton migration), so
# this directory is added before src/engine and sts_engine links
# registry_generated.

# On Windows, CMake will happily select a Microsoft Store Python, and it is
# almost never the interpreter the developer installed PyYAML into -- a Store
# Python is sandboxed with its own site-packages. Two directories are involved
# and BOTH have to go: %LOCALAPPDATA%\Microsoft\WindowsApps holds the alias
# stubs, while the real executable lives under a versioned directory in
# C:\Program Files\WindowsApps, which is what CMake actually selected here.
# CMAKE_IGNORE_PATH matches whole directories rather than patterns, so the
# versioned ones are globbed.
if(WIN32)
    if(DEFINED ENV{LOCALAPPDATA})
        list(APPEND CMAKE_IGNORE_PATH "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps")
    endif()
    if(DEFINED ENV{ProgramW6432})
        file(GLOB _sts_store_pythons
            "$ENV{ProgramW6432}/WindowsApps/PythonSoftwareFoundation.Python*")
        foreach(_p IN LISTS _sts_store_pythons)
            if(IS_DIRECTORY "${_p}")
                list(APPEND CMAKE_IGNORE_PATH "${_p}")
            endif()
        endforeach()
    endif()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# The codegen needs PyYAML, and "an interpreter exists" does not imply "that
# interpreter can import yaml". Checking here turns a confusing mid-build
# failure -- a Python traceback inside a ninja rule, several hundred lines into
# the log, reported as a failure to generate nine header files -- into a
# configure-time message that names the interpreter and the fix. This fired for
# real on Windows, where CMake picked the Store Python 3.9 while PyYAML was
# installed in C:\Python39.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import yaml"
    RESULT_VARIABLE _sts_yaml_rc
    OUTPUT_QUIET ERROR_QUIET)
if(NOT _sts_yaml_rc EQUAL 0)
    message(FATAL_ERROR
        "The registry codegen needs PyYAML, and the selected interpreter cannot "
        "import it:\n    ${Python3_EXECUTABLE}\n"
        "Install it there (\"${Python3_EXECUTABLE}\" -m pip install pyyaml), or "
        "point CMake at an interpreter that has it:\n"
        "    cmake --preset <preset> -DPython3_EXECUTABLE=<path-to-python>")
endif()

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
