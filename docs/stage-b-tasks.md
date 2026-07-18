# Stage B Task Ledger

Execution tracker for [stage-b-design.md](stage-b-design.md) (the frozen Stage
B spec — **this file never overrides it**; on any conflict, the design doc wins
and this file gets fixed). [stage-a-design.md](stage-a-design.md) remains
frozen and in force for everything it covers. Target milestones: **M2** (oracle
bridge live, gate G4), **M3** (S1 rules complete, gate G6), **M4** (S1
verified, gate G7) — InitialPlan's milestone table, S1 = Ironclad / Act 1 /
A20.

## Orchestrator protocol

- Statuses: `[ ]` todo · `[~]` in progress · `[x]` done · `[!]` blocked.
  Update the checkbox and the **Log** line of a task when its state changes.
- A task is **done only when its Acceptance block passes** — run the commands,
  don't infer. Tests land in the same change as the code they verify.
  Registry YAML is code: entries land with their tier-2 tests in one commit.
- Respect `Deps:`. Tasks with disjoint deliverables and satisfied deps may run
  in parallel (parallel-safe groups are marked ∥).
- **Gates** (G4–G7, continuing Stage A's numbering) are stop-the-line: nothing
  past a gate starts until the gate task is `[x]`. Phase B3/B4 content work
  additionally requires **both** G4 and G5 (design §3.2: bridge first, then
  registry migration, then mass content).
- Every commit builds and tests green: `cmake --preset debug && cmake --build
  --preset debug && ctest --preset debug` (WSL Ubuntu-2404). Run the `asan`
  preset before marking any task with new engine code done. Bridge-side
  components (fork jar, Python driver) build/run on the **Windows host** (JDK
  8 / Python 3) and are excluded from WSL CI; their acceptance commands say so
  explicitly.
- Provenance: any behavior taken from the game cites
  `ClassName.method (File.java:line)` into `D:\STS_BG_Mod\SlayTheSpireDecompiled`
  (stage-a §1). If implementation contradicts the design docs' reading of the
  Java, stop and re-read the cited source before coding around it. Once G4 is
  `[x]`, a reproduced live-game observation outranks the decompiled Java per
  design §1.3 — record every such override in the design-doc change log.
- Layout: engine as in Stage A; registry sources `registry/*.yaml`; codegen
  `tools/registry_gen/`; bridge `tools/oracle_bridge/` (fork source under
  `communicationmod-oracle/`, driver under `driver/`, translator under
  `translator/`); distributional tests `tools/dist_check/`; reports
  `tools/verify_report/` + `docs/verification/`.

## Working agreements (enforced on every task)

### Git discipline

- Work on `master` (local repo, no remote; if a remote appears, push tags too).
- **One task = one commit**, made at task completion. Gates always get their
  own commit. The ledger's checkbox + Log update goes **in the same commit**
  as the task's code — history and ledger never disagree.
- Commit message: subject `B1.2: <what changed>`; body lists the acceptance
  evidence (test target names that ran green, incl. asan; for bridge tasks,
  the Windows-host command that ran and its recorded result) and cites
  provenance for any behavior derived from the game. Claude-authored commits
  carry the `Co-Authored-By: Claude` trailer.
- Commit only from a green tree — debug **and** asan presets pass; release
  too at gates. Never `--no-verify`, never amend or rebase committed work;
  fix forward.
- Tag gates: `g4-bridge-live` (= M2), `g5-registry-live`, `g6-s1-content`
  (= M3), `g7-s1-verified` (= M4).
- **Never commit:** decompiled Java or anything from `sts-classes.jar` /
  `desktop-1.0.jar` (license hygiene — the repo re-expresses, never copies),
  built jars (incl. the fork jar), campaign artifacts (raw JSONL or translated
  traces — they live under the §7.3 data root), scratch files, `build/`.
  Exceptions, all small and reviewed: golden vectors (`tests/golden/`, as in
  Stage A), the curated CI oracle corpus (compressed, `tests/golden/
  oracle_corpus/`), promoted regression reproducers, generated verification
  reports (`docs/verification/`). The vendored fork *source* is committed
  (upstream is MIT — verified at B0.1) with its license file intact.

### Canonical references (reading order when picking up a task)

1. This ledger — the task's Deps/Deliverables/Acceptance.
2. [stage-b-design.md](stage-b-design.md), the cited §§ — the Stage B frozen
   spec.
3. [stage-a-design.md](stage-a-design.md) — frozen mechanics (RNG, state,
   queue, damage, schema) that Stage B builds on.
4. The decompiled Java at `D:\STS_BG_Mod\SlayTheSpireDecompiled` — all
   `File.java:line` citations resolve here. Read the cited method *before*
   implementing it, even when a design doc paraphrases it.
5. The live game via the oracle bridge (once G4 is `[x]`) — the runtime
   oracle. **Precedence on conflict** (design §1.3): reproduced live-game
   observation > decompiled Java > design docs > this ledger > InitialPlan.
   A live-game override needs a reproducer, a second reproduction, and a
   strip-patch audit before it wins.
6. [InitialPlan.md](../InitialPlan.md) — intent and rationale only, never
   mechanics.
7. Toolchains: JDK 8 (`C:\Program Files\Java\jdk1.8.0_171`) builds the fork;
   Java 25 runs golden capture (stage-a A0.1); Python 3 runs the driver and
   codegen; the game + mods live in the Steam dirs cited in design §1.2.

**Discovering a conflict is stop-the-line:** fix the losing document in the
same change and record it in the task Log and the change log below (and in
the design doc's §11 / stage-a §12 when a design doc is the loser).

### Hygiene

- **No rule without its test** — the test lands in the same commit as the
  behavior. For registry entries the test is the tier-2 table test; for
  bridge/campaign code the test is the recorded acceptance run.
- Golden mismatch, fixture divergence, or **campaign divergence** =
  stop-the-line. Debugging step 1 is re-reading the cited Java; step 2 is
  auditing the fork's strip patches (design §1.3); root cause goes in the
  task Log, reproducers get promoted to regression fixtures.
- Registry ids and opcode numbers are **append-only** (design §4.4). Any
  change that would renumber an existing id is forbidden without a design-doc
  change-log entry and a schema-version bump.
- No new third-party dependencies beyond those granted in design §2.6/§4.3
  (nlohmann/json tools-only; PyYAML codegen-only) without a design-doc
  change-log entry.
- Session-start ritual for any executing agent: `git status` + `git log
  --oneline -5`; a dirty tree or a `[~]` task with an empty Log gets
  investigated before new work starts.
- At each gate: update CLAUDE.md "Current state" so a fresh session orients
  correctly without reading history.

---

## Phase B0 — Bridge groundwork

### B0.1 `[x]` ∥ CommunicationMod source pin + protocol survey
**Deps:** none · **Spec:** design §2.3, §2.6-2.7 · **Provenance:** local jar
(workshop 2131373661, v1.2.1); upstream `ForgottenArbiter/CommunicationMod`
**Deliverables:** upstream source fetched and pinned at the commit matching
v1.2.1, vendored to `tools/oracle_bridge/communicationmod-oracle/` with
license file (verify MIT); `tools/oracle_bridge/PROTOCOL.md` documenting, from
the actual source: message framing (stdin/stdout direction, line-delimited
JSON, `ready_for_command` semantics), the full command grammar (esp. `start`
arg syntax — character/ascension/seed, and whether seed is base-35 string or
raw long, design §2.3's open item), the complete `GameStateConverter` JSON
field catalog with a disposition for every field (schema-mapped /
ignored-with-reason / oracle-block, per design §2.6's fail-loudly policy).
**Acceptance:** PROTOCOL.md's field table covers every field emitted by
`GameStateConverter` (checked against source, not samples); the two design-doc
**[confirm at B0.1]** items are resolved and recorded in the design-doc change
log; license verified.
**Log:** Verified against source, not inferred: cloned upstream
`ForgottenArbiter/CommunicationMod`, pinned at tag `v1.2.1` = commit
`70ca84b1e8daff3eb4fe7f66775ce39926133c7f` (matches local workshop jar
2131373661 `ModTheSpire.json` v1.2.1 / sts 11-30-2020, and source `pom.xml`
v1.2.1). Vendored source-only to `tools/oracle_bridge/communicationmod-oracle/`
with `LICENSE` intact — **MIT** ("MIT License", Copyright (c) 2019
ForgottenArbiter); no jars/`.class`/build artifacts committed (only source +
626 B `Icon.png` build-input resource). Wrote `tools/oracle_bridge/PROTOCOL.md`
from source: framing (state JSON → child **stdin**, commands ← child **stdout**,
`\n`/NUL-delimited — DataWriter.java:31-33, DataReader.java:28-34,
CommunicationMod.java:222-227), `ready_for_command` = `waitingForCommand`
(GameStateListener.java:235-237), full command grammar, and the complete
`GameStateConverter` field-disposition catalogue. Both `[confirm at B0.1]`
items resolved + recorded in design §11: (1) message framing/direction;
(2) `start` seed = base-35 display string, **not** raw long
(CommandExecutor.java:353-359). Coverage: 155 `.put` sites → 141 distinct
(container,key) fields, every one has a table row + disposition; verified via
`grep -noE '\.put\("[^"]+"'`. `GameStateConverter.java` is the sole game-state
JSON emitter.

### B0.2 `[ ]` ∥ Stock-jar bridge bring-up + environment audit
**Deps:** none · **Spec:** design §2.3, §1.1 · **Provenance:** config at
`%LOCALAPPDATA%\ModTheSpire\CommunicationMod\config.properties`
**Deliverables:** `tools/oracle_bridge/driver/echo_driver.py` — minimal child
process that logs every state JSON and can send hand-typed commands; a
documented, scriptable game-launch command line (ModTheSpire + BaseMod +
stock CommunicationMod from the workshop paths, design §1.2); one complete
manual seeded run (`start` → Neow → several floors) captured as JSONL;
recorded baseline actions/sec with the stock (unstripped) game; **profile
unlock audit** — verify the save profile is fully unlocked (bosses, cards,
relics; design §1.1) and record the evidence (or the chosen remedy) in the
Log.
**Acceptance:** captured JSONL replays cleanly through `echo_driver.py`
parsing; the same seed string typed into the game's own seeded-run UI and via
`start` produce the same floor-1 state JSON; baseline throughput number
recorded in this file.
**Log:** —

---

## Phase B1 — The oracle fork, driver, translator (Gate G4 = M2)

### B1.1 `[ ]` Fork build pipeline
**Deps:** B0.1, B0.2 · **Spec:** design §2.4
**Deliverables:** JDK-8 build script (`tools/oracle_bridge/build_fork.ps1` or
equivalent) compiling the vendored fork against `desktop-1.0.jar` +
ModTheSpire + BaseMod from the local install; jar output to a non-committed
location ModTheSpire picks up; fork modid distinct from upstream
(`CommunicationMod-oracle`) so both can't load together confusingly.
**Acceptance:** fork jar, with zero behavioral patches yet, reproduces B0.2's
captured run byte-for-byte (same seed, same script → same JSONL states);
build script is deterministic and documented in the fork README.
**Log:** —

### B1.2 `[ ]` Oracle state block
**Deps:** B1.1 · **Spec:** design §2.5 (the frozen 10-row field inventory) ·
**Provenance:** AbstractDungeon.java:149-161; Random.java:17-18;
NeowEvent.java:62/289/363; AbstractDungeon.java:247-250;
AbstractRoom.java:100-101; EventHelper.java:88-92; ShopScreen.java:100-102;
AbstractDungeon.java:182-184, 1221-1256; monster move-history fields (read
here)
**Deliverables:** fork patch emitting the `"oracle"` JSON block (all §2.5
rows: 13 dungeon streams + Neow rng as `{counter,s0,s1}`, both pity counters,
event pity floats, purgeCost, remaining event/shrine/special lists, relic
pool orders ×5, per-monster move history, seed/floor/act/ascension anchors)
behind a fork config flag; PROTOCOL.md updated with the block's schema.
**Acceptance:** for one scripted run: relicRng counter reads exactly 5 after
dungeon init (AbstractDungeon.java:1237-1241); floor-scoped stream `(s0,s1)`
at floor N equals the sim's `floor_stream(seed, N)` (tier-1-tested) for
floors 1-3; blizzardPotionMod visibly ratchets across two combat rewards;
event-list contents shrink after an event fires. Each checked from the dump,
recorded in the Log.
**Log:** —

### B1.3 `[ ]` Rendering-strip / fast-forward patches
**Deps:** B1.2 · **Spec:** design §2.2 (semantic guard; throughput floor)
**Deliverables:** the strip patch family (draw suppression, animation-time
collapse, fast update cadence) in the fork, individually toggleable via fork
config; throughput measurement script in the driver.
**Acceptance:** ≥ 20 seeds × scripted multi-floor runs: oracle dumps (incl.
the full §2.5 block) are **byte-identical** with strip patches on vs. off;
sustained throughput ≥ 5 actions/sec (floor), number recorded here; if the
floor is unreachable, stop and amend design §2.2/§8 per its own rule before
proceeding.
**Log:** —

### B1.4 `[ ]` Campaign driver
**Deps:** B1.2 · **Spec:** design §2.7, §3.3 (generators), §7.2
**Deliverables:** `tools/oracle_bridge/driver/` grows into the real driver:
seeded A20 Ironclad starts; action-script execution (scripted sequences) and
random-legal generation from the game's own accepted-command set; JSONL
artifacts per design §2.7 (version-stamped header incl. fork-jar hash, seed
both encodings); crash detection + game restart + campaign resume; batch
mode over seed lists.
**Acceptance:** unattended 10-seed campaign (full Act-1 runs, random-legal)
completes without manual intervention, surviving at least one deliberately
induced game kill mid-campaign; artifacts validate against the PROTOCOL.md
schema.
**Log:** —

### B1.5 `[ ]` Translator (JSON → binary schema)
**Deps:** B1.4 · **Spec:** design §2.6 · **Provenance:** PROTOCOL.md field
table (B0.1)
**Deliverables:** `tools/oracle_bridge/translator/` C++ target (nlohmann/json,
tools-only per the §2.6 grant): campaign JSONL → trace files; the full
field-disposition table enforced (unknown field = hard error); skeleton-set
hand table for id mapping (replaced by generated tables after B2.2);
`RunState` fields Stage A left placeholder get their translation where the
schema already has storage — fields needing **new** RunState storage are
listed in the Log for B4.3 (they translate to the trace only after B4.3
lands; the translator versions its output accordingly).
**Acceptance:** gtest `translator_test` (WSL, runs on committed sample JSONL):
every §2.5 oracle field lands in the right schema field bit-for-bit
(counters, s0/s1, pity values); an artifact with an unknown field is refused;
round-trip stability (translate twice → identical traces).
**Log:** —

### B1.6 `[ ]` Diff-harness run-level + oracle adapter
**Deps:** B1.5 · **Spec:** design §3.3 · **Provenance:** stage-a §8 (format),
A6.1 (adapter seam)
**Deliverables:** trace format v2: `state_kind` discriminator (combat/run
records in one container), `SCHEMA_VERSION` bump, loader refusal unchanged;
differ gains RunState field groups (deck, relics, potions, gold, map, pool
orders, pity counters, 8 run-level streams) with named-field output;
`CommunicationModOracleAdapter` (file-based over translated traces, both
state kinds); `replay` generalized to seed a sim replay from any translated
`RunState` (combat replay from run context lands with B4.4 — here the
adapter + format only).
**Acceptance:** `differ_test` extended: synthetic divergence in every new
RunState field group caught and named; v1 traces (A6.2 fixtures) still load
via a compatibility read or are regenerated by their checked-in generator
with zero diff against the engine (choose in Log; stage-a fixtures must not
be hand-edited); adapter answers `query` correctly for prefix/unknown-seed
cases over a translated campaign sample.
**Log:** —

### G4 `[ ]` **Gate: oracle bridge live (M2)** — tag `g4-bridge-live`
**Deps:** B1.3, B1.6
Checklist (all must hold, evidence linked in Log):
- [ ] 20-seed campaign: every dumped state translates with zero unknown-field
      errors; all 14 stream counters + both pity counters + event pity floats
      + purgeCost present in every record.
- [ ] RNG cross-check across the campaign: floor-scoped stream `(s0,s1)` at
      every floor entry matches sim `floor_stream(seed, floor)`; `mapRng`
      state matches `map_stream(seed, 1)`; relicRng counter = 5 at init —
      i.e. the run-scoped/act-scoped derivations hold against the **live
      game**, not just golden vectors.
- [ ] Strip-patch equivalence (B1.3 acceptance) re-confirmed on the final
      fork build; throughput ≥ 5 actions/sec recorded.
- [ ] WSL CI untouched and green (bridge code adds no CI dependency on the
      game).
Then: update CLAUDE.md "Current state".
**Log:** —

---

## Phase B2 — Registry system + skeleton migration (Gate G5) — ∥ with Phase B1

### B2.1 `[ ]` ∥ Registry schema + codegen tool
**Deps:** none (Stage A G3) · **Spec:** design §4.1-4.3
**Deliverables:** `registry/` YAML schemas for all 8 domains (design §4.1)
with the §4.2 entry shape; `tools/registry_gen/gen.py` (Python 3 + PyYAML)
emitting: the id enums, constexpr effect-program tables (A4.3's exact
`CardDef`/step shape), `game_id`↔enum string tables, and the row-count
manifest; CMake custom-command wiring (generated headers under the build
tree, never committed); generator determinism (sorted iteration, no
timestamps).
**Acceptance:** gtest `registry_gen_test`: running the generator twice
produces byte-identical output; a YAML entry with a duplicate or reused id
fails generation with a clear error; generated headers compile standalone.
**Log:** —

### B2.2 `[ ]` Skeleton migration onto the registry
**Deps:** B2.1 · **Spec:** design §3.2, §4.4 (the stop-the-line decision) ·
**Provenance:** types.hpp / cards.hpp / monster_jaw_worm.hpp as of
`m1-walking-skeleton`
**Deliverables:** the 5 skeleton cards, 3 powers, and Jaw Worm expressed as
registry YAML (ids pinned to their current numeric values: STRIKE=1…
POMMEL_STRIKE=5, STRENGTH=1..WEAK=3, JAW_WORM=1); generated enums replace
`types.hpp`'s hand enums (Action/ActionVerb stay hand-written); `cards.hpp`'s
hand table replaced by the generated table; Jaw Worm stats/moves from
`monsters.yaml` (AI selection may stay native per design §4.2 — the move
*effects* and A17/A7 stat columns are data); `static_assert` pins on every
migrated id in the generated headers.
**Acceptance:** **Stage A's full suite (131 tests) and all 20 combat fixtures
pass in debug + asan with zero test-file edits and zero fixture
regeneration** (design §4.4 acceptance, verbatim). `sizeof(CombatState)`
unchanged; `SCHEMA_VERSION` unchanged.
**Log:** —

### G5 `[ ]` **Gate: registry live** — tag `g5-registry-live`
**Deps:** B2.2
All Stage A tests green through the generated path in debug, asan, release;
hand tables deleted (no dual system); manifest reports exactly the skeleton
row counts; CI runs the generator (PyYAML available in the CI image — extend
the workflow in this commit). Nothing in Phases B3/B4 starts before G4 **and**
G5 are both `[x]`. Then: update CLAUDE.md "Current state".
**Log:** —

---

## Phase B3 — Combat content closure (all tasks additionally gated on G4 + G5)

Each content task ships: registry entries with provenance (every cited
`use()`/ctor/`getMove` **read in full** before encoding), any new
powers/opcodes those entries need (append-only numbering), tier-2 table tests
per entry, a directed tier-3 script added to the campaign script library, and
— once the run layer exists — at least one oracle spot-diff exercising the
batch. Until run-level replay lands (B4.4), oracle spot-diffs for combat
content use combat-state comparison inside bridge runs whose context the
translator can seed (B1.6), or are deferred to the batch's campaign debut and
noted in the Log.

### B3.1 `[ ]` Interpreter/card-mechanics extensions
**Deps:** G4, G5 · **Spec:** design §5.1; stage-a §5-§6 · **Provenance:**
AbstractCard/AbstractPlayer use/cost paths; UseCardAction; read at task
**Deliverables:** engine support the full red set needs beyond the skeleton:
X-cost (energy-consume) cards, multi-hit and ALL_ENEMY targeting,
`CardInstance.flags` bits for exhaust/ethereal/innate/unplayable/retain,
upgrade plumbing (two-row registry lookup by `upgrade` bit), temporary/
conditional cost modifiers, and the `SHUFFLE_IN`/discard-pile card-creation
paths status cards need. New opcodes appended (≥9), documented in interp.hpp.
**Acceptance:** gtest coverage per mechanic in constructed states (incl.
X-cost consuming all energy, AOE hitting only live monsters, hand-cap
interaction with created cards); all existing tests green.
**Log:** —

### B3.2 `[ ]` Power-hook framework completion
**Deps:** B3.1 · **Spec:** stage-a §5.3-5.5 hook order (frozen) ·
**Provenance:** AbstractPower hook inventory; GameActionManager.java:214-249,
329-377 (re-read)
**Deliverables:** the full hook set real content triggers (onPlayCard,
onExhaust, onCardDraw, atEndOfTurn pre/post-card, onAttack/onAttacked,
onDamageReceived, atStartOfTurn pre/post-draw, onDeath…) wired through the
pump in the frozen §5.2/§5.3 order, replacing A4.3's no-op stubs; power
registry schema columns for hook→effect-program bindings; native-hook escape
hatch.
**Acceptance:** ordering tests: player powers → monster powers → relics
(acquisition order) → hand/discard/draw cards on card play (stage-a §5.3);
end-of-turn hand triggers before discard (stage-a §5.4); regression: full
suite green (the stub removal must not shift any fixture).
**Log:** —

### B3.3 `[ ]` ∥ Red commons — attacks
**Deps:** B3.2 · **Spec:** design §5.1 · **Provenance:** cards/red, the
CardRarity.COMMON attack set (enumerate from source at task start; ~12 beyond
the skeleton's Pommel Strike)
**Deliverables:** registry entries (base + upgraded programs) incl. the
mechanically loaded ones: Body Slam (block-scaled damage), Clash
(only-attacks-in-hand playability), Headbutt (discard→draw-top), Heavy Blade
(Strength multiplier), Perfected Strike (per-"Strike" scaling), Sword
Boomerang (random multi-hit via cardRandomRng), Cleave/Thunderclap (AOE),
Wild Strike (shuffle Wound), Anger (copy to discard).
**Acceptance:** tier-2 table test per card (both upgrade rows), hand-computed
from the cited `use()`; trap-10 coverage for Sword Boomerang (dequeue-time
rolls); directed script added.
**Log:** —

### B3.4 `[ ]` ∥ Red commons — skills
**Deps:** B3.2 · **Spec:** design §5.1 · **Provenance:** cards/red COMMON
skills (~6 beyond Shrug It Off)
**Deliverables:** registry entries: Armaments (upgrade-in-combat, grid
choice), Flex (temporary Strength — StrengthDown), Havoc (play top of draw,
exhaust it), True Grit (random/targeted exhaust), Warcry (draw + put-back).
Introduces the in-combat card-choice screen verb plumbing (CHOOSE in combat)
where needed (Armaments+, True Grit+).
**Acceptance:** tier-2 per card; CHOOSE-in-combat legal-action masking tested;
directed script.
**Log:** —

### B3.5 `[ ]` ∥ Red uncommons — attacks
**Deps:** B3.2 · **Provenance:** cards/red UNCOMMON attacks (~12; enumerate)
**Deliverables:** registry entries incl. Blood for Blood (cost falls per HP
loss), Carnage (ethereal), Dropkick/Hemokinesis/Uppercut/Pummel/Rampage
(scaling misc counter), Reckless Charge (Dazed), Searing Blow
(multi-upgrade! — the one card violating the single-upgrade bit; decide and
document the `CardInstance.upgrade` count encoding here), Sever Soul
(exhaust-others), Whirlwind (X-cost AOE).
**Acceptance:** tier-2 per card; Searing Blow's multi-upgrade decision
recorded in the design-doc change log if it touches the schema; directed
script.
**Log:** —

### B3.6 `[ ]` ∥ Red uncommons — skills
**Deps:** B3.2 · **Provenance:** cards/red UNCOMMON skills (~13; enumerate)
**Deliverables:** registry entries incl. Battle Trance (No Draw power),
Bloodletting/Burning Pact/Seeing Red (resource conversion), Disarm/Shockwave
(debuffs), Dual Wield (card copy), Entrench (block double), Flame Barrier
(thorns-on-attack), Ghostly Armor (ethereal), Infernal Blade (random card
gen via cardRandomRng), Intimidate, Power Through (Wounds to hand), Second
Wind (exhaust non-attacks for block), Sentinel (on-exhaust energy), Spot
Weakness (conditional Strength).
**Acceptance:** tier-2 per card; cardRandomRng draw-count tests for the
generators; directed script.
**Log:** —

### B3.7 `[ ]` ∥ Red uncommons — power cards
**Deps:** B3.2 · **Provenance:** cards/red UNCOMMON powers (~11; enumerate)
**Deliverables:** registry entries + their powers: Combust, Dark Embrace,
Evolve, Feel No Pain, Fire Breathing, Inflame, Metallicize, Rage, Rupture,
plus the power-card play path (card→power, no discard).
**Acceptance:** tier-2 per power incl. trigger-order interactions (e.g. Feel
No Pain + Dark Embrace on the same exhaust, list-order resolution per
stage-a §5.5); directed script.
**Log:** —

### B3.8 `[ ]` ∥ Red rares
**Deps:** B3.2 · **Provenance:** cards/red RARE (16)
**Deliverables:** registry entries: Barricade, Berserk, Bludgeon, Brutality,
Corruption, Demon Form, Double Tap, Exhume, Feed, Fiend Fire, Immolate,
Impervious, Juggernaut, Limit Break, Offering, Reaper. Wires the block-decay
Barricade branch A3.1 left structural, Corruption's cost/exhaust rewrite,
Feed/Reaper HP-max/heal opcodes.
**Acceptance:** tier-2 per card; Barricade block-persistence through the
frozen start-of-turn sequence; directed script.
**Log:** —

### B3.9 `[ ]` ∥ Status + curses
**Deps:** B3.2 · **Spec:** design §5.1 (11 curses + 5 statuses) ·
**Provenance:** cards/status (5), cards/curses (the 10 poolable +
AscendersBane)
**Deliverables:** registry entries with their end-of-turn/unplayable/passive
behaviors (Burn damage, Decay/Doubt/Regret/Shame end-turn triggers via the
stage-a §5.4 sentinel path, Normality play-cap, Pain on-card-play, Parasite
on-remove, Writhe innate, Void on-draw energy loss, Ascender's Bane
ethereal-curse); curse pool membership for `returnRandomCurse`.
**Acceptance:** tier-2 per entry; end-of-turn trigger ordering vs. hand
discard tested against the frozen §5.4 order; directed script.
**Log:** —

### B3.10 `[ ]` ∥ Colorless uncommons
**Deps:** B3.2 · **Provenance:** cards/colorless UNCOMMON (20)
**Deliverables:** registry entries for the 20 (Bandage Up, Blind, Dark
Shackles, Deep Breath, Discovery, Dramatic Entrance, Enlightenment, Finesse,
Flash of Steel, Forethought, Good Instincts, Impatience, Jack of All Trades,
Madness, Mind Blast, Panacea, Panic Button, Purity, Swift Strike, Trip —
verify enumeration from source).
**Acceptance:** tier-2 per card; Discovery/Madness cardRandomRng draw
accounting; directed script.
**Log:** —

### B3.11 `[ ]` ∥ Colorless rares
**Deps:** B3.2 · **Provenance:** cards/colorless RARE (15)
**Deliverables:** registry entries for the 15 (Apotheosis, Chrysalis, Hand of
Greed, Magnetism, Master of Strategy, Mayhem, Metamorphosis, Panache, Sadistic
Nature, Secret Technique, Secret Weapon, The Bomb, Thinking Ahead,
Transmutation, Violence — verify from source).
**Acceptance:** tier-2 per card; directed script.
**Log:** —

### B3.12 `[ ]` Multi-monster combat + encounter framework
**Deps:** B3.2 · **Spec:** design §5.2 · **Provenance:** Exordium.java:
110-186 (pools/exclusions), MonsterHelper.java:389-604 + 614-836
(compositions, miscRng), AbstractMonster.java:765-775 (HP roll)
**Deliverables:** `encounters.yaml` (pool weights, exclusion table,
composition programs incl. miscRng draw order); `combat_begin` generalized
from hard-coded Jaw Worm to encounter-driven spawn (positions, HP rolls per
monster in spawn order); weak/strong/elite/boss pool draw logic
(monsterRng + `populateFirstStrongEnemy` exclusions); multi-monster
targeting/legal-action masks (slot × target).
**Acceptance:** gtest: for fixed miscRng states, compositions match
hand-derived draws (louse variants, gremlin order, slime mixes); HP rolls
consume monsterHpRng in spawn order; legal-action mask over dead/alive slots;
existing single-monster tests green.
**Log:** —

### B3.13 `[ ]` ∥ Monsters: Cultist + louses
**Deps:** B3.12 · **Provenance:** Cultist.java (:59/66/95 A-branches),
LouseNormal.java (:55-68/95/130), LouseDefensive.java
**Deliverables:** registry entries (A2/A7/A17 columns cited per branch):
Cultist (Ritual), LouseNormal/Defensive (Curl Up, bite damage roll at spawn
— note the per-instance damage roll's stream and timing, read at task).
**Acceptance:** tier-2 per monster: move tables vs. hand-derived aiRng
sequences (A3.2 fixture pattern); Curl Up block trigger on first attack
damage.
**Log:** —

### B3.14 `[ ]` ∥ Monsters: small/medium slimes
**Deps:** B3.12 · **Provenance:** SpikeSlime_S/M.java, AcidSlime_S/M.java
**Deliverables:** registry entries with A-columns; Slimed-card attacks
(status insertion), Frail application (Spike M), lick debuffs.
**Acceptance:** tier-2 per monster; status cards land in discard per the
cited actions; aiRng draw counts per turn match hand-derivation.
**Log:** —

### B3.15 `[ ]` ∥ Monsters: slavers + Looter + Fungi Beast
**Deps:** B3.12 · **Provenance:** SlaverBlue/Red.java, Looter.java,
FungiBeast.java
**Deliverables:** registry entries: entangle (Red Slaver), Looter's
gold-steal + escape (combat-end-without-death path + stolen-gold return
rules), Fungi Beast Spore Cloud (on-death Vulnerable).
**Acceptance:** tier-2 per monster; escape terminal state distinct from kill
(reward implications tested at B4.5); on-death trigger ordering.
**Log:** —

### B3.16 `[ ]` ∥ Monsters: gremlin gang
**Deps:** B3.12 · **Provenance:** GremlinWarrior/Thief/Fat/Tsundere/
Wizard.java; MonsterHelper.java:737-765
**Deliverables:** registry entries ×5 (Angry thorns, sneaky escape?, fat
weak, shield tsundere protect logic — native where the protect targeting
doesn't fit the table shape, per design §4.2), gang spawn order from B3.12.
**Acceptance:** tier-2 per gremlin; gang composition draws already covered by
B3.12 — here per-monster behavior incl. Tsundere's block-ally logic.
**Log:** —

### B3.17 `[ ]` ∥ Monsters: large slimes + split
**Deps:** B3.14 · **Provenance:** AcidSlime_L.java, SpikeSlime_L.java (split
at ≤ half HP), SlimeBoss split machinery shared reading
**Deliverables:** split framework (mid-combat monster replacement: L →
2×M at current HP, position/queue semantics per the cited actions) + the two
L-slime entries with A-columns.
**Acceptance:** tier-2: split triggers at the exact threshold, children HP =
parent current HP, intents/queue state after split match the cited Java;
split during the monster's own turn vs. player turn both covered.
**Log:** —

### B3.18 `[ ]` ∥ Elites: Gremlin Nob + Sentries
**Deps:** B3.12 · **Provenance:** GremlinNob.java (:67/72/92-93/133),
Sentry.java (Artifact, Dazed insertion, alternating pattern)
**Deliverables:** registry entries with A3/A8/A18 columns; Artifact power
(debuff negation — a general power, lands here); Nob's skill-anger trigger.
**Acceptance:** tier-2: Nob Anger triggers on skill plays only (A18 column
cited); Sentry alternating moves by position; 3-Sentry spawn from B3.12.
**Log:** —

### B3.19 `[ ]` ∥ Elite: Lagavulin
**Deps:** B3.12 · **Provenance:** Lagavulin.java (:77/82/83; asleep/stun/
metallicize wake logic)
**Deliverables:** registry entry (native AI per design §4.2 budget —
sleep-wake state machine), Metallicize power, the elite `Lagavulin(true)`
variant flag.
**Acceptance:** tier-2: wakes on damage or turn 3, debuff move cadence,
A18 −2 column; asleep block gain each turn.
**Log:** —

### B3.20 `[ ]` ∥ Boss: Slime Boss
**Deps:** B3.17 · **Provenance:** SlimeBoss.java (:89/94/125), Goop Spray /
split at half
**Deliverables:** registry entry (native AI), boss split (→ L slimes at
current HP), Slimed hand insertion; boss-fight terminal only when all
descendants die.
**Acceptance:** tier-2: split threshold exact, children chain to B3.17
machinery; A4/A9/A19 columns cited.
**Log:** —

### B3.21 `[ ]` ∥ Boss: The Guardian
**Deps:** B3.12 · **Provenance:** TheGuardian.java (:97-107/185; mode shift
thresholds 30/35/40, Sharp Hide)
**Deliverables:** registry entry (native AI: offensive/defensive mode state
machine keyed on damage-taken threshold), Mode Shift + Sharp Hide powers.
**Acceptance:** tier-2: mode flips at the exact cumulative-damage threshold
incl. threshold growth per cycle; Sharp Hide triggers on attack plays; A19
column.
**Log:** —

### B3.22 `[ ]` ∥ Boss: Hexaghost
**Deps:** B3.12 · **Provenance:** Hexaghost.java (:99, :137-142 —
Body/Orb components), Divider damage = f(player HP)
**Deliverables:** registry entry (native AI: orb-count state, Divider math,
Inferno upgrade of Burns, Sear/Tackle/Inflame cycle); decision recorded on
modeling orbs (monster `misc` fields vs. extra powers — CombatState additive
change needs a schema bump + fixture regeneration via checked-in generators,
design §4.4).
**Acceptance:** tier-2: Divider = player-HP-derived exactly per the cited
line; move cycle across 12+ turns matches hand-derivation; Burn upgrades at
the cited turn.
**Log:** —

### B3.23 `[ ]` ∥ Potions
**Deps:** B3.2 · **Spec:** design §5.4 · **Provenance:**
PotionHelper.java:70-71, 88-172; AbstractDungeon.java:829-850; per-potion
classes (33, enumerate)
**Deliverables:** `potions.yaml` (33 Ironclad-pool entries: tier, effect
program or native, `game_id`); USE_POTION/discard verbs through run+combat
layers; potion-slot storage incl. A11 count; the trap-14 rejection-sampling
identity roll; Fairy/Smoke Bomb natives flagged (out-of-combat-trigger and
combat-escape semantics — escape reuses B3.15's path).
**Acceptance:** tier-2 per potion effect; identity-roll draw-count test
(rejection loop consumes exactly the observed number for a fixed stream);
slots at A20 = 2 (A11 row cited).
**Log:** —

### B3.24 `[ ]` ∥ Relics: starter + commons
**Deps:** B3.2 · **Spec:** design §5.3 · **Provenance:** relics/ COMMON tier,
Ironclad-obtainable subset (enumerate via class/canSpawn gates at task);
Ironclad.java starting relic
**Deliverables:** `relics.yaml` rows: Burning Blood + the common pool
(Anchor, Bag of Marbles, Bag of Preparation, Blood Vial, Bronze Scales,
Centennial Puzzle, Lantern, Nunchaku, Oddly Smooth Stone, Orichalcum, Pen
Nib, Red Skull, Vajra, War Paint, Whetstone, … — enumerate); trigger
bindings through the B3.2 hook framework in **acquisition order** (trap 8).
**Acceptance:** tier-2 per relic (combat triggers in constructed states);
counter-based relics (Nunchaku, Pen Nib) persist counters in RunState relic
slots (stage-a §4.3's `{relic_id, counter}`).
**Log:** —

### B3.25 `[ ]` ∥ Relics: uncommons
**Deps:** B3.24 · **Provenance:** relics/ UNCOMMON, Ironclad-obtainable
**Deliverables:** registry rows + triggers (Blue Candle, Bottled trio,
Eternal Feather, Gremlin Horn, Horn Cleat, Ink Bottle, Kunai?, Letter
Opener?, Meat on the Bone, Mercury Hourglass, Molten Egg, Mummified Hand,
Ornamental Fan, Pantograph, Paper Phrog, Self-Forming Clay, Shuriken,
Strike Dummy, Sundial, Toxic Egg?, White Beast Statue, … — enumerate and
class-filter at task; non-Ironclad-obtainable rows excluded with the filter
evidence in the Log).
**Acceptance:** tier-2 per relic; Paper Phrog's Vulnerable-multiplier branch
(the stage-a A4.1 "unreachable" note retires here — update that inline
comment in the same commit).
**Log:** —

### B3.26 `[ ]` ∥ Relics: rares + shop
**Deps:** B3.24 · **Provenance:** relics/ RARE + SHOP, Ironclad-obtainable
**Deliverables:** registry rows + triggers (Bird-Faced Urn, Calipers —
retiring A3.1's structural branch, Champion Belt, Charon's Ashes, Dead
Branch, Du-Vu Doll, Fossilized Helix, Gambling Chip, Ginger, Girya, Ice
Cream — retiring A4.3's EnergyManager SET simplification note, Incense
Burner, Lizard Tail, Magic Flower, Mango, Old Coin, Peace Pipe, Pocketwatch,
Prayer Wheel, Shovel, Stone Calendar, Thread and Needle, Torii, Tungsten
Rod, Turnip, Unceasing Top, Wing Boots, plus SHOP tier — enumerate).
**Acceptance:** tier-2 per relic; Ice Cream forces the energy-recharge
rewrite from SET to conditional-carry (stage-a §12 A4.3 entry's documented
boundary) — regression: all Stage A energy tests still green.
**Log:** —

### B3.27 `[ ]` ∥ Relics: boss (Neow pool) + event-specials
**Deps:** B3.24 · **Spec:** design §5.3, §5.6 (Neow cat-3 in scope) ·
**Provenance:** relics/ BOSS + SPECIAL, Ironclad-obtainable; event sources
(design §5.6)
**Deliverables:** registry rows + triggers for the Ironclad boss pool
(Black Blood, Snecko Eye — cardRandomRng cost rolls, Runic Dome — observation
impact documented, Coffee Dripper, Cursed Key, Ectoplasm, Fusion Hammer,
Mark of Pain, Philosopher's Stone, Runic Cube?, Sozu, Velvet Choker, … —
enumerate/filter) and the Act-1-event specials (Golden Idol, Neow's Lament,
Necronomicon?, … — filter by S1 event reachability, B4.11-13).
**Acceptance:** tier-2 per relic; Snecko Eye's per-draw cost roll stream +
draw-order accounting tested (trap-10 family).
**Log:** —

---

## Phase B4 — The run layer (Gate G6 = M3)

### B4.1 `[ ]` Map path generation
**Deps:** G4, G5 · **Spec:** design §5.6 · **Provenance:**
MapGenerator.java:23, 62-77, 157-190, 270-276; AbstractDungeon.java:510-540
**Deliverables:** path generation into `RunState`'s 7×15 node grid
(mapRng-driven: 6 walks, first-two-distinct rule, ancestor-gap re-rolls,
edge dedup) — exact draw order per the cited lines.
**Acceptance:** gtest: for ≥ 3 seeds, generated edges match the **oracle's
dumped map** node-for-node (bridge artifacts from B1.4 — this is the first
run-layer bit-exactness proof); mapRng draw count per generation matches
the hand-derived count for one seed.
**Log:** —

### B4.2 `[ ]` Room-type assignment
**Deps:** B4.1 · **Spec:** design §5.6; §10 trap 12 · **Provenance:**
AbstractDungeon.java:558-594, 571-573; RoomTypeAssigner.java:65-143
**Deliverables:** quota computation (incl. A1 elite ×1.6), the trap-12
direct-XS128 `Collections.shuffle`, assignment rules (row gates, parent/
sibling exclusions, monster fill), fixed rows 0/8/14.
**Acceptance:** gtest: room layouts match oracle dumps for the B4.1 seeds;
trap-12 named test (counter unchanged across the shuffle; permutation matches
golden category 7 from the A0.1 harness extension — extend the capture
harness in this commit); quota table tested at A0 vs A20 (1.6× elites).
**Log:** —

### B4.3 `[ ]` RunState population + additive fields (schema v2)
**Deps:** B4.2 · **Spec:** design §2.6 (placeholder population; additive
inventory) · **Provenance:** design §2.5 rows 5-8; SaveFile parity fields
stage-a §3.4
**Deliverables:** `RunState` gains, additively: event pity floats ×3,
purgeCost, remaining-pool bookkeeping (relic pool orders ×5, event/shrine/
special membership bitsets, card-pool removal bookkeeping), potion-slot
count; placeholder fields (boss id, event/shop flags) become real;
`SCHEMA_VERSION` bump + trace/fixture regeneration via checked-in generators
only; translator (B1.5) upgraded to emit the now-representable fields.
**Acceptance:** `state_test` updated ceilings (`RunState` ≤ 8192 holds);
schema-bump discipline verified (old traces refused, regenerated fixtures
zero-diff); translator round-trips the full §2.5 inventory bit-for-bit.
**Log:** —

### B4.4 `[ ]` Run-level advance + room lifecycle
**Deps:** B4.3, B3.12 · **Spec:** stage-a §3.4 (floor reseed), §7 (one enum,
all phases) · **Provenance:** AbstractDungeon.java:1747-1751 (reseed after
increment, trap 7), 1766-1770 (eventRng duplicate quirk); room
transition/proceed flow (read at task)
**Deliverables:** `run_begin(seed, ascension)` (Neow-pending initial state);
run-level `advance()`/`legal_actions()` (CHOOSE for map nodes/screens,
PLAY_CARD/END_TURN delegate to combat, USE_POTION both layers);
`nextRoomTransition` (floor++ then reseed, trap 7); combat spawn via B3.12
and **fold-back** (hp/gold/potions/deck/relic counters, escape vs. kill);
combat-reward/proceed screens as CHOOSE states.
**Acceptance:** gtest: full-floor cycle vs. hand-derivation for one seed
(map pick → combat → rewards → next floor, stream counters checked at each
boundary); floor-reseed trap test at run level; batch `advance()`
heterogeneity (mixed run/combat phases in one batch) green in asan.
**Log:** —

### B4.5 `[ ]` Combat rewards
**Deps:** B4.4, B3.3-B3.11 (full card pool for oracle acceptance) · **Spec:**
design §5.6 · **Provenance:** AbstractRoom.java:291-296, 314-325, 580-617,
108-109, 148-177; AbstractDungeon.java:1423-1498, 1597-1624
**Deliverables:** gold rolls (boss=miscRng ±5 ×0.75@A13, elite/normal=
treasureRng — trap 18), potion drop (40 % + blizzardPotionMod ratchet, trap
family), card rewards (3 cards, `cardRng.random(99)+cardBlizzRandomizer` vs
3/37/60, pity reset/growth, no-duplicate re-roll — read the dupe loop at
task, upgrade chance 0 in Act 1), colorless handling, reward-screen CHOOSE
flow incl. skip.
**Acceptance:** tier-2: pity dynamics across scripted reward sequences match
hand-derivation; stream attribution named tests (trap 13, 18); oracle
spot-diff: ≥ 3 bridge runs' reward screens zero-diff through the differ.
**Log:** —

### B4.6 `[ ]` Relic pools + acquisition
**Deps:** B4.3, B3.24 · **Spec:** design §5.3; §10 trap 15 · **Provenance:**
AbstractDungeon.java:676-819, 1221-1256; RelicLibrary population (read at
task)
**Deliverables:** pool initialization (5 shuffles = 5 relicRng draws,
JDK-LCG route), front-pop vs end-pop, tier roll 50/33/17, canSpawn re-check +
Circlet fallback, acquisition wiring (RunState relic list in acquisition
order, trap 8) incl. on-pickup effects.
**Acceptance:** tier-2: pool orders for fixed relicRng match hand-derived
shuffles (golden JDK route from A1.2); trap-15 named test (front vs end);
oracle spot-diff of pool order via the §2.5 block for ≥ 3 seeds.
**Log:** —

### B4.7 `[ ]` Treasure rooms
**Deps:** B4.6 · **Spec:** design §5.6; §10 trap 16 · **Provenance:**
AbstractDungeon.java:499-508; AbstractChest.java:54-102; Small/Medium/
LargeChest.java:18-22
**Deliverables:** chest size roll, single-roll gold+tier (trap 16), gold
amount ×(0.9,1.1), relic grant via B4.6, the fixed treasure row (map row 8).
**Acceptance:** tier-2: chest tables vs. hand-derivation across the roll
range; trap-16 named test; oracle spot-diff ≥ 2 treasure floors.
**Log:** —

### B4.8 `[ ]` Shop
**Deps:** B4.5, B4.6, B3.23 · **Spec:** design §5.6 · **Provenance:**
ShopScreen.java:100-136, 227-292, 340-428, 601-661; Merchant.java (stock
build — read at task)
**Deliverables:** stock generation (5 colored + 2 colorless w/ 0.3 rare
chance, 3 relics incl. end-pop + SHOP tier slot, 3 potions), pricing (base ×
jitter, colorless ×1.2, A16 ×1.1, sale card /2), purge (75 + 25 ramp,
persistent), purchase/purge as CHOOSE flow, merchantRng draw-order exactness.
**Acceptance:** tier-2: full stock + prices for a fixed merchantRng state
match hand-derivation draw-for-draw; purge ramp persists across two shops;
oracle spot-diff of a shop floor (stock, prices, sale index) zero-diff.
**Log:** —

### B4.9 `[ ]` Rest sites
**Deps:** B4.4, B3.26 (Girya/Peace Pipe/Shovel options) · **Spec:** design
§5.6 · **Provenance:** RestOption.java:25; CampfireUI.java:81-107; smith
grid flow (read at task)
**Deliverables:** rest (30 % max-HP heal, the frozen no-ascension-effect
negative), smith (upgrade grid CHOOSE), relic-added options for implemented
relics, the fixed rest row (14) + no-rest-row-13 rule already in B4.2.
**Acceptance:** tier-2: heal amount, smith upgrade writes the upgrade bit
via registry rows; option availability matrix (no upgradable cards → no
smith).
**Log:** —

### B4.10 `[ ]` Event framework + ?-room resolution
**Deps:** B4.4 · **Spec:** design §5.6; §10 traps 17/19 · **Provenance:**
EventHelper.java:88-211; AbstractDungeon.java:1864-1990, 1340-1358; stage-a
§3.4's eventRng-duplicate quirk
**Deliverables:** the ?-room roll (float table, pity growth/reset — float
arithmetic, trap 19), shrine-vs-event split (0.25), pool draw + removal
bookkeeping (RunState bitsets from B4.3), event dialog framework (options as
CHOOSE, conditional options, one-shot flags), Juzu/Tiny-Chest hooks for the
relics that alter the table.
**Acceptance:** tier-2: pity float sequences match hand-derivation
bit-for-bit across 20 ?-rooms; pool-removal bookkeeping vs oracle §2.5 lists
for ≥ 3 seeds; trap-19 named test.
**Log:** —

### B4.11 `[ ]` ∥ Exordium events I
**Deps:** B4.10 · **Provenance:** events/exordium: Big Fish, The Cleric,
Dead Adventurer, Golden Idol, Golden Wing, World of Goop (each read in full;
A15 branches per event)
**Deliverables:** the 6 events as native logic + `events.yaml` metadata
(conditions, option tables, A15 columns); Dead Adventurer's escalating
encounter, Golden Idol's relic+curse branches.
**Acceptance:** tier-2 per event (every option's state delta, A15 variants);
directed script per event.
**Log:** —

### B4.12 `[ ]` ∥ Exordium events II
**Deps:** B4.10 · **Provenance:** events/exordium: Liars Game, Living Wall,
Mushrooms, Scrap Ooze, Shining Light
**Deliverables:** the 5 events (Mushrooms' combat + Parasite, Scrap Ooze's
escalating relic odds, Shining Light's upgrade-random-two).
**Acceptance:** as B4.11.
**Log:** —

### B4.13 `[ ]` ∥ Shrines + one-time specials
**Deps:** B4.10 · **Provenance:** events/shrines: Match and Keep, Golden
Shrine, Transmorgrifier, Purifier, Upgrade Shrine, Wheel of Change + the
AbstractDungeon.java:1340-1358 one-time list filtered to Act-1 reachability
(floor/hp/gold gates, AbstractDungeon.java:1949-1980 — record the excluded
ones with their gate evidence in the Log)
**Deliverables:** the 6 shrines + reachable specials (Accursed Blacksmith,
Bonfire Elementals, Duplicator, Fountain of Cleansing, Lab, N'loth?, The
Woman in Blue, … per the filter) with transform/remove/upgrade mechanics
(cardRandomRng vs miscRng attribution read per event).
**Acceptance:** tier-2 per event; transform draw-stream attribution named
tests; Match-and-Keep's card dealing vs oracle spot-check.
**Log:** —

### B4.14 `[ ]` Neow
**Deps:** B4.4, B4.6, B3.27 (boss pool) · **Spec:** design §5.6; §10 trap 17
· **Provenance:** NeowEvent.java:62, 163, 289, 349-371; NeowReward.java:
68-128, 190-368
**Deliverables:** the four-category blessing (fresh `Random(seed)` — trap
17), category tables incl. cat-2 drawback-first roll order, all payout
activations (cards via NeowEvent.rng, relics/potions via relicRng/potionRng
pools), cat-3 boss swap, mini-blessing path flagged out-of-scope if
unlock-gated (fully-unlocked profile ⇒ full blessing; record the check).
**Acceptance:** tier-2: option sets + payouts for fixed seed match
hand-derivation draw-for-draw; oracle spot-diff of the Neow screen across
≥ 10 seeds zero-diff (options AND post-choice state).
**Log:** —

### B4.15 `[ ]` A20 run-setup modifiers + negative freezes
**Deps:** B4.3 · **Spec:** design §6 · **Provenance:**
AbstractDungeon.java:2582-2600; AbstractPlayer.java:211-213;
Ironclad.java:113-115, 168-170
**Deliverables:** `a20.yaml` complete (every §6 row, numbers filled from the
cited lines read in this task); run-setup application order at `run_begin`
(A6 90 % HP, A10 curse, A11 slot, A14 −5 — exact order per
`dungeonTransitionSetup`); tier-2 negative tests pinning the §6 "no such
modifier" list (campfire heal, potion chance, normal/elite gold, rarity,
A12-in-Act-1).
**Acceptance:** tier-2 per row incl. the negatives; a20 manifest complete vs
design §6's table (every row implemented or explicitly N/A-for-S1 with
reason).
**Log:** —

### G6 `[ ]` **Gate: S1 rules complete (M3)** — tag `g6-s1-content`
**Deps:** all B3.*, all B4.*
Checklist (evidence linked in Log):
- [ ] 100 % tier-2 registry coverage: every manifest row has named passing
      tests (scripted check, `tools/verify_report/`).
- [ ] Sim-only soak: 1,000-seed random-policy full Act-1 runs complete (win,
      die, or legal-action exhaustion — never an assert/illegal state) in
      debug + asan.
- [ ] Oracle spot campaign: ≥ 20 full-run seeds, Neow through boss reward,
      **zero un-triaged diffs** through the run-level differ.
- [ ] All Stage A tests + fixtures still green (schema bumps accounted).
Then: update CLAUDE.md "Current state".
**Log:** —

---

## Phase B5 — Verification campaigns + S1 exit (Gate G7 = M4)

### B5.1 `[ ]` ∥ Sim self-replay fuzz soak
**Deps:** B4.4 · **Spec:** design §7.1(2); stage-a §2 (replay-twice memory
guard)
**Deliverables:** `tools/fuzz/` sim-side fuzzer: random-legal + heuristic
policies (design §3.3's E0 stand-ins, implemented here) over seed sweeps;
every run replayed twice, final-state hashes compared; assert/hash-mismatch
triage output with reproducers; overnight-runnable script.
**Acceptance:** ≥ 10M actions across ≥ 10k seeds, zero nondeterminism, zero
asserts, asan-clean sample (≥ 1 % of runs under asan); numbers recorded here.
**Log:** —

### B5.2 `[ ]` ∥ Oracle campaign automation
**Deps:** B1.4, B4.4 · **Spec:** design §7.1-7.3
**Deliverables:** campaign orchestration (seed sharding, nightly schedule,
resume, artifact root fixed per §7.3, translation + diffing pipeline run as
one command); triage queue (divergence → reproducer → this ledger/change-log
workflow); promoted-reproducer corpus layout.
**Acceptance:** one unattended overnight campaign (≥ 50 seeds) runs
end-to-end (game → JSONL → traces → diff reports) without intervention;
throughput and diff counts land in a generated report.
**Log:** —

### B5.3 `[ ]` ∥ Tier-4 distributional suite
**Deps:** G6 · **Spec:** design §3.4 (analytic-first scope)
**Deliverables:** `tools/dist_check/`: the §3.4 analytic expectation set
(encounter weights, compositions, rarity+pity dynamics, potion ratchet,
chest tables, ?-room pity, relic/potion tier rolls, map quotas) with
chi-square/exact tests over ≥ 10k sim seeds; the ≥ 200-run oracle-harvested
spot comparison.
**Acceptance:** all analytic tests pass at pre-registered significance
(document the correction for multiple comparisons in the tool README —
choose and justify); oracle spot set shows no flagged aggregate; failures
are stop-the-line divergences, not tuning targets.
**Log:** —

### B5.4 `[ ]` Verification report + CI corpus
**Deps:** B5.2 · **Spec:** design §7.4-7.5, §7.1(1)
**Deliverables:** `tools/verify_report/` (diffs per million actions,
divergence inventory, per-registry-row oracle-sighting + tier coverage join
via `game_id`); latest report committed under `docs/verification/`; the
curated 50-seed CI corpus (compressed translated traces +
zero-diff-replay gtest wired into the existing CI matrix).
**Acceptance:** CI runs the 50-seed replay smoke in seconds and fails on an
injected synthetic divergence (proven once, then reverted); report
regenerates deterministically from campaign artifacts.
**Log:** —

### B5.5 `[ ]` Throughput floors
**Deps:** G6 · **Spec:** design §8(4); InitialPlan §0.2 floors
**Deliverables:** benchmark additions: full-combat/sec/core (random policy)
and full-run/sec whole-machine on `bench_advance`'s pattern; methodology
notes (random-policy stand-in for 25-sim MCTS, per design §8).
**Acceptance:** release-preset numbers recorded here: ≥ 50k combat
steps/sec/core, ≥ 300 combats/sec/core, ≥ 0.4 runs/sec whole-machine — or a
stop-the-line design-doc amendment with profiling evidence (fast-but-wrong
is death; slow-but-honest gets a Stage C plan).
**Log:** —

### G7 `[ ]` **Gate: S1 verified (M4)** — tag `g7-s1-verified`
**Deps:** B5.1-B5.5, G6
The design §8 bar, checked literally (evidence linked in Log):
- [ ] ≥ 1,000,000 oracle-diffed actions across ≥ 2,000 seeds, zero
      un-triaged diffs (campaign reports).
- [ ] ≥ 10M sim-side fuzz actions, zero nondeterminism/asserts (B5.1).
- [ ] 100 % tier-2 registry coverage (manifest check, re-run at gate).
- [ ] Every a20.yaml row verified per design §8(3).
- [ ] Throughput floors hold (B5.5 numbers).
- [ ] Tier-4 suite green (B5.3).
Then: update CLAUDE.md "Current state"; open Stage C planning (perf
hardening) and the S2 scope conversation as fresh planning exercises.
**Log:** —

---

## Parallelism map

```
B0.1 ─┬─▶ B1.1 ─▶ B1.2 ─┬─▶ B1.3 ──────────┐
B0.2 ─┘                 └─▶ B1.4 ─▶ B1.5 ─▶ B1.6 ─▶ G4     (B0.1 ∥ B0.2)
B2.1 ─▶ B2.2 ─▶ G5                                          (Phase B2 ∥ Phase B0/B1)

G4 + G5 ─▶ B3.1 ─▶ B3.2 ─┬─▶ B3.3 … B3.11 (card batches ∥)
                         ├─▶ B3.12 ─▶ B3.13 … B3.22 (monster batches ∥;
                         │            B3.17 ─▶ B3.20; B3.14 ─▶ B3.17)
                         ├─▶ B3.23 (potions)
                         └─▶ B3.24 ─▶ B3.25 ∥ B3.26 ∥ B3.27 (relics)

G4 + G5 ─▶ B4.1 ─▶ B4.2 ─▶ B4.3 ─▶ B4.4 ─┬─▶ B4.5 ─▶ B4.8
   (B4.1-B4.3 ∥ with Phase B3)           ├─▶ B4.6 ─▶ B4.7, B4.8
                                         ├─▶ B4.9
                                         ├─▶ B4.10 ─▶ B4.11 ∥ B4.12 ∥ B4.13
                                         ├─▶ B4.14 (also needs B4.6, B3.27)
                                         └─▶ B4.15
all B3.* + all B4.* ─▶ G6
B4.4 ─▶ B5.1 ∥ B5.2 (campaigns start before G6; DoD volume accrues)
G6 ─▶ B5.3 ∥ B5.5 ; B5.2 ─▶ B5.4 ; B5.1-B5.5 ─▶ G7
```

## Change log

- 2026-07-17 — v0.1 created from stage-b-design.md v0.1 (§2-§8 → phases
  B0-B5). Task counts: 57 tasks + 4 gates (G4-G7, continuing Stage A's gate
  numbering; G4=M2, G6=M3, G7=M4). Card/monster/relic batch enumerations
  marked "enumerate at task start" are counted from the CardRarity/RelicTier
  greps recorded in design §5 — the named example lists in B3.x are
  orientation, not authority; the source enumeration at task start wins.
