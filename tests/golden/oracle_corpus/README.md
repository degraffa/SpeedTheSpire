# Oracle corpus

This directory contains the small committed oracle artifacts allowed by
`docs/conventions.md`.

- `skeleton_sample.jsonl` is the B1.5 translator/adapter fixture.
- `act1_a20_50.tar.gz` and `act1_a20_50.manifest.json` are the B5.4 curated
  full-run CI corpus.

The compressed corpus contains exactly 50 distinct zero-diff A20 Act-1 seed
captures plus their translated combat traces. The manifest records the
selection policy, campaign provenance, source and trace hashes, and translated
trace headers. Rebuild it only from the fixed external campaign root:

```bat
C:\Python39\python.exe tools\verify_report\build_ci_corpus.py ^
  --archive tests\golden\oracle_corpus\act1_a20_50.tar.gz ^
  --manifest tests\golden\oracle_corpus\act1_a20_50.manifest.json
```

`OracleCorpusReplay.*` verifies archive integrity and whole-run zero-diff
replay in CI. Raw campaign artifacts remain outside the repository; this
compressed curated exception is the frozen smoke corpus required by the Stage
B design.
