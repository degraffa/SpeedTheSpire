# S3 Smoke Bomb BackAttack source audit

## Finding and fix

`SmokeBomb.canUse` (`SmokeBomb.java:51-63`) first calls the superclass gate,
then walks every member of the current monster group. It refuses use when any
member `hasPower("BackAttack")`, and separately refuses any boss-type member.
There is no dead/dying/escaping filter in this loop.

`combat_potion_legal` already implemented the boss-type half but omitted the
BackAttack half under a stale comment that called the power an unregistered
Act-3 Snecko/Spiker mechanic. BackAttack is now a live registered marker on the
Act-4 Shield-and-Spear encounter. The fix scans every stored monster power slot
and refuses Smoke Bomb on `PowerId::BACK_ATTACK`. It tests identity/presence,
not a positive amount: `BackAttackPower` has the marker amount -1. It applies no
liveness filter. Boss handling and the later superclass-equivalent
`areMonstersBasicallyDead` gate are unchanged.

`back_attack.hpp` previously claimed nothing in the engine reads the marker.
That is corrected: damage reads the geometric predicate, while Smoke Bomb
legality reads marker presence. The existing declared timing deviation can
therefore affect the potion mask during a card resolution, though it cannot
affect damage.

## Evidence status

This is an exact source-derived correction, but remains runtime-oracle
unverified. The available integrated `STS511413 / sim_search_keys / ps76`
artifact is a policy script, not a captured state stream; passing it to
`replay_run_diff` refuses record 0 as `unknown record_kind`, recorded under
`D:\STS_BG_Mod\_oracle_data\s3\source_audit_20260907\smoke\natural_script_replay.txt`.
It therefore cannot supply a translated guard state for the proposed copied-
state inventory diagnostic. No synthetic state is presented as natural or
oracle evidence, and no live game was launched.

The smallest future runtime witness is a real Shield-and-Spear capture with a
Smoke Bomb in inventory. A copied real guard state with a Smoke Bomb inserted
would be useful directed-counterfactual evidence only: legality must be false
with either live or dead BackAttack-bearing group members and may become true
when that marker is removed and no boss remains. It would not be a natural
capture or oracle witness.

## Acceptance

Windows `win-debug`, `win-asan`, and `win-release` configured and built the
`replay_run_diff` target through `tools/win_build.cmd`. WSL `release` configured
and built through `tools/wsl_run.cmd --script tools/build_presets.sh release`.
The committed oracle corpora were replayed with
`PYTHON3=C:/Python39/python.exe bash tools/corpus_replay.sh win-debug`; all
three archives ran in `--replay`, `--costs`, and `--masks` modes with zero
diffs, and all nine injected controls failed with the required exit 1. The
corpus log is under the external source-audit directory above (SHA256
`819af8fc43a2dcb462fce6e89b8147a83486cba4826e90a3e0382ab3334c1c1c`).
Builds completed interactively; no separate build log was retained. No unit tests were
written or run. No schema, registry, facing, damage, trainer pin, live game, or
jar changed.
