"""power_table.hpp -- the B3.2 power hook -> effect-program bindings."""

from __future__ import annotations

from ..loader import power_id_map
from ..steps import POWER_DOMAIN, padded_step_literals, parse_steps
from ..vocab import (BANNER, HOOKS, POWER_STACK, POWER_TYPES, fail, pascal)


def emit_power_table(domains: dict[str, list[dict]]) -> str:
    powers = domains["powers"]
    power_ids = power_id_map(domains)

    # Resolve rows first so the array budgets and validation errors surface
    # before any output is emitted.
    rows = []
    max_hooks = 1        # floor 1 so std::array<PowerHookBinding, N> is valid
    max_hook_steps = 1   # floor 1 likewise
    for p in powers:
        ptype = p.get("type")
        if ptype not in POWER_TYPES:
            raise fail(f"powers.yaml: power {p['name']} has unsupported type "
                       f"{ptype!r} (known: {sorted(POWER_TYPES)})")
        stack = str(p.get("stack", "intensity")).lower()
        if stack not in POWER_STACK:
            raise fail(f"powers.yaml: power {p['name']} has unknown stack "
                       f"{stack!r} (known: {sorted(POWER_STACK)})")
        native = bool(p.get("native", False))

        raw_hooks = p.get("hooks") or {}
        if not isinstance(raw_hooks, dict):
            raise fail(f"powers.yaml: power {p['name']} 'hooks' must be a mapping "
                       f"of hook -> effect program, got {type(raw_hooks).__name__}")
        # Emit hooks sorted by their pinned Hook value (deterministic, order-stable).
        bindings = []
        for hook_name in sorted(raw_hooks, key=lambda h: HOOKS.get(h, 1 << 30)):
            if hook_name not in HOOKS:
                raise fail(f"powers.yaml: power {p['name']} has unknown hook "
                           f"{hook_name!r} (known: {sorted(HOOKS)})")
            # `target: SELF` is the power's owner; ALL_ENEMY/RANDOM_ENEMY fan out
            # at execute time. A step's 0 `amount` means the power's stack amount.
            steps = parse_steps(
                POWER_DOMAIN,
                f"powers.yaml: power {p['name']} hook {hook_name}",
                raw_hooks[hook_name], power_ids)
            if steps and native:
                raise fail(f"powers.yaml: power {p['name']} hook {hook_name} has "
                           f"a data program AND native: true -- a native power "
                           f"lists its hooks with an EMPTY program (the escape "
                           f"hatch handles the body)")
            if not steps and not native:
                raise fail(f"powers.yaml: power {p['name']} hook {hook_name} has "
                           f"an empty program but native is not set -- a data-"
                           f"bound hook needs at least one step")
            bindings.append((HOOKS[hook_name], hook_name, steps))
            max_hook_steps = max(max_hook_steps, len(steps))
        max_hooks = max(max_hooks, len(bindings))
        rows.append({"name": p["name"], "type": POWER_TYPES[ptype],
                     "stack": POWER_STACK[stack], "native": native,
                     "bindings": bindings})

    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <array>", "#include <cstdint>\n",
                      '#include "sts/registry/card_table.hpp"',
                      '#include "sts/registry/ids.hpp"\n',
                      "// Power hook->effect-program tables (design doc B §4.2; "
                      "B3.2). A power",
                      "// binds Hook points to effect programs (reusing "
                      "CardEffectStep) or is",
                      "// `native` (the escape hatch: power_hooks.cpp handles the "
                      "body, the",
                      "// binding lists the hook with an empty program). The frozen "
                      "dispatch order",
                      "// (stage-a §5.2-5.5) lives in the framework, not here. Types "
                      "are duplicated",
                      "// in sts::registry so this header compiles standalone; "
                      "powers.hpp pins them",
                      "// byte-equal to the engine's power_hooks.hpp.\n",
                      "namespace sts::registry {\n"]

    out.append("enum class PowerType : uint8_t {")
    for name, val in sorted(POWER_TYPES.items(), key=lambda kv: kv[1]):
        out.append(f"    {name} = {val},")
    out.append("};\n")
    out.append("enum class PowerStack : uint8_t {")
    for name, val in sorted(POWER_STACK.items(), key=lambda kv: kv[1]):
        out.append(f"    {name.upper()} = {val},")
    out.append("};\n")
    out.append("// Hook identity tags (MIRROR of power_hooks.hpp Hook; pinned, "
               "append-only).")
    out.append("enum class Hook : uint8_t {")
    for name, val in sorted(HOOKS.items(), key=lambda kv: kv[1]):
        out.append(f"    {name.upper()} = {val},")
    out.append("};")
    for name, val in sorted(HOOKS.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>(Hook::{name.upper()}) "
                   f"== {val}, \"Hook::{name.upper()} is pinned to {val} "
                   f"(append-only, never renumber)\");")
    out.append(f"inline constexpr int kPowerHookCount = {len(HOOKS)};\n")
    out.append(f"inline constexpr int kMaxPowerHooks = {max_hooks};")
    out.append(f"inline constexpr int kMaxPowerHookSteps = {max_hook_steps};\n")
    out.append("// One (hook, program) binding. A `native` power's programs are "
               "empty (step_count")
    out.append("// == 0); the framework routes those hooks to the native escape "
               "hatch instead.")
    out.append("struct PowerHookBinding {")
    out.append("    Hook hook;")
    out.append("    uint8_t step_count;")
    out.append("    std::array<CardEffectStep, kMaxPowerHookSteps> steps;")
    out.append("};\n")
    out.append("struct PowerDef {")
    out.append("    PowerId id;")
    out.append("    PowerType type;")
    out.append("    PowerStack stack;")
    out.append("    bool native;")
    out.append("    uint8_t hook_count;")
    out.append("    std::array<PowerHookBinding, kMaxPowerHooks> hooks;")
    out.append("    [[nodiscard]] constexpr const PowerHookBinding* "
               "hook_binding(Hook h) const noexcept {")
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
        for hval, hname, steps in bindings:
            lines.append(f"        {{Hook::{hname.upper()}, {len(steps)}, "
                         f"{{{{{pad_steps(steps)}}}}}}},")
        # Pad to kMaxPowerHooks with empty NOP bindings (hook value 0, count 0).
        empty_hook = next(k for k, v in HOOKS.items() if v == 0)
        while len(lines) < max_hooks:
            lines.append(f"        {{Hook::{empty_hook.upper()}, 0, "
                         f"{{{{{pad_steps([])}}}}}}},")
        return lines

    for r in rows:
        out.append(f"inline constexpr PowerDef k{pascal(r['name'])}Power{{")
        stack_name = next(k for k, v in POWER_STACK.items() if v == r["stack"])
        out.append(f"    PowerId::{r['name']}, PowerType::"
                   f"{next(k for k, v in POWER_TYPES.items() if v == r['type'])}, "
                   f"PowerStack::{stack_name.upper()}, "
                   f"{'true' if r['native'] else 'false'}, "
                   f"{len(r['bindings'])},")
        out.append("    {{")
        out.extend(pad_bindings(r["bindings"]))
        out.append("    }}};\n")

    out.append("[[nodiscard]] inline const PowerDef* "
               "power_def(PowerId id) noexcept {")
    out.append("    switch (id) {")
    for r in rows:
        out.append(f"        case PowerId::{r['name']}: "
                   f"return &k{pascal(r['name'])}Power;")
    out.append("        case PowerId::NONE:")
    out.append("        default: return nullptr;")
    out.append("    }")
    out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"
