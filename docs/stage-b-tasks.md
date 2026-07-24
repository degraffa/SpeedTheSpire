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

### B0.2 `[x]` ∥ Stock-jar bridge bring-up + environment audit
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
**Log:** Verified by running, not inferred (stock unstripped game, Windows
host — excluded from WSL CI). Delivered `tools/oracle_bridge/driver/
echo_driver.py` (minimal CommunicationMod child: logs each state JSON to JSONL,
forwards operator commands from a side-channel file since the child's stdio
belongs to the game; `--verify` re-parse mode) + `driver/README.md` (config
.properties wiring + scriptable ModTheSpire launch `--skip-launcher --mods
basemod,CommunicationMod` from the workshop paths, design §1.2). One complete
manual seeded run captured — `start ironclad 20 STS12345` → Neow → floors 1-3
(single Cultist, multi-monster combats, combat/card rewards, map nav), 205
game-states. **(1)** Capture replays cleanly through `echo_driver.py --verify`:
418 records, 205 recv, 205 parsed OK, 0 errors. **(2)** Seed cross-check:
`start …STS12345` and the game's own seeded-run **UI** with `STS12345` both
yield seed long **1790052133945** and a **byte-identical** normalized floor-0
Neow state (11 cards, 56 map nodes; deck/relics/map/hp/gold all equal;
per-instance `uuid`s dropped per PROTOCOL.md). Both paths share
`SeedHelper.getLong`; `STS12345 ↔ 1790052133945` round-trips exactly. **(3)**
Baseline throughput (stock, unstripped): **~0.36 states/s** (stepper-paced,
dominated by unsuppressed combat animations — the ≥5/s floor is the
strip-patched fork target, B1.3). **Profile unlock audit — PASS:** fully
unlocked at the pool gate. All 60 unlock-gated cards/relics in
`UnlockTracker.refresh()` have `STSUnlocks` flag=2, so `lockedCards`/
`lockedRelics` are empty (`addCard`/`addRelic` lock only keys `!= 2`) →
complete pools; every Act-1 boss beaten (`STSAchievements`); A20 launches. The
`IRONCLADUnlockLevel=3` meta-counter does **not** gate run pools —
`isCardLocked` reads per-card flags, not the level (source `CardLibrary.
addRedCards`, `UnlockTracker.addCard`/`refresh`); profile left untouched (no
save edit). Config.properties and all JSONL captures live under the §7.3 data
root `D:\STS_BG_Mod\_oracle_data`, uncommitted; the stepper that drove the
capture is a throwaway data-root helper, not the committed driver.

---

## Phase B1 — The oracle fork, driver, translator (Gate G4 = M2)

### B1.1 `[x]` Fork build pipeline
**Deps:** B0.1, B0.2 · **Spec:** design §2.4
**Deliverables:** JDK-8 build script (`tools/oracle_bridge/build_fork.ps1` or
equivalent) compiling the vendored fork against `desktop-1.0.jar` +
ModTheSpire + BaseMod from the local install; jar output to a non-committed
location ModTheSpire picks up; fork modid distinct from upstream
(`CommunicationMod-oracle`) so both can't load together confusingly.
**Acceptance:** fork jar, with zero behavioral patches yet, reproduces B0.2's
captured run byte-for-byte (same seed, same script → same JSONL states);
build script is deterministic and documented in the fork README.
*(Amended 2026-07-22 — the B0.2 capture is timing-contaminated and not
cleanly reproducible; see the change log. Satisfied as: fork == stock
byte-identical on the same derived script, with stock-determinism control and
B0.2 anchors.)*
**Log:** Verified by running, not inferred (Windows host — excluded from WSL
CI). Delivered `tools/oracle_bridge/build_fork.ps1`, a no-Maven JDK-8
pipeline: stages sources with the upstream pom's gson shade relocation applied
textually (`com.google.gson`→`com.autoplay.gson`; vendored sources untouched),
packages the already-relocated gson 2.8.5 classes **extracted from the stock
workshop jar** (byte-identical gson bytecode to the B0.2 baseline, no new
dependency), compiles with `javac -g` (**required** — ModTheSpire resolves
`@SpirePatch` parameter names from the LocalVariableTable; without `-g`
patching dies with "Illegal patch parameter: Cannot determine name"), and
writes a **deterministic jar** (ordinal-sorted entries, pinned timestamps, no
manifest): `-CheckDeterminism` runs the full pipeline twice → byte-identical,
sha256 `6AB875C8EA374C9014643CB4BDC3FA6E93264D0C8DD3B7BD7C23DAAB2323FD16`.
Fork identity: modid `CommunicationMod-oracle` (ModTheSpire.json made concrete
— the maven `${…}` placeholders died with the maven build); `SpireConfig`
name untouched so the B0.2 driver wiring works as-is; docs in
`communicationmod-oracle/README-oracle.md` + layout map in
`tools/oracle_bridge/README.md`. Deploys to `<game>\mods\` (jar never
committed). Launch notes: `--skip-launcher --mods
basemod,CommunicationMod-oracle` works; run MTS under the game's **bundled JRE
8** (`<game>\jre\bin\java.exe`) — system Java 25 was being upgraded by a
concurrent process mid-task, which killed the first launch's JVM.
**Acceptance evidence:** B0.2's capture turned out timing-contaminated — its
stepper re-sent `end` 7× during one slow monster-turn window (send seqs
37-44); the game queued extras, so the recorded turn-3 state shows
`times_damaged`=5 / HP 45 while the turn-2 decision state was never dumped.
No clean state-paced replay can reproduce that. Controlled experiment on the
**derived 205-command effective script** (8 noise sends dropped, the one
invisible `end` re-inserted; replayer sends each command only on a fresh
`ready_for_command` state): **(1)** stock determinism — two independent
stock-jar game sessions → 206/206 normalized states byte-identical (uuid
dropped per PROTOCOL.md, the only nondeterministic field); **(2)** **fork ==
stock: 206/206 normalized states byte-identical** (seed STS12345, A20
Ironclad, Neow → floor-3 reward); **(3)** anchors to the B0.2 capture:
floor-0 Neow state byte-identical (seed long 1790052133945), first 18 states
byte-identical (everything pre-contamination), and the whole-run floor/screen
trajectory identical modulo the one extra turn-2 state. Fork capture parses
clean (`echo_driver.py --verify`: 206 recv, 0 errors). Replay/compare helpers
(`replay_b11.py`, `compare_b11.py`) are data-root throwaways per the B0.2
stepper precedent. WSL suite untouched: 140/140 green in debug + asan.

### B1.2 `[x]` Oracle state block
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
**Log:** Verified by running, not inferred (Windows host — excluded from WSL
CI). Delivered the oracle-state-block fork patch: `GameStateConverter`
gains `getOracleState()`/`rngToJson()` appending a single `"oracle"` key to
every in-dungeon dump, gated by a new fork config flag `oracleBlock` (default
true; `config.properties` + a mod-settings toggle) so with the flag off the
fork output is byte-identical to stock (the B1.3 strip-equivalence baseline).
The block carries **every** design-§2.5 row: the 14 RNG streams as
`{counter,s0,s1}` — 7 run-scoped (`monsterRng eventRng merchantRng cardRng
treasureRng relicRng potionRng`), 5 floor-scoped (`monsterHpRng aiRng
shuffleRng cardRandomRng miscRng`), `mapRng`, and `neowRng`
(`NeowEvent.rng`, key absent until the blessing screen) — plus
`cardBlizzRandomizer`, `blizzardPotionMod`, the three `eventPity` floats
(`monster/shop/treasure`), `purgeCost`, the three remaining-pool lists
(`eventList/shrineList/specialOneTimeEventList`), the five `relicPools`
orders, per-monster `monster_move_history` (combat only), and
seed/floor/act/ascension anchors. `s0`/`s1` come from public
`Random.random.getState(0/1)` (RandomXS128 seed0/seed1); the two private
`EventHelper` pity floats use the sanctioned `ReflectionHacks` fallback
(design §2.5), everything else is public/static — **no other reflection
needed** on the 11-30-2020 build. PROTOCOL.md gains §5 (the block's full
schema). **Provenance** (all read in full before coding): AbstractDungeon.java
:149-161 (stream fields), :247-250 (cardBlizz), :182-184 (event/shrine/special
lists), :1221-1256 (5 `relicRng.randomLong()` pool shuffles), :1747-1751 (floor
reseed = `Random(seed+floorNum)`); Random.java:17-18; NeowEvent.java:62/289/363;
AbstractRoom.java:100-101; EventHelper.java:88-92; ShopScreen.java:100-102,
278-292; AbstractMonster.java:106; RandomXS128.getState.
**Acceptance — one fixed 205-cmd scripted run (seed STS12345, A20 Ironclad,
Neow→floor-3 reward; B1.1's derived script) + one short adaptive second run for
the event observation the fixed path can't reach (its map only visits monster
rooms; its one `?` room rolls MONSTER on this seed):**
**(1)** `relicRng.counter` = **5** at the first in-dungeon dump (floor-0 Neow),
exactly the 5 init pool shuffles.
**(2)** floor-scoped `(s0,s1)` == sim `floor_stream(1790052133945, N)` **bit-for-bit**
(read off `cardRandomRng` at `counter==0`, pristine per-floor reseed), floors
1/2/3: floor 1 `s0=7342732389453056061 s1=-1677929205632241533`, floor 2
`s0=-6609920522105378709 s1=-9206057524216559371`, floor 3
`s0=1660650329036603239 s1=1975031184179605387` — each equal to the sim's
tier-1-tested derivation (WSL probe over `include/sts/engine/rng_stream.hpp`
`floor_stream`).
**(3)** `blizzardPotionMod` ratchets across the combat rewards: 0 (Neow) → **10**
(floor-1 reward, +10 no-drop) → 0 (floor-2 reward, potion dropped→reset) → **10**
(floor-3 reward, +10) — two visible +10 ratchets.
**(4)** `eventList` shrinks **11→10** when an event fires: floor-5 `?`-room rolled
EVENT and removed **"Liars Game"** (`eventRng.counter` 0→3). The fixed run's
floor-2 `?` rolled MONSTER (`eventRng.counter` 0→1, `room_type=MonsterRoom`, no
list change) — the oracle correctly captured the non-event roll too.
Windows-host commands: `build_fork.ps1` (jar sha256
`7A735C1F1B16368DF4B7E68042EFC23533708FBA08263592CF2C4302552E9960`, deployed to
`<game>\mods\`, never committed); game launched under the bundled JRE 8
(`<game>\jre\bin\java.exe … --skip-launcher --mods basemod,CommunicationMod-oracle`);
scripted via `echo_driver.py` + data-root throwaway feeders
(`feed_b12.py`/`autopilot2_b12.py`/`analyze_b12.py`) per the B0.2/B1.1
precedent; captures under the §7.3 data root, uncommitted. WSL suite green:
**140/140 in debug + asan** (no engine change; tree verified green to commit).
Spec note resolved: design §2.5 row 6 names `ShopScreen.purgeCost` — emitted as
`purgeCost`; the relic-adjusted `actualPurgeCost` is already the stock
`purge_cost` shop-screen field, so both are observable. B1.3 note: with
`oracleBlock=false` the dump has no `oracle` key (the strip-equivalence
baseline); block size ~3.85 KB/dump at floor 0 (the 5 full relic-pool id-string
lists dominate; shrinks as pools pop).

### B1.3 `[x]` Rendering-strip / fast-forward patches
**Deps:** B1.2 · **Spec:** design §2.2 (semantic guard; throughput floor)
**Deliverables:** the strip patch family (draw suppression, animation-time
collapse, fast update cadence) in the fork, individually toggleable via fork
config; throughput measurement script in the driver.
**Acceptance:** ≥ 20 seeds × scripted multi-floor runs: oracle dumps (incl.
the full §2.5 block) are **byte-identical** with strip patches on vs. off;
sustained throughput ≥ 5 actions/sec (floor), number recorded here; if the
floor is unreachable, stop and amend design §2.2/§8 per its own rule before
proceeding.
*(Amended 2026-07-23 — satisfied as: the §2.5 oracle block byte-identical + all
stock `GameStateConverter` fields byte-identical except a closed, enumerated set
of dually-proven presentation fields, per design §11 v0.1.2; a fix-forward
readiness gate on pending obtain effects (`fork: gate readiness on pending obtain
effects`) removes a pre-existing card-obtain race first. No logical/semantic
field differs.)*
**Log:** Verified by running, not inferred (Windows host — excluded from WSL
CI). Delivered three individually-toggleable rendering-strip families (fork
`SpireConfig` flags, default on; **all-off ⇒ pre-B1.3 behaviour**):
`stripDrawSuppression` — Prefix-return `AbstractDungeon.render(SpriteBatch)`
(dungeons/AbstractDungeon.java:2153; `CardCrawlGame.render` keeps `update()`
:368 + `sb`/`glClear` so the GL surface stays live; input is update-driven —
`Hitbox.update` not `render`, helpers/Hitbox.java:40-67 vs :122-131);
`stripAnimationCollapse` — Prefix `LwjglGraphics.getDeltaTime()`
(backends/lwjgl/LwjglGraphics.java:132) → a fixed **non-round STEP = 0.043**
while stripping, collapsing every `-= getDeltaTime()` timer at one chokepoint
(`AbstractGameAction.tickDuration` :74; `AbstractRoom` waitTimer/endBattleTimer
:233,:279; fade AbstractDungeon.java:2311,2318; `AbstractEvent.waitTimer` :103);
`stripFastCadence` — Postfix `DesktopLauncher.loadSettings`
(desktop/DesktopLauncher.java:107) → `foregroundFPS=backgroundFPS=0`,
`vSyncEnabled=false` (overrides :118,145), read pre-init via its own read-only
`SpireConfig`. **STEP=0.043 is deliberately small AND non-round:** < 0.05 so a
0.1 s timer window still gets ≥ 2 frames (no one-frame *leap-over* of an
intermediate presentation edge such as `BattleStartEffect.showIntent`, which
would leave `intent` stale), and non-round so it evenly divides none of the
game's round timer values — otherwise a countdown lands on *exactly* 0.0 and
skips a strict `< 0.0f` edge (e.g. `AbstractEvent.update` :101-107 shows the
event dialog only as `waitTimer` crosses `< 0.0`; Neow's starts at 1.5, so 0.05
→ 30 steps lands on 0.0 and hangs the event). `getRawDeltaTime()` is left
unpatched, so the frame-skip guard `CardCrawlGame.java:362` keeps working.
Driver grows the A/B harness: `measure_throughput.py` (sustained act/s from a
per-run timing sidecar), `compare_ab.py` (uuid-normalized byte compare),
`extract_scripts.py`; `campaign_driver.py` gains per-seed `--script-dir` replay,
the throughput sidecar, and a script-mode verb settle-wait; `orchestrator.py`
gains `--strip-*` flags. **Acceptance — 20 seeds (STS00001–20, 16 multi-floor,
spanning floors 1–6), the same fixed per-seed scripts run strip-OFF vs strip-ON
on the SAME fork build (sha256 04477E4E), dumps byte-compared after dropping
`uuid`:** 996 records / **12,245,722 B** compared — **12/20 byte-identical,
8/20 differ only in the enumerated presentation fields** (11 `intent` [`move_id`
identical], 3 `move_adjusted_damage` [= −1 when `intent`==DEBUG], 3 residual
`powers` on dead monsters, 1 `neowRng` presence pre-blessing — all dual-prong
proven, design §11 v0.1.2); **0 cmd diffs, 0 length diffs, 0 semantic leaks** —
all 14 RNG streams / pity / pools / `neowRng`, per-monster move history, HP,
master-deck contents, gold, map and living-monster powers byte-identical.
**Throughput (stripped): sustained pooled 32.0 act/s, median 30.1, worst 20.9
(floor 5) — PASS** (~89× the 0.36 states/s B0.2 baseline). Obtain-race
prerequisite fixed in the preceding fix-forward commit (readiness gate on a
pending `ShowCardAndObtainEffect`): the Golden Idol `[Outrun]` Injury, dropped by
stock+driver (master deck 13) and kept by strip (14), now committed in both
(14) so STS00016 converges. Windows-host build via `build_fork.ps1` (JDK 8,
`javac -g`, deterministic jar); game under the bundled JRE 8 `--skip-launcher
--mods basemod,CommunicationMod-oracle`; A/B campaigns + captures under the §7.3
data root, uncommitted. **WSL suite green — 163/163 debug + 163/163 asan** (no
engine change). Follow-ups flagged: (1) reclassify `intent` /
`move_adjusted_damage` disposition `S`→derived in PROTOCOL.md §3 + the B1.5
translator (`move_id` the anchor), one commit — **not** touched here; (2)
b14_accept2 obtain-race capture-fidelity → B5.2 triage (B1.4 acceptance
unaffected).

### B1.4 `[x]` Campaign driver
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
**Log:** Verified by running, not inferred (Windows host — excluded from WSL
CI). Grew `tools/oracle_bridge/driver/` into the real driver:
`campaign_driver.py` (the CommunicationMod-oracle child), `orchestrator.py`
(Windows-host game-lifecycle owner), `validate_artifacts.py` (PROTOCOL.md
schema check); `echo_driver.py` kept as the B0.2 bring-up tool.
**Driver design.** Strict lock-step stepper — **one command per fresh
`ready_for_command`**, freshness by a monotonic recv-index, per-command
wall-clock watchdog, **never blind-resends** (the B0.2 contamination
discipline; the game spawns the child and owns its stdio, so process lifecycle
is the orchestrator's job). Seeded start `start ironclad 20 <SEED>` (base-35
display string); the seed long is read back from `game_state.seed` and
crosschecked against a bit-exact `SeedHelper.getLong` port
(STS12345↔1790052133945; all 10 run headers `crosscheck_ok:true`). Two
generators: **random-legal** (uniform over the game's own `available_commands`,
expanded to concrete legal actions — play+target / potion / choose / confirm /
cancel; `key`/`click`/`wait`/`state`/`start` excluded per scoping §4) and
**script** (paced through the same gate). Per-seed policy RNG (`policy_seed:seed`)
so any run reproduces in isolation (design §7.5 (seed, action-prefix)
reproducers). Terminations (§4.3 / design §1.1): death (GAME_OVER → `proceed`
back to the menu, then the next `start`), **Act-1 boss reward claimed** — stops
BEFORE the boss chest / boss-relic pick — action cap, legal-action exhaustion.
**Artifacts** (design §2.7): one JSONL per run — `header` (schema/driver ver,
game+mod-set, **fork-jar sha256 computed at runtime**, seed **both** encodings,
`oracle_block_enabled`), then one `action` record per injected action
`{action_command, sim_action_bits:null, ready_for_command, available_commands,
state_json}` where `state_json` is the game's dump **verbatim / un-pruned**
(lossless — 64-bit stream longs preserved exactly; B1.5's unknown-field-is-error
contract honoured), then a `terminal` record. **Resume:** durable
`campaign_progress.json` (fsync'd per seed transition) + heartbeat; the
orchestrator relaunches under the bundled JRE 8 on crash / hang / boss-reward;
resume granularity = one seed (retry-once-then-fail, so one seed can't wedge the
campaign). Artifacts live under the §7.3 data root
`D:\STS_BG_Mod\_oracle_data\campaigns`, **never committed**.
**Acceptance — unattended 10-seed campaign, A20 Ironclad, random-legal,
policy-seed 1234, seeds STS00001–STS00010, one deliberate mid-campaign game kill
after 3 seeds** (Windows-host: `python orchestrator.py --campaign-id
b14_accept2 --seeds …/b14_seeds.txt --policy random-legal --policy-seed 1234
--kill-after-seeds 3 --fresh`). Result: **10/10 done, 0 failed, 574 injected
actions**, 2 launches (1 induced kill → 1 relaunch); the killed seed **STS00004
resumed from `start` on launch #2 and completed (attempt 2)**:

| seed | outcome | floor | actions | attempts |
|---|---|---|---|---|
| STS00001 | death | 4 | 79 | 1 |
| STS00002 | death | 2 | 51 | 1 |
| STS00003 | death | 1 | 24 | 1 |
| STS00004 | death | 5 | 67 | **2** (killed+resumed) |
| STS00005 | death | 2 | 42 | 1 |
| STS00006 | death | 1 | 26 | 1 |
| STS00007 | death | 3 | 56 | 1 |
| STS00008 | death | 5 | 76 | 1 |
| STS00009 | death | 6 | 89 | 1 |
| STS00010 | death | 3 | 64 | 1 |

All 10 artifacts **validate against the PROTOCOL.md schema**
(`validate_artifacts.py --campaign`: 10 files, **0 errors**) — header
provenance present, every in-game `state_json` carrying the §3.1 status keys +
§3.2 anchors + the fork **`oracle`** block (14 streams as {counter,s0,s1}, pity,
pools), each run ending in a `terminal` record.
**Honest triage (recorded per the hygiene rules).** A first campaign run
(`b14_accept`, pre-fix code) had **STS00007 fail after 2 crash-retries** (9/10;
the induced kill + crash-resume still let the campaign run unattended to the
end). Root cause, from the seed's partial JSONL + game log (no game exception,
game process alive): on Living Wall's event **GRID**, the random policy sent
`cancel` on the grid-confirm — a game-advertised legal action that **never
re-armed `ready_for_command`**; the 90 s watchdog fired and, retry-once
exhausted, the seed was failed. Classified as a **driver deficiency**, not a
flake: the watchdog conflated "game gone" with "advertised no-op". **Fixed** —
on a watchdog trip the driver now pings `state` (forces a dump regardless of
readiness); a reply proves the game is alive, so the command was a no-op and the
driver escapes by completing the pending screen (`confirm`) instead of declaring
a crash (bounded by `--max-noops`). Reproduced+fixed on the exact seed (per-seed
RNG): STS00007 hits the `cancel` no-op **3×** and now recovers to a natural death
(floor 3). The canonical 10/10 table above was then produced end-to-end by this
committed code.
**Windows-host command + WSL.** Command above; **WSL suite green — 140/140 in
debug + asan** (no engine change; `tools/oracle_bridge/driver` is excluded from
WSL CI, per the working agreements). **B1.5 note:** `state_json` is lossless /
un-pruned (oracle block + signed 64-bit stream longs bit-preserved);
`sim_action_bits` is present-but-`null` for a stable shape; per-record
`ready_for_command` lets the translator drop the driver's no-op recovery
actions. **B1.3 note:** the strip-equivalence A/B and throughput measurement
reuse this lock-step transport; `--timeout`/`--probe-timeout`/`--stall-timeout`
are tunable for the strip-patched ≥5/s floor.

### B1.5 `[x]` Translator (JSON → binary schema)
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
**Log:** Verified by running (WSL Ubuntu-2404), not inferred. New tools-only C++
target `tools/oracle_bridge/translator/` (`oracle_translator` static lib +
`translate_cli`): campaign JSONL → RunState/CombatState via a typed recursive
disposition walker. **Dependency grant (design §2.6):** nlohmann/json v3.11.3
fetched header-only, exposed SYSTEM/INTERFACE (the xxHash pattern), linked
PRIVATE into `oracle_translator` only; proven `sts_engine` links no nlohmann
(its `link.txt` is clean; engine sources grep nlohmann = 0). **Disposition table
(fail-loud, design §2.6):** every container from PROTOCOL.md §3 (stock) + §5
(oracle) has a typed parser that consumes exactly its known keys as
mapped / ignore-with-reason / oracle-advisory / deferred-to-B4.x; any leftover
key, unknown content id (`*_from_game_id`→NONE on a non-empty string), unknown
15th stream name, or oracle-anchor mismatch (seed/floor/act/ascension) throws
with `source:record N:path.key`. Id joins use the **generated** registry tables
(B2.2), not a hand table (the deliverable's hand-table clause is moot post-B2.2).
**Translates NOW (schema storage exists):** RunState `run_seed`, `master_deck`
(registry-known cards), hp/max_hp/gold/ascension/act/floor, relics, potions,
`card_blizz_randomizer`, `blizzard_potion_mod`, the 7 run-scoped streams +
`mapRng`; CombatState player/monsters/piles/turn + the 5 floor-scoped streams.
**Log-DEFERRED to B4.3** (no schema storage — the walker knows the fields and
does not write them; the translator versions its output when B4.3 adds storage):
`neowRng` (14th stream), event-pity floats ×3 (MONSTER/SHOP/TREASURE_CHANCE),
`purgeCost`, `eventList`/`shrineList`/`specialOneTimeEventList`, relic-pool
orders ×5, per-monster move history beyond 3, potion-slot count, real map
nodes/room-types, boss_ids/keys/event_flags/shop_flags, and all `screen_state`
content (events/rewards/shop/grid/map screens). **Boundary vs B1.6:** emits v1
CombatState traces (`write_combat_trace` — "the trace files the diff harness
already reads", design §2.6); the v2 `state_kind` container, `SCHEMA_VERSION`
bump, RunState differ, and oracle adapter are B1.6, untouched here. **Acceptance
— gtest `translator_test`, 7 cases on the committed curated golden sample**
`tests/golden/oracle_corpus/skeleton_sample.jsonl` (13.7 KB, skeleton-scope
content; its `oracle` block copied VERBATIM from real artifact campaign
`b14_accept2` seed STS00001 so the bit-for-bit checks run against genuine
sign-varied 64-bit state — `cardRng.s0` negative, `relicRng.counter==5`):
(1) every stored §2.5 oracle field lands bit-for-bit (7 run streams + mapRng,
5 floor streams, cardBlizz/blizzardPotion; signed longs preserved); (2) unknown
field / unknown card id / unknown stream / anchor mismatch each refused with the
loud message; (3) round-trip stability — translate twice → byte-identical
RunState+CombatState and a byte-identical emitted v1 trace that reads back through
`read_trace` with the floor streams intact. **Full WSL suite green: debug
147/147, asan 147/147** (140 baseline + 7 new). **Real §7.3 corpus (10 artifacts,
`b14_accept2`):** all 10 fail loudly at `record 1
state_json.game_state.deck[0].id "AscendersBane"` — the A20 starting curse the
skeleton registry deliberately lacks; the id-drift guard working on real data
(B3/B4 content lands the registry rows that let real runs translate). Campaign
artifacts never committed. **B1.6 needs:** the v2 container (per-record
`state_kind` + both state sizes in the header; SCHEMA_VERSION 1→2 with a
compat-read for the 20 v1 fixtures per the scoping report), a RunState query
path on the adapter (streams/deck/relics/pity live in RunState, which v1 traces
do not persist — B1.5 verifies them in-memory), and the RunState field-group
differ reusing `cmp_stream` for the 8 run+act streams.

### B1.6 `[x]` Diff-harness run-level + oracle adapter
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
**Log:** Verified by running (WSL Ubuntu-2404), not inferred. **All three presets
green — debug 163/163, asan 163/163, release 163/163** (147 B1.5 baseline + 16
new). **Trace format v2 (`SCHEMA_VERSION` 1->2):** added a per-record
`state_kind` discriminator {COMBAT, RUN} so one container holds both structs;
the 32-byte `TraceHeaderV2` advertises BOTH `sizeof(CombatState)` and
`sizeof(RunState)` for the refusal check. New `write_trace_v2`/`read_trace_v2`
(`tools/diff_harness/src/trace.cpp`); loader refusal preserved and extended (bad
magic / unknown version / struct-size mismatch on combat OR run / unknown record
kind / truncated read -> false). **v1-fixture compat — chose route (a),
compat-read, NOT regeneration** (`stage-b-tasks.md:527-529`): the on-disk format
tag (`kTraceFormatV1`=1 / `kTraceFormatV2`=2) is decoupled from
`engine::SCHEMA_VERSION`, so the v1 `write_trace`/`read_trace` keep their exact
Stage-A behavior and the 20 frozen fixtures load byte-identically with **zero
regeneration** (`git status tests/golden/` empty;
`FixtureOracle.AllFixturesReplayWithZeroDiffs` + `FixtureCountIsAtLeastTwenty`
green). `read_trace_v2` additionally COMPAT-reads v1 files (every record a COMBAT
record, `run_state_size` reported 0) — `TraceV2Compat.ReadsV1TraceAsCombatRecords`.
Only ONE existing assertion changed: `differ_test.cpp:498` now asserts the v1
format tag (the bump's sole consequence; sanctioned by "differ_test extended");
`fixture_oracle_test.cpp` and `translator_test.cpp` UNTOUCHED. `SCHEMA_VERSION`
bump accounts for every pin: `observation.hpp`/`observation_test` use it
symbolically (green); `sizeof(CombatState)`=3504 unchanged (no struct edit).
**RunState differ:** `diff_run_states` reuses the `cmp_stream`/`cmp_i`/`cmp_u`/
`cmp_card_id` idioms with the same named-field output — field groups: character
sheet (run_seed, hp/max_hp/gold/ascension/act/floor), master deck (counted,
positional), relics (counted, **order-sensitive** — a swap is caught, not
treated as an equal set, per trap 8), potions, map grid, boss/keys/event/shop
placeholders, both pity counters, and each of the **8 run-level streams** (7
run-scoped + `map_rng`) individually; memcmp fast path. **Adapter:**
`CommunicationModOracleAdapter` fills the `oracle.hpp:76` seam — file-based over
translated v2 traces (design §2.1 offline model), serving both kinds via `query`
(COMBAT) and a new additive `query_run` virtual (default-false on the base, so
`FixtureFileOracleAdapter` is unaffected); prefix-match / unknown-seed /
prefix-past-end / divergent-prefix / wrong-kind -> false. It reads pre-translated
BINARY traces, so it stays engine/nlohmann-free (§2.6 grant intact). **Tests
added (16):** `differ_test.cpp` +15 (9 `RunDiffer*` = one per field group +
fast-path + per-stream attribution; 5 `TraceV2*` = mixed round-trip
byte-identical, wrong-combat-size / wrong-run-size / unknown-kind refusal,
v1-compat; 1 `CommunicationModOracle` both-kinds + negatives) and new
`tests/oracle_adapter_test.cpp` +1 (end-to-end over the REAL translated golden
sample `skeleton_sample.jsonl` -> translator -> `write_trace_v2` -> adapter,
exercising its 1 RUN + 1 COMBAT dump for both `query`/`query_run`, unknown-seed,
prefix-past-end). **Scope deferrals (noted per hygiene):** (1) **replay-from-
RunState -> B4.4** — the deliverable scopes B1.6 to "adapter + format only"; no
RunState->CombatState derivation exists yet, so `replay` is not generalized here.
(2) **relic-/card-pool ORDER lists -> B4.3** — no RunState storage yet (consistent
with the B1.5 Log deferral); `diff_run_states` compares what has storage. **G4
readiness:** the end-to-end machinery is complete — campaign driver (B1.4) JSONL
-> translator (B1.5) -> v2 container (B1.6) -> adapter + `diff_run_states`/
`diff_states`; run-scoped streams/deck/relics/pity now PERSIST in v2 traces (v1
could not), so run-level RNG cross-checks are diffable. The real 20-seed A20 bar
(G4's "zero unknown-field errors") still needs **B3 registry rows** — a real
campaign fails loudly at unknown content ids (`AscendersBane`/`Burning Blood`/
`Cultist`, the id-drift guard working as designed); validated now on the golden
sample + synthetic mixed containers.

### G4 `[x]` **Gate: oracle bridge live (M2)** — tag `g4-bridge-live`
**Deps:** B1.3, B1.6
Checklist (all must hold, evidence linked in Log):
- [x] 20-seed campaign: every dumped state translates with zero unknown-field
      errors; all 14 stream counters + both pity counters + event pity floats
      + purgeCost present in every record.
- [x] RNG cross-check across the campaign: floor-scoped stream `(s0,s1)` at
      every floor entry matches sim `floor_stream(seed, floor)`; `mapRng`
      state matches `map_stream(seed, 1)`; relicRng counter = 5 at init —
      i.e. the run-scoped/act-scoped derivations hold against the **live
      game**, not just golden vectors.
- [x] Strip-patch equivalence (B1.3 acceptance) re-confirmed on the final
      fork build; throughput ≥ 5 actions/sec recorded.
- [x] WSL CI untouched and green (bridge code adds no CI dependency on the
      game).
Then: update CLAUDE.md "Current state".
**Log:** Verified by running every check against evidence, not inferred. Corpus:
the FINAL-build 20-seed stripped campaign
`D:\STS_BG_Mod\_oracle_data\campaigns\b13_on20b` (STS00001-20, A20 Ironclad,
script policy, floors 1-6; manifest `fork_jar_sha256` =
`04477E4E…B2C36636`), **996 in-dungeon action records** (all 996 carry an
`oracle` block). **Gate tooling landed in this commit** (G4-scoped, clearly
described): (a) translator id-tolerance accounting mode
`TranslateOptions::tolerate_unknown_ids` (CLI `--tolerate-unknown-ids`) —
unknown content ids are tallied per-`<domain>:<id>` and joined to `NONE` instead
of aborting, while unknown **fields**/stream names/anchor mismatches stay fatal;
+2 focused tests (`Translator.TolerateUnknownIdsTalliesInsteadOfThrowing`,
`Translator.TolerateUnknownIdsStillFailsOnUnknownField`); (b)
`tools/oracle_bridge/translator/src/oracle_gate_check.cpp` — a reusable verifier
that `#include`s the engine's tier-1-tested `rng_stream.hpp`/`rng_xs128.hpp`
(the exact constexpr code `rng_stream_test` pins) and checks presence + the RNG
cross-checks over campaign JSONL (reads the uncommitted §7.3 corpus at runtime;
**not** a CI test).

**(1) Translate + presence — PASS.** `translate_cli --tolerate-unknown-ids` over
all 20 runs: **0 drift/error → zero unknown-FIELD errors** across the 996
records (the only fatal conditions left in tolerate mode are unknown fields,
unknown 15th stream names, and oracle-anchor mismatches — none fired).
Unknown-id tally: **94 distinct ids / 5711 hits**, entirely the A20 content the
skeleton registry deliberately lacks pre-B3 — `relic:Burning Blood` 738,
`card:AscendersBane` 1648 (the A20 starting curse), `card:Slimed` 293,
`monster:Cultist` 149, `monster:SpikeSlime_M` 130, `power:Ritual` 124,
`card:Headbutt` 200, … (the id-drift guard reporting, not swallowing). Presence
(`oracle_gate_check`): **0 presence failures over 996 records** — every record
exposes all **13 dungeon stream counters**, **`neowRng`-when-present** (989/996;
absent only in the ~7 pre-blessing dumps, exactly the §2.5 phase rule), both
pity counters (`cardBlizzRandomizer`, `blizzardPotionMod`), the three
`eventPity` floats (`monster`/`shop`/`treasure`), and `purgeCost`.

**(2) RNG cross-check — PASS (all 20 seeds).** Against the engine's own
`floor_stream`/`map_stream`: **`relicRng.counter == 5` at dungeon init** for all
20 (the 5 init relic-pool shuffles); **floor-scoped `(s0,s1)` == `floor_stream(seed,
floor)` bit-for-bit at 75 floor entries** (floors 0-6 across the seeds, read off
`cardRandomRng` at `counter==0` — the pristine per-floor reseed, B1.2 anchor);
**`mapRng` lies on the `map_stream(seed,1)` trajectory** for all 20 (dumped raw
state reached from the pristine act-1 seed `from_seed(seed+1)` in 136-151 raw
`next_long()` steps — map generation mixes wrapper draws (`counter`≈94) with
direct `.random.*` draws so the wrapper counter under-counts `next_long()`s;
matching the full 128-bit state at a specific step is a ~2⁻¹²⁸ coincidence, so
trajectory membership IS the act-scoped seeding proof against the live game).
`oracle_gate_check` summary: `20 run(s): 996 records checked, 0 presence
failures, 75 floor-entries cross-checked, 0 run(s) FAILED`.

**(3) Strip equivalence + throughput — PASS (re-confirmed on the final build).**
`tools/oracle_bridge/build_fork.ps1 -CheckDeterminism -NoDeploy` (Windows host,
JDK 8) reproduces sha256 **`04477E4E…B2C36636`** with **determinism PASS** (two
full builds byte-identical); the **deployed jar** that produced the corpus
(`<game>\mods\CommunicationMod-oracle.jar`) hashes to the same
`04477E4E…B2C36636`, and the corpus manifest stamps the same sha — so B1.3's
evidence was produced on this exact build and stands without re-running game
legs. Cited from B1.3 (design §11 v0.1.2 refined definition): A/B over the same
20 seeds on this build (`b13_on20b` ON twin vs `b13_offscript2` OFF twin, 996
records / **12,245,722 B** each) → **12/20 byte-identical, 8/20 differ only in
the enumerated dually-proven presentation fields**, **0 cmd/length diffs, 0
semantic leaks** (all 14 streams, pity, pools, `neowRng`, move history, HP,
master deck, gold, map, living-monster powers byte-identical). **Throughput
(stripped): sustained pooled 32.0, median 30.1, worst 20.9 (floor 5) act/s** — ≥5
floor met (~89× the 0.36 states/s B0.2 baseline).

**(4) WSL CI untouched + green — PASS.** `.github/workflows/ci.yml` is generic
Ubuntu (`ninja-build` + `python3-yaml`, configure→build→ctest matrix
debug×{asan off,on}) with **no** game / JDK / fork-jar / campaign dependency and
is **untouched** by this commit (the bridge tools read campaign data only at
runtime; nothing in CI builds or runs the game). Full clean suite (`rm -rf
build/<preset>` each) green in all three presets: **debug 166/166, asan 166/166,
release 166/166** (164 baseline + the 2 new id-tolerance translator tests; no
engine change — `SCHEMA_VERSION`=1, `sizeof(CombatState)`=3504 unchanged; changes
confined to `tools/oracle_bridge/translator/**` + `tests/translator_test.cpp`).

---

## Phase B2 — Registry system + skeleton migration (Gate G5) — ∥ with Phase B1

### B2.1 `[x]` ∥ Registry schema + codegen tool
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
**Log:** Verified by running, not inferred: `cmake --preset {debug,asan} &&
build && ctest` in WSL Ubuntu-2404 — **137/137 green in both presets** (Stage A
baseline 131 + 6 new `registry_gen_test` cases:
`RegistryGen.{DeterministicByteIdentical, DuplicateIdFailsWithClearError,
EnumIdsMatchEngine, CardTableMatchesEngine, GameIdTablesRoundTrip,
ManifestCounts}`). `registry/` seeded with all 8 domains (5 cards STRIKE=1..
POMMEL_STRIKE=5, 3 powers STRENGTH=1..WEAK=3, Jaw Worm JAW_WORM=1; the other 5
valid-but-empty); `tools/registry_gen/gen.py` (Python 3 + PyYAML) emits the
id enums, the `CardDef`/step table in cards.hpp's exact shape, the
`game_id`↔enum string tables, and the row-count manifest — deterministic
(sorted, no timestamps, byte-identical across two runs) with append-only ids
re-pinned by `static_assert`. CMake custom-command wiring emits headers under
`<build>/generated/` (never committed; `.gitignore` extended). Non-breaking:
the engine still uses its hand tables (`types.hpp`/`cards.hpp` untouched); the
generated `sts::registry` tables are proven byte-equal to them by the test, so
B2.2's swap is zero-change. Provenance: `Strike_Red`/`Defend_Red`/`Bash`/
`ShrugItOff`/`PommelStrike`.use + ID; `StrengthPower`/`VulnerablePower`/
`WeakPower` POWER_ID; `JawWorm` stat/getMove branches (all read in full).

### B2.2 `[x]` Skeleton migration onto the registry
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
**Log:** Verified by running, not inferred: full clean rebuild in WSL
Ubuntu-2404 (`rm -rf build && cmake --preset {debug,asan} && build && ctest`) —
**140/140 green in both presets**: Stage A's 131 baseline tests (incl. all 20
combat fixtures, `FixtureOracle.AllFixturesReplayWithZeroDiffs` /
`FixtureCountIsAtLeastTwenty`) + B2.1's 6 registry_gen cases + 3 new
`RegistryGen.{EngineReExportsGeneratedTables, MonsterTableMatchesJava,
DuplicateMoveIdFailsWithClearError}` — with **zero test-file edits and zero
fixture regeneration** (no Stage A test or `tests/golden/` byte changed; only
the B2.1 registry-gen test TUs grew the new monster-table cases).
`sizeof(CombatState)` = 3504 and `SCHEMA_VERSION` = 1, compile-probed identical
before/after the swap. Migration shape: `types.hpp`'s
CardId/PowerId/MonsterId/RelicId are now using-aliases of the generated
`sts::registry` enums (ids re-pinned by generated `static_assert`s;
Action/ActionVerb stay hand-written); `cards.hpp` re-exports the generated
`CardDef` table (kStrike…kPommelStrike, `card_def`) with drift pins holding
`sts::registry::Opcode` byte-equal to interp.hpp's; Jaw Worm's HP range and
move-effect amounts are per-ascension-tier columns in `monsters.yaml`
(base/a2/a7/a17, each citing its JawWorm.java:79-104 branch; move ids
CHOMP=1/BELLOW=2/THRASH=3 per :65-67), emitted by gen.py's new
`monster_table.hpp` (`MonsterDef` with constexpr last-matching-threshold tier
lookups) and enqueued data-driven by `jaw_worm_take_turn` at the skeleton's
fixed A20 — getMove *selection* stays native per design §4.2. No dual system:
the hand enums/tables are deleted;
`RegistryGen.EngineReExportsGeneratedTables` pins engine == registry
entity-for-entity. Build: `tools/registry_gen` now precedes `src/engine`
(unconditional); `sts_engine` links `registry_generated` PUBLIC with an
explicit codegen dependency, so generated headers exist before any engine TU
compiles.

### G5 `[x]` **Gate: registry live** — tag `g5-registry-live`
**Deps:** B2.2
All Stage A tests green through the generated path in debug, asan, release;
hand tables deleted (no dual system); manifest reports exactly the skeleton
row counts; CI runs the generator (PyYAML available in the CI image — extend
the workflow in this commit). Nothing in Phases B3/B4 starts before G4 **and**
G5 are both `[x]`. Then: update CLAUDE.md "Current state".
**Log:** Verified by running, not inferred (orchestrator re-ran every check on
integrated master `5180930`). **All 140 tests green through the generated path
in debug, asan, AND release** (each a clean `rm -rf build` config→build→ctest;
release run via `ctest --test-dir build/release` since there is no release
test-preset — build/test both exit 0). **Hand tables deleted / no dual system:**
`types.hpp` has no `enum class CardId/PowerId/MonsterId/RelicId` bodies left
(all `using`-aliased to `sts::registry`), `cards.hpp`/`monster_jaw_worm.hpp`
re-export the generated tables; `RegistryGen.EngineReExportsGeneratedTables`
pins engine == registry entity-for-entity. **Manifest = exactly the skeleton
counts:** cards=5, powers=3, monsters=1, relics/potions/events/encounters/a20=0
(total 9), read from `build/release/generated/sts/registry/manifest.hpp`.
**CI runs the generator:** `.github/workflows/ci.yml` install step gains
`python3-yaml` (PyYAML for the system `python3` that CMake's
`find_package(Python3)` selects); the literal CI flow was reproduced locally
(`cmake -S . -B build/ci -G Ninja -DCMAKE_BUILD_TYPE=Debug` → build → ctest)
— configure/build/test all exit 0, 140/140, and all five generated headers
(`ids/card_table/monster_table/game_ids/manifest.hpp`) are emitted under the
build tree (never committed). `SCHEMA_VERSION`=1 and `sizeof(CombatState)`=3504
unchanged. **Note:** B3/B4 remain blocked — they require **both** G4 and G5,
and G4 (oracle bridge) is not yet reached.

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

### B3.1 `[x]` Interpreter/card-mechanics extensions
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
**Log:** Verified by running (WSL Ubuntu-2404), not inferred. Landed the B3.1
engine surface with **zero CombatState/CardInstance layout change**
(`CardInstance.flags`/`cost_now`/`misc`/`upgrade` already existed) -> `SCHEMA_VERSION`
stays 2, all 20 combat fixtures load with **zero regeneration** (`git status
tests/golden` clean; `FixtureOracle` green). Every semantics cite read in full
before coding.
**New opcodes (append-only from 9, documented interp.hpp; gen.py OPCODES +
cards.hpp drift-pin extended):** `MAKE_CARD`=9 (card creation into a pile),
`SET_COST`=10 (cost_now write primitive). **Targeting:** new `StepTarget`
ALL_ENEMY=2 / RANDOM_ENEMY=3 + execute-time actor sentinels
`kActorAllEnemies`=0xFD / `kActorRandomEnemy`=0xFE. `execute_opcode` resolves
them at EXECUTE time -- AoE fans out over LIVE monsters with a SEPARATE
DamageInfo per target (DamageAllEnemiesAction.update:56-83); RANDOM rolls one
`card_random_rng` draw PER hit (AttackDamageRandomEnemyAction.update). **Flags:**
`CardFlag` bits EXHAUST/ETHEREAL/INNATE/UNPLAYABLE/RETAIN/XCOST (types.hpp),
mirrored in gen.py `CARD_FLAGS` + generated `kCardFlag*`, drift-pinned in
cards.hpp; seeded onto each `CardInstance.flags` at combat_begin/creation. B3.1
WIRES: EXHAUST (played card -> exhaust pile, UseCardAction), UNPLAYABLE (barred
in `legal_actions`), XCOST; ETHEREAL/INNATE/RETAIN are named reserved bits whose
end-of-turn/combat-begin sweeps land with their first content consumer (§5.4
frozen order). **X-cost** (XCOST flag; gen maps YAML `cost: -1`): consumes ALL
energy, repeats the effect program `energyOnUse` times, zeroes energy
(WhirlwindAction.update); cost_now 0 keeps it affordable at 0 energy
(costForTurn -1). **Upgrade plumbing (two-row):** the generated `CardDef` gains
`flags` + `upgraded_cost`/`upgraded_flags`/`upgraded_step_count`/`upgraded_steps`
(a card with no `upgraded:` block emits upgraded == base); `card_effect_steps`/
`card_cost`/`card_flags(def, upgrade)` select the row by `CardInstance.upgrade`
(a count -> 0 base, >0 upgraded; Searing Blow's count-encoding left to B3.5 per
the scoping report). **Cost modifiers:** SET_COST writes `card_pool[src].cost_now`
(clamped u8); the per-instance cost is honored at play (the "temporary"/per-turn
reset + which-card selection belong to the consumer power hook (B3.2) / CHOOSE).
**MAKE_CARD** into HAND/DRAW/DISCARD/DRAW_RANDOM: allocates a free 160-row pool
slot, seeds cost/flags from the registry; hand-full spills to discard
(MakeTempCardInHandAction.update:71-77); DRAW_RANDOM inserts at
`cardRandomRng.random(size-1)` (one draw; CardGroup.addToRandomSpot:463-468),
empty pile appends with no draw. **Provenance read in full:** Whirlwind.java /
WhirlwindAction, DamageAllEnemiesAction, AttackDamageRandomEnemyAction,
MakeTempCardInHandAction, CardGroup.addToRandomSpot, AbstractCard flag/cost
fields (exhaust/isEthereal/isInnate/retain/costForTurn/energyOnUse; cost -1 X /
-2 unplayable).
**Acceptance -- new tier-2 suite `tests/card_ext_test.cpp` (15 cases)** + 2 new
`registry_gen_test` cases: AoE live-only + per-target DamageInfo; random per-hit
one-draw/exclude-dead; X-cost consumes-all-energy + repeat (and 0-energy plays
for nothing, still legal); EXHAUST routing; UNPLAYABLE gating; MAKE_CARD into
hand (with room + hand-full->discard spill), discard, draw-top, draw-random
(one card_random_rng draw / empty-append no draw); SET_COST write+clamp+honored-
at-play; upgrade helpers select base/upgraded by bit + resolve honors the bit;
codegen: skeleton flags==0 & upgraded==base, and a synthetic `upgraded:`+`flags:`+
X-cost card emits a DISTINCT upgraded row + flag word end-to-end through gen.py.
**Suites: debug 191/191, asan 191/191, release 191/191** (166 pre-B3 baseline +
17 new mechanic tests + 8 concurrent B4.1 map tests; generator determinism green;
gen.py runs on PyYAML per CI). **B3.2 / card batches inherit:** effect-program
authoring conventions -- ALL_ENEMY/RANDOM_ENEMY step targets fan out at execute
time; the two upgrade rows are selected by `card_effect_steps`; the MAKE_CARD
ActionQueueItem encoding is `{flags low16 = CardId, src = CardPile, amount =
count, tgt = kActorPlayer}` and SET_COST is `{src = pool index, amount = cost}`
(gen.py step-authoring for these two ops is deferred to their first card
consumer -- the opcodes/interpreter are ready); YAML `flags:` (+ `cost: -1/-2`)
and an optional `upgraded:` full-program block are now honored by codegen. B3.2's
power hooks attach the cost_now/exhaust/ethereal sweeps to the frozen §5.3-5.5
order.

### B3.2 `[x]` Power-hook framework completion
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
**Log:** Verified by running (WSL Ubuntu-2404), not inferred. Landed the power-
hook framework with **zero CombatState/CardInstance/PowerSlot layout change** ->
`SCHEMA_VERSION` stays 2, all 20 combat fixtures load with **zero regeneration**
(`git status tests/golden` clean; `FixtureOracle` green). Every hook firing site
read in full in the decompiled Java before coding.
**Framework.** `Hook` enum + dispatch (`include/sts/engine/power_hooks.hpp`,
`src/engine/power_hooks.cpp`): 14 hook points, each dispatched in the FROZEN
per-hook source order (verified against Java, which differs by hook -- NOT one
generic fan-out): §5.3 onPlayCard = player powers -> monster powers -> relics ->
stance -> blights -> hand/discard/draw cards (GameActionManager.java:222-245);
UseCardAction onUseCard = player powers -> relics -> hand/discard/draw cards ->
monster powers (UseCardAction.java:41-64, monsters LAST -- distinct from
onPlayCard); §5.4 callEndOfTurnActions (:369-377); §5.5 onExhaust relics ->
player powers (CardGroup.moveToExhaustPile:851-856); APPLY_POWER source
onApplyPower then target Artifact nullify (ApplyPowerAction.java:106-138);
wasHPLost victim powers guarded on info.owner (AbstractPlayer.damage:1445-1447);
onGainedBlock (AbstractCreature.addBlock:426-433); atStart/atEnd pre/post.
Per-power a hook is DATA (a hook->effect-program binding, run by queuing the
steps owner-relative) or **native** (the escape hatch `dispatch_native_hook`).
**Registry schema.** `powers.yaml` gains `type` (BUFF/DEBUFF -- the interception
reads it), `stack` (intensity/none), `native`, and `hooks:` (hook -> effect
program, reusing the CardEffectStep shape; `amount: 0` = "use the power's stack
amount"). `gen.py` emits `power_table.hpp` (`PowerDef` + `hook_binding` +
`power_def()`, deterministic, sorted); new engine re-export
`include/sts/engine/powers.hpp` with the `Hook`/`kPowerHookCount` drift-pins
(byte-equal to `power_hooks.hpp`). The 3 skeleton powers bind ZERO hooks (their
damage-pipeline behaviour stays native in `interp.cpp`, unchanged).
**New opcode `LOSE_HP`=11** (append-only; interp.hpp + cards.hpp drift-pin +
`gen.py`): card/self HP loss bypassing block (HP_LOSS), the firing site for
wasHPLost with source==self -- the Rupture attribution. `execute_opcode` /
`op_apply_power` (now threads `src`) / `op_block` / DRAW / `op_damage` all invoke
the dispatch; the pump (`action_queue.cpp` start/end-of-turn) and `card_play.cpp`
(onPlayCard/onUseCard fan-out, onExhaust on played-card exhaust) + `piles.cpp`
(onExhaust on the EXHAUST opcode) replace the A4.3/skeleton no-op stubs.
**Regression invariant** held by construction: every dispatch site is a pure
no-op when no hook-bearing power is present
(`PowerHooks.NoBoundPowerQueuesNothing`), so the fixtures -- which carry only
Strength/Vulnerable/Weak -- stay byte-identical.
**Framework powers (ids 4-12, the hook plumbing the ~30 card-applied powers
attach to; the CARDS that create them + the remaining powers land with B3.3+):**
Artifact, Metallicize, Feel No Pain, Dark Embrace, Combust, Rupture, Sadistic,
Corruption, Rage -- each cited to its `*Power.java` hook body (read in full) in
`powers.yaml`. **Acceptance -- new tier-2 suite `tests/power_hooks_test.cpp` (12
cases)** + `registry_gen` updates (ManifestCounts 3->12 powers; determinism now
covers `power_table.hpp`; standalone-compile asserts on the generated PowerDef):
ordering (§5.3 player-before-monster fan-out) + the **five §5.3-5.5 stress
cases**, each hand-derived from the Java: (1) onExhaust list order --
`OnExhaustFollowsPlayerPowerListOrder` (Feel No Pain + Dark Embrace, both
orderings); (2) onUseCard fan-out -- `CorruptionRedirectsPlayedSkillToExhaust` /
`CorruptionDoesNotRedirectAttacks` / `CorruptionZeroesDrawnSkillCost`; (3)
atEndOfTurn stack -- `EndOfTurnPreCardPowersBeforeAtEndOfTurnPowers`
(Metallicize pre-card BLOCK queues before Combust LOSE_HP+AoE); (4) APPLY_POWER
interception -- `SadisticFiresWhenPlayerDebuffsUnprotectedTarget` /
`ArtifactNullifiesDebuffAndBeatsSadistic` / `ArtifactDoesNotBlockBuffs`; (5)
wasHPLost attribution -- `RuptureFiresOnSelfInflictedHpLoss` /
`RuptureDoesNotFireOnUnblockedEnemyDamage`. **Suites: debug 213/213, asan
213/213, release 213/213** (201 pre-B3.2 baseline incl. the concurrent B4.2 map
tests + 12 new power-hook cases; generator determinism green). **Deferred (noted
per hygiene, land with their consumers):** DAMAGE-opcode damage-TYPE (THORNS/
HP_LOSS vs NORMAL -- Sadistic/Combust THORNS is NORMAL-typed today, so a
Vulnerable target over-counts hook-queued damage; the stress-4 test uses Weak to
stay on the dispatch the framework owns); per-power counter storage (Panache
every-5th, The Bomb 3-turn, Combust hpLoss ratchet, Rampage) -- no new PowerSlot
field in B3.2; recursive-play (Double Tap/Havoc/Mayhem, opcode R14); card-level
hooks (Sentinel onExhaust, curse EOT/on-draw triggers -- B3.6/B3.9) and relic
onPlayCard/onExhaust (B3.24+) -- their dispatch stages are present as ordered
structural call sites; Barricade block-decay branch (B3.8, left structural).
**Card batches inherit:** a power row declares hooks via `hooks: {<hook>: [steps]}`
(data) or `native: true` (escape hatch, body in `dispatch_native_hook`); stacking
is additive (`stack: intensity`, the `op_apply_power` default); a hook step's
`target: SELF` is the owner and `amount: 0` pulls the power's stack amount.

### B3.3 `[x]` ∥ Red commons — attacks
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
**Log:** Verified by running (WSL Ubuntu-2404); every card / action read in full
in the decompiled Java before coding. Landed **cards.yaml ids 11-23** (the 13
red common attacks, base+upgraded) **+ Wound (id 24, first STATUS card)** as the
Wild Strike dependency (B3.9 lands the rest). **Zero CombatState/CardInstance
layout change** → `SCHEMA_VERSION` unchanged, all combat fixtures load with zero
regeneration (the new card-property columns are CardDef-only, NOT seeded onto
CardInstance.flags). **New opcodes (append-only from 15; interp.hpp + gen.py
OPCODES + cards.hpp drift-pin):** `DAMAGE_BLOCK`=15 (Body Slam — base ==
player_block at execute; BodySlam.java:96), `DAMAGE_STR_MULT`=16 (Heavy Blade —
`amount` base with Strength counted x `extra` via a new `compute_damage`
strength_mult overload; HeavyBlade.java:426-435), `DAMAGE_PER_STRIKE`=17
(Perfected Strike — `amount` + `extra`-per-"Strike"-card, **baked into a plain
DAMAGE at QUEUE time** with the just-played source excluded, matching
applyPowers-at-use; PerfectedStrike.java:565-607). **New engine surface (no new
opcode):** (a) `ChoiceKind::DISCARD_TO_DRAW_TOP`=3 — Headbutt, a DISCARD-source
CHOOSE_CARD; the just-played source card is stamped into the item's `tgt` and
excluded from the choice because resolve_card_play moves it to the discard early
but the game keeps it in limbo (UseCardAction is queued AFTER the card's own
DiscardPileToTopOfDeckAction, AbstractPlayer.useCard:1369-1375) — `excluded`
threaded through choice_slot_eligible/count_eligible/choice_requires_user +
`ActionMask.choice_from_discard`; (b) Clash `canUse` — a CardDef `requires_all_attacks`
column checked in legal_actions (playable only if every hand card is an Attack;
Clash.java:184-194); (c) `is_strike` CardDef column (mirrors CardTags.STRIKE;
set on Strike/Pommel/Perfected/Twin/Wild) driving the Perfected Strike count;
(d) `CardType::STATUS`=2 (Wound). **MAKE_CARD authoring (gen.py, B3.1 left it to
the first consumer):** the step packs `{CardId | CardPile<<16 | upgraded-copy<<24}`
into `extra`; card_play.cpp splits it into `{flags=CardId(+upg bit), src=CardPile}`;
op_make_card honors the upgraded-copy bit — Anger clones an UPGRADED Anger when
upgraded (makeStatEquivalentCopy preserves timesUpgraded), Wild Strike shuffles a
base Wound into a random draw-pile spot (one card_random_rng draw). AoE
(Cleave/Thunderclap) reuse B3.1's ALL_ENEMY fan-out; Sword Boomerang reuses
RANDOM_ENEMY (one card_random_rng draw per hit, dead excluded). **Acceptance —
new tier-2 suite `tests/card_attacks_test.cpp` (35 cases)**, per-card both upgrade
rows hand-computed from the cited use(): block-derived (Body Slam +Strength),
Strength×3/×5 + Strength-then-Vulnerable (Heavy Blade), per-Strike count
excluding self (Perfected Strike), Anger base/upgraded self-copy, Wild Strike
Wound + one-draw, Cleave/Thunderclap AoE live-only + Vulnerable-to-all,
Sword Boomerang 3/4 hits one-draw-per-hit + dead-exclusion (trap-10), Clash
canUse masking, Headbutt empty/auto/prompt discard-choice with source excluded,
Wound unplayable, + a directed advance() script (Cleave then Thunderclap over 2
monsters). `registry_gen_test` counts updated (cards 10→24, total 105→ tracks
powers; standalone kMaxCardSteps 2→4). **Verified in an ISOLATED worktree at HEAD
40e1715 + only my files** (the concurrent potion-powers agent's uncommitted
powers/potions/relics.yaml were excluded): **debug 308/308, asan 308/308, release
308/308** (273 baseline + 35 new). Also removed an orphaned `DamageType`/`kBlockNoPowers`
hunk the powers agent left in interp.hpp (unreferenced anywhere; that branch is
authoritative for it).

### B3.4 `[x]` ∥ Red commons — skills
**Deps:** B3.2 · **Spec:** design §5.1 · **Provenance:** cards/red COMMON
skills (~6 beyond Shrug It Off)
**Deliverables:** registry entries: Armaments (upgrade-in-combat, grid
choice), Flex (temporary Strength — StrengthDown), Havoc (play top of draw,
exhaust it), True Grit (random/targeted exhaust), Warcry (draw + put-back).
Introduces the in-combat card-choice screen verb plumbing (CHOOSE in combat)
where needed (Armaments+, True Grit+).
**Acceptance:** tier-2 per card; CHOOSE-in-combat legal-action masking tested;
directed script.
**Log:** Verified by running (WSL Ubuntu-2404), not inferred; every card /
action / power read in full in the decompiled Java before coding. Landed the 5
cards (cards.yaml ids 6-10, base+upgraded) + LoseStrength power (powers.yaml id
13) with **zero CombatState/CardInstance/PowerSlot layout change** ->
`SCHEMA_VERSION` stays 2, all combat fixtures load with zero regeneration
(FixtureOracle green). **CHOOSE-in-combat lives IN the action queue** (new
opcode CHOOSE_CARD=12), not a new state field: the pump peeks the queue front
and BLOCKS (WAITING_ON_USER, item left at head) when a CHOOSE_CARD needs a real
selection (`choice_requires_user`: non-random, eligible > amount);
`legal_actions` exposes eligible slots via `ActionMask.can_choose[]` /
`choice_pending`; `advance(CHOOSE, hand_slot)` applies one selection, decrements
the queued amount, re-pumps. Forced (eligible<=amount) and RANDOM selections
auto-resolve at execute time (ExhaustAction/PutOnDeckAction/ArmamentsAction
no-screen branches). **New opcodes** (append-only from 12; interp.hpp + gen.py
OPCODES + cards.hpp drift-pin): CHOOSE_CARD=12 (exhaust/put-on-draw-top/upgrade;
kind+RANDOM bit packed in step `extra`, gen.py CHOICE_KINDS mirror),
PLAY_TOP_DRAW=13 (Havoc), REMOVE_POWER=14 (LoseStrength self-removal). **Havoc**
excludes the just-played source card from its own PLAY_TOP_DRAW deck (the game
keeps it cardInUse/limbo — AbstractPlayer.useCard removeCard+cardInUse,
UseCardAction queued after; our synchronous resolve moves it to discard early,
so resolve_card_play stamps the source pool index and op_play_top_draw lifts it
out of discard for the duration and restores it). **Flex** LoseStrength (native,
DEBUFF, game_id "Flex") at_end_of_turn queues Strength -amount + REMOVE_POWER on
the owner (both addToBot), so +Strength lasts one turn. **Acceptance — new
tier-2 suite `tests/card_skills_test.cpp` (18 cases)** + `registry_gen_test`
ManifestCounts (cards 5->10, powers 12->13) + the codegen SYNTH_XCOST card's id
(6->100, since 6 is now ARMAMENTS): gen<->engine CHOOSE flag pin; Flex
apply/upgrade/end-of-turn reversal+self-removal; True Grit random/forced exhaust
(card_random_rng draw accounting) + upgraded CHOOSE mask/resolve; Warcry
draw-then-put-back + upgraded draw-2; Armaments forced/prompted/upgrade-all +
mask excluding upgraded; Havoc play-top/empty-noop/discard-reshuffle; directed
CHOOSE->upgrade->play script. All hand-computed from the cited Java. Triple-preset
(debug/asan/release) green at 6c5f7f4; merged debug green at e3f71c9 (potions +
relics landed) — my power_hooks/action_queue additions are disjoint from B3.24's
relic wiring. **Suites: debug/asan/release green (baseline + 18 new B3.4 cases).**

### B3.5 `[x]` ∥ Red uncommons — attacks
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
**Log:** Done 2026-07-23. Enumerated the exact 11-card constructor roster:
Blood for Blood, Carnage, Dropkick, Hemokinesis, Pummel, Rampage, Reckless
Charge, Searing Blow, Sever Soul, Uppercut, and Whirlwind (Infernal Blade is an
uncommon Skill and is therefore B3.6). Appended card IDs 40–50 and opcodes
21–24 without renumbering existing registry values; manifest is cards 50 / total
162 and deterministic generation remains byte-identical. Implemented the Java
behavior from the 11 card classes plus `DropkickAction`,
`ModifyDamageAction`, `MakeTempCardInDrawPileAction`,
`ExhaustAllNonAttackAction`, `WhirlwindAction`,
`AbstractPlayer.updateCardsOnDamage`, and `AbstractCard` upgrade/cost handling:
per-positive-HP-loss Blood for Blood cost updates across hand/discard/draw,
execute-time Dropkick Vulnerable gating, Rampage per-instance combat scaling,
Reckless Charge's random Dazed insertion, Sever Soul filtering, and X-cost AOE.
Searing Blow uses the existing `CardInstance.upgrade` `uint8_t` as its upgrade
count (damage `12 + 4n + n(n-1)/2`, repeated-upgrade eligibility through 255)
and Rampage uses the existing combat-only `misc`; `sizeof(CardInstance)` stays
8, so there is no schema/design-log change. Added 18 tier-2/directed tests,
including RNG draw count and the public Hemokinesis → reduced Blood for Blood
script. Hand-derived from the cited decompiled Java; no live-oracle capture or
fixture regeneration was needed. Verification: focused registry 14/14 and card
suite 18/18; full debug 405/405, leak-detecting ASan/UBSan 405/405 with no
diagnostics, and release 405/405.

### B3.6 `[x]` ∥ Red uncommons — skills
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
**Log:** Done 2026-07-24 on base 01d085a. **Enumeration evidence:** grepping
every cards/red constructor for `CardRarity.UNCOMMON` x `CardType.SKILL` yields
exactly **17** members — the deliverable's 16 **plus Rage** (Rage.java:31 is
`CardType.SKILL`; this ledger's B3.7 deliverable list misfiles it under power
cards — source wins, Rage lands HERE; B3.7 should skip it). All 17 classes plus
DualWieldAction / DoubleYourBlockAction / BlockPerNonAttackAction /
SpotWeaknessAction / ExhaustAllEtherealAction / MakeTempCardInHandAction /
NoDrawPower / FlameBarrierPower / RagePower / DrawCardAction:69-73 /
ApplyPowerAction:96-138 / AbstractDungeon.returnTrulyRandomCardInCombat:964-979
/ AbstractRoom.endTurn:393-408 / AbstractCard.makeStatEquivalentCopy:826-848 /
resetAttributes:2035-2045 read in full. **Registry:** card ids 51–67 (addRedCards
order); powers NO_DRAW=24, FLAME_BARRIER=25 (22/23 reserved by the orchestrator
for B3.17 SPLIT / B3.25 NEXT_TURN_BLOCK); opcodes DOUBLE_BLOCK=30,
BLOCK_PER_NON_ATTACK=31, SPOT_WEAKNESS=32, RANDOM_ATTACK_TO_HAND=33 (25–29
reserved for B3.17); ChoiceKind DUPLICATE=4 (kind bit 2 packs at extra bit 3
above the RANDOM bit, `copies`-1 in bits 4–7 — every pre-B3.6 packed extra is
byte-identical); CardType POWER=4 appended (Dual Wield eligibility;
no POWER rows yet); CardFlag COST_MODIFIED_FOR_TURN=1<<6 (per-instance
setCostForTurn bit, reset by the end-turn sweep + on exhaust); CardDef gains the
on_exhaust/upgraded_on_exhaust program pair (Sentinel triggerOnExhaust, fired
LAST in the §5.5 moveToExhaustPile order, addToTop). This branch's manifest:
cards 50→67, powers 21→23 rows, total 167→186 (deltas +17/+2/+19 vs 01d085a).
**Infernal Blade:** kIroncladAttackPool generated from color/rarity/type/healing
columns (RED C/U/R ATTACK minus HEALING) = 25 rows today; membership
self-completes when B3.8 lands its rares — **B3.8 must set `healing: true` on
Feed and Reaper** (CardTags.HEALING) or the pool goes wrong. One
card_random_rng draw per play (tested). DOCUMENTED interim deviation: the
game fills pools in CardLibrary-HashMap "library order" (design §5.1); this
pool is emitted in registry-id order until B4.5's oracle capture pins library
order (one-line fix in gen.py; the B4.6 relicPools-translator deferral is the
precedent). Also recorded: an IB-generated Blood for Blood that then takes an
HP-loss event models its cost via cost_now only (a fresh-copy `cost` field does
not exist per-instance), so the reset-at-end-of-turn restores 4 rather than the
game's reduced base — unreachable without generating BfB and holding it unplayed
through the turn; revisit if G7 ever hits it. **Stop-the-line finds (Java vs
prior code, fixed forward in this commit):** (1) StrengthPower/DexterityPower.
stackPower REMOVE the slot when a stack lands on exactly 0 (StrengthPower.java:
48-53 / DexterityPower.java:44-49, queued addToTop) — B3.4's Flex test and
B3.23's LoseDexterity test asserted a 0-amount residue slot; both tests
corrected, op_apply_power now queues the removal (Disarm exercises it). (2) A
negative-amount Strength/Dexterity constructs as PowerType.DEBUFF
(StrengthPower ctor :37 → updateDescription :81-89), so Disarm IS
Artifact-nullified/Sadistic-visible — op_apply_power flips is_debuff for
amount<=0 on those two ids. (3) RAGE (power id 12) completed per its recorded
B3.2 deferral: rebound from on_play_card(data) to native ON_USE_CARD with the
ATTACK guard + AT_END_OF_TURN self-removal — the on_use_card timing is
OBSERVABLE via Body Slam (game reads base==block at use() while Rage's block
rides the later UseCardAction; tested); the power_hooks ordering probe moved to
the on_use_card fan-out. (4) NoDraw re-application short-circuits the WHOLE
ApplyPowerAction before source hooks/Artifact (ApplyPowerAction:102-105);
NoDraw amount is the -1 marker and the DRAW opcode is hard-gated
(DrawCardAction:69-73), incl. the start-of-turn 5. Entrench's and Ghostly
Armor's odd triggerOnEndOfPlayerTurn→ExhaustAllEtherealAction overrides are
behaviorally subsumed by the existing ethereal sweep (base AbstractCard:
2176-2179; no S1 on-exhaust-order consumer) — no engine change, noted in YAML.
Dual Wield reproduces DualWieldAction exactly: ATTACK/POWER eligibility, zero/
forced/prompted branches, the prompted screen's hand reorder ([other eligibles]
+ [ineligibles] + [selected + copies]), stat-equivalent clones (upgrade count,
cost_now incl. FOR_TURN bit, misc), hand-cap spill to discard. Second Wind
exhausts non-Attacks in reverse hand order BEFORE its per-card block gains
(each card-style, Dexterity/Frail per gain — this.block is applyPowers block);
Sentinel's energy fires mid-sweep (tested). **Acceptance — new tier-2 suite
`tests/card_uncommon_skills_test.cpp` (35 cases)**: per-card BASE and UPGRADED
rows hand-computed from the cited use(); cardRandomRng draw-count tests
(Infernal Blade one draw; True Grit/Wild Strike unchanged); No Draw
block/expiry; Dual Wield forced/prompted/copies/mask; Flame Barrier
fully-blocked reflect + no-Vulnerable-amplification + next-turn expiry; cost-
for-turn end-of-turn reset; Spot Weakness intent gate both ways; directed
public advance()/legal_actions() script (Rage → Second Wind exhausting
Sentinel → Strike). registry_gen_test ManifestCounts 67/23/186 +
registry_gen_standalone pool/count pins updated; power_hooks Rage probe +
LoseDexterity and card_skills Flex expectations corrected per (1)/(3).
Hand-derived from the decompiled Java throughout; no live-oracle capture and
no fixture regeneration (SCHEMA_VERSION unchanged, CardInstance still 8 B).
Verification: focused suite 35/35; full **debug 464/464, ASan/UBSan
(detect_leaks=1) 464/464 with zero diagnostics, release 464/464**.

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

### B3.9 `[x]` ∥ Status + curses
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
**Log:** Done 2026-07-23. Read the full status/curses sources before coding: `Burn`, `Dazed`, `Slimed`, `VoidCard`, `Wound`, `Clumsy`, `Decay`, `Doubt`, `Injury`, `Normality`, `Pain`, `Parasite`, `Regret`, `Shame`, `Writhe`, and `AscendersBane`, plus `CardLibrary.getCurse`/`AbstractDungeon.returnRandomCurse`, `CardGroup.initializeDeck`, `DiscardAtEndOfTurnAction`, and `AbstractCreature.decreaseMaxHealth`.

- Registry card ids 25–39 add the four remaining statuses and all eleven curses. Append-only integration after B3.13 preserves `RITUAL=19` and `CURL_UP=20`, then adds native `FRAIL=21`; combined manifest counts are cards 39 / powers 21 / monsters 4 / total 151. The generated card table carries passive-trigger, ten-card curse-pool, and master-deck-removal metadata. `return_random_curse(card_rng)` consumes exactly one cardRng draw and excludes Ascender's Bane; `remove_master_deck_card` applies Parasite's -3 max-HP/clamp rule.
- End-of-turn now queues hand-card effects, then player at-end powers, then `DISCARD_HAND=19`: Regret/Burn/Decay/Doubt/Shame resolve against the full hand before ethereals exhaust and non-Retain cards discard. `LOSE_HP_PER_HAND=18` and queued `REDUCE_POWER=20` extend the opcode table without renumbering prior values. Void, Pain, Normality, Slimed, and Writhe innate opening-hand ordering are wired through their cited runtime paths.
- Frail is fully live, not an inert row: card block runs its x0.75 float modifier and floors once after all powers; direct GainBlockAction block bypasses it. `CombatState.flags` stores the player instance's `justApplied` latch without a layout/schema change; a new Shame/monster-sourced instance skips its first end-of-round decrement, stacking preserves the existing latch, and later rounds queue ReducePowerAction-equivalent reduction/removal after the power-list walk. B3.13's Cultist Ritual end-of-round and louse Curl Up behavior remain intact.
- Added `status_curse_test` (9 tier-2/directed tests). Correct hand discard intentionally changes the 20 combat traces; all were regenerated from the checked-in generator, replay green, and a repeated generation produced an identical sorted SHA-256 manifest. Updated the affected CardIntegration hash/trace, power-hook action count, and translator unknown-id probe rather than weakening them.
- Verification: task branch full Debug 353/353 and leak-detecting ASan/UBSan 353/353. After semantic integration with B3.13 and Frail completion, full WSL Ubuntu-2404 Debug 368/368 and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1` ASan/UBSan 368/368. No schema bump; `git diff --check` clean.

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

### B3.12 `[x]` Multi-monster combat + encounter framework
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
**Log:** Done 2026-07-23. Shipped the FRAMEWORK (individual monsters land in
B3.13-B3.22); JAW_WORM is the only spawnable monster today, so combat_begin
still fields a single Jaw Worm — but through the generalized path.
- **encounters.yaml** rebuilt from an empty stub into the Act-1 Exordium data:
  20 encounters (4 weak / 10 strong / 3 elite / 3 boss) with pool + weight +
  WEAK exclusions + a data-driven miscRng COMPOSITION PROGRAM each. New codegen
  `emit_encounter_table` (gen.py) -> generated `encounter_table.hpp`
  (EncounterPool/CompOp enums, CompStep/EncounterDef, kEncounters,
  encounter_by_game_id). Composition op set EMIT/BOOL/PICK/SEQ_BOOL/POOL covers
  every S1 shape; monster refs are the game's AbstractMonster.ID strings
  (verified per class), join keys to monsters.yaml as those land later.
- **engine** `encounters.{hpp,cpp}`: `resolve_composition` (miscRng) +
  `generate_monster_lists` (monsterRng: stable ascending-weight roll TRAP 1,
  populateMonsterList no-repeat/no-A-B-A, populateFirstStrongEnemy exclusion
  loop, boss jdk_shuffle). `monster_dispatch.{hpp,cpp}`: per-MonsterId
  init/turn tables + `dispatch_monster_turn` (generalized MonsterTurnFn seam) +
  `spawn_group` (HP roll in spawn order). `combat_begin`/`advance` now spawn via
  spawn_group([JAW_WORM]) and pump via dispatch_monster_turn -- byte-identical to
  the old single-Jaw-Worm path (all 20 fixtures replay unchanged).
- **targeting**: ActionMask gained `can_play_target[hand_slot][target]`
  (enemy-target cards x live monster slots; dead/absent slots excluded);
  `can_play[i]` keeps its affordability meaning (additive, zero-risk to prior
  tests).
- **schema bump 3->4**: kMonsterCap 5->7 (dead-in-place records for a fully-split
  Slime Boss; scoping §1.5/§6). sizeof(CombatState) 3672->3896 (<= 4096, 200 B
  margin -- budget HOLDS). kMonsterQueueCap stays 5 (max S1 alive = 5).
  kObsMonsterCap tracks kMonsterCap so ObsBuffer 188->240. 20 combat fixtures
  REGENERATED via the checked-in generator; zero-diff-in-meaning proof
  (scratchpad/b312_fixture_proof.py, B4.3 precedent) PASSED over 20 fixtures /
  111 records: header schema_version stays v1, record_count/seed/action/aux
  unchanged, only 224 zero bytes (2 MonsterState slots) inserted per record in
  the monsters[] region -- every pre-existing byte preserved in order.
- **CORRECTION to the scoping report (§1.4)**: bottomGetWeakWildlife /
  bottomGetStrongHumanoid construct getLouse/getSlaver UNCONDITIONALLY during
  ArrayList build (MonsterHelper.java:801-822), so the louse/slaver coin ALWAYS
  fires BEFORE the random(0,n) select -- not "only if the louse index is
  picked". The PICK op models this eager construction; tests pin the exact draw
  order.
- **tests** `encounters_test.cpp` (13, all green): DIFFERENTIAL composition
  tests hand-derive each miscRng draw sequence (louse variants, gremlin/lots-of-
  slimes draw-without-replacement order, small/large slime mixes, Exordium
  Thugs/Wildlife eager-PICK order) vs the resolver, pinning draw ORDER + count;
  spawn_group HP rolls consume monsterHpRng in spawn order; the target grid over
  a dead middle slot; pool-draw determinism + shape (16/10/3) + no-repeat/no-
  A-B-A + membership + exclusion honored + boss permutation.
- Suite 286/286 debug+asan+release (273 baseline + 13). registry_gen manifest
  counts updated (encounters 0->20, total 91->111).
- **Deferred / honest gaps**: (a) a BIT-EXACT oracle for the raw monsterRng
  monster/elite/boss lists needs the B4 dungeon-construction wiring (monsterRng
  seed derivation) + a captured run; B3.12 pins the algorithm (Java-verified
  draw order) + structural invariants + determinism, not a golden list. (b) G4's
  live corpus can cross-check compositions per-fight but most target monsters
  are unimplemented pre-B3.13 and the corpus is uncommitted (non-CI); the
  Java-verified differential test is the committed check. (c) usePreBattleAction
  (a later monsterHpRng phase, e.g. Louse curl-up) is a spawn seam for B3.13 --
  no B3.12 monster has one.

### B3.13 `[x]` ∥ Monsters: Cultist + louses
**Deps:** B3.12 · **Provenance:** Cultist.java (:59/66/95 A-branches),
LouseNormal.java (:55-68/95/130), LouseDefensive.java
**Deliverables:** registry entries (A2/A7/A17 columns cited per branch):
Cultist (Ritual), LouseNormal/Defensive (Curl Up, bite damage roll at spawn
— note the per-instance damage roll's stream and timing, read at task).
**Acceptance:** tier-2 per monster: move tables vs. hand-derived aiRng
sequences (A3.2 fixture pattern); Curl Up block trigger on first attack
damage.
**Log:** Done 2026-07-23. Appended Cultist (monster 2), LouseNormal
(monster 3), LouseDefensive (monster 4), and Curl Up (power 20) without
renumbering IDs/opcodes or changing the fixed state layout. Generated monster
roll metadata now records inclusive tier ranges plus RNG stream/timing: louse
bite is `monsterHpRng` during construction after HP (base 5–7, A2 6–8), and
Curl Up is the later pre-battle draw (base 3–7, A7 4–8, A17 9–12).
Cultist Ritual is 3/4/5 at base/A2/A17 and preserves its first-round skip.
Native turn/pre-battle hooks implement both louse move tables, rolled bite
damage, Strength/Weak effects, and Curl Up's synchronous one-shot latch before
queued block/removal (including queued multi-hit, fully blocked, and lethal
guards). Committed independent Python XS128 fixtures cover 32 seeds × 20 turns
for Cultist and both louses, asserting every move and exact RNG state/counter;
registry tests pin all A2/A7/A17 columns and stream/timing metadata. Provenance:
`Cultist.java:57-67,82-108,145-153`,
`LouseNormal.java:50-73,75-104,128-153`,
`LouseDefensive.java:53-76,78-107,131-156`,
`RitualPower.java:19-55`, `CurlUpPower.java:25,36-46`, and
`AbstractMonster.java:431-491,622-678`. Verification: focused
Cultist/Louse/RegistryGen 29/29; full Debug 359/359; full ASan 359/359.

### B3.14 `[x]` ∥ Monsters: small/medium slimes
**Deps:** B3.12 · **Provenance:** SpikeSlime_S/M.java, AcidSlime_S/M.java
**Deliverables:** registry entries with A-columns; Slimed-card attacks
(status insertion), Frail application (Spike M), lick debuffs.
**Acceptance:** tier-2 per monster; status cards land in discard per the
cited actions; aiRng draw counts per turn match hand-derivation.
**Log:** Done 2026-07-23. Appended `MonsterId` 5–8 for Spike Slime S/M and
Acid Slime S/M plus `MonsterIntent::ATTACK_DEBUFF` 6; no power or opcode IDs
were added or renumbered. Registry A2/A7 columns pin HP and damage, and the
generator now validates monster-authored `MAKE_CARD` card/pile operands while
packing the existing opcode's `CardId | (CardPile << 16)` metadata. Native A20
AI preserves each decompiled history branch and exact RNG API: all constructors
draw HP once, all initial rolls consume one `aiRng.random(99)`, Spike S ignores
that value, Acid S alternates directly with no later AI draws, and Acid M keeps
its conditional `nextBoolean`/`nextFloat` tie-break draws. Medium tackles queue
damage before a fresh Slimed in discard; licks apply Frail 1 or Weak 1, including
Frail's live just-applied latch. Small/Lots of Slimes encounter compositions now
map through the registry to implemented dispatch hooks. Independent XS128
fixtures cover all four classes at 32 seeds × 20 turns, checking HP plus every
move, full AI state, and counters; regeneration preserved all four SHA-256s
byte-for-byte. Provenance: `SpikeSlime_S.java:42-76`,
`SpikeSlime_M.java:51-117`, `AcidSlime_S.java:45-95`,
`AcidSlime_M.java:56-168`, `AbstractMonster.java:431-491,712-715,765-775`,
`MakeTempCardInDiscardAction.java:24-50`, `FrailPower.java:25-54`, and
`WeakPower.java:27-60`. Verification: focused Slime 7/7 and RegistryGen 15/15;
full Debug 395/395; leak-detecting ASan/UBSan 395/395; Release 395/395.
After integration on top of B3.5, the combined manifest is cards 50 / powers 21 /
monsters 8 / relics 34 / potions 33 / encounters 20 / total 166; focused Slime
7/7 and RegistryGen 15/15 remained green, followed by full integrated Debug
413/413 and leak-detecting ASan/UBSan 413/413.

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

### B3.17 `[x]` ∥ Monsters: large slimes + split
**Deps:** B3.14 · **Provenance:** AcidSlime_L.java, SpikeSlime_L.java (split
at ≤ half HP), SlimeBoss split machinery shared reading
**Deliverables:** split framework (mid-combat monster replacement: L →
2×M at current HP, position/queue semantics per the cited actions) + the two
L-slime entries with A-columns.
**Acceptance:** tier-2: split triggers at the exact threshold, children HP =
parent current HP, intents/queue state after split match the cited Java;
split during the monster's own turn vs. player turn both covered.
**Log:** Done 2026-07-24. Appended `MonsterId` 9 SPIKE_SLIME_LARGE / 10
ACID_SLIME_LARGE, `MonsterIntent::UNKNOWN` 7, `PowerId` 22 SPLIT (the ctor's
display-only marker, amount -1), and opcodes 25-29 CANNOT_LOSE / CAN_LOSE /
SUICIDE / SPAWN_MONSTER / SET_MOVE -- nothing renumbered, NO schema/layout
change (the cannotLose latch is CombatState.flags bit 1 and splitTriggered is
MonsterState.flags 0x0004, both reserved bitfields per the Frail/Curl Up
precedent; kMonsterCap 7 was already sized for splits at B3.12).
- **Split semantics (Java-exact):** the damage() override seam fires from
  op_damage AND op_lose_hp (LoseHPAction routes through damage()) AFTER the hit
  lands, for EVERY DamageInfo type; trigger iff alive && 2*hp <= max_hp (the
  float `(float)cur <= (float)max/2.0f` is integer-exact) && nextMove != 3 &&
  !splitTriggered -> synchronous setMove(SPLIT, UNKNOWN) + a bottom-queued
  SetMoveAction + one-shot latch (AcidSlime_L.java:142-152 / SpikeSlime_L.java:
  130-140). The split turn queues CannotLose -> Suicide(this,false) (hp=0, NO
  relic triggers, block untouched) -> 2x SpawnMonsterAction -> CanLose;
  children are the UNCHANGED B3.14 mediums, constructed at takeTurn time at the
  parent's CURRENT hp (= their max, 4-arg ctor, NO monsterHpRng draw) with
  init()'s single aiRng roll at SPAWN resolve time, in queue order. Slots per
  smart positioning over the S1 solo layout (saveX∓134, MonsterHelper.java:
  409-414): left child AT the parent's slot, dead parent shifts to +1
  (dead-in-place record), right child at +2; monster_queue indices remap on
  insert, later-queued actions pre-compute post-insert slots (B3.20's boss
  coords need their own index derivation). Spike L's unconditional trailing
  RollMoveAction (:127) rolls POSTHUMOUSLY on the dead parent (one wasted aiRng
  draw, pre-computed slot mi+1); Acid L's split case queues no roll. Own-turn
  interrupts resolve RollMove-then-SetMove (the wasted roll draws first), and
  the pump's victory gate honors the cannotLose window, so combat ends only
  when all descendants are dead.
- **ROLL_MOVE (opcode 8) is now real:** it dispatches to a per-monster
  queued-roll fn (large slimes only); inline-rolling monsters keep it a strict
  no-op (damage_pipeline pin updated). Registry rows carry base/A2/A7/A17
  columns (Spike L 64-70/67-73 hp, 16/18 tackle, Frail 2/3; Acid L 65-69/68-72
  hp, 11/12 + 16/18 tackles, Weak 2; 2 Slimed per tackle); the SPLIT move is a
  NOP-placeholder row (Louse bite precedent) with the sequence native in
  monster_slime_large.cpp.
- **Fixtures:** two new committed independent XS128 corpora (32 seeds x 20
  turns) for Spike/Acid L incl. Acid L's 0.6f/0.4f nextFloat tiebreaks (double
  vs float threshold proven boolean-equivalent); regeneration preserved all
  four B3.14 slime fixture SHA-256s byte-for-byte.
- Provenance: `AcidSlime_L.java:73-215`, `SpikeSlime_L.java:68-181`,
  `SlimeBoss.java:149-157,171-179` (shared reading),
  `SpawnMonsterAction.java:28-73`, `SuicideAction.java:21-36`,
  `CannotLoseAction.java/CanLoseAction.java:12-15`, `RollMoveAction.java:17-21`,
  `SetMoveAction.java:52-56`, `SplitPower.java:12-32`, `LoseHPAction.java:41`,
  `AbstractMonster.java:139,150,431-437,465-467,712-715,869,925-951`,
  `MonsterGroup.java:35-40,90-122`.
- Verification: focused LargeSlime 12/12, Slime 7/7 (B3.14 unregressed),
  RegistryGen 17/17; full WSL Debug 441/441; leak-detecting ASan/UBSan 441/441
  (zero diagnostics); Release 441/441. Manifest now cards 50 / powers 22 /
  monsters 10 / relics 35 / potions 33 / encounters 20 / total 170.

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

### B3.23 `[x]` ∥ Potions
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
**Log:** Verified by running in an ISOLATED worktree at f0617c5 (clean of the
concurrent B3.4/B3.24 in-flight edits in the shared tree): **debug 232/232, asan
232/232** (213 baseline + 19 new potion cases; generator determinism green).
Every potion's use()/getPotency read in full before coding.
**Registry.** `potions.yaml` = the 33 Ironclad-obtainable potions
(getPotions(IRONCLAD,false): 3 Ironclad-specific + 30 shared; the Silent/Defect/
Watcher class potions are NOT in-pool), ids 1..33 in pool order so the identity-
roll index maps 1:1. Potion potency is **ascension-independent** (every getPotency
override returns a constant), so potency@A20 == base -> flat `potency` field (no
tier columns). `gen.py` emits `potion_table.hpp` (`PotionDef` id/rarity/native/
potency + the reused `CardEffectStep` USE program; sorted, deterministic) per the
power_table precedent; `PotionRarity` pinned/append-only; manifest kPotionsCount
0->33.
**Engine (new module `potions.hpp`/`potions.cpp`).** `use_potion()` queues a
potion's USE effect program onto the action queue (identical translation to
card_play's queue_effect_step), or routes a `native` potion to
`dispatch_native_potion`. `PotionId` is aliased into sts::engine here (types.hpp
never did). **10 DATA potions** (opcode set + an already-registered power suffice
-- Metallicize/Strength/Weak/Vulnerable/Artifact) run end-to-end (queue->pump):
HeartOfIron, Block, Energy, Explosive, Fire, Strength, Swift, Weak, Fear, Ancient.
**BloodPotion** percent-heal is a native body (float floor of maxHP*potency/100,
clamped). Each checked at tier-2 (effect + potency@A20).
**Trap 14** (AbstractDungeon.java:829-850): `return_random_potion()` = the
65/25/10 tier roll (`potion_tier_for_roll`) then the rejection loop
(getRandomPotion until the rolled rarity matches), consuming a VARIABLE number of
potionRng draws. The test hand-derives the exact draw sequence over an identical
stream and asserts same PotionId AND same draw count (rng.counter delta), 10 seeds
across all tiers.
**A11 seam (design §5.4).** `potion_slot_count(asc)` = 3, -1 at A11+ (A20->2) as
a **pure function** -- the RunState potion-slot FIELD is **B4.3's** (schema v2);
this task's combat-side USE mechanics do NOT touch RunState/CombatState layout and
do not depend on that field landing. Run-level USE_POTION/discard action wiring is
**B4.4** ("USE_POTION both layers"); a potion discard has no combat effect (pure
slot removal), so use_potion is the only combat verb.
**DEFERRED (per hygiene; registry rows complete now -- rarity/potency/native flag
tested; runtime bodies land with their dependency).** (1) **BLOCKED on powers.yaml
(B3.4, not ownable concurrently):** potions granting powers not yet registered --
Dexterity, Steroid (Strength+StrengthDown), Speed (Dex+LoseDex), Regen,
LiquidBronze (Thorns), EssenceOfSteel (PlatedArmor), Duplication, Cultist
(Ritual). These cannot even name a PowerId today; B3.23 Deps lists only B3.2 but
the effect-side functionally needs these powers, and design §5.5 assigns "potion-
granted powers" to per-batch extraction. **Orchestrator ruling (APPROVED):** the
native+deferred approach stands; a queued follow-up task **"potion-support
powers"** runs immediately after B3.4 lands -- it appends the missing PowerId rows
(continuing from B3.4's allocation, 13+), wires their hook bodies, and **un-defers
these potion USE bodies with their tier-2 effect tests**. The potion rows are
complete here (rarity/potency/native flag tested); only the runtime effect awaits
that follow-up, which is the traceable owner of the un-deferral. (2)
verb-owned elsewhere: in-combat card CHOOSE = B3.4 (Elixir, Attack/Skill/Power/
Colorless, GamblersBrew, LiquidMemories, BlessingOfTheForge); recursive play =
later opcode (DistilledChaos); cost randomization (SneckoOil); run-layer mutation
= B4.x (FruitJuice max-HP, EntropicBrew slot-fill); combat escape = B3.15
(SmokeBomb, flagged); out-of-combat revive (FairyPotion, flagged). (3) Fire's
THORNS/applyEnemyPowersOnly typing rides **B3.2's deferred DAMAGE damage-TYPE**
item (NORMAL today; coincides on the number when the player has no Strength -- the
tier-2 test's condition). **gen.py note:** the potion-emission section
(POTION_RARITIES + emit_potion_table + generate() wiring) is self-contained and
sits between emit_power_table and the Monster table section. **Shared-file flag:**
tests/registry_gen_test.cpp kTotalCount/kGenFiles-size are cross-agent sums --
reconcile at serialize (my delta: +33 potions, +potion_table.hpp).
**DISCHARGED (potion-support-powers follow-up):** deferral (1) is cleared --
powers.yaml ids 14-19 (Dexterity/LoseDexterity/Thorns/PlatedArmor/Regen/Ritual)
registered, and Dexterity/Speed/Steroid (Strength+LoseStrength id 13)/Regen/Liquid
Bronze/Essence of Steel/Cultist re-authored as DATA APPLY_POWER programs with
tier-2 effect tests. Duplication STAYS deferred, but on the recursive-play opcode
(not powers.yaml). Fire Potion's applyEnemyPowersOnly typing remains its own item.

### B3.24 `[x]` ∥ Relics: starter + commons
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
**Log:** Verified by running (WSL Ubuntu-2404), not inferred — debug **250/250**,
asan **250/250** (232 baseline incl. the concurrent B3.23 potions + 18 new relic
cases), rebased onto master `6c5f7f4`. Every relic body read in full in the
decompiled Java before coding (per-row provenance in `registry/relics.yaml`).
**Batch (34 = starter + 33 commons):** enumerated from
RelicLibrary.initialize() (shared `add()` + `addRed()`, :231-382), filtered to
RelicTier.COMMON per-file; color-gated commons EXCLUDED as non-Ironclad
(Damaru→addPurple, DataDisk→addBlue, SneckoSkull→addGreen; Test5 unused), Red
Skull the only RED common. `game_id` join keys are the AbstractRelic `ID`
strings (some differ from display: `Boot`, `CeramicFish`, `MawBank`,
`MealTicket`, `PreservedInsect`).
**Registry + codegen.** `registry/relics.yaml` defines the entry schema following
the powers.yaml `hooks:`/`native:` precedent (id/name/game_id/tier/native/hooks +
provenance). `gen.py` emits `relic_table.hpp` (`RelicTier`+`RelicHook` enums with
pinned static_asserts, `RelicDef`/`RelicHookBinding`/`relic_def()`, deterministic/
sorted, mirroring the `power_table.hpp` pattern); the `RelicId` enum + game_id
tables come from the existing id/game_id emission. Determinism + standalone-compile
tests now cover `relic_table.hpp`. Ids append-only 1..34 from Burning Blood.
**Framework.** `include/sts/engine/relic_hooks.hpp` (a DISTINCT `RelicHook` enum —
relics carry battle-start/turn-start/end-turn/victory hooks powers do not,
AbstractRelic.java:492-620) + `src/engine/relic_hooks.cpp` dispatch relics in
**ACQUISITION ORDER** (relic-list index order, trap 8), each either DATA (Anchor
BLOCK 10, Bag of Marbles Vulnerable-all, Bag of Preparation DRAW 2, Vajra Strength
1) or NATIVE (Burning Blood/Blood Vial heal, Centennial Puzzle first-HP-loss draw,
Orichalcum unblocked-block, Red Skull bloodied +3 Strength, and the counter relics
Nunchaku/Pen Nib/Happy Flower/Lantern whose counter persists in the RelicSlot
`{relic_id, counter}`). `include/sts/engine/relics.hpp` re-exports the generated
table and pins `RelicHook` byte-equal to the engine's.
**Wiring + the B4.3 relic-storage seam.** `power_hooks.cpp` (onPlayCard/onUseCard/
onExhaust/onGainedBlock) and `action_queue.cpp` (applyEndOfTurnRelics
onPlayerEndTurn / applyStartOfTurnRelics atTurnStart) call the relic dispatchers at
the exact structural sites B3.2 left, in the frozen relic-vs-power interleave,
reading `player_relics(s)`. **CombatState has no relic mirror** (adding one is a
schema-bump additive field owned by B4.3), so `player_relics()` returns an EMPTY
view and every wired site is a pure **no-op today** → zero CombatState/schema
change, `SCHEMA_VERSION` untouched, the 20 combat fixtures byte-identical
(`fixture_oracle` green). A one-line `TODO(B4.3)` returns `{s.relics, s.relic_count}`
once B4.3 lands the mirror. Acquisition-order + per-relic combat behaviour are
proven by `relic_hooks_test.cpp` constructing relic lists directly.
**G4:** `relic:Burning Blood` now resolves in **strict-mode** translation
(`join_relic` → `relic_from_game_id`, translate.cpp:168), clearing it from the
94-unknown-id tolerant tally (738 live-corpus hits).
**Deferred (documented per hygiene; un-deferral owners):** power-granting relics
whose power row is not yet registered — **Bronze Scales (Thorns) / Oddly Smooth
Stone (Dexterity)** land with the potion-support-powers follow-up (powers.yaml ids
14+, right after B3.4); **Akabeko (Vigor)** and **Pen Nib (double-damage
PenNibPower)** land with their card-batch consumers (Pen Nib's attack counter is
already live). *(B3.4 next takes powers.yaml id 13 = LOSE_STRENGTH.)* Also
deferred: **Boot** (a DAMAGE-pipeline `onAttackToChangeDamage` modifier — keeps the
frozen float-exact pipeline untouched); **Red Skull onNotBloodied** −3 heal-cross
(needs a heal-cross hook); **Art of War / Ancient Tea Set** (cross-turn/cross-room
energy flags beyond `RelicSlot.counter`); **Preserved Insect** (elite-room HP
scaling); **Toy Ornithopter** (potion-use trigger, B3.23). Each is a documented
no-op native branch — the relic row + hook are registered so the accounting/wiring
is already in place.
**DISCHARGED (potion-support-powers follow-up):** Bronze Scales (Thorns 3) and
Oddly Smooth Stone (Dexterity 1) un-deferred -- their power rows landed (powers.yaml
ids 16/14), so both are now DATA at_battle_start APPLY_POWER relics (mirroring
Vajra), dropped from the deferred native switch, with tier-2 battle-start tests.
Akabeko (Vigor) / Pen Nib / Boot / the energy-flag + HP-scale relics stay deferred.

### B3.25 `[x]` ∥ Relics: uncommons
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
**Log:** Done 2026-07-24 on base `01d085a`. Verified by running (WSL
Ubuntu-2404), not inferred — **debug 454/454, leak-detecting ASan/UBSan
454/454, release 454/454**. Every relic body read in full in the decompiled
Java before coding (per-row provenance in `registry/relics.yaml`).
**Batch (30 = 28 shared + 2 red), ids 36-65 (append-only after Circlet=35):**
enumerated from the constructor tiers (`super(..., RelicTier.UNCOMMON, ...)`)
of every relic registered by RelicLibrary.initialize(). EXCLUDED as
non-Ironclad-obtainable, with filter evidence: NinjaScroll + PaperCrane
(addGreen, RelicLibrary.java:370-371), GoldPlatedCables + SymbioticVirus
(addBlue, :395/:399), TeardropLocket + Duality (addPurple, :407-408);
Test1/Test6/DiscerningMonocle (UNCOMMON ctors, never registered in
initialize()); DEPRECATEDYin/DEPRECATEDDodecahedron (deprecated pkg, never
registered). PaperFrog + SelfFormingClay are the only RED uncommons
(addRed, :387/:390). `game_id` join keys are the AbstractRelic ID strings
(display-name traps: `HornCleat`, `InkBottle`, `StrikeDummy`, `The Courier`,
`Paper Frog`, `Frozen/Molten/Toxic Egg 2`, `White Beast Statue`,
`Self Forming Clay`).
**Pool order (B4.6 compatibility).** Canonical pre-shuffle UNCOMMON
`pool_order` 0..29 derived by INVERTING the JDK Fisher-Yates (relicRng draw
#2) against the live b14_accept `oracle.relicPools.uncommon` captures for
seeds 1790050543751..1790050543753 — all three seeds invert to ONE
pre-shuffle order (three-way agreement is the correctness proof; the same
tooling replicated the B4.6 common order first as a control). A new oracle
test pins all three shuffled uncommon orders; **B4.6's three-seed common
pins and the post-five-draw relicRng (s0,s1,counter) triples are GREEN
UNCHANGED** (the five draws are unconditional, so populating a tier cannot
move them) — only the scaffold "tiers 1-4 empty" placeholder assertions
(marked "belongs to B3.25-B3.27") narrowed to tiers 2-4.
**Live combat relics.** Native: Kunai/Shuriken/Ornamental Fan (per-turn
ATTACK cadence ×3 → Dex/Str/4-block), Letter Opener (SKILL ×3 → 5 THORNS
AoE), Ink Bottle (any-card ×10 → draw 1; counter persists — no onVictory
reset), Horn Cleat (turn 2 → 14 block, once per combat; counter<0 encodes
the grayscale latch), Sundial (every 3rd reshuffle → 2 energy; persists),
Gremlin Horn (new `on_monster_death` hook 14, wired at the op_damage/
op_lose_hp death edge with the !areMonstersBasicallyDead guard), Blue
Candle (curse playability in legal_actions per AbstractCard.canUse:920;
on_use_card CURSE → LoseHP 1 + instance-EXHAUST redirect; resolve_card_play
now guards effect programs on trigger==ON_PLAY so a candle-played curse
runs no trigger program), Self-Forming Clay (wasHPLost → Next Turn Block 3
addToTop; **powers.yaml id 23 NEXT_TURN_BLOCK** (22 at branch time,
renumbered at integration — B3.17's SPLIT landed first as 22) native at_start_of_turn
BLOCK+self-remove). DATA: Mercury Hourglass (at_turn_start DAMAGE 3
ALL_ENEMY THORNS — gen.py relic steps now accept `damage_type`, mirroring
the card-step encoding). Bespoke-site rows (no hook bindings): **Paper
Phrog** — the VulnerablePower.atDamageReceive ×1.75 monster branch
(VulnerablePower.java:67-69) is live in the frozen pipeline's
at_damage_receive keyed on player_has_relic; the stage-a A4.1 "unreachable"
inline note (interp.hpp provenance block) RETIRED in this commit (Odd
Mushroom ×1.25 stays with B3.26; Paper Crane stays unreachable —
Silent-only). **Strike Dummy** — atDamageModify +3 baked at queue time for
CardDef.is_strike DAMAGE steps (AbstractCard.java:2229-2237 runs relic
atDamageModify on float(baseDamage) BEFORE power atDamageGive; int base+3
is float-exact). **Meat on the Bone** — bespoke pre-victory site
(AbstractRoom.endBattle:418-420 fires BEFORE player.onVictory, so before
Burning Blood regardless of acquisition order): apply_meat_on_the_bone_
pre_victory, wired in run_advance end_combat.
**New hook plumbing (append-only):** RelicHook ON_MONSTER_DEATH=14 +
ON_SHUFFLE=15 (kRelicHookCount 16; gen.py + relics.hpp pins extended);
onShuffle wired in piles.cpp shuffle_discard_into_draw (EmptyDeckShuffle
ctor timing — before the shuffle draw, only on a real reshuffle); relic
wasHPLost wired into power_hooks dispatch_was_hp_lost AFTER player powers
(AbstractPlayer.damage:1445-1449) — makes B3.24's Centennial Puzzle/Red
Skull live too; every new site is a no-op with an empty mirror, so the 20
combat fixtures stay byte-identical (fixture_oracle green, SCHEMA_VERSION
untouched).
**Run layer.** acquire_relic: Pear (+10 max/+10 cur, increaseMaxHp(10,true)).
New `run_deck.hpp add_card_to_master_deck` (the onObtainCard door): Molten/
Toxic Egg upgrade an un-upgraded ATTACK/SKILL on obtain, Darkstone Periapt
pays +6 max/+6 cur on a CURSE obtain; Frozen Egg's POWER branch documented
inert (no POWER CardType until B3.7). canSpawn gates: floor≤48 family
(Darkstone/FrozenEgg/MeatOnTheBone/MoltenEgg/QuestionCard/SingingBowl/
ToxicEgg), The Courier floor≤48 && !in_shop, Matryoshka floor≤40, and the
Bottled trio's deck-content gates (RelicSpawnContext.deck_has_* +
fill_deck_spawn_gates; checked BEFORE the endless bypass — the Java bottled
canSpawn has no isEndless clause; non-basic == not Strike/Defend/Bash, the
only CardRarity.BASIC red rows).
**Deferred (documented per hygiene; un-deferral owners):** Mummified Hand
(onUseCard POWER → cardRandomRng 0-cost — no POWER CardType until **B3.7**);
Pantograph (atBattleStart boss heal 25 — no EnemyType/BOSS monster metadata
until **B3.15-B3.17**); Bottled trio bottling at acquisition (needs run-layer
acquisition-choice machinery + a per-master-deck-instance innate flag —
B4-owner; rows/gates live so pools and B4.7 chests are complete); Eternal
Feather (rest-room heal, **B4.9**); Question Card/Singing Bowl/White Beast
Statue (reward-screen modifiers, **B4.5**); The Courier (shop, **B4.8**);
Matryoshka (chests, **B4.7**). Each is a registered row with a documented
no-op (native branch or run-layer note) so accounting/wiring is in place.
**Manifest** (integrated, after B3.17): cards 50 / powers 23
(+NEXT_TURN_BLOCK=23) / monsters 10 / relics 65 (+30) / potions 33 /
encounters 20 / **total 201**. Focused: RelicHooks +
RelicHooksUncommon, RelicPools (incl. the new uncommon three-seed oracle),
RelicAcquisition, RunDeck, PaperPhrog, RegistryGen — 82/82.

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

### B4.1 `[x]` Map path generation
**Deps:** G4, G5 · **Spec:** design §5.6 · **Provenance:**
MapGenerator.java:23, 62-77, 157-190, 270-276; AbstractDungeon.java:510-540
**Deliverables:** path generation into `RunState`'s 7×15 node grid
(mapRng-driven: 6 walks, first-two-distinct rule, ancestor-gap re-rolls,
edge dedup) — exact draw order per the cited lines.
**Acceptance:** gtest: for ≥ 3 seeds, generated edges match the **oracle's
dumped map** node-for-node (bridge artifacts from B1.4 — this is the first
run-layer bit-exactness proof); mapRng draw count per generation matches
the hand-derived count for one seed.
**Log:** Verified by running (WSL Ubuntu-2404), isolated from a concurrent
B3.1 dirty tree via a detached worktree at HEAD `e2e4f6e`. Delivered
header-only `include/sts/engine/map_gen.hpp` re-expressing MapGenerator path
generation bit-for-bit on `mapRng`, in GAME semantics (15 floors × 7 cols),
with a documented game-oriented index adapter (`run_state_map_index`,
floor-major) onto the current transposed 7×15 storage so B4.3's rename is
mechanical (room_type left untouched — B4.2). Pipeline: createNodes (no RNG) →
createPaths (6 walks, first-two-distinct walk-1 re-roll) → 6× _createPaths
random walk (per-floor primary `randRange` draw + conditional per-parent
ancestor-gap re-rolls + no-RNG sibling clamp + boss terminator) →
filterRedundantEdgesFromRow (row-0 dedup). Provenance (each read in full):
MapGenerator.java:23-28,50-77,111-131,133-211,270-276; MapRoomNode.java:75-84,
149-151; MapEdge.java:71-90; AbstractDungeon.java:510-540.
Hazards honoured: H1 (MathUtils global RNG in node/edge ctors NOT modelled);
H4 (edge lists stable-sorted by MapEdge.compareTo); parents appended WITH
duplicates and addParent called unconditionally (even on de-duped edges).
**H5 replicated verbatim** — getCommonAncestor's :116 bug `node1.x < node2.y`
(should be `node2.x`) — PROVEN exercised (ancestor re-roll fires 1..12×/seed
on the corpus) and PROVEN load-bearing (a counterfactual "fixed" comparison
diverges ≥1 corpus seed).
**Acceptance — gtest `map_gen_test`, 8 cases over the 20-seed A20-Ironclad
live-oracle corpus `b13_on20b` (fork 04477E4E)**, curated to
`tests/golden/map_paths/oracle_maps.txt` (derived golden vector, not a raw
artifact): (1) **generated edges match the oracle's dumped map NODE-FOR-NODE
for all 20 seeds** (`EdgesMatchOracleNodeForNode`; ledger asked ≥3) — first
run-layer bit-exactness proof; (2) **path-gen mapRng draw count == hand-derived
count** — STS00001 decomposes as 6 walk seeds + 84 primaries (14×6) + 3
ancestor re-rolls = 93 (`HandDerivedPathDrawCountSingleSeed`), and across all
20 seeds the path-gen counter == oracle_counter − 1 exactly
(`PathGenCounterMatchesOracleMinusEmerald`); (3) boss row-14→(3,16) edge case +
RunState edge-bitfield round-trip. Full suite green in the HEAD-isolated
worktree: **debug 174/174, asan 174/174, release 174/174** (166 baseline + 8
new; header-only — no engine `.cpp` changed, `sizeof(RunState)` /
`SCHEMA_VERSION` untouched).
**STOP-THE-LINE finding (live oracle > design docs):** across ALL 20 seeds the
oracle's post-`generateMap` `mapRng.counter` is EXACTLY path-gen + 1,
independent of the varying re-roll counts (1..12). The +1 is **`setEmeraldElite`
(AbstractDungeon.java:539,551) firing** — its guard `Settings.isFinalActAvailable
&& !hasEmeraldKey` (:543) PASSES because `isFinalActAvailable = IRONCLAD_WIN &&
SILENT_WIN && DEFECT_WIN && …` (Settings.java:642) is TRUE on the frozen
fully-unlocked profile (design §1.1:45-49) and no emerald key exists at act-1
start. This **overturns design §1.1:43** ("`setEmeraldElite` is likewise
final-act-gated … never fires in S1") and the scoping report's H6/R3.
RoomTypeAssigner's only mapRng use is the trap-12 raw `Collections.shuffle`
(no counter advance). **B4.1 is unaffected** (setEmeraldElite runs
post-room-assignment, outside path-gen). **B4.2 MUST model the setEmeraldElite
draw** (one `mapRng.random(0, eliteNodes.size()-1)` after the shuffle) to hit
the full {counter,s0,s1} oracle match; its counter target = path-gen + 0
(shuffle) + 1 (emerald). **Design §1.1:43 + a §11 change-log entry need
correction** — flagged for orchestrator sequencing (frozen shared doc not
edited unilaterally mid-parallel-work).

### B4.2 `[x]` Room-type assignment
**Deps:** B4.1 · **Spec:** design §5.6; §10 trap 12 · **Provenance:**
AbstractDungeon.java:558-594, 571-573; RoomTypeAssigner.java:65-143
**Deliverables:** quota computation (incl. A1 elite ×1.6), the trap-12
direct-XS128 `Collections.shuffle`, assignment rules (row gates, parent/
sibling exclusions, monster fill), fixed rows 0/8/14.
**Acceptance:** gtest: room layouts match oracle dumps for the B4.1 seeds;
trap-12 named test (counter unchanged across the shuffle; permutation matches
golden category 7 from the A0.1 harness extension — extend the capture
harness in this commit); quota table tested at A0 vs A20 (1.6× elites).
*(Amended 2026-07-23 — trap-12 permutation verified against the live oracle
instead: 20-seed room-symbol match + post-generateMap {counter,s0,s1} triple
match jointly pin the shuffle permutation and draw count; strictly stronger
than an isolated synthetic capture and WSL-CI-runnable, whereas the A0.1
harness is Windows-host/CI-excluded. Standalone golden category 7 remains
available as optional future work if shuffle debugging ever needs isolation.)*
**Log:** Verified by running (WSL Ubuntu-2404), not inferred. Delivered
header-only `include/sts/engine/map_rooms.hpp` re-expressing the tail of
`AbstractDungeon.generateMap` (everything after B4.1 path-gen) bit-for-bit, in
GAME semantics (15 floors × 7 cols, row-major y-outer/x-inner exactly as the
map ArrayList is iterated). Consumes a B4.1 `GeneratedMap` at its post-path
`mapRng` state and produces the per-node `RoomType` grid + the end-of-generateMap
`mapRng`. Pipeline: availableRoomCount (edge nodes, `y != map.size()-2`=13, H7) →
generateRoomTypes quotas (shop .05 / rest .12 / treasure 0 / event .22 / elite
.08, **elite ×1.6 at ascension≥1**; roomList `[Shop,Rest,Elite,Event]`) →
fixed rows 14=Rest / 0=Monster / 8=Treasure → distributeRoomsAcrossMap
(nodeCount incl. row 13, monster padding, **trap-12 shuffle**, row-major
assignment via ruleAssignableToRow / ruleParentMatches / ruleSiblingMatches +
row-0 override, lastMinuteNodeChecker monster fill) → **setEmeraldElite draw**.
Provenance (each read in full): AbstractDungeon.java:510-540,558-594,542-556;
RoomTypeAssigner.java:30-143; Exordium.java:95-99; MonsterRoom/EventRoom/
MonsterRoomElite/RestRoom/ShopRoom/TreasureRoom/MonsterRoomBoss mapSymbols.
**Hazards honoured — Trap 12** (`RoomTypeAssigner.java:135`): `xs128_room_shuffle`
runs JDK `Collections.shuffle` Fisher-Yates (`for i=size..2: swap(i-1,
nextInt(i))`) on the **raw RandomXS128** off `mapRng`'s (s0,s1) — raw state
advances by exactly `size-1` next_long draws, wrapper `counter` does NOT advance;
never routed through the JDK-LCG deck/pool path (`rng_jdk.hpp`). **H2**:
`java_round_f` is a bit-exact **Java 8** `Math.round(float)` (significand-shift
algorithm, not `(int)floor(a+0.5)`); quota multiply in float precision, left-assoc
`((count*.08f)*1.6f)`. **H7**: quota base excludes row 13, padding does not.
**setEmeraldElite** (design §11 **v0.1.3**; §1.1:43 corrected): on the fully-
unlocked A20 profile the `isFinalActAvailable && !hasEmeraldKey` guard PASSES, so
one `mapRng.random(0, eliteNodes.size()-1)` draw fires after assignment (+1
wrapper draw). Modelled (the chosen elite's key flag is out of S1 scope; the DRAW
and its s0/s1 effect are what the triple needs).
**Acceptance — gtest `map_rooms_test`, 10 cases over the 20-seed A20-Ironclad
live-oracle corpus** `tests/golden/map_paths/oracle_maps.txt` (the B4.1 golden;
its header carries the post-generateMap `{counter,s0,s1}`, each node its room
`symbol`): (1) **room symbols match the oracle NODE-FOR-NODE for all 20 seeds**
(`RoomSymbolsMatchOracleAllSeeds`); (2) **full post-generateMap {counter,s0,s1}
triple matches the live oracle for all 20 seeds** (`PostGenerateMapRngTripleMatchesOracle`)
— jointly pins the trap-12 shuffle permutation/length AND the emerald draw;
`TailCounterAdvanceIsExactlyEmeraldDraw` confirms the whole tail advances counter
by exactly +1 (shuffle 0 + emerald 1) with ≥1 elite every seed; (3) **trap-12
named test** (`Trap12ShuffleAdvancesRawStateNotCounter`): counter unchanged +
raw (s0,s1) == an independent RandomXS128 stepped `size-1` times, sizes
{2,5,17,42,105}; + determinism/multiset-preservation; (4) **quota A0 vs A20**
(`QuotaTableA0VsA20`, count∈{50,62,40,10}: elite differs only by ×1.6, all else
ascension-invariant) + `JavaRoundFloatHalfUp` (H2 half-up parity); (5) structural
rules (fixed rows, no-rest 0-4/13, no-elite 0-4, treasure only row 8, no
unassigned edge node) + RunState room_type encode round-trip. Full suite green
all three presets: **debug 201/201, asan 201/201, release 201/201** (191 baseline
+ 10 new; header-only — no engine `.cpp` changed, `sizeof(RunState)` /
`SCHEMA_VERSION` untouched). **B4.3/B4.4 need:** `RoomType {None=0,Monster=1,
Event=2,Elite=3,Rest=4,Shop=5,Treasure=6,Boss=7}` written into
`RunState.map[].room_type` via `encode_rooms_into_run_state`
(game-oriented `run_state_map_index`, mirrors B4.1's edge encoding) — B4.3 owns
final RunState population + schema-v2 reorientation, values append-only-friendly;
`RoomAssignment.rng` is `mapRng` at the very END of generateMap (shuffle+emerald
applied) so B4.4 run-advance must NOT re-consume the emerald draw. **Unmodeled**
(out of S1 scope): the chosen elite's `hasEmeraldKey` flag (only the draw);
`Boss` room-type reserved/unused (boss is not a grid node); fixed-row no-edge
nodes the game also stamps (irrelevant to the traversable grid).

### B4.3 `[x]` RunState population + additive fields (schema v2)
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
**Log:** Verified by running (WSL Ubuntu-2404), not inferred; isolated in a
detached worktree at HEAD `f0617c5`. **`sizeof(RunState)` 1648 -> 2184 B**
(design §2.6 baseline confirmed 1648; well under the 8192 budget). **Schema bump
2 -> 3** (NOT 1 -> 2: B1.6 already advanced `SCHEMA_VERSION` to 2 when it
decoupled the trace FORMAT tag from the engine version, so this additive struct
edit is v3 per stage-a §12 / §8's "bumped by any struct edit"). The `(schema v2)`
in the heading is the pre-B1.6 expectation; the effective bump is 2 -> 3.
**Additive RunState fields** (`run_state.hpp`, all POD / value-init-clean per
stage-a §12): `neow_rng` (event-scoped 14th stream, §2.5 #2); `event_pity_monster/
shop/treasure` floats (EventHelper MONSTER/SHOP/TREASURE_CHANCE, §2.5 #5);
`purge_cost` i16 (§2.5 #6); `potion_slots` u8 (A11 count); `event_membership` u16
/ `shrine_membership` u8 / `special_membership` u16 (remaining-pool bitsets,
§2.5 #7); `relic_pools[5][48]` u16 + `relic_pool_count[5]` (relic pool orders ×5,
§2.5 #8, trap 15; tier order Common/Uncommon/Rare/Shop/Boss). Existing
`event_flags`/`shop_flags` retained as the one-shot *fired* view (distinct from
the new *remaining-pool* membership); `boss_ids` already real storage.
**Map-orientation reorientation (owned since B4.1):** `kMapRows` 7 -> 15 (floors)
/ `kMapCols` 15 -> 7 (cols) so `RunState.map` is game-native (AbstractDungeon.java:
210-211). This is a RENAME ONLY: the 105-node backing array and the row-major
index `floor*7 + col` are byte-identical either way (`run_state_map_index` now
`y*kMapCols + x`), so ZERO map bytes move and it is NOT fixture regeneration. A
`static_assert(kGameMapFloors==kMapRows && kGameMapCols==kMapCols)` in
`map_gen.hpp` pins it; `map_rooms.hpp`'s `RoomType` is finalized as the
`MapNode.room_type` encoding (append-only). All 20-seed `map_gen_test` /
`map_rooms_test` stay green UNCHANGED in meaning (they index via
`run_state_map_index`, whose value is unchanged). **Trace/schema pins:**
`kTraceFormatV2` 2 -> 3 (tracks `SCHEMA_VERSION`; the `static_assert(kTraceFormatV2
== engine::SCHEMA_VERSION)` holds). The v2 CONTAINER format is unchanged; a
stale-sized RunState trace is refused by BOTH the stamped version and the
header's `run_state_size` check. `observation.hpp` uses `SCHEMA_VERSION`
symbolically (compiles green). **Fixture discipline (per the frozen rules):** the
20 frozen v1 combat fixtures carry `kTraceFormatV1`=1 and load via
`read_trace`/`read_trace_v2` compat with **ZERO regeneration**
(`FixtureOracle.AllFixturesReplayWithZeroDiffs` green); no run-level (RUN/v2)
trace goldens are committed, so there is nothing to regenerate. **Translator
un-deferral (`translate.cpp`) — the "now-representable" deliverable clause:**
`neowRng`, `eventPity`×3, `purgeCost` now MAP bit-for-bit; `potion_slots` = the
potions-array length. **Kept DEFERRED with storage present:** `relicPools`,
`eventList`/`shrineList`/`specialOneTimeEventList` — mapping the golden's real
values bit-for-bit needs content-id enums that DO NOT EXIST at HEAD (`relics.yaml`
and `events.yaml` are empty skeletons; a fail-loud join would throw). Their
RunState storage now exists (front-loaded per design §2.6), so they un-defer with
no further schema bump when **B4.6** populates `relics.yaml` (relic pools) and
**B4.10-B4.13** populate `events.yaml` + the canonical list order (membership
bitsets); each is that task's translator un-deferral. `monster_move_history`
(>3) and real `act_boss`/screen fields stay deferred to their owning tasks
(`act_boss` is null in the golden; no boss registry). Deferred keys are still
STRUCTURALLY consumed, so a new/renamed oracle key still trips the fail-loud
drift error. **card-pool removal bookkeeping** (a deliverable sub-item) added NO
storage: design §2.5's note is "add if pools mutate", and the B4.5 card-reward
dup-loop operates on pools derived from the card library filtered by the master
deck, not a persisted removal set — flagged for B4.5 to add then if it needs it.
**Differ (`differ.cpp`):** `diff_run_states` gains named comparisons for every new
field group (pity floats via a new exact-bit `cmp_f`, `purge_cost`,
`potion_slots`, the three membership bitsets, the 5 relic-pool orders with
positional/count checks, and `neow_rng` as the 9th run-level stream). **Tests:**
`translator_test` (+1 `NeowRngMapsWhenPresent`; `OracleFieldsLandBitForBit`
extended: `event_pity` bit-for-bit {0.1/0.03/0.02f}, `purge_cost`==75,
`potion_slots`==2 on both records, `neow_rng` value-init when absent);
`differ_test` (+3 `RunDifferAdditive` cases; `neow_rng` added to the per-stream
matrix; `MakeBaseRun` populates the new fields); `state_test`
(`RunMemcpyRoundTripIsEqual` exercises the new fields; the `<= 8192` ceiling
holds). **COMBAT RELIC MIRROR (orchestrator-approved addition beyond the block's
literal RunState-only deliverables; escalated + ruled IN because it is
schema-efficient to fold into this bump and makes B3.24's committed relic
dispatch live instead of dead code).** `CombatState` gains `relics[kRelicCap]`
+ `relic_count` (`combat_state.hpp`; the RUN-LEVEL fold-back that populates it
across combats stays B4.4's, whose deliverable lists "relic counters");
`relic_hooks.cpp::player_relics()` now returns `{s.relics, s.relic_count}`, so the
wired sites in `power_hooks.cpp`/`action_queue.cpp` are live. **`sizeof(CombatState)`
3504 -> 3672** (+168 = `RelicSlot[40]` 160 + count 1 + 7 pad; a clean 8-aligned
insertion at offset 3382). **Fixture regeneration (explicitly authorized by the
orchestrator; sanctioned by THIS block's own deliverable "trace/fixture
regeneration via checked-in generators only" + acceptance "regenerated fixtures
zero-diff"):** growing `CombatState` invalidates the 20 v1 combat fixtures (they
embed `sizeof(CombatState)` in the header + as raw bytes; `read_trace` refuses a
size mismatch). Regenerated ALL 20 via the committed INDEPENDENT reference
simulator `tools/fixture_gen/gen_combat_fixtures.cpp` (never by replaying the
engine-under-test -- the generator's independence is the point). **This
distinguishes the B1.6 zero-regen precedent:** B1.6 kept `sizeof(CombatState)`
UNCHANGED so the fixtures loaded via compat-read with zero regeneration; B4.3
GROWS `CombatState`, so the ledger's generator-regeneration path applies instead
(the stage-a "no hand-edit" rule is honoured -- the checked-in generator, not a
human, rewrote them). **Zero-diff-in-meaning proven mechanically**
(`<scratchpad>/b43_fixture_proof.py`, output archived): over all **20 fixtures /
111 records**, each new record's `CombatState` == the old record's bytes with a
single contiguous run of **168 zero bytes inserted at offset 3382** (== the mirror
field's offset) and NOTHING else changed -- header magic/schema_version(v1)/
record_count/seed and every per-record action/aux byte identical; i.e. every
pre-existing byte is preserved in order and the only delta is the zero-init relic
mirror. `FixtureOracle.AllFixturesReplayWithZeroDiffs` is green on the regenerated
set (engine replay == regenerated fixture, all 20). **`observation.hpp` pin
unaffected:** `ObsBuffer` (188 B) does not mirror relics, so its `sizeof`
static_assert is unchanged; there is no exact `sizeof(CombatState)` pin in code
(only the `<= 4096` budget assert, which holds at 3672). **`relic_hooks_test`
against the REAL mirror:** the stale "empty until B4.3" seam test is replaced by
two live tests -- empty-mirror (view returns `{s.relics,0}`, dispatch no-op, so
the zeroed-mirror fixtures stay behaviourally identical) and populated-mirror
(Anchor+Bag-of-Preparation via the mirror view drive the battle-start dispatch);
`RelicHooks` 50/50. **Full suite green all three presets on the final base
`a9c1c63` (post B3.4/B3.23/B3.24): debug 273/273, asan 273/273, release 273/273**
(268 baseline + 5: translator +1, differ +3, relic-seam +1). (RunState-only slice
was independently green 254/254 x3 on the e3f71c9 checkpoint before the mirror.)
**B4.6/B4.10 note:** RunState storage for relic pools + event/shrine/special
membership is in place; those tasks add only their population + translator
un-deferral (no schema bump). **B4.4 note:** `neow_rng` / `potion_slots` / pity
floats are set at `run_begin`; the relic pools are shuffled at dungeon init (5
relicRng draws) into `relic_pools[]`; the combat relic mirror is filled from
`RunState.relics` at combat spawn and folded back after.

### B4.4 `[x]` Run-level advance + room lifecycle
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
**Log:** Done 2026-07-23. Verified by running in the isolated
`b44-room-lifecycle` worktree at base `481f65b`: **debug 363/363, asan
363/363**; the B4.4 task slice is 19/19 in both presets (344 baseline + 19 new).
After semantic integration on top of B3.13 and B3.9, the focused
`run_advance_test` is **19/19**, and the complete WSL Ubuntu-2404 matrix is
**debug 387/387, leak-detecting ASan/UBSan 387/387, release 387/387**.
Integration restored B3.9's post-shuffle Innate partition in run-created
combats and invokes B3.13 pre-battle actions after group spawn; the named
two-louse regression pins six monsterHpRng draws, both Curl Up powers, drained
queues, WAITING_ON_USER, and Writhe in the opening hand.
- **Public run API/state machine:** `RunController` is a trivially-copyable
  snapshot containing persistent `RunState`, the live `CombatState`, generated
  B3.12 encounter lists/cursors, map position, `RunPhase`, and
  `RunCombatOutcome`. `advance`/`legal_actions` overload on RunController (the
  stage-a §7 one-name/all-phases contract); NEOW, MAP_CHOICE, COMBAT,
  COMBAT_REWARD, ROOM_UNIMPLEMENTED, and RUN_OVER dispatch independently in a
  heterogeneous batch. CHOOSE drives Neow-proceed, legal map edges, combat card
  choices, and reward/proceed; PLAY_CARD/END_TURN delegate to combat.
- **run_begin:** initializes the base Ironclad sheet/deck/Burning Blood and a
  Neow-pending floor-0 state; all run streams + Neow stream start from `seed`,
  B3.12 consumes monsterRng into encounter lists, the five unconditional relic
  pool-shuffle seeds consume relicRng exactly 5x (B4.6 owns pool contents), and
  B4.1/B4.2 populate map edges/rooms + the post-generateMap mapRng. A11 is live
  through `potion_slot_count` (A20=2), per B4.3's explicit handoff; A6/A10/A14
  remain B4.15's literal owner. The committed live-oracle floor-0 triple test
  pins monsterRng/relicRng/mapRng bit-for-bit.
- **room/combat lifecycle:** `next_room_transition` removes the room's encounter
  cursor on exit, increments floor, THEN reseeds all five floor streams from
  `(seed,floor)` (trap 7), enters the chosen node, and spawns implemented groups
  through B3.13. Combat construction mirrors `combat_begin` byte-for-byte for
  Jaw Worm, including deck shuffle, B3.9 Innate ordering, spawn/AI order, and the
  B4.3 relic mirror; Cultist/louse groups run their pre-battle actions before
  player control.
  On kill or Smoke Bomb escape, `AbstractRoom.endBattle`-equivalent victory
  relics fire before fold-back; hp/max-hp and relic counters copy back, while
  gold/potions/master-deck remain canonical in RunState throughout combat.
  Kill vs Smoke Bomb is explicit in `RunCombatOutcome`; the smoked screen is a
  no-reward CHOOSE/proceed state, not a kill. Death alone terminates the run.
- **USE_POTION both layers:** RunState slot masks include target grids in combat
  and the legal non-combat Fruit Juice/Entropic Brew overrides. Successful use
  consumes the positional slot; data/native combat effects route through
  `use_potion` + the normal pump. Fruit Juice mutates persistent/live hp and
  max-hp; Entropic Brew performs `potion_slots` **limited** identity rolls before
  destroying its slot, including Java's discard-first/Fruit-Juice rejection
  quirk, then fills available slots; Toy Ornithopter now triggers in combat and
  outside it. Smoke Bomb rejects bosses, leaves monsters alive, consumes its
  slot, fires victory hooks, and opens the smoked proceed state.
- **Acceptance tests:** full floor cycle Neow -> legal map pick -> B3.12 combat ->
  kill reward/proceed -> floor 2 checks every run/floor stream boundary plus
  gold/potions/deck/relic fold-back ownership; named post-increment floor reseed;
  run-combat equivalence; Fruit Juice/Entropic/Toy/target-potion/Smoke Bomb;
  live floor-0 oracle triples; and one allocation-free batch simultaneously
  stepping NEOW CHOOSE, MAP CHOOSE, combat PLAY_CARD/END_TURN, and run-layer
  USE_POTION under ASan.
- **Provenance read in full:** AbstractDungeon.nextRoomTransition
  (AbstractDungeon.java:1687-1813, including eventRng duplicate/commit semantics),
  AbstractRoom.update/endBattle (AbstractRoom.java:220-445), ProceedButton.update
  (ProceedButton.java:79-171), CombatRewardScreen.open/openCombat
  (CombatRewardScreen.java:229-305), AbstractPotion.canUse + PotionPopUp.update
  / TopPanel.destroyPotion, FruitJuice.java, EntropicBrew.java,
  ObtainPotionAction.java/ObtainPotionEffect.java, SmokeBomb.java + the player
  escape timer, MonsterGroup escape predicates, and ToyOrnithopter.java.
- **Downstream boundaries (not B4.4 seams):** B4.5 owns reward assembly/claims;
  B4.6 owns populated relic pools; B4.7-B4.10 own non-combat room content and
  B4.10 owns the ?-room eventRng duplicate roll; B4.14 owns Neow choices/payouts;
  B4.15 owns the remaining A20 setup modifiers. Unimplemented monster groups
  park after consuming their B3.12 composition draws until B3.14-B3.22 land;
  enemy self-escape/stolen-gold mechanics remain B3.15's explicit owner. No
  schema change or fixture regeneration: RunState/CombatState layouts are
  unchanged.

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

### B4.6 `[x]` Relic pools + acquisition
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
**Log:** Done 2026-07-23 on base `0dc9bcf`. `relic_pools.hpp/.cpp` now
populates the five existing fixed-capacity RunState tier pools from generated
registry metadata and performs the five unconditional relicRng `randomLong`
draws in Common/Uncommon/Rare/Shop/Boss order, each routed through the JDK-LCG
shuffle. Front/end removal (including the boss end-path front pop), empty-tier
cascades, 50/33/17 tier selection, current `canSpawn` floor/shop gates, and the
failed-front-to-end recheck are live. The generated relic table adds validated,
contiguous `pool_order`, validated int16 `initial_counter`, and `kRelicDefs`;
the complete B3.24 common pool's canonical pre-shuffle order is recorded without
renumbering any existing id. B3.25-B3.27 remain the owners of uncommon,
rare/shop, and boss rows; their future rows will populate the same generic five-
tier initializer, while all five shuffle draws already occur even for empty
tiers. The translator's all-tier `relicPools` field therefore remains deferred
until those registries land rather than accepting unknown oracle ids.

Acquisition appends before pickup handling and preserves trap-8 ordering;
ordinary duplicates append, while Circlet duplicates increment the owned
counter. Initial counters, the fixed-cap fail-without-mutation path, Strawberry,
Potion Belt, War Paint, and Whetstone pickup effects are wired (the two upgrade
relics consume one miscRng `randomLong` and use a JDK shuffle). `run_begin` now
uses this acquisition path for Burning Blood and preserves its Java counter
`-1`. Exhausted boss returns the Java key `"Red Circlet"`, but
`RelicLibrary.initialize` does not register `RedCirclet`; the observed
`RelicLibrary.getRelic` fallback is therefore modeled as ordinary `CIRCLET`, not
as a fabricated registry id.

Provenance read: `AbstractDungeon.java:676-819,1221-1256`,
`RelicLibrary.initialize/populateRelicPool/getRelic`,
`AbstractRelic.instantObtain/obtain`, `Circlet`, `RedCirclet`, and every current
common relic's `canSpawn`/constructor/`onEquip`. Tier-2 oracle evidence uses the
live §2.5 `b14_accept` captures for seeds `1790050543751..1790050543753`: all 33
common ids match the three literal shuffled orders and the post-five-draw
relicRng `(s0,s1,counter)` triples exactly. Named trap-15, fallback, gate,
acquisition/capacity, counter, pickup shuffle, and run-begin regressions are
**14/14**; registry generation (including duplicate pool-order rejection) is
**15/15** and run lifecycle is **19/19**. Complete WSL Ubuntu-2404 suites are
**debug 402/402, leak-detecting ASan/UBSan 402/402, release 402/402**.
After integration on top of B3.5 and B3.14, the combined manifest is cards 50 /
powers 21 / monsters 8 / relics 35 / potions 33 / encounters 20 / total 167.
Focused RelicPools 14/14, RegistryGen 16/16, and run lifecycle 19/19 remained
green; the complete integrated WSL matrix is **debug 428/428, leak-detecting
ASan/UBSan 428/428, release 428/428**.

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

- 2026-07-22 — B1.1 acceptance amended (ledger is the losing document; fixed
  in the same change per the working agreement). "Reproduces B0.2's captured
  run byte-for-byte" is unsatisfiable as written: the B0.2 capture is
  timing-contaminated (queued stepper `end`-resends produced a game history —
  5 monster hits by "turn 3" — that no clean state-paced replay can recreate;
  root cause in the B1.1 Log). Replaced with the controlled equivalent: two
  stock-jar runs of the derived effective script must be byte-identical
  (determinism control), the fork run must be byte-identical to stock on the
  same script, and the run must anchor to the B0.2 capture (Neow floor-0
  state, pre-contamination 18-state prefix, floor/screen trajectory). The
  B1.3 strip-equivalence and G4 acceptance style (fork-vs-fork A/B on
  identical scripts) is unaffected.
- 2026-07-17 — v0.1 created from stage-b-design.md v0.1 (§2-§8 → phases
  B0-B5). Task counts: 57 tasks + 4 gates (G4-G7, continuing Stage A's gate
  numbering; G4=M2, G6=M3, G7=M4). Card/monster/relic batch enumerations
  marked "enumerate at task start" are counted from the CardRarity/RelicTier
  greps recorded in design §5 — the named example lists in B3.x are
  orientation, not authority; the source enumeration at task start wins.
