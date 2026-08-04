#!/usr/bin/env python3
"""Verify that every historical G7 defect family owns passing regressions."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parent.parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_tier2_coverage import (  # noqa: E402
    ctest_registered,
    parse_lasttest,
    run_ctest,
)

FORMAT = "STS-G7-PROACTIVE-MANIFEST v1"


class AuditError(RuntimeError):
    pass


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AuditError(f"cannot read manifest {path}: {exc}") from exc
    if not isinstance(value, dict) or value.get("format") != FORMAT:
        raise AuditError(f"{path}: unsupported manifest format")
    families = value.get("families")
    if not isinstance(families, list) or not families:
        raise AuditError(f"{path}: families must be a non-empty list")
    sources = value.get("sources")
    if not isinstance(sources, list) or not sources:
        raise AuditError(f"{path}: sources must be a non-empty list")
    if len(sources) != len(set(sources)):
        raise AuditError(f"{path}: sources contain a duplicate")
    for source in sources:
        if not isinstance(source, str) or not source.strip():
            raise AuditError(f"{path}: source paths must be non-empty strings")
        if not (REPO / source).is_file():
            raise AuditError(f"{path}: historical source does not exist: {source}")
    ids: set[str] = set()
    tests: set[str] = set()
    for family in families:
        if not isinstance(family, dict):
            raise AuditError(f"{path}: family must be an object")
        family_id = str(family.get("id", ""))
        if not family_id or family_id in ids:
            raise AuditError(f"{path}: missing or duplicate family id {family_id!r}")
        ids.add(family_id)
        if not str(family.get("risk", "")).strip():
            raise AuditError(f"{path}: family {family_id} has no risk statement")
        regressions = family.get("regressions")
        if not isinstance(regressions, list) or len(regressions) < 2:
            raise AuditError(
                f"{path}: family {family_id} needs at least two regressions")
        for regression in regressions:
            if not isinstance(regression, dict):
                raise AuditError(f"{path}: malformed regression in {family_id}")
            name = str(regression.get("test", ""))
            if not name or name in tests:
                raise AuditError(f"{path}: missing or duplicate test {name!r}")
            tests.add(name)
            if not str(regression.get("witness", "")).strip():
                raise AuditError(f"{path}: {name} has no witness statement")
    return value


def evaluate(manifest: dict[str, Any], registered: set[str],
             results: dict[str, bool]) -> dict[str, Any]:
    families_out = []
    all_pass = True
    for family in manifest["families"]:
        regressions_out = []
        family_pass = True
        for regression in family["regressions"]:
            name = regression["test"]
            is_registered = name in registered
            passed = results.get(name) is True
            ok = is_registered and passed
            family_pass = family_pass and ok
            regressions_out.append({
                **regression,
                "registered": is_registered,
                "passed": passed,
            })
        all_pass = all_pass and family_pass
        families_out.append({
            "id": family["id"],
            "risk": family["risk"],
            "passed": family_pass,
            "regressions": regressions_out,
        })
    return {
        "format": "STS-G7-PROACTIVE-AUDIT v1",
        "passed": all_pass,
        "sources": manifest.get("sources", []),
        "families": families_out,
    }


def markdown(report: dict[str, Any]) -> str:
    lines = [
        "# G7 proactive regression audit",
        "",
        "Generated deterministically by "
        "`tools/verify_report/check_g7_proactive_coverage.py`.",
        "",
        f"Verdict: **{'PASS' if report['passed'] else 'FAIL'}**.",
        "",
    ]
    for family in report["families"]:
        lines.extend([
            f"## {family['id']}",
            "",
            family["risk"],
            "",
            "| Required regression | Historical witness | Registered | Passing |",
            "|---|---|---:|---:|",
        ])
        for regression in family["regressions"]:
            lines.append(
                f"| `{regression['test']}` | {regression['witness']} | "
                f"{'yes' if regression['registered'] else 'NO'} | "
                f"{'yes' if regression['passed'] else 'NO'} |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path,
                        default=REPO / "build" / "debug")
    parser.add_argument("--manifest", type=Path,
                        default=Path(__file__).with_name(
                            "g7_proactive_manifest.json"))
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--use-last-log", action="store_true")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args(argv)
    try:
        manifest = load_manifest(args.manifest)
        registered = set(ctest_registered(args.build_dir))
        results = (parse_lasttest(args.build_dir) if args.use_last_log
                   else run_ctest(args.build_dir, args.jobs))
        report = evaluate(manifest, registered, results)
    except (AuditError, SystemExit) as exc:
        if isinstance(exc, AuditError):
            print(f"G7 proactive audit error: {exc}", file=sys.stderr)
            return 2
        raise

    out_dir = args.out_dir or args.build_dir / "verify_report"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "g7_proactive_audit.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (out_dir / "g7_proactive_audit.md").write_text(
        markdown(report), encoding="utf-8")
    print(f"G7 proactive regression families: "
          f"{sum(f['passed'] for f in report['families'])}/"
          f"{len(report['families'])}")
    print(f"VERDICT: {'PASS' if report['passed'] else 'FAIL'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
