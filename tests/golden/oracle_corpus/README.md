# Oracle corpus

This directory contains the small committed oracle artifacts allowed by
`docs/conventions.md`.

- `skeleton_sample.jsonl` is the B1.5 translator/adapter fixture.
- `act1_a20_50.tar.gz` and `act1_a20_50.manifest.json` are the B5.4 curated
  full-run CI corpus (`STS-ORACLE-CI-CORPUS v1`).
- `three_act_a20_5.tar.gz` and `three_act_a20_5.manifest.json` are the S2.46
  curated Acts 1–3 corpus (`STS-ORACLE-CI-CORPUS v2`).

The B5.4 corpus contains exactly 50 distinct zero-diff A20 Act-1 seed
captures plus their translated combat traces. The manifest records the
selection policy, campaign provenance, source and trace hashes, and translated
trace headers. Rebuild it only from the fixed external campaign root:

```bat
C:\Python39\python.exe tools\verify_report\build_ci_corpus.py ^
  --archive tests\golden\oracle_corpus\act1_a20_50.tar.gz ^
  --manifest tests\golden\oracle_corpus\act1_a20_50.manifest.json
```

The S2.46 corpus contains **five whole-run Acts 1–3 A20 captures**, named one by
one with their reasons in `build_ci_corpus.py`: two completed A20 double-boss
victories over different first bosses, an Act-3 boss kill that then loses to the
second boss, the Mind Bloom Act-1-boss re-fight, and the boss-relic **skip**
policy axis. It is the only committed evidence that replays a boss chest, an
act-2→3 transition, an Act-3 kill and the double-boss `COMPLETE` handoff out of
a real capture. Its builder re-replays every pick rather than trusting a stored
classification, so it needs a built binary and runs from inside WSL:

```bash
tools/wsl_run.sh release            # builds replay_run_diff
python3 tools/verify_report/build_ci_corpus.py --three-act \
  --artifact-root /mnt/d/STS_BG_Mod/_oracle_data/campaigns \
  --replay-bin build/release/tools/oracle_bridge/replay/replay_run_diff \
  --archive tests/golden/oracle_corpus/three_act_a20_5.tar.gz \
  --manifest tests/golden/oracle_corpus/three_act_a20_5.manifest.json
```

Both manifests are platform-independent (they carry content hashes, never
paths), so either host regenerates them byte for byte.

`OracleCorpusReplay.*` verifies archive integrity, the v2 corpus's own contract
(every entry act-3, at least one completed double-boss run, both boss-relic
axes) and whole-run zero-diff replay in CI, in every preset. Raw campaign
artifacts remain outside the repository; these compressed curated exceptions are
the frozen smoke corpora required by the Stage B and S2 designs.
