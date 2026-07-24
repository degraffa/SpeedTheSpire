"""ids.hpp -- the CardId/PowerId/MonsterId/RelicId/PotionId/EventId enums."""

from __future__ import annotations

from ..vocab import BANNER, DOMAINS


def emit_ids(domains: dict[str, list[dict]]) -> str:
    out: list[str] = [BANNER, "#pragma once\n", "#include <cstdint>\n",
                      "namespace sts::registry {\n"]
    for key, _filename, enum, underlying in DOMAINS:
        if enum is None:
            continue
        entries = domains[key]
        out.append(f"// {enum} -- generated from {key}.yaml (design doc §4.2).")
        out.append(f"enum class {enum} : {underlying} {{")
        out.append("    NONE = 0,")
        for e in entries:
            out.append(f"    {e['name']} = {e['id']},")
        out.append("};\n")
        # Re-pin every id (design doc §4.4): a renumber becomes a compile error.
        for e in entries:
            out.append(
                f"static_assert(static_cast<{underlying}>({enum}::{e['name']}) "
                f"== {e['id']}, \"{enum}::{e['name']} id is pinned to {e['id']} "
                f"(append-only, never renumber)\");")
        out.append("")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"
