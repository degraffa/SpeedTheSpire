#!/usr/bin/env python3
"""Generate the deterministic Stage-B verification dashboard.

The report aggregates B5.2 campaign reports, de-duplicates exact repeated
seeds, joins literal game_id sightings from the source campaign states to the
G6 tier-2 coverage report, and applies only explicit divergence dispositions.
It never turns a captured action into a replay-clean action and never infers
G7 acceptance from campaign completion.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

REPO = Path(__file__).resolve().parent.parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(REPO / "tools" / "registry_gen"))
from stsgen.loader import load_registry  # noqa: E402
from stsgen.vocab import DOMAINS  # noqa: E402

REPORT_FORMAT = "STS-VERIFICATION-REPORT v1"
DEFAULT_CAMPAIGNS = (
    "b52_accept_locked_20260729_71000_71049",
    "b52_accept_20260729_70000_70049",
    "b53_full_act1_20260729",
)


class ReportError(RuntimeError):
    pass


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReportError(f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ReportError(f"{path}: top level must be an object")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_text_lf(path: Path, value: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(value)


def walk_strings(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from walk_strings(item)
    elif isinstance(value, dict):
        for item in value.values():
            yield from walk_strings(item)


def artifact_sightings(path: Path, game_ids: set[str]) -> Counter[str]:
    """Count exact game_id string occurrences in action state_json objects."""
    counts: Counter[str] = Counter()
    try:
        with path.open("r", encoding="utf-8") as stream:
            for number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ReportError(f"{path}:{number}: invalid JSON: {exc}") from exc
                if record.get("record_kind") != "action":
                    continue
                state = record.get("state_json")
                if not isinstance(state, dict):
                    raise ReportError(f"{path}:{number}: action has no state_json object")
                for value in walk_strings(state):
                    if value in game_ids:
                        counts[value] += 1
    except OSError as exc:
        raise ReportError(f"cannot scan {path}: {exc}") from exc
    return counts


def row_label(domain: str, row: dict[str, Any]) -> str:
    if domain == "a20":
        return f"A{row['level']}"
    return str(row.get("name") or row.get("game_id") or f"id{row['id']}")


def validate_coverage(coverage: dict[str, Any],
                      domains: dict[str, list[dict[str, Any]]]) -> None:
    actual = coverage.get("coverage")
    if not isinstance(actual, dict):
        raise ReportError("tier-2 coverage JSON has no coverage object")
    for domain, rows in domains.items():
        entries = actual.get(domain)
        if not isinstance(entries, dict):
            raise ReportError(f"tier-2 coverage JSON has no {domain} object")
        expected = {row_label(domain, row) for row in rows}
        if set(entries) != expected:
            missing = sorted(expected - set(entries))
            extra = sorted(set(entries) - expected)
            raise ReportError(
                f"tier-2 coverage registry drift for {domain}: "
                f"missing={missing}, extra={extra}")


def load_dispositions(path: Path) -> dict[tuple[str, str, str], dict[str, Any]]:
    value = read_json(path)
    if value.get("format") != "STS-DIVERGENCE-DISPOSITIONS v1":
        raise ReportError(f"{path}: unsupported dispositions format")
    items = value.get("items")
    if not isinstance(items, list):
        raise ReportError(f"{path}: items must be a list")
    result: dict[tuple[str, str, str], dict[str, Any]] = {}
    allowed = {
        "open-product-divergence", "open-harness-gap",
        "standing-deviation", "resolved",
    }
    for item in items:
        if not isinstance(item, dict):
            raise ReportError(f"{path}: disposition item must be an object")
        key = tuple(str(item.get(k, "")) for k in
                    ("campaign_id", "seed", "classification"))
        if not all(key):
            raise ReportError(f"{path}: disposition identity is incomplete: {item}")
        if key in result:
            raise ReportError(f"{path}: duplicate disposition {key}")
        if item.get("status") not in allowed:
            raise ReportError(f"{path}: invalid status for {key}: {item.get('status')}")
        if not str(item.get("reference", "")).strip() or \
                not str(item.get("note", "")).strip():
            raise ReportError(f"{path}: {key} needs a reference and note")
        result[key] = item
    return result


def aggregate(campaign_root: Path, campaign_ids: list[str],
              coverage: dict[str, Any], dispositions_path: Path,
              registry_dir: Path) -> dict[str, Any]:
    domains = load_registry(registry_dir)
    validate_coverage(coverage, domains)
    game_ids = {
        str(row["game_id"])
        for domain, rows in domains.items() if domain != "a20"
        for row in rows
    }
    dispositions = load_dispositions(dispositions_path)
    seen: dict[str, dict[str, Any]] = {}
    campaigns: list[dict[str, Any]] = []
    findings: list[dict[str, Any]] = []
    sightings: Counter[str] = Counter()
    totals = Counter()
    provenance: tuple[Any, ...] | None = None

    for campaign_id in campaign_ids:
        campaign_dir = campaign_root / campaign_id
        report = read_json(campaign_dir / "report.json")
        if report.get("report_format") != "STS-ORACLE-CAMPAIGN-REPORT v1":
            raise ReportError(f"{campaign_id}: unsupported campaign report format")
        if report.get("campaign_id") != campaign_id:
            raise ReportError(f"{campaign_id}: report campaign_id mismatch")
        if report.get("campaign_status") != "complete":
            raise ReportError(f"{campaign_id}: campaign is not complete")
        current_provenance = (
            report.get("schema_version"), report.get("driver_version"),
            report.get("pipeline_version"), report.get("fork_jar_sha256"),
        )
        if provenance is None:
            provenance = current_provenance
        elif current_provenance != provenance:
            raise ReportError(
                f"{campaign_id}: campaign provenance differs from the aggregate")

        accepted = 0
        deduplicated = 0
        for run in report.get("runs", []):
            seed = str(run.get("seed", ""))
            identity = {
                "source_artifact_sha256": run.get("source_artifact_sha256"),
                "actions": run.get("actions"),
                "classification": run.get("classification"),
                "outcome": run.get("outcome"),
            }
            if seed in seen:
                if seen[seed] != identity:
                    raise ReportError(
                        f"conflicting duplicate seed {seed} in {campaign_id}")
                deduplicated += 1
                continue
            seen[seed] = identity
            accepted += 1
            actions = int(run.get("actions", 0))
            totals["captured_actions"] += actions
            if run.get("classification") == "clean":
                totals["replay_clean_actions"] += actions
                if int(run.get("known_obtain_race_records", 0)) == 0:
                    totals["strict_zero_diff_actions"] += actions
            artifact = campaign_dir / str(run.get("source_artifact", ""))
            expected_hash = str(run.get("source_artifact_sha256", ""))
            if sha256_file(artifact) != expected_hash:
                raise ReportError(f"{campaign_id}/{seed}: source artifact hash drift")
            sightings.update(artifact_sightings(artifact, game_ids))

            if run.get("classification") != "clean":
                key = (campaign_id, seed, str(run.get("classification")))
                disposition = dispositions.get(key)
                finding = {
                    "campaign_id": campaign_id,
                    "seed": seed,
                    "classification": run.get("classification"),
                    "actions": actions,
                    "first_divergence": run.get("first_divergence"),
                    "disposition": disposition,
                }
                findings.append(finding)

        campaigns.append({
            "campaign_id": campaign_id,
            "policy": report.get("policy"),
            "requested_runs": len(report.get("runs", [])),
            "included_distinct_runs": accepted,
            "deduplicated_runs": deduplicated,
            "report_sha256": sha256_file(campaign_dir / "report.json"),
        })

    finding_keys = {
        (f["campaign_id"], f["seed"], f["classification"]) for f in findings
    }
    stale_dispositions = sorted(set(dispositions) - finding_keys)
    if stale_dispositions:
        raise ReportError(f"dispositions do not match an included finding: "
                          f"{stale_dispositions}")
    untriaged = [f for f in findings if f["disposition"] is None]

    rows_out: list[dict[str, Any]] = []
    tier2_total = tier2_covered = 0
    for domain, _filename, _enum, _underlying in DOMAINS:
        for row in domains[domain]:
            label = row_label(domain, row)
            tier = coverage["coverage"][domain][label]
            tier2_total += 1
            if tier.get("tier"):
                tier2_covered += 1
            game_id = row.get("game_id")
            rows_out.append({
                "domain": domain,
                "id": row["id"],
                "label": label,
                "game_id": game_id,
                "oracle_sightings": (
                    sightings[str(game_id)] if game_id is not None else None),
                "tier2": {
                    "tier": tier.get("tier"),
                    "tests": tier.get("tests", []),
                },
            })

    captured = totals["captured_actions"]
    state_diff_count = sum(
        f["classification"] == "state_divergence" for f in findings)
    all_diff_count = len(findings)
    per_million = lambda n: (n * 1_000_000.0 / captured) if captured else None
    strict = totals["strict_zero_diff_actions"]
    distinct = len(seen)
    return {
        "format": REPORT_FORMAT,
        "inputs": {
            "campaigns": campaigns,
            "dispositions_sha256": sha256_file(dispositions_path),
        },
        "provenance": {
            "schema_version": provenance[0] if provenance else None,
            "driver_version": provenance[1] if provenance else None,
            "pipeline_version": provenance[2] if provenance else None,
            "fork_jar_sha256": provenance[3] if provenance else None,
        },
        "g7_evidence": {
            "distinct_seeds": distinct,
            "captured_actions": totals["captured_actions"],
            "replay_clean_actions": totals["replay_clean_actions"],
            "strict_zero_diff_actions": strict,
            "untriaged_findings": len(untriaged),
            "zero_untriaged": not untriaged,
            "seed_shortfall_to_2000": max(0, 2000 - distinct),
            "strict_zero_diff_action_shortfall_to_1000000":
                max(0, 1_000_000 - strict),
            "g7_oracle_volume_met": distinct >= 2000 and strict >= 1_000_000
                                     and not untriaged,
        },
        "diffs_per_million_captured_actions": {
            "state_divergence_runs": per_million(state_diff_count),
            "all_nonclean_runs": per_million(all_diff_count),
            "state_divergence_run_count": state_diff_count,
            "all_nonclean_run_count": all_diff_count,
            "denominator_captured_actions": captured,
        },
        "divergence_inventory": findings,
        "registry_coverage": {
            "tier2_rows": tier2_total,
            "tier2_covered_rows": tier2_covered,
            "tier2_complete": tier2_total == tier2_covered,
            "rows": rows_out,
            "zero_oracle_sighting_rows": sum(
                row["oracle_sightings"] == 0 for row in rows_out),
        },
    }


def markdown(report: dict[str, Any]) -> str:
    ev = report["g7_evidence"]
    rates = report["diffs_per_million_captured_actions"]
    reg = report["registry_coverage"]
    lines = [
        "# Stage B verification report",
        "",
        "Generated deterministically by `tools/verify_report/generate_report.py`.",
        "This is evidence accounting, not a G7 acceptance inference.",
        "",
        "## G7 oracle evidence (literal)",
        "",
        f"- Distinct seeds: **{ev['distinct_seeds']}**; shortfall to 2,000: "
        f"**{ev['seed_shortfall_to_2000']}**.",
        f"- Captured actions: **{ev['captured_actions']}**.",
        f"- Replay-clean actions: **{ev['replay_clean_actions']}**.",
        f"- Strict zero-diff actions: **{ev['strict_zero_diff_actions']}**; "
        f"shortfall to 1,000,000: "
        f"**{ev['strict_zero_diff_action_shortfall_to_1000000']}**.",
        f"- Untriaged findings: **{ev['untriaged_findings']}**. "
        "A finding counts as triaged only when an exact campaign/seed/"
        "classification disposition exists.",
        f"- Oracle volume criterion met: **"
        f"{'YES' if ev['g7_oracle_volume_met'] else 'NO'}**.",
        "",
        "## Diff rates",
        "",
        f"- State-divergence runs per million captured actions: "
        f"**{rates['state_divergence_runs']:.3f}** "
        f"({rates['state_divergence_run_count']} findings).",
        f"- All non-clean runs per million captured actions: "
        f"**{rates['all_nonclean_runs']:.3f}** "
        f"({rates['all_nonclean_run_count']} findings).",
        "",
        "## Divergence inventory",
        "",
        "| Campaign | Seed | Classification | Disposition | Reference |",
        "|---|---|---|---|---|",
    ]
    for finding in report["divergence_inventory"]:
        disp = finding["disposition"]
        status = disp["status"] if disp else "**UNTRIAGED**"
        ref = disp["reference"] if disp else ""
        lines.append(
            f"| {finding['campaign_id']} | {finding['seed']} | "
            f"{finding['classification']} | {status} | {ref} |")
    lines += [
        "",
        "Open dispositions remain open defects or harness gaps; disposition is "
        "not acceptance.",
        "",
        "## Registry coverage and oracle sightings",
        "",
        f"- Tier-2 rows covered: **{reg['tier2_covered_rows']} / "
        f"{reg['tier2_rows']}**.",
        f"- Rows with a `game_id` and zero oracle sightings: "
        f"**{reg['zero_oracle_sighting_rows']}**.",
        "",
        "| Domain | ID | Row | game_id | Oracle sightings | Tier-2 |",
        "|---|---:|---|---|---:|---|",
    ]
    for row in reg["rows"]:
        sightings = "n/a" if row["oracle_sightings"] is None \
            else str(row["oracle_sightings"])
        tier = row["tier2"]["tier"] or "UNCOVERED"
        lines.append(
            f"| {row['domain']} | {row['id']} | {row['label']} | "
            f"{row['game_id'] or 'n/a'} | {sightings} | {tier} |")
    return "\n".join(lines) + "\n"


def csv_text(report: dict[str, Any]) -> str:
    output = io.StringIO(newline="")
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow([
        "domain", "id", "label", "game_id", "oracle_sightings",
        "tier2_tier", "tier2_tests",
    ])
    for row in report["registry_coverage"]["rows"]:
        writer.writerow([
            row["domain"], row["id"], row["label"], row["game_id"] or "",
            "" if row["oracle_sightings"] is None else row["oracle_sightings"],
            row["tier2"]["tier"] or "",
            "; ".join(row["tier2"]["tests"]),
        ])
    return output.getvalue()


def write_report(report: dict[str, Any], out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    write_text_lf(
        out_dir / "stage-b-verification.json",
        json.dumps(report, indent=2, sort_keys=True) + "\n")
    write_text_lf(
        out_dir / "stage-b-verification.md", markdown(report))
    write_text_lf(
        out_dir / "stage-b-registry-sightings.csv", csv_text(report))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--artifact-root", type=Path,
                        default=Path(r"D:\STS_BG_Mod\_oracle_data\campaigns"))
    parser.add_argument("--campaign", action="append", dest="campaigns")
    parser.add_argument("--coverage", type=Path,
                        default=REPO / "build" / "debug" / "verify_report" /
                                "tier2_coverage.json")
    parser.add_argument("--dispositions", type=Path,
                        default=Path(__file__).with_name(
                            "divergence_dispositions.json"))
    parser.add_argument("--registry", type=Path, default=REPO / "registry")
    parser.add_argument("--out-dir", type=Path,
                        default=REPO / "docs" / "verification")
    args = parser.parse_args(argv)
    try:
        report = aggregate(
            args.artifact_root,
            args.campaigns or list(DEFAULT_CAMPAIGNS),
            read_json(args.coverage),
            args.dispositions,
            args.registry,
        )
        write_report(report, args.out_dir)
    except ReportError as exc:
        print(f"verification report error: {exc}", file=sys.stderr)
        return 2
    print(args.out_dir / "stage-b-verification.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
