#!/usr/bin/env python3
"""The TE.1 survival-biased campaign policy, as an EXTERNAL action source.

This is the reference binary for the campaign harness's `--policy external`
hook (campaign_driver.ExternalPolicy, protocol STS-POLICY-IO v1): the promoted
B5.1-family survival heuristic -- block-aware under attack, potion-using in
boss/elite/low-HP fights, elite-avoiding on the map -- packaged as a separate
process so the SAME seam that carries it today can carry a trained agent
checkpoint later (training-tasks.md GT2 routes checkpoints through exactly
this hook).

WHY IT DELIBERATELY REUSES greedy_policy. The scoring rules ARE the survival
policy: greedy_policy.py's module header documents the block-under-attack
weights, the R1/R2/R3 boss rules, and the inverted (survival-first) map
scoring, all of which were derived from captured-campaign evidence. A second
copy of those rules would drift; this binary imports the one implementation
and adds only (a) the process boundary, and (b) a CONFIG surface for tuning
the scoring constants per cohort without editing code.

DETERMINISM (design 7.5). The driver stamps every request with
(policy_seed, seed). The tie-break RNG is reseeded
`Random(f"{policy_seed}:{seed}")` whenever the seed changes, which is exactly
how the in-driver greedy policy seeds its own RNG -- so a cohort driven
through this binary with an empty config makes byte-identical decisions to
`--policy greedy`, and any run is reproducible from (policy_seed, seed)
alone.

CONFIG (`--config <json>`), all keys optional, unknown keys FAIL LOUD:

    {
      "constants": {"MAP_ELITE": 150, ...},   # overrides for greedy_policy's
                                              # ALL-CAPS numeric constants
      "card_table": "path/to/sidetable.json"  # alternate card side table
    }

A config typo that silently fell back to defaults would produce a cohort
labeled with a policy it did not run, so validation is strict: every
constants key must name an existing int/float module constant.

Protocol errors are fatal by design: exit non-zero and let the driver's
FatalEnvironmentDrift path stop the campaign durably.
"""

from __future__ import annotations

import argparse
import json
import random
import sys

import greedy_policy

PROTOCOL = "STS-POLICY-IO v1"


class ConfigError(ValueError):
    """The policy config asked for something this policy does not have."""


def apply_constants(module, overrides: dict) -> dict:
    """setattr each override onto `module`, strictly validated.

    Returns {name: (old, new)} for logging. Only existing ALL-CAPS int/float
    constants are overridable; bool is excluded (it is an int subclass but no
    scoring constant is a flag).
    """
    if not isinstance(overrides, dict):
        raise ConfigError("constants must be an object of {NAME: number}")
    applied = {}
    for name, value in overrides.items():
        if not isinstance(name, str) or not name.isupper():
            raise ConfigError(f"constants key {name!r} is not a policy "
                              "constant name")
        current = getattr(module, name, None)
        if not isinstance(current, (int, float)) or \
                isinstance(current, bool):
            raise ConfigError(
                f"constants key {name!r} does not name a numeric "
                "greedy_policy constant")
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            raise ConfigError(f"constants[{name!r}] must be a number, got "
                              f"{value!r}")
        applied[name] = (current, value)
        setattr(module, name, value)
    return applied


def load_config(path: str | None) -> dict:
    if not path:
        return {}
    with open(path, "r", encoding="utf-8") as fh:
        config = json.load(fh)
    if not isinstance(config, dict):
        raise ConfigError("policy config must be a JSON object")
    unknown = set(config) - {"constants", "card_table"}
    if unknown:
        raise ConfigError(f"unknown policy config keys: {sorted(unknown)}")
    return config


class SurvivalPolicy:
    """Stateful wrapper: per-seed RNG + the (possibly overridden) scorer."""

    def __init__(self, config: dict) -> None:
        self.applied = apply_constants(
            greedy_policy, config.get("constants") or {})
        self.table = greedy_policy.load_side_table(config.get("card_table"))
        self._seed = None
        self._rng = None

    def decide(self, request: dict) -> str:
        seed = request.get("seed")
        candidates = request.get("candidates")
        if not isinstance(candidates, list) or not candidates:
            raise ConfigError(f"decide request carries no candidates: "
                              f"{candidates!r}")
        if seed != self._seed:
            self._seed = seed
            self._rng = random.Random(
                f"{request.get('policy_seed')}:{seed}")
        return greedy_policy.pick(
            candidates, request.get("state"), self.table, self._rng)


def serve(stdin, stdout, config: dict) -> int:
    policy = SurvivalPolicy(config)
    print(f"[survival_policy] side table entries={len(policy.table)} "
          f"constant overrides={sorted(policy.applied)}", file=sys.stderr,
          flush=True)
    for line in stdin:
        if not line.strip():
            continue
        request = json.loads(line)
        if request.get("format") != PROTOCOL or \
                request.get("kind") != "decide":
            print(f"[survival_policy] out-of-contract request: "
                  f"{ {k: request.get(k) for k in ('format', 'kind')} }",
                  file=sys.stderr, flush=True)
            return 2
        response = {
            "format": PROTOCOL,
            "kind": "decision",
            "command": policy.decide(request),
        }
        stdout.write(json.dumps(response, ensure_ascii=False) + "\n")
        stdout.flush()
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="TE.1 survival-biased external campaign policy")
    parser.add_argument("--config", default=None,
                        help="optional JSON config (constants overrides, "
                             "card table path)")
    args = parser.parse_args(argv)
    try:
        config = load_config(args.config)
    except (OSError, ValueError) as exc:
        print(f"[survival_policy] bad config: {exc}", file=sys.stderr,
              flush=True)
        return 2
    return serve(sys.stdin, sys.stdout, config)


if __name__ == "__main__":
    sys.exit(main())
