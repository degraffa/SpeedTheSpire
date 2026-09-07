# S3 bridge facing repair: source review

**SOURCE-REVIEWED; runtime witness pending.** Reviewed and built 2026-09-07
on base `edc3d09dd830d5f4c1113af04c4e13e845068ef8`. This is a repair of the
bridge's input behavior; the simulator's facing mechanic is unchanged.

The bridge's targeted `play` command directly enqueued a `CardQueueItem`,
bypassing the facing update in retail `AbstractPlayer.playCard`
(AbstractPlayer.java:1285-1302, assignment :1292). Its targeted potion
command similarly bypassed `PotionPopUp.updateTargetMode`
(PotionPopUp.java:183-215, assignment :198). With Surrounded active, a
command targeting the Shield could leave the game facing right while the
simulator correctly turned left, changing which guard applies back attack.
Both command methods and both retail methods were read in full; no other
bridge Java source supplied the omitted facing write.

`CommandExecutor` now turns the player toward the selected monster, guarded
by `hasPower("Surrounded")`, immediately before targeted card enqueue or
potion use. The targeted-potion path also queues the retail combat
`HandCheckAction` after use and before relic callbacks (:202-204).
Non-target actions keep their existing facing. The related simulator comment
now correctly names `PotionPopUp.updateTargetMode`, not `updateInput`.

This preserves the command grammar and JSON schema. No protocol version or
runtime manifest pin changes; the built jar hash identifies the source
revision. `PROTOCOL.md`'s existing fork provenance and build flow were read.
The jar has **not been deployed** and no game was launched.

## Compilation evidence

From the task's worktree:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/oracle_bridge/build_fork.ps1 -NoDeploy -CheckDeterminism -OutDir D:/STS_BG_Mod/_wt/codex/s3-guards-review/build/oracle_facing_20260907
```

The sanctioned JDK 8 pipeline exited 0; a second full build produced an
identical jar. SHA256:
`0AFB0176699C555DE9CFF62D14249BFECA72E658F059928D6BC6A30AFB723C2D`.
A copy of the compiled artifact and build log is preserved outside the
worktree at `_oracle_data/s3/source_audit_20260907/bridge/`; this is not an
installation. Source audit and raw build evidence remain at
`D:/STS_BG_Mod/_oracle_data/s3/source_audit_20260907/`.
No unit tests were written or run. Markdown-link, stale-count and diff checks
pass.

## What remains

A live command sequence alternating targets, including a targeted potion,
still needs to verify facing, incoming damage and settled marker placement.
`HandCheckAction` itself only calls hand `applyPowers` and `glowCheck`
(HandCheckAction.java:14-18); it does not settle the simulator's separately
documented `refreshHandLayout` marker-timing approximation.

The same review found an independent simulator omission in Smoke Bomb
legality: `combat_potion_legal` skipped the `BackAttack` rejection in
`SmokeBomb.canUse` (SmokeBomb.java:51-63). That finding was handed to the
orchestrator for a separate correction. This bridge repair does not close
S3.62's kill-order, terminal-capture or full verification gate requirements.
