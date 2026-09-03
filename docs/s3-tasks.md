# S3 Task Ledger — keys, Act 4 (TheEnding), the Corrupt Heart

Execution tracker for [s3-design.md](s3-design.md) (the S3 scope +
verification spec — this file never overrides it; on conflict the design doc
wins and this file gets fixed). [stage-a-design.md](stage-a-design.md),
[stage-b-design.md](stage-b-design.md) and [s2-design.md](s2-design.md) remain
frozen and in force for everything they cover;
[conventions.md](conventions.md) is binding on every task here, **as amended
by the 2026-09-03 owner evidence directive** (protocol bullet 4 below, design
§6.0). Opened by the S2-G2 gate Log's "S3 planning opens as its own fresh
exercise". The training program's Phase **T5 — the headline A20 Heart kill —
is blocked on this ledger's exit gate S3-G2**
([training-plan.md](training-plan.md) §4.3, §7).

**This file holds only what is open.** When tasks land, their blocks gain Log
lines; large completed blocks move to an `s3-log.md` archive once one exists,
mirroring the Stage B and S2 convention.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked. One
  further row status is S3-specific and first-class:
  **`UNVERIFIED-until-captured`** — the behaviour is landed and the code is
  believed right, but no real-run capture witnesses it yet. It is written in
  the row, with the blocking prerequisite named. It is legal at S3-G1 and
  **illegal at S3-G2** (design §6 item 9).
- One sub-agent per task, self-contained brief, own worktree via
  `tools/task_worktree.sh create <task>` (from Windows); orchestrator
  re-verifies and lands on `master` — one task = one commit. Model choice per
  CLAUDE.md.
- A task is **done only when its Acceptance block passes** — run the commands,
  don't infer.
- **The evidence rule (owner directive, 2026-09-03; supersedes the unit-test
  clauses of conventions §1 and §5 for all S3 work).** This project no longer
  writes or runs unit tests. The marker of truth is **oracle / real-run
  replay**. Every Acceptance block below is phrased as *build + real-run
  evidence*: the named live-game captures replaying **zero-diff** through
  `replay_run_diff --replay` (plus `--vitals` / `--combat` where the drift is
  combat-internal), the committed CI corpora staying zero-diff, and the
  reach/cohort reports `seed_scan` / `sim_search` / the campaign pipeline
  emit. A registry row lands with **the capture that witnesses it** (or the
  corpus file it was promoted into), never with a table test. Content whose
  behaviour has no capture yet must obtain one by **directed capture** — the
  S2.43 mechanism — or say `UNVERIFIED-until-captured` out loud.
  `check_stale_counts.sh` and `check_doc_links.sh` remain. A **build** on all
  six presets is not a test and remains required. The Stage-A fixtures, the
  golden vectors and the twin binaries are replay artifacts, not unit tests,
  and they remain. conventions.md §1 and §5 carry the same directive
  (amended the same day, commit `dd15937`), so there is no document conflict;
  the Deferred-obligations row that recorded one while this ledger was being
  written is discharged.
- **Reach precedes content** (design §6.0 consequence 4). Nothing in Act 4 can
  be witnessed before a live run reaches it carrying three keys, so the keys'
  engine surface, the Act-4-aware fork redeploy and the key-aware driver are
  **Phases S3.1–S3.2**, ahead of the Act-4 content in S3.3–S3.4. A phase order
  that put content first would manufacture a wall of
  `UNVERIFIED-until-captured` rows and defer every real finding to the end.
- Respect `Deps:`; ∥ marks parallel-safe groups (disjoint deliverables). Gates
  **S3-G1 / S3-G2** are stop-the-line for their phase. The gate namespace is
  ledger-local on purpose: the G-series stays reserved for Stage C planning
  (G7's closing note), the GT-series is the training ledger's.
- **Cross-ledger shared namespaces** (`RunPhase`, fuzz `MoveCat`,
  `MonsterIntent`, opcodes, `PowerId`/`CardId`/… blocks) remain allocated in
  [stage-b-tasks.md](stage-b-tasks.md) "Shared namespaces — allocation now in
  force" — the single authority. This ledger records only which S3 task holds
  which granted block; claim there first, record here second.
- **`SCHEMA_VERSION` 8 → 9 has exactly one owner (S3.31) and
  `PUBLIC_VIEW_VERSION` 6 → 7 exactly one (S3.51).** Any other task that
  believes it needs a bump is stop-the-line (conventions §5).
- Task body prose before the `**Deps:**` line is the block's Deliverables
  field.
- Capacity rule ([training-plan.md](training-plan.md) §4.4) still applies: S3
  authoring never starves open training-ledger work; content authoring has
  zero dependency on training results, and a trained checkpoint is an
  accelerant for S3 verification cohorts, never a precondition (design §6.1
  step 3).
- **The bridge never runs from a task worktree** and campaign artifacts are
  never committed (design §7.3 data root, `D:\STS_BG_Mod\_oracle_data`).
  Capture tasks run from the main checkout with the user's go-ahead for the
  machine takeover; a task whose Acceptance needs a capture it cannot run says
  so and hands the capture to the campaign task, exactly as S2.33 handed Mind
  Bloom to S2.43.

## Registry id blocks granted to Wave 1

Granted at ledger creation, maxima re-derived from the tree on 2026-09-03
(`encounters` 61, `events` 51, `powers` 111, `monsters` 65, `cards` 132,
`relics` 150, `a20` 20; `RunPhase` 11, `RoomType` 8, fuzz `MoveCat` COUNT 32,
opcode 74, power `Hook` 17 / `kHookCount` 18, `MonsterIntent` 15,
`RelicHook` 16, `CardTrigger` 3, `ChoiceKind` 12, `MONSTER_ROLL_TIMINGS` 2,
`SCHEMA_VERSION` 8, `PUBLIC_VIEW_VERSION` 6). Append-only; unspent ids **gap
permanently, never backfill** — that includes the whole unspent tail of S2's
granted windows (`monsters` 48 and 66, `powers` 112–135, `cards` 133,
`relics` 151–154).

| Domain / namespace | Block | Holder |
|---|---|---|
| `encounters.yaml` | **62–63** (Shield and Spear, The Heart); 64 reserve | S3.41 |
| `events.yaml` | **52** (`SPIRE_HEART`, member of no act list); 53 reserve | S3.41 |
| `powers.yaml` | **136–139** (Surrounded, Back Attack, Beat of Death, Invincible); 140 reserve | S3.41 (rows), S3.42/S3.43 (bodies) |
| `monsters.yaml` | **67–69** (SpireShield, SpireSpear, CorruptHeart); 70 reserve | S3.41 |
| `cards.yaml` | **134–135** contingency — design §2.5 predicts **zero** | S3.4x, release unspent |
| `relics.yaml` | **155–156** contingency — design §2.5 predicts **zero** | S3.4x, release unspent |
| `a20.yaml` | no new rows; six existing rows' notes refreshed | S3.41 |
| `RunPhase` (`run_advance.hpp`) | **12** contingency — the preferred model rides `EVENT_DIALOG` (design §4.1); release unspent | S3.31 |
| `RoomType` (`map_rooms.hpp`) | **9** `Victory`, **10** `TrueVictory`; `kRoomTypeCount` 9 → 11 with its `static_assert` | S3.31 / S3.33 |
| fuzz `MoveCat` (`tools/fuzz/.../policy.hpp`) | **32–35** (`REWARD_CLAIM_KEY`, the Act-4 map choice, the Spire-Heart dialog, one reserve); `COUNT` → 36 | S3.52 |
| Opcode (`interp.hpp`, `vocab.py`) | **75–77** contingency — design §2.3 expects the four powers to be native binders on existing opcodes; release unspent | S3.42 / S3.43 |
| power `Hook` | **18** `on_attacked_to_change_damage` contingency (`kHookCount` → 19) — Invincible may instead ride the existing `apply_buffer` site natively, in which case release it | S3.43 |
| `RewardItemKind` (`combat_rewards.hpp`) | **two new values** `EMERALD_KEY` (6), `SAPPHIRE_KEY` (7); `kRewardKindCount` 5 → **8**, not 7 — see S3.11's Log for why the granted arithmetic was wrong (the constant lived only in the fuzz coverage table and already under-covered `STOLEN_GOLD`) | S3.11 `[x]` |
| `MonsterState.flags` | a **type-scoped block** for the three Act-4 classes' per-instance counters (Shield/Spear `moveCount` 2 bits each; Heart `isFirstMove` 1 bit + `moveCount` 2 bits + a saturating `buffCount` ≥ 3 bits); exact bits claimed at dispatch | S3.42 / S3.43 |
| `SCHEMA_VERSION` | **8 → 9**, one site | **S3.31 only** |
| `PUBLIC_VIEW_VERSION` | **6 → 7**, one site | **S3.51 only** |

**No new `MonsterIntent`, `RelicHook`, `CardTrigger`, `ChoiceKind` or
`MONSTER_ROLL_TIMINGS` value is expected** (design §2.2, §2.3, §7); 14 remains
B3.15's published `MonsterIntent` reserve and stays unissued. Note on the fuzz
`MoveCat` grant: S2.28 was allocated 32 and **released it unspent**, and
`COUNT` never moved past 32, so no committed soak artifact can key on that
value — it is therefore re-issued here rather than gapped, and this sentence is
the record of why.

## Deferred obligations

Same semantics as the Stage B and S2 tables (live carrier; discharge in place,
never delete). Rows above the rule are **inherited** from
[stage-b-tasks.md](stage-b-tasks.md) or [s2-tasks.md](s2-tasks.md) and are
re-homed here with their new owner or an explicit re-deferral; rows below it
are S3's own.

| Obligation | Deferred by | Owner task | Detail |
|---|---|---|---|
| **Keys as obtainable content** — emerald-elite node flag + `EMERALD_KEY` reward row; `SAPPHIRE_KEY` linked-row claim semantics | stage-b design §1.1 "Out" / s2-design §1; owner-directed to S3 planning 2026-08-10 | **DISCHARGED 2026-09-03 by S3.11** (engine surface), with its behaviour evidence `UNVERIFIED-until-captured` under **S3.23** — see the two forward rows below the rule | **ACCEPTED INTO S3 SCOPE, and the row's premise is corrected.** The row says "the mapRng draw is already modelled — combat_rewards.hpp:107-112 records that only the node flag is missing". The node flag is **not** missing: `setEmeraldElite`'s chosen node has been stored as `emerald_x`/`emerald_y` since the S1 map work (map_rooms.hpp:226-243, :414-435) and the entry buff is applied (`run_advance.cpp` step (9)). What is actually owed is (a) the `EMERALD_KEY` reward row at the burning elite (MonsterRoomElite.java:90,94-98), (b) the sapphire chest's real two-way claim semantics (RewardItem.java:85-90, :298-301, :317-326), and (c) the item the row never mentions — the `!Settings.hasEmeraldKey` guard that **removes the `mapRng` draw from every act generated after the key is taken** (AbstractDungeon.java:543), which changes later maps and is S3's highest-risk trap (design §5 trap 1). Ruby is already live. The S2 row stays as written (it is history); s3-design §9 carries the correction |
| **The Courier's restocked colored-card identity** (the one unseeded value in scope) | S1 shop model (`shop.hpp` `kShopRestockedUnknownCard`); owner-directed 2026-08-10 to a post-S2-G2 task | **S3.24** | **ACCEPTED INTO S3 SCOPE, sized to ride the one fork redeploy S3 is doing anyway** — which is exactly the condition the row set ("ride it with one that is happening anyway"). Both halves land together: sim-side a dedicated seeded stream reproducing retail's uniform draw over the eligible (rarity, type) pool, fork-side one patched call consuming the same stream under the patched-fork oracle-contract precedent. S3.21 carries the patch into the redeploy; S3.24 owns the sim half and the zero-diff shop capture that witnesses it. **DISCHARGED 2026-09-03 by S3.24** — both halves landed in one commit: `courier_restock_stream` (shop.hpp, a derived stream, no schema byte) and `patches/CourierRestockSeedPatch` (flag `oracleCourierRestockSeed`), with `kShopRestockedUnknownCard` and its buy-refusal deleted. Two live tails remain, each owned elsewhere and named in the S3.24 Log: the jar redeploy + `PROTOCOL.md` §5.5 are **S3.21**'s (hand-over in the fork tree), and the witnessing restock capture is **S3.62**'s, so the S3.24 row stands `UNVERIFIED-until-captured` |
| **Per-step throughput attribution across S2** (×0.712 combat step / ×0.498 batch vs B5.5) | S2.45 | **S3.64** | **ACCEPTED INTO S3 SCOPE.** The named A/B is `d57e077` against `646bd18` on `bench_advance_mask` + `bench_throughput`, interleaved through `tools/bench_ab.sh` (never two sequential runs), with `RESULT: UNMEASURED` an acceptable answer. S3.64 also owns the *new* honest whole-run baseline: S2's "three-act runs/sec" was unquotable because no weight-free policy leaves Act 1 ([verification/s245-throughput.md](verification/s245-throughput.md)), and S3 is the first stage with a policy that finishes runs |
| **Sharp Hide THORNS retaliation on the killing blow** | TE.1 (stage-b table: "UNASSIGNED — S1 pump semantics, owner-approved task") | **S3.44** | **ACCEPTED INTO S3 SCOPE.** Witness STS420252 (te1_survival_b160) already exists and is a promoted reproducer — under the 2026-09-03 evidence rule this row is unusually well placed, because the capture that proves the fix is already on disk. Act 4 sharpens the motive: `BeatOfDeathPower` fires a THORNS-typed hit after **every** card the player plays (BeatOfDeathPower.java:40-44), so terminal adjudication at the Heart is exactly this ordering question at its most consequential. **DISCHARGED by S3.44, 2026-09-03, and the "already on disk" premise did not hold.** The `te1_survival_b160` group and its promoted reproducer are gone from the §7.3 data root, and the row's own defect had already been closed by `86fc2be` (S2.43's four-arm survivor set, which names the Sharp Hide retaliation and seed STS431342) and `d57e077` (S2.49's THORNS exemption) — both later than TE.1's 2026-08-03 finding. S3.44 re-witnessed the family from a 21-instance same-shape cohort mined out of the 184 on-disk Sharp Hide captures and fixed the **residual**: the terminal resolver drained a snapshot, so an action a survivor queued while resolving was never popped — which is precisely the Heart's `BeatOfDeathPower` case. See the S3.44 Log |
| **~60 out-of-yaml MIRROR sites carrying citations `wave3-citations` corrected in `registry/*.yaml`** | wave3-citations, 2026-07-28 (stage-b, UNASSIGNED) | **S3.65** | **ACCEPTED INTO S3 SCOPE**, together with the three sibling citation rows the same sweep left open (the nine repo-wide out-of-range `File.java:line`s, the eleven and fifteen `relics.yaml` +1000-class ones, the nine `cards.yaml` ones). Folded into one provenance task because S3 touches `src/`, `include/` and `tools/` broadly and a split-brain between the registry and its mirrors is precisely the thing that makes a re-read at task time untrustworthy. Comment/provenance only — a behaviour change discovered mid-sweep is stop-the-line, not a drive-by |
| **Windows CI job** | build effort (stage-b, UNASSIGNED) | **S3.66** | **ACCEPTED INTO S3 SCOPE**, and promoted in importance by the evidence rule: with unit tests gone, CI's job is to prove the six presets still **build** and that the committed corpora still replay zero-diff, which is now the whole automated safety net. **Pin the LLVM version** (the googletest `/WX-` workaround exists because a clang release added a warning gtest trips over). The proposed workflow is unverified because Actions cannot run locally; S3.67 owes a green run on a real push, not a plausible YAML |
| **Translator: power `misc` fields other than player-owned Combust** (the five-way untagged union) | B3.7 (stage-b, UNASSIGNED; re-scoped by `wave2-harness` stage 3) | **S3.21** | **ACCEPTED INTO S3 SCOPE because Act 4 makes it live.** `GameStateConverter` emits whichever of `basePower`/`maxAmt`/`storedAmount`/`hpLoss`/`cardsDoubledThisTurn` is present first (PROTOCOL §3.14), with nothing telling a reader which. `InvinciblePower` carries a private **`maxAmt`** (InvinciblePower.java:18-29) — the union's *second* member — on a power the differ must compare every turn of the Heart fight. S3.21 owns the disambiguation as part of the redeploy (a tagged emission is the obvious fix and is a fork change, so it rides the same jar). **DISCHARGED 2026-09-03 by S3.21** — `power.misc_field` names the source field and is emitted only alongside `misc` (PROTOCOL §3.14, §5.6(c)); the translator keeps its inference on an untagged (pre-redeploy) capture and VERIFIES it against the tag on a new one, so a Combust whose tag is not `hpLoss` aborts. The tag's LIVE witness is `UNVERIFIED-until-captured` and is **S3.23**'s: exactly five classes declare the union's members (Malleable, Invincible, Flight, Combust, Echo Form) and none of them can appear in an Act-1 Ironclad run, so the preflight could not witness it and an Act-2 crossing witnesses it automatically |
| **`--replay` compares `RunState`, not in-combat card COSTS** | S2.43 read-out ("worth its own row"); [verification/s2-verification.md](verification/s2-verification.md) §9 limit 2 | **S3.53** | **ACCEPTED INTO S3 SCOPE, and re-rated from "worth a row" to load-bearing.** A whole cost-state family reached the S2 depth wave undetected because no acceptance surface compared in-combat card costs against a capture. Under the evidence rule the replay differ *is* the acceptance surface, so a blind spot in it is a blind spot in the project's only marker of truth. S3.53 closes it and the event-grid-mask sibling below |
| **Audit the event-grid legal-action masks against live captures** | raised as a background-task chip at the S2-G2 close (2026-08-27); **no ledger row was ever written for it**, which is why it is being written here | **S3.53** | **ACCEPTED INTO S3 SCOPE, with its provenance stated honestly:** this obligation exists as a one-line note in the S2 session hand-off and nowhere in the repository, so its exact original scope is not recoverable. S3.53 therefore defines it: for every screen that presents a card/relic grid (Neow, campfire Smith/Toke, event grids, the shop purge grid, the boss-relic screen), compare the **engine's legal-action mask** against the live `ChoiceScreenUtils` candidate list on a real capture, and make the comparison an acceptance surface rather than an inspection. Motivated by the same S2 findings the row above cites, plus The Library GRID identity and the Match-and-Keep index-space fixes |
| **SecretPortal: should the simulator model a wall clock?** | S2.43 ("OWNER CALL LEFT OPEN"); [verification/s2-verification.md](verification/s2-verification.md) §9 limit 4 | **RE-DEFERRED — owner policy call, not engineering** | Both sides are pinned to `playtime = 0` today (the engine's `kUnmodelledPlaytimeSeconds`; the fork's `OraclePlaytimePinPatch`), so **no capture can disagree with the sim** and the question is invisible to every S3 bar. The 2026-08-10 ratification rested on "the event is avoidable and essentially never optimal", which is true of the event and not of the draw-index shift its omission causes — that is the open half. It is out of S3 scope because deciding it is a scope decision about what the simulator *is*, and because acting on it would unpin the fork mid-stage. Surfaced to the owner with the S3 plan; carried by s2-design §5 trap 5 until answered |
| `colorlessCardPool` is shuffled IN PLACE by `returnColorlessCard` | B4.13 (stage-b, UNASSIGNED) | **RE-DEFERRED** | Act 4 adds no colorless consumer beyond the shop, which S1 already models; the row's named consumer turned out not to be one and S3 does not create a new one. Stays in the stage-b table |
| `replay` generalized to seed a sim replay from any translated `RunState` (narrowed to the mid-run resume) | B1.6 (stage-b, UNASSIGNED) | **RE-DEFERRED** | Tempting under the evidence rule and still wrong for S3: a mid-run resume would let a capture be scored from Act 4 without the run that got there, which is exactly the shortcut design §6.1 refuses. Revisit only if the Act-4 reach cost proves prohibitive **and** the owner sanctions a weaker evidence chain |
| `increaseMaxHp`'s second parameter does not do what three registry rows say it does (Waffle, Mango, FaceOfCleric) | wave3-citations (stage-b, UNASSIGNED) | **RE-DEFERRED** | A prose-vs-behaviour mismatch needing an engine/comment owner, not a citation pass, and S3.66 is explicitly a citation pass. No Act-4 consumer. Stays in the stage-b table |
| `RETAIN` `CardFlag` end-of-turn sweep | B3.1 (stage-b, "first content consumer") | **RE-DEFERRED** | No Act-4 card or relic uses Retain; the first consumer is still an S4 character |
| Archived soak kv summaries predating the `victories` counter no longer parse | fix-postboss-shop (stage-b, UNASSIGNED) | **RE-DEFERRED** | S3.52 rewrites the soak's counters again (four-act buckets, key coverage), so any parser work done now is thrown away; the loud-failure behaviour is correct in the meantime |
| --- | --- | --- | --- |
| **conventions.md still carries the superseded unit-test wording** | this planning exercise (S3 ledger creation, 2026-09-03) | **DISCHARGED 2026-09-03** — conventions `dd15937` (§1 owner-directive block, §5 "no rule without its witness") | Recorded while this ledger was drafted, before the orchestrator amended conventions.md the same day. No S3 brief was dispatched in between, so the condition ("before the first S3 task is dispatched") held. A real document conflict under conventions §4, recorded rather than silently tolerated. conventions §1 ("Tests land in the same change as the code they verify. Registry YAML is code: entries land with their tier-2 tests in one commit"), §5's "No rule without its test — … for registry entries the test is the tier-2 table test", and §6's three-ways-the-test-suite-lies subsection all describe a practice the 2026-09-03 owner directive retires. This planning exercise was scoped to two new documents plus two one-line cross-references and could not edit that file. conventions.md is the authority that wins on conflict, so **it must be amended to carry the directive before it is quoted at an S3 agent**, or the first brief will cite a rule the ledger contradicts |
| **The `SpireHeart$CUR_SCREEN` enum order is derived, not read** | s3-design §4.1 | **S3.31** | The inner enum was stripped from the decompile source jar, so CFR rendered the switch with bare integer labels. The mapping (INTRO=1, MIDDLE=2, MIDDLE_2=3, DEATH=4, GO_TO_ENDING=5) is derived unambiguously from the arms' bodies, but "derived" is not "read in full" (stage-a §1). Recover it mechanically via `RECOVERED-INNER-CLASSES.md` §2 and cite the recovered file, or record the derivation as the provenance with the reason |
| **Back-attack facing: model it, or collapse it?** | s3-design §2.3 | **S3.42** | `applyBackAttack` (AbstractMonster.java:1015-1017) reads the player's `flipHorizontal`, which changes when a target is hovered (AbstractPlayer.java:1291-1293) and is re-evaluated on every hand layout (CardGroup.java:204-223). The proposed collapse — "with exactly two guards, the one the player is not facing takes 1.5×" — must be proven exact or rejected, against those three methods read in full **and** against a live Shield-and-Spear capture where the player attacks each guard in turn |
| **Act-4 first-row map choice width** | s3-design §4.4 | **S3.32** | `MapRoomNode.update`'s first-room arm gates only on `y == 0` and hover (:254-279), and Act 4's row 0 has six roomless nodes beside the rest room. Whether the game offers 1 or 7 candidates decides the legal-action mask; an over-wide mask is a leak-gate problem, not a cosmetic one. Answer from the fork's own `ChoiceScreenUtils` output on the first Act-4 capture, and pin the same source for the elite→boss action kind (`MAP_BOSS` vs `MAP_NODE`, DungeonMap.java:68) |
| **The Act-4 floor pair is A20-dependent** | s3-design §4.3 | **S3.32** | Act 4's floor base is 51 below A20 and 52 at A20, because the A20 second Act-3 boss room is a real floor. That breaks `act_floor_base(act) = (act-1) * kActFloorSpan`, so the base must be run state written at the crossing and `run_cur_row` must read it. Both halves need **separate** witnesses at **both** ascension bands — a single matching number on one band hides the pair, which is the mistake s2-design §4.2's row existed to prevent |
| **The emerald key's CLAIM, and with it §5 trap 1, has no capture** | S3.11 | **S3.23** | The engine assembles the `EMERALD_KEY` row, skips `setEmeraldElite`'s `mapRng` draw once the key is held, and both are corpus-clean — but no committed capture ever *presses* the key row, and none takes a key and then crosses an act. So the highest-risk change in S3 is landed on a source read plus a zero-diff that cannot see it. The discharging capture is a PAIR on one seed: emerald claimed → next act generated, and emerald skipped → next act generated, with the two acts' maps **differing**. A single matching run is not evidence, because a sim that kept drawing would still match a capture that never took the key |
| **`RunState.keys` is neutralized on both sides of every replay comparison** | S3.11 (inherited shape from the ruby bit) | **S3.21** | `neutralize_incomparable` and `neutralize_presentation_only` both zero `keys`: the sim has three writers now (Recall, and S3.11's two key-row claims) while neither CommunicationMod's `game_state` nor the fork's oracle block exposes `Settings.hasRubyKey/hasEmeraldKey/hasSapphireKey`, so the capture side is structurally 0. Until S3.21 (a) emits the three booleans, every key claim is proved only through its CONSEQUENCES — the spent campfire, the abandoned relic still popped from `relic_pool_*`, the moved `map_rng`. Remove both zeroings in the same change that lands the emit, or the new field is emitted and still not compared. **DISCHARGED 2026-09-03 by S3.21** — both zeroings are gone. The field is compared as a **pair**, not unconditionally (`neutralize_unattested_keys`, gated on `TranslatedRecord::has_keys`): deleting the zeroing and stopping there REDs `act1_a20_50/STS71037`, whose seq 83 claims the `SAPPHIRE_KEY` row so the sim rightly holds `kKeySapphire` while the pre-redeploy capture has no key block and translates to a structural 0 (`keys: 0 -> 4`). Every capture from the new jar on is compared; only unattested pre-redeploy records are neutralized |
| **`Spire Heart` clicks 1–2: collapse or model?** | s3-design §4.1 | **S3.31** | Clicks 1 and 2 change no run state and are exactly the shape the engine already collapses (shrines.cpp / beyond_events.cpp, accepted at G7), and the follower's glue rule 3 answers collapsed one-click dialogs either way. But the differ compares **record counts**, so the choice must be made once, recorded, and reflected in the follower — not discovered during scoring |

---

## Phase S3.1 — Keys: the engine surface (reach precedes content)

- **S3.11** `[x]` **Keys as obtainable content.** The three keys become real
  run content, discharging the owner-directed deferred row. Three parts, all
  from source read in full at task time. (a) **Emerald:**
  `MonsterRoomElite.addEmeraldKey` (MonsterRoomElite.java:90, :94-98) appends a
  free-standing `EMERALD_KEY` reward row **after** the relic (and after Black
  Star's second relic) under `isFinalActAvailable && !hasEmeraldKey &&
  !rewards.isEmpty() && currMapNode.hasEmeraldKey`; the EMERALD arm of
  `RewardItem(RewardItem, RewardType)` **discards** its linked argument
  (RewardItem.java:91-95), and `claimReward` case 7 (:327-332) sets
  `keys |= kKeyEmerald` and removes the row. The node flag and the entry buff
  are **already** modelled (`emerald_x`/`emerald_y`, `run_advance.cpp` step
  (9)) — do not re-derive them, verify them. (b) **Sapphire:** replace the S1
  ignored-linked-row model with the real two-way link
  (`AbstractChest.open` :95-97 → `AbstractRoom.addSapphireKey`
  AbstractRoom.java:545-547 → `RewardItem.java:85-90`) and its two mutually
  destructive claim branches (:298-301 relic-taken kills the key row silently,
  :317-326 key-taken kills the relic row unrewarded), plus
  `removeOneRelicFromRewards`'s partner removal (:549-559). The `BossChest`
  path is already proven not to reach any of this (S2.11). (c) **Holding a key
  changes later acts** (design §3.4, §5 trap 1): thread `!hasEmeraldKey` into
  map generation so `setEmeraldElite` **skips its `mapRng` draw entirely**
  once the key is held (map_rooms.hpp:425-439 draws unconditionally today);
  thread `!hasSapphireKey` into the chest append; the `!hasRubyKey` campfire
  gate is already live. Claims the two new `RewardItemKind` values
  (`kRewardKindCount` 5 → 7). The keys' storage (`RunState::keys`) already
  exists — **no schema bump here.**
  **Inherited:** the s2-tasks "Keys as obtainable content" row (see Deferred
  obligations, incl. its corrected premise).
  **Deps:** — **Acceptance:** all six presets **build**; the committed Act-1
  and three-act CI corpora still replay **zero-diff** in every preset (the
  chest append changes reward-row shape, so this is the real regression
  surface and a RED here is the point of running it); `check_stale_counts.sh`
  and `check_doc_links.sh` clean. Behaviour evidence is
  `UNVERIFIED-until-captured` at landing and is discharged by **S3.23**, which
  this task must name in its Log with the exact captures it needs: an emerald
  claim + the next act's crossing, a sapphire claim on **both** branches, and
  a paired key-not-taken control on the same seed.
  **Log:** 2026-09-03. All three parts landed. **(a) Emerald:**
  `assemble_combat_rewards` gained a `node_has_emerald_key` parameter and
  appends a free-standing `EMERALD_KEY` row inside its Elite arm, after the
  relic and after Black Star's second relic, under `kFinalActAvailable &&
  !(keys & kKeyEmerald) && out.count != 0 && node flag`
  (MonsterRoomElite.java:90, :94-98); the linked argument the Java passes is
  discarded because the EMERALD arm assigns no `relicLink`
  (RewardItem.java:91-95). `run_advance.cpp` supplies the node flag from the
  new `on_emerald_elite_node(rc)` predicate, which the entry buff (step (9))
  now shares instead of inlining. **(b) Sapphire:** `open_treasure_chest`
  appends the linked row between the base relic and the `onChestOpenAfter`
  fan-out (AbstractChest.java:95-97 → AbstractRoom.java:545-547) under
  `!hasSapphireKey`; both destructive claim branches are in `claim_reward`
  (relic-taken silently kills the key row, RewardItem.java:298-301; key-taken
  kills the relic row unrewarded, :317-326) and `remove_first_relic_item` takes
  the pair for N'loth's Mask (:549-559). **The link is stored as adjacency, not
  a field** — `addSapphireKey` always appends directly after the row it links
  to, nothing is ever inserted mid-list, removal compacts, and the Java itself
  tests `relicLink != i.next()`; so `RunRewardItem` keeps its 24-byte layout.
  **(c) Held keys change later acts:** `assign_room_types` gained
  `has_emerald_key` and now skips the `mapRng.random(0, eliteNodes.size()-1)`
  draw entirely (AbstractDungeon.java:543), fed from `rs.keys` at BOTH
  generation sites; the chest append is gated on `!hasSapphireKey`; the ruby
  campfire gate was verified live at `rest_sites.cpp:203` and needed nothing.
  `kFinalActAvailable` moved from `rest_sites.hpp` to `run_state.hpp` beside the
  `kKey*` bits, because four gates in four modules now read it and
  `relic_pools.hpp` includes `map_rooms.hpp` (so map generation can never reach
  `rest_sites.hpp`). Mask + PublicView: the key rows are ordinary reward rows
  and need no new code — `reward_claim_legal` returns true unconditionally for
  both (`claimReward` cases 6/7 have no precondition), the existing
  `can_claim_reward[i]` loop publishes them, and `PvRewardItem.kind` already
  carried the byte; `docs/public-view-audit.md` §8.1 gained the classification
  rows and a change-log entry, and **`PUBLIC_VIEW_VERSION` did not move** (S3.51
  still owns 6 → 7).
  **Grant deviation, recorded:** the ledger granted `kRewardKindCount` 5 → 7.
  It is **8**. `kRewardKindCount` was not an engine constant at all — it was a
  hand-written `5` in `tools/fuzz/.../coverage.hpp` sizing `reward_claimed[]`,
  and it had ALREADY under-covered the enum (`STOLEN_GOLD` == 5 was outside the
  array and `reward_kind_name` had no case for it). 7 would have dropped
  `SAPPHIRE_KEY` == 7 the same way. It is now published by the engine as
  last-enumerator + 1 with a `static_assert`, and the fuzz constant aliases it —
  the pattern `kRoomTypeCount` already uses two lines above, for the same
  stated reason. No id was renumbered and no schema moved.
  **Acceptance evidence (commands + verdicts).** Six presets BUILD:
  `cmake --preset win-{debug,asan,release} && cmake --build --preset win-…`
  through a vcvars64+LLVM wrapper — all three exit 0; and
  `tools/wsl_run.sh --script tools/build_presets.sh debug asan release` —
  `PRESETS BUILT: debug asan release`. Both committed corpora replay
  **zero-diff** via `tools/wsl_run.sh --script tools/corpus_replay.sh`
  (`replay_run_diff <capture> --replay --stop-on-diff` per seed, through
  `ci_corpus_smoke.py`): `act1_a20_50 --replay: ZERO-DIFF (exit 0)` (50 seeds),
  `three_act_a20_5 --replay: ZERO-DIFF (exit 0)` (5 whole three-act runs), with
  BOTH injected-divergence negative controls failing loud. The chest append did
  change reward-row shape and the corpora do exercise it: 2 of 50 act-1 and 5 of
  5 three-act captures carry `SAPPHIRE_KEY` rows (18 chest screens), and the
  three-act `s2v2_mb_102529__STS102529` carries an `EMERALD_KEY` row at floor 12
  (`[GOLD,RELIC,EMERALD_KEY,POTION,CARD]`). `tools/check_stale_counts.sh` and
  `tools/check_doc_links.sh` clean.
  **What the corpora already witness, and what they do not.** Both sapphire
  branches are witnessed: 17 chest screens claim the RELIC (the key row must
  vanish with it or the next ordinal misindexes) and `STS71037` seq 83 claims
  the KEY (`choose 1` on `[RELIC,SAPPHIRE_KEY]`), whose abandoned relic must
  stay popped from `relic_pool_*` — a compared field. The emerald ROW's
  presence and position are witnessed by STS102529's seq 134-136, where the
  capture claims ordinals 1, 0, 1 across a shrinking list: `choose 1` at seq 136
  is the POTION only if the sim carries the key row at index 0. Two things are
  **UNVERIFIED-until-captured** and are S3.23's: the emerald key CLAIM (no
  corpus capture presses it), and with it §5 trap 1 — no committed capture takes
  a key and then crosses an act, so the skipped `setEmeraldElite` draw has no
  witness. `RunState.keys` itself is still neutralized on both sides by the
  replay differ (the fork emits no `Settings.has*Key`; S3.21 (a) returns it), so
  the key BITS are proved only through their consequences.
  **S3.23 needs exactly these captures:** (1) an emerald claim at a burning
  elite **and the run continuing into the next act's map generation**, with (2)
  its paired key-NOT-taken control on the same seed, the two acts' maps
  differing — that pair is the whole of trap 1 and the claim record alone
  cannot witness it; (3) a sapphire claim on the KEY branch and (4) a sapphire
  claim on the RELIC branch, each with (5) a key-not-taken control on the same
  seed; and, for §5 trap 6, (6) a burning-elite claim on a **Black Star** run,
  whose four assembled rows force the potion chance to 0 while the ±10 ratchet
  still moves. A directed capture of (6) is the only way that branch is ever
  seen: gold + relic + EMERALD_KEY is three rows, not four.

## Phase S3.2 — Reach instruments and the oracle contract (∥ where marked)

- **S3.21** `[x]` **Oracle contract v2: the Act-4-aware fork redeploy.** The
  one fork redeploy S3 gets, carrying everything that needs a jar change so
  nothing else has to ask for a second. (a) The `oracle` block emits the three
  key booleans (`Settings.hasRubyKey/hasEmeraldKey/hasSapphireKey`,
  Settings.java:65-67) and the burning-elite node flag
  (`MapRoomNode.hasEmeraldKey`, :61), so the differ can compare key state
  instead of inferring it. (b) Act-4 emission: the `TheEnding` dungeon id, the
  5×7 special map (TheEnding.java:72-139), the `Spire Heart` event's screens,
  and the `TrueVictoryRoom` terminal. (c) **The `misc` five-way untagged
  union is disambiguated** — a tagged emission — because `InvinciblePower`'s
  private `maxAmt` (InvinciblePower.java:18-29) makes the ambiguity live on a
  power the differ compares every Heart turn (PROTOCOL §3.14). (d) The
  Courier patch from S3.24 rides this jar. (e) Repo side:
  `PROTOCOL.md` sections for all of the above; the translator learns the
  `Spire Heart` event id and the Act-4 rows (today it **aborts** on that id);
  `replay_run_diff` **stops skipping** post-victory ending records (the named
  "Spire Heart cinematic — out of S2 scope" exception in `main.cpp` and the
  `post_victory_ending_records` counter in `translate.hpp`) and starts
  comparing them; the planner's `kMaxActs` goes 3 → 4
  (`seed_scan.hpp:221-229`) so `--need-*-act 4` stops being a refusal
  (`planner/src/main.cpp:285-300`).
  **Inherited:** the stage-b "translator power `misc` fields" row (see
  Deferred obligations); **S3.24's Courier restock patch** —
  `patches/CourierRestockSeedPatch` + the `oracleCourierRestockSeed` flag are
  in the tree and build, and this task carries them into the jar, records the
  new SHA-256 with them, and writes the **`PROTOCOL.md` §5.5** section the
  patch and `shop.hpp` already cite by name. Everything S3.21 needs (before/
  after, the exact seed formula, the offline seam verification, the three
  differ consequences) is in
  [../tools/oracle_bridge/communicationmod-oracle/COURIER-RESTOCK-HANDOVER.md](../tools/oracle_bridge/communicationmod-oracle/COURIER-RESTOCK-HANDOVER.md).
  Also inherited: S3.11's `RunState.keys` neutralization row: the
  same change that makes the fork emit the three key booleans must delete the
  `s.keys = 0` in BOTH `neutralize_incomparable` and
  `neutralize_presentation_only` (`replay/src/main.cpp`), or the new field is
  emitted and still never compared.
  **Deps:** S3.24 (the Courier patch, so one redeploy carries both)
  **Acceptance:** the new jar's SHA-256 recorded and **live-preflighted** by a
  clean single-seed campaign, the way every prior redeploy was; every S2
  capture in `tests/golden/oracle_corpus/` (Act-1 and three-act, both
  archives) re-translated and replayed **zero-diff** under the new jar, with
  the two archives byte-unchanged; determinism check (the same seed twice,
  identical dumps); six presets **build**; `check_doc_links.sh` clean. A
  redeploy that moves any existing capture's verdict is stop-the-line.
  **Log:** 2026-09-03 — **the redeploy landed and is live.** Jar
  `build/oracle_fork/CommunicationMod-oracle.jar`, SHA-256
  `49b5eeef1f7ec04eb3eef7c0ed41e3e79865564d1cd3c3ec6930d69c48c460f0`,
  built by `powershell -ExecutionPolicy Bypass -File
  tools\oracle_bridge\build_fork.ps1 -CheckDeterminism` under JDK 8
  (`C:\Program Files\Java\jdk1.8.0_171`) and **DEPLOYED** to
  `D:\SteamLibrary\steamapps\common\SlayTheSpire\mods\`. The determinism
  switch passed (a second full build byte-identical). The jar it replaced,
  `ABD95268462FA31E7F7498B45BA4539E3731CC38E59850B547D03AE6F372A4C1` — the
  playtime-pinned S2-G2 pin — is preserved byte-for-byte at
  `D:\STS_BG_Mod\_oracle_data\jar_backup_ABD95268_pre_s321.jar`, so the
  redeploy is reversible with one copy. **Every capture taken from
  2026-09-03 on carries the new pin**; the two committed corpora do not, and
  the contract is two-sided precisely so that stays true.

  *(a) Keys and the dungeon identity.* `getOracleState` emits
  `hasRubyKey` / `hasEmeraldKey` / `hasSapphireKey` (Settings.java:64-67),
  `isFinalActAvailable` (the fourth conjunct of the same gate,
  SpireHeart.java:151 and MonsterRoomElite.java:90) and **`dungeonId`**
  (`AbstractDungeon.id`), and `convertMapRoomNodeToJson` emits
  `has_emerald_key` **only when true** (MapRoomNode.java:61) — absent-means-
  default, so every pre-redeploy capture is byte-unchanged. The translator
  maps the three key booleans into `RunState::keys` and cross-checks
  `dungeonId` against the `act` anchor (`Exordium`/`TheCity`/`TheBeyond`/
  `TheEnding`), aborting on disagreement.

  *The inherited `RunState.keys` row is discharged, and the way it is
  discharged is itself a finding.* Both `s.keys = 0` zeroings are gone from
  `neutralize_incomparable` and `neutralize_presentation_only`, exactly as the
  row demanded — but the field is **not** compared unconditionally. Deleting
  the zeroing and stopping there **REDs the committed Act-1 corpus**:
  `act1_a20_50/STS71037` seq 83 opens a LargeChest offering
  `RELIC Mummified Hand` + `SAPPHIRE_KEY` and answers `choose 1`, **taking the
  key**, so S3.11's run layer rightly sets `kKeySapphire` while the
  pre-redeploy capture has no key block and translates to a value-init 0 —
  `keys: 0 -> 4`, a RED on a record where nothing is wrong. A structural zero
  is an ABSENCE OF CLAIM, not a claim of "no keys". So `keys` takes the same
  **pair shape** `boss_chest` takes (`neutralize_unattested_keys`, gated on
  the new `TranslatedRecord::has_keys`): compared on every record that carries
  the block, neutralized on both sides where it does not. That is "actually
  compared" for every capture from this jar on, and it is what keeps the
  redeploy from moving an existing verdict — which the block's own Acceptance
  makes stop-the-line. Controls, both run: a synthesised capture carrying the
  truthful key state replays CLEAN; the same capture with `hasRubyKey` forced
  true REDs at seq 0 on `keys: 2 -> 0`.

  *(b) Act 4 — audited, not re-implemented.* PROTOCOL §5.6(b) carries the
  table. `GameStateConverter` is dungeon-agnostic almost everywhere, so
  `act == 4`, the boss key `The Heart`, the special map, the `Spire Heart`
  screens (`getEventState` reads the class's static `ID` by reflection;
  `VictoryRoom.phase == EVENT` puts `ChoiceScreenUtils` on its EVENT arm) and
  the `TrueVictoryRoom` terminal (`room_type` + `screen_name` `NO_INTERACT`,
  `screen_type` `NONE` from the `default` arm) were all **verified already
  correct**. Exactly one gap was real — `AbstractDungeon.id` was emitted
  nowhere — and it is what `dungeonId` fills. One retail behaviour is recorded
  rather than changed: `convertMapToJson` emits only nodes with edges, and
  Act 4's `TrueVictoryRoom` node (3,4) has none (it is entered through a fresh
  `MapRoomNode`, ProceedButton.java:191-192), so an Act-4 `map` array carries
  **four** nodes, (3,0)…(3,3). Emitting edgeless nodes would rewrite the `map`
  array of every capture ever taken; the engine's Act-4 map is a constant
  (design §4.4), so four is the whole navigable map.

  *(c) The tagged `misc` union — the stage-b deferred row, discharged.*
  `power.misc_field` names which of `basePower`/`maxAmt`/`storedAmount`/
  `hpLoss`/`cardsDoubledThisTurn` the value came from, emitted **only
  alongside `misc`**. The contract is two-sided and backward-compatible: an
  old capture has no tag and the translator keeps its per-power inference; a
  new capture carries the tag and the inference is **verified** against it —
  a Combust whose tag is not `hpLoss` now aborts instead of being silently
  misread. Three negative controls run and fail loudly: a bogus
  `misc_field` value, a partial key block (2 of 3), and a `dungeonId`
  contradicting its act. The compiled bytecode is the emitter's proof:
  `javap -c` over the jar's `GameStateConverter.class` shows the `misc_field`
  put inside the same `ifnull` guard as the `misc` put, in the same loop
  iteration, writing that iteration's field-name local.

  *(d) The Courier patch rides the jar.* `CourierRestockSeedPatch.class` is
  in it (verified by `jar -tf`), and **PROTOCOL.md §5.5** is written — the
  section `shop.hpp` and the patch javadoc already cited by name. §5.6(d) and
  README-oracle's patch-family table now name it too, with the five-flag
  equivalence baseline restated: the three strip flags, `oraclePlaytimePin`
  and `oracleCourierRestockSeed`. S3.21's own additions need no flag: they
  ride the existing `oracleBlock` gate or are emitted-only-when-set, and none
  changes a game rule.

  *(e) Repo side.* The translator learns `"Spire Heart"` as the **second**
  non-pool sentinel event id after Neow's (it is constructed by its room and
  is a member of no act list, so a pool `EventId` would corrupt the three
  membership bitsets), and learns that Act 4's `eventList`/`shrineList` are
  **empty by construction** (`TheEnding` overrides both initialisers with
  empty bodies, TheEnding.java:198-200, :211-213) — read through the act-local
  "initially present, now absent ⇒ fired" derivation they would have marked
  all eleven Exordium events and all six shrines FIRED off two empty arrays.
  `event_framework.hpp` is deliberately **not** touched: the per-act
  fall-through is S3.32's grant. The post-victory ending tail is now
  **translated instead of dropped** — it lands in `records` at
  `first_post_victory_ending_record` and the differ's `screens.resize`
  compensation is deleted — and `replay_run_diff` reports
  `N of M post-victory ending record(s) compared`. **Today that reads
  `0 of 5`, and the 0 is a fact about the ENGINE, not about the differ:**
  `command_map.hpp`'s COMPLETE-screen `proceed` arm still ends the replay at
  `run_is_victory` one record earlier, because the run layer ends the run at
  the last Act-3 boss instead of walking into the VictoryRoom. **S3.31 moves
  that terminal and the tail starts being compared with no further differ
  change.** Planner `kMaxActs` 3 → 4, so `--need-boss-act 4` /
  `--need-boss-kill-act 4` are accepted clauses that match nothing until
  `engine::kFinalAct` moves (S3.32); `engine::kFinalAct` is untouched here.

  *What the newly-visible tail actually contains* (both corpus victories,
  identically): five records — four `Spire Heart` `choose 0` clicks plus the
  `__terminal_observed__` at `screen_type` GAME_OVER. The **first** carries
  the whole state change and it is exactly design §5 trap 11 and §4.3's A20
  row: `floor` 51 → **52**, all five floor-scoped streams reseeded to
  `seed + 52` (every counter to 0, one shared `(s0,s1)` pair), one entry
  popped off `oracle.encounterLists.monster`, `room_type`
  `MonsterRoomBoss` → `VictoryRoom`, `room_phase` COMPLETE → EVENT. Clicks
  **2, 3 and 4 change nothing but the option label and `choice_list`** — no
  gold, no HP, no stream, no pool. That is the measured answer to the
  clicks-1-and-2 collapse question the deferred row hands S3.31: the entire
  dialog is presentation, and only the ROOM TRANSITION is state. Neither
  corpus victory holds Maw Bank, so the +12 gold half of trap 11 is still
  unwitnessed and stays S3.31's.

  *Evidence.* Six presets **build**: `win-debug` / `win-asan` /
  `win-release` via the vcvars64 + LLVM wrapper, `debug` / `asan` /
  `release` through `tools/wsl_run.sh --script tools/build_presets.sh debug
  asan release` (configure + build, no ctest, per the 2026-09-03 owner
  directive). Both committed corpora replay **zero-diff** with the two
  archives byte-unchanged — 50/50 `act1_a20_50` and 5/5 `three_act_a20_5`,
  via `tools/verify_report/ci_corpus_smoke.py` on Windows and
  `tools/wsl_run.sh --script tools/corpus_replay.sh` in WSL, with both
  injected-divergence controls failing loud. `--replay --vitals` over the
  three-act corpus is vitals-clean. `check_doc_links.sh` and
  `check_stale_counts.sh` clean.

  *Live preflight (2026-09-03, the new jar).* No stale game lock existed —
  `D:\STS_BG_Mod\_oracle_data\oracle_game.lock` was absent and pid 288796 was
  not alive, so nothing was deleted. `campaign_pipeline.py run
  --campaign-id s321_preflight --seeds <STS430000> --policy random-legal
  --policy-seed 1234 --instances 1` captured 28 actions to a floor-1 death;
  scored with **this branch's** tooling it is **CLEAN to its terminal**, 29
  records compared, zero diverging, and `--replay --vitals` **vitals-clean**
  over 24 in-combat records. **Determinism:** the same seed captured twice
  (`s321_preflight` / `s321_preflight_b`) gives **identical** records —
  normalised SHA-256
  `8f7777a1818faae3944044a2d96174c6e330343214f8f34c8ed6a3af30fe6570` on both,
  the only differences being the header's
  `campaign_id`/`created_utc`/`fork_jar_path` and the terminal record's
  `created_utc`. The new fields are live in the capture: every one of the 29
  in-dungeon records carries
  `{"dungeonId":"Exordium","act":1,"hasRubyKey":false,"hasEmeraldKey":false,
  "hasSapphireKey":false,"isFinalActAvailable":true}`, and exactly ONE map
  node per record carries `has_emerald_key` —
  `{"symbol":"E","x":1,"y":9,"has_emerald_key":true,...}`, the first live
  witness of `setEmeraldElite`'s chosen node in any artifact.

  *One expected non-failure worth naming:* the campaign's OWN report says
  `translation_drift`, because `campaign_pipeline.py` post-processes with the
  repo root it is launched from and it was launched from the main checkout,
  whose translator is master's and does not yet know `oracle.dungeonId`. The
  abort message is verbatim `record 1: unknown field
  state_json.game_state.oracle.dungeonId` — the fail-loud field policy working
  exactly as designed, and a positive proof that the deployed jar really is
  emitting the new block. It disappears when this commit lands on master. Every
  verdict quoted above is from this branch's binaries.

  *Owed — `UNVERIFIED-until-captured`, one item.* The `misc_field` tag is
  **not yet live-witnessed**, and the reason is structural rather than
  incidental: exactly five classes in the 12-18-2022 tree declare the union's
  five members — `MalleablePower` (`basePower`), `InvinciblePower` (`maxAmt`),
  `FlightPower` (`storedAmount`), `CombustPower` (`hpLoss`) and `EchoPower`
  (`cardsDoubledThisTurn`) — and **none of them can appear in an Act-1
  Ironclad run** unless the player acquires and plays Combust. No Act-1
  capture can witness the tag. It is discharged by the first capture that
  reaches **Act 2** (Byrd's Flight, Shelled Parasite's / Spheric Guardian's
  Malleable — automatic there), i.e. **S3.23**'s key wave, whose emerald pair
  crosses an act by construction; a Heart capture (`Invincible`) witnesses the
  union's second member at S3.43. Until then the tag rests on the compiled
  bytecode and on the offline synthetic capture with its three negative
  controls, both quoted above.

- **S3.22** `[ ]` **Key-aware sim-consulting driver + reach scan.** Extend the
  S2.V2 instrument rather than the bar (design §6.1 step 1). A `sim_search`
  variant that seeks keys: prefer the burning-elite node while HP allows and
  claim the `EMERALD_KEY` row; claim `SAPPHIRE_KEY` at the first chest; spend
  exactly one campfire on `RECALL`. `seed_scan` gains `--need-keys`,
  `--need-act 4` and `--need-heart-kill` filters, and the emitted STS-SCRIPT
  lines gain the reward-row and campfire steps the follower needs. Then the
  measured reach report: breadth over fresh seeds, then the **re-seeding
  depth pass** on surviving seeds (a (seed, policy-seed) pair is the cohort
  unit — the S2.V2 wave structure), reporting key-carry rate, Act-3 double-boss
  rate with keys, Act-4 entry rate, Shield-and-Spear kill rate and Heart kill
  rate. Report the numbers honestly including the zeros; a zero with a
  positive control beside it is the finding.
  **Inherited from S3.21:** the planner's `kMaxActs` is **4**, so
  `--need-boss-act 4` and `--need-boss-kill-act 4` already parse — the
  `--need-act 4` clause this block asks for is a filter over that vocabulary,
  not a widening of it, and `act_bit(4)` is `0x8`. Note what is NOT moved:
  `engine::kFinalAct` is still 3 (S3.32 owns it), so until that lands an act-4
  clause is answerable and matches nothing; report that zero rather than
  reading it as a scan bug. `seed_scan`'s `boss_ids` is bounded by the
  engine's `kBossIdCap`, which has been 4 since the schema froze.
  **Deps:** S3.11 **Acceptance:** a committed reach report under
  `docs/verification/` in the [verification/s2v2-sim-reach.md](verification/s2v2-sim-reach.md)
  mould, regenerable from the commands it quotes; a `--verify-determinism`
  sweep over both policies with **zero** mismatches; every emitted cohort
  script replaying to its recorded `final_hash`; six presets **build**. If the
  scan produces **no** Act-4 line, that is a reportable result and triggers
  the design §6.1 step-3 escalation ladder (more policy-seed budget, then a
  deeper boss-floor ply) — **not** a weakened bar, and **not** rule handicaps.
  **Log:** —

- **S3.23** `[ ]` **Keys capture wave** (the S3-G2 item-2 evidence, and the
  first live exercise of the new jar). Directed captures through
  `script_policy_cmd.py` scheduled off S3.22's triples: an emerald claim at a
  burning elite **and the run continuing into the next act's map generation**
  (this is what witnesses design §5 trap 1 — the claim record alone cannot),
  a sapphire claim on **both** branches (key taken, relic taken), a ruby
  Recall, and a paired key-not-taken control on the same seed for each. Triage
  every divergence to a root cause per the Stage B process; promote each
  reproducer.
  **Inherited:** S3.11's `UNVERIFIED-until-captured` behaviour evidence — the
  emerald-key CLAIM and §5 trap 1 (see Deferred obligations). S3.11's Log names
  the six captures it needs; note the sixth, which this block's prose does not:
  a burning-elite claim on a **BLACK STAR** run, the only shape in which
  `addPotionToRewards`' four-item suppression (AbstractRoom.java:597-599, §5
  trap 6) is reachable — gold + relic + `EMERALD_KEY` is three rows, not four.
  **Inherited from S3.21:** (1) the deployed jar is
  `49b5eeef1f7ec04eb3eef7c0ed41e3e79865564d1cd3c3ec6930d69c48c460f0`; the
  previous pin is restorable from
  `D:\STS_BG_Mod\_oracle_data\jar_backup_ABD95268_pre_s321.jar`. (2) Every
  capture this wave takes carries `oracle.hasRubyKey/hasEmeraldKey/
  hasSapphireKey`, and `RunState::keys` is **compared** on exactly those
  records (`neutralize_unattested_keys`), so a key claim is now proved
  directly rather than only through its consequences — the emerald pair's
  positive control gains a second, independent signal. (3) Map nodes carry
  `has_emerald_key` when marked, so a capture NAMES the burning elite; today
  `--spot` still seeds that flag from the capture's own reward row
  (`captured_emerald_key_row`), and switching it to the node flag is a
  cheap follow-up this wave's captures make possible for the first time.
  (4) **This wave discharges S3.21's one `UNVERIFIED-until-captured` item**
  — the `misc_field` tag, which no Act-1 capture can witness (only Malleable,
  Invincible, Flight, Combust and Echo Form declare the union's members) and
  which an Act-2 crossing witnesses automatically via Byrd or Shelled
  Parasite. Say so in this block's Log.
  **Deps:** S3.21, S3.22 **Acceptance:** each of the captures above replays
  **zero-diff** to its terminal through `replay_run_diff --replay` with zero
  capture-race records, and the emerald pair shows the two runs' Act-2/3 maps
  **differing** (the positive control that the gate is being modelled, not
  accidentally matched); S3.11's `UNVERIFIED-until-captured` marker
  discharged **in this task's commit**; zero untriaged findings.
  **Log:** —

- **S3.24** `[x]` ∥ **The Courier, fully.** Both halves of the standing
  refusal, landing together (the row's own condition). Sim side: draw the
  restocked colored card's identity from a dedicated seeded stream
  reproducing retail's uniform draw over the eligible (rarity, type) pool —
  the exact eligibility and duplicate loop re-derived from `ShopScreen` and
  `CardGroup.getRandomCard` at task time — and lift the
  `kShopRestockedUnknownCard` buy-refusal. Fork side: the patch that makes
  the one retail call consume the same seeded stream, under the established
  patched-fork oracle-contract precedent (the Discovery wasted-regens /
  Explosive-Potion THORNS boundary: the contract is the patched fork, not the
  retail client). The patch is **handed to S3.21** to ride the single
  redeploy.
  **Inherited:** the s2-tasks "Courier's restocked colored-card identity" row.
  **Deps:** — **Acceptance:** six presets **build**; the committed corpora
  replay zero-diff (no shop in them restocks, so this must be a no-op there
  and a RED means the stream was threaded wrong); the patch reviewed and
  handed over with its own before/after description. The zero-diff **restock
  capture** that witnesses the draw is scheduled by S3.62 and named here;
  until it lands the row is `UNVERIFIED-until-captured`.
  **Log:** 2026-09-03 — **both halves landed together; the row is
  `UNVERIFIED-until-captured` pending S3.62's restock capture.**

  *Sim side.* The colored restock's identity now comes from
  `courier_restock_stream(run_seed, card_rng.counter)`
  (`include/sts/engine/shop.hpp`), a stream **constructed at the draw and held
  nowhere**: seed `run_seed + kCourierRestockSeedOffset(1000003) +
  cardRng.counter`, read after the restock's own `rollRarity` draw, then ONE
  index draw through the shared `shop_card_from_pool` walk — the same
  type-filtered, game-id-sorted view, the same downward fallthrough / POWER
  recursion, the same "an empty view costs no draw" rule
  (`CardGroup.java:539-553`, `AbstractDungeon.java:1538-1576`). Because the
  stream is derived rather than stored, **no `RunState` byte is added, no
  `ByteClass` row changes and `SCHEMA_VERSION` does not move** (the 8→9 bump
  stays S3.31's alone), and `cardRng`/`merchantRng`/`potionRng` motion per
  restock is byte-for-byte what wave2cap_courier_* measured. The
  `kShopRestockedUnknownCard` sentinel and its buy-refusal are **deleted**: a
  restocked slot is a real `CardId`, egg-previewed, priced off its own drawn
  rarity, on the legal-action mask (still derived from public state alone —
  the whole shelf is public on room entry), and buyable, so a colored slot can
  now restock repeatedly in one visit. `run_advance.hpp`'s unimplemented list,
  `public_view.hpp` and
  [public-view-audit.md](public-view-audit.md) lost the deviation with it.

  *Fork side.* `patches/CourierRestockSeedPatch` — a `@SpireInstrumentPatch`
  on `ShopScreen.purchaseCard(AbstractCard)` whose `ExprEditor` replaces the
  two `AbstractDungeon.getCardFromPool` calls at `ShopScreen.java:615-617`
  with a helper that makes the identical `new Random(Settings.seed + 1000003L
  + cardRng.counter)` draw; flag `oracleCourierRestockSeed`, default on, off =
  retail bit for bit (so it joins `oraclePlaytimePin` and the three strip
  flags in the equivalence baseline). Under the **patched-fork
  oracle-contract precedent** (Discovery wasted-regens, Explosive-Potion
  THORNS, the SecretPortal playtime pin): the contract is the patched fork,
  not the retail client. The jar was built to prove the patch compiles
  (`build_fork.ps1 -NoDeploy`, JDK 8 — `CommunicationMod-oracle.jar`
  carrying `CourierRestockSeedPatch.class`) and **deliberately not deployed or
  committed**; S3.21 owns the single redeploy, the recorded SHA-256 and
  `PROTOCOL.md` §5.5. Hand-over:
  [../tools/oracle_bridge/communicationmod-oracle/COURIER-RESTOCK-HANDOVER.md](../tools/oracle_bridge/communicationmod-oracle/COURIER-RESTOCK-HANDOVER.md).

  *Evidence.* Six presets **build** (`win-debug`/`win-asan`/`win-release` via
  the vcvars64 + LLVM wrapper; `debug`/`asan`/`release` through
  `tools/wsl_run.sh --script`, configure + build, no ctest per the 2026-09-03
  owner directive). Both committed corpora replay **zero-diff** through
  `replay_run_diff --replay --stop-on-diff` in WSL — 50/50
  `act1_a20_50` and 5/5 `three_act_a20_5`, the expected no-op since no shop in
  either corpus restocks, and the check that the new stream was threaded
  without disturbing anything else. The fork patch's **seam** was verified
  offline the way `OraclePlaytimePinPatch` was: ModTheSpire's own
  `InstrumentPatchInfo.doPatch` sequence run against the real `ShopScreen`
  bytecode from `desktop-1.0.jar` gives 2 `getCardFromPool` calls before,
  `instrumentedCalls == 2`, 0 `getCardFromPool` / 2 `restockCardFromPool`
  after, `getColorlessCardFromPool` still 1 and `setPrice` still 2, and the
  class still recompiles. `check_stale_counts.sh` / `check_doc_links.sh`
  clean.

  *Owed — the capture S3.62 must schedule.* A **Courier restock capture**: a
  seed on which the driver owns **The Courier** (a SHOP-tier relic — so a
  shop stocking it, or a Neow/boss grant) and then reaches a shop with enough
  gold to buy a **colored** card **twice**, the second purchase being the
  restocked row. Find it by filtering `seed_scan` for a shop floor plus the
  relic, then script it through `driver/script_policy_cmd.py` buying **by
  name** (a restock renumbers the choice list under a script written in
  advance — the wave2cap_courier_* runbook §4 already does this). Acceptance:
  the capture replays zero-diff through `replay_run_diff --replay` **and**
  `--shop`, with the restocked row's id matching. Until then this row reads
  `UNVERIFIED-until-captured`.

## Phase S3.3 — The Act-3 terminal and the Act-4 run layer

- **S3.31** `[ ]` **The `Spire Heart` dialog, the Door, and the run-outcome
  kind.** The room S2 collapsed. `goToVictoryRoomOrTheDoor`
  (ProceedButton.java:199-208) as a **full room transition** — `++floorNum`,
  the trap-7 five-stream reseed, the relic `onEnterRoom` fan-outs (Maw Bank's
  +12 gold), design §5 trap 11 — into `VictoryRoom(EventType.HEART)`
  (VictoryRoom.java:21-33), which grants nothing and runs the `Spire Heart`
  event. The event's four clicks and its key gate
  (SpireHeart.java:118-188; the gate at :151 tests `isFinalActAvailable` and
  all three keys), the **DEATH arm** as the Act-3-stop terminal (:170-177) and
  the **GO_TO_ENDING arm** into `goToFinalAct` → `DoorUnlockScreen.open(true)`
  → `nextDungeon = "TheEnding"` + `isDungeonBeaten = true`
  (SpireHeart.java:94-98, DoorUnlockScreen.java:143-161). Replaces the boolean
  `run_is_victory` (run_advance.hpp:975-1000) with
  `RunVictoryKind { NONE, ACT3_STOP, HEART }` — the game's own independent
  `victory` / `trueVictor` pair (Metrics.java:82,107; DeathScreen.java:291-299
  vs VictoryScreen.java:254-269) — keeping `run_is_victory()` as
  `kind != NONE` so no existing consumer breaks. Claims `RoomType` **9**
  `Victory` and the `RunPhase` **12** contingency (the preferred model rides
  `EVENT_DIALOG` with the registry event body; release 12 if unspent).
  **Owns the one `SCHEMA_VERSION` 8 → 9 bump** — the outcome kind, the Act-4
  floor base S3.32 will write, and S3.11's reward-row kinds if they did not
  fit a pad carve; prefer a pad carve, tail-append as the fallback, on the
  S2.47 precedent, and regenerate the Stage-A fixtures exactly once here via
  the checked-in generator. Decide, record and implement the clicks-1-and-2
  collapse question (deferred row) — the differ compares record counts.
  **Deps:** S3.21 **Acceptance:** six presets **build**; the 20 Stage-A
  fixtures and the golden vectors byte-identical in meaning after the single
  sanctioned regeneration, with the schema.hpp version-log entry; the
  committed Act-1 and three-act CI corpora replay **zero-diff** — and the
  three-act corpus is the live surface here, because its two double-boss
  victories now run **through** a `Spire Heart` room the engine previously
  ended before, so their trailing records stop being skipped and start being
  compared (that is the whole point, and a RED is a finding, not a blocker);
  `check_doc_links.sh` clean.
  **Log:** —

- **S3.32** `[ ]` **Act-4 construction, the special map, and the crossing.**
  `TheEnding`'s constructor chain (TheEnding.java:40-52): the frozen
  `AbstractDungeon` order with `dungeonTransitionSetup` (`++actNum` to 4, the
  cardRng counter snap, the list clears, `blizzardPotionMod = 0`, **the A5
  between-act heal**), `initializeLevelSpecificChances` (:145-160, design
  §2.6's live-and-dead table), `mapRng = seed + actNum*300 = seed + 1200`
  (:49), and `generateSpecialMap` (:72-139) — the hand-built 5×7 map with
  rooms only in column 3, one-directional rest→shop→elite edges plus the
  explicit elite→boss edge, and a `TrueVictoryRoom` node with **no inbound
  edge**. `generateMonsters` / `initializeBoss` are fixed lists with **no
  RNG** (:162-196) and the `getXForRoomCreation` readers never pop
  (AbstractDungeon.java:1846-1862). `setEmeraldElite` never runs in Act 4 (its
  only call site is inside `generateMap`). Moves `engine::kFinalAct` **3 → 4**
  and **audits every reader** — `run_is_victory`, the fuzz `kActBuckets`
  sizing, `event_framework.hpp`'s per-act event-list fall-through, the
  planner's `kMaxActs` — because an unaudited reader makes an Act-4 run
  silently read Act-1 tables. Writes the **Act-4 floor base as run state at
  the crossing** (51 below A20, 52 at A20 — design §4.3) and makes
  `run_cur_row` read it. Resolves the first-row map-choice width and the
  elite→boss action kind against the fork (deferred rows).
  **Deps:** S3.31 **Acceptance:** six presets **build**; the committed corpora
  replay **zero-diff** (Acts 1–3 are untouched by this task and a RED means
  the `kFinalAct` audit missed a reader — that is exactly what this
  acceptance is for); the fixtures and golden vectors byte-identical;
  `check_stale_counts.sh` / `check_doc_links.sh` clean. Act-4 behaviour is
  `UNVERIFIED-until-captured` at landing, discharged by **S3.62**, whose
  needed captures this Log names: an Act-4 entry at A20 and one below A20
  (the floor pair needs both bands), with the `mapRng`/`monsterRng` counters
  compared on the first Act-4 record (design §5 traps 2 and 3).
  **Log:** —

- **S3.33** `[ ]` **The Act-4 rooms and the true-victory terminal.** The four
  playable rooms as ordinary rooms with their Act-4 specifics: the **rest**
  (3,0) with **no Recall option** (reaching Act 4 implies the ruby key, so
  `!hasRubyKey` is false — CampfireUI.java:94-96) and everything else the S1
  model; the **shop** (3,1) unchanged but for cosmetics
  (ShopRoom.java:45, ShopScreen.java:132-136); the **elite** (3,2) with its
  full reward set — gold `treasureRng.random(25,35)`, the relic at
  `MonsterRoomElite`'s own hard-coded tier thresholds (:100-112), the potion
  roll and the card reward — and with **no** emerald key and **no** emerald
  buff (both gate on a node flag no Act-4 node has); the **boss** (3,3) whose
  `dropReward`/`addPotionToRewards`/reward screen are suppressed
  (AbstractRoom.java:327) but whose **gold add is not** (:286-297 — one
  `miscRng.random(-5,5)`, ×0.75 at A13+, design §5 trap 5). Then
  `goToTrueVictoryRoom` (ProceedButton.java:107-109, :189-197) as a full
  transition into `TrueVictoryRoom` (TrueVictoryRoom.java:20-32,
  `NO_INTERACT`) — the engine's terminal, with the cutscene as presentation,
  the S3 analogue of S2's post-victory skip. Claims `RoomType` **10**
  `TrueVictory` (`kRoomTypeCount` → 11 with its `static_assert`). Pins the
  design §2.6 dead constants and the design §4.6 A20 negatives (no double
  boss in Act 4, no elite quota) as recorded negatives with their citations.
  Note the floor-gated `canSpawn` relic family is now rejecting almost
  everywhere (floors 51+, s2-design §5 trap 9).
  **Deps:** S3.32 **Acceptance:** six presets **build**; committed corpora
  zero-diff; fixtures byte-identical. Act-4 room behaviour is
  `UNVERIFIED-until-captured`, discharged by **S3.62**: an Act-4 rest, an
  Act-4 shop purchase, an Act-4 elite reward claim and the Heart's terminal
  gold, each named here.
  **Log:** —

## Phase S3.4 — Act-4 content

- **S3.41** `[ ]` ∥ **Registry rows for Act 4.** The whole content
  denominator, in one small task because it is ten rows: `encounters.yaml`
  62–63 (`Shield and Spear` ELITE, `The Heart` BOSS — act 4, **no weights and
  no pool draw**, the codegen emitting act-4 tables as constants);
  `events.yaml` 52 `SPIRE_HEART`, a row belonging to **no** act's draw list
  (a new, explicitly-declared property — it must be excluded from every
  membership bitset so `event_flags`/`event_flags_hi` stay byte-comparable
  across the crossing); `powers.yaml` 136–139 identity rows (Surrounded, Back
  Attack, Beat of Death, Invincible — bodies in S3.42/S3.43);
  `monsters.yaml` 67–69 with their HP bands and full move/`getMove` programs
  read from source in full; `a20.yaml` notes refreshed on the six rows of
  design §4.6 plus the two Act-4 negatives. Records the negatives that keep
  the inventory honest: no new `MonsterIntent` (all six telegraphs exist), no
  new card or relic row, no `rolls:` column anywhere (Act 4 spends no
  `monsterHpRng`), and `FocusPower` deliberately unregistered (orb-gated,
  Ironclad-unreachable, S4).
  **Deps:** S3.32 (the act-4 dimension exists) **Acceptance:** codegen
  deterministic and its emitted headers byte-stable across two runs; six
  presets **build**; committed corpora zero-diff (Acts 1–3 emit nothing new,
  so a RED means the act dimension was extended destructively rather than
  additively); `check_stale_counts.sh` / `check_doc_links.sh` clean. Every
  row lands `UNVERIFIED-until-captured` and names **S3.62** as the witness —
  which for ten Act-4 rows is two captures, not ten (one Shield-and-Spear
  fight, one Heart fight), and the Log says so explicitly rather than
  implying a per-row capture debt.
  **Log:** —

- **S3.42** `[ ]` **Shield and Spear.** The Act-4 elite, both actors and the
  two flag powers. **SpireShield** (SpireShield.java:36-176): fixed HP
  110/125@A8, damage 12/34 → 14/38@A3, `FORTIFY_BLOCK` 30, the
  pre-battle `SurroundedPower` **on the player** plus Artifact 1/2@A18, the
  three moves in their exact `addToBottom` order — including SMASH's block
  from `damage.get(1).output` (the **post-power** output) below A18 and a flat
  99 at A18+ — and the `moveCount % 3` cycle whose case 0 is an
  `aiRng.randomBoolean()` coin flip and whose case 1 reads `lastMove(1)`.
  **The BASH orb branch (:86-91) is short-circuited behind
  `!player.orbs.isEmpty()`, so an Ironclad never consumes its
  `randomBoolean()` and always takes the `StrengthPower(-1)` arm** — model the
  short circuit, not the outcome, and record why. **SpireSpear**
  (SpireSpear.java:37-183): fixed HP 160/180@A8, damage 5/10 → 6/10@A3 with
  `skewerCount` 3 → 4@A3, Artifact only at pre-battle, BURN_STRIKE's two hits
  plus **2 Burns to the discard below A18 and to the draw pile at A18+**,
  PIERCER's +2 Strength to **every** monster including itself, SKEWER's
  `skewerCount` hits, and its own `moveCount % 3` cycle. **Both `die()`
  bodies are identical and remove `Surrounded` from the player and
  `BackAttack` from the survivor** (SpireShield.java:164-176 ==
  SpireSpear.java:171-183) — design §5 trap 7, kill order is observable.
  Powers 136 (Surrounded — a pure player-side flag, no hooks) and 137 (Back
  Attack — applied by `AbstractMonster.applyPowers` itself, :998-1002, not by
  either monster), with the **1.5× multiplier hard-coded in
  `AbstractMonster`** at :982-984 (intent) and :998-1013 (real hit), not in
  either power. Resolves the facing question (deferred row) against the three
  cited methods read in full.
  **Deps:** S3.41 **Acceptance:** six presets **build**; committed corpora
  zero-diff (no Act-1/2/3 encounter contains either actor, so this must be a
  no-op there); fixtures byte-identical. `UNVERIFIED-until-captured` until
  **S3.62** delivers the two kill-order captures the design §5 trap 7 witness
  requires, replayed `--combat` zero-diff; named here.
  **Log:** —

- **S3.43** `[ ]` **The Corrupt Heart.** The final boss
  (CorruptHeart.java:49-211). Fixed HP 750/800@A9, damage 40/2 → 45/2@A4 with
  `bloodHitCount` 12 → 15@A4. Pre-battle (:88-103): `InvinciblePower(300)`,
  **200 at A19+**, and `BeatOfDeathPower(1)`, **2 at A19+** — and **no
  Artifact at battle start**. `getMove` (:171-200): the **first roll returns
  DEBILITATE and returns early, so `moveCount` is not incremented on it**,
  then `moveCount % 3` with a coin flip at case 0, a `lastMove(2)` read at
  case 1 and an unconditional buff at case 2. DEBILITATE (:108-119): Vulnerable
  / Weak / Frail 2 each with `isSourceMonster = true`, then **five status cards
  shuffled to RANDOM draw-pile positions** in the order Dazed, Slimed, Wound,
  Burn, Void. The buff move (:120-151): **negate any negative Strength first**
  (:121-124), always `+2` net Strength, then the `buffCount` ladder
  0→Artifact 2, 1→Beat of Death +1, 2→Painful Stabs (existing power id 97,
  second producer), 3→Strength 10, **≥4→Strength 50 forever**. Powers 138
  (Beat of Death — `onAfterUseCard` → one THORNS `DamageAction` per card
  played, binding the existing `on_after_use_card` hook 16) and 139
  (**Invincible** — `onAttackedToChangeDamage` caps and drains,
  `atStartOfTurn` restores `amount = maxAmt`, `priority = 99`; design §5
  trap 9 puts it at the engine's `apply_buffer` site between `decrementBlock`
  and the `onAttacked` fan-out, **not** in `atDamageFinalReceive`). May claim
  the power `Hook` **18** contingency if a native binder at that site is not
  the cleaner shape; release it otherwise. Note the interaction the fight
  turns on: Beat of Death is THORNS-typed, so it does **not** trigger Painful
  Stabs (PainfulStabsPower.java:40-44 excludes THORNS) — Blood Shots does, one
  Wound per landed hit.
  **Deps:** S3.41 **Acceptance:** six presets **build**; committed corpora
  zero-diff; fixtures byte-identical. `UNVERIFIED-until-captured` until
  **S3.62** delivers the Heart capture, replayed `--combat` zero-diff, in
  which a single hit exceeds the Invincible pool and a later turn's hit lands
  after the restore (the trap-9 witness) and the `buffCount` ladder reaches at
  least step 3; named here.
  **Log:** —

- **S3.44** `[x]` ∥ **Pump/combat-over ordering: THORNS retaliation on the
  killing blow.** The owner-approved stage-b obligation, taken now because Act
  4 makes it consequential: the live game applies THORNS retaliation even when
  the damage kills its source, and the sim's pump drops the queued retaliation
  at combat-over. Fix the ordering at the pump/combat-over seam, with the
  fixture and corpus impact assessed rather than discovered. Motive beyond the
  original witness: `BeatOfDeathPower` queues a THORNS hit after **every**
  card the player plays (BeatOfDeathPower.java:40-44), so the Heart fight
  reaches this seam on every lethal turn, and S2.49's attacker-side cancel
  work established the neighbouring semantics with THORNS deliberately
  exempted.
  **Inherited:** the stage-b "Sharp Hide THORNS retaliation on the killing
  blow" row.
  **Deps:** — **Acceptance:** **STS420252** (the te1_survival_b160 promoted
  reproducer, hp 22 sim vs 26 game to terminal) replays **zero-diff** through
  `replay_run_diff --replay --combat` where it previously diverged — the
  witness already exists on disk, so this row is not
  `UNVERIFIED-until-captured`; the committed Act-1 and three-act CI corpora
  still replay zero-diff (a change to terminal adjudication is exactly the
  kind that moves other captures, and if one moves, that is a finding to
  triage, not a number to accept); six presets **build**; Stage-A fixtures
  regenerated only if the change is proven to alter them in meaning, with the
  reason recorded.
  **Log:** 2026-09-03 — landed. **Two findings, and the first one re-scoped the
  task.**
  **(1) The inherited defect was already closed, by two S2 tasks that landed
  after TE.1 wrote the row and never re-owned it.** TE.1 recorded STS420252 on
  **2026-08-03**. `86fc2be` (S2.43 residual wave 2) restored the four-arm
  `clearPostCombatActions` survivor set — `HealAction || GainBlockAction ||
  UseCardAction || actionType == DAMAGE` (GameActionManager.java:130-137) — and
  its own derivation names *"the Guardian's Sharp Hide onUseCard retaliation,
  addToBot'd after Blood for Blood's own damage"* and *"seed STS431342: 4 hp,
  the Sharp Hide amount"*; `d57e077` (S2.49) exempted THORNS from the
  attacker-side cancel (`damage_attacker_cancelled`, DamageAction.java:69-73).
  Together those are exactly the fix this row asked for, so a THORNS
  `DamageAction` already sitting in the queue when the field empties has been
  landing since `86fc2be` (2026-08-26); `45f9528` (2026-08-27) then settled the
  player-death arm on top of it.
  **(2) The Acceptance's witness is not on disk.** The whole
  `te1_survival_b160_20260803_420000_420499` campaign group — the promoted
  `STS420252.reproducer.json` with it — has been removed from the §7.3 data
  root; `find` over `D:` to depth 8 and `E:` to depth 6 for `*te1_survival*` /
  `STS420252*` returns nothing, and the reproducer was never promoted into
  `tests/golden/oracle_reproducers/` (that directory still holds only
  `b14-living-wall-obtain-race`). The Acceptance's premise ("the capture that
  proves the fix is already on disk") is therefore **false as written**, and the
  row's `hp 22 (sim) vs 26 (game)` is transposed against its own prose: a
  *dropped* retaliation leaves the SIM higher, not lower.
  **The witness was replaced by a cohort of the identical shape, not assumed
  away.** 184 on-disk captures carry a `Sharp Hide` power; a scripted scan over
  them for the exact witness shape — a `play <attack>` command issued while a
  Defensive-Mode Guardian holds Sharp Hide 4 at positive HP, whose next record
  has the field empty — found **21 instances**, **STS431342 among them**. All 21
  replay through `replay_run_diff --replay --combat --vitals` with `first
  divergence: none`, at the base commit and after this change, and the log sets
  are **byte-identical before and after** (`diff -r`). The one file whose exit
  code is non-zero, STS210868, stops on an unrelated Act-3 Writhing Mass
  `Writhe` placement at seq 629 / floor 50 (hand vs discard) — run-level `first
  divergence: none`, identical on both sides of the change, and it is a finding
  for whoever owns Writhing Mass, not for this row.
  **What actually changed** is the residual half of the same seam, which is this
  row's stated Act-4 motive.
  `resolve_pending_post_combat_actions_at_terminal` (action_queue.cpp) resolved
  the survivors out of a SNAPSHOT taken before the drain while the ring held the
  *abandoned* actions — so every action a survivor QUEUED while resolving landed
  in a ring nothing would pop again, and a survivor's `addToTop` never ran at
  all. The Java keeps popping: `clearPostCombatActions` prunes the queue once,
  and `AbstractRoom.update` then drains `actions` to EMPTY, because the COMPLETE
  transition is gated on `actions.isEmpty()` (AbstractRoom.java:277) behind a
  deathTimer-gated `endBattle()` (AbstractMonster.java:866-871). The resolver
  now rebuilds the ring **as** the survivor list and pops the front until it is
  empty, the player is dead, or a four-ring step bound is hit (the Java's drain
  is frame-driven and unbounded; a headless resolver must not be able to spin).
  Everything a survivor queues is re-filtered through the **same** four-arm set —
  the clear is not one-shot, every damage-shaped action re-calls it while the
  field is empty (DamageAction.java:88-91 and its 19 siblings) — so **no action
  class starts resolving at a terminal that did not resolve there before**, and
  the victory-terminal survivor set is untouched. S2.49's attacker-side cancel is
  untouched too: THORNS is still exempt, and the drain runs through
  `execute_opcode`, so the predicate stays live.
  **The THORNS shape this buys is Act 4's.** `BeatOfDeathPower.onAfterUseCard`
  addToBot's one THORNS `DamageAction` at the player per card played
  (BeatOfDeathPower.java:40-44), and `onAfterUseCard` fires from
  `UseCardAction.update` (:77-88) — i.e. **from a survivor**. Under the snapshot
  form the Heart's killing card dropped its own retaliation; under the drain it
  lands, in queue order, before the pump adjudicates.
  **Evidence** (build + real-run only, per the 2026-09-03 directive — no gtest
  written, no ctest run): committed corpora `tools/corpus_replay.sh` —
  `act1_a20_50` and `three_act_a20_5` **ZERO-DIFF**, both injected-divergence
  controls fail loud, identical before and after; `--vitals` over all 55 corpus
  captures and the 21-instance cohort — no vitals divergence introduced; the 20
  Stage-A fixtures replayed through the engine by `fixture_oracle_test` run
  **directly** (not via ctest) — 3/3, zero diffs; `fuzz_soak --seeds 3000
  --threads 6` — **24,000 cases, 3,286,411 counted actions, 142,082 combats
  entered / 113,876 killed / 23,620 deaths / 788 Smoke-Bomb escapes, failures:
  0**, which is the drain's termination and determinism bar (every case replayed
  to an equal content hash, `no_progress: 0`); six presets **build** (`debug`,
  `asan`, `release` via `wsl_run.sh --script tools/build_presets.sh`;
  `win-debug`, `win-asan`, `win-release` through a vcvars64 + LLVM wrapper);
  `check_stale_counts.sh` and `check_doc_links.sh` clean.
  **Stage-A fixtures: NOT regenerated, and the reason is recorded.** The change
  touches no struct, no `SCHEMA_VERSION`, and no mechanic the fixtures reach —
  the scripted Jaw Worm fights never queue a survivor-class action during a
  terminal — and the generator is the independent reference simulator, which
  this change cannot reach at all. `fixture_oracle_test`'s zero-diff replay of
  the committed traces through the changed engine is the proof, not the
  assumption.
  **`UNVERIFIED-until-captured` for the Beat of Death instance**, named here
  rather than left implicit: Act 4 content is not landed, so in Acts 1–3 the
  drain has no observable consumer, which is exactly why every capture on disk
  is byte-identical across the change. **S3.62**'s Heart capture must replay
  `--combat` zero-diff on a lethal turn — a turn where the killing card's
  `BeatOfDeathPower` retaliation lands — and that is this row's outstanding
  witness.
  **Discharged in place:** the stage-b "Sharp Hide THORNS retaliation on the
  killing blow" row, with the same two findings recorded there.

## Phase S3.5 — Information layer and harness

- **S3.51** `[ ]` **PublicView, KnowledgeState and the belief sampler for
  keys + Act 4.** The consumer-facing half of S3. `keys_reserved` and
  `act_reserved` are **already populated** (public_view.hpp:438-446), so the
  additive tail is the run-outcome kind (S3.31's `RunVictoryKind`), the Act-4
  map shape and the reward-row key rows; **owns the one
  `PUBLIC_VIEW_VERSION` 6 → 7 bump**. The legal-action mask is an observation
  channel (training-plan §2.1), so the key claim rows and the Act-4 map choice
  enter the hashed public serialization. `resample_hidden`'s contract gains
  exactly one fact — **Act 4's content is public and constant**, so a fake
  Act-4 future is deterministic, which narrows the posterior rather than
  widening it and touches none of the four declared coarsenings.
  [training-contract.md](training-contract.md) updated with the new fields and
  its field-by-field completeness audit re-run;
  [docs/public-view-audit.md](public-view-audit.md) extended.
  **Deps:** S3.33, S3.43 **Acceptance:** the GT0 leak gates re-run green as
  the **replay-and-compare instruments they are** — hidden-twin byte equality
  in every phase including the Act-4 ones, the total-byte classification
  tripwire over `RunController`, the sampler distributional suite, the
  grep-enforced omniscient boundary; `twins_v1.bin` regenerated via its
  checked-in generator; six presets **build**; committed corpora zero-diff;
  `check_doc_links.sh` clean. A leak gate is not a unit test — it plays runs
  and compares bytes — and the 2026-09-03 directive does not retire it.
  **Log:** —

- **S3.52** `[ ]` ∥ **Fuzz/soak across four acts.** The determinism and reach
  instrument, extended: per-act buckets to act 4 (the `kActBuckets` slot has
  been reserved on purpose since S2 — `coverage.hpp:80-88`), the Act-4 room
  kinds, **per-key acquisition counters**, the Spire-Heart branch taken, and
  the run-outcome kind at terminal. Claims fuzz `MoveCat` **32–35**
  (`REWARD_CLAIM_KEY`, the Act-4 map choice, the Spire-Heart dialog, one
  reserve; `COUNT` → 36). The counters exist so that "we never got there" and
  "we got there and did not count it" stay distinguishable — the rule the
  `kActBuckets` comment already states and the shop-entry hole already cost
  this tool once.
  **Deps:** S3.33 **Acceptance:** a soak of ≥ 10M counted actions at A20 with
  **zero** nondeterminism (replay-twice hashing), zero asserts, zero
  `room_unimplemented` and zero `no_legal_moves`/`livelock`/`no_progress`,
  reported with its per-act and per-key witnesses **including the zeros and
  their positive control** (an E0 policy will not reach Act 4, and saying so
  with `terminal_act_sum == runs_counted` beside it is the honest form —
  the S2.41/S2.45 precedent); six presets **build**; soak artifacts
  uncommitted under the campaign data root.
  **Log:** —

- **S3.53** `[ ]` ∥ **Close the two replay blind spots.** Under the evidence
  rule the replay differ *is* the acceptance surface, so its blind spots are
  the project's blind spots. Two, both inherited. (a) **In-combat card
  costs:** `--replay` compares `RunState` and therefore not card cost state,
  which is why a whole cost-state family reached the S2 depth wave undetected
  ([verification/s2-verification.md](verification/s2-verification.md) §9 limit
  2). Extend the comparison to per-instance in-combat costs across every pile.
  (b) **Event-grid legal-action masks:** for every screen presenting a
  card/relic grid — Neow, campfire Smith/Toke, event grids, the shop purge
  grid, the boss-relic screen — compare the engine's **mask** against the live
  `ChoiceScreenUtils` candidate list on a real capture, and make that
  comparison an acceptance surface rather than an inspection. Motivated by
  The Library GRID identity and Match-and-Keep index-space findings, and by
  the two index spaces `command_map` already documents.
  **Inherited:** the s2-tasks `--replay` card-cost row and the
  audit-event-grid-masks chip (see Deferred obligations, incl. the note that
  the chip's original scope is not recoverable and is **defined** here).
  **Deps:** S3.21 **Acceptance:** both new comparisons run over the committed
  Act-1 and three-act corpora with the result **zero-diff**, and each is
  proven to have teeth by an **injected synthetic divergence** that it catches
  and names (the `OracleCorpusReplay.ThreeActInjectedSyntheticDivergenceFailsLoud`
  pattern — a comparison nobody has seen fail is not a comparison); six
  presets **build**.
  **Log:** —

### S3-G1 `[ ]` **Gate: S3 rules complete** — tag `s3-g1-content`
**Deps:** all S3.1x, S3.2x, S3.3x, S3.4x, S3.5x
The design §6 S3-G1 bar, checked literally: every §2 inventory row landed and
either witnessed by a zero-diff capture or explicitly
`UNVERIFIED-until-captured` with its blocking prerequisite named; every §5
trap discharged by its Witness clause or carrying the same marker; the
`a20.yaml` rows IMPLEMENTED with the A20 capture that exercises each named;
the ≥ 10M-action four-act soak clean with its per-key and per-act witnesses;
six presets **building** green with the Stage-A fixtures, golden vectors and
both committed CI corpora byte-identical / zero-diff; the information layer at
`PUBLIC_VIEW_VERSION` 7 with the GT0 leak gates green;
`check_stale_counts.sh` + `check_doc_links.sh` clean. **The gate publishes the
full `UNVERIFIED-until-captured` list** — that list is S3.62's work order, and
a gate that cannot print it has not been checked. Then: update CLAUDE.md
"Current state".
**Log:** —

## Phase S3.6 — Verification campaigns + S3 exit

- **S3.61** `[ ]` ∥ **Reach re-measurement on the landed engine.** S3.22's
  reach numbers were measured against an engine with no Act 4; re-run its
  commands verbatim on the gated tree so the depth wave schedules from real
  cohorts. Report the key-carry, Act-4-entry, Shield-and-Spear-kill and
  Heart-kill rates, and emit the triple list S3.62 schedules from.
  **Deps:** S3-G1 **Acceptance:** the S3.22 report regenerated from the same
  commands with the deltas explained rather than absorbed; determinism sweep
  zero mismatches; every emitted cohort script replaying to its recorded
  `final_hash`; six presets **build**.
  **Log:** —

- **S3.62** `[ ]` **Oracle campaigns: breadth + Act-4 depth.** The design §6
  S3-G2 evidence. Breadth: ≥ 2,000 distinct mixed-policy A20 attempts, all
  triage per the Stage B process, zero untriaged, zero open. Depth, scheduled
  off S3.61's triples: the `Spire Heart` dialog on **both** branches (a key
  deliberately not taken → the Act-3 stop; all three keys → the Door); an
  Act-4 entry continuing through the rest, the shop and the elite; the
  **Shield and Spear killed** with a four-item reward screen (trap 6) and with
  **both kill orders** (trap 7); and **≥ 1 complete A20 Heart kill** to the
  `TrueVictoryRoom` terminal. Simulator-selected seed cohorts are explicitly
  sanctioned (design §6 item 4, the S2-G2 item-3 precedent). This task
  **discharges every `UNVERIFIED-until-captured` marker S3-G1 published**, or
  converts it into an exact per-row disposition with a recorded reachability
  argument — no wildcards.
  **Inherited:** the S3.11 / S3.24 / S3.32 / S3.33 / S3.41 / S3.42 / S3.43
  capture debts, each named in its own Log.
  **Deps:** S3-G1, S3.61 **Acceptance:** every capture above replays
  **zero-diff** to its terminal through `replay_run_diff --replay` (with
  `--combat` on the two Act-4 fights) with zero capture-race records; a
  deterministic dashboard regenerating byte-identically over unchanged inputs
  and reopening every artifact; the `UNVERIFIED-until-captured` list from
  S3-G1 reduced to **zero**; the coverage join exact.
  **Log:** —

- **S3.63** `[ ]` ∥ **Distributional (tier-4) additions.** Pre-registered
  hypotheses, Holm-corrected as one family with the B5.3/S2.44 α discipline
  and the replicate-before-flagging rule: the emerald-gate map divergence
  (trap 1) as a two-arm comparison over paired seeds; the three Act-4 coin
  flips (Shield `getMove` case 0, Spear case 2, Heart case 0) against a
  deterministic surround; the Heart's `buffCount` ladder; and the Act-4
  shop/elite reward draws under the floor-gated `canSpawn` family at floors
  51+. These are run-generating experiments over the engine, not unit tests.
  **Deps:** S3-G1 **Acceptance:** the registered family run at its
  pre-declared scale with `RESULT PASS`, negative controls two-stage rejected,
  and the re-run command recorded in the report.
  **Log:** —

- **S3.64** `[ ]` ∥ **Throughput: the S2 attribution, and the first honest
  whole-run baseline.** Two halves. (a) Discharge the S2.45 attribution
  obligation: the interleaved `tools/bench_ab.sh` A/B of **`d57e077` against
  `646bd18`** on `bench_advance_mask` + `bench_throughput`, two binaries built
  into their own trees, `RESULT: UNMEASURED` an acceptable and reportable
  answer. (b) Re-run the B5.5/S2.45 methodology on the S3 tree: the per-step
  and per-combat floors must hold unchanged, and — because S2's whole-run
  number was explicitly *unquotable* (no weight-free policy leaves Act 1) —
  record the **first whole-run rate over a policy that actually finishes
  runs**, with its corpus and methodology stated so it is comparable next
  time.
  **Inherited:** the s2-tasks per-step throughput attribution row.
  **Deps:** S3-G1 **Acceptance:** release-preset numbers recorded with the
  worst-of-N discipline; per-step and per-combat floors green; the A/B's
  pair-wise deltas, mean, sd and standard error printed by the sanctioned
  script (never two sequential runs).
  **Log:** —

- **S3.65** `[ ]` ∥ **Provenance sweep: the ~60 mirror sites and the four
  citation families.** Comment/provenance only. Bring `src/`, `include/`,
  `tests/` and `tools/` into line with the citations `wave3-citations`
  corrected in `registry/*.yaml`, and resolve the four out-of-range families
  it left open (nine repo-wide; eleven and fifteen in `relics.yaml`; nine in
  `cards.yaml`), each by re-reading the cited method in full and correcting
  the line, not by deleting the citation. A behaviour discrepancy discovered
  mid-sweep is **stop-the-line**, surfaced, not fixed in passing.
  **Inherited:** the stage-b mirror-citation row and its three siblings.
  **Deps:** — **Acceptance:** every `File.java:line` in the swept files
  resolves in `D:\STS_BG_Mod\SlayTheSpireDecompiled`, proven by a committed
  checker run over the tree; six presets **build**; committed corpora
  zero-diff and fixtures byte-identical (a comment-only change that moves
  either is not comment-only); `check_doc_links.sh` clean.
  **Log:** —

- **S3.66** `[ ]` ∥ **Windows CI job.** The stage-b row, promoted by the
  evidence rule: with unit tests retired, CI's job is to prove the six presets
  **build** and that the committed Act-1 and three-act corpora still replay
  **zero-diff**, which is now the entire automated safety net. **Pin the LLVM
  version.** Keep the existing `stale-numbers` job's two steps
  (`check_stale_counts.sh`, `check_doc_links.sh`) and add the corpus replay as
  a first-class step on both hosts.
  **Inherited:** the stage-b "Windows CI job" row.
  **Deps:** — **Acceptance:** a **green run on a real push**, linked — not a
  plausible YAML; the job demonstrated to fail loudly on an injected corpus
  divergence and on a deliberately broken preset, so it is known to have
  teeth.
  **Log:** —

- **S3.67** `[ ]` **Verification report + CI corpus + proactive audit.** The
  S2.46 analogue. Answer the design §6 S3-G2 bar item by item with linked
  evidence; extend the committed CI corpus with a curated **four-act** archive
  (it must contain at least one complete A20 Heart-kill victory and one
  Act-3-stop run, and the builder must re-replay every pick rather than trust
  a stored classification — the v2 format's own rule); extend the proactive
  defect-family audit with every S3-campaign-discovered family, each retaining
  multiple named passing regressions, and re-run the executable audit. State
  the standing limits plainly, as
  [verification/s2-verification.md](verification/s2-verification.md) §9 does.
  **Deps:** S3.62, S3.63, S3.64 **Acceptance:** the report regenerating
  deterministically over unchanged inputs; the new corpus replaying
  **zero-diff** in every preset alongside the Act-1 and three-act archives,
  with its own contract assertions (a four-act corpus that quietly lost its
  Heart kill would otherwise still replay green); the executable audit `PASS`.
  **Log:** —

### S3-G2 `[ ]` **Gate: S3 verified (unblocks training Phase T5 — the headline)** — tag `s3-g2-verified`
**Deps:** S3.61–S3.67, S3-G1
The design §6 S3-G2 bar, checked literally, every item with linked evidence —
including **item 9: zero `UNVERIFIED-until-captured` rows remain**, which is
the 2026-09-03 evidence directive's own gate condition and the one item that
cannot be argued around. Then: update CLAUDE.md "Current state"; notify the
training ledger ([training-tasks.md](training-tasks.md) — T5's `Deps: S3` is
this tag); confirm the deferred-obligations table's dispositions are all
either DISCHARGED in place or explicitly re-deferred with a named owner; S4
planning opens as its own fresh exercise (not claimed here).
**Log:** —

## Parallelism map

```
Wave 1 (now):  S3.11 ∥ S3.24
S3.24 ─▶ S3.21 ; S3.11 ─▶ S3.22
S3.21 + S3.22 ─▶ S3.23          (the keys capture wave; discharges S3.11)
S3.21 ─▶ S3.31 ─▶ S3.32 ─▶ S3.33
S3.32 ─▶ S3.41 ─▶ {S3.42 ∥ S3.43}
S3.44 ∥ S3.65 ∥ S3.66           (no deps; schedule against spare capacity)
S3.33 + S3.43 ─▶ S3.51 ; S3.33 ─▶ S3.52 ; S3.21 ─▶ S3.53
all S3.1x/2x/3x/4x/5x ─▶ S3-G1
S3-G1 ─▶ S3.61 ─▶ S3.62 ; S3-G1 ─▶ S3.63 ∥ S3.64
S3.62 + S3.63 + S3.64 ─▶ S3.67
S3.61–S3.67 ─▶ S3-G2
```

The shape to notice: **the capture instruments (S3.21–S3.23) sit before the
content**, which inverts S2's order. That is design §6.0 consequence 4 — under
the evidence rule, content authored before its capture route exists can only
land as a debt, and S2's own experience is that the campaign is where the real
findings are.

## Change log

- 2026-09-03 — ledger created as the S3 planning exercise the S2-G2 gate Log
  opened. Phases S3.1–S3.6, gates S3-G1/S3-G2, the Wave-1 id blocks, and the
  inherited-obligation dispositions (three accepted from the s2 table, five
  from the stage-b table, six explicitly re-deferred with reasons). Scope
  denominator is [s3-design.md](s3-design.md) v0.1.0.
- 2026-09-03 — **the owner's evidence directive is carried into this ledger
  from creation**: no unit tests; every Acceptance block is build + real-run
  replay evidence; registry rows land with the capture that witnesses them;
  `UNVERIFIED-until-captured` is a first-class status, legal at S3-G1 and
  illegal at S3-G2 (design §6.0, §6 item 9). Two structural consequences: the
  phase order puts the reach instruments ahead of the content, and
  **conventions.md's superseded unit-test wording is recorded as an open
  document conflict with an orchestrator owner** — it must be amended before
  the first S3 brief quotes it.
