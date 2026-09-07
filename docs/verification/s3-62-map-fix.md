# S3.62 Act-4 map-symbol correction

Verification resumed 2026-09-07 in the existing `s362` worktree, clean at
`c0e5212ceb8fe59db9ee2cebe52c165243d670d2` (parent `85af052`). The existing
commit changed the planner and policy's destination-row lookups to
`act_floor_base_of(rc.run)`. It had no ledger entry; this report and the S3.62
in-progress Log repair that historical bookkeeping gap in a forward commit.
The original commit is preserved. This does not mark the campaign complete.

## Correctness and provenance

The flat `act_floor_base(act)` returns 51 for Act 4. At A20 the Door floor
and stored `act4_floor_base` are 52, so the previous emitter read the next map
row and printed `$`, `E`, `B` for the rest, shop and elite destinations.
`act_floor_base_of` reads the crossing's stored byte and retains identical
arithmetic for Acts 1–3. The search policy's four equivalent lookups now use
the same contract. No engine state, mechanics, registry ids or schema changed.

The source review read `TheEnding.generateSpecialMap` (TheEnding.java:72-139)
in full: column 3 holds rest at row 0, shop at row 1 and elite at row 2.
`DoorUnlockScreen.exit` (DoorUnlockScreen.java:143-161) sets
`isDungeonBeaten`, preventing the fade's extra room transition;
`ProceedButton.update` (ProceedButton.java:81-177), `goToVictoryRoomOrTheDoor` (:199-208) and
`goToDoubleBoss` (:210-220) establish the preceding room transitions.
These agree with [the frozen floor table](../s3-design.md#43-the-act-4-crossing-and-act-4-floor-numbers)
and the `run_advance.hpp` contract. The witness below independently records
Door floor 52 and first rest floor 53 at A20.

The original live capture is
`_oracle_data/campaigns/s362_depth_STS511413_sim_search_keys_ps76.worker-001-of-001/run_STS511413_a20_ironclad.jsonl`.
At seq 776, floor 52, the MAP screen offers `(x=3,y=0,symbol=R)`; its map array
also contains `(3,1,$)` and `(3,2,E)`. Seq 779 offers the shop at floor 53.
All three corrected symbols therefore match the recorded live map. The old
script's symbol mismatch made the follower miss its `(column,symbol)` join
and progress using glue without consuming the script step.

## Fresh deterministic emissions

Using the newly rebuilt Windows release `seed_scan`, each row of
`_oracle_data/s3/s361_triples.tsv` was emitted with:

```text
build/win-release/bin/seed_scan.exe --seeds SEED-SEED --policies POLICY --policy-seeds PS --min-floor 1 --script-dir OUT/scripts --verify-determinism --out OUT/NAME.tsv
```

Every run reported `determinism_mismatches=0`. Reopening each emitted script
and scan TSV confirmed the recorded final hash and simulator action count:

| Seed / policy / policy seed | Actions | Final hash |
|---|---:|---|
| STS511413 / sim_search_keys / 76 | 769 | d594a50d1e40f524 |
| STS511413 / sim_search_keys_deep / 804 | 840 | 4a980e3a1ea09092 |
| STS511413 / sim_search / 0 | 447 | 89e48ffc5e7ca243 |
| STS517934 / sim_search_keys_deep / 181 | 770 | 8d2a9b677ab84eab |
| STS517934 / sim_search / 0 | 305 | 99804e80b75a1884 |
| STS511413 / sim_search_keys / 5 | 684 | 9bb575936936a5f3 |

Compared as complete parsed JSON records against the original S3.61 scripts,
only six fields differ: three Act-4 `sym` fields in each of the first two
lines, from `($,E,B)` to `(R,$,E)`. Every other field and record is identical.
The ps181 line has 768 emitted steps but 770 simulator actions, identically
before and after; action-toggle normalization explains that distinction.

## Build and replay evidence

Raw commands, logs, regenerated scripts and comparison output stay under
`D:/STS_BG_Mod/_oracle_data/s3/s362/verification_20260907/`.
`build_windows.cmd` configures and builds `win-debug`, `win-asan` and
`win-release` through the conventions' vcvars64 plus LLVM wrapper: all pass
(exit 0). The WSL invocation is the build-only form:

```text
tools/wsl_run.cmd --script tools/build_presets.sh
```

The WSL helper completed with `PRESETS BUILT: debug asan release` and exit 0.
All six presets therefore build successfully. Windows corpus replay ran as
`PYTHON3=C:/Python39/python.exe bash tools/corpus_replay.sh win-debug`
through Git-for-Windows bash. All three committed archives replayed zero-diff
in all three modes (`--replay`, `--costs`, `--masks`), and all nine injected
divergence controls failed with the required exit 1; the overall script exited
0. Repository-wide stale-count and Markdown-link checks pass.
No unit tests were written or run.

The directly invoked original-capture replay uncovered an independent
translator omission, which is not concealed by neutralizing the field:

```text
build/win-debug/bin/replay_run_diff.exe --replay CAPTURE1 CAPTURE2
```

`CAPTURE1` is the original depth witness above. It compares 780 records and
reports four mismatches at seq 776–779, all solely `act4_floor_base: 0 -> 52`.
The translator never populates that byte; the differ intentionally compares
it. `CAPTURE2`, the existing follow-up
`_oracle_data/campaigns/s362_d2_STS511413_sim_search_keys_ps76.worker-001-of-001/run_STS511413_a20_ironclad.jsonl`,
replays CLEAN across 749 records but exhausts at floor 51, before Act 4.
The orchestrator owns the separate translator fix and integrated replay.

There is no complete post-fix live capture here. The original map symbols
witness this tooling correction, while neither interrupted capture discharges
S3.62's terminal, breadth, combat or no-race acceptance. Those obligations
remain open.
