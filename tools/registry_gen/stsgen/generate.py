"""The generation pass: load the registry, run every emitter, write the headers.

Determinism (design doc §4.3): entries are emitted sorted by id, domains in a
fixed order, no timestamps -- the same YAML yields byte-identical output on every
run.
"""

from __future__ import annotations

from pathlib import Path

from .emit import (emit_card_table, emit_encounter_table, emit_game_ids,
                   emit_ids, emit_manifest, emit_monster_table,
                   emit_potion_table, emit_power_table, emit_relic_table)
from .loader import load_registry


def write_if_changed(path: Path, content: str) -> None:
    """Write `content` (LF newlines, no BOM) only if it differs, so a rebuild
    with unchanged YAML does not touch mtimes needlessly."""
    data = content.encode("utf-8")
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as fh:
        fh.write(data)


def generate(registry_dir: Path, out_dir: Path) -> list[Path]:
    domains = load_registry(registry_dir)
    reg_out = out_dir / "sts" / "registry"
    outputs = {
        "ids.hpp": emit_ids(domains),
        "card_table.hpp": emit_card_table(domains),
        "power_table.hpp": emit_power_table(domains),
        "relic_table.hpp": emit_relic_table(domains),
        "potion_table.hpp": emit_potion_table(domains),
        "monster_table.hpp": emit_monster_table(domains),
        "encounter_table.hpp": emit_encounter_table(domains),
        "game_ids.hpp": emit_game_ids(domains),
        "manifest.hpp": emit_manifest(domains),
    }
    written = []
    for name, content in outputs.items():
        path = reg_out / name
        write_if_changed(path, content)
        written.append(path)
    return written
