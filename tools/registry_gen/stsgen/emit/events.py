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
        "    for (const EventDef& row : kEventTable) {",
        "        if (row.id == id) return &row;",
        "    }",
        "    return nullptr;",
        "}\n",
        "// Living Wall transform pools. Membership follows transformCard:",
        "// RED uses every non-basic reward-pool card, COLORLESS uses every",
        "// uncommon/rare colorless card, and CURSE uses every poolable curse.",
        "// Registry iteration order is stable and the arrays self-complete as",
        "// later colorless content rows land.",
    ])
    cards = domains["cards"]
    transform_groups = {
        "Red": [c for c in cards
                if str(c.get("color", "")).upper() == "RED"
                and str(c.get("rarity", "")).upper()
                in ("COMMON", "UNCOMMON", "RARE")],
        "Colorless": [c for c in cards
                      if str(c.get("color", "")).upper() == "COLORLESS"
                      and str(c.get("rarity", "")).upper()
                      in ("UNCOMMON", "RARE")],
        "Curse": [c for c in cards if bool(c.get("curse_pool", False))],
    }
    for label, pool in transform_groups.items():
        out.append(
            f"inline constexpr std::array<CardId, {len(pool)}> "
            f"kEventTransform{label}Pool{{{{")
        for card in pool:
            out.append(f"    CardId::{card['name']},")
        out.append("}};")
    out.extend([
        "[[nodiscard]] inline constexpr uint8_t event_transform_color(",
        "    CardId id) noexcept {",
        "    switch (id) {",
    ])
    for card in cards:
        color = str(card.get("color", "")).upper()
        tag = 1 if color == "RED" else 2 if color == "COLORLESS" else 3 if color == "CURSE" else 0
        out.append(f"        case CardId::{card['name']}: return {tag};")
    out.extend([
        "        case CardId::NONE:",
        "        default: return 0;",
        "    }",
        "}\n",
        "// B4.13: the master-deck card RARITY, which CardDef does not carry.",
        "// Bonfire Elementals pays out by the offered card's rarity",
        "// (Bonfire.java:117-152), We Meet Again excludes BASIC",
        "// (WeMeetAgain.java:68-79) and Match and Keep deals one card per",
        "// rarity band (GremlinMatchGame.java:63-78). Emitted from the same",
        "// registry `rarity` column the pool builders read, so it cannot drift",
        "// from kIroncladCommonPool / kIroncladUncommonPool / kIroncladRarePool.",
        "enum class EventCardRarity : uint8_t {",
        "    NONE = 0,",
        "    BASIC = 1,",
        "    COMMON = 2,",
        "    UNCOMMON = 3,",
        "    RARE = 4,",
        "    SPECIAL = 5,",
        "    CURSE = 6,",
        "};\n",
        "[[nodiscard]] inline constexpr EventCardRarity event_card_rarity(",
        "    CardId id) noexcept {",
        "    switch (id) {",
    ])
    rarity_tags = {
        "BASIC": "BASIC",
        "COMMON": "COMMON",
        "UNCOMMON": "UNCOMMON",
        "RARE": "RARE",
        "SPECIAL": "SPECIAL",
        "CURSE": "CURSE",
    }
    for card in cards:
        # An absent/unknown column maps to NONE, exactly as
        # event_transform_color maps an unknown color to 0: cards.yaml's schema
        # does not make `rarity` mandatory, so this emitter must not be the
        # thing that rejects a row the loader accepts.
        tag = rarity_tags.get(str(card.get("rarity", "")).upper(), "NONE")
        out.append(
            f"        case CardId::{card['name']}: "
            f"return EventCardRarity::{tag};")
    out.extend([
        "        case CardId::NONE:",
        "        default: return EventCardRarity::NONE;",
        "    }",
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
