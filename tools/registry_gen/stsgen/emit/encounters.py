"""encounter_table.hpp -- the B3.12 Exordium pool weights + miscRng composition
programs."""

from __future__ import annotations

from ..vocab import BANNER, COMP_OPS, ENCOUNTER_POOLS, cpp_string, fail


def parse_comp_node(owner: str, node) -> dict:
    """Parse one composition-program node into a flat CompStep dict:
    {op, count, aux, choice_bool, refs:[game_id, ...]}. Ref layout per op is the
    contract the engine's resolve_composition reads (see encounter_table emit)."""
    if not isinstance(node, dict) or len(node) != 1:
        raise fail(f"{owner}: each program node must be a single-key mapping "
                   f"(emit/bool/seq_bool/pick/pool), got {node!r}")
    (kind, body), = node.items()

    def _gid(v):
        if not isinstance(v, str) or not v.strip():
            raise fail(f"{owner}: monster game_id must be a non-empty string, "
                       f"got {v!r}")
        return v

    if kind == "emit":
        return {"op": "EMIT", "count": 0, "aux": 0, "choice_bool": 0,
                "refs": [_gid(body)]}
    if kind == "bool":
        if not isinstance(body, dict) or "if_true" not in body or "if_false" not in body:
            raise fail(f"{owner}: bool node needs {{if_true, if_false}}, got {body!r}")
        return {"op": "BOOL", "count": 0, "aux": 0, "choice_bool": 0,
                "refs": [_gid(body["if_true"]), _gid(body["if_false"])]}
    if kind == "seq_bool":
        if (not isinstance(body, dict) or not isinstance(body.get("if_true"), list)
                or not isinstance(body.get("if_false"), list)):
            raise fail(f"{owner}: seq_bool node needs {{if_true:[...], "
                       f"if_false:[...]}}, got {body!r}")
        t = [_gid(g) for g in body["if_true"]]
        f = [_gid(g) for g in body["if_false"]]
        if not t or not f:
            raise fail(f"{owner}: seq_bool sequences must be non-empty")
        return {"op": "SEQ_BOOL", "count": len(t), "aux": len(f),
                "choice_bool": 0, "refs": t + f}
    if kind == "pick":
        if not isinstance(body, list) or not body:
            raise fail(f"{owner}: pick node needs a non-empty choice list")
        refs: list[str] = []
        choice_bool = 0
        for i, ch in enumerate(body):
            if not isinstance(ch, dict) or len(ch) != 1:
                raise fail(f"{owner}: pick choice {i} must be a single-key "
                           f"{{emit}} or {{bool}}, got {ch!r}")
            (ck, cb), = ch.items()
            if ck == "emit":
                refs += [_gid(cb), ""]           # MON: refs[2i] used, refs[2i+1] pad
            elif ck == "bool":
                if not isinstance(cb, dict) or "if_true" not in cb or "if_false" not in cb:
                    raise fail(f"{owner}: pick bool choice {i} needs "
                               f"{{if_true, if_false}}, got {cb!r}")
                choice_bool |= (1 << i)
                refs += [_gid(cb["if_true"]), _gid(cb["if_false"])]
            else:
                raise fail(f"{owner}: pick choice {i} kind must be emit/bool, "
                           f"got {ck!r}")
        return {"op": "PICK", "count": len(body), "aux": 0,
                "choice_bool": choice_bool, "refs": refs}
    if kind == "pool":
        if (not isinstance(body, dict) or not isinstance(body.get("members"), list)
                or not body["members"]):
            raise fail(f"{owner}: pool node needs {{members:[...], count:n}}, "
                       f"got {body!r}")
        cnt = body.get("count")
        if not isinstance(cnt, int) or isinstance(cnt, bool) or not (1 <= cnt <= len(body["members"])):
            raise fail(f"{owner}: pool count must be an int in [1, len(members)], "
                       f"got {cnt!r}")
        return {"op": "POOL", "count": cnt, "aux": 0, "choice_bool": 0,
                "refs": [_gid(g) for g in body["members"]]}
    raise fail(f"{owner}: unknown program node kind {kind!r} "
               f"(known: emit/bool/seq_bool/pick/pool)")


def parse_encounter(entry: dict) -> dict:
    game_id = entry["game_id"]
    owner = f"encounters.yaml: encounter '{game_id}' (id {entry['id']})"
    pool = entry.get("pool")
    if pool not in ENCOUNTER_POOLS:
        raise fail(f"{owner}: 'pool' must be one of {sorted(ENCOUNTER_POOLS)}, "
                   f"got {pool!r}")
    act = entry.get("act")
    if not isinstance(act, int) or isinstance(act, bool) or act < 1:
        raise fail(f"{owner}: 'act' must be an integer >= 1, got {act!r}")
    weight = entry.get("weight")
    if isinstance(weight, bool) or not isinstance(weight, (int, float)):
        raise fail(f"{owner}: 'weight' must be a number, got {weight!r}")
    excludes = entry.get("excludes", [])
    if excludes is None:
        excludes = []
    if not isinstance(excludes, list) or not all(isinstance(x, str) for x in excludes):
        raise fail(f"{owner}: 'excludes' must be a list of encounter game_id "
                   f"strings, got {excludes!r}")
    if excludes and pool != "WEAK":
        raise fail(f"{owner}: 'excludes' is only valid on WEAK encounters "
                   f"(Exordium.generateExclusions keys on the 3rd weak monster)")
    raw_program = entry.get("program")
    if not isinstance(raw_program, list) or not raw_program:
        raise fail(f"{owner}: 'program' must be a non-empty list of nodes")
    program = [parse_comp_node(owner, n) for n in raw_program]
    return {"id": entry["id"], "game_id": game_id, "pool": pool, "act": act,
            "weight": float(weight), "excludes": excludes, "program": program}


def emit_encounter_table(domains: dict[str, list[dict]]) -> str:
    encs = [parse_encounter(e) for e in domains["encounters"]]

    # Cross-check: every `excludes` entry names a real STRONG encounter key in the
    # same act (the exclusion loop rejects strong-pool rolls by key) -- OR the
    # row's own key. The second case is TheBeyond's INERT self-exclusion
    # (TheBeyond.java:135-138 adds "Orb Walker", a weak-only key the first-strong
    # loop can never match); the registry models it verbatim rather than dropping
    # it, so the escape hatch is exactly that shape and nothing wider. A typo'd
    # or otherwise unknown key still fails.
    for e in encs:
        strong_keys = {o["game_id"] for o in encs
                       if o["pool"] == "STRONG" and o["act"] == e["act"]}
        for x in e["excludes"]:
            if x in strong_keys or x == e["game_id"]:
                continue
            raise fail(f"encounters.yaml: encounter '{e['game_id']}' excludes "
                       f"'{x}', which is neither a STRONG encounter in act "
                       f"{e['act']} nor its own key (an inert self-exclusion)")

    # Array budgets, floored at 1 so the header stays valid when the domain is empty.
    max_refs = 1
    max_steps = 1
    max_excl = 0
    for e in encs:
        max_steps = max(max_steps, len(e["program"]))
        max_excl = max(max_excl, len(e["excludes"]))
        for s in e["program"]:
            max_refs = max(max_refs, len(s["refs"]))
    max_excl = max(max_excl, 1)

    out: list[str] = [BANNER, "#pragma once\n",
                      "#include <array>", "#include <cstdint>",
                      "#include <string_view>\n",
                      "// Encounter framework tables (design doc §5.2, B3.12): the "
                      "Exordium pool",
                      "// weights + exclusions the monsterRng list-generation reads, "
                      "and the miscRng",
                      "// COMPOSITION PROGRAMS the spawn resolver interprets. Monster "
                      "refs are the",
                      "// game's AbstractMonster.ID strings (join keys to "
                      "monsters.yaml).\n",
                      "namespace sts::registry {\n"]

    out.append("// Which generateXxx pool an encounter belongs to (Exordium.java). "
               "Pinned/append-only.")
    out.append("enum class EncounterPool : uint8_t {")
    for pname, val in sorted(ENCOUNTER_POOLS.items(), key=lambda kv: kv[1]):
        out.append(f"    {pname} = {val},")
    out.append("};")
    for pname, val in sorted(ENCOUNTER_POOLS.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>(EncounterPool::{pname}) "
                   f"== {val}, \"EncounterPool::{pname} is pinned to {val} "
                   f"(append-only)\");")
    out.append("")
    out.append("// miscRng composition-program op (B3.12). Pinned/append-only.")
    out.append("enum class CompOp : uint8_t {")
    for cname, val in sorted(COMP_OPS.items(), key=lambda kv: kv[1]):
        out.append(f"    {cname} = {val},")
    out.append("};")
    for cname, val in sorted(COMP_OPS.items(), key=lambda kv: kv[1]):
        out.append(f"static_assert(static_cast<uint8_t>(CompOp::{cname}) == {val}, "
                   f"\"CompOp::{cname} is pinned to {val} (append-only)\");")
    out.append("")

    out.append(f"inline constexpr int kMaxCompRefs = {max_refs};")
    out.append(f"inline constexpr int kMaxCompSteps = {max_steps};")
    out.append(f"inline constexpr int kMaxEncounterExcludes = {max_excl};\n")

    out.append("// One composition step. Ref layout per op (resolve_composition "
               "reads this):")
    out.append("//   EMIT     refs[0] = the monster.                    (0 draws)")
    out.append("//   BOOL     refs[0]=if_true, refs[1]=if_false.        (1 randomBoolean)")
    out.append("//   SEQ_BOOL count=n_true, aux=n_false; refs[0..count) = if_true seq,")
    out.append("//            refs[count..count+aux) = if_false seq.     (1 randomBoolean)")
    out.append("//   PICK     count=nchoices; choice i uses refs[2i] (MON, or BOOL if_true)")
    out.append("//            and refs[2i+1] (BOOL if_false); choice_bool bit i set => that")
    out.append("//            choice is a BOOL (draws 1 randomBoolean during list build).")
    out.append("//            Then 1 random(0,nchoices-1) selects.       (nbool + 1 draws)")
    out.append("//   POOL     count=draw count; refs[0..ref_count) = pool members;")
    out.append("//            draw-without-replacement.                  (count draws)")
    out.append("struct CompStep {")
    out.append("    CompOp op;")
    out.append("    uint8_t count;        // POOL draw count / PICK nchoices / SEQ_BOOL n_true")
    out.append("    uint8_t aux;          // SEQ_BOOL n_false")
    out.append("    uint8_t choice_bool;  // PICK: bit i => choice i is a BOOL")
    out.append("    uint8_t ref_count;    // used entries in refs")
    out.append("    std::array<std::string_view, kMaxCompRefs> refs;")
    out.append("};\n")

    out.append("struct EncounterDef {")
    out.append("    uint8_t id;")
    out.append("    EncounterPool pool;")
    out.append("    int32_t act;")
    out.append("    float weight;         // MonsterInfo weight (BOSS = 0)")
    out.append("    std::string_view game_id;  // Exordium pool / getEncounter key")
    out.append("    uint8_t step_count;")
    out.append("    std::array<CompStep, kMaxCompSteps> program;")
    out.append("    uint8_t exclude_count;")
    out.append("    std::array<std::string_view, kMaxEncounterExcludes> excludes;")
    out.append("};\n")

    def sv(s: str) -> str:
        return "std::string_view{" + cpp_string(s) + "}" if s else "std::string_view{}"

    def comp_step_literal(s: dict) -> str:
        refs = list(s["refs"])
        ref_count = len(refs)
        while len(refs) < max_refs:
            refs.append("")
        refs_txt = ", ".join(sv(r) for r in refs)
        return (f"{{CompOp::{s['op']}, {s['count']}, {s['aux']}, "
                f"{s['choice_bool']}, {ref_count}, {{{{{refs_txt}}}}}}}")

    out.append(f"inline constexpr int kEncounterCount = {len(encs)};")
    out.append("inline constexpr std::array<EncounterDef, kEncounterCount> "
               "kEncounters{{")
    for e in encs:
        steps = list(e["program"])
        step_txts = [f"        {comp_step_literal(s)}," for s in steps]
        # pad program to kMaxCompSteps with EMIT-of-nothing NOP-ish steps (never read)
        while len(step_txts) < max_steps:
            pad_refs = ", ".join("std::string_view{}" for _ in range(max_refs))
            step_txts.append(f"        {{CompOp::EMIT, 0, 0, 0, 0, "
                             f"{{{{{pad_refs}}}}}}},")
        excl = list(e["excludes"])
        excl_count = len(excl)
        while len(excl) < max_excl:
            excl.append("")
        excl_txt = ", ".join(sv(x) for x in excl)
        out.append(f"    EncounterDef{{{e['id']}, EncounterPool::{e['pool']}, "
                   f"{e['act']}, {e['weight']}f, {sv(e['game_id'])},")
        out.append(f"        {len(steps)}, {{{{")
        out.extend(step_txts)
        out.append("    }},")
        out.append(f"        {excl_count}, {{{{{excl_txt}}}}}}},")
    out.append("}};\n")

    # --- Per-act pool tables (S2.01) ----------------------------------------
    # One row per (act, pool) group that has members. Members are listed in the
    # game's ArrayList add() order, which the registry's id order reproduces --
    # that order is what MonsterInfo.normalizeWeights' STABLE ascending-weight
    # sort ties on, so it is load-bearing, not presentation. Emitting the groups
    # keeps the act dimension out of every consumer's inner loop; the Act-1
    # groups are the same rows, in the same order, that the pre-S2.01 header's
    # single kEncounters array already carried.
    groups: dict[tuple[int, str], list[dict]] = {}
    for e in encs:
        groups.setdefault((e["act"], e["pool"]), []).append(e)
    pool_order = {name: val for name, val in ENCOUNTER_POOLS.items()}
    group_keys = sorted(groups, key=lambda k: (k[0], pool_order[k[1]]))
    max_members = max((len(v) for v in groups.values()), default=1)
    max_members = max(max_members, 1)
    max_act = max((e["act"] for e in encs), default=1)

    out.append(f"inline constexpr int kMaxEncounterPoolMembers = {max_members};")
    out.append(f"inline constexpr int32_t kEncounterMaxAct = {max_act};\n")
    out.append("// One generateXxx list: the (act, pool) group's members in the "
               "game's ArrayList")
    out.append("// add() order (== registry id order -- the stable sort's tie "
               "order, TRAP 1).")
    out.append("// EVENT groups are lookup-only and never enter a generated "
               "list; they are")
    out.append("// tabulated here too so the act dimension is complete.")
    out.append("struct EncounterPoolTable {")
    out.append("    int32_t act;")
    out.append("    EncounterPool pool;")
    out.append("    uint8_t count;")
    out.append("    std::array<std::string_view, kMaxEncounterPoolMembers> keys;")
    out.append("    std::array<float, kMaxEncounterPoolMembers> weights;")
    out.append("};\n")
    out.append(f"inline constexpr int kEncounterPoolTableCount = {len(group_keys)};")
    out.append("inline constexpr std::array<EncounterPoolTable, "
               "kEncounterPoolTableCount>")
    out.append("    kEncounterPoolTables{{")
    for act, pool in group_keys:
        rows = groups[(act, pool)]
        keys = [sv(r["game_id"]) for r in rows]
        wts = [f"{r['weight']}f" for r in rows]
        while len(keys) < max_members:
            keys.append("std::string_view{}")
            wts.append("0.0f")
        out.append(f"    EncounterPoolTable{{{act}, EncounterPool::{pool}, "
                   f"{len(rows)},")
        out.append(f"        {{{{{', '.join(keys)}}}}},")
        out.append(f"        {{{{{', '.join(wts)}}}}}}},")
    out.append("}};\n")
    out.append("// The generateXxx list for one act, or nullptr when that act "
               "has no such pool.")
    out.append("[[nodiscard]] inline const EncounterPoolTable* "
               "encounter_pool_table(")
    out.append("    int32_t act, EncounterPool pool) noexcept {")
    out.append("    for (const auto& t : kEncounterPoolTables) {")
    out.append("        if (t.act == act && t.pool == pool) { return &t; }")
    out.append("    }")
    out.append("    return nullptr;")
    out.append("}\n")

    out.append("// Lookup an encounter by its game key (Exordium pool / getEncounter "
               "string).")
    out.append("[[nodiscard]] inline const EncounterDef* "
               "encounter_by_game_id(std::string_view key) noexcept {")
    out.append("    for (const auto& e : kEncounters) {")
    out.append("        if (e.game_id == key) { return &e; }")
    out.append("    }")
    out.append("    return nullptr;")
    out.append("}\n")
    out.append("}  // namespace sts::registry")
    return "\n".join(out) + "\n"
