"""game_ids.hpp + manifest.hpp -- the cross-domain metadata headers.

Neither is a content table: game_ids.hpp is the translator's game_id<->enum join
and manifest.hpp is the per-domain row count the §7.4 dashboard reads, so both
are driven straight off DOMAINS rather than a domain-specific parse.
"""

from __future__ import annotations

from ..vocab import BANNER, DOMAINS, cpp_string, pascal


def emit_game_ids(domains: dict[str, list[dict]]) -> str:
    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <cstdint>", "#include <string_view>\n",
                      '#include "sts/registry/ids.hpp"\n',
                      "// game_id <-> enum string tables (design doc §2.6, §4.3):",
                      "// the translator's join between the game's string ids and",
                      "// the sim's u16 enums.\n",
                      "namespace sts::registry {\n"]
    for key, _filename, enum, _underlying in DOMAINS:
        if enum is None:
            continue
        entries = domains[key]
        lname = key[:-1] if key.endswith("s") else key  # cards -> card, etc.
        out.append(f"// --- {enum} <-> game_id ---")
        out.append(f"[[nodiscard]] inline std::string_view "
                   f"{lname}_game_id({enum} id) noexcept {{")
        out.append("    switch (id) {")
        for e in entries:
            out.append(f"        case {enum}::{e['name']}: "
                       f"return {cpp_string(e['game_id'])};")
        out.append("        case {}::NONE:".format(enum))
        out.append('        default: return "";')
        out.append("    }")
        out.append("}")
        param = "std::string_view s" if entries else "[[maybe_unused]] std::string_view s"
        out.append(f"[[nodiscard]] inline {enum} "
                   f"{lname}_from_game_id({param}) noexcept {{")
        for e in entries:
            out.append(f"    if (s == {cpp_string(e['game_id'])}) "
                       f"return {enum}::{e['name']};")
        out.append(f"    return {enum}::NONE;")
        out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"


def emit_manifest(domains: dict[str, list[dict]]) -> str:
    out: list[str] = [BANNER, "#pragma once\n", "#include <cstddef>\n",
                      "// Per-domain row counts (design doc §4.3): consumed by the",
                      "// tier-2 coverage check and the §7.4 dashboard.\n",
                      "namespace sts::registry::manifest {\n"]
    total = 0
    for key, _filename, _enum, _underlying in DOMAINS:
        n = len(domains[key])
        total += n
        out.append(f"inline constexpr std::size_t k{pascal(key)}Count = {n};")
    out.append(f"inline constexpr std::size_t kTotalCount = {total};")
    out.append("\n}  // namespace sts::registry::manifest")
    return "\n".join(out) + "\n"
