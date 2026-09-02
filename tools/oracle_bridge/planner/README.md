# `seed_scan` — the capture planner

The oracle capture campaigns walk `STS%05d` sequentially, so what a campaign
sees is whatever those seeds happen to contain. A rare target — one specific
shrine, a treasure floor, a run that reaches the boss — is then a lottery, and
nobody knows whether the list contains it until the game has been driven for
hours.

`seed_scan` answers that question in the simulator first. Event selection is a
pure function of `RunState` (`generate_event`, `src/engine/event_framework.cpp`),
so a full A20 run costs microseconds and a five-thousand-seed sweep costs
seconds. Point the capture at seeds already known to contain the target.

## Build and run

Built with the tests (it links `fuzz_core`), so any preset produces it:

```bash
tools/wsl_run.sh release
build/release/tools/oracle_bridge/planner/seed_scan --help
```

Use the **release** preset for real scans; debug and asan are 1–2 orders of
magnitude slower and the tool is throughput-bound.

## The two output files, and why there are two

```bash
seed_scan --seeds STS00100-STS05099 \
          --policies random,greedy_damage --policy-seeds 0,1 \
          --out scan.tsv \
          --need-event "Match and Keep!" --min-hit-count 2 \
          --seed-list seeds_match_and_keep.txt
```

* `--out` is the **evidence**: one row per (seed, policy, policy_seed), hit or
  not, with the decoded event flags, the max floor, whether a treasure room was
  entered and whether the boss was reached. A later question can be answered by
  re-filtering this file instead of rescanning.
* `--seed-list` is the **artifact a campaign consumes**: nothing but seed
  strings, with a `#` header recording the scan that produced it. A campaign
  driver reading it never has to parse a verdict.
* `--cohort-list` (S2.42) is the **depth** artifact — see below. It holds
  (seed, policy, policy_seed) **triples**, not seeds.

Artifacts belong under the non-repo data root (`docs/stage-b-design.md` §7.3),
not in the tree.

## Per-act depth (S2.42)

Every design §6 S2-G2 depth bar is stated *per act*, and several are stated as
**kills**. Standing in a boss room and walking out of it are different facts,
and until S2.42 the scan could only see the first one.

```bash
seed_scan --seeds STS00100-STS05099 \
          --policies random,greedy_damage,greedy_block --policy-seeds 0,1 \
          --out scan.tsv --summary scan.txt \
          --need-boss-kill-act 1 --cohort-list cohort_act1_kill.tsv
```

| Flag | Means |
|---|---|
| `--min-act <n>` | highest act reached ≥ n |
| `--need-boss-act <n>` | the act-*n* **boss room** was entered |
| `--need-boss-kill-act <n>` | the act-*n* boss was **killed** |
| `--need-victory` | the run was won (`run_is_victory`) |
| `--need-boss-id <encounter game id>` | any-of over the run's per-act boss identities — how a cohort covers every registry BOSS row, or ≥ 2 distinct identities |

**The kill probe is exact, not inferred.** The post-boss chest is entered *only*
through the boss reward's `proceed` (`ProceedButton.goToTreasureRoom`, a full
room transition), and Acts 1 and 2 both end in one — while Act 3's boss opens no
reward screen and no chest at all. So `RunPhase::BOSS_TREASURE` observed at act
*N* **is** the act-*N* kill for N ∈ {1,2}, and act 3's kill is
`run_is_victory()`. `--need-boss-kill-act 3` and `--need-victory` are therefore
one clause. The live capture driver runs the same pair of probes against the
protocol dump (`campaign_driver.py _observe_reach`), deliberately, so the two
instruments agree.

**Column order is append-only.** The five new TSV columns (`act`,
`boss_reached_acts`, `boss_killed_acts`, `victory`, `boss_ids`) go **after**
`fail_kind`, and `boss` keeps its old meaning (`boss_reached_acts != 0`), so a
script that has been doing `cut -f10` since S1 still selects the boss column.

**`--max-actions` defaults to 12000**, raised from the Act-1-era 4000. A
three-act A20 run is roughly three times the actions, and a truncated deep run
ends as `ACTION_CAP` — which in a depth scan reads as a *policy* failure while
actually being the tool's own truncation. The summary prints the `ACTION_CAP`
count next to the reach numbers so the artifact is visible rather than inferred.

## The sim-consulting policies and the scripted line (S2.V2)

S2.43's breadth wave measured the b1.7.0 driver family at **0 Act-2 boss
fights in 2,000 live A20 attempts**, the exact trigger for design §6's
sanctioned escalation. S2.V2 adds the two pieces the escalation names:

**`sim_search` / `sim_search_skip`** (`--policies`) are the sim-consulting
policies (`tools/fuzz/src/policy_search.cpp`): combat decisions run a bounded
1-ply search over engine snapshots (2-ply through a boss fight's opening),
map-node and event-option decisions are scored by a one-floor rollout, and the
remaining run-layer decisions are the b1.7.0 survival heuristics ported from
`greedy_policy.py` (R1/R2/R4 + `ACT_PROFILES`). Deterministic and weight-free:
integer arithmetic only, every bound a constant, the single stochastic input
the shared one-draw tie-break from `policy_seed`. The `_skip` variant differs
in exactly one rule — R4's boss-relic pick answers SKIP — mirroring the
`policy_bossrelic_take/skip.json` cohort identities. The search preview is
deliberately omniscient (it advances copies of the real controller); the
product is a scripted LINE for a capture to replay, not an agent under the
information contract.

**`--script-dir <dir>`** writes an **STS-SCRIPT v1** file for every row that
hits the filter — the scripted action line the live script follower
(`tools/oracle_bridge/driver/script_policy_cmd.py`) replays over STS-POLICY-IO
v1. Emission re-drives the row's pass-A trajectory from `run_begin`, decodes
every action against the state it was taken in, and refuses to write a file
whose replay does not land on the row's `final_hash`. Under
`--verify-determinism` the trajectory is re-derived and compared too.

### STS-SCRIPT v1 (normative schema)

JSON Lines. Line 1 is the header:

```json
{"format":"STS-SCRIPT v1","seed":"STS00001","seed_int":1790050543751,
 "ascension":20,"policy":"sim_search","policy_seed":0,"engine_schema":8,
 "steps":186,"final_hash":"e8dfee586097dab7","end_reason":"run_over",
 "victory":false,"max_act":2,"max_floor":19}
```

then one object per decision: `{"i":N,"floor":N,"act":N,"phase":"...",
"k":"<kind>", ...identity...}`. The identity vocabulary is what a live driver
can match against a CommunicationMod dump — **stable identity, never a bare
sim index** (an index mismatch that names the same card is presentation; a
different card at the same index is the desync the follower must stop on):

| `k` | identity fields | live join |
|---|---|---|
| `play` | `card` (game id), `up` (upgrade count), `ord` (n-th same-identity copy in hand order), `t` (monster index, −1 untargeted), `tmon` (monster game id, advisory) | n-th matching card in `combat_state.hand` → `play <slot> [t]` |
| `end` | — | `end` |
| `potion` / `potion_discard` | `slot`, `potion` (game id), `t` | slot must hold that potion → `potion use/discard <slot> [t]` |
| `map` / `map_boss` | `x` (column), `sym` (node symbol) | node with that x/symbol in `next_nodes` → `choose i`; boss → `choose 0` |
| `claim` | `rtype` (reward_type), `id` (relic/potion game id when carried), `ord` (n-th row of same type) | n-th matching `rewards[]` row → `choose i` |
| `take_card` / `skip_card` / `sing` | `card`+`up`+`ord` / — / — | match in `screen_state.cards` → `choose i`; `skip`; the bowl row |
| `rest` | `opt` (`rest`/`smith`/`lift`/`toke`/`dig`/`recall`) | choice_list entry of that name |
| `grid` / `grid_cancel` | `ctx` + `card`+`up`+`ord` | match in the grid's card list → `choose i`; cancel alias |
| `grid` with `ctx` = `library` | `event` (game id), `card`+`up`+`ord`, `opt` (sim board slot, advisory) | The Library's twenty-card read: the run layer models it as event options, but the game hosts it on `GridCardSelectScreen` (TheLibrary.java:91) so the live dump is `screen_type: GRID` — identity join in `screen_state.cards`, same as any other grid |
| `choose_card` | `src` (pile) + `card`+`up`+`ord`+`index`, or `skip` = 1 with an empty `card` | combat choice screens; identity join like `grid`. **`skip`: 1 is the typed discovery screen's Skip button** (`src` = `generated`; `ActionMask::can_skip_choice`) — the flag is the decision and the empty `card` is its consequence, so the follower reads `skip` before any identity join and answers the live `skip`/`cancel` alias |
| `confirm` | — | the hand-select confirm (`proceed`) |
| `event` / `neow` | `event` (game id), `opt` (full-list ordinal), `index` (**enabled-only** index — the `choose` command's own space, command_map.hpp's two-index-space note inverted; on Match and Keep's twelve-card board that space is ordered by SCREEN POSITION, not by board slot — see `script.cpp`'s `event_live_choose_index`) | `choose <index>` |
| `open_chest` / `boss_open` | — | the `open` choice |
| `boss_pick` / `boss_skip` | `relic` (game id) / — | match in `screen_state.relics` → `choose i`; `skip` |
| `proceed` | `ctx` (advisory) | `proceed` |
| `shop` | `index` (raw) | never emitted by the sim-consulting policies (they buy nothing); decode-completeness only |

**A cancelling run of hand-select toggles is not emitted.** The optional
(zero-to-N) hand-select — Elixir's exhaust screen, Purity, upgraded
Forethought — is the one screen where `choose` TOGGLES: picking an
already-picked card takes it back out. A searching policy oscillates there
(`policy_search.cpp`'s CONFIRM tie-bias comment names the residual), and the
deselect is **inexpressible live** — `HandCardSelectScreen` moves a picked card
out of `hand`, so CommunicationMod's list no longer contains it and the
follower stops (witness: campaign `s2v3_wave1_STS207337_ps255`, floor 16 turn
1, two identical `choose_card AscendersBane` steps). The emitter therefore
elides a run of toggles that returns the controller to a state it already held
inside that screen and emits the **net selection in pick order**. The ACTIONS
are still replayed — only the lines go — so `final_hash` is unchanged and the
verification stays honest; and the elision is decided by the engine's own
content hash, not by a "same card twice" rule, because a deselect returns the
card to the END of the unselected run, so a select of any earlier slot plus its
deselect is a real hand REORDER the game performs too. Consequences for the
schema: `steps` counts EMITTED lines (the follower's header check still
holds), and `i` stays the action's index in the trajectory, so a gap in `i` is
where a cancelling run was dropped. The follower does **not** mirror the rule:
a same-card re-`choose` against a screen that still lists the card is a genuine
desync, not a deselect, and must stop.

**Event pages whose options are CARDS.** The engine has exactly two
(`EventDialogState::board`): Match and Keep's twelve-card board and The
Library's twenty-card read. Only Match and Keep stays an `event` step — its
live screen really is the event page and its cards are face DOWN, so there is
no identity to emit and the `index` permutation above is the whole answer. The
Library's board is face UP on a real `GRID` screen (TheLibrary.java:91), so it
emits card identity instead. Every other event card screen is a master-deck
grid (`EventGridKind`), already emitted as `grid` + `card`/`up`/`ord`.

The follower consumes steps strictly in order; its only four glue commands
(a confirmation-only screen; the GRID pick-then-confirm seam; a collapsed
one-click dialog whose sole candidate is a single `choose` — Neow's opening
`talk` and the vestigial-click event class the engine collapses; and the
`COMPLETE` screen) never advance the script cursor — "sole" reads the
PROGRESS candidates, i.e. every always-available BELT command is excluded —
`potion discard N` and `potion use N [t]` alike, since CommunicationMod
advertises the `potion` verb on any screen whose belt holds a
discardable/usable potion and neither form gates that screen — while a
scripted `potion`/`potion_discard` still matches first; one SKIP rule covers
the mirror seam (a scripted `proceed`/`confirm` whose live state offers
neither alias — the game auto-advanced where the sim stepped — is consumed
without emitting, and a real desync still stops on the following step). The
first three glue rules are evaluated only **after** the next step failed to
match, so a scripted decision is never glued past; the `COMPLETE` rule is
the one evaluated **before** it, and has to be. `COMPLETE` is
ChoiceScreenUtils' label for a room at RoomPhase.COMPLETE with no screen
over it, which in S2's scope only the two Act-3 boss rooms produce; the
engine has already made both crossings when the capture shows the button
(the first Act-3 kill runs `goToDoubleBoss` inline, the second press is the
run terminal), so **the emitter writes no step there, ever**. Its live
command is `proceed` — exactly the alias a scripted `confirm`/`proceed`
answers — so under match-first the crossing ate a step belonging to the
next floor (s2v3_wave2 STS205404 ps20: Gambling Chip's floor-51 hand-select
`confirm` was consumed by the floor-50 handoff, and the follower then
stopped on the `end` behind it). **Any other mismatch stops the run as
divergent** — a desync is capture evidence for Stage-B triage, never
something to route around. `script_policy_cmd.py`'s module header carries the
stop contract; its unit tests round-trip the schema against the committed
corpus of recorded dumps.

## `--min-hit-count` inverts for a depth cohort

The rule above exists because the capture is driven by a **different** policy
from the scan, so a seed found by one combination says *reachable*, not
*reached*. **A depth cohort is the opposite case.** Design §6 sanctions "the sim
pre-scan chooses (seed, policy, policy-seed) triples whose scripted line reaches
the target" precisely because a deep line is **fragile**: the property is not
"this seed can be won" but "this exact line wins this seed". A one-hit triple is
a perfectly good cohort member, and raising `--min-hit-count` here would discard
most of an Act-3 cohort for a robustness property its consumer does not use.
`--cohort-list` therefore emits **every qualifying row**, and passing
`--min-hit-count > 1` alongside it prints a note.

### What the policy column does and does not mean

`fuzz::PolicyKind` (the sim's seven) and the driver's `--policy` family
(`random-legal` / `greedy` / `script` / `external`+config) are **different
families**. A triple naming `greedy_damage` names a *sim* policy; the oracle
campaign cannot execute it. The triple asserts that **a scripted line of that
shape reaches the target on that seed** — it is provenance for a reachability
claim, and the capture then confirms with its own scripted policy. S2.42 adopted
that reading deliberately rather than building a correspondence between the two
families, which would have meant a new `PolicyKind` in the one file S2.41 is
concurrently editing. The cohort file's own `#` header says so, so a consumer
cannot pick it up without meeting the caveat.

**S2.V2 closes the gap for the sim-consulting policies specifically**: a
`sim_search`/`sim_search_skip` triple emitted with `--script-dir` carries its
complete decision sequence as an STS-SCRIPT v1 file, and the live campaign
replays that exact line through `script_policy_cmd.py` — so for THOSE triples
the policy column is executable after all, through the script rather than
through a policy correspondence. Triples of the E0 policies keep the
provenance-only reading above.

## `--min-hit-count` is the whole point

What a run encounters depends on the path taken through the map, which is the
*policy's* choice, not the seed's — and the capture will be driven by a
**different** policy from the scan. A seed whose target was found by exactly one
scanned combination says the target is *reachable*, not that it will be
*reached*.

So the qualifying rule counts hits across combinations. Measured on
`STS00100-STS05099` × {random, greedy_damage} × {0, 1} at the commit that
introduced this tool, `Match and Keep!` qualified 152 seeds at
`--min-hit-count 1` and 91 at `--min-hit-count 2` — the 61 seeds that dropped
out are precisely the ones a capture would have been sent to for nothing.
**Use 2 or more.** (Re-derive the figures with the command above rather than
quoting these; they move with the engine.)

## Determinism

Every stochastic decision in a scanned run comes from `fuzz::PolicyRng`, a
private splitmix64 seeded from `policy_seed` — no engine stream is touched — so
a row is a pure function of its case. `--verify-determinism` scans every case
twice and exits 1 on any difference; the same scan run as two separate processes
is byte-identical, which `seed_scan_test` also pins.

## Naming events

`--need-event` takes either spelling, case-insensitively: the enum symbol
(`MATCH_AND_KEEP`) or the game id (`Match and Keep!`). `--list-events` prints
the table. Multiple `--need-event` flags are an AND *within one run*, not an OR
across runs.

## Relationship to `tools/fuzz`

The run loop is `sts::fuzz::run_case`, not a copy of it. This directory adds a
`fuzz::StepObserver` over pass A and nothing else — the scan's value is that its
verdict matches what a capture will see, and a forked policy loop is a loop that
drifts away from that.
