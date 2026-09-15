# S3.62 terminal clearing repair — 2026-09-15

The terminal drain now preserves Sentinel's exhaust energy before Heart
retaliation. The unchanged original constructed probe gives energy **5** after
the repair, versus **3** on a freshly built baseline; HP 20 and block 3 agree
across both. This repairs the specific defect in the
[2026-09-07 source-order audit](s3-heart-terminal-order-audit.md).

This is provisional source-backed engineering acceptance. No new live capture,
Heart kill, survival benefit, route-policy benefit, or S3-G2 closure is claimed.
S3.62 retains the live witness. No unit tests ran; no game was launched and no
bridge was deployed. The trainer's engine pin is unchanged by this task.

## Scope and source derivation

Base: `0d2cbffd62c5a7ea73def3596497269edad1456b`. Only
`src/engine/action_queue.cpp` changes engine behavior. The private predicate
`terminal_action_reaches_clear` is evaluated **before** execution, so a hit's
own effects cannot change the decision about its initial cancellation guards.
The existing four-arm allowlist is reapplied after execution only when that
source action reaches its clear and the monster group remains basically dead.
The ring retains insertion order, including additions at its front.

Complete cited methods were read from
`D:/STS_BG_Mod/SlayTheSpireDecompiled/com/megacrit/cardcrawl/`. The orchestrator
independently reviewed the mapping and diff.

| Action represented by the opcode | Repeated-clear rule |
|---|---|
| `DamageAction.update` (`actions/common/DamageAction.java:64–96`) | NORMAL cancels before clearing for a null/dead/escaped target or a dying/half-dead owner; THORNS bypasses both early-return sites. `AbstractGameAction.shouldCancelAction` (`actions/AbstractGameAction.java:81–83`) supplies the target/source condition. |
| `DamageAllEnemiesAction.update` (`actions/common/DamageAllEnemiesAction.java:49–91`) | Clears once after completion, including an empty live-target loop; per-target fan-out is not a separate clearing event. A random-target opcode with no live target executes no child and does not clear. |
| `LoseHPAction.update` (`actions/common/LoseHPAction.java:35–51`) | Clears at completion even when HP loss has no effect. Regret uses the same action. |
| `FeedAction.update`, `GreedAction.update`, `RitualDaggerAction.update` (`actions/unique/FeedAction.java:34–49`, `GreedAction.java:33–47`, `RitualDaggerAction.java:36–61`) | Require a non-null target; a dead target or failed reward condition does not skip the following clear. Invalid simulator card stamps remain rejected. |
| `VampireDamageAction.update`, `VampireDamageAllEnemiesAction.update` (`actions/unique/VampireDamageAction.java:29–45`, `VampireDamageAllEnemiesAction.java:34–80`) | Reach the completed clear even when no healing is earned. |
| `UseCardAction.update` (`actions/utility/UseCardAction.java:75–137`), `GainEnergyAction.update` (`actions/common/GainEnergyAction.java:22–31`), `SuicideAction.update` (`actions/common/SuicideAction.java:31–39`) | No clear. Suicide's DAMAGE action type determines survival, not whether it clears later actions. |

The actual damage-wrapper dispatch arms are distinguished from baked no-op
`DAMAGE_PER_STRIKE` and `DAMAGE_UPGRADE_SCALE`. Body Slam, Heavy Blade, Mind
Blast and Rampage's damage path use the DamageAction rule, with the existing
simulator's invalid-stamp guard retained. This predicate describes source
clearing calls, not whether damage changed HP.

The Sentinel sequence was reread in full: `AbstractPlayer.useCard`
(`characters/AbstractPlayer.java:1358–1384`), `Sentinel.use/triggerOnExhaust`
(`cards/red/Sentinel.java:32–43`), `CorruptionPower.onUseCard`
(`powers/CorruptionPower.java:45–50`), `PanachePower.onUseCard`
(`powers/PanachePower.java:53–61`), `BeatOfDeathPower.onAfterUseCard`
(`powers/BeatOfDeathPower.java:40–44`), and `CardGroup.moveToExhaustPile`
(`cards/CardGroup.java:850–862`). The card queues block, Panache kills, and the
surviving UseCardAction queues Beat of Death at the bottom and Sentinel energy
at the top. Energy executes before retaliation. UseCardAction is not a clearing
site. `GameActionManager.clearPostCombatActions` (`actions/GameActionManager.java:130–137`)
and the complete `AbstractRoom.update` (`rooms/AbstractRoom.java:206–391`)
establish the filter and subsequent pumping.

## Constructed evidence

External root: `D:/STS_BG_Mod/_oracle_data/s3/terminal_repair_20260915/`.
`terminal_probe.cpp` explicitly initializes every case from zeroed state. The
base case is the original audit's HP 20/80, energy 3, Corruption, Panache
countdown 1/damage 10, Sentinel, and Heart HP 10/800 with Invincible 200/200 and
Beat of Death 2. It calls the real card-play and pump entry points. No prior
turns or acquisition history are supplied, and these are not captured seeds.

| Constructed variant | Final HP / block / energy | Observation |
|---|---|---|
| Original Sentinel | 20 / 3 / 5 | Exhaust energy executes; card exhausts; queue empty. |
| Upgraded Sentinel | 20 / 6 / 6 | Upgraded block and three exhaust energy execute. |
| Sentinel + Charon's Ashes | 20 / 3 / 5 | Energy precedes the exhaust-generated empty AoE clear and retaliation. |
| Initial HP 1, Beat of Death 10 | 0 / 0 / 5 | Energy executes before lethal retaliation; death remains defeat. |
| Initial HP 1; explicit extra THORNS 99 then HEAL 50 behind UseCard | 0 / 0 / 5 | The lethal queued hit freezes the heal and later Beat of Death. This extra queue is deliberately constructed, not a legal-prefix claim. |
| Already at HP 0, same extra queue | 0 / 0 / 3 | No queued action executes; standing limbo normalization still files the card. |

The same six cases executed against fresh `win-release` and `win-asan`
libraries. ASan/UBSan compilation uses the preset's `/MD` runtime,
`-fsanitize=address,undefined`, and nonrecoverable UBSan. Both runs exit 0,
emit identical output, and report no sanitizer finding.

The orchestrator independently rebuilt baseline and patched `win-release` in
the main checkout and reran the **unchanged original audit probe**. Its files
are under `D:/STS_BG_Mod/_oracle_data/s3/terminal_integration_20260915/`, with
library/source/probe/log hashes in `manifest.json`. It reproduced energy 3 to
5 with HP/block unchanged. The expanded probes above provide additional bounded
observations; they do not independently witness every predicate branch.

## Acceptance

All six presets are targeted configure/build checks of `sts_engine` and
`replay_run_diff`, including dependencies, rather than full default-target or
unit-test runs. Commands are recorded in external scripts and logs:

```text
tools/win_build.cmd win-debug --target sts_engine replay_run_diff --parallel 4
tools/win_build.cmd win-asan --target sts_engine replay_run_diff --parallel 4
tools/win_build.cmd win-release --target sts_engine replay_run_diff --parallel 4
tools/wsl_run.cmd --script /mnt/d/STS_BG_Mod/_oracle_data/s3/terminal_repair_20260915/build_wsl.sh
tools/corpus_replay.sh win-release
tools/wsl_run.cmd --script tools/corpus_replay.sh release
```

The WSL script configures `debug`, `asan`, and `release`, then builds those two
targets with four jobs. Windows and WSL corpus replay cover all three committed
archives in ordinary replay, costs, and masks modes. Each of the nine
archive/mode combinations remains zero-diff and every corresponding injected
divergence is detected. The corpus's existing key-animation-race allowance is
unchanged. The orchestrator separately repeated the full Windows corpus walk.

`git diff --check`, `tools/check_doc_links.sh`, and
`tools/check_stale_counts.sh` are the repository hygiene checks. The external
manifest pins engine source, probe inputs/output, build libraries, scripts,
source-method files, and acceptance logs. Build warnings are existing translator
unused-variable/CMake deprecation warnings; WSL emitted a systemd user-session
startup warning before successful compilation.

| External artifact | SHA256 |
|---|---|
| `manifest.json` | `b4d4f4b3de3ed431ed5bd158008bf978e9a6b331c0a077d6da3e6b6992ade8b3` |
| `terminal_probe.cpp` | `0cb6786c4c77bbdd7c8e4f618ec32c5da4cacc3454931f6dacd693526b30cccc` |
| `probe_output.txt` and `probe_asan_output.txt` | `45bb389d127bddd42b67384a8483d903ee0cbf170a33877f2679bc4555896536` |

## Remaining boundaries

The initial terminal partition is still the pump's existing approximation of
the killing action's first clear. This task fixes **subsequent** clearing;
it does not certify initial termination caused by a non-clearing action.
Player-escape behavior, defeat stopping, halted-queue storage and the four-ring
drain bound are preserved. No schema, registry id or opcode number changes.

The terminal drain still directly executes opcodes rather than exposing a new
interactive choice continuation. No generated terminal choice is witnessed by
these probes. Dark Embrace cannot supply one here: its complete `onExhaust`
method (`powers/DarkEmbracePower.java:36–41`) refuses to draw when the monster
group is basically dead, matching the simulator. This repair does not certify
all hypothetical generated actions or frame-sensitive room completion paths.

S3.62 retains the Sentinel/Corruption/Panache live terminal-energy/action-order
witness, lethal Heart retaliation, and the full Heart-route evidence under its
unchanged acceptance. S3-G2 remains open.
