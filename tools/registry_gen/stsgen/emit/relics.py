"""relic_table.hpp -- the B3.24 relic RelicHook -> effect-program bindings."""

from __future__ import annotations

from ..loader import power_id_map
from ..steps import RELIC_DOMAIN, padded_step_literals, parse_steps
from ..vocab import BANNER, RELIC_HOOKS, RELIC_TIERS, fail, pascal
from .powers import native_dispatch_macro

# The out-of-combat ("pickup") dispatch surfaces a relic row may override, in
# emission order. Unlike `hooks:` these are NOT combat hook points: they are the
# run-layer seams a relic can sit on, and they differ from each other in shape --
# `can_spawn` is a PREDICATE consulted while drawing from a pool (and is therefore
# RNG-visible: it decides whether a drawn id is kept or re-drawn), the other two
# are ACTIONS that mutate RunState. They get one X-macro each rather than one
# merged surface precisely because the signatures and defaults differ.
#
#   key -> (macro name, handler prefix, consuming translation unit, note)
PICKUP_SURFACES = {
    "can_spawn": ("STS_REGISTRY_RELIC_CAN_SPAWN", "relic_can_spawn_",
                  "relic_pools.cpp", None),
    "on_equip": ("STS_REGISTRY_RELIC_ON_EQUIP", "relic_on_equip_",
                 "relic_pools.cpp", None),
    "on_obtain_card": ("STS_REGISTRY_RELIC_ON_OBTAIN_CARD",
                       "relic_on_obtain_card_", "run_deck.cpp", None),
}


def parse_pickup(r: dict) -> set[str]:
    """The set of pickup surfaces relic row `r` overrides.

    `pickup:` is a mapping of surface -> true. There is deliberately no `false`
    spelling: absence is the only way to say "no override", so the row cannot
    carry two representations of the same fact. A surface listed here MUST have a
    native handler (the generated dispatch table odr-uses it), which is what makes
    a forgotten body a link error rather than a silently skipped case.
    """
    raw = r.get("pickup")
    if raw is None:
        return set()
    if not isinstance(raw, dict):
        raise fail(f"relics.yaml: relic {r['name']} 'pickup' must be a mapping of "
                   f"surface -> true, got {type(raw).__name__}")
    if not raw:
        raise fail(f"relics.yaml: relic {r['name']} has an empty 'pickup' mapping "
                   f"-- omit the key entirely instead")
    surfaces = set()
    for key, value in raw.items():
        if key not in PICKUP_SURFACES:
            raise fail(f"relics.yaml: relic {r['name']} has unknown pickup surface "
                       f"{key!r} (known: {sorted(PICKUP_SURFACES)})")
        if value is not True:
            raise fail(f"relics.yaml: relic {r['name']} pickup {key} must be "
                       f"literally `true`, got {value!r} -- omit the key to mean "
                       f"'no override' (there is no false spelling)")
        surfaces.add(key)
    return surfaces


def pickup_dispatch_macro(rows: list[dict], surface: str) -> list[str]:
    """The dispatch list for one pickup surface, as an X-macro.

    Same shape and the same guarantee as powers.py's native_dispatch_macro (which
    keys on `native: true`), but keyed on one `pickup:` surface and with its own
    handler-name prefix, so the three surfaces stay separate dispatch tables. The
    handler name is derived from the row name by the frozen convention
    (``WAR_PAINT`` -> ``relic_on_equip_war_paint``).
    """
    macro, prefix, consumer, note = PICKUP_SURFACES[surface]
    listed = [r for r in rows if surface in r["pickup"]]
    lines = [
        "// Pickup dispatch list: one entry per relic row whose `pickup:` lists",
        f"// {surface}, X(RELIC_ID, handler). {consumer} expands this for the",
        "// extern declarations AND for the id -> handler switch, so the table is a",
        "// projection of the registry instead of a hand-maintained switch.",
        "//",
        "// The handler is odr-used at the expansion site, so a row that lists the",
        "// surface but whose body nobody wrote is an UNDEFINED REFERENCE at link",
        "// time -- never a silent no-op. A relic whose override is deliberately",
        "// DEFERRED still lists the surface and defines an explicit empty body in",
        "// its tier translation unit, carrying the deferral reason and citation.",
    ]
    if note is not None:
        lines.append("//")
        words, line = note.split(), "//"
        for word in words:
            if len(line) + len(word) + 1 > 78:
                lines.append(line)
                line = "//"
            line += " " + word
        lines.append(line)
    if not listed:
        lines.append(f"#define {macro}(X)")
        return lines
    entries = [f"    X({r['name']}, {prefix}{r['name'].lower()})" for r in listed]
    width = max(len(e) for e in entries) + 1
    body = [f"{e:<{width}}\\" for e in entries[:-1]] + [entries[-1]]
    lines.append(f"#define {macro}(X) \\")
    lines.extend(body)
    return lines


def emit_relic_table(domains: dict[str, list[dict]]) -> str:
    relics = domains["relics"]
    power_ids = power_id_map(domains)

    rows = []
    max_hooks = 1        # floor 1 so std::array<RelicHookBinding, N> is valid
    max_hook_steps = 1   # floor 1 likewise
    pool_tiers = {"COMMON", "UNCOMMON", "RARE", "SHOP", "BOSS"}
    pool_orders: dict[str, set[int]] = {tier: set() for tier in pool_tiers}
    for r in relics:
        tier = r.get("tier")
        if tier not in RELIC_TIERS:
            raise fail(f"relics.yaml: relic {r['name']} has unsupported tier "
                       f"{tier!r} (known: {sorted(RELIC_TIERS)})")
        native = bool(r.get("native", False))
        raw_pool_order = r.get("pool_order", -1)
        if isinstance(raw_pool_order, bool) or not isinstance(raw_pool_order, int):
            raise fail(f"relics.yaml: relic {r['name']} pool_order must be an "
                       f"integer, got {raw_pool_order!r}")
        if tier in pool_tiers:
            if raw_pool_order < 0:
                raise fail(f"relics.yaml: pool relic {r['name']} ({tier}) must "
                           "define a non-negative pool_order")
            if raw_pool_order in pool_orders[tier]:
                raise fail(f"relics.yaml: duplicate {tier} pool_order "
                           f"{raw_pool_order}")
            pool_orders[tier].add(raw_pool_order)
        elif raw_pool_order != -1:
            raise fail(f"relics.yaml: non-pool relic {r['name']} ({tier}) must "
                       "omit pool_order")
        raw_initial_counter = r.get("initial_counter", -1)
        if (isinstance(raw_initial_counter, bool) or
                not isinstance(raw_initial_counter, int) or
                raw_initial_counter < -32768 or raw_initial_counter > 32767):
            raise fail(f"relics.yaml: relic {r['name']} initial_counter must be "
                       f"an int16, got {raw_initial_counter!r}")

        raw_hooks = r.get("hooks") or {}
        if not isinstance(raw_hooks, dict):
            raise fail(f"relics.yaml: relic {r['name']} 'hooks' must be a mapping "
                       f"of hook -> effect program, got {type(raw_hooks).__name__}")
        bindings = []
        for hook_name in sorted(raw_hooks, key=lambda h: RELIC_HOOKS.get(h, 1 << 30)):
            if hook_name not in RELIC_HOOKS:
                raise fail(f"relics.yaml: relic {r['name']} has unknown hook "
                           f"{hook_name!r} (known: {sorted(RELIC_HOOKS)})")
            # `target: SELF` is the player; ALL_ENEMY/RANDOM_ENEMY fan out at
            # execute time. Unlike powers, a relic step's amount is ALWAYS a
            # literal (relics carry no stack amount).
            steps = parse_steps(
                RELIC_DOMAIN,
                f"relics.yaml: relic {r['name']} hook {hook_name}",
                raw_hooks[hook_name], power_ids)
            if steps and native:
                raise fail(f"relics.yaml: relic {r['name']} hook {hook_name} has "
                           f"a data program AND native: true -- a native relic "
                           f"lists its hooks with an EMPTY program (the escape "
                           f"hatch handles the body)")
            if not steps and not native:
                raise fail(f"relics.yaml: relic {r['name']} hook {hook_name} has "
                           f"an empty program but native is not set -- a data-"
                           f"bound relic hook needs at least one step")
            bindings.append((RELIC_HOOKS[hook_name], hook_name, steps))
            max_hook_steps = max(max_hook_steps, len(steps))
        max_hooks = max(max_hooks, len(bindings))
        rows.append({"name": r["name"], "tier": RELIC_TIERS[tier],
                     "pool_order": raw_pool_order,
                     "initial_counter": raw_initial_counter,
                     "native": native, "bindings": bindings,
                     "pickup": parse_pickup(r)})

    for tier, orders in pool_orders.items():
        if orders and orders != set(range(len(orders))):
            raise fail(f"relics.yaml: {tier} pool_order values must be contiguous "
                       f"0..{len(orders) - 1}, got {sorted(orders)}")

    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <array>", "#include <cstdint>\n",
                      '#include "sts/registry/card_table.hpp"',
                      '#include "sts/registry/ids.hpp"\n',
                      "// Relic hook->effect-program tables (design doc B §4.2, "
                      "§5.3; B3.24). A relic",
                      "// binds RelicHook points to effect programs (reusing "
                      "CardEffectStep) or is",
                      "// `native` (the escape hatch: relic_hooks.cpp handles the "
                      "body, the binding",
                      "// lists the hook with an empty program). The frozen "
                      "ACQUISITION-order dispatch",
                      "// (stage-a trap 8) lives in the framework, not here. Types "
                      "are duplicated in",
                      "// sts::registry so this header compiles standalone; "
                      "relics.hpp pins RelicHook",
                      "// byte-equal to the engine's relic_hooks.hpp.\n",
                      "namespace sts::registry {\n"]

    out.append("enum class RelicTier : uint8_t {")
    for name, val in sorted(RELIC_TIERS.items(), key=lambda kv: kv[1]):
        out.append(f"    {name} = {val},")
    out.append("};")
    for name, val in sorted(RELIC_TIERS.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>(RelicTier::{name}) "
                   f"== {val}, \"RelicTier::{name} is pinned to {val} "
                   f"(append-only, never renumber)\");")
    out.append("")
    out.append("// Hook identity tags (MIRROR of relic_hooks.hpp RelicHook; "
               "pinned, append-only).")
    out.append("enum class RelicHook : uint8_t {")
    for name, val in sorted(RELIC_HOOKS.items(), key=lambda kv: kv[1]):
        out.append(f"    {name.upper()} = {val},")
    out.append("};")
    for name, val in sorted(RELIC_HOOKS.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>(RelicHook::{name.upper()}) "
                   f"== {val}, \"RelicHook::{name.upper()} is pinned to {val} "
                   f"(append-only, never renumber)\");")
    out.append(f"inline constexpr int kRelicHookCount = {len(RELIC_HOOKS)};\n")
    out.append(f"inline constexpr int kMaxRelicHooks = {max_hooks};")
    out.append(f"inline constexpr int kMaxRelicHookSteps = {max_hook_steps};\n")
    out.append("// One (hook, program) binding. A `native` relic's programs are "
               "empty (step_count")
    out.append("// == 0); the framework routes those hooks to the native escape "
               "hatch instead.")
    out.append("struct RelicHookBinding {")
    out.append("    RelicHook hook;")
    out.append("    uint8_t step_count;")
    out.append("    std::array<CardEffectStep, kMaxRelicHookSteps> steps;")
    out.append("};\n")
    out.append("struct RelicDef {")
    out.append("    RelicId id;")
    out.append("    RelicTier tier;")
    out.append("    int16_t pool_order;       // pre-shuffle RelicLibrary order; -1 = not pooled")
    out.append("    int16_t initial_counter;  // post-construction/onEquip counter encoding")
    out.append("    bool native;")
    out.append("    uint8_t hook_count;")
    out.append("    std::array<RelicHookBinding, kMaxRelicHooks> hooks;")
    out.append("    [[nodiscard]] constexpr const RelicHookBinding* "
               "hook_binding(RelicHook h) const noexcept {")
    out.append("        for (uint8_t i = 0; i < hook_count; ++i) {")
    out.append("            if (hooks[i].hook == h) { return &hooks[i]; }")
    out.append("        }")
    out.append("        return nullptr;")
    out.append("    }")
    out.append("};\n")

    def pad_steps(steps) -> str:
        return ", ".join(padded_step_literals(steps, max_hook_steps))

    def pad_bindings(bindings) -> list[str]:
        lines = []
        for _hval, hname, steps in bindings:
            lines.append(f"        {{RelicHook::{hname.upper()}, {len(steps)}, "
                         f"{{{{{pad_steps(steps)}}}}}}},")
        empty_hook = next(k for k, v in RELIC_HOOKS.items() if v == 0)
        while len(lines) < max_hooks:
            lines.append(f"        {{RelicHook::{empty_hook.upper()}, 0, "
                         f"{{{{{pad_steps([])}}}}}}},")
        return lines

    for r in rows:
        out.append(f"inline constexpr RelicDef k{pascal(r['name'])}Relic{{")
        tier_name = next(k for k, v in RELIC_TIERS.items() if v == r["tier"])
        out.append(f"    RelicId::{r['name']}, RelicTier::{tier_name}, "
                   f"{r['pool_order']}, {r['initial_counter']}, "
                   f"{'true' if r['native'] else 'false'}, "
                   f"{len(r['bindings'])},")
        out.append("    {{")
        out.extend(pad_bindings(r["bindings"]))
        out.append("    }}};\n")

    out.append("inline constexpr std::array<const RelicDef*, "
               f"{len(rows)}> kRelicDefs{{{{")
    for r in rows:
        out.append(f"    &k{pascal(r['name'])}Relic,")
    out.append("}};\n")
    out.append("[[nodiscard]] inline const RelicDef* "
               "relic_def(RelicId id) noexcept {")
    out.append("    switch (id) {")
    for r in rows:
        out.append(f"        case RelicId::{r['name']}: "
                   f"return &k{pascal(r['name'])}Relic;")
    out.append("        case RelicId::NONE:")
    out.append("        default: return nullptr;")
    out.append("    }")
    out.append("}\n")
    out.append("}  // namespace sts::registry\n")
    out.extend(native_dispatch_macro(
        rows, "STS_REGISTRY_NATIVE_RELICS", "relic_native_",
        "relic", "relic_hooks.cpp"))
    for surface in PICKUP_SURFACES:
        out.append("")
        out.extend(pickup_dispatch_macro(rows, surface))
    return "\n".join(out) + "\n"
