# S3.62: translate the Act 4 floor base

The translator left `RunState::act4_floor_base` at zero on every record.
The first available Act 4 capture therefore reported four differences at
sequences 776–779: expected 0, simulator 52. This was a translator omission,
separate from the route emitter's map-symbol error documented in the S3.62
task Log.

`parse_game_state` now derives the byte from the capture's own act and
ascension: 51 below A20, 52 at A20, populated only in Act 4. It does not read
the simulator's state or suppress comparison of this field. The derivation
follows [S3 design §4.3](../s3-design.md#43-the-act-4-crossing-and-act-4-floor-numbers):
`ProceedButton.update/goToDoubleBoss/goToVictoryRoomOrTheDoor`
(`ProceedButton.java:101–106,199–220`) includes the second boss at A20;
`DoorUnlockScreen.exit` (`DoorUnlockScreen.java:143–161`) sets
`isDungeonBeaten`, so `AbstractDungeon.updateFading`
(`AbstractDungeon.java:2309–2328`) skips a further room transition at the
Door. These methods and `TheEnding.generateSpecialMap` were read in full.
No engine rule, schema version or capture bytes changed.

## Witness and limits

The original capture is
`D:/STS_BG_Mod/_oracle_data/campaigns/s362_depth_STS511413_sim_search_keys_ps76.worker-001-of-001/run_STS511413_a20_ironclad.jsonl`.
Before the change, `c0e5212` reports the four floor-base differences. After
the change, `replay_run_diff --replay <capture>` compares 780 records with no
differences. Its verdict is `CLEAN`, with two explicitly reported key-animation
races and `stop: artifact exhausted`. The capture has no terminal record;
this result verifies the translator correction on the captured prefix and
does **not** complete S3.62 or discharge its zero-race terminal-capture bar.

The A20 value is live-witnessed by this capture. The below-A20 value follows
the source and frozen design; a below-A20 live entry remains owed by S3.62.
Logs are retained outside the worktree under
`D:/STS_BG_Mod/_oracle_data/s3/s362/translator_20260907/`.

## Verification

All six presets configured and built `replay_run_diff` and its engine,
translator and differ dependencies. Windows used the conventions' vcvars64
plus LLVM wrapper; WSL used `tools/wsl_run.cmd --script
build/act4_build_targets.sh`, whose retained copy configures each of
`debug`, `asan`, `release` and runs `cmake --build --preset <preset>
--target replay_run_diff --parallel 4`. The inherited map-fix task separately
built the full default targets in all six presets.

The live prefix replays with identical verdicts under Windows release,
Windows ASan/UBSan, and WSL release. `tools/corpus_replay.sh win-release`
(Git-for-Windows bash) and `tools/wsl_run.cmd --script
tools/corpus_replay.sh release` both finish with exit 0: all three committed
archives are zero-diff in `--replay`, `--costs`, and `--masks` modes, and all
nine injected-divergence controls per host reject with the required exit 1.
The original four mismatches on `c0e5212` supply the before-change witness.
The map-fix worker independently reviewed the translator derivation.
Documentation-link, stale-count, and whitespace checks pass. No unit tests
were written or run.

## Capture-debt correction

The [S3-G1 report](s3-g1-content.md#84-task-level-capture-debts-named-in-their-own-logs)
incorrectly called Echo Form an ordinary Ironclad card and proposed obtaining
its translator-field witness through ordinary breadth. The full constructor
in `cards/blue/EchoForm.java:25–28` declares `CardColor.BLUE`; the Prismatic
Shard registry row explicitly leaves cross-colour rewards inert. The report
now names that reachability limitation and assigns the exact per-row
disposition to S3.62. Its unverified status remains open. This corrects the
verification report, without changing frozen mechanics or waiving a gate.
