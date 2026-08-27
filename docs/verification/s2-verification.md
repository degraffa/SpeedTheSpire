# S2 verification report — the S2-G2 bar, item by item

Written by S2.46 (2026-08-27), the last row before gate **S2-G2**. Denominator:
[../s2-design.md](../s2-design.md) §6, S2-G2 items 1–7. Ledger row:
[../s2-tasks.md](../s2-tasks.md) S2.46.

This is the B5.4 pattern — **evidence accounting, not a gate inference.** Every
item below is answered by an instrument that already exists and regenerates,
and this file's job is to say, for each of the seven, *which* instrument
answers it, what the instrument said, and what it did **not** say. Where an
item is short of the letter of its own wording, the shortfall is stated
literally in this file rather than argued away; §9 collects the five places
that happened, none of which is an unmet bar.

**Numbers that regenerate are cited, not restated.** Items 1–4 are the S2.43
dashboard's output; that report reopens and hashes every consumed capture and
rewrites byte-for-byte over unchanged inputs, so copying its counts here would
create a second, staler copy of a number with one owner. What is copied is the
verdict line and the date it was generated on. The one set of numbers this
report owns outright is item 5's, because S2.46 is the task that produced it.

---

## Verdicts at a glance

| Item | Bar | Verdict | Instrument (the owner of the number) |
|---|---|---|---|
| 1 | Breadth: ≥ 2,000 distinct mixed-policy A20 attempts, zero untriaged, zero open | **MET** | [s243-dashboard.md](s243-dashboard.md) |
| 2 | Act-2 depth: per-BOSS-row zero-diff boss-reward claim + boss-relic pick + act-2→3 transition, take and skip both witnessed | **MET** | [s243-dashboard.md](s243-dashboard.md) |
| 3 | Act-3 depth: every Act-3 BOSS row killed zero-diff, ≥ 3 completed double-boss runs over ≥ 2 first-boss identities | **MET** | [s243-dashboard.md](s243-dashboard.md) |
| 4 | Event depth: every Act-2/3 event row sighted zero-diff or carrying an exact per-row disposition | **MET** | [s243-dashboard.md](s243-dashboard.md) |
| 5 | Defect-family audit: the g7 proactive manifest extended with every S2-discovered family, each with multiple named passing regressions, audit re-run green | **MET** | [g7_proactive_audit.md](g7_proactive_audit.md) — **21 families, 101 regressions, PASS** |
| 6 | Tier-4 additions: pre-registered act-2/3 distributional family, Holm-corrected, negative controls rejected | **MET** | `tools/dist_check/dist_check_s2` ([README](../../tools/dist_check/README.md)) — `RESULT PASS` |
| 7 | Throughput floors re-baselined honestly | **MET, with the item's premise falsified** | [s245-throughput.md](s245-throughput.md) |

**No item is UNMET, and no item is pending.** The four dashboard items were
UNMET on 2026-08-27 morning and are MET on the same instrument that printed
those shortfalls, with only its inputs changed — which is the property that
makes them worth citing at all.

---

## 1. Breadth — MET

> ≥ 2,000 distinct full-run A20 Ironclad oracle attempts under mixed policies
> (random-legal + survival/scripted external policies), zero untriaged
> findings, zero open dispositions.

**Evidence:** [s243-dashboard.md](s243-dashboard.md) item 1, generated
2026-08-27 from the S2.43 breadth cohorts under three SHA-pinned policy
identities. Verdict as generated: **MET** — distinct seeds and full-run
attempts both at or above the bar, zero untriaged, zero open, every
non-clean run carrying an exact disposition in `s243_dispositions.json`.

**Two things worth reading in that report rather than here.** First, the
instrument found a shortfall this task's prose had not: the original wave held
1,998 full-run attempts, not 2,000, because two seeds ended `noop_wedge` — a
non-gameplay terminal is not a full-run attempt. It was closed by a top-up
cohort folded into the breadth role, not excused. Second, the "zero capture-race
records" reading belongs to the settle-lag fork pin specifically; the dashboard
prints the count per named recapture instead of asserting a corpus-wide zero.

**What breadth did not do, and it matters to items 2 and 3:** 0 Act-2 boss
fights across the whole 2,000-seed wave. That measured zero is the §8 escalation
number that opened S2.V2, and it is why the depth bars rest on a different
cohort family entirely.

## 2. Act-2 depth — MET

> ≥ 1 zero-diff boss-reward claim **and boss-chest boss-relic pick** for every
> Act-2 registry BOSS row (both a take and at least one skip witnessed across
> the cohort), each followed by a zero-diff act-2→3 transition.

**Evidence:** [s243-dashboard.md](s243-dashboard.md) item 2. All three Act-2
BOSS rows carry a claim, a chest, a pick and a transition; take is witnessed for
all three rows and skip across the cohort, which is what the bar asks (per-row
claim+pick+transition, cohort-wide take AND skip).

**The pick is a *zero-diff* assertion because of S2.47**, not by convention: the
three boss-chest offers and their reveal bits live in `RunState`, the translator
emits them from a live `BOSS_REWARD` dump, and `diff_run_states` compares the
group member by member (`RunDifferBossChest.EveryMemberNamedSeparately`). Before
that storage landed, the field was dispositioned `I` and never diffed at all,
and this item was structurally unachievable.

**The take/skip axis is read from the SHA-pinned policy config each capture's
own campaign names, never from a cohort directory name.** The same rule is now
enforced in the CI corpus builder (§8).

## 3. Act-3 depth — MET

> every Act-3 registry BOSS row witnessed killed zero-diff, and ≥ 3 completed
> A20 **double-boss** runs (both bosses in one run, gold settlement zero-diff,
> covering ≥ 2 distinct first-boss identities).

**Evidence:** [s243-dashboard.md](s243-dashboard.md) item 3. All three Act-3
BOSS rows witnessed killed; three completed double-boss victories over two
distinct first-boss identities; two further runs that reached the second boss
and lost, reported and deliberately **not** counted.

**Two instrument notes the dashboard states and this report will not restate as
if they were free.** Double-boss detection is artifact-side — the campaign
report's `boss_kill_acts` is a *set* of act numbers and cannot express two kills
in one act — and Act-3 kills are read the same way, because the captures show
the Act-2 boss chest's trailing MAP record already carrying `act: 3`, so
`3 in boss_kill_acts` is true of every run that merely crossed. Gold settlement
is not a separate column because it is not a separate assertion: each counted
run replays clean to its run terminal and `RunState.gold` is compared at every
one of those records.

**Simulator-selected seed cohorts are the sanctioned G7 mechanism** and were
used: S2.V2's sim-consulting scripted driver pre-scans (seed, policy,
policy-seed) triples whose scripted line reaches the target, and the oracle then
confirms the full run zero-diff. That is the escalation design §6's driver-risk
paragraph named in advance, taken because the alternative was measured empty.

## 4. Event depth — MET with dispositions

> every Act-2/3 event row sighted in ≥ 1 zero-diff oracle run *or* carrying an
> explicit per-row disposition — no wildcard dispositions.

**Evidence:** [s243-dashboard.md](s243-dashboard.md) item 4 and
`s243-event-coverage.csv`. Of the 40 Act-2/3 registry event rows, most are
sighted in act 2 or 3 inside clean runs and the remainder each carry an exact
per-row `reachability-argument`; **0 OWED, 0 wildcards.** An Act-1 draw of a
cross-act row is reported in a separate any-act column and deliberately does
**not** satisfy the bar.

**Two of the dispositioned rows are stronger than "rare", and one is a standing
deviation this report does not want mistaken for a coverage gap.**
`NoteForYourself` is structurally absent from the A20 special list, and
`SecretPortal` is pinned unreachable on **both** sides — trap 5 in the engine
and `OraclePlaytimePinPatch` in the fork, so the gate and the capture's own
`oracle.playtime` anchor are one function and cannot disagree. The owner
question S2.43 left open (whether the *simulator* should model a clock at all,
given that omitting SecretPortal shifts a draw INDEX rather than merely hiding
an event) is unresolved and is listed in §9.

## 5. Defect-family audit — MET (this report's own number)

> the g7 proactive manifest extended with every S2-campaign-discovered defect
> family, each retaining multiple named passing regressions; the executable
> audit re-run green.

**Evidence:** [g7_proactive_audit.md](g7_proactive_audit.md) and
`g7_proactive_audit.json`, regenerated by S2.46 from a full debug ctest run.

- Families: **21** — the six G7-era families, unchanged, plus **15** S2 ones.
- Named regressions: **101**, every one registered in CTest and passing.
- Verdict: **PASS** — every family passing, none partial.

The fifteen new families and what each one is the memory of:

| Family | What the campaigns found |
|---|---|
| `s2-replay-copy-shared-instance-state` | Per-instance card state the Java keys by **uuid** across every pile — a double-tapped Rampage under-damaged because the replay copy snapshotted `misc` instead of sharing it. |
| `s2-spawn-prepass-group-visibility` | The run layer's spawn path published `monster_count` slot by slot, so a slot-0 Centurion decided its opener against an effectively empty group. |
| `s2-combat-terminal-adjudication` | Which terminal a combat ends on, and what still resolves behind the killing hit: a HEAL resolved past the player's death and un-killed him. |
| `s2-card-cost-state-lifecycle` | `resetAttributes` on every Soul-routed pile move, and an in-combat upgrade that must not clobber live cost or per-instance flag state. |
| `s2-reward-offer-preview` | A held Egg upgrades the reward **OFFER** at assembly; the run state was already right, which is why nothing caught it. |
| `s2-liveness-senses` | Three distinct senses of "dead" (`isDying`, dead-or-escaped, basically-dead) that one predicate cannot express. |
| `s2-double-boss-handoff` | The A20 act-3 handoff: the `COMPLETE` screen seam, and the stale `bossKey` mirror it was hiding. |
| `s2-unmodelled-clock-inputs` | An unmodelled input that shortens a draw list and so changes which event **every** Act-3 `?` room returns. |
| `s2-cross-act-capture-derivation` | A per-record derivation exact within one act is not exact across a run — the FIRED and `boss_ids` folds. |
| `s2-live-run-state-projection` | Run-level state that combat mutates mid-action: the live purse, the fairy belt, Ritual Dagger's master-deck write. |
| `s2-deferred-hook-boundaries` | Effects the engine resolved inline that the game queues, or queues to the other end of the queue. |
| `s2-positional-monster-identity` | A missing construction-time x-offset rotated the monster array and sent every positional replay target to the wrong minion. |
| `s2-screen-arm-shape-and-ownership` | Screens whose click count, ownership or hidden RNG work differed from the game's — each stopped a replay somewhere that looked like a mapping bug. |
| `s2-emitter-index-space-identity` | An emitter index space one permutation away from the simulator's is indistinguishable from an engine divergence. |
| `s2-act-two-three-replay-seams` | Every Act-2 crosser in the breadth wave diverged at the same replay seam; its regressions include the new three-act corpus replay (§8). |

**Two rules kept while extending it.** A family names its regressions by CTest
name and the checker fails if any is unregistered or non-passing — so a renamed
or deleted test breaks the audit rather than silently shrinking it. And every
family carries **at least two** names, including at least one negative control
wherever the fix had one, because a family pinned by a single assertion is a
family that a plausible-looking refactor can delete in one edit.

**What the audit is not.** It is an audit of *regressions*, not of coverage: it
proves each historical defect family still has teeth, and says nothing about
families nobody has found yet. Design §6 chose it over a raw action quota for
exactly that reason, and the honest statement of its power is the list above.

## 6. Tier-4 additions — MET

> pre-registered distributional hypotheses for act-2/3 encounter pools (incl.
> first-strong exclusion effects), per-act `cardUpgradedChance`, boss shuffle +
> double-boss conditioning, and the §2.3 one-time-pool cross-act depletion —
> Holm-corrected family, same α discipline as B5.3.

**Evidence:** S2.44's registered family, `tools/dist_check/dist_check_s2`, with
its registration (13 hypotheses, family-wise α 0.01, Holm-Bonferroni, minimum
10,000 seeds, acceptance 20,000) committed in
[tools/dist_check/README.md](../../tools/dist_check/README.md). Verdict at both
scales: **`RESULT PASS`**, with all four deliberately-wrong negative-control
samplers two-stage rejected.

Three properties of that family this report cites because they are what make
its PASS mean something:

- **It is a new family alongside B5.3's sixteen, not an extension of them.**
  Reopening the closed set would retroactively move every threshold it was
  judged against.
- **A flag was triaged, not re-seeded away.** `s2.encounter.act3_weak_pair` came
  in inside its Holm threshold at the acceptance scale — the α tail a 13-row
  family at α 0.01 produces about once in a hundred runs — and the registered
  seed base, sample size, α and expectations were left exactly as written. The
  resulting protocol change (**replicate before flagging**, one confirmatory
  replicate at the same per-row threshold on a derived disjoint block) is a
  permanent rule with its own tests, not a one-off.
- **Power is asserted rather than assumed.** The controls must clear the
  family's strictest threshold in both stages on every campaign run; a survivor
  fails the run.

**S2.46 did not re-run this family**; the numbers above are S2.44's acceptance
run, which is the registered evidence. Re-running it is
`tools/wsl_run.sh --script tools/dist_check/s2_run.sh release --seeds 20000`.

## 7. Throughput — MET, with the item's own premise falsified

> three-act runs are longer, so the S1 whole-machine runs/sec floor is not
> comparable; S2-G2 re-runs the B5.5 methodology, requires the *per-step* and
> *per-combat* floors to hold unchanged, and records a new three-act whole-run
> number with methodology as the S3 baseline.

**Evidence:** [s245-throughput.md](s245-throughput.md).

- **The floors half is met outright.** All three frozen floors hold, checked
  against the *worst* reading of five invocations rather than the median, by
  ≥ ×166. Nothing in S2 — S2.48's `sync_live_gold` on every in-combat advance
  and S2.49's per-`DAMAGE`-item guard included — moved any floor.
- **The baseline half is met by a documented substitution, and that is the
  literal shortfall.** The item expects the whole-run rate to fall
  proportionally to run length. Measured, it did not: run length is unchanged
  (47.04 actions/run against B5.5's 46.93) because **no run in the benchmark
  corpus leaves Act 1** under the weight-free E0 policy — the harness now prints
  `act2_runs=0 act3_runs=0` over ~35,000 measured runs, and `terminal_act_sum`
  equals `runs_counted` exactly, which is the positive control. A single
  runs/sec therefore cannot carry the meaning "three-act runs per second", so
  the S3 baseline is recorded as a **pair** — a corpus-conditional runs/sec and
  a length-independent run-steps/sec — with the projection rule spelled out.
- **What is unattributed, and is deliberately left so.** Combat stepping is
  ×0.712 of B5.5 and the 10,000-state batch ×0.498; both are per-step cost
  ratios, not workload changes, and the leading candidate for the batch is
  `sizeof(CombatState)` 3,896 → 8,088 B against a 96 MiB L3. S2.45 is an
  absolute one-build floor task and did not A/B; the cheap, named follow-up
  (`d57e077` against `646bd18` on `bench_advance_mask` + `bench_throughput`) is
  carried as a deferred-obligations row, not as a claim here.

**A genuine three-act rate is now schedulable and still unmeasured.** S2.V2's
sim-consulting driver does produce lines that win two boss fights — items 2 and
3 rest on them — so the missing ingredient is a benchmark corpus built from such
a policy, not the absence of one. Quoting the corpus-conditional runs/sec as a
three-act number would still be wrong today.

---

## 8. What S2.46 added: the three-act CI corpus

Items 1–4 rest on ~4 GB of captures that are **uncommitted by design**
(stage-B design §7.3); what pins them is the dashboard's per-artifact SHA-256
manifest. That is the right trade for evidence, and it leaves a gap: nothing in
the committed test suite replayed a real capture past the Act-1 boss. The
Act-1 corpus (`act1_a20_50.tar.gz`, 50 runs) is the B5.4 answer for Act 1;
S2.46 adds its Acts 1–3 sibling.

`tests/golden/oracle_corpus/three_act_a20_5.tar.gz` — **5 curated whole-run
captures, 977 KB compressed**, archive SHA-256
`9572f3012b8347ad334c30dc2f1035dff210b69948486a79595c75be5efe6826`, manifest
`three_act_a20_5.manifest.json`, format `STS-ORACLE-CI-CORPUS v2`:

| Cohort / seed | Role | Why it is in the corpus |
|---|---|---|
| `s2v2_dbv_103509a` / STS103509 | double-boss victory | Donu and Deca → Time Eater, a completed A20 double-boss run carrying the whole item-3 shape: Act-2 boss chest, act-2→3 transition, both Act-3 fights, the `COMPLETE` handoff, the victory terminal. |
| `s2v2_db47_b` / STS128113 | double-boss victory | The second first-boss identity: Time Eater → Donu and Deca. |
| `s2v2_awk_105835` / STS105835 | Act-3 boss kill | The Awakened One killed zero-diff, then a death to the second boss — the **losing** double-boss shape, which no victory can exercise. |
| `s2v2_mb_102529` / STS102529 | directed event | The Mind Bloom Act-1-boss re-fight (Guardian) the S2.33 deferred row asked for, played to its fixed gold reward claim. |
| `s2v2_skip_b` / STS111111 | boss-relic skip | The skip policy axis into Act 3; every other pick runs the take config. |

Four properties of the v2 format, each forced by the evidence rather than
chosen:

1. **Provenance is per entry.** Two fork pins are represented on purpose (the
   escape-window settle-lag hold and the SecretPortal playtime pin), and one
   aggregate pin would hide the fact the cohorts exist to record — the same
   argument the S2.43 dashboard makes for its cohort table.
2. **A translated trace is optional, with a stated reason when absent.** Two
   entries have none: one campaign's translation aborted on an Act-4
   `Spire Heart` event id (out of S2 scope), and one wave was stopped mid-seed
   by the driver before any postprocess ran. Both captures replay clean, which
   is what the corpus asserts; the smoke refuses a missing trace that carries no
   reason.
3. **The stored classification is not trusted at all.** Two picks were
   classified non-clean by the postprocess that ran on capture day and are clean
   on the landed engine — they are the captures whose divergences drove the
   fixes, which is precisely why they belong in a regression corpus. So the
   builder **re-replays every pick** with a real `replay_run_diff` and refuses
   anything that is not zero-diff to its run terminal with zero capture-race
   records.
4. **The corpus asserts its own contract in CI.** The smoke fails if any entry
   is not an act-3 capture, if no entry is a *completed* double-boss run, or if
   either boss-relic axis is missing. A curated corpus that quietly lost its
   double-boss run would otherwise still replay green and no longer be the
   evidence this report cites.

CI: `OracleCorpusReplay.ThreeActCorpusReplaysZeroDiff` and
`OracleCorpusReplay.ThreeActInjectedSyntheticDivergenceFailsLoud` run in every
preset beside the Act-1 pair, ~13 s under `debug`. The Act-1 corpus and its
manifest are **byte-unchanged**; the new archive is an addition, and the
builder's pinned pick list is kept independent of the moving dashboard defaults
for the same reason the B5.4 campaign list is
([tools/verify_report/README.md](../../tools/verify_report/README.md)).

---

## 9. Standing limits — what this report does not claim

Five, stated plainly so the gate is read against them rather than around them.

1. **Act 4 is out of scope and shows up in the evidence.** A victorious run's
   trailing Spire Heart records are a named skip on the replay side and a
   translator refusal on the translation side; one corpus entry carries no
   trace for exactly that reason. Nothing in S2-G2 covers the Ending.
2. **`--replay` compares `RunState`, not in-combat card cost state.** That blind
   spot is why a whole cost-state family reached the depth wave undetected
   before being closed. No acceptance surface compares in-combat card COSTS
   against a capture today; the S2.43 row flags it as worth its own row.
3. **Five archived S2.V2 cohort lines are artifacts of a superseded engine.**
   The pile-move/upgrade fix moved them (four no longer qualify for the filter
   that scheduled them). Re-capturing or retiring them is a campaign decision;
   they are not evidence for any bar above.
4. **The SecretPortal owner question is open.** The 2026-08-10 ratification of
   trap 5 rested on "the event is avoidable and essentially never optimal",
   which is true of the event and not of the draw-index shift its omission
   causes. Both sides are pinned to the same value today, so no capture can
   disagree with the sim; whether the simulator should model a clock is
   undecided.
5. **The throughput attribution is unattributed on purpose** (§7), and the
   three-act whole-run rate does not exist to be measured with any weight-free
   policy in this repository.

---

## 10. Reproduce

```bash
# Items 1-4 -- the dashboard, no arguments; byte-identical over unchanged inputs
C:\Python39\python.exe tools\verify_report\generate_s2_report.py

# Item 5 -- the executable defect-family audit (writes both files below)
tools/verify_report/check_g7_proactive_coverage.sh debug
tools/verify_report/check_g7_proactive_coverage.sh debug --use-last-log   # fast path

# Item 6 -- the S2 tier-4 family at its acceptance scale
tools/wsl_run.sh --script tools/dist_check/s2_run.sh release --seeds 20000

# Item 7 -- the throughput harness (release, benchmarks on)
tools/wsl_run.sh --script benchmarks/run_throughput.sh

# Section 8 -- the corpora, and the CI assertion over them
tools/wsl_run.sh debug asan release        # OracleCorpusReplay.* in every preset
python3 tools/verify_report/build_ci_corpus.py --three-act \
    --artifact-root /mnt/d/STS_BG_Mod/_oracle_data/campaigns \
    --replay-bin build/release/tools/oracle_bridge/replay/replay_run_diff \
    --archive tests/golden/oracle_corpus/three_act_a20_5.tar.gz \
    --manifest tests/golden/oracle_corpus/three_act_a20_5.manifest.json
```
