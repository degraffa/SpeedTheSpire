# Oracle bridge driver (Stage B)

This directory holds the oracle-bridge driver family. Protocol details are in
[../PROTOCOL.md](../PROTOCOL.md) (surveyed from the vendored source at B0.1).

| File | Task | Role |
|---|---|---|
| `campaign_driver.py` | **B1.4** | the real campaign driver — the CommunicationMod-oracle child: seeded A20 Ironclad starts, random-legal / greedy / scripted generators, per-run JSONL artifacts (design 2.7), crash detection, seed-level resume, batch over a seed list |
| `greedy_policy.py` | **B4.x** | the `--policy greedy` scorer: a pure, depth-seeking heuristic over the parsed dump (combat, map, rewards, screens) |
| `survival_policy_cmd.py` | **TE.1** | the survival-biased policy as an **external binary** (`--policy external --policy-cmd`): greedy scoring behind the STS-POLICY-IO v1 stdio protocol, with a strict JSON config surface for per-cohort constant overrides |
| `script_policy_cmd.py` | **S2.V2** | the STS-SCRIPT v1 follower as an **external binary** (`--policy external --policy-cmd`): replays a `seed_scan --script-dir` scripted line by matching each step's stable identity against the live dump, and on ANY desync writes a `script_divergence` record and stops the run (exit 3) — a desync is Stage-B capture evidence, never routed around. Schema in the planner README; unit-tested against the committed corpus without launching the game |
| `standing_triage.py` | **TE.1** | conservative shape-checker that classifies a campaign's divergent runs against the reviewed standing deviations (Looter stolen-gold ordering, Fairy-bottle belt-slot timing); anything else stays unmatched for manual triage |
| `cards_sidetable.json` | **B4.x** | committed per-card damage/block numbers the greedy policy scores with |
| `gen_cards_sidetable.py` | **B4.x** | regenerates `cards_sidetable.json` from `registry/cards.yaml` (dev-time, needs PyYAML; the driver itself never imports it) |
| `orchestrator.py` | **B1.4** | Windows-host outer loop that owns the game process: writes config.properties, launches ModTheSpire under the bundled JRE 8, relaunches on crash/hang/boss-reward, induces a kill for acceptance |
| `campaign_pipeline.py` | **B5.2+** | one-command isolated N-instance capture → strict validation → serial translation/traces → replay + encounter-list diffs → per-worker and aggregate reports/triage; deterministic seed shards and Windows nightly scheduling |
| `instance_runtime.py` | **B5.2+** | fail-closed per-worker game/config/temp/profile runtime materialization and resume identity |
| `postprocess_campaign.sh` | **B5.2** | WSL half of the pipeline, reached only through `tools/wsl_run.cmd --script`; builds the release tools and writes per-seed derived artifacts |
| `validate_artifacts.py` | **B1.4** | validates run JSONL against the PROTOCOL.md schema (header + action `state_json` + oracle block + terminal) |
| `echo_driver.py` | B0.2 | bring-up child: logs every state JSON, forwards side-channel commands, `--verify` re-parse |

## B5.2 one-command campaigns

The campaign artifact root is now fixed at
`D:\STS_BG_Mod\_oracle_data\campaigns`. It is outside the repository and is the
same root the B1.4 orchestrator already used; B5.2 removes the pipeline-level
root override so a nightly task cannot accidentally write raw captures into a
worktree.

On the Windows host, one command owns the whole path:

```bat
C:\Python39\python.exe campaign_pipeline.py run ^
  --campaign-id b52_nightly_20260729 ^
  --seeds D:/STS_BG_Mod/_oracle_data/b52_50_seeds.txt ^
  --policy random-legal ^
  --instances auto
```

Resume is the default. Re-running the identical command resumes the seed-level
progress ledger and then regenerates derived outputs. `--fresh` is explicit and
has the orchestrator's narrow cleanup semantics; a changed seed list, policy,
policy seed, or shard identity is refused under an existing campaign id.

If a parallel campaign's original source list has been retired, resume it with
`--resume-existing` instead of `--seeds`. The pipeline reloads only that
group's persisted, canonical seed partition and still refuses policy, shard,
topology, runtime, or worker-identity drift. This option does not apply to the
pre-parallel single-runtime layout, which still requires its original list.
An interrupted current seed is retried first; its incomplete JSONL and timing
sidecar are replaced before the new attempt writes its header, while completed
seeds are retained.

`--instances N` starts that many simultaneous isolated game workers;
`--instances auto` (the default for a new campaign group) resolves a
conservative count from the selected seed count, one slot per presumed
physical core (half the logical CPU count), and currently available RAM. The
resolved number, exact seed partition, JVM heap/recycle
limits and worker campaign ids are persisted before launch. Resume reuses that
topology; requesting an explicit count that would change it under the same id
is refused.
An explicit count larger than the selected seed set is reduced to one worker
per seed.

The top-level coordinator still takes the nonblocking OS-backed lock at
`D:\STS_BG_Mod\_oracle_data\oracle_game.lock`. It now protects scheduler and
capacity ownership rather than a shared game config: a second coordinator
fails immediately with exit `11` and names the owner, while the one owner may
launch all of its recorded workers. The OS releases the lock after a crash.
Existing in-progress campaigns created before multi-instance support are
detected and resume through their original one-game/install-directory path;
start a new campaign id to gain parallelism.

Before any worker starts, the Windows entrypoint places the coordinator itself
in a kill-on-close Job Object, so every orchestrator inherits containment at
process creation. Each orchestrator gives its JVM tree a nested kill-on-close
job. A coordinator crash therefore retires the orchestrators and games before
a successor can safely reuse their runtimes; ordinary exceptions and
interrupts use the same cleanup path explicitly.

Every new worker is an ordinary B5.2 shard with its own artifact directory and
an external runtime under `D:\STS_BG_Mod\_oracle_data\runtimes`. The runtime
hard-links the large immutable game jar, copies the exact fork jar it loads,
copies the fixed fully-unlocked profile template, and gives the JVM private
`preferences/`, `saves/`, `runs/`, logs, temp, `LOCALAPPDATA` and `APPDATA`.
The driver hashes the worker-local fork copy, so artifact provenance names the
bytes ModTheSpire actually loaded. Immutable runtime inputs, the pinned profile
template, or any worker unlock-state drift refuse resume; ordinary statistics
changes in the original install profile do not replace an existing template.
The first template snapshot also refuses if the install profile changes while
it is being copied; stop any legacy install-directory campaign and retry.

Workers use the installed game's own memory boundary (`-Xms256m -Xmx1g`) and
are recycled after a bounded number of completed seeds (default 50). Override
the count with `--seeds-per-launch` when measuring a different lifecycle.
Capture workers overlap; translation/replay/report post-processing is joined
and run serially because all shards share the repository's WSL build tree.

Arbitrary campaign sizes use the same format. This is the 200-run harvest shape
for the distributional suite:

```bat
C:\Python39\python.exe campaign_pipeline.py generate-seeds ^
  --start 50000 --count 200 ^
  --out D:/STS_BG_Mod/_oracle_data/b53_200_seeds.txt
C:\Python39\python.exe campaign_pipeline.py run ^
  --campaign-id b53_oracle_spot_20260729 ^
  --seeds D:/STS_BG_Mod/_oracle_data/b53_200_seeds.txt ^
  --policy greedy
```

A promoted triage prefix can be captured through the same singleton pipeline:

```bat
C:\Python39\python.exe campaign_pipeline.py run ^
  --campaign-id b52_obtain_race_repro ^
  --seeds STS00009 --policy script ^
  --script ../../../tests/golden/oracle_reproducers/b14-living-wall-obtain-race/commands.txt
```

The script path and SHA-256 are part of the resumable campaign identity.

For multiple hosts, pass the same source list and `--shard-count N
--shard-index I` (zero-based). The host first selects `seeds[I::N]`, then its
local `--instances` workers interleave only that selection. Hosts may therefore
use different local worker counts without changing one another's seed sets.
The host group is named `<campaign-id>.shard-<I+1>-of-<N>` and every worker has
its own suffix and progress ledger; their union is exactly the input list.

Install a resumable daily Windows Task Scheduler entry (local time):

```bat
C:\Python39\python.exe campaign_pipeline.py schedule ^
  --campaign-prefix oracle_nightly ^
  --seeds D:/STS_BG_Mod/_oracle_data/nightly_seeds.txt ^
  --policy greedy --instances auto --at 01:00
```

The scheduled action runs the `nightly` subcommand, whose campaign id includes
the UTC date. Running it again on the same date resumes; the next date gets a
new immutable evidence directory. The full nightly argv is stored under
`D:\STS_BG_Mod\_oracle_data\schedules`; Task Scheduler receives only a short
`scheduled --config ...` action, staying below `schtasks.exe`'s `/TR` length
limit. Add `--print-only` to audit the command without changing Task Scheduler
(it still refreshes that external config).

After capture, the pipeline calls the sanctioned WSL boundary helper to build
the release post-process tools. Each campaign directory then contains:

```text
run_<SEED>_a20_ironclad.jsonl        raw self-describing capture
run_<SEED>_a20_ironclad.timing.jsonl action timing
traces/<SEED>.trace                  translated CombatState trace
translation/<SEED>.{log,status}      fail-loud translator result
diffs/<SEED>.{log,status}            whole-run replay/diff report
encounter_lists/<SEED>.{log,status}  raw monster/elite/boss list oracle
triage/pending/index.json            authoritative open queue
triage/pending/<SEED>.reproducer.json
triage/pending/<SEED>.commands.txt   prefix through first divergence
report.json                          machine-readable counts/throughput
report.md                            generated operator summary
```

`report.json` is aggregation-ready: it binds schema/driver/fork provenance,
hashes every source artifact, keeps the per-seed outcome/floor/action/attempt
rows, emits outcome and floor histograms, and distinguishes captured,
replay-clean, and strict-zero-diff action totals. Strict totals exclude every
capture-race family named by the replay summary (currently obtain/Entropic,
Smoke-Bomb escape settlement, and Living Wall's transform-preview cardRng
burn). Reports retain the legacy obtain-only field and
add an all-family total plus by-kind map, so older v1 reports remain readable
without allowing a current escape race into strict evidence. Known
capture-race records remain an explicit separate count and never enter strict
evidence. Adding another family requires its own narrow replay classifier;
ordinary pending product or harness findings still follow the exact
disposition workflow instead of being treated as capture races.

Exit `0` means the full pipeline completed with no untriaged item. Exit `10`
means every artifact/report was produced but one or more translation, raw-list,
or replay divergences were queued; this is intentionally non-green and must be
triaged, not tuned away. Infrastructure/validation failures retain their own
nonzero exit; exit `11` specifically means another campaign owns the
machine-wide coordinator/capacity lock.

Promoted, minimized cases live under
`tests/golden/oracle_reproducers/`; its README defines the review and promotion
bar. A pending item names that destination and the binding workflow: reproduce
twice, audit the fork's strip patches, then update the ledger and the owning
change log with the fix or sanctioned frozen-spec decision.

## B1.4 campaign driver

`campaign_driver.py` is the CommunicationMod-oracle child (the game spawns it and
owns its stdio — see [Topology](#topology-protocolmd-1)). It is a strict
**lock-step** stepper: it sends **one command per fresh `ready_for_command`**
state and **never blind-resends** on silence (prolonged silence is a crash, per a
wall-clock watchdog — the B0.2 contamination postmortem, ledger B1.1 Log). It
plays seeded A20 Ironclad runs to a terminal state (death, **victory**,
legal-action exhaustion, or an action cap), writing one JSONL artifact per run.

> **Terminal moved in `b1.7.0` (S2.42).** Drivers up to `b1.6.0` stopped at the
> **Act-1** boss combat reward and refused the `proceed` that opens the boss
> chest, because the chest was out of S1 scope (design 1.1 "Out"). S2-G2 items
> 2–3 are *about* the boss chest, so the terminal is now the game's own
> GAME_OVER — the run plays all three acts. Two consequences worth knowing:
> the `act1_boss_reward` outcome no longer occurs on new captures (old
> artifacts carrying it stay valid and the pipeline still counts them), and
> deep runs no longer force an orchestrator relaunch, because a GAME_OVER
> screen *can* walk back to the menu.

Because the game (not the orchestrator) is the driver's parent, the driver cannot
own game launch/kill/relaunch — `orchestrator.py` does. The driver owns the
per-run protocol, the artifacts, and a durable `campaign_progress.json`; a
crashed game costs one run, not the campaign (design 7.1(2)). Resume granularity
is **one seed** (the protocol exposes no mid-run save): an interrupted seed is
re-run from `start` on the next launch (retry-once, then failed).

**Current capture driver: `b1.7.1`** (`--boss-reward-via-policy`: the policy
loop claims boss combat rewards — required for scripted-line depth cohorts,
whose claims live in the script; default off = `b1.7.0` behavior) —
`campaign_driver.DRIVER_VERSION` is
authoritative; this line said `b1.5.3` against a `b1.6.0` tree until S2.42, so
re-derive it rather than quoting it. A driver exit code is not visible to the
orchestrator because the game owns the child process. The driver therefore
publishes a durable, one-launch-token-bound restart request before every
mid-dungeon or broken-pipe exit; the orchestrator sees it on its ordinary poll,
kills that exact game process, and relaunches immediately. A stale request from
the previous launch cannot kill its successor. Progress and heartbeat atomic
renames retry only the bounded Windows sharing/access errors caused by the
orchestrator reading the same file and otherwise fail loud. Finally, a
temporarily empty action expansion is allowed a bounded 50 ms-per-probe render
settle window, and Calling Bell's three mandatory Neow relic rows suppress the
misleading simultaneously advertised `proceed`. These are capture-liveness
rules, not changes to artifact or simulator semantics.

### Running a lower-level single campaign (diagnostics only, Windows host)

```bat
C:\Python39\python.exe orchestrator.py ^
    --campaign-id b14_accept ^
    --seeds D:/STS_BG_Mod/_oracle_data/campaigns/b14_seeds.txt ^
    --policy random-legal ^
    --kill-after-seeds 3 --fresh
```

Use `campaign_pipeline.py run` for ordinary and parallel campaigns. A direct
orchestrator invocation is retained for diagnostics and historical acceptance;
never run two of them independently. In the legacy form above it writes the
host `%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties`. The
pipeline instead supplies the private runtime working/config/temp paths and
worker-local fork jar. In either form the orchestrator launches under
`<game>\jre\bin\java.exe` (bundled JRE 8 — never system Java), watches
`campaign_progress.json` + `campaign_heartbeat.json`, and relaunches on crash,
hang, boss reward or the configured seed-recycle boundary until the driver
marks the campaign complete. `--kill-after-seeds N` induces one deliberate
mid-campaign game kill to exercise crash-resume.

An oracle campaign is valid only when the explicitly selected
`CommunicationMod-oracle` fork emits `game_state.oracle`. The driver checks the
first in-dungeon dump before it creates an artifact or advances the policy; a
missing block records `status: fatal_environment_drift`, and the orchestrator
stops instead of relaunching.

**The runtime stack is observed, never assumed (B4.5).** Immediately after that
check the driver reads the exact append-only `mts_launch<N>.log` allocated for
its game process, parses ModTheSpire's `Version Info:` / `Mod list:` block, and
writes **those** values into the artifact header (`game.sts_version`,
`game.mts_version`, `game.basemod_version`, `game.version_source`, plus the full
`mods_loaded` map). The orchestrator binds that filename to the driver command
and passes a one-use token through the game process's inherited environment;
resume continues numbering above every preserved log, while a later GUI launch
inherits the persisted command but not the matching environment binding. It
refuses with the same
`fatal_environment_drift` status, carrying a distinguishing `kind`, when the
observed stack is not the sanctioned one
(`stack_version_mismatch` — which also covers stock `CommunicationMod` loaded
beside the fork, and the fork being absent), when the log has no readable
version block (`stack_unparseable`), or when there is no launch log at all
or it is not bound to this orchestrator launch (`stack_unobservable` — the
GUI-launch case, which is exactly how the invalid `b45_rewards` capture arose).
Before B4.5 those header fields were hard-coded constants, so every artifact
ever written asserted the sanctioned versions regardless of what actually
launched. The sanctioned values now live in `campaign_driver.py`'s
`SANCTIONED_*` constants and are only ever *compared* against the log, never
copied into a header; moving the pin means editing those constants **and**
design §1.2 deliberately (design §11 v0.1.7).

Run campaign acceptance with
`validate_artifacts.py --require-oracle`; the default validator remains
backward-compatible with old, deliberately non-oracle B1.4 artifacts.
Strict campaign validation requires a complete, failure-free progress and
manifest ledger, exact ordered seed completion, at least one valid in-game
oracle action per run, and one current run plus timing artifact per completed
seed with no missing or extra files. Run headers are bound to campaign id,
seed string/long/getLong, attempt, policy, schema, driver, and fork identity;
every in-game `game_state.seed` and `oracle.seed` must agree. Strict run grammar
requires contiguous action sequence numbers, exactly one final terminal, and
matching injected-action/terminal/done summaries. Timing sidecars are parsed in
full and joined mark-for-action (sequence, command, floor, and screen), with
their headers bound to the same campaign/schema/driver/fork identity.

Treat a campaign id as immutable evidence. A new live attempt gets a new id;
failed directories are preserved rather than retried in place. `--fresh`
removes only this invocation's known control files, launch logs, and exact
requested-seed run/timing names. It deliberately preserves unexpected files,
which strict validation then rejects as stale instead of silently deleting.
An in-progress ledger also refuses resume under a different driver revision,
preventing one campaign from mixing capture logic even when schema and fork are
unchanged.
Campaign ids are single safe path components; both CLIs reject rooted paths,
separators, `.`/`..`, and every symlink/junction/reparse redirect at a campaign
directory or direct-child file. `--fresh` never follows an owned-looking name
to a different target, even when that target remains inside the campaign.

Artifacts land under `<data-root>/<campaign-id>/`:
`run_<seed>_a20_ironclad.jsonl` (one per run), `campaign_progress.json`,
`campaign_manifest.json`, `campaign_heartbeat.json`,
`orchestrator_timeline.json`, `mts_launch<N>.log`. The **data root is never
committed** (design 7.3); default `D:\STS_BG_Mod\_oracle_data\campaigns`.

### Artifact schema (design 2.7)

Line 1 is a `header` (schema/driver version, game+mod-set, **fork-jar sha256**,
seed as **both** base-35 string and long, ascension, character, policy). Then one
`action` record per injected action — `{action_command, sim_action_bits (null,
B1.5 fills it), ready_for_command, available_commands, state_json}` — where
`state_json` is the game's dump **verbatim / un-pruned** (lossless: the translator
B1.5 enforces unknown-field-is-error). A final synthetic
`__terminal_observed__` action may preserve the post-claim state; it is
sequence-bearing but is not counted as an injected action or timing mark. The
file ends with exactly one `terminal` record (`outcome`, floor, act, actions).
Validate with:

```bash
python validate_artifacts.py --campaign D:/STS_BG_Mod/_oracle_data/campaigns/b14_accept
```

### Policies

| `--policy` | Choice rule |
|---|---|
| `random-legal` (default) | uniform over `expand_legal_actions` — the game's own `available_commands`, expanded to concrete arguments |
| `greedy` | the **same** expansion, ranked by `greedy_policy.score_action` |
| `script` | a fixed command list (one per line), `--script <file>` or `--script-dir <dir>` |
| `external` | the **same** expansion, sent to a policy binary (`--policy-cmd <exe-or-.py>`, optional `--policy-config <file>`) over STS-POLICY-IO v1; the reply must be one of the candidates |

**`--policy external` (TE.1 hook).** The campaign harness accepts a policy
binary plus config as the action source: each decision ships
`{seed, policy_seed, candidates, state}` as one JSON line to the child's
stdin, and the child answers `{"kind": "decision", "command": <candidate>}`.
Legality remains a property of the expansion — an out-of-candidate reply is a
protocol violation that stops the campaign durably
(`fatal_environment_drift`, kind `external_policy_error`), because a broken
binary would fail every remaining seed identically. Determinism is the
binary's obligation: the action sequence must be a pure function of
(policy_seed, seed). Binary and config are SHA-256-pinned into the campaign
identity, so a resume under a changed policy refuses exactly like a changed
fork jar. The reference binary is `survival_policy_cmd.py` — the survival
heuristic below behind the protocol, byte-identical to `--policy greedy`
under an empty config, with `{"constants": {...}}` overrides for cohort
tuning. Paths must contain no whitespace (the driver command line crosses the
space-split `command=` value in config.properties).

**`--policy greedy` (depth).** `random-legal` is unbiased and therefore shallow:
across 41 recent runs its median death floor was 3 and its deepest was 12, so the
states a deep capture needs — the treasure chest at floor 8+, the Act-1 boss at
16-17 — essentially never appear. `greedy` keeps the same legal-by-construction
expansion and only changes which candidate is taken:

- **combat** — mirrors the sim's fuzz scoring (`tools/fuzz/src/policy.cpp`
  `move_score`): lethal first, then `damage*4 + block` when nothing is swinging
  and `block*W + damage` while a monster's intent is an attack, focus fire on the
  lowest-HP live monster, `end` strictly last. `W` is 4 for a lone attacker and
  climbs by 2 per extra banner to a cap of 8 (**R3** below). Per-card numbers
  come from `cards_sidetable.json`; a card outside the S1 registry scores as
  cheap utility.
- **map** — non-combat > monster > elite, and the boss node when it is offered.
  This is deliberately **inverted** relative to the sim's elite-first fuzz
  weights: the fuzzer wants long varied fights, this wants floors survived.
- **screens** — claim relics/gold/potions, **never** a `SAPPHIRE_KEY` row (it
  retires the linked relic ungranted — `RewardItem.claimReward`,
  RewardItem.java:255-330 case 6), take card rewards while the deck is short of
  attacks (**R1** below), open treasure chests, rest when hurt / smith when
  healthy, and leave shops without buying.
- **potions** — held below `end` unless the room is a boss or elite, HP has
  fallen to 40 %, or the belt is already full (**R2** below).

**The three Act-1 boss rules (b1.5.0).** 69 captured runs produced 12 Act-1 boss
fights and zero boss-reward claims. Reading the six STS01221 fights record by
record corrected the diagnosis the g6 runbook recorded: **greedy never killed the
Slime Boss.** `SlimeBoss.damage` (SlimeBoss.java:173-182) queues SPLIT at
`currentHealth <= maxHealth / 2`, and `takeTurn` case 3 (:148-159) suicides the
boss and spawns `SpikeSlime_L` + `AcidSlime_L` **each at the boss's remaining
HP**. So a `Slime Boss 0/150 GONE` row in a dump is that SuicideAction, not a
kill, and the fight's effective HP is ~220, not 150. The captures show the
threshold crossings directly (ps7 `70/150` → two slimes at `70/70`; ps42 75 →
75/75; ps777 72 → 72/72). Greedy was ~35 % through the fight, with a 12-14 card
starter deck, having never opened a card row in sixteen floors.

| | Rule | Evidence it comes from |
|---|---|---|
| **R1** | open the card row and **take** while the deck holds `< 10` attacks and `< 20` cards; rank ATTACK first, then damage, `+6` for an ALL_ENEMY card | the never-take-cards default capped output at ~15 damage a turn against ~220 effective HP |
| **R2** | hold potions unless boss/elite room, HP ≤ 40 %, or the belt is full | two of six captures spent the last potion on floor 8 or 12 and met the boss with an empty two-slot belt |
| **R3** | under-attack block weight `min(4 + 2*(attackers-1), 8)` | the split puts two large slimes on the board at once, and that board is where every STS01221 run died |

R1's take-or-skip decision reads the **deck only**, never the cards on offer.
That is load-bearing: `skip` on a `CARD_REWARD` screen does **not** retire the
row it came from (b13_off20 `run_STS00004` seq 30-33 — skip, and `COMBAT_REWARD`
still lists `card`), so a policy whose two decisions could disagree would
alternate between the screens forever with a signature the driver's stuck
detector cannot see. Both decisions read the same `deck`, which cannot change
between the two screens, so they agree by construction and the row is always
retired by a take. `test_the_two_screens_can_never_disagree` sweeps the whole
census matrix for exactly that property.

Ties are broken with the run's own policy RNG (`Random(f"{policy_seed}:{seed}")`,
one draw per decision), so a greedy campaign is reproducible from
`(--policy-seed, seed)` exactly as the random-legal one is.

#### Three-act survival: act profiles and the boss-relic pick (b1.7.0, S2.42)

Every constant above was tuned against Act-1 evidence, because S1 runs ended at
the Act-1 boss. `greedy_policy.ACT_PROFILES` is a per-act overlay over the same
ALL-CAPS numeric constants, read through `_const(name, state)`:

| Constant | Act 1 | Act 2 | Act 3 | Why it moves |
|---|---|---|---|---|
| `MAP_ELITE` | 200 | 450 | 500 | Skipping every elite is how a run reaches the Act-1 boss and how it reaches the Act-3 boss with no relics. Acts 2/3 put the elite above `MAP_MONSTER` (400) but still below `MAP_NON_COMBAT` (600) — **and only while HP > `ELITE_APPETITE_HP_FRACTION` (60 %)**, so a hurt run keeps the Act-1 avoidance. |
| `DECK_ATTACK_TARGET` / `DECK_SIZE_CAP` | 10 / 20 | 12 / 28 | 14 / 35 | R1's gate. 10-in-20 is a floor-17 deck. Both move together so the gate keeps its shape. |
| `POTION_LOW_HP_FRACTION` | 0.40 | 0.50 | 0.60 | R2's floor. Deeper acts kill from a higher HP fraction. |
| `POTION_HIGH_STAKES_FROM_ACT` | — | — | 3 | A3: from Act 3 every *normal* combat room is high-stakes too. An Act-3 normal hits harder than an Act-1 elite, and R2's room-name gate was written when "normal room" meant Cultist. |

Three properties, each with a test:

- **Act 1 is byte-identical to `b1.6.0`.** `ACT_PROFILES` has no key `1`, and a
  dump with no `act` resolves to 1, so `_const` returns the module constant.
- **The cohort config still wins, in every act.** `apply_constants` records each
  configured name in `greedy_policy.CONFIG_PINNED`, and `_const` yields to it.
  Without that, a `{"constants": {"MAP_ELITE": …}}` cohort would be a silent
  no-op in Acts 2/3 — a cohort labelled with a policy it did not run.
- **R1's two-screens invariant survives**: `wants_card_reward` still reads the
  deck alone, and the act cannot change between the `COMBAT_REWARD` row and the
  `CARD_REWARD` screen that row opens.

**R4 — the boss-relic pick (`BOSS_REWARD`).** The boss chest offers three
BOSS-tier relics plus `skip`. S2-G2 item 2 needs **both** a take and a skip
witnessed per Act-2 boss, so this is a *cohort selection*, not a coin flip:
`BOSS_RELIC_SKIP_MODE` is an ordinary numeric constant, and the two configs
below are two SHA-pinned campaign identities over one binary.

| Config | Cohort |
|---|---|
| `policy_survival_act.json` | act-aware survival baseline, module defaults |
| `policy_bossrelic_take.json` | takes a boss relic (`BOSS_RELIC_SKIP_MODE: 0`) |
| `policy_bossrelic_skip.json` | skips every boss relic (`BOSS_RELIC_SKIP_MODE: 1`) |

Five BOSS relics are **never** taken, and the criterion is checkable rather than
a taste list: each one invalidates a rule `greedy_policy` itself owns, so taking
it would leave the policy scoring a game it is no longer playing — **Sozu** (R2
has no potions left to decide about), **Runic Dome** (no intents, so
`attacker_count` and therefore R3's block weight read zero forever), **Snecko
Eye** (randomised costs break the cheap-utility term and the side table's cost
column), **Pandora's Box** (R1's deck-attack gate counts a deck that no longer
exists), **Calling Bell** (a three-relic modal reward screen plus a curse; the
driver already carries a b1.5.3 suppression path for that screen at Neow). In
the take cohort `skip` sits *between* a takeable relic and a never-take one, so
a chest offering three of those five leaves without picking rather than
unseating a rule.

**Skip is a reversible screen close** (`boss_chest.hpp`: `relicSkipLogic` →
`chest.close()`, which does not clear the three offers, and
`ChoiceScreenUtils.getChestRoomChoices` re-advertises `open` the instant
`isOpen` goes false). A stateless policy that both opens chests and skips picks
therefore has a legal open/skip 2-cycle alternating between two screens —
invisible to the stuck detector, exactly the b5.2 GRID-cancel trap. The policy
does *not* close that hole; `CampaignDriver._boss_chest_reopen_filter` does, by
dropping the second and later `open` of one boss chest. That costs nothing: a
reopened chest offers the same three relics, and `proceed` is always advertised
in the room (`isConfirmButtonAvailable`, `CHEST` → true), so the candidate set
can never be emptied.

Regenerate the side table after any `registry/cards.yaml` change:

```bash
python tools/oracle_bridge/driver/gen_cards_sidetable.py
```

`test_oracle_campaign.py::CardSideTableTest` fails if the committed JSON drifts
from the registry.

### Scripted mode and its two gates

`--policy script` paces a fixed command list through the same lock-step gate.
Two checks stand between a stale script and a silently mutated run:

- `cmd_verb_ready` waits (bounded by `--max-settle`) for the game to advertise
  the command's **verb**; a verb that never arrives ends the run as
  `cmd_never_ready`.
- `cmd_args_ready` then checks the **arguments** against the very state the
  command is about to enter: `choose <i>` against `choice_list`, `play <i> [t]`
  against the hand and the monster list. Out of range ends the run as
  `cmd_arg_invalid` instead of firing a command the game answers with an
  InvalidCommand that the eight-error budget quietly absorbs. `choose <name>`
  (the protocol's string form) and any collection the dump does not carry are
  passed through untouched. Neither live policy is affected — both draw from
  `expand_legal_actions` and are in range by construction.

---

## B0.2 echo_driver

`echo_driver.py` is the minimal CommunicationMod child process: it logs every
game-state JSON the game emits and forwards commands the operator supplies. It is
the bring-up tool for the bridge; `campaign_driver.py` grew from here.

## Topology (PROTOCOL.md §1)

CommunicationMod launches one external child and wires **the child's own
stdio** to the game:

```
game --(state JSON, one object per line, to child stdin)--> echo_driver.py
game <--(commands, \n/NUL-delimited, from child stdout)---- echo_driver.py
```

Because the child's stdin/stdout belong to the game, the operator cannot type
commands into them. `echo_driver.py` therefore reads commands from a
**side-channel file** (`--commands`): every newline-appended line is forwarded
verbatim to the game. On startup it emits one `state` command (the safe
readiness signal — forces a dump, changes nothing) to unblock the game's
`readMessageBlocking` handshake (10 s timeout, PROTOCOL.md §1.3).

## Wiring (config.properties)

CommunicationMod reads its config **only at game startup** from
`%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties`. Set:

```properties
runAtGameStart=true
command=C:/Python39/python.exe D:/STS_BG_Mod/SpeedTheSpire/tools/oracle_bridge/driver/echo_driver.py --log D:/STS_BG_Mod/_oracle_data/run_capture.jsonl --commands D:/STS_BG_Mod/_oracle_data/commands.txt --latest D:/STS_BG_Mod/_oracle_data/latest_state.json
```

- **Forward slashes only.** Java `.properties` treats `\` as an escape; forward
  slashes avoid that and Java `ProcessBuilder` accepts them on Windows.
- **No spaces in any path.** The mod does `command.trim().split("\\s+")` into
  argv (CommunicationMod.java:271), so a space inside a path would split it.
- `runAtGameStart=true` makes the mod spawn the driver at launch. Otherwise use
  the mod-settings **"(Re)start external process"** button. Editing the file
  while the game is running has no effect until the next launch.

## Scriptable launch (design §1.2 paths)

For oracle work, use `orchestrator.py`; its launch is deliberately explicit:

```bat
cd /d D:\SteamLibrary\steamapps\common\SlayTheSpire
"D:\SteamLibrary\steamapps\common\SlayTheSpire\jre\bin\java.exe" -jar "D:\SteamLibrary\steamapps\workshop\content\646570\1605060445\ModTheSpire.jar" --skip-launcher --mods basemod,CommunicationMod-oracle
```

- ModTheSpire `1605060445`, BaseMod `1605833019`, CommunicationMod
  `2131373661` (Steam workshop, appid 646570).
- The stock workshop `CommunicationMod` and the fork share the same
  `SpireConfig("CommunicationMod", ...)` namespace. A GUI launch may therefore
  spawn the campaign driver while loading the stock jar, producing plausible
  artifacts with no oracle block. The GUI path is **not equivalent for oracle
  campaigns**; never rely on its remembered mod selection.
- Stock `CommunicationMod` remains usable only for the deliberately non-oracle
  B0.2 protocol bring-up. Never load stock and fork together.
- CommunicationMod's stderr lands in `communication_mod_errors.log` in the game
  directory — first place to look if the child never attaches.

## Driving a run

Append commands (one per line) to the `--commands` file; blank lines and `#`
comments are ignored. Command grammar: PROTOCOL.md §2.

```bash
CMD=D:/STS_BG_Mod/_oracle_data/commands.txt
echo 'start ironclad 20 <SEED>'  >> "$CMD"   # seeded A20 Ironclad run
echo 'state'                     >> "$CMD"   # force a fresh dump
echo 'choose 0'                  >> "$CMD"   # pick the first listed choice
echo '__quit__'                  >> "$CMD"   # detach the driver cleanly
```

Send state-changing commands only while the latest state has
`ready_for_command: true` (PROTOCOL.md §1.5). The seed is the game's **base-35
display string**, not a raw long (PROTOCOL.md §2.1).

## Artifacts and verification

- Capture (`--log`) and the latest-state snapshot (`--latest`) live under the
  bridge **data root** `D:\STS_BG_Mod\_oracle_data` (design §7.3), outside the
  repo — **never committed** (raw JSONL traces are campaign artifacts).
- `python echo_driver.py --verify <log.jsonl>` re-parses a capture and reports
  that every recorded state line is valid JSON (the B0.2 acceptance check). Runs
  standalone, no game.
- The driver logs a `meta` throughput record (`recv`, `sent`, `recv_per_s`) on
  clean exit; that `recv_per_s` is the stock-game baseline actions/sec.
