# Promoted oracle reproducers

This is the committed, minimized regression corpus for divergences discovered
by B5.2 campaigns. Raw campaigns remain under the fixed non-repo data root;
only a reviewed case is promoted here.

Each case owns one directory:

```text
<case-id>/
  manifest.json       identity, source hashes, classification, expected result
  commands.txt        exact CommunicationMod prefix, one command per line
  witness.jsonl       minimal self-describing artifact slice, when required
  README.md           provenance, reproduction status, strip-patch audit, fix
```

`manifest.json` uses `STS-ORACLE-PROMOTED v1` and must carry:

- `case_id`, `seed`, `ascension`, `character`, and `classification`;
- the immutable source campaign id and SHA-256 of its raw JSONL;
- `first_divergence` (`seq`, `floor`, `screen`, named fields);
- `expected` (`clean`, a narrowly named capture race, or an intentionally
  retained documented deviation);
- the ledger row/change-log entry that owns the disposition.

Promotion is not a way to silence a queue item. A product divergence needs an
independent second reproduction and a fork rendering-strip audit before a
mechanic changes (the frozen precedence bar). A capture-fidelity classifier
may instead be promoted from one live witness when direct Java control-flow
evidence and later-state reconvergence prove the transient shape; its manifest
must say that a second campaign did not reproduce it. The minimized case then
gets an executable test in the same commit as its
engine/translator/harness fix. If minimization needs state not present in a
command prefix, keep the smallest self-describing JSONL slice as
`witness.jsonl`; never hand-edit a captured state.

The `b14-living-wall-obtain-race` directory records B5.2's inherited capture
fidelity triage. It is a classification witness, not a product divergence:
every differing field is the temporary one-card deck suffix while
`ShowCardAndObtainEffect` is still animating, and the next-floor state contains
the obtained card. The run-level harness recognizes only that exact shape.
