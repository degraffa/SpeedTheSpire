#!/usr/bin/env python3
"""Registry code generator (Stage B design doc §4.3) -- command-line driver.

Turns the rules-as-data YAML under ``registry/`` into ``constexpr`` C++ headers
under the build tree (``<build>/generated/sts/registry/*.hpp``). The YAML is the
single source of truth; nothing here is hand-maintained C++, and the generated
headers are never committed.

Emitted headers (namespace ``sts::registry``):
  * ``ids.hpp``           -- CardId/PowerId/MonsterId/RelicId/PotionId/EventId
                             enums, each re-pinning every id with a
                             ``static_assert``.
  * ``card_table.hpp``    -- the ``CardDef``/``CardEffectStep`` effect-program
                             table (the shape stage-a §6 froze).
  * ``monster_table.hpp`` -- the ``MonsterDef`` stat/move tables: per-ascension-
                             tier HP and amount columns (design doc §4.2) plus
                             each move's intent and effect program.
  * ``game_ids.hpp``      -- game_id<->enum string tables for the translator.
  * ``manifest.hpp``      -- per-domain row counts.

Determinism (design doc §4.3): entries are emitted sorted by id, domains in a
fixed order, no timestamps -- the same YAML yields byte-identical output on every
run. Any duplicate/reused id, or a missing mandatory field, fails generation with
a clear ``error:`` message on stderr and a non-zero exit.

The generated tables ARE the engine's tables: ``types.hpp``/``cards.hpp``/
``monster_jaw_worm.hpp`` re-export them into ``sts::engine`` via using-aliases
(the skeleton migration, design doc §4.4 -- no hand copies remain). They live in
``sts::registry`` so the headers compile standalone (registry_gen_standalone.cpp)
and tools that never link the engine (the translator) can consume them directly.

The implementation lives in the sibling ``stsgen`` package (vocabularies, loader,
the one effect-step parser, and one emitter module per header); this file is only
the CLI.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Keep the source tree clean: the generator runs from a build custom command, so
# it must not litter tools/registry_gen with __pycache__ directories.
sys.dont_write_bytecode = True
# CMake invokes this by absolute path, which puts THIS directory on sys.path[0];
# make that explicit so the package also resolves when the module is imported or
# executed through a symlink.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from stsgen import GenError, generate  # noqa: E402  (must follow the path setup)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="SpeedTheSpire registry codegen")
    parser.add_argument("--registry", required=True, type=Path,
                        help="directory holding the registry/*.yaml files")
    parser.add_argument("--out", required=True, type=Path,
                        help="output root (headers land under <out>/sts/registry/)")
    args = parser.parse_args(argv)
    try:
        written = generate(args.registry, args.out)
    except GenError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    for path in written:
        print(f"generated {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
