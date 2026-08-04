"""event_table.hpp -- native-event metadata + fail-loud body dispatch."""

from __future__ import annotations

from ..vocab import BANNER, fail

# The three canonical draw lists an event row can belong to. `pool` names which
# one holds the key, and it decides what `acts` means (S2.02):
#   EVENT   -- the per-act eventList (Exordium.java:223-236,
#              TheCity.java:185-198, TheBeyond.java:179-186). Each act builds
#              its OWN list, so `acts` is literal list membership.
#   SHRINE  -- the per-act shrineList (Exordium.java:238-246,
#              TheCity.java:211-217, TheBeyond.java:199-205). All three acts
#              add the same six keys, so every shrine row is acts [1, 2, 3].
#   SPECIAL -- specialOneTimeEventList, which is built ONCE in Exordium and
#              carried by reference across acts (CardCrawlGame.java:1102-1119;
#              only call site Exordium.java:54). Membership is therefore
#              act-independent and `acts` records the ACT HALF of the row's
#              getShrine gate instead (AbstractDungeon.java:1886-1936).
# In every case `acts` is the set of acts in which the row can be DRAWN, and
# `draw` carries whatever is left of the gate once the act test is removed.
EVENT_POOLS = ("EVENT", "SHRINE", "SPECIAL")
EVENT_ACTS = (1, 2, 3)


def _validate_conditions(event: dict) -> tuple[str, int]:
    """Check `conditions` and return (pool, act_mask). Mandatory on EVERY row,
    implemented or not: the draw gate is the identity metadata S2.13 consumes,
    and a row that arrives without it is exactly the silent drift this rejects."""
    name = event["name"]
    conditions = event.get("conditions")
    if not isinstance(conditions, dict) or not conditions:
        raise fail(
            f"events.yaml: event {name} needs non-empty conditions metadata "
            "(pool + acts + draw)")
    pool = conditions.get("pool")
    if pool not in EVENT_POOLS:
        raise fail(
            f"events.yaml: event {name} has unknown conditions.pool {pool!r}; "
            f"expected one of {', '.join(EVENT_POOLS)}")
    acts = conditions.get("acts")
    if not isinstance(acts, list) or not acts:
        raise fail(
            f"events.yaml: event {name} needs a non-empty conditions.acts list "
            "(the acts in which the row can be drawn)")
    if any(not isinstance(a, int) or isinstance(a, bool) or a not in EVENT_ACTS
           for a in acts):
        raise fail(
            f"events.yaml: event {name} conditions.acts must hold only act "
            f"numbers {EVENT_ACTS}, got {acts!r}")
    if acts != sorted(set(acts)):
        raise fail(
            f"events.yaml: event {name} conditions.acts must be strictly "
            f"ascending with no repeats, got {acts!r}")
    if not str(conditions.get("draw", "")).strip():
        raise fail(
            f"events.yaml: event {name} needs a conditions.draw gate string "
            "(ALWAYS when the act test is the whole gate)")
    mask = 0
    for a in acts:
        mask |= 1 << (a - 1)
    return pool, mask


def emit_event_table(domains: dict[str, list[dict]]) -> str:
    rows: list[dict] = []
    for event in domains["events"]:
        native = bool(event.get("native", False))
        implemented = bool(event.get("implemented", False))
        if implemented and not native:
            raise fail(
                f"events.yaml: event {event['name']} is implemented but not native")
        pool, act_mask = _validate_conditions(event)
        if implemented:
            options = event.get("options")
            a15 = event.get("a15")
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
            "pool": pool,
            "act_mask": act_mask,
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
        "// Which canonical draw list holds the row's key.",
        "//   EVENT   per-act eventList   (Exordium.java:223-236,",
        "//                                TheCity.java:185-198,",
        "//                                TheBeyond.java:179-186)",
        "//   SHRINE  per-act shrineList  (Exordium.java:238-246,",
        "//                                TheCity.java:211-217,",
        "//                                TheBeyond.java:199-205)",
        "//   SPECIAL specialOneTimeEventList (AbstractDungeon.java:1340-1358),",
        "//           built ONCE in Exordium and carried by reference across",
        "//           acts (CardCrawlGame.java:1102-1119).",
        "enum class EventPool : uint8_t {",
        "    NONE = 0,",
        "    EVENT = 1,",
        "    SHRINE = 2,",
        "    SPECIAL = 3,",
        "};\n",
        "// act_mask bit (act - 1). For EVENT/SHRINE rows it is literal per-act",
        "// list membership; for SPECIAL rows -- whose list is act-independent --",
        "// it is the ACT HALF of the row's getShrine gate",
        "// (AbstractDungeon.java:1886-1936). Either way it answers \"can this",
        "// key be drawn in act N\", and EventDef carries nothing of the",
        "// remaining gate (gold/HP/relic/map-position tests): those stay",
        "// conditions.draw prose in events.yaml until an engine consumer",
        "// (S2.13) implements them against the Java it cites.",
        "inline constexpr uint8_t kEventActMaskExordium = 0x1u;",
        "inline constexpr uint8_t kEventActMaskCity = 0x2u;",
        "inline constexpr uint8_t kEventActMaskBeyond = 0x4u;\n",
        "struct EventDef {",
        "    EventId id;",
        "    bool native;",
        "    bool implemented;",
        "    EventPool pool;",
        "    uint8_t act_mask;",
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
            f"EventPool::{row['pool']}, {row['act_mask']}, "
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
        "// `act` is 1-based (1 = Exordium, 2 = TheCity, 3 = TheBeyond). An",
        "// unknown id or an out-of-range act is false -- never a shift by a",
        "// negative or oversized amount.",
        "[[nodiscard]] inline constexpr bool event_in_act(",
        "    EventId id, int act) noexcept {",
        "    if (act < 1 || act > 3) return false;",
        "    const EventDef* row = event_def(id);",
        "    if (row == nullptr) return false;",
        "    return (row->act_mask & (1u << (act - 1))) != 0u;",
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
