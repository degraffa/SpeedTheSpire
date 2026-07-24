"""potion_table.hpp -- the B3.23 potion USE effect programs + potency/rarity."""

from __future__ import annotations

from ..loader import power_id_map
from ..steps import POTION_DOMAIN, padded_step_literals, parse_steps
from ..vocab import BANNER, POTION_RARITIES, fail, pascal


def emit_potion_table(domains: dict[str, list[dict]]) -> str:
    potions = domains["potions"]
    power_ids = power_id_map(domains)

    # Resolve rows first so array budgets + validation errors surface before any
    # output is emitted.
    rows = []
    max_steps = 1  # floor 1 so std::array<CardEffectStep, N> stays valid when empty
    for p in potions:
        rarity = str(p.get("rarity", "")).upper()
        if rarity not in POTION_RARITIES:
            raise fail(f"potions.yaml: potion {p['name']} has unknown rarity "
                       f"{p.get('rarity')!r} (known: {sorted(POTION_RARITIES)})")
        if "potency" not in p or not isinstance(p["potency"], int) or \
                isinstance(p["potency"], bool):
            raise fail(f"potions.yaml: potion {p['name']} 'potency' must be an "
                       f"integer (getPotency value), got {p.get('potency')!r}")
        native = bool(p.get("native", False))
        # `target: SELF` is the player; CARD_TARGET is the used-on monster;
        # ALL_ENEMY fans out at execute time. The step `amount` is the potency.
        steps = parse_steps(POTION_DOMAIN,
                            f"potions.yaml: potion {p['name']}",
                            p.get("effects"), power_ids)
        # native XOR effect-program (mirrors the B3.2 power convention: a native
        # potion's USE body is the escape hatch, so it carries no data steps).
        if native and steps:
            raise fail(f"potions.yaml: potion {p['name']} is native AND has an "
                       f"effect program -- a native potion lists no effects (the "
                       f"escape hatch handles the body)")
        if not native and not steps:
            raise fail(f"potions.yaml: potion {p['name']} has no effect program "
                       f"and native is not set -- a data potion needs >= 1 step")
        max_steps = max(max_steps, len(steps))
        rows.append({"name": p["name"], "rarity": POTION_RARITIES[rarity],
                     "potency": p["potency"], "native": native, "steps": steps})

    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <array>", "#include <cstdint>\n",
                      '#include "sts/registry/card_table.hpp"',
                      '#include "sts/registry/ids.hpp"\n',
                      "// Potion USE-effect tables (design doc §5.4; B3.23). A "
                      "potion binds its",
                      "// USE to an effect program (reusing CardEffectStep) or is "
                      "`native` (the",
                      "// escape hatch: potions.cpp dispatch_native_potion handles "
                      "the body, and the",
                      "// row carries an empty program). potency is the "
                      "ascension-independent",
                      "// getPotency value; rarity drives the 65/25/10 reward tier "
                      "gate + the",
                      "// identity roll. Types are duplicated in sts::registry so "
                      "this header",
                      "// compiles standalone; potions.hpp re-exports it into "
                      "sts::engine.\n",
                      "namespace sts::registry {\n"]

    out.append("enum class PotionRarity : uint8_t {")
    for name, val in sorted(POTION_RARITIES.items(), key=lambda kv: kv[1]):
        out.append(f"    {name} = {val},")
    out.append("};")
    for name, val in sorted(POTION_RARITIES.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>(PotionRarity::{name}) "
                   f"== {val}, \"PotionRarity::{name} is pinned to {val} "
                   f"(append-only, never renumber)\");")
    out.append("")
    out.append(f"inline constexpr int kMaxPotionSteps = {max_steps};\n")
    out.append("// One potion registry entry: potency + rarity + the USE effect")
    out.append("// program (empty for a native potion; step_count == 0).")
    out.append("struct PotionDef {")
    out.append("    PotionId id;")
    out.append("    PotionRarity rarity;")
    out.append("    bool native;")
    out.append("    int32_t potency;")
    out.append("    uint8_t step_count;")
    out.append("    std::array<CardEffectStep, kMaxPotionSteps> steps;")
    out.append("};\n")

    def pad(steps) -> str:
        return ", ".join(padded_step_literals(steps, max_steps))

    for r in rows:
        rarity_name = next(k for k, v in POTION_RARITIES.items()
                           if v == r["rarity"])
        out.append(f"inline constexpr PotionDef k{pascal(r['name'])}Potion{{")
        out.append(f"    PotionId::{r['name']}, PotionRarity::{rarity_name}, "
                   f"{'true' if r['native'] else 'false'}, {r['potency']}, "
                   f"{len(r['steps'])},")
        out.append(f"    {{{{{pad(r['steps'])}}}}}}};\n")

    out.append("inline constexpr std::array<const PotionDef*, "
               f"{len(rows)}> kPotionDefs{{{{")
    for r in rows:
        out.append(f"    &k{pascal(r['name'])}Potion,")
    out.append("}};\n")
    out.append("[[nodiscard]] inline const PotionDef* "
               "potion_def(PotionId id) noexcept {")
    out.append("    switch (id) {")
    for r in rows:
        out.append(f"        case PotionId::{r['name']}: "
                   f"return &k{pascal(r['name'])}Potion;")
    out.append("        case PotionId::NONE:")
    out.append("        default: return nullptr;")
    out.append("    }")
    out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"
