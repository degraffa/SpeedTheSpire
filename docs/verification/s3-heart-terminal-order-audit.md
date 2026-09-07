# Heart terminal action ordering — source audit, 2026-09-07

This bounded follow-up to the [S3 source audit](s3-source-audit.md) found a
specific mismatch in the simulator's post-combat queue filtering. A constructed
Sentinel/Corruption/Panache play leaves terminal energy at 3; the complete
game-source action sequence gives 5. Beat of Death still resolves and the
constructed case has the same HP and block. No survival improvement or full-run
performance effect has been demonstrated.

The engine is unchanged. This is a precise repair obligation, not a completed
repair or a live witness. Work began at
`e27db2ede3074185288bdcf2f9b79249471a60e5`. No game was launched, no bridge was
deployed, and no unit tests were written or run. The trainer's engine pin is
unaffected.

## Mismatch and source trace

Source paths are relative to
`D:/STS_BG_Mod/SlayTheSpireDecompiled/com/megacrit/cardcrawl/`. Complete methods
named below were read, including their early returns and queue insertions.

1. `AbstractPlayer.useCard` (`characters/AbstractPlayer.java:1358–1384`)
   executes the card's `use` before constructing and appending `UseCardAction`.
   `Sentinel.use/triggerOnExhaust` (`cards/red/Sentinel.java:32–43`) queues
   block on play and adds energy to the **top** on exhaust.
2. `UseCardAction`'s constructor (`actions/utility/UseCardAction.java:33–68`)
   calls player powers' `onUseCard`. `CorruptionPower.onUseCard`
   (`powers/CorruptionPower.java:45–50`) marks Sentinel to exhaust, and
   `PanachePower.onUseCard` (`powers/PanachePower.java:53–61`) appends its
   fifth-card damage. Thus the queue is block → Panache damage → UseCardAction.
3. When Panache kills the final monster, `DamageAllEnemiesAction.update`
   (`actions/common/DamageAllEnemiesAction.java:49–91`) calls
   `GameActionManager.clearPostCombatActions`
   (`actions/GameActionManager.java:130–137`). UseCardAction survives.
4. `UseCardAction.update` (`actions/utility/UseCardAction.java:75–137`) queues
   Beat of Death from `onAfterUseCard`, then exhausts Sentinel through
   `CardGroup.moveToExhaustPile` (`cards/CardGroup.java:850–862`).
   `BeatOfDeathPower.onAfterUseCard` (`powers/BeatOfDeathPower.java:40–44`)
   appends THORNS damage; Sentinel's exhaust trigger prepends GainEnergyAction.
   The meaningful queue is now energy → Beat of Death.
5. UseCardAction does **not** call `clearPostCombatActions`.
   `GainEnergyAction.update` (`actions/common/GainEnergyAction.java:22–31`)
   therefore executes before the retaliation. `AbstractPlayer.gainEnergy`
   (`characters/AbstractPlayer.java:1555–1558`) has no dead-monster guard.
   The room continues pumping while actions remain
   (`rooms/AbstractRoom.java:206–391`). A later completed `DamageAction`
   may clear the queue again (`actions/common/DamageAction.java:64–96`).

The simulator's `resolve_pending_post_combat_actions_at_terminal` in
`src/engine/action_queue.cpp` instead reapplies the survivor allowlist after
**every** executed action. Immediately after UseCardAction, it moves the new
GAIN_ENERGY to the abandoned queue because that opcode is not on the allowlist.
The claim in its adjacent comment that damage actions clear repeatedly does
not justify clearing after a non-damage action too.

The required repair must preserve the distinction between the initial clear,
subsequent actual clearing actions, and actions that return before clearing.
Simply admitting all generated actions, or clearing after every damage-shaped
opcode regardless of its early return, is not a source-derived repair. This
touches the general terminal drain, so it remains a separately scoped follow-up.
It narrows the earlier audit's positive finding: lethal Heart retaliation is
preserved, but the entire surrounding survivor queue is not yet source-consistent.

## Constructed simulator evidence

`terminal_probe.cpp` is an external constructed-state probe. It is neither a
unit test nor a captured seed/action prefix. It initializes a player with HP
20/80, energy 3, zero block, Corruption, Panache countdown 1/damage 10, and one
unupgraded Sentinel. The Heart has HP 10/800, zero block, Invincible 200/200 and
Beat of Death 2. Other state bytes start zero. This deliberately omits prior
turns, acquisition history and a claim that any saved seed reaches this build.

It calls the real `resolve_card_play`, then `pump_step` until combat-over.
Observed sequence: block 5; Panache deals 10 and drains the cap to 190; Sentinel
exhausts; retaliation consumes 2 block. The final state is HP 20, block 3,
energy 3, with abandoned `GAIN_ENERGY(2)`. The manual source trace predicts
energy 5, with the same HP/block and exhausted card. The combination is
source-reachable in supported Ironclad content, but not witnessed live.

The probe was compiled with one clang-cl process and linked against the
existing master `win-release` library. The clean master checkout was confirmed
at the base above; the library is hash-pinned in the manifest. This audit did
not rebuild the engine or re-run corpus replay, and does not add fresh engine
acceptance evidence. Earlier integration acceptance remains recorded in the
[prior audit](s3-source-audit.md#integration-acceptance).

External root: `D:/STS_BG_Mod/_oracle_data/s3/targeted_audit_20260907/`.

| Artifact | SHA256 |
|---|---|
| `terminal_probe.cpp` | `5034f0353021d5df537d02611dd5793c85b584c31a1d90bf5cce377624ef544b` |
| `probe_output.txt` | `06a2cdc218fe66ab1e327775536c6ef93375f0f522660346ec7445faf877fddc` |
| `saved_scan.json` | `c9ed3cf335c416d0b73bc281344de166edfb75cc8f364f00a422ed6c49bc35df` |
| `manifest.json` | `4a0cbaa393e5bf6f7e0c946c0413de486a0887b6ecef015bfd7bacc57de1ee9a` |

The root also contains `build_probe.ps1`, `scan_saved.py` and
`make_manifest.py`. The manifest records source hashes, library hash and
commands. `saved_scan.json` records every inspected input identity and hash.

## Bounded positive checks and missing evidence

The integer damage tail agrees for the examined ordinary Heart paths:
`AbstractMonster.damage` (`monsters/AbstractMonster.java:622–710`) applies block,
then Boot, then Invincible. `Boot.onAttackToChangeDamage`
(`relics/Boot.java:31–38`) lifts positive NORMAL residue below 5 before the cap;
Invincible then clips and drains it (`powers/InvinciblePower.java:32–42`).
For example, an unblocked residue of 2 with a remaining pool of 3 becomes 5
through Boot and then 3 through Invincible. This is a manual source calculation,
not an executed probe. `interp_damage.cpp` preserves that order.

Beat of Death's THORNS type bypasses Torii's NORMAL-only reduction
(`relics/Torii.java:31–38`), while Tungsten Rod still subtracts one from positive
post-block damage (`relics/TungstenRod.java:26–32`). Buffer's reduction is before
those relics (`powers/BufferPower.java:41–46`;
`characters/AbstractPlayer.java:1387–1516`). The simulator agrees on these
receive sites. Exhaust callbacks also preserve the initial queue directions:
Charon's Ashes prepends damage (`relics/CharonsAshes.java:36–43`), while Feel No
Pain appends block (`powers/FeelNoPainPower.java:36–39`). Neither observation
certifies the subsequent terminal filtering.

The bounded saved scan reopened raw captures in all three committed corpus
archives plus the existing S3.62 STS511413 prefix: 60 files, 5,436 combat records.
It found one record with Sentinel in a combat pile and 118 with Panache active,
but no record combining Sentinel with active Panache and Corruption, and no
Heart record. It therefore provides no live witness of this discrepancy.
Seven S3.61/integrated saved scripts were also inspected and hashed; their
Act-4 combat actions supply partial guards, not a Heart terminal.

S3.62 retains the obligation to witness terminal energy and action order with
this combination after a scoped repair, alongside the existing lethal
retaliation and full Heart-route evidence. S3-G2 remains open.
