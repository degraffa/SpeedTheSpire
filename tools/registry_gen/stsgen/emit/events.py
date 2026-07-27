"""event_table.hpp -- native-event metadata + fail-loud body dispatch."""

from __future__ import annotations

from ..vocab import BANNER, fail


def emit_event_table(domains: dict[str, list[dict]]) -> str:
    rows: list[dict] = []
    for event in domains["events"]:
        native = bool(event.get("native", False))
        implemented = bool(event.get("implemented", False))
        if implemented and not native:
            raise fail(
                f"events.yaml: event {event['name']} is implemented but not native")
        if implemented:
            conditions = event.get("conditions")
            options = event.get("options")
            a15 = event.get("a15")
            if not isinstance(conditions, dict) or not conditions:
                raise fail(
                    f"events.yaml: implemented event {event['name']} needs "
                    "non-empty conditions metadata")
            if (not isinstance(options, list) or not options or
                    any(not isinstance(screen, list) or not screen
                        for screen in options)):
                raise fail(
                    f"events.yaml: implemented event {event['name']} needs "
                    "a non-empty list of non-empty option screens")
            if not isinstance(a15, dict) or "changes" not in a15:
                raise fail(
                    f"events.yaml: implemented event {event['name']} needs "
                    "explicit a15.changes metadata (an empty list means no branch)")
            changes = a15["changes"]
            if not isinstance(changes, list):
                raise fail(
                    f"events.yaml: event {event['name']} a15.changes must be a list")
            for change in changes:
                if (not isinstance(change, dict) or
                        not {"field", "base", "a15"}.issubset(change)):
                    raise fail(
                        f"events.yaml: event {event['name']} has malformed A15 "
                        "change; each needs field/base/a15")
        rows.append({
            "name": event["name"],
            "native": native,
            "implemented": implemented,
            "screen_count": len(event.get("options", [])),
            "a15_count": len(event.get("a15", {}).get("changes", [])),
        })

    out = [
        BANNER,
        "#pragma once\n",
        "#include <array>",
        "#include <cstddef>",
        "#include <cstdint>\n",
        '#include "sts/registry/ids.hpp"\n',
        "namespace sts::registry {\n",
        "struct EventDef {",
        "    EventId id;",
        "    bool native;",
        "    bool implemented;",
        "    uint8_t screen_count;",
        "    uint8_t a15_change_count;",
        "};\n",
        f"inline constexpr std::array<EventDef, {len(rows)}> kEventTable{{{{",
    ]
    for row in rows:
        out.append(
            f"    {{EventId::{row['name']}, "
            f"{'true' if row['native'] else 'false'}, "
            f"{'true' if row['implemented'] else 'false'}, "
            f"{row['screen_count']}, {row['a15_count']}}},")
    out.extend([
        "}};\n",
        "[[nodiscard]] inline constexpr const EventDef* event_def(",
        "    EventId id) noexcept {",
        "    const auto raw = static_cast<uint16_t>(id);",
        "    if (raw == 0 || raw > kEventTable.size()) return nullptr;",
        "    const EventDef& row = kEventTable[raw - 1];",
        "    return row.id == id ? &row : nullptr;",
        "}\n",
        "}  // namespace sts::registry\n",
        "// Implemented native-body dispatch. Expanding this table odr-uses every",
        "// handler, so marking a row implemented without a body is a link error.",
        "#define STS_REGISTRY_NATIVE_EVENTS(X) \\",
    ])
    implemented = [r for r in rows if r["implemented"]]
    if implemented:
        for i, row in enumerate(implemented):
            suffix = " \\" if i + 1 < len(implemented) else ""
            out.append(
                f"    X({row['name']}, event_native_{row['name'].lower()}){suffix}")
    else:
        out.append("    /* no implemented native events */")
    return "\n".join(out) + "\n"
