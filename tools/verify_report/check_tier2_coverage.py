#!/usr/bin/env python3
"""Tier-2 registry coverage check (G6 checklist leg 1; design doc B §7.4, §8(2)).

Answers one question, mechanically: does EVERY registry manifest row have at
least one NAMED, REGISTERED, PASSING tier-2 test exercising it?

Rows come from the same machinery the build uses -- ``tools/registry_gen``'s
loader (never an ad-hoc YAML parse), and the per-domain counts are re-derived by
actually running ``gen.py`` and reading the freshly generated ``manifest.hpp``.
Nothing here quotes a count from a document.

THE LINKING CONVENTION (discovered from the existing corpus, not imposed on it;
see README.md in this directory for the survey):

  * Enum domains (cards, powers, monsters, relics, potions, events): a test
    covers a row when its body references the row's generated enumerator --
    ``CardId::STRIKE``, ``RelicId::ANCHOR``, ... .  This is how the corpus
    already links tests to rows (roster tests and behavior tests alike).
  * encounters (no enum): a test covers a row when its body contains the row's
    ``game_id`` as an exact quoted string literal ("Gremlin Gang", ...), which
    is how the corpus drives ``resolve_encounter``.
  * a20 (no enum, no game_id): covered by the registry-sweep tests the ledger
    itself names (B4.15: "machine-checked by A20Manifest.*"), declared in the
    explicit SWEEPS allowlist below.  A sweep entry is data, not inference --
    extending it is a reviewed edit, so coverage cannot silently go vacuous.

  Attribution tiers, strongest first (a row is covered by the strongest tier
  that has at least one registered passing test):
    direct      -- the reference appears inside the TEST body itself.
    file-scope  -- the reference appears in a test file's shared helpers /
                   fixtures outside any TEST body; every registered passing
                   test in that file is credited, because those helpers only
                   execute through the file's tests.
    sweep       -- an allowlisted whole-domain sweep test (SWEEPS below).

  "Registered" means the name appears in ``ctest -N`` output for the build
  tree; "passing" comes from a fresh full ``ctest`` run this script performs
  (default -- the suite is ~20s parallel), or from the existing LastTest.log
  with ``--use-last-log``.  A source reference in a test that ctest never
  runs, or that fails, covers nothing.

  TRAP (hit while building this): ``ctest -N`` OVERWRITES
  Testing/Temporary/LastTest.log with an empty start/end stanza, destroying
  the previous run's recorded results.  This script snapshots and restores the
  log around its own ``-N`` call; a bare ``ctest -N`` by hand still clobbers
  it, which is why running the suite fresh is the default mode.

Exit codes: 0 = every row covered (the G6 bar), 1 = uncovered rows or failing
attributed tests, 2 = usage/environment error.

Outputs (deterministic: sorted, no timestamps):
  <out-dir>/tier2_coverage.md    -- committable report (B5.4 will move/extend
                                    it under docs/verification/).
  <out-dir>/tier2_coverage.json  -- full machine-readable attribution.

Deliberately NOT in this script: oracle-campaign sighting joins (B5.4 extends
this same row enumeration with the campaign-log join per design §7.4).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent


def die(msg: str) -> "NoReturn":  # noqa: F821 -- doc contract: env errors exit 2
    print(msg, file=sys.stderr)
    sys.exit(2)

# Reuse the generator's own loader/vocab -- the sanctioned row enumeration
# (design §4.3: the manifest is what the tier-2 coverage check consumes).
sys.dont_write_bytecode = True
sys.path.insert(0, str(REPO / "tools" / "registry_gen"))
from stsgen.loader import load_registry  # noqa: E402
from stsgen.vocab import DOMAINS, pascal  # noqa: E402

# --------------------------------------------------------------------------
# Whole-domain sweep allowlist.  Each entry: (full-test-name regex, domain key,
# justification).  Keep this SHORT and justified -- a sweep credits every row
# of its domain, so an unjustified entry makes the whole check vacuous.  The
# generator's own tests (RegistryGen*) are deliberately NOT here: they verify
# the codegen machinery, not the per-row rules content.
SWEEPS = [
    (r"^A20Manifest\.", "a20",
     "ledger B4.15: every a20.yaml row machine-checked by "
     "A20Manifest.EveryRowCarriesScopeProvenanceAndAnS1Status "
     "(one row per level, id==level, scope/provenance/S1-status per row)"),
]

ENUM_TO_DOMAIN = {enum: key for key, _f, enum, _u in DOMAINS if enum is not None}
ENUM_REF_RE = re.compile(
    r"\b(" + "|".join(ENUM_TO_DOMAIN) + r")::([A-Z][A-Z0-9_]*)\b")
TEST_MACRO_RE = re.compile(
    r"\b(TEST|TEST_F|TEST_P|TYPED_TEST)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")


# --------------------------------------------------------------------------
# A small C++ lexer: comments removed everywhere; a parallel copy with string
# literal CONTENTS blanked drives macro finding + brace matching, so a brace in
# a string cannot desync the parser and a symbol in a comment cannot cover a
# row.  Handles //, /* */, "..." with escapes, '...', and R"delim(...)delim".
def lex(src: str) -> tuple[str, str]:
    """Return (code, codeblank): same length as src; comments -> spaces in
    both; string/char contents preserved in `code`, blanked in `codeblank`."""
    n = len(src)
    code = list(src)
    blank = list(src)
    i = 0
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                code[i] = blank[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            end = src.find("*/", i + 2)
            end = n if end == -1 else end + 2
            while i < end:
                if src[i] != "\n":
                    code[i] = blank[i] = " "
                i += 1
        elif c == '"':
            # Raw string?  Look back over an optional encoding prefix for R.
            j = i - 1
            while j >= 0 and src[j] in "uU8":
                j -= 1
            if j >= 0 and src[j] == "R" and (j == 0 or not (src[j-1].isalnum() or src[j-1] == "_")):
                paren = src.find("(", i + 1)
                delim = src[i + 1:paren] if paren != -1 else ""
                close = src.find(")" + delim + '"', paren + 1) if paren != -1 else -1
                end = n if close == -1 else close + len(delim) + 2
                i += 1
                while i < end - (0 if close == -1 else len(delim) + 1):
                    if src[i] != "\n":
                        blank[i] = " "
                    i += 1
                i = end
            else:
                i += 1
                while i < n and src[i] != '"':
                    if src[i] == "\\":
                        blank[i] = " "
                        i += 1
                        if i < n:
                            blank[i] = " "
                            i += 1
                        continue
                    if src[i] != "\n":
                        blank[i] = " "
                    i += 1
                i += 1  # closing quote
        elif c == "'":
            i += 1
            while i < n and src[i] != "'":
                if src[i] == "\\":
                    i += 2
                    continue
                i += 1
            i += 1
        else:
            i += 1
    return "".join(code), "".join(blank)


def extract_tests(path: Path) -> tuple[list[dict], str]:
    """Parse one test source: list of {suite, name, body} + file-scope text
    (everything outside TEST bodies, comments stripped)."""
    src = path.read_text(encoding="utf-8", errors="replace")
    code, codeblank = lex(src)
    tests = []
    spans = []
    for m in TEST_MACRO_RE.finditer(codeblank):
        brace = codeblank.find("{", m.end())
        if brace == -1:
            continue
        depth = 0
        end = brace
        for k in range(brace, len(codeblank)):
            if codeblank[k] == "{":
                depth += 1
            elif codeblank[k] == "}":
                depth -= 1
                if depth == 0:
                    end = k + 1
                    break
        tests.append({"suite": m.group(2), "name": m.group(3),
                      "body": code[m.start():end]})
        spans.append((m.start(), end))
    outside = []
    prev = 0
    for a, b in spans:
        outside.append(code[prev:a])
        prev = b
    outside.append(code[prev:])
    return tests, "".join(outside)


def find_refs(text: str, encounter_gids: dict[str, str]) -> set[tuple[str, str]]:
    """(domain_key, row_name) references in a text region."""
    refs: set[tuple[str, str]] = set()
    for m in ENUM_REF_RE.finditer(text):
        if m.group(2) != "NONE":
            refs.add((ENUM_TO_DOMAIN[m.group(1)], m.group(2)))
    for gid, row_name in encounter_gids.items():
        if '"' + gid + '"' in text:
            refs.add(("encounters", row_name))
    return refs


# --------------------------------------------------------------------------
def regenerated_manifest_counts(registry_dir: Path) -> dict[str, int]:
    """Run gen.py into a scratch dir; parse the freshly generated manifest.hpp.
    This is the §4.3 're-derive counts from the manifest' step -- it also runs
    the generator's full validation over every row."""
    gen = REPO / "tools" / "registry_gen" / "gen.py"
    with tempfile.TemporaryDirectory() as tmp:
        proc = subprocess.run(
            [sys.executable, str(gen), "--registry", str(registry_dir),
             "--out", tmp],
            capture_output=True, text=True)
        if proc.returncode != 0:
            die(f"error: registry_gen failed:\n{proc.stderr}")
        text = (Path(tmp) / "sts" / "registry" / "manifest.hpp").read_text()
    counts: dict[str, int] = {}
    pascal_to_key = {pascal(key): key for key, _f, _e, _u in DOMAINS}
    for m in re.finditer(r"k(\w+)Count = (\d+);", text):
        if m.group(1) == "Total":
            counts["__total__"] = int(m.group(2))
        else:
            counts[pascal_to_key[m.group(1)]] = int(m.group(2))
    return counts


def ctest_registered(build_dir: Path) -> list[str]:
    # `ctest -N` OVERWRITES Testing/Temporary/LastTest.log with an empty
    # start/end stanza (module-docstring trap).  Snapshot and restore it so
    # this script's own listing never destroys a real run's recorded results.
    log = build_dir / "Testing" / "Temporary" / "LastTest.log"
    saved = log.read_bytes() if log.exists() else None
    proc = subprocess.run(["ctest", "--test-dir", str(build_dir), "-N"],
                          capture_output=True, text=True)
    if saved is not None:
        log.write_bytes(saved)
    if proc.returncode != 0:
        die(f"error: ctest -N failed in {build_dir}:\n{proc.stderr}")
    names = [m.group(1)
             for m in re.finditer(r"Test\s+#\d+:\s+(\S+)", proc.stdout)]
    if not names:
        die(f"error: ctest -N listed no tests in {build_dir} -- "
                 "build first (tools/wsl_run.sh debug)")
    return names


def parse_lasttest(build_dir: Path) -> dict[str, bool]:
    """Results of the LAST recorded ctest run (ctest_registered snapshots and
    restores the log around its own -N call, so ordering no longer matters,
    but a bare `ctest -N` run by hand still empties it)."""
    log = build_dir / "Testing" / "Temporary" / "LastTest.log"
    if not log.exists():
        die(f"error: {log} not found -- run the suite first "
                 "(tools/wsl_run.sh debug) or drop --use-last-log")
    results: dict[str, bool] = {}
    cur = None
    for line in log.read_text(errors="replace").splitlines():
        m = re.match(r"\s*\d+/\d+\s+Test(?:ing)?:\s+(\S+)", line)
        if m:
            cur = m.group(1)
            continue
        if cur and re.match(r"^Test Passed", line):
            results[cur] = True
            cur = None
        elif cur and re.match(r"^Test (Failed|Timeout)|^\*\*\*", line):
            results[cur] = False
            cur = None
    if not results:
        die(f"error: could not parse any results from {log} -- it was "
                 "probably clobbered by a bare `ctest -N` (which empties it); "
                 "re-run the suite or drop --use-last-log")
    return results


def run_ctest(build_dir: Path, jobs: int) -> dict[str, bool]:
    """Run the full suite now; parse pass/fail from the run's own output."""
    proc = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "-j", str(jobs)],
        capture_output=True, text=True)
    results: dict[str, bool] = {}
    for line in proc.stdout.splitlines():
        m = re.match(r"\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s", line)
        if not m:
            continue
        results[m.group(1)] = ("***" not in line) and (" Passed " in line)
    if not results:
        die(f"error: ctest run in {build_dir} produced no parseable "
                 f"results:\n{proc.stdout[-2000:]}\n{proc.stderr[-2000:]}")
    return results


def matches_registered(suite: str, name: str, registered: list[str]) -> list[str]:
    """ctest names for a gtest TEST -- exact for TEST/TEST_F, instantiated
    pattern for TEST_P (Inst/Suite.Name/k)."""
    base = f"{suite}.{name}"
    pat = re.compile(rf"^(.+/)?{re.escape(suite)}\.{re.escape(name)}(/.+)?$")
    return [r for r in registered if r == base or pat.match(r)]


# --------------------------------------------------------------------------
def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", type=Path, default=REPO / "build" / "debug",
                    help="configured+tested build tree (default build/debug)")
    ap.add_argument("--out-dir", type=Path, default=None,
                    help="report output dir (default <build-dir>/verify_report)")
    ap.add_argument("--use-last-log", action="store_true",
                    help="reuse the build tree's existing LastTest.log instead "
                         "of running the suite now (fast path; the log must be "
                         "from a real run, not clobbered by a bare `ctest -N`)")
    ap.add_argument("--jobs", type=int, default=8,
                    help="ctest -j for the fresh run (default 8)")
    ap.add_argument("--tests-dir", type=Path, default=REPO / "tests")
    args = ap.parse_args(argv)

    build_dir = args.build_dir
    out_dir = args.out_dir or (build_dir / "verify_report")

    # 1. Rows, via the generator's loader; counts re-derived from a fresh gen.
    domains = load_registry(REPO / "registry")
    manifest = regenerated_manifest_counts(REPO / "registry")
    for key, rows in domains.items():
        if manifest.get(key) != len(rows):
            die(f"error: manifest disagreement for '{key}': loader says "
                     f"{len(rows)}, regenerated manifest.hpp says "
                     f"{manifest.get(key)}")

    def row_label(key: str, row: dict) -> str:
        if key == "a20":
            return f"A{row['level']}"
        return row.get("name") or row.get("game_id") or f"id{row['id']}"

    encounter_gids = {r["game_id"]: row_label("encounters", r)
                      for r in domains["encounters"]}

    # 2. Parse the test corpus.
    all_tests: list[dict] = []          # {suite,name,file,refs,file_refs}
    for path in sorted(args.tests_dir.glob("*.cpp")):
        tests, outside = extract_tests(path)
        file_refs = find_refs(outside, encounter_gids)
        for t in tests:
            all_tests.append({
                "suite": t["suite"], "name": t["name"], "file": path.name,
                "direct": find_refs(t["body"], encounter_gids),
                "filescope": file_refs,
            })

    # 3. Registration + pass/fail.
    if args.use_last_log:
        results = parse_lasttest(build_dir)
        registered = ctest_registered(build_dir)
    else:
        registered = ctest_registered(build_dir)
        print(f"running full ctest suite in {build_dir} (-j {args.jobs}) ...",
              flush=True)
        results = run_ctest(build_dir, args.jobs)

    live: dict[tuple[str, str], list[str]] = {}   # (suite,name) -> passing ctest names
    dead: dict[tuple[str, str], str] = {}         # -> reason
    for t in all_tests:
        key = (t["suite"], t["name"])
        names = matches_registered(t["suite"], t["name"], registered)
        if not names:
            dead[key] = "not registered in ctest -N"
            continue
        passing = [nm for nm in names if results.get(nm) is True]
        failing = [nm for nm in names if results.get(nm) is False]
        if failing:
            dead[key] = f"FAILING: {', '.join(sorted(failing))}"
        elif not passing:
            dead[key] = "registered but no recorded result in LastTest.log"
        else:
            live[key] = sorted(passing)

    # 4. Attribute rows.
    sweep_res = [(re.compile(rx), dom, why) for rx, dom, why in SWEEPS]
    coverage: dict[str, dict[str, dict]] = {}
    for key, rows in domains.items():
        coverage[key] = {}
        for row in rows:
            label = row_label(key, row)
            entry = {"id": row["id"], "tier": None, "tests": [], "notes": []}
            direct, filescope = [], []
            for t in all_tests:
                tk = (t["suite"], t["name"])
                if tk not in live:
                    if (key, label) in t["direct"] and tk in dead:
                        entry["notes"].append(
                            f"{t['suite']}.{t['name']} references it but is "
                            f"{dead[tk]}")
                    continue
                if (key, label) in t["direct"]:
                    direct.append(f"{t['suite']}.{t['name']}")
                elif (key, label) in t["filescope"]:
                    filescope.append(f"{t['suite']}.{t['name']} [{t['file']}]")
            sweeps = sorted(
                f"{s}.{n}" for (s, n) in live
                for rx, dom, _why in sweep_res
                if dom == key and rx.match(f"{s}.{n}"))
            if direct:
                entry["tier"], entry["tests"] = "direct", sorted(direct)
            elif filescope:
                entry["tier"], entry["tests"] = "file-scope", sorted(filescope)
            elif sweeps:
                entry["tier"], entry["tests"] = "sweep", sweeps
            coverage[key][label] = entry

    # 5. Report.
    out_dir.mkdir(parents=True, exist_ok=True)
    dom_order = [key for key, _f, _e, _u in DOMAINS]
    lines_md = [
        "# Tier-2 registry coverage",
        "",
        "Generated by `tools/verify_report/check_tier2_coverage.py` "
        "(G6 checklist leg 1; design §7.4, §8(2)).",
        "Deterministic: a function of `registry/*.yaml`, `tests/*.cpp`, and "
        "the build tree's ctest results only.",
        "",
        "| domain | rows | covered | uncovered |",
        "|---|---:|---:|---:|",
    ]
    total_rows = total_cov = 0
    uncovered_all: dict[str, list[str]] = {}
    for key in dom_order:
        rows = coverage[key]
        cov = [l for l, e in rows.items() if e["tier"]]
        unc = sorted((l for l, e in rows.items() if not e["tier"]),
                     key=lambda l: rows[l]["id"])
        total_rows += len(rows)
        total_cov += len(cov)
        if unc:
            uncovered_all[key] = unc
        lines_md.append(f"| {key} | {len(rows)} | {len(cov)} | {len(unc)} |")
    lines_md.append(f"| **total** | {total_rows} | {total_cov} | "
                    f"{total_rows - total_cov} |")
    lines_md.append("")
    if uncovered_all:
        lines_md.append("## Uncovered rows (by name)")
        lines_md.append("")
        for key in dom_order:
            for label in uncovered_all.get(key, []):
                e = coverage[key][label]
                note = ("; ".join(sorted(set(e["notes"])))) or \
                    "no test references this row under the linking convention"
                lines_md.append(f"- **{key} / {label}** (id {e['id']}): {note}")
        lines_md.append("")
    lines_md.append("## Attribution (strongest tier per row)")
    lines_md.append("")
    for key in dom_order:
        lines_md.append(f"### {key}")
        lines_md.append("")
        for label, e in sorted(coverage[key].items(),
                               key=lambda kv: kv[1]["id"]):
            if e["tier"]:
                shown = e["tests"][:4]
                more = f" (+{len(e['tests']) - 4} more)" if len(e["tests"]) > 4 else ""
                lines_md.append(f"- {label} (id {e['id']}) -- {e['tier']}: "
                                f"{', '.join(shown)}{more}")
            else:
                lines_md.append(f"- {label} (id {e['id']}) -- **UNCOVERED**")
        lines_md.append("")
    (out_dir / "tier2_coverage.md").write_text(
        "\n".join(lines_md) + "\n", encoding="utf-8", newline="\n")

    payload = {
        "manifest_counts": {k: manifest[k] for k in dom_order},
        "convention": {
            "enum_domains": "enumerator reference (e.g. CardId::STRIKE) in a "
                            "TEST body (direct) or test-file helpers (file-scope)",
            "encounters": "exact quoted game_id string literal",
            "a20": "allowlisted sweep tests (see sweeps)",
            "sweeps": [{"pattern": rx, "domain": dom, "why": why}
                       for rx, dom, why in SWEEPS],
        },
        "coverage": coverage,
    }
    (out_dir / "tier2_coverage.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8", newline="\n")

    # 6. Console summary + verdict.
    print(f"registry rows (regenerated manifest): {total_rows}")
    for key in dom_order:
        rows = coverage[key]
        n_cov = sum(1 for e in rows.values() if e["tier"])
        mark = "OK " if n_cov == len(rows) else "GAP"
        print(f"  {mark} {key:<12} rows={len(rows):<4} covered={n_cov:<4} "
              f"uncovered={len(rows) - n_cov}")
    for key in dom_order:
        for label in uncovered_all.get(key, []):
            print(f"  UNCOVERED {key}/{label}")
    print(f"reports: {out_dir / 'tier2_coverage.md'}")
    if total_cov == total_rows:
        print("VERDICT: PASS -- every manifest row has a named passing test")
        return 0
    print(f"VERDICT: FAIL -- {total_rows - total_cov} uncovered row(s)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
