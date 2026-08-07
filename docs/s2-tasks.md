# S2 Task Ledger — Acts 2–3 (TheCity + TheBeyond)

Execution tracker for [s2-design.md](s2-design.md) (the S2 scope +
verification spec — this file never overrides it; on conflict the design doc
wins and this file gets fixed). [stage-a-design.md](stage-a-design.md) and
[stage-b-design.md](stage-b-design.md) remain frozen and in force for
everything they cover; [conventions.md](conventions.md) is binding on every
task here exactly as it was for Stage B. Authored by TE.2
([training-tasks.md](training-tasks.md)); the training program's Phase T4 is
blocked on this ledger's exit gate S2-G2.

**This file holds only what is open.** When tasks land, their blocks gain
Log lines; large completed blocks move to an `s2-log.md` archive once one
exists, mirroring the Stage B convention.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
- One sub-agent per task, self-contained brief, own worktree via
  `tools/task_worktree.sh create <task>` (from Windows); orchestrator
  re-verifies and lands on `master` — one task = one commit. Model choice
  per CLAUDE.md.
- A task is **done only when its Acceptance block passes** — run the
  commands, don't infer. Registry YAML is code: entries land with their
  tier-2 tests in one commit.
- Respect `Deps:`; ∥ marks parallel-safe groups (disjoint deliverables).
  Gates **S2-G1 / S2-G2** are stop-the-line for their phase. The gate
  namespace is ledger-local on purpose: the G-series stays reserved for
  Stage C planning (G7's closing note), the GT-series is the training
  ledger's.
- **Cross-ledger shared namespaces** (`RunPhase`, fuzz `MoveCat`,
  `MonsterIntent`, opcodes, `PowerId`/`CardId`/… blocks) remain allocated
  in [stage-b-tasks.md](stage-b-tasks.md) "Shared namespaces — allocation
  now in force" — the single authority. This ledger records only which S2
  task holds which granted block; claim there first, record here second.
- Task body prose before the `**Deps:**` line is the block's Deliverables
  field.
- Capacity rule (training-plan §4.4) still applies: S2 authoring never
  starves open T0.x work; content authoring has zero dependency on
  training results.

## Registry id blocks granted to Wave 1

Granted by TE.2 at ledger creation, recorded per the stage-b protocol
(append-only; unspent ids gap, never backfill; re-derive current maxima
from `registry/*.yaml` before extending):

| Domain | Block | Holder |
|---|---|---|
| `encounters.yaml` | 22–43 (Act 2), 44–61 (Act 3) | S2.01 |
| `events.yaml` | 32–44 (Act-2 list), 45–51 (Act-3 list) | S2.02 |
| `relics.yaml` | 143–150 (151–154 returned unissued — design's ~10 was 8) | S2.03 |
| `cards.yaml` | 128–132 used, 133 reserve | S2.03 |
| `monsters.yaml` | 27–48 (Act 2), 49–66 (Act 3) | S2.2x batches, sub-blocks at dispatch |
| `powers.yaml` | 93–135 | S2.2x batches, sub-blocks at dispatch |

Wave-2 sub-blocks (granted 2026-08-07; full table incl. RunPhase/MoveCat/
RoomType/opcode/hook claims and adjudications is in
[stage-b-tasks.md](stage-b-tasks.md) "S2 Wave-2 allocations"):
`monsters.yaml` S2.21 = 27–30 (+31 reserve), S2.22 = 32–36;
`powers.yaml` S2.21 = 93–94 (HEX, FLIGHT), S2.22 = 95 (MALLEABLE — row
ownership corrected from the S2.21 block text, which stale-listed Malleable/
PlatedArmor/Barricade; the design doc wins).

## Deferred obligations

Same semantics as the Stage B table (live carrier; discharge in place).

| Obligation | Deferred by | Owner task | Detail |
|---|---|---|---|
| Gremlin move-99 escape (`EscapeAction` body + `deathReact`/`escapeNext` trigger) | B3.16 (stage-b table: "UNASSIGNED — Act-2 owner") | S2.23 | Reachable in Act 2 via Gremlin Leader minions; land both halves together and mark the stage-b row DISCHARGED in the same commit |
| `JawWorm(..., true)` constructor variant semantics | TE.2 scope pass | S2.26 | Jaw Worm Horde constructs the variant (MonsterHelper.java:549-551); UNVERIFIED — needs decompile check what the boolean changes (stats? starting Strength?) before the row's tier columns are trusted |
| Rest-site Recall option surface at Acts 2–3 (`isFinalActAvailable`, ruby key) | TE.2 scope pass (s2-design §4.5) | S2.13 | UNVERIFIED — needs decompile check of CampfireUI/rest-option construction for a key-gated option; if present it is on-screen at every Act-2/3 rest and must be modeled as a visible row (grant stubbed to S3) or the oracle diverges on the menu |
| Boss chest + sapphire-key row interaction | TE.2 scope pass (s2-design §4.5) | S2.11 | UNVERIFIED — needs decompile check whether BossChest ever routes the AbstractChest.java:95-96 key-append path (likely not — different open path); pin either way in tier-2 |
| `Lab` in ProceedButton.java:115's combat-event list with no encounter | TE.2 scope pass (s2-design §2.3) | S2.33 | UNVERIFIED — needs decompile check why it is listed; suspected reward-screen plumbing only |
| Exact Act-2/3 entry floors (17/34 assumption) | TE.2 scope pass (s2-design §4.2) | S2.12 | floorNum is continuous (verified: no reset in dungeonTransitionSetup); the exact boundary floor values must come from map-flow reading, not memory |
| Fork redeploy + bottle-taking capture (stage-b table row, "next capture-campaign owner") | wave-runlayer S3 (stage-b) | S2.43 | S2.43 is the next capture campaign; validate the `in_bottle_*` boundary end-to-end and mark the stage-b row DISCHARGED there |

---

## Phase S2.0 — Registry authoring + codegen groundwork (∥; no engine deps; **Wave 1 = S2.01–S2.04**)

Safe to dispatch immediately: append-only ids, disjoint files, tier-2 tests
are table-projection tests that do not require engine consumers. These four
are the "first registry authoring wave" the TE.2 acceptance names.

- **S2.01** `[x]` ∥ **encounters.yaml Acts 2–3 + act-keyed codegen.** All 40
  rows of design §2.1 (Act 2: 5 weak / 8 strong / 3 elite / 3 boss / 3
  event; Act 3: 3 weak / 8 strong / 3 elite / 3 boss / 1 event), weights,
  exclusions (incl. Chosen's two-key exclusion and 3 Darklings'
  self-exclusion), and miscRng composition programs (spawnGremlin ×2 pool
  draw; spawnShapes 3-or-4 draw-without-replacement over the 6-slot pool;
  getAncientShape ×2; fixed lists elsewhere). Extend
  `tools/registry_gen` so `encounter_table.hpp` emits per-act pool tables;
  the emitted `act: 1` tables stay byte-identical. Composition programs may
  need one new node kind for spawnShapes' shared-pool 3/4 draw — reuse
  `{pool: ...}` if it fits; any new node kind is a generator+schema change
  landed with its negative test (unknown node fails loudly).
  **Deps:** — **Acceptance:** tier-2 table tests pin every new row (pool
  membership, weight, exclusion set, program shape) against the cited
  Java, re-read in full; codegen determinism check green; S1 encounter
  table hash unchanged; six presets green.
  **Log:** 2026-08-04 — landed. 40 rows, ids 22–61 (both blocks exactly
  filled); per-(act,pool) `EncounterPoolTable` emission with flat
  `kEncounters` unchanged; NO new composition node kind ({pool:} covered
  spawnGremlin with-replacement, spawnShapes 3/4 without-replacement,
  getAncientShape construct-only); self-exclusion loosening with its own
  negative test. Act-1 byte identity proven by diff (one deleted count
  line) + sha256 of the extracted section + the durable
  `ActOnePoolsUnchangedByTheActExtension`. 9 differential tier-2 tests +
  2 codegen negatives; six presets green. Design-doc §2.1 fixes recorded
  in-row: live Act-3 event key is "2 Orb Walkers" (the "Mysterious
  Sphere" ENCOUNTER key at MonsterHelper.java:582-584 is dead content,
  excluded per S1 practice); TheCity pool citations re-derived
  (weak 94-104 / strong 106-120 / elite 122-130). The per-act weak-draw
  count (Acts 2–3 use generateWeakEnemies(2) vs kWeakSegment = 3)
  is run-layer state deliberately left to S2.12.
- **S2.02** `[x]` ∥ **events.yaml Act-2/3 rows.** 20 identity rows
  (design §2.3) in Java insertion order (TheCity.java:184-199 then
  TheBeyond.java:178-187), `implemented: false`, each with `conditions`
  metadata for its draw gate (Moai Head's idol/hp gate, Colosseum's
  map-position gate, floor gates) and provenance; per-act list-membership
  metadata for the six existing shrine rows and the 14 one-time rows'
  act gates (AbstractDungeon.java:1882-1942) — schema extension mirrors
  how S1 recorded Exordium membership, additive only.
  **Deps:** — **Acceptance:** generated event tables list the new rows
  exactly once with S1 enum values unchanged (static_assert pins);
  membership/gate metadata pinned by tier-2 against the cited lines;
  six presets green.
  **Log:** 2026-08-04 — landed. 20 identity rows (32–44 City, 45–51
  Beyond) in Java add order; `conditions` now mandatory with
  `pool` + `acts` → `EventDef::pool`/`act_mask`/`event_in_act()`;
  SPECIAL masks carry the getShrine act-gate half (list built once in
  Exordium, carried by reference — CardCrawlGame.java:1102-1119).
  16 tier-2 tests + 6 generator negatives; six presets green. Found
  load-bearing: **shrine list order differs between Act 1 and Acts
  2–3** (Wheel of Change last vs first) — Act-1 shrine-bitset
  bit↔position mapping must be re-derived per act, pinned by
  `ShrineListOrderDivergesBetweenActOneAndActsTwoThree`, owned by
  S2.13; also S2.13's: `event_flags` uint32 has no bit for ids 32–51
  (guarded + tested). Beggar's gold ≥ 75 gate authored from source
  (design §2.3 omitted it); Cursed Tome Circlet fallback flagged for
  S2.31–S2.33.
- **S2.03** `[x]` ∥ **relics.yaml + cards.yaml S2 identity rows.** The ~10
  event-relic rows already enumerated in relics.yaml's header commentary
  (Bloody Idol, Enchiridion, Nilry's Codex, Necronomicon, Mutagenic
  Strength, N'loth's Gift, Red Mask, Mark of the Bloom, Cultist Mask,
  N'loth's Mask — SPECIAL/EVENT tier, no `pool_order`), plus the 5 card
  rows (Apparition, Bite, J.A.X., Ritual Dagger, Necronomicurse). Rows
  whose hook bodies are combat-relevant land `native:` only with bodies
  (link-error discipline) — otherwise land acquisition-only metadata with
  the body task named in the row comment, following the S1 "deliberate
  no-op" convention where the Java hook is genuinely out-of-combat.
  **Deps:** — **Acceptance:** tier-2 rows for every new entry (tier,
  sources, unremovability flags for Necronomicurse per CardGroup.java:981 /
  AbstractPlayer.java:744); id pins unchanged for all prior rows; six
  presets green.
  **Log:** 2026-08-04 — landed. 8 SPECIAL relic rows (ids 143–150; the
  design's "~10" counted two FaceTrader faces already landed in S1 —
  correction recorded in the registry block comment, **ids 151–154
  returned unissued**) + 5 SPECIAL card rows (128–132; 133 reserve).
  Both canSpawn gate families were already fully S1-encoded (enumerated
  and cross-checked complete); what S2 changes is reachability
  (S2.11/S2.12). Bodies deferred with named owners except the two the
  acceptance names: Necronomicon on_equip (Omamori/Calling Bell
  precedent) and Necronomicurse's two unremovability sites; Ritual
  Dagger's program deliberately EMPTY (loud) pending its bespoke misc
  opcode. 16 tier-2 tests; six presets green. Ectoplasm provenance
  corrected in place (canSpawn :54-57, onEquip :44-47 — design §2.4 and
  the S1 row were both off). Flagged for S2.31/S2.32: Ghosts A15+ deals
  3 Apparitions not 5; Vampires strips STARTER_STRIKE from the master
  deck. Stale sidetable `_provenance.scope` string noted, left to its
  generator's owner.
- **S2.04** `[x]` ∥ **a20.yaml S2 status refresh.** Update the affected
  rows' notes/status per design §2.5/§4: A5 (between-act heal becomes
  live), A6 (verified run-start-only — negative pinned), A12 (per-act
  0.125/0.25 halving), A13 (boss gold in Acts 2–3 incl. double boss and
  Mind Bloom's 25/50), A20 (double boss). No engine change in this task —
  status text + provenance only, with the engine work owned by S2.12/S2.24
  /S2.28; rows keep S1 status until their owner lands (this task makes the
  ownership explicit, preventing the "someone fixes A12 casually" drift).
  **Deps:** — **Acceptance:** `check_stale_counts.sh` +
  `check_doc_links.sh` clean; registry manifest row count unchanged;
  tier-2 suite green (no behavior change).

## Phase S2.1 — Run layer

  **Log:** 2026-08-04 — landed. Five rows' notes/provenance extended
  (A5/A6/A12/A13/A20) with re-verified citations; S1 STATUS prefixes
  unmoved by design, pinned by `AffectedRowsKeepTheirS1StatusPrefix`;
  new fact recorded: Mind Bloom boss re-fight pays fixed 25/50 gold at
  A13+ with no miscRng draw (MindBloom.java:73-77), ownership split
  S2.24/S2.28/S2.33. 6 tier-2 tests; six presets green; row count
  unchanged (20).
- **S2.11** `[ ]` **Boss chest + boss-relic pick.** TreasureRoomBoss room
  flow after Act-1/Act-2 boss rewards: chest construction at entry, 3
  front-pops of `bossRelicPool` with `canSpawn` recursion + Red Circlet
  fallback, pick/skip semantics (skip burns all three; `noPick` on
  leave), Neow-swap pool-state composition. Claims a `RunPhase` value (and
  fuzz `MoveCat`) via the stage-b namespace table. Design §4.1 + §5 trap 3.
  **Deps:** — (the boss pool is an S1 domain; S2.03's Necronomicon row is
  only needed if a directed test wants its on-equip curse, and that test
  belongs to S2.03 itself) **Acceptance:** unit tests for pop order,
  pool depletion across both consumers, skip, canSpawn rejection
  (Ectoplasm in Act 2 — trap 9), sapphire-key row question resolved and
  pinned (deferred-obligations row); fuzz soak reaches the new phase;
  six presets green.
- **S2.12** `[ ]` **Act transition + Acts 2–3 map generation.**
  `dungeonTransitionSetup` semantics in engine order (design §4.2):
  actNum, **cardRng counter snap** (trap 1), pity resets, list clears,
  `blizzardPotionMod` reset, A5 heal vs full heal; constructor-chain
  ordering (generateMonsters → initializeBoss → …); `mapRng = seed +
  actNum*100/200`; per-act constants (§2.5); `setEmeraldElite` per act;
  monster-list generation for both acts off the continuing `monsterRng`
  (uses S2.01 tables); card-pool rebuild idempotence; one-time list
  carried. Floor continuity (exact boundary floors from source — deferred
  row).
  **Deps:** S2.01 **Acceptance:** named tests per trap (counter-snap
  bands incl. the exact-250 non-snap; A5 arithmetic; pity reset vs
  cardBlizz carry; mapRng offsets per act); a three-act sim run under a
  scripted policy completes deterministically twice with identical
  hashes; six presets green.
- **S2.13** `[ ]` **?-rooms, one-time pool, and rest sites across acts.**
  Per-act event/shrine list rebuild + the one-time pool's cross-act
  depletion semantics; the act-gated one-time draw filters
  (design §2.3); EventHelper pity reset wiring; the Recall-option probe
  (deferred row) resolved and either modeled or pinned absent.
  **Deps:** S2.02, S2.12 **Acceptance:** draw-gate tests per gated row
  (Designer/Duplicator/FaceTrader/Knowing Skull/N'loth/Joust/
  SecretPortal-pinned-false per trap 5); cross-act depletion test (Act-1
  draw removes for Act 2); six presets green.

## Phase S2.2 — Monster batches (each = YAML rows + engine bodies + tier-2, the B3.13–B3.22 pattern; ∥ across disjoint batches once S2.01 lands)

- **S2.21** `[ ]` ∥ City normals I — Chosen, Byrd, Shelled Parasite,
  Spheric Guardian (+ Hex/Flight/PlatedArmor/Barricade/Malleable-family
  power rows their moves pull in).
  **Deps:** S2.01 **Acceptance:** per-monster move/stat tables pinned
  against every ascension branch read in full; encounter compositions
  spawn-order-exact; six presets green.
- **S2.22** `[ ]` ∥ City normals II — Mugger, Snake Plant, Snecko,
  Centurion + Healer (2 Thieves / Snake Plant / Snecko / Centurion and
  Healer / 3 Cultists / Cultist and Chosen groups).
  **Deps:** S2.01 **Acceptance:** as S2.21.
- **S2.23** `[ ]` ∥ City elites — Gremlin Leader (minion mechanics +
  spawnGremlin), Slavers (Taskmaster + S1 slavers), Book of Stabbing.
  **Inherited:** the stage-b Gremlin move-99 escape row (see Deferred
  obligations).
  **Deps:** S2.01 **Acceptance:** as S2.21, plus escape-trigger tests and
  the stage-b row discharged in the same commit.
- **S2.24** `[ ]` ∥ City bosses — Bronze Automaton (+ BronzeOrb, Stasis
  model), The Champ, The Collector (+ TorchHead). A2/3/4-A19 columns per
  boss.
  **Deps:** S2.01 **Acceptance:** as S2.21, plus boss-flag typing
  (Pantograph-style consumers) and A13 gold tests.
- **S2.25** `[ ]` ∥ Beyond normals I — Darkling (Regrow/revival), Orb
  Walker, Repulsor/Exploder/Spiker (3/4 Shapes, Sphere and 2 Shapes).
  **Deps:** S2.01 **Acceptance:** as S2.21.
- **S2.26** `[ ]` ∥ Beyond normals II — Spire Growth, Transient, Maw, Jaw
  Worm Horde (variant-ctor deferred row), Writhing Mass (Reactive +
  master-deck Parasite).
  **Deps:** S2.01 **Acceptance:** as S2.21, plus the master-deck Parasite
  fold-back test.
- **S2.27** `[ ]` ∥ Beyond elites — Giant Head, Nemesis (Intangible +
  Burn), Reptomancer (+ SnakeDagger spawns).
  **Deps:** S2.01 **Acceptance:** as S2.21.
- **S2.28** `[ ]` ∥ Beyond bosses — Awakened One (two phases, Curiosity/
  Unawakened, Void insertion, Cultist adds), Time Eater (TimeWarp/
  DrawReduction/Slimed), Donu and Deca.
  **Deps:** S2.01 **Acceptance:** as S2.21, plus phase-transition and
  TimeWarp turn-economy tests.

## Phase S2.3 — Events closure (B4.11–B4.13 pattern; ∥ across disjoint batches once S2.02 + S2.13 land)

- **S2.31** `[ ]` ∥ City events I (non-combat): Addict, Back to Basics,
  Beggar, Cursed Tome, Drug Dealer, Forgotten Altar, Ghosts, Nest.
  **Deps:** S2.02, S2.13, S2.03 (payout rows) **Acceptance:** per-event
  option/gate/A15 audit against the source read in full; payout rows
  (relics/cards/curses) acquisition-tested; six presets green.
- **S2.32** `[ ]` ∥ City events II: The Library, The Mausoleum, Vampires,
  Colosseum + Masked Bandits (combat embeds), Knowing Skull, The Joust,
  N'loth, Designer, Duplicator (act-gated one-timer bodies).
  **Deps:** S2.02, S2.13, S2.01 (event encounter groups) **Acceptance:**
  as S2.31, plus combat-embed flow tests (two-fight Colosseum sequence).
- **S2.33** `[ ]` ∥ Beyond events: Falling, Mind Bloom (boss re-fight +
  miscRng shuffle — trap 6), The Moai Head, Mysterious Sphere, Sensory
  Stone, Tomb of Lord Red Mask, Winding Halls; SecretPortal pinned per
  trap 5; the `Lab` listing resolved (deferred row).
  **Deps:** S2.02, S2.13, S2.01 **Acceptance:** as S2.31, plus Mind
  Bloom's Act-1-boss re-fight replays zero-diff in a directed capture.

### S2-G1 `[ ]` **Gate: S2 rules complete** — tag `s2-g1-content`
**Deps:** all S2.0x, S2.1x, S2.2x, S2.3x
Checked literally per design §6 S2-G1: registry closure vs the §2
inventory; 100 % tier-2 per the manifest; every §5 trap named-tested;
a20 rows IMPLEMENTED; ≥ 10M-action three-act fuzz soak clean; six presets
green; Stage-A fixtures byte-identical. Then: update CLAUDE.md "Current
state".
**Log:** —

## Phase S2.4 — Verification campaigns + S2 exit

- **S2.41** `[ ]` ∥ **Three-act fuzz soak extension.** B5.1 machinery over
  Acts 1–3: new MoveCats claimed for the boss-relic phase, coverage
  report extended per act; the S2-G1 soak is this task's tooling run at
  gate time.
  **Deps:** S2.11, S2.12 (runs incrementally as content lands)
  **Acceptance:** soak sweep with zero nondeterminism/asserts at
  S2-G1-scale volume; shard/resume paths proven.
- **S2.42** `[ ]` ∥ **Deep-reach scripted drivers + sim pre-scan.** The
  design §6 driver-risk mitigation: extend the TE.1 external-policy
  family for three-act survival (act-aware heuristics; boss-relic pick
  rule; potion discipline), plus the sim pre-scan tooling that selects
  (seed, policy, policy-seed) triples reaching Act-2/Act-3 bosses; if
  measured reach is insufficient for the S2-G2 depth bars, escalate to
  the sim-consulting scripted driver (shallow rollout behind the same
  STS-POLICY-IO seam) — still deterministic and weight-free.
  **Deps:** S2.12 (three-act sim runs) **Acceptance:** measured reach
  report (per-act boss-fight and boss-kill rates per policy at scanned
  scale) committed; drivers replay deterministically; the S2-G2 depth
  cohorts are demonstrably schedulable from the scan output.
- **S2.43** `[ ]` **Oracle campaigns, breadth + depth.** The §6 S2-G2
  evidence: ≥ 2,000 distinct mixed-policy A20 attempts; Act-2 boss-reward
  + boss-relic-pick cohort; Act-3 kill + double-boss cohort
  (simulator-selected seeds sanctioned); event-depth coverage join; all
  triage per the Stage B process, zero untriaged/open.
  **Inherited:** the stage-b "fork redeploy + bottle-taking capture" row
  (see Deferred obligations).
  **Deps:** S2-G1, S2.42 **Acceptance:** deterministic dashboard
  reopening every artifact; per-bar numbers meeting design §6 S2-G2
  items 1–4; dispositions exact, no wildcards.
- **S2.44** `[ ]` ∥ **Tier-4 additions.** Pre-registered hypotheses per
  design §6 item 6 (act pools + exclusion effects, per-act upgrade
  chance, boss shuffle + double-boss conditioning, one-time-pool
  depletion, canSpawn-gate pool-cursor effects), Holm-corrected family.
  **Deps:** S2-G1 **Acceptance:** suite green at B5.3 scale with α
  discipline unchanged; negative-control mutant rejected.
- **S2.45** `[ ]` ∥ **Throughput re-baseline.** B5.5 methodology over
  three-act runs: per-step and per-combat floors must hold unchanged; new
  whole-machine three-act run rate recorded with methodology as the S3
  baseline (expected lower per run — not a regression; design §6 item 7).
  **Deps:** S2-G1 **Acceptance:** release-preset numbers recorded;
  per-step/per-combat floors green.
- **S2.46** `[ ]` **Verification report + CI corpus + proactive audit.**
  B5.4 pattern: aggregated report with literal S2-G2 shortfalls; curated
  compressed corpus extended with three-act traces incl. one double-boss
  run; `g7_proactive_manifest` extended with S2-discovered families and
  the executable audit re-run.
  **Deps:** S2.43, S2.44 **Acceptance:** report committed under
  `docs/verification/`; CI replay of the extended corpus green in every
  preset; audit green.

### S2-G2 `[ ]` **Gate: S2 verified (unblocks training Phase T4)** — tag `s2-g2-verified`
**Deps:** S2.41–S2.46, S2-G1
The design §6 S2-G2 bar, checked literally, every item with linked
evidence. Then: update CLAUDE.md "Current state"; notify the training
ledger (T4.1's `Deps: S2` is this tag); S3 planning opens as its own fresh
exercise (not claimed here).
**Log:** —

## Parallelism map

```
Wave 1 (now):  S2.01 ∥ S2.02 ∥ S2.03 ∥ S2.04
S2.01 ─▶ S2.12 ─▶ S2.13 ; S2.11 (∥ with S2.12)
S2.01 ─▶ S2.21 … S2.28 (batches ∥)
S2.02 + S2.13 ─▶ S2.31 ∥ S2.32 ∥ S2.33
all S2.0x/1x/2x/3x ─▶ S2-G1
S2.12 ─▶ S2.42 ; S2.11+S2.12 ─▶ S2.41
S2-G1 ─▶ S2.43 (needs S2.42) ∥ S2.44 ∥ S2.45 ; S2.43+S2.44 ─▶ S2.46
S2.41–S2.46 ─▶ S2-G2
```

## Change log

- 2026-08-03 — ledger created by TE.2 with Phases S2.0–S2.4, gates
  S2-G1/S2-G2, Wave-1 id blocks, and the inherited-obligation rows;
  scope denominator is [s2-design.md](s2-design.md) v0.1.0.
