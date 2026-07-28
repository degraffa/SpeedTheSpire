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
        "// THE EVENT-GRID TRANSFORM POOLS ARE DELIBERATELY NOT EMITTED HERE.",
        "// Living Wall / Transmorgrifier reach AbstractDungeon.transformCard",
        "// (AbstractDungeon.java:860-878) and its",
        "// returnTrulyRandomCardFromAvailable list (:1016-1045) -- the SAME list",
        "// Neow's transform grid reaches, differing only in which Random is",
        "// passed. `sts::engine::transform_card` (card_pools.hpp) is its one",
        "// authority; it reads kIroncladCommonPool forwards and then",
        "// kIroncladUncommonPool / kIroncladRarePool BACKWARDS, because",
        "// initializeCardPools copies the src* pools with the PREPENDING",
        "// addToBottom (:1180-1199, CardGroup.java:459-461).",
        "//",
        "// This file used to emit kEventTransform{Red,Colorless,Curse}Pool: the",
        "// same MEMBERSHIP as kIronclad{Common,Uncommon,Rare}Pool / kColorlessPool",
        "// / kPoolableCurses, in a THIRD order (a plain walk of cards.yaml rows).",
        "// That second rendering drifted from the first and was the whole",
        "// remaining divergence of replayed run STS00051, whose floor-2 Living",
        "// Wall the game answered with Havoc and the emitted pool with Iron Wave.",
        "// Re-emitting a pool for this call site re-creates that failure mode:",
        "// the order lives in exactly one place now, next to the Java it cites.",
    ])
    cards = domains["cards"]
    out.extend([
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
