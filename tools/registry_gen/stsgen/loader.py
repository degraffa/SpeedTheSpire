"""Registry YAML loading + the domain-independent validation pass.

Every domain file goes through the same gate: a top-level list of mappings, a
mandatory unique append-only `id`, a mandatory `provenance` string, and (where
the domain generates an enum) a unique UPPER_SNAKE `name`. Domains are returned
sorted by id so emission is deterministic (design doc §4.3).
"""

from __future__ import annotations

from pathlib import Path

import yaml

from .vocab import DOMAINS, IDENT_RE, fail


def load_domain(registry_dir: Path, filename: str) -> list[dict]:
    path = registry_dir / filename
    if not path.exists():
        raise fail(f"missing registry file: {path}")
    with path.open("r", encoding="utf-8") as fh:
        data = yaml.safe_load(fh)
    if data is None:
        data = []
    if not isinstance(data, list):
        raise fail(f"{filename}: top-level must be a YAML list, got {type(data).__name__}")
    for i, entry in enumerate(data):
        if not isinstance(entry, dict):
            raise fail(f"{filename}[{i}]: each entry must be a mapping")
    return data


def validate_common(filename: str, entries: list[dict], *, require_name: bool,
                    require_game_id: bool) -> None:
    """Validate mandatory fields + id/name uniqueness for one domain."""
    seen_ids: dict[int, str] = {}
    seen_names: dict[str, int] = {}
    for entry in entries:
        if "id" not in entry:
            raise fail(f"{filename}: entry missing mandatory 'id': {entry!r}")
        eid = entry["id"]
        if not isinstance(eid, int) or isinstance(eid, bool):
            raise fail(f"{filename}: id must be an integer, got {eid!r}")
        if eid < 1:
            raise fail(f"{filename}: id must be >= 1 (0 is reserved for NONE), got {eid}")
        label = entry.get("name", entry.get("game_id", "?"))
        if eid in seen_ids:
            raise fail(
                f"{filename}: duplicate/reused id {eid} "
                f"('{label}' collides with '{seen_ids[eid]}') -- "
                f"ids are append-only and must be unique")
        seen_ids[eid] = str(label)

        if "provenance" not in entry or not str(entry.get("provenance", "")).strip():
            raise fail(f"{filename}: entry id {eid} missing mandatory 'provenance'")
        if require_game_id and not str(entry.get("game_id", "")).strip():
            raise fail(f"{filename}: entry id {eid} missing mandatory 'game_id'")

        if require_name:
            name = entry.get("name")
            if not isinstance(name, str) or not IDENT_RE.match(name):
                raise fail(
                    f"{filename}: entry id {eid} 'name' must be an UPPER_SNAKE "
                    f"enum symbol, got {name!r}")
            if name in seen_names:
                raise fail(f"{filename}: duplicate name '{name}' (ids "
                           f"{seen_names[name]} and {eid})")
            seen_names[name] = eid


def load_registry(registry_dir: Path) -> dict[str, list[dict]]:
    domains: dict[str, list[dict]] = {}
    for key, filename, enum, _underlying in DOMAINS:
        entries = load_domain(registry_dir, filename)
        # a20 has no game_id concept; every other domain requires it. Names are
        # required only for domains that generate an enum.
        validate_common(
            filename, entries,
            require_name=enum is not None,
            require_game_id=(key != "a20"))
        domains[key] = sorted(entries, key=lambda e: e["id"])
    return domains


def power_id_map(domains: dict[str, list[dict]]) -> dict[str, int]:
    return {e["name"]: e["id"] for e in domains["powers"]}


def card_id_map(domains: dict[str, list[dict]]) -> dict[str, int]:
    # name -> CardId, so a MAKE_CARD step can reference a created card by symbol
    # (forward references allowed: the map covers every card in the domain).
    return {e["name"]: e["id"] for e in domains["cards"]}
