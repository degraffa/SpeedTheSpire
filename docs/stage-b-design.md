# Stage B Design — Frozen Decisions (v0.1)

**Status:** frozen. This document is the Stage B design deliverable from
[InitialPlan.md](../InitialPlan.md) §Stage B (B.1–B.4), covering scope tier
**S1** (InitialPlan §A.5: Ironclad, Act 1, all A20 modifiers, full
Neow/map/events/shops/rest/rewards for Act 1). Every decision below is binding
for the rest of Stage B and for Stage C. Amendments require evidence (a
diff-harness divergence, an oracle observation with a reproducer, a profiling
result, or a source-reading correction) and a version bump of this document.

[stage-a-design.md](stage-a-design.md) remains frozen and is **not** superseded:
its RNG subsystem (§3), state structs (§4), action queue (§5), rules-as-data
shape (§6), batch API (§7), trajectory schema (§8), and trap list (§10) all
carry forward unchanged. Where this document says "stage-a §n" it cites that
document; a bare "§n" cites this one. Conflicts between the two documents are
stop-the-line and resolve through stage-a §12's change-log process — Stage B
never silently overrides Stage A.

Everything cited here was read from the actual game code or the actual local
artifacts, not from memory or wikis. Line numbers refer to the decompiled
reference tree described in stage-a §1. Where a fact could not be verified
locally (a handful of CommunicationMod protocol details), it was marked
**[confirm at B0.1]** rather than asserted; all such items are now resolved
against the vendored upstream source (§11, and tools/oracle_bridge/PROTOCOL.md).

---

## 1. Scope, references, and the updated precedence chain

### 1.1 S1 scope boundary (frozen)

- **In:** Neow (all four blessing categories, including the boss-relic swap),
  Act 1 map generation, all Act 1 room types (monster, elite, event, shop,
  rest, treasure), full combat content closure (every red card, the colorless
  shop pool, status/curse cards, all Act 1 monsters/elites/bosses, the
  Ironclad-obtainable relic pools including boss relics reachable via Neow,
  all 33 Ironclad potions), combat and non-combat rewards with all pity
  counters, and every A20 modifier that has an Act-1 effect (§6).
- **Out (S2+):** the act-1→act-2 transition and the post-boss **boss chest /
  boss-relic pick** (the run terminates when the act-1 boss's combat rewards
  are claimed), acts 2–4, keys (the sapphire-key reward branch in chests is
  final-act-gated, AbstractChest.java:95-96, so it never fires in S1;
  `setEmeraldElite`, by contrast, is gated by `isFinalActAvailable`, **not** by
  keys — a profile-unlock condition (`IRONCLAD_WIN && SILENT_WIN && DEFECT_WIN
  && …`, Settings.java:642) that the frozen fully-unlocked assumption below
  makes TRUE, so it **DOES fire every act in S1**, consuming one `mapRng` draw
  (`mapRng.random(0, eliteNodes.size()-1)`) **after** room assignment
  (AbstractDungeon.java:539, 542-556); corrected at B4.1 from live evidence,
  see §11 v0.1.3), save-file loading, other characters.
- **Environment assumption (frozen):** the sim models a **fully-unlocked
  profile**. Unlock state gates real content — e.g. `initializeBoss` special-
  cases unseen bosses via `UnlockTracker.isBossSeen` (Exordium.java:196-201)
  before falling back to the uniform `monsterRng` shuffle (Exordium.java:195,
  206, 215). The oracle game profile must therefore be fully unlocked, and
  B0.2 verifies this (card/relic unlock gates too) before any campaign runs.

### 1.2 Canonical references

Unchanged from stage-a §1 (decompiled tree, class tree, `sts-classes.jar`)
plus the Stage B additions:

- **The installed game:** `D:\SteamLibrary\steamapps\common\SlayTheSpire`
  (`desktop-1.0.jar`, patch dated `11-30-2020` per CommunicationMod's declared
  `sts_version`), with its bundled JRE.
- **ModTheSpire 3.18.1 + BaseMod** (Steam workshop items 1605060445,
  1605833019, both installed locally).
- **CommunicationMod v1.2.1** — local jar at Steam workshop item
  `2131373661\CommunicationMod.jar` (`ModTheSpire.json`: version 1.2.1,
  sts_version 11-30-2020, depends on basemod). Upstream source:
  `ForgottenArbiter/CommunicationMod` on GitHub, pinned at tag `v1.2.1`
  (commit `70ca84b1e8daff3eb4fe7f66775ce39926133c7f`), matching the local
  workshop jar (`ModTheSpire.json` version 1.2.1, sts_version 11-30-2020) and
  the source `pom.xml` (`<version>1.2.1</version>`). Resolved at B0.1 (§11).
- **JDK 8** (`C:\Program Files\Java\jdk1.8.0_171`) for building the fork —
  the game runs Java 8; mods must target it. (Java 25 remains the
  golden-capture JVM, stage-a §3.7 — two different toolchains, two different
  jobs.)

### 1.3 Precedence chain (amended — a frozen Stage B decision)

Stage A's chain was: decompiled Java > design doc > task ledger > InitialPlan.
Once the oracle bridge is live (gate G4), the chain becomes:

**live game observation (with a reproducer) > decompiled Java > design docs
(stage-a for frozen mechanics, this doc for Stage B scope) > stage-b task
ledger > InitialPlan.**

Rationale: the game binary is the specification; the decompiled tree is a
*proxy* for it, and Stage A already caught the proxy being wrong once — the
decompiled `GameActionManager` is missing the `monsterAttacksQueued` reset
that the shipped game observably performs (stage-a §12, A3.1 entry). The
bridge turns "observably" into evidence on demand.

Guard rails, because a bridge observation can also be a *bridge bug*: a
live-game observation outranks the Java only when it (a) carries a
`(seed, action-prefix)` reproducer, (b) reproduces on a second run, and
(c) has been checked against the fork's own patch list (§2.5) to rule out
oracle-side contamination — the strip patches (§2.4) are the prime suspect
whenever oracle and Java disagree. Every such override gets a change-log
entry here or in stage-a §12, exactly like A3.1's.

---

## 2. The oracle bridge (InitialPlan B.1)

### 2.1 Topology (frozen)

Five components, one direction of data flow:

```
[1] Slay the Spire (Windows host, Steam install, ModTheSpire launch)
      + BaseMod + CommunicationMod-oracle (our fork, §2.3–2.5)
        │  state JSON → child stdin, commands ← child stdout, \n-delimited (resolved B0.1, §11)
[2] Campaign driver — Python 3, Windows host (tools/oracle_bridge/driver/)
      starts seeded runs, injects action scripts, records every state
        │  JSONL campaign artifacts (one file per run, version-stamped)
[3] Translator — C++ (tools/oracle_bridge/translator/)
      JSON → RunState/CombatState binary schema; unknown-field ledger
        │  trace files (stage-a §8 format, v2 with run records, §3.3)
[4] Diff harness — existing tools/diff_harness/ + CommunicationModOracleAdapter
        │  DiffReports, (seed, action-prefix) reproducers
[5] Triage — reproducers become permanent regression fixtures (§7.4)
```

The bridge is **offline/batch, not a live loop**: the driver produces
artifacts; the C++ side consumes files. This is the same
`FixtureFileOracleAdapter` pattern A6.1 froze (the fixture format *is* the
trace format), extended with a `CommunicationModOracleAdapter` that reads
translated campaign traces instead of hand-derived fixtures. No IPC in C++,
no game process under test-runner control, campaigns survive crashes of
either side. A live TCP adapter is deliberately deferred (§9) until a
generator actually needs in-loop decisions (the adversarial-agent generator,
which does not exist yet — §3.3).

### 2.2 "Rendering-stripped" concretely — and the headless question answered

The game is a libGDX/LWJGL2 desktop app; it **cannot run truly headless**: it
creates a GL context unconditionally. On Linux the standard trick is a
virtual display (Xvfb); this project's dev box is Windows 10 Home (build
19045), where no Xvfb equivalent exists and WSLg (which would let the WSL
side host GUI apps) requires Windows 11. CommunicationMod itself does nothing
about rendering — it is a state-dump/command mod, not a headless mod.

**Frozen decision:** the oracle runs as a real windowed process on the
Windows host, and "rendering-stripped" means a ModTheSpire patch set in our
fork that makes wall-clock time stop mattering:

1. **Draw suppression** — no-op the render bodies (scene/room/VFX draw
   calls), keeping the GL surface alive. Pure frame-cost removal.
2. **Animation-time collapse** — zero the durations that gate action
   resolution: `AbstractGameAction` duration timers, effect `duration`s, the
   action-manager wait states. The game's own logic distinguishes "logic
   done" from "animation done"; the patches collapse the latter only.
3. **Fast update cadence** — uncap/raise the frame rate so `update()` ticks
   as fast as the CPU allows.

**Semantic guard (frozen rule):** a strip patch may remove *time and pixels*,
never *order or state*. Stage-a §5.2 froze the scope rule "an action is in
scope iff it mutates gameplay state or its queue position can reorder
something that does" — the same rule bounds stripping from the other side:
patches must not remove or reorder queued actions (even presentation ones),
because their queue positions can reorder gameplay. The acceptance test for
the whole patch set is behavioral, not visual: same seed + same action script
⇒ **byte-identical oracle state dumps** with patches on vs. off (run at
B1.3 over ≥20 seeds), plus a throughput measurement. (Refined at B1.3 — the
dump carries some presentation fields that timing legitimately shifts; see §11
v0.1.2 for the enumerated, dually-proven presentation exceptions.)

**Throughput budget (floor frozen, target aspirational):** ≥ 5 injected
actions/sec sustained is the floor; ~20/sec is the working target. At 5/s the
B.4 campaign volume (1M actions) is ~56 unattended hours — feasible on one
machine over nights (§7.1); below 5/s the B.4 bar must be formally amended,
not quietly missed (§8).

### 2.3 CommunicationMod: what it is, verified locally

Facts confirmed by inspecting the local jar (class list + string constants)
and the on-disk config:

- **Process model:** the mod launches an external process and communicates
  with it (`config.properties` keys: `command`, `runAtGameStart`,
  `maxInitializationTimeout`, `verbose`; child stderr goes to
  `communication_mod_errors.log`; a `ready_for_command` field appears in the
  state messages). Config lives at
  `C:\Users\Alex\AppData\Local\ModTheSpire\CommunicationMod\config.properties`
  (present on this machine, `command=` currently empty). Message direction is
  confirmed against the vendored source: state JSON is written to the child's
  **stdin** (`DataWriter` on `listener.getOutputStream()`, DataWriter.java:31-33)
  and commands are read from the child's **stdout** (`DataReader` on
  `listener.getInputStream()`, DataReader.java:28-34), each message
  newline-terminated (the reader also accepts NUL). Resolved at B0.1 (§11).
- **Command vocabulary** (string constants in `CommandExecutor.class`):
  `start`, `play`, `end`, `potion` (use/discard), `choose`, `proceed` /
  `confirm`, `return` / `leave` / `skip` / `cancel`, `key`, `click`, `wait`,
  `state`. `start <character> [ascension] [seed]`: the seed argument is the
  game's **base-35 display string** (uppercased, validated `^[A-Z0-9]+$`,
  converted to the internal long by `SeedHelper.getLong`), **not** a raw long
  — matching stage-a §3.5 (CommandExecutor.java:353-359; trial seeds route to
  `Settings.specialSeed`, :360-365). Resolved at B0.1 (§11).
- **State dump:** `GameStateConverter` serializes the visible game state
  (screen type, combat state incl. hand/piles/monsters/powers, map, relics,
  potions, gold, available commands) after every stable state. It does
  **not** expose RNG stream counters or hidden pity counters — that is
  exactly the gap InitialPlan B.1 predicted and §2.5 closes.
- **Patch surface:** the jar's patch classes show where it hooks the game —
  `GameActionManagerTopPatch`/`BottomPatch` (action-queue drain detection for
  "stable state"), screen patches for every choice UI (map, events, shop,
  campfire, card/grid/hand select, rewards). This is the action-injection
  path: **commands are executed through the game's own screen/input logic**
  (the same entry points a player's clicks reach), not by conjuring
  `AbstractGameAction`s directly. Consequence, frozen as a feature: the
  oracle continuously exercises the game's own legality rules, so
  `CommandExecutor`'s accepted-command set is itself an oracle for
  `legal_actions()` (§3.3), and an action the sim thinks legal but the game
  rejects is a first-class divergence.

### 2.4 The fork (frozen decision: fork CommunicationMod, don't sidecar it)

A separate companion mod could theoretically add the missing state, but
CommunicationMod has no extension API for its JSON (`GameStateConverter` is
static), so a sidecar would end up patching the patcher. **Decision:** fork
CommunicationMod as `CommunicationMod-oracle`, source vendored under
`tools/oracle_bridge/communicationmod-oracle/` (upstream is MIT-licensed —
`LICENSE` file verified at B0.1, "MIT License", Copyright (c) 2019
ForgottenArbiter; §11), built with JDK 8 against
`desktop-1.0.jar` + ModTheSpire + BaseMod from the local install. The jar is
a build artifact, never committed. The fork carries three patch families:

1. the **oracle state block** (§2.5) appended to every state dump;
2. the **strip patches** (§2.2);
3. quality-of-life for campaigns: deterministic fast startup (skip
   intro/menus where safe), auto-abandon/restart hooks for the driver
   **[scope at B1.1]**.

Upstream drift policy: the fork pins upstream v1.2.1 and never tracks later
upstream changes during S1 (the game itself is frozen at 11-30-2020, so
there is nothing to chase).

### 2.5 The oracle state block — the concrete fork-patch list (frozen scope)

Stage-a §11 flagged "all 13 stream counters + `cardBlizzRandomizer` +
`blizzardPotionMod`". Read against the source, the full hidden-state
inventory S1 diffing needs is:

| # | Field(s) | Provenance |
|---|---|---|
| 1 | The 13 dungeon streams — 7 run-scoped (`monsterRng, eventRng, merchantRng, cardRng, treasureRng, relicRng, potionRng`), 5 floor-scoped (`monsterHpRng, aiRng, shuffleRng, cardRandomRng, miscRng`), `mapRng` — each as `{counter, s0, s1}` (counter for save-parity, raw xorshift state to catch wrapper bugs directly) | AbstractDungeon.java:149-161; Random.java:17-18 (`public RandomXS128 random; public int counter`) — both fields public, no reflection needed |
| 2 | `NeowEvent.rng` counter (event-scoped 14th stream, fresh `Random(Settings.seed)`) | NeowEvent.java:62, 289, 363 |
| 3 | `cardBlizzRandomizer` | AbstractDungeon.java:247-250, 1597-1624; SaveFile parity per stage-a §3.4 |
| 4 | `AbstractRoom.blizzardPotionMod` | AbstractRoom.java:100-101, 580-608 |
| 5 | EventHelper pity chances: `MONSTER_CHANCE`, `SHOP_CHANCE`, `TREASURE_CHANCE` (floats, grow 0.1/0.03/0.02 per miss, reset on hit) | EventHelper.java:88-92, 143-181 |
| 6 | `ShopScreen.purgeCost` (base 75, +25 per purge, run-persistent) | ShopScreen.java:100-102, 278-292 |
| 7 | Remaining `eventList` / `shrineList` / `specialOneTimeEventList` contents (events are removed on use) | AbstractDungeon.java:182-184, 1986-1987, 1937-1939 |
| 8 | Remaining relic pool **order** for all 5 tiers (shuffled once at init, then popped — front for rewards, end for shop) | AbstractDungeon.java:1221-1256, 757-808, 704-755 |
| 9 | Per-monster `moveHistory` (the sim tracks 3; dump what the game keeps) | AbstractMonster move-history fields, read at B1.2 |
| 10 | `Settings.seed` echo + `floorNum` + act id + ascension (sanity anchors for every record) | AbstractDungeon.java |

Everything in the table is reachable via public/static fields or trivial
accessors on the 11-30-2020 build (spot-checked for #1; the rest verified at
B1.2, with reflection as the fallback for anything private). The block is
emitted under a single `"oracle"` key, gated by a fork config flag, so stock
CommunicationMod consumers are unaffected.

### 2.6 The translator

C++ target `tools/oracle_bridge/translator/`: campaign JSONL →
`RunState`/`CombatState` per the frozen binary schema, emitting standard
trace files (§3.3) the diff harness already reads.

- **JSON parsing:** nlohmann/json (single-header, FetchContent), linked into
  `tools/` targets **only** — never into `sts_engine`. This is a new
  third-party dependency, granted here per the working agreement (tools-side,
  zero engine/hot-path exposure). Same grant, same terms, for **PyYAML** in
  the registry codegen (§4.3).
- **Id mapping:** the game identifies content by string ids (`"Strike_R"`);
  the sim by u16 enums. The registry (§4) carries `game_id` per entry, and
  codegen emits the string→enum table the translator uses. Until the registry
  lands, a hand table covering the skeleton set bootstraps B1.5.
- **Unknown-field policy (frozen):** fail loudly, not silently — every JSON
  field is either (a) mapped to a schema field, (b) on an explicit
  ignore-list with a reason (`"presentation"`, `"S2 scope"`), or (c) an
  error. The B0.1 protocol survey produces the initial full-field disposition
  table; drift (a field appearing that is in no list) fails the translation.
  This is stage-a §4.1's "fail loudly" applied to the bridge.
- **RunState placeholder population:** Stage A left `RunState`'s map, relic,
  potion, boss-id, and event/shop-flag fields as placeholders (stage-a §4.3).
  Stage B populates them and **additively extends** `RunState` for the
  §2.5 inventory items that are genuinely run state (event pity floats,
  purge cost, remaining-pool order/membership, potion-slot count for A11).
  Additive fields follow stage-a §12's amendment process: POD, capacity
  ceilings intact (8 KB budget; current size 1648 B leaves ample room),
  `SCHEMA_VERSION` bump per stage-a §8.

### 2.7 Campaign artifact format

One JSONL file per run: a header record (game version, mod-set + fork-jar
hash, driver version, seed as long **and** base-35 string, ascension,
character), then one record per injected action: `{action_command,
sim_action_bits?, state_json}`. Artifacts live outside the repo (§7.3);
a curated, compressed corpus is committed for CI (§7.5). Every artifact is
self-describing — replayable without this conversation, this machine's
config, or the driver that made it.

---

## 3. The four verification tiers made concrete (InitialPlan B.2)

### 3.1 Tier 1 — RNG stream tests (built; one addition)

The tier-1 suite (gates every commit since G1) needs **no changes for
run-scoped streams**: all 13 streams share the same wrapper primitives, and
A0.1's golden categories already cover every wrapper method across the
38-seed battery, floor derivations for floors 1–55, and all four act
constants for `mapRng`. When tier-3 first exercises run-scoped streams for
real and a counter mismatches, that attributes to *consumption-order* bugs in
rule code, not RNG-core bugs — which is precisely why tier 1 stays frozen and
commit-gated: it keeps the core above suspicion.

**One genuine addition (new golden category 7 + trap 12):** map room-type
assignment shuffles via `Collections.shuffle(roomList, rng.random)`
(RoomTypeAssigner.java:135) — it hands the raw `RandomXS128` (`Random.java:17`,
which *extends* `java.util.Random`) directly to `Collections.shuffle`. That
Fisher–Yates therefore consumes **xorshift draws via RandomXS128's overridden
`nextInt(bound)`, and the game-side `counter` does not advance** — unlike
every deck/pool shuffle, which draws one `randomLong()` and shuffles with the
JDK LCG (stage-a §3.3). A0.1's harness gains a category capturing
shuffled permutations under this direct-XS128 mode; trap list §10 gains the
corresponding named test.

### 3.2 Tier 2 — registry unit tests (requires §4; migration first)

Tier 2's InitialPlan definition — "per card/relic/power, a table-driven test
asserting effect outcomes in constructed states" — presumes entries live in a
registry that doubles as the coverage checklist. Stage A hand-coded the
skeleton (5 cards in `cards.hpp`, Jaw Worm in `monster_jaw_worm.hpp`, enums
in `types.hpp`) as an explicit stopgap.

**Frozen decision: migrate first, then extend.** The registry system (§4) is
built and the skeleton content migrated onto it *before any new content
lands* (ledger phase B2, gated at G5). Rationale: migrating known-good,
fixture-pinned content is the cheapest possible validation of the codegen —
acceptance is "Stage A's 131 tests and 20 combat fixtures pass **unmodified,
byte-identically**". Building the registry alongside new content instead
would validate codegen against content that has never been verified by
anything, and would leave two content systems alive simultaneously. The
migration risk (§4.4) is called out as its own stop-the-line decision.

Coverage definition (frozen): every registry entry has a named tier-2 test;
`native: true` entries (§4.2) additionally need hand-written cases per native
branch. "100 % registry coverage" in the B.4 bar counts registry rows, so the
registry is the denominator of done — exactly the InitialPlan intent.

### 3.3 Tier 3 — seed-replay differential tests (the workhorse)

The consumer is A6.1's diff harness, already built: trace format, semantic
field-by-field differ with named-field output, `(seed, action-prefix)`
reproducers, and the `OracleAdapter` seam that A6.1 documented for exactly
this moment. Stage B extends it three ways:

1. **Run-level traces.** The stage-a §8 container gains `RunState` records:
   header gets a `state_kind` discriminator (schema-version bump; loaders
   refuse mismatches, unchanged policy). The differ gains `RunState`
   field-group coverage (deck, relics, potions, gold, map, pools, pity
   counters, the 8 run-level streams) with the same named-field output.
2. **`CommunicationModOracleAdapter`.** Implements `OracleAdapter::query`
   over translated campaign traces (file-based, §2.1) for both state kinds.
   The `FixtureFileOracleAdapter` and the 20 A6.2 fixtures remain untouched
   as fast offline regression.
3. **Replay drivers.** `replay_skeleton` (fixed skeleton deck) generalizes to
   replay-from-`RunState` so any translated oracle state can seed a sim-side
   replay mid-run.

**Action-sequence generators — honest adaptation of InitialPlan.** The plan
names three: random-legal, *current agent policy*, and directed scripts. There
is no agent yet — Stage A just ended, no training pipeline exists, and the
imitation nets are Part-2 work. Frozen substitution for S1: (a) random-legal
(driver-side, from the game's own accepted-command set; sim-side, from
`legal_actions()` — disagreement between those two sets is itself a tier-3
divergence, per §2.3); (b) directed scripts targeting known-nasty
interactions (each content batch ships its own, ledger B3/B4); (c)
**scripted heuristic policies** (greedy-damage, greedy-block, hoard-gold,
always-event — the E0 baselines from InitialPlan Part 2, pulled forward) as
the adversarial-ish traffic stand-in. When E2 produces a real agent policy,
it joins the generator set — deferred, not dropped.

### 3.4 Tier 4 — distributional tests (new infrastructure, scoped)

InitialPlan framed tier 4 as chi-square vs *oracle-collected* distributions.
Collecting oracle distributions at scale is bridge-throughput-bound, and for
anything deterministic-per-seed, tier 3 is strictly stronger evidence.
**Frozen scope for S1:**

1. **Analytic expectations (the bulk).** Chi-square / exact tests of sim
   aggregates over ≥10k sim seeds against distributions *computed from the
   cited tables*, not harvested: encounter-pool weights (Exordium.java:
   117-154), louse/slime composition rolls (MonsterHelper.java:694-836),
   card-reward rarity including the `cardBlizzRandomizer` pity dynamics
   (AbstractDungeon.java:1597-1624 + AbstractRoom.java:148-177), potion-drop
   ratchet (AbstractRoom.java:580-608), chest size/contents tables
   (Exordium.java:100-102, AbstractChest.java:54-60, per-chest ctors),
   ?-room outcome table with pity growth (EventHelper.java:100-187),
   relic-tier rolls (AbstractDungeon.java:810-819, ShopScreen.java:418-428),
   potion tier gate 65/25/10 (PotionHelper.java:70-71, AbstractDungeon.java:
   829-838), map room-type quotas (AbstractDungeon.java:558-594).
2. **Oracle-harvested spot set (small).** One campaign of ≥200 full Act-1
   runs; compare sim-vs-oracle frequencies for a handful of end-to-end
   aggregates (encounter mix, reward rarity mix, event mix). This catches
   "both formulas right, wiring wrong" errors analytic tests can't.

Tooling: `tools/dist_check/` (C++ target driving the sim at full speed +
a small stats module; no new deps — chi-square needs nothing heavy).
Tier 4 runs in campaigns (§7), not per-commit CI.

---

## 4. The registry system (stage-a §6 made real)

### 4.1 Files

`registry/` gains one YAML file per domain: `cards.yaml`, `powers.yaml`,
`monsters.yaml`, `encounters.yaml`, `relics.yaml`, `potions.yaml`,
`events.yaml`, `a20.yaml`. Map-generation constants and room-flow logic stay
in code with provenance comments (they are engine subsystems, not per-entry
content); event *logic* is native code with registry metadata (§4.2) — event
dialog trees are irreducibly bespoke.

### 4.2 Entry schema (frozen shape; illustrative entry)

```yaml
# cards.yaml
- id: 3                      # numeric enum value — append-only, NEVER renumbered
  name: BASH                 # generated enum symbol (CardId::BASH)
  game_id: "Bash"            # the game's string id (cardID) — translator key
  color: RED
  rarity: BASIC
  type: ATTACK
  cost: 2
  target: ENEMY              # ENEMY | ALL_ENEMY | SELF | NONE | RANDOM_ENEMY | SELF_AND_ENEMY
  flags: []                  # exhaust | ethereal | innate | unplayable | retain | ...
  provenance: "Bash.use (Bash.java) — read in full"
  effects:                   # base effect program, stage-a §6 {op, target, amount, flags}
    - {op: DAMAGE,      target: CARD_TARGET, amount: 8}
    - {op: APPLY_POWER, target: CARD_TARGET, amount: 2, power: VULNERABLE}
  upgraded:                  # FULL program, not a delta (auditable against the Java)
    - {op: DAMAGE,      target: CARD_TARGET, amount: 10}
    - {op: APPLY_POWER, target: CARD_TARGET, amount: 3, power: VULNERABLE}
  native: false              # true → named native branch; effects may be absent/partial
```

Schema decisions frozen here:

- **Explicit stable numeric ids.** Ids are hand-assigned in the YAML,
  append-only, and never derived from file order. Rationale in §4.4.
- **`game_id` is mandatory** — it is the translator's join key (§2.6) and the
  oracle-coverage join key (§7.4).
- **Upgrades are full programs, not deltas.** Two constexpr rows per card
  (indexed by the existing `CardInstance.upgrade` bit); the duplication is
  the point — each row is independently checkable against its `use()` read.
- **`provenance` is mandatory** on every entry (stage-a §1 convention).
- **`native: true` is the escape hatch** for genuinely bespoke logic
  (stage-a §6 budgeted ~15 % for cards; monsters will run far higher — the
  three bosses and Lagavulin are certainly native; the honest budget for
  monster AI is "move *effects* as data, move *selection* native where the
  table shape doesn't fit"). A native entry still gets id/game_id/provenance
  and tier-2 coverage obligations.
- Monster entries carry HP ranges and move damage as **per-ascension-tier
  columns** (base /A2+ /A7+ etc. as applicable — §6), the move list with
  intents and effect programs, and either an `ai_table` (threshold/history
  rules in the universal `getMove(roll 0-99)` shape, stage-a §6) or
  `ai: native`.

### 4.3 Codegen

Python 3 + PyYAML (dep granted in §2.6), `tools/registry_gen/gen.py`, run as
a CMake custom command; outputs generated headers under the build tree
(`<build>/generated/sts/registry/*.hpp`) — **generated code is never
committed**. Output is deterministic (sorted iteration, no timestamps): the
same YAML must produce byte-identical headers on every machine — CI asserts
this by running the generator twice. Generated artifacts: the four Stage A
enums (`CardId`/`PowerId`/`MonsterId`/`RelicId`) plus `PotionId`/`EventId`,
the constexpr effect-program tables (the exact `CardDef`/step shape A4.3
already established — "deliberately the shape that codegen will emit", per
cards.hpp's own header comment), the `game_id` string↔enum tables for the
translator, and a registry manifest (row counts per domain) that the tier-2
coverage check and the §7.4 dashboard consume.

### 4.4 The enum migration — an explicit stop-the-line-worthy decision

Replacing `types.hpp`'s hand-written enums with generated ones is a **real
migration with a real blast radius**, not a refactor detail:

- **Numeric ids are load-bearing on disk.** A6.2's 20 combat fixtures and
  every future trace store raw `CombatState` bytes containing `card_id`/
  `power_id`/`monster_id` values. Renumbering silently invalidates them all
  while every type still compiles.
- **Frozen mitigation:** (a) ids are explicit in YAML and append-only
  (§4.2); (b) the skeleton's existing values are pinned exactly —
  `CardId`: STRIKE=1, DEFEND=2, BASH=3, SHRUG_IT_OFF=4, POMMEL_STRIKE=5;
  `PowerId`: STRENGTH=1, VULNERABLE=2, WEAK=3; `MonsterId`: JAW_WORM=1
  (types.hpp as of M1) — and the generated headers carry `static_assert`s
  re-pinning each of them; (c) migration acceptance is Stage A's full suite
  plus all 20 fixtures passing **with zero test-file edits and zero fixture
  regeneration** — if a fixture needs regenerating to pass, the migration is
  wrong, stop the line; (d) opcode numbering gets the same append-only rule
  (A4.1 numbered §6's set 1–8 with NOP=0; new opcodes append from 9).
- `RelicId` currently has only `NONE` and `Action`/`ActionVerb` are not
  registry-generated (they are API, not content) — both stay hand-written.

---

## 5. S1 content inventory (counted from the source — the ledger's denominator)

All counts from the decompiled tree (file counts by `CardRarity`/`RelicTier`
constants; rosters read from the cited methods).

### 5.1 Cards — ~126 registry rows

| Pool | Count | Provenance |
|---|---|---|
| Red basic | 3 (Strike, Defend, Bash) | cards/red: 75 files, 3 tagged BASIC |
| Red common | 20 | cards/red, CardRarity.COMMON |
| Red uncommon | 36 | cards/red, CardRarity.UNCOMMON |
| Red rare | 16 | cards/red, CardRarity.RARE |
| Colorless uncommon | 20 | cards/colorless (39 files; 4 SPECIAL excluded from pools) |
| Colorless rare | 15 | cards/colorless |
| Status | 5 (Burn, Dazed, Slimed, Void, Wound) | cards/status |
| Curses | 11 = 10 poolable (Clumsy, Decay, Doubt, Injury, Normality, Pain, Parasite, Regret, Shame, Writhe) + Ascender's Bane | cards/curses (14 files; Necronomicurse, CurseOfTheBell, Pride are special-source, out of S1 reach) |

The 72 red non-basics are the reward pool; colorless enters via the shop's 2
colorless slots (ShopScreen.java:130 layout; `colorlessRareChance` 0.3,
Exordium.java:106) and Neow's RANDOM_COLORLESS options. Card pools are filled
in library order at dungeon init with **no RNG** (AbstractDungeon.java:
1135-1201); randomness is per-draw via `cardRng` (`getRandomCard(true)`),
with `srcX` mirror pools serving transforms via `cardRandomRng`
(AbstractDungeon.java:923-942).

### 5.2 Monsters — 25 encounter actors (+2 Hexaghost components)

Rosters and weights read from Exordium.java / MonsterHelper.java:

- **Weak pool** (3 drawn; equal weight 2.0/8.0 each — Exordium.java:117-126):
  Cultist; Jaw Worm; 2 Louse; Small Slimes.
- **Strong pool** (12 drawn; weights /16 — Exordium.java:128-144): Blue
  Slaver 2, Gremlin Gang 1, Looter 2, Large Slime 2, Lots of Slimes 1,
  Exordium Thugs 1.5, Exordium Wildlife 1.5, Red Slaver 1, 3 Louse 2,
  2 Fungi Beasts 2 — plus the weak→strong exclusion table
  (Exordium.java:156-186).
- **Elites** (10 drawn, uniform — Exordium.java:146-154): Gremlin Nob,
  Lagavulin (asleep variant, `Lagavulin(true)`), 3 Sentries.
- **Boss** (Exordium.java:188-221): The Guardian, Hexaghost, Slime Boss —
  shuffled via `monsterRng.randomLong()` under the §1.1 fully-unlocked
  assumption.
- **Composition rolls consume `miscRng`** (MonsterHelper.java: louse variant
  `:831-836`, small-slimes branch `:694-704`, gremlin draws-without-
  replacement `:737-765`, slaver pick `:824-829`, thugs/wildlife slot rolls
  `:780-822`, many-slimes `:706-735`) — order and count of these draws is
  bit-exactness-relevant.

Distinct classes: Cultist, JawWorm, LouseNormal, LouseDefensive,
SpikeSlime_S/M/L, AcidSlime_S/M/L, SlaverBlue, SlaverRed, Looter, FungiBeast,
GremlinWarrior/Thief/Fat/Tsundere/Wizard, GremlinNob, Lagavulin, Sentry,
TheGuardian, Hexaghost (+HexaghostBody/Orb components), SlimeBoss. Of the 28
files in monsters/exordium only ApologySlime is unreachable (the
`getEncounter` fallback, MonsterHelper.java:603).

### 5.3 Relics

Tier populations across all characters (relics/: 187 files): starter 4,
common 39, uncommon 40, rare 37, boss 31, shop 20, special 20. S1 needs the
**Ironclad-obtainable subset**, resolved at extraction time via each relic's
class/`canSpawn` gates (RelicLibrary population — provenance read at B4.5):
Burning Blood (starter); the common/uncommon/rare/shop pools; the boss pool
(reachable in S1 **only** through Neow's category-3 swap — frozen scope
decision, §5.6); and the special-tier relics granted by Act-1 events (e.g.
Golden Idol, Neow's Lament). Pool mechanics: 5 `Collections.shuffle(pool, new
java.util.Random(relicRng.randomLong()))` at init (AbstractDungeon.java:
1237-1241 — the already-frozen stage-a §3.3 pattern), then deterministic pops
— **front** (`remove(0)`) for reward/chest draws vs. **end** (`size()-1`) for
shop stock (AbstractDungeon.java:757-808 vs. 704-755), with tier-roll tables
at AbstractDungeon.java:810-819 (50/33/17) and ShopScreen.java:418-428
(48/34/18), `canSpawn` re-check and Circlet fallback (AbstractDungeon.java:
804, 765-799).

### 5.4 Potions — 33 for Ironclad

PotionHelper.getPotions (PotionHelper.java:88-162): 3 Ironclad-specific
(Blood Potion, Elixir, Heart of Iron) + 30 shared. Tier gate 65/25/10
(PotionHelper.java:70-71 via AbstractDungeon.java:829-838), identity via
`potionRng` with **rejection sampling until the rolled tier matches**
(AbstractDungeon.java:840-850 — consumes a variable number of draws; trap
§10). Drop chance 40 % ± `blizzardPotionMod` ratchet (AbstractRoom.java:
580-608). Potion slots: 3, minus one at A11+ (AbstractPlayer.java:211-213).

### 5.5 Powers

powers/ holds 124 files; S1 needs the closure over §5.1–5.4's content (card
powers, monster powers — Ritual, Curl Up, Anger, Spore Cloud, Thievery, Mode
Shift, Sharp Hide, Split…, relic/potion-granted powers). The registry is
extracted per content batch (each B3 task lists the powers its cards/monsters
pull in), so the power count is an output of extraction, not an input;
working estimate ~55–65 rows.

### 5.6 Run layer — the numbers that shape it

- **Map:** 15 rows × 7 columns, 6 path walks (AbstractDungeon.java:510-540,
  MAP_HEIGHT/WIDTH/DENSITY :210-213); `mapRng` consumed by path generation
  first (MapGenerator.java:62-77, 157-190, `randRange` :270-276), then the
  trap-12 room shuffle (RoomTypeAssigner.java:135), then assignment rules
  (RoomTypeAssigner.java:65-115). Quotas: shop ×0.05, rest ×0.12, treasure
  ×0.0, event ×0.22, elite ×0.08 (×1.6 at A1+, AbstractDungeon.java:571-573),
  monster = remainder; fixed rows: 0 = monster, 8 = treasure, 14 = rest
  (AbstractDungeon.java:525-530, 558-594).
- **? rooms:** EventHelper.roll (EventHelper.java:100-187) — one
  `eventRng.random()` float against a 100-slot table; base MONSTER 0.1 /
  SHOP 0.03 / TREASURE 0.02 with pity growth on miss and reset on hit; then
  `generateEvent` (AbstractDungeon.java:1864-1880): `shrineChance` 0.25
  (:2715), event/shrine pick + **removal from the pool lists**
  (:1944-1990, :1882-1942).
- **Events:** 11 Exordium (Exordium.java:223-236), 6 shrines
  (Exordium.java:238-246), 14 shared one-time (AbstractDungeon.java:
  1340-1358, incl. conditional NoteForYourself) — each event's own
  reachability conditions (floor/hp/gold gates, AbstractDungeon.java:
  1949-1980) filter the actual Act-1-reachable set at task time.
- **Neow** (NeowEvent.java, NeowReward.java): dedicated
  `rng = new Random(Settings.seed)` (NeowEvent.java:363; :289); four options
  = categories 0–3 (NeowReward.getRewardOptions :85-128), category 2 rolls a
  drawback first (:76-83, :107); payouts consume NeowEvent.rng for cards but
  `relicRng`/`potionRng` pools for relics/potions (NeowReward.java:190-368).
  Category 3 (boss-relic swap) is **in S1 scope** — it is on screen every
  run, so excluding it would diverge on every seed; this is what pulls the
  Ironclad boss-relic pool into §5.3.
- **Shop** (ShopScreen.java): 5 colored + 2 colorless cards, 3 relics
  (2 rolled-tier + 1 shop-tier, :360-372), 3 potions (:374-384); prices =
  base × `merchantRng.random(0.9f,1.1f)` (cards, :246-276; colorless ×1.2
  :265) / ×(0.95,1.05) (relics :368, potions :380); one sale card at half
  price (`merchantRng.random(0,4)`, :272-273); purge base 75 +25 ramp
  (:100-102, 278-292); A16 ×1.1 (:227-229).
- **Rewards:** boss gold 100±5 via **miscRng** (AbstractRoom.java:291), ×0.75
  at A13+ (:293); elite 25–35 and normal 10–20 via **treasureRng**
  (:316, :324); card rewards 3 × rarity roll `cardRng.random(99) +
  cardBlizzRandomizer` against 3/37/60 (AbstractDungeon.java:1597-1624,
  AbstractRoom.java:108-109, 148-177); upgrade chance 0.0 in Act 1
  (Exordium.java:107 — A12 is a no-op in S1, §6).
- **Treasure:** chest size 50/33/17 (Exordium.java:100-102 via
  AbstractDungeon.java:499-508); **one** `treasureRng.random(0,99)` drives
  both gold-presence and relic tier (AbstractChest.java:54-60; per-chest
  tables SmallChest/MediumChest/LargeChest.java:18-22); gold amount
  ×(0.9,1.1) (:72).
- **Rest:** heal = 30 % of max HP (`0.3f`, RestOption.java:25); smith =
  upgrade grid; no RNG on the menu itself (CampfireUI.java:81-107); relic
  options only for relics S1 implements.
- **Ironclad loadout:** 80 HP, 99 gold, 5-card draw, starter deck 5×Strike /
  4×Defend / 1×Bash, Burning Blood (Ironclad.java:113-115 `getLoadout`;
  deck/relic methods same file — read in full at task time).

## 6. A20 modifier table (registry shape + verified sources)

`a20.yaml` entry shape (frozen):
`{id, level, scope: run-setup | monster | economy | event, mechanic,
provenance, notes}` — one row per modifier, **numbers live in the YAML only
after the cited line has been read during that row's task** (stage-a §11
rule). The verified source map, from a dedicated survey of `ascensionLevel`
branches:

| Level | Effect (Act-1-relevant) | Provenance |
|---|---|---|
| A1 | elite map-count ×1.6 | AbstractDungeon.java:571-573 |
| A2/A3/A4 | normal/elite/boss damage & move params | per-monster ctor/getMove branches (`>=2`/`>=3`/`>=4`) |
| A5 | between-act heal = 75 % of missing HP | AbstractDungeon.java:2582-2586 (Act-1 entry: full heal unaffected at start; matters at S1 boundary only as the *post-boss* heal, S2) |
| A6 | start act at 90 % HP | AbstractDungeon.java:2594-2596 |
| A7/A8/A9 | normal/elite/boss HP | per-monster ctor branches (`>=7`/`>=8`/`>=9`; some bosses add `>=19` tiers) |
| A10 | Ascender's Bane added to deck | AbstractDungeon.java:2597-2600 |
| A11 | one fewer potion slot | AbstractPlayer.java:211-213 |
| A13 | boss gold ×0.75 | AbstractRoom.java:291-296 |
| A14 | −5 max HP (Ironclad) | AbstractDungeon.java:2591-2593 + Ironclad.java:168-170 |
| A15 | harsher event branches, per event | e.g. WomanInBlue.java:47-48, 96-98; each event's own `>=15` branch |
| A16 | shop prices ×1.1 | ShopScreen.java:227-229 |
| A17/A18/A19 | normal/elite/boss special behavior | per-monster `>=17`/`>=18`/`>=19` branches (e.g. Cultist.java:95, GremlinNob.java:92-93, Lagavulin.java:83, TheGuardian.java:97-107, SlimeBoss.java:125) |

Per-monster examples verified: Cultist.java:59/66/95; JawWorm.java:81/86/92;
LouseNormal.java:55-68/95/130; GremlinNob.java:67/72/92-93/133;
Lagavulin.java:77/82/83; TheGuardian.java:97-107/185; SlimeBoss.java:89/94/125.
Monster registry entries encode these as the per-tier columns of §4.2, each
column citing its branch.

**Negative results, frozen so nobody "fixes" them in later reading:** no
ascension scaling exists on campfire heal, potion-drop chance, normal/elite
combat gold, or card-reward *rarity*; **A12 (upgraded-card chance) is a no-op
in Act 1** (Exordium pins `cardUpgradedChance = 0.0f`, Exordium.java:107);
A20's double boss is Act-3-only (ProceedButton.java:102,
AbstractMonster.java:1059) — out of S1. Tier-2 tests pin the negatives too.

## 7. Continuous verification operation (InitialPlan B.3, adapted honestly)

### 7.1 The single-machine reality

InitialPlan B.3 assumed a two-machine setup with a dedicated 3600X node
fuzzing 24/7. The project's actual Stage B footprint is **one Windows
machine** that is simultaneously the dev box, the WSL build host, and the
only place the oracle game can run (Steam install + workshop mods live here;
the game needs a display; WSLg is unavailable on this Windows 10 build —
§2.2). InitialPlan's B.3 as written does not fit; pretending otherwise would
just produce an unrunnable ledger. **Adapted operating model (frozen):**

1. **CI (every commit, unchanged + one addition):** tiers 1–2 in the
   existing debug+asan matrix, plus a **50-seed sim-replay smoke** — replay
   of a committed, curated corpus of translated oracle traces (compressed;
   the only oracle artifacts in the repo) with zero-diff assertion. Pure
   sim-side, no game process in CI, runtime seconds.
2. **Nightly/idle campaigns (unattended, scripted):** the fuzz driver runs
   when the machine would otherwise idle — sim-side self-replay fuzzing at
   full speed (millions of actions/hour; catches asserts, nondeterminism,
   memory corruption via stage-a §2's replay-twice-hash-compare) and
   oracle campaigns at bridge speed (§2.2 budget: 1M oracle-diffed actions ≈
   50–200 unattended hours — one to three weeks of nights). Campaign state is
   resumable; a crashed game process costs one run, not the campaign.
3. **If a second machine materializes**, the campaign scripts already
   shard by seed range — nothing in the design assumes one host. But no task
   in the ledger depends on hardware that doesn't currently exist.

### 7.2 Generators

Per §3.3: random-legal (both sides), directed scripts (shipped per content
batch), heuristic policies. Seed scheduling: sequential sweep for coverage +
a pinned frozen set (the eventual E-rung eval seeds are never fuzzed —
InitialPlan §2.3's hygiene, honored from the start).

### 7.3 Artifacts

Campaign outputs (JSONL, translated traces, diff reports) live under a
non-repo data root (`D:\STS_BG_Mod\SpeedTheSpire-campaigns\` or similar,
fixed at B5.2); the repo holds the curated CI corpus, reproducers promoted to
regression fixtures, and generated reports.

### 7.4 The "divergence dashboard", right-sized

A generated markdown/CSV report, not a service: diffs per million actions,
divergence inventory (open/closed with reproducer links), and **coverage per
registry entry** — for each row, how many times its `game_id` appeared in
oracle campaign states vs. tier-2/tier-3 exercise (the §4.3 manifest joined
against campaign logs). Regenerated per campaign by `tools/verify_report/`;
the latest report is committed to `docs/verification/` so the ledger's gates
can cite it. "Registry entries with zero oracle sightings" is the working
to-fuzz list.

### 7.5 Triage protocol (unchanged from Stage A, now with teeth)

Divergence → auto-saved `(seed, action-prefix)` reproducer (A6.1 machinery)
→ stop-the-line per the hygiene rules → root-cause via §1.3 precedence
(re-read the cited Java first; suspect the strip patches second) → fix →
reproducer becomes a permanent regression fixture in the CI corpus. RNG
divergence anywhere remains stop-the-line (InitialPlan A.4).

## 8. Definition of done for S1 (InitialPlan B.4, frozen as the exit bar)

Gate G7 (= InitialPlan milestone M4) requires **all** of:

1. **≥ 1,000,000 oracle-diffed actions across ≥ 2,000 distinct seeds with
   zero un-triaged state diffs** — full Act-1 A20 Ironclad runs (Neow through
   boss-reward claim), mixed generators (§7.2). Sim-side self-replay fuzz
   (≥ 10M actions, zero nondeterminism/asserts) runs in addition, not as a
   substitute. If bridge throughput lands below the 5 actions/s floor, this
   criterion must be amended by change-log entry with new math — it does not
   silently shrink (§2.2).
2. **100 % tier-2 registry coverage** — every registry row has its named
   passing test(s), counted against the §4.3 manifest; native branches
   included.
3. **All A20 modifiers verified** — every `a20.yaml` row (§6) has provenance
   read, implementation, tier-2 coverage, and appears exercised in at least
   one zero-diff oracle campaign run (A20 is the only ascension the
   campaigns run, so this falls out of criterion 1's corpus plus the §7.4
   coverage join).
4. **Throughput ≥ InitialPlan §0.2 floors on this machine:** ≥ 50k combat
   steps/sec/core (interpreter, no NN), ≥ 300 full combats/sec/core (random
   policy), ≥ 0.4 full A20 runs/sec whole-machine at 25-sim MCTS-equivalent
   load (the last measured as random-policy full runs until the MCTS harness
   exists — recorded with methodology). Hardening to *target* levels remains
   Stage C.

M-milestone mapping: G4 = M2 (oracle bridge; tier-1 was already green at G1),
G6 = M3 (S1 rules complete), G7 = M4 (S1 verified).

## 9. Deliberately deferred (with reasons, so deferral ≠ drift)

- **Live TCP/socket oracle adapter** — until a generator needs in-loop
  decisions (§2.1, §3.3).
- **Boss chest & act transition, acts 2–4, keys** — S2/S3 scope (§1.1).
- **Trained-agent adversarial generator** — E2 output; heuristic policies
  stand in (§3.3).
- **Oracle-harvested distributional testing at scale** — tier 4 runs
  analytic-first (§3.4).
- **Second-machine sharding** — scripts are shard-ready; no hardware
  dependency today (§7.1).
- **Save-file load/restore parity** — the sim doesn't save/load; counters are
  dumped for diffing only (stage-a §3.4's quirk note stands).
- **SoA re-bucketing, CUDA port, perf targets** — Stage C, unchanged.

## 10. Bit-exactness trap list — Stage B additions (each becomes a named test)

Continuing stage-a §10's numbering:

12. Map room-type shuffle passes the raw `RandomXS128` to
    `Collections.shuffle` (RoomTypeAssigner.java:135) — xorshift-driven
    Fisher–Yates via the overridden `nextInt(bound)`, **counter does not
    advance**. Do not route it through the JDK-LCG path used by deck/pool
    shuffles (stage-a §3.3), and do not "fix" the counter.
13. `AbstractDungeon.rollRarity(Random rng)` **ignores its parameter** and
    always draws `cardRng` (AbstractDungeon.java:1597-1598).
14. Potion identity is **rejection-sampled**: `returnRandomPotion(rarity)`
    loops `getRandomPotion()` until the tier matches, consuming a variable
    number of `potionRng` draws (AbstractDungeon.java:840-850).
15. Relic pools consume `relicRng` exactly 5× at dungeon init (one
    `randomLong` per tier shuffle, AbstractDungeon.java:1237-1241); draws
    afterward are rng-free pops — **front** for rewards, **end** for shop
    (AbstractDungeon.java:757-808 vs 704-755).
16. One `treasureRng.random(0,99)` decides both a chest's gold presence and
    its relic tier (AbstractChest.java:54-60) — not two rolls.
17. Neow rolls on a **fresh `Random(Settings.seed)`** (NeowEvent.java:289,
    363), not a dungeon stream; its draws never touch run-stream counters.
18. Encounter composition (louse variants, slaver color, gremlin order,
    slime mixes, thugs/wildlife slots) consumes **miscRng**
    (MonsterHelper.java:614-836), and boss gold consumes **miscRng** while
    elite/normal gold consume **treasureRng** (AbstractRoom.java:291, 316,
    324) — stream attribution is part of the spec.
19. EventHelper's ?-room pity chances are **floats** accumulating by
    0.1/0.03/0.02 (EventHelper.java:143-181) — replicate float arithmetic,
    not rational bookkeeping; and the ?-roll is `eventRng.random()` (a float
    draw), not an int roll (EventHelper.java:102).
20. The boss list order depends on `UnlockTracker.isBossSeen` before the
    `monsterRng` shuffle (Exordium.java:195-221) — the sim's fully-unlocked
    assumption (§1.1) must hold in the oracle profile or every boss diff is
    noise.

## 11. Change log

- v0.1 (2026-07-17) — initial freeze. Grants recorded: nlohmann/json
  (tools-only, §2.6), PyYAML (codegen-only, §4.3). Precedence chain amended
  per §1.3 (live game above decompiled Java, with reproduction guard rails).
  Trap list extended 12–20 (§10). Environment assumption frozen:
  fully-unlocked profile (§1.1).
- v0.1.1 (2026-07-17) — B0.1 protocol survey. Resolved the CommunicationMod
  `[confirm at B0.1]` items against the vendored upstream source, pinned at
  tag `v1.2.1` = commit `70ca84b1e8daff3eb4fe7f66775ce39926133c7f` (matches
  the local workshop jar item 2131373661, `ModTheSpire.json` version 1.2.1,
  and the source `pom.xml` `<version>1.2.1</version>`). Full command grammar
  and the complete `GameStateConverter` field-disposition catalog are in
  `tools/oracle_bridge/PROTOCOL.md`.
  1. **Message framing / direction** (§2.1, §2.3). State JSON is written to
     the child process's **stdin** and commands are read from its **stdout**,
     each message terminated with a newline (`'\n'`); the reader additionally
     treats NUL (`\0`) as a message delimiter. Settling source lines:
     `DataWriter.run` writes each queued state message to the stream and
     appends `'\n'` then flushes (DataWriter.java:31-33); `DataReader.run`
     reads the stream char-by-char and breaks a message on `'\n'` or `0`
     (DataReader.java:28-34); the two threads are wired to
     `listener.getOutputStream()` / `listener.getInputStream()` respectively
     in `CommunicationMod.startCommunicationThreads` (CommunicationMod.java:
     222-227). `ready_for_command` is `GameStateListener.waitingForCommand`
     (GameStateConverter.java:54; GameStateListener.java:235-237), set true
     when a stable state change is detected and cleared on command execution.
  2. **`start` seed argument** (§2.3, stage-a §3.5). The seed token is the
     game's **base-35 display string**, **not** a raw long: it is uppercased,
     validated against `^[A-Z0-9]+$`, and converted to the internal long via
     `SeedHelper.getLong(seedString)`. Settling source lines:
     CommandExecutor.java:353-359 (`start <character> [ascension] [seed]`
     parsing); trial seeds are detected via `TrialHelper.isTrialSeed` and
     routed to `Settings.specialSeed` with `seedSet=false` (:360-365). Note
     `SeedHelper.generateUnoffensiveSeed` supplies a random seed when none is
     given (:367-369).
  Also confirmed at B0.1: the `LICENSE` file is MIT ("MIT License",
  Copyright (c) 2019 ForgottenArbiter) — the one vendored source tree allowed
  under the working rules, kept intact.
- v0.1.2 (2026-07-23) — B1.3 rendering-strip acceptance refinement + obtain-race
  fix. §2.2's acceptance ("byte-identical oracle state dumps with patches on vs.
  off") is refined to: the **§2.5 oracle block byte-identical, AND all stock
  `GameStateConverter` fields byte-identical EXCEPT a closed, enumerated set of
  presentation fields**, each dually proven (a) derived-redundant with a
  byte-identical semantic field / already phase-specified by disposition, AND
  (b) transient (converges to identity by a later dump). This re-expresses the
  frozen guard "remove time and pixels, never order or state" for a dump that
  happens to carry presentation fields; it is **not** a weakening — over the
  B1.3 20-seed A/B (strip-off vs strip-on, same fork build) **no logical field
  differs**: all 14 RNG streams, both pity counters, event pity floats, purgeCost,
  the pool orders, `neowRng`, per-monster move history, HP, master-deck contents,
  gold, map, and living-monster powers are byte-identical. Enumerated
  presentation fields (evidence in the B1.3 ledger Log):
    · monster `intent` — the `AbstractMonster.Intent` display of the current
      move; semantic anchor is `move_id` (byte-identical in every observed
      diff). Reads `DEBUG` until the intent banner refreshes
      (`BattleStartEffect`/`PlayerTurnEffect` → `showIntent`), a phase the strip
      timing shifts (18/20 stripped seeds carry a `DEBUG`-at-`ready_for_command`
      intent on a *living* monster with a valid `move_id`; stock hits it too).
    · monster `move_adjusted_damage` — "shown intent damage"; equals `-1` exactly
      when `intent`==`DEBUG`; display-coupled to `intent`.
    · residual monster `powers` on a monster both dumps mark dead (`is_gone` /
      `hp<=0`, byte-identical) — cleanup lag on an inert, about-to-be-removed
      entity.
    · in-flight card zone membership (`deck`/draw/discard/hand counts) while a
      card animates between zones — full card multiset across all zones
      conserved; converges next dump.
    · `neowRng` stream *presence* before the Neow blessing screen — §2.5 already
      specifies this key is absent until blessing; value byte-identical when
      present.
  Reclassification of `intent` and `move_adjusted_damage` from disposition `S`
  (semantic) to derived in PROTOCOL.md §3 and the B1.5 translator (`move_id` the
  semantic anchor) is a **queued follow-up task** (docs + `translate.cpp` +
  tests in one commit); flagged here, deliberately not done in the B1.3 commit.
  **Obtain-race fix** (separate fix-forward commit `fork: gate readiness on
  pending obtain effects`): the game reported `ready_for_command` before a
  `ShowCardAndObtainEffect` committed its card to the master deck
  (`vfx/cardManip/ShowCardAndObtainEffect.java:94-108` → `cards/Soul.java:145-148`),
  so a fast-navigating driver could **drop** an obtained card (observed: the
  Golden Idol `[Outrun]` Injury — stock+driver dropped it, master deck stayed 13;
  stripped timing kept it, 14). `GameStateListener.hasDungeonStateChanged` now
  holds readiness while any obtain effect is pending, making the master deck
  deterministic regardless of animation speed. This race means pre-fix captures
  (e.g. b14_accept2) may have dropped cards wherever the driver navigated during
  an obtain — flagged for B5.2 triage; B1.4's acceptance (driver mechanics) is
  unaffected.
- v0.1.3 (2026-07-23) — B4.1 map-path-generation live-game override (§1.3:
  reproduced live-game observation > decompiled Java > design docs). §1.1's
  "Out (S2+)" list previously claimed `setEmeraldElite` "is likewise
  final-act-gated … never fires in S1". **Corrected: it fires every act on the
  fully-unlocked profile**, consuming exactly one `mapRng` draw
  (`mapRng.random(0, eliteNodes.size()-1)`, AbstractDungeon.java:551) **after**
  room assignment (called from `generateMap`, AbstractDungeon.java:539). Its
  guard is `Settings.isFinalActAvailable && !Settings.hasEmeraldKey` (:543):
  `isFinalActAvailable = IRONCLAD_WIN && SILENT_WIN && DEFECT_WIN && !isDailyRun
  && !isTrial || CustomModeScreen.finalActAvailable` (Settings.java:642) is TRUE
  on the frozen fully-unlocked profile (§1.1), and no emerald key exists at
  act-1 start — so the guard passes. The earlier reading conflated
  "needs the 3 keys to *enter* Act 4" with `isFinalActAvailable` (a
  *profile-unlock* condition the fully-unlocked assumption guarantees). **Live
  evidence:** over the 20-seed A20-Ironclad corpus `b13_on20b`, every seed's
  post-`generateMap` `mapRng.counter` is EXACTLY the B4.1 path-generation
  counter + 1, independent of the per-seed ancestor-re-roll counts (1..12) and
  walk-1 re-rolls; `RoomTypeAssigner`'s only `mapRng` use is the trap-12 raw
  `Collections.shuffle` (no counter advance, RoomTypeAssigner.java:135), so the
  +1 is uniquely attributable to `setEmeraldElite`. This overturns the scoping
  report's H6/R3. **Scope impact:** B4.1 (path generation) is unaffected — the
  draw is post-room-assignment. **B4.2 (room-type assignment) MUST model the
  `setEmeraldElite` draw** (after the shuffle) to reach the full
  `{counter, s0, s1}` oracle state; its `mapRng` counter target is
  `path-gen + 0 (shuffle) + 1 (emerald)`. Evidence + the named tests are in the
  B4.1 ledger Log (docs/stage-b-tasks.md) and `tests/map_gen_test.cpp`
  (`PathGenCounterMatchesOracleMinusEmerald`, `HandDerivedPathDrawCountSingleSeed`).
