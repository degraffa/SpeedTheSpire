# Stage B Task Log (archive)

The verbatim task blocks of every **completed** Stage B task, moved out of
[stage-b-tasks.md](stage-b-tasks.md) so the active ledger stays small. This
file is **append-only reference**: read it only when you need the provenance
of a landed task (what exactly it shipped, which Java lines it cited, which
ids/opcodes it consumed, what it measured). Nothing here is a live
instruction.

Forward-looking obligations that these Logs left to future tasks have been
lifted into the active ledger's **Deferred obligations** table — do not
re-derive them from here.

Blocks are byte-for-byte as they stood in the ledger at the time of the
split (`docs: split Stage B ledger …`), in ledger order. Each is preceded by
an `<a id="…">` anchor (e.g. `#b325`) that the ledger's index entries link
to. Later Logs are appended here by the orchestrator when a task lands.


---

## Phase B0 — Bridge groundwork

<a id="b01"></a>

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

<a id="b02"></a>

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

<a id="b11"></a>

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

<a id="b12"></a>

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

<a id="b13"></a>

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

<a id="b14"></a>

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

<a id="b15"></a>

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

<a id="b16"></a>

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

<a id="g4"></a>

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

## Phase B2 — Registry system + skeleton migration (Gate G5)

<a id="b21"></a>

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

<a id="b22"></a>

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

<a id="g5"></a>

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

## Phase B3 — Combat content closure

<a id="b31"></a>

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

<a id="b32"></a>

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

<a id="b33"></a>

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

<a id="b34"></a>

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

<a id="b35"></a>

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

<a id="b36"></a>

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

<a id="b37"></a>

### B3.7 `[x]` ∥ Red uncommons — power cards
**Deps:** B3.2 · **Provenance:** cards/red UNCOMMON powers (~11; enumerate)
**Deliverables:** registry entries + their powers: Combust, Dark Embrace,
Evolve, Feel No Pain, Fire Breathing, Inflame, Metallicize, Rage, Rupture,
plus the power-card play path (card→power, no discard).
**Acceptance:** tier-2 per power incl. trigger-order interactions (e.g. Feel
No Pain + Dark Embrace on the same exhaust, list-order resolution per
stage-a §5.5); directed script.
**Log:** Done 2026-07-24. Source enumeration found eight actual
RED/UNCOMMON/POWER cards: Combust, Dark Embrace, Evolve, Feel No Pain, Fire
Breathing, Inflame, Metallicize, and Rupture. Rage is a RED/UNCOMMON
`CardType.SKILL` already owned by B3.6, so the source roster wins over the
orientation list. Card ids 68-75 and native power ids 26-27 were appended
without renumbering. POWER-card play now applies the power and removes the card
from every pile rather than discarding it. Native hooks implement Combust's
per-application HP-loss ratchet, Evolve/Fire Breathing draw triggers, and the
source live-monster/No Draw guards.

Tier-2 tests cover every base/upgraded row, the no-discard path, Combust
stacking, Evolve and Fire Breathing type filters, Feel No Pain + Dark Embrace
same-exhaust list order, death/No Draw guards, Metallicize/Rupture, and an
`advance()` directed script. Provenance read in full: the nine ledger-listed
card classes (including Rage for its source classification),
`CombustPower`, `DarkEmbracePower`, `EvolvePower`, `FeelNoPainPower`,
`FireBreathingPower`, `MetallicizePower`, `RupturePower`, and
`StrengthPower`. Verified by running, not inferred, in WSL Ubuntu-2404:
focused B3.7 **12/12**; complete debug **515/515**; leak-detecting
ASan/UBSan **515/515**.

**Fix-forward — Combust oracle import (2026-07-24):** Independent review found
that `GameStateConverter.convertCreaturePowersToJson` exports Combust's private
`hpLoss` through semantic power `misc`, while the translator deferred every
power `misc` value. Imported stacked Combust snapshots therefore lost their
per-turn HP-loss counter and subsequent applications stacked from the fallback.
The translator now maps only player-owned Combust `misc` into the reserved
combat-flag field, requiring an integer in `[1,255]`; all other power `misc`
fields remain deferred. Five regressions cover base/stacked import,
reapplication plus end-turn behavior, remove→reapply reset, fail-loud validation
for missing/type/range errors, and non-Combust deferral. Provenance:
`GameStateConverter.java` power reflection, `PROTOCOL.md` §3.14, and
`CombustPower.java` constructor/`stackPower`/`atEndOfTurn`. Verified by running,
not inferred: focused translator **16/16**, focused B3.7 **12/12**, complete
debug **526/526**, and leak-detecting ASan/UBSan **526/526**. No schema, layout,
or ID change.

<a id="b38"></a>

### B3.8 `[x]` ∥ Red rares
**Deps:** B3.2 · **Provenance:** cards/red RARE (16)
**Deliverables:** registry entries: Barricade, Berserk, Bludgeon, Brutality,
Corruption, Demon Form, Double Tap, Exhume, Feed, Fiend Fire, Immolate,
Impervious, Juggernaut, Limit Break, Offering, Reaper. Wires the block-decay
Barricade branch A3.1 left structural, Corruption's cost/exhaust rewrite,
Feed/Reaper HP-max/heal opcodes.
**Acceptance:** tier-2 per card; Barricade block-persistence through the
frozen start-of-turn sequence; directed script.
**Inherited:** `healing: true` on **Feed** and **Reaper** (CardTags.HEALING) — deferred by
B3.6; `kIroncladAttackPool` is generated from the healing column, so Infernal Blade's
pool is WRONG until this lands. Barricade's block-decay branch and the recursive-play
opcode for Double Tap — deferred by B3.2.
**Log:** Verified by running, not inferred (`tools/wsl_run.sh debug asan` from
the Windows host, both presets PASS; the new `card_rares_test` target's 27 cases
were confirmed **by name** in `ctest -N` before the green was trusted;
`tools/check_stale_counts.sh` and `tools/check_doc_links.sh` both clean). Landed
as commit `603cac2`, merged at `c47d534`, integrated at `8235477`. `cards.yaml`
ids **76-91** — the 16 enumerated from source (`cards/red` constructors passing
`CardRarity.RARE`), every `use()`/ctor read in full before encoding.
`powers.yaml` ids **48-53**; opcodes **34-39** including **`PLAY_CARD`**;
`ChoiceKind::EXHAUST_TO_HAND` = 5 for Exhume's exhaust-pile source; and
`CardFlag::PURGE_ON_USE`.
- **Corruption needed no power row** — id 11 is the row B3.2's hook framework
  had already registered; only its card was missing. `PowerId` **47 was left the
  permanent gap the Guardian batch reserved**, not backfilled.
- **`PLAY_CARD` is the GENERAL recursive-play verb**, not a Double Tap special
  case: the source is a card-pool index *or* the top of the draw pile, with
  flags for choose-copy / purge / forced exhaust / queue-front. **B3.11's Mayhem
  reuses `{op: PLAY_CARD, play: [from_draw_top]}` unchanged**, which is what
  discharges B3.2's obligation in its general form. Double Tap calls the same
  body **directly rather than queueing**, because `DoubleTapPower.onUseCard`
  inserts **synchronously inside the `UseCardAction` constructor**.
- **Three deferred obligations discharged.**
  - **`healing: true` on Feed and Reaper** (`CardTags.HEALING`, `Feed.java:38` /
    `Reaper.java:37`). The generated `kIroncladAttackPool` moves **25 → 28** —
    Bludgeon, Fiend Fire and Immolate join; Feed and Reaper leave — which fixes
    Infernal Blade's pool. Both heals route through `heal_player_with_relics`,
    so Magic Flower applies (named tests assert the ×1.5).
  - **Barricade's block-decay branch, on the `kSubsequentTurn` side of the
    turn-1 gate, with Calipers.** The whole guard paragraph is
    `GameActionManager.java:353-359`, **inside step 6**, and
    `AbstractRoom.java:236-258` has **no `loseBlock` line at all**. Calipers was
    left exactly as B3.26 landed it.
  - **The recursive-play opcode for Double Tap** — see above.
- **Two fix-forwards, both made reachable by Corruption's card landing:**
  `AbstractPlayer.useCard:1378`'s energy skip for a SKILL under Corruption, and
  `CorruptionPower.onCardDraw`'s missing `COST_MODIFIED_FOR_TURN` mark (without
  it the free cost outlived the turn).

<a id="b39"></a>

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
  - **CORRECTION appended 2026-07-27** (append-only — the sentence above stands as written and is wrong in one respect). "Queues hand-card effects" is not what `triggerOnEndOfTurnForPlayingCard` does: each of the five cards re-queues **itself** into the `cardQueue` with `dontTriggerOnUseCard` and is PLAYED, so it LEAVES THE HAND for the discard pile at the trigger stage — ahead of the `DISCARD_HAND` sweep rather than as part of it. `LOSE_HP_PER_HAND`'s hand size is likewise locked at trigger time, not read at execute. Caught by the STS00048 oracle replay; see **Combat: end-of-turn curses play themselves out of the hand** under [Landed non-task work](stage-b-tasks.md#landed-non-task-work).
- Frail is fully live, not an inert row: card block runs its x0.75 float modifier and floors once after all powers; direct GainBlockAction block bypasses it. `CombatState.flags` stores the player instance's `justApplied` latch without a layout/schema change; a new Shame/monster-sourced instance skips its first end-of-round decrement, stacking preserves the existing latch, and later rounds queue ReducePowerAction-equivalent reduction/removal after the power-list walk. B3.13's Cultist Ritual end-of-round and louse Curl Up behavior remain intact.
- Added `status_curse_test` (9 tier-2/directed tests). Correct hand discard intentionally changes the 20 combat traces; all were regenerated from the checked-in generator, replay green, and a repeated generation produced an identical sorted SHA-256 manifest. Updated the affected CardIntegration hash/trace, power-hook action count, and translator unknown-id probe rather than weakening them.
- Verification: task branch full Debug 353/353 and leak-detecting ASan/UBSan 353/353. After semantic integration with B3.13 and Frail completion, full WSL Ubuntu-2404 Debug 368/368 and `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1` ASan/UBSan 368/368. No schema bump; `git diff --check` clean.

<a id="b310a"></a>

### B3.10a `[x]` ∥ Colorless uncommons — the fourteen
**Deps:** B3.2 · **Provenance:** cards/colorless UNCOMMON, the fourteen below
**Deliverables:** Bandage Up, Blind, Deep Breath, Dramatic Entrance, Finesse,
Flash of Steel, Good Instincts, Impatience, Madness, Mind Blast, Panacea, Panic
Button, Swift Strike, Trip — ids 92, 93, 95, 97, 99, 100, 102, 103, 105, 106,
107, 108, 110, 111 (library order, interior gaps reserved).
**Acceptance:** tier-2 per card; Madness `cardRandomRng` draw accounting;
directed script; a named test pinning the two block passes.

**Log:** commit `4da9739`, merged at `f552e84`, landed in `20825be`. Opcodes 45
`DAMAGE_DRAW_PILE` / 46 `CONDITIONAL_DRAW` / 47 `RESHUFFLE_ALL` / 48 `MADNESS`,
power 77 `NO_BLOCK`. The allocated block was spent exactly; ids 94/96/98/101/104/
109 stay empty and are **pinned empty by a named test** (`InteriorIdsStayReserved`).

**This task exists because its predecessor refused to do it.** B3.10 was
dispatched whole; the agent read every `use()` and ctor in full, found that ten
of the twenty cards each need a *distinct* new verb — about 2.5× the allocated
opcode budget — and **stopped without committing**, proposing the split
(conventions §5). It also declined to land the clean fourteen-card subset,
because doing so would spend all four opcodes on four of the ten verbs and
consume interior ids under an allocation about to be revised. Both calls were
right. The same read produced the two brief errors recorded in the ledger's
change log (`vocab.py` wrongly declared off limits when `OPCODES` lives in it;
the count guards briefed as four sites when there are five).

**`NO_BLOCK` overrides `modifyBlockLast`, not `modifyBlock`**
(`NoBlockPower.java:58-60`) — the whole game's only overrider of that hook
(verified by searching the tree: `AbstractPower.java` declares it,
`NoBlockPower.java` overrides it, `AbstractCard.java` calls it, nothing else
mentions it). `applyPowersToBlock` (`AbstractCard.java:2291-2307`) runs
`modifyBlock` over **all** powers and *then* `modifyBlockLast` over all powers.
The engine's `modify_block` was a **single pass**, which yields `N` where the
game yields `0` whenever a Dexterity sits later in the power list. So Panic
Button was a **structural** `interp_block.cpp` change, not a data row.
Pinned by `ModifyBlockLastBeatsADexterityLaterInTheList` and
**demonstrated RED before green**: folding `NO_BLOCK` back into the single pass
failed that test with exactly the predicted 3 — and *nothing else moved*, so
that one test is the sole thing pinning the ordering, which the Log records
rather than hides. Three companion cases keep it from decaying (Dexterity alone
→ 9, reversed list → 0, Frail → 4).

**Deep Breath exposed a live, engine-wide divergence.** `AbstractPlayer.useCard`
(`:1369-1375`) queues a card's own actions **before** the `UseCardAction` that
files it away, so the played card is in `cardInUse` limbo — in **no pile** —
while its own reshuffle runs; `resolve_card_play` moves it to the discard early.
For Deep Breath that changes both the `discardPile.size() > 0` guard's answer
*and* the Fisher-Yates permutation of every other card: played onto an otherwise
empty discard it would have drawn **2** `shuffleRng` values where the game draws
**0**. Fixed the Headbutt way (a source-pool index stamped into `tgt`, excluded
by `reshuffle_all`). Deep Breath also **cannot be two authored `SHUFFLE_IN`
steps** — the game guards *both* `EmptyDeckShuffleAction` and
`ShuffleAction(drawPile)` on the same test (`DeepBreath.java:34-38`), and after
the first runs the discard is empty either way, so a second step can no longer
see the guard's input. Hence one fused opcode. **The general limbo gap was
deliberately not widened into** and is now an obligations row.

**Two count-guard sites the published inventory missed**, both hit here:
`registry_gen_test.cpp`'s `kTotalCount` is a **derived** site naming neither
constant, so grepping for either misses it; and a synthetic test card was pinned
at **id 100** under a comment reading "past the real card ids 1-10 so it never
collides" — `FLASH_OF_STEEL` is id 100, and the loader rejected the duplicate.
Any card batch reaching 100 rows would have hit it. `relic_pools.cpp`'s
`kCardsCount` assert was **answered, not bumped**: all fourteen rows are
UNCOMMON and every one is SKILL or ATTACK, so neither the hard-coded BASIC set
nor Bottled Tornado's POWER scan moves.

**Verified:** union green on debug / asan / release at integration-16 with zero
NOT_BUILT; no committed fixture or golden vector modified, deleted or renamed.

<a id="b310b"></a>

### B3.10b `[x]` Colorless uncommons — the four needing generated pools
**Deps:** B3.10a (shares `cards.yaml`; branch off it, do not run beside it)
**Deliverables:** Dark Shackles, Discovery, Enlightenment, Jack of All Trades —
ids **94, 96, 98, 104**, opcodes **49–52**, power **78** (Dark Shackles'
`GainStrengthPower`, "Shackled").
**The real deliverable is two generated combat pools.** Discovery's no-argument
`DiscoveryAction` calls `generateCardChoices(null)`, which uses
`returnTrulyRandomCardInCombat()` — the full non-HEALING RED common, uncommon
and rare combat pool (`DiscoveryAction.java:44-62,106-121`;
`AbstractDungeon.java:944-979`). Jack alone uses
`returnTrulyRandomColorlessCardInCombat()` — COLORLESS uncommon + rare minus
HEALING (`JackOfAllTrades.java:28-34`; `AbstractDungeon.java:981-995`). The
colorless pool reaches its final 34 members when mandatory B3.10c and B3.11
land. Discovery additionally needs a **generated, unique 3-card choice** with
one `cardRandomRng` draw per attempt, including rejected duplicates.
**Acceptance:** tier-2 per card; Discovery `cardRandomRng` draw accounting;
directed script.

**Log:** Done 2026-07-26. A full source reread corrected the task's inherited
source claim before implementation: Discovery does **not** use the colorless
pool. Its no-argument action calls `generateCardChoices(null)`, which
rejection-samples the player's RED common/uncommon/rare combat pool. Jack of All
Trades is the sole consumer here of the colorless uncommon/rare pool. Both
memberships are emitted from `cards.yaml`, exclude HEALING rows and
self-complete as later mandatory card rows land. Their interim registry-id
ordering is carried by the existing B4.5 HashMap-order capture obligation;
membership and RNG draw counts are live now.

Dark Shackles' fused opcode performs the Artifact presence test before queuing
either child effect, then resolves Strength loss before Shackled, exactly
preserving the Java case where Artifact consumes the Strength debuff and
prevents Shackled from ever being queued. `PowerId` 78 is a real additive
DEBUFF whose end-turn program restores precisely its own amount of Strength and
removes itself. Named tests cover base/upgraded 9/15, Artifact, and a target
that already had Strength.

Discovery persists three distinct generated `CardId`s in its queue item and
blocks as a generated choice source. The public mask exposes three legal offer
slots; `advance(CHOOSE, slot)` creates the chosen base copy, makes it free this
turn and spills it to discard at the hand cap. A multi-seed test independently
replays rejection sampling and proves one `cardRandomRng` draw per attempt,
including duplicate retries. The upgrade removes Discovery's exhaust flag and
does not change its cost or choice.

Enlightenment reconstructs `AbstractCard.cost` separately from
`costForTurn`: base caps the current turn then restores the combat base;
upgraded changes bases above 1 for the rest of combat without overwriting an
already-cheaper temporary cost. `CardFlag` bit 10 plus a private three-bit
payload in bits 11–13 preserves a Confusion-modified base without changing the
fixed `CardInstance` layout; bits 14–15 remain free. Tests cover ordinary
temporary/permanent behavior, a Confusion base, and a pre-existing zero
`costForTurn`. Jack performs exactly one/two independent colorless-pool draws,
allows duplicates, adds base copies and exhausts at both tiers.

The directed test drives all four cards through only `legal_actions()` and
`advance()`. Generated-pool membership tests pin all three RED rarities,
HEALING exclusions and live colorless rows. Codegen now rejects any
`cards.yaml native:` key with a named negative test, discharging the
documented-but-silent no-op trap. No `CombatState`, registry schema, fixture or
golden vector changed. Final-tree WSL Debug, leak-detecting ASan/UBSan and
Release are green; stale-count, documentation-link and whitespace checks are
clean.

<a id="b310c"></a>

### B3.10c `[x]` Colorless uncommons — mandatory optional-selection closure
**Deps:** B3.10b + card-limbo
**Deliverables:** Purity (`CardId` **109**) and Forethought (`CardId` **101**),
both live. `ChoiceKind` **8** `PUT_ON_DRAW_BOTTOM`, `ActionVerb` **4**
`CONFIRM`, fuzz `MoveCat` **25** `CHOICE_CONFIRM`, `CardFlag` bit **14**
`FREE_TO_PLAY_ONCE`. No new opcode.
**The real deliverable is the optional multi-select surface.** Every choice the
engine had selected a fixed count and ended when that count was met. These two
select **zero to N** and end on an explicit button, which is a public
`ActionMask` change: `CHOOSE` becomes a toggle and a new verb resolves the
accumulated selection, the empty one included.
**Provenance:** `Purity.java:24-46`; `ExhaustAction.java:28-36,73-110`;
`Forethought.java:24-45`; `ForethoughtAction.java:24-66`;
`HandCardSelectScreen.java:88-101,330-341,375-390,441-447,488-542`;
`UseCardAction.java:87,132`; `AbstractCard.java:888,2057-2062`;
`AbstractPlayer.java:1378`; `ConfusionPower.java:46`;
`CardGroup.java:459-461,471-473,850-861,898-902`;
`GameStateConverter.java:538-557`.
**Acceptance:** tier-2 per card; directed public-API scripts for the zero-card
confirm, partial and max selection, the base forced-one with its RNG billing,
multi-card draw-bottom order and the one-play flag lifetime; translator tests
for the new screen shape; a fuzz enumerator test that reaches the empty confirm.

**Log:** Done 2026-07-27. Both cards are live and neither is inert; with them
the colorless UNCOMMON block 92–111 is dense, and the two interior ids that had
been held open are pinned as filled by the test that used to pin them as holes.

**The selection state needed no new storage, because the game does not store it
either.** `HandCardSelectScreen` never marks a card in place: selecting it
`removeCard`s it from `p.hand` and appends it to `selectedCards`, and
deselecting appends it to the **end** of `p.hand` rather than back where it came
from. The two groups are therefore, at every instant, one ordered sequence —
what is left of the hand, then the picks in pick order — and that is exactly how
the engine holds it: the picks are the trailing suffix of `hand`, and the only
added state is a four-bit count in the open `CHOOSE_CARD`'s flags. Pick order is
observable, because the confirm applies the picks in it, and the array carries
that order for free. It is also what makes the oracle's `hand` / `selected`
split translate as a plain concatenation.

`ActionMask` grows three fields — `choice_optional`, `can_confirm_choice` and
`choice_selected_count` — all false/zero for every choice that existed before,
so a policy written against the older surface keeps working unchanged. The
debug-only mask-equality assert behind the four-span `advance()` overload
compares all three, and a directed test drives a whole toggle / deselect /
confirm sequence through that overload, so they are part of what "the mask
matches the state" means rather than fields the assert forgot. The confirm
button is legal for as long as an optional screen is open, nothing picked
included: `canPickZero` enables it at open and `refreshSelectedCards` never
disables it again for an `anyNumber && canPickZero` screen.

**Two Java branch guards are load-bearing and both are easy to get backwards.**
`ExhaustAction`'s "hand.size() <= amount → exhaust the whole hand with no
screen" is guarded by `!anyNumber`, and Purity passes `anyNumber` true — so a
two-card hand under a three-card Purity still opens the screen, and taking none
of them is a legal answer. Purity's `isRandom` is false, so it spends **no** RNG
on any path, the empty-hand one included. `ForethoughtAction`'s forced path
(`hand.size() == 1 && !chooseAny`) takes `getTopCard()` with **no**
`cardRandomRng` draw at all — unlike the `PutOnDeckAction` forced path whose
per-card billing was fix-forwarded earlier in this phase. Only what the Java
bills is billed, and both facts are pinned by named tests.

Draw-bottom order is pinned end to end: `moveToBottomOfDeck` → `addToBottom` →
`group.add(0, c)`, so with several picks the last one ends up deepest and the
first is the first of them drawn again. `FREE_TO_PLAY_ONCE` is granted per moved
card on the strict `c.cost > 0` predicate against the reconstructed combat
**base** cost, not `costForTurn` — so a card that is merely free for the turn
still qualifies and a permanently-zeroed one does not. Its lifetime mirrors the
action-local exhaust bit the card-limbo work landed: it admits the card at
`hasEnoughEnergy`, suppresses the spend in `useCard`, and is consumed when the
queued `UseCardAction` files the card — cleared unconditionally at the top of
that update, so even a POWER, which lands in no pile at all, spends it. The
terminal limbo flush clears it too. Confusion's redraw clears it as well; that
clear was already quoted in the power's body comment with nothing to apply it
to, and the prerequisite has now arrived.

The translator's deferred `can_pick_zero` is discharged: `HAND_SELECT` is mapped
rather than deferred, all four of its keys. The screen's `hand` is cross-checked
against `combat_state.hand` (the same `p.hand.group`, emitted twice in one
converter run), `selected` is appended in pick order onto the hand suffix, and
`max_cards` / `can_pick_zero` become the synthesized open choice's `amount` and
optional bit. What it deliberately does not claim is the **manipulation kind**:
`getHandSelectState` emits no field for it, because in the game that lives in
the action which opened the screen and the protocol serializes no actions at
all. The synthesized item therefore carries the selection shape and leaves the
kind at zero — and it exists precisely so that the concatenated hand cannot be
silently misread as an ordinary larger hand. Everything outside the model
refuses loudly: a mandatory screen holding a partial selection (the sim applies
each mandatory pick immediately and has no held-aside state to translate), more
picked than `max_cards`, a `hand` that disagrees with the combat state, a wrong
JSON type, a missing key.

Fuzz `MoveCat` 25 is its own category rather than a share of `combat_choose`,
because the thing worth counting is that the **empty** confirm is reached and a
shared counter could not tell a confirm from a toggle. The enumerator offers the
confirm from the first step and scores it equal to a toggle, so the heuristic
policies neither confirm the instant a screen opens nor pick the whole hand up
before pressing it; `COUNT` moves to 26, and a named test pins it past every
enumerator and every enumerator to a name. The reproducer round-trip covers the
new verb.

No `CombatState`, registry schema, fixture or golden vector changed: the flag
bit, the fourth choice-kind bit, the optional bit and the selected-count nibble
all fit previously-zero bits of existing words. Final-tree WSL Debug,
leak-detecting ASan/UBSan and Release are green; stale-count,
documentation-link and whitespace checks are clean.

Integration note (recorded at landing, 2026-07-27): these two rows were the
last members of B4.12's metadata-derived Living Wall COLORLESS transform pool,
so its temporary completeness-dependency row in the Deferred obligations table
is discharged and deleted in the integration commit — membership is complete
and pinned by the generated-shape tests.

<a id="b311"></a>

### B3.11 `[x]` ∥ Colorless rares
**Deps:** B3.2 · **Provenance:** cards/colorless RARE (15)
**Deliverables:** registry entries for the 15 (Apotheosis, Chrysalis, Hand of
Greed, Magnetism, Master of Strategy, Mayhem, Metamorphosis, Panache, Sadistic
Nature, Secret Technique, Secret Weapon, The Bomb, Thinking Ahead,
Transmutation, Violence — verified from source), all mandatory and live.
**Acceptance:** tier-2 per card; directed script.
**Inherited:** the recursive-play authoring (Mayhem) and per-power counter
storage (Panache, The Bomb) — both discharged here.

**Log:** Done 2026-07-27. All fifteen rares are live. Landed as four staged
commits in one worktree under the ledger's Wave-B allocation — ids 112–126 in
`addColorlessCards` alphabetical order, PowerIds 81–84, opcodes 54–57,
`ChoiceKind` 9, `ChoiceSource::DRAW` = 4 — plus the orchestrator's ledger/log
commit; serially integrated after an independent full-matrix re-run.

Stage A (Apotheosis, Master of Strategy, Sadistic Nature, Thinking Ahead).
`UPGRADE_ALL` = 54 upgrades hand, draw, discard **and exhaust** in the Java
pile order behind the shared `canUpgrade` gate (Searing Blow's always-true
override included; the played copy sits in limbo and is never self-upgraded,
`ApotheosisAction.java:25-45`). Two source-driven corrections: the native
Sadistic body predated `PowerId::SHACKLED` and lacked the Java's Shackled
exclusion (`SadisticPower.java:42`) — fixed RED-first now that Dark Shackles
makes it reachable; and the dispatch brief's Thinking Ahead premise was wrong —
`AbstractPlayer.useCard` runs `use()` **before** `hand.removeCard`
(`AbstractPlayer.java:1358-1384`), so the empty-hand put-back skip is
autoplay-only, encoded as a queue-time `hand_nonempty` guard and proven from
both directions.

Stage B (Secret Technique, Secret Weapon, Violence). `ChoiceSource::DRAW` with
a card-type filter and mandatory-1 `DRAW_TO_HAND` — the combat grid shows no
cancel button (`GridCardSelectScreen.java:446-448`), auto-takes a single match
and no-ops on zero; a `requires_draw_pile_type` `CardDef` column feeds both the
public mask and autoplay revalidation, so a Mayhem-flipped Secret card with no
match takes the no-trigger filing path. The browse's temp group bills
`cardRandomRng` exactly k−1 times (`CardGroup.addToRandomSpot` appends free on
an empty group, `CardGroup.java:463-469`); Violence adds one `shuffleRng`-fed
JDK shuffle per non-empty pick and takes the bottom card
(`DrawPileToHandAction.java:30-71`), pinned by a hand-derived seeded case.
Fix-forward with stop-the-line honors: the forced (screenless) put-on-deck path
bills one `cardRandomRng` draw per moved card (`PutOnDeckAction.java:45-53`) —
the sim billed none for Warcry; demonstrated RED, no committed fixture reaches
the path. The choice-kind flag packing was wrong for kinds ≥ 8 and is fixed
(`(kv & 0x4)`, high bit at `extra` bit 9, filter bits 10–12, latch 13) — the
groundwork B3.10c's kind 8 now builds on instead of colliding with. Java's own
shrinking-loop bug in `PutOnDeckAction`'s forced branch is reproduced, not
corrected (unreachable at every authored amount, documented at the site).

Stage C (Chrysalis, Magnetism, Mayhem, Metamorphosis, Transmutation).
`RANDOM_CARD_TO_DRAW` = 55 reproduces the Java stream order exactly — all N
pool rolls in `use()`, then N random-spot insertions as the queued actions
resolve — with permanent-for-combat zero cost on copies whose base cost was
positive (X-cost untouched), `Chrysalis.java:31-42`. `kIroncladSkillPool` is
emitted and pinned as the in-order SKILL subsequence of the combat pool, so
opcode 55's filtered view inherits the existing HashMap-order capture
obligation rather than adding one. Magnetism rolls at **hook time** — the pool
call is a constructor argument in `MagnetismPower.atStartOfTurn`
(`MagnetismPower.java:30-38`), and a queued roll would interleave with
Mayhem's own start-of-turn consumption — then queues the same
make-card-in-hand body the Java queues, spilling to discard at the hand cap.
Mayhem is the power row plus an `at_start_of_turn` body queueing
`{op: PLAY_CARD, play: [from_draw_top]}` per stack, unchanged and without
Havoc's exhaust; the one-draw random-target roll at execution matches the
verified parallel. **Declared evidence gap:** `MayhemPower.java:37` is a
CFR-unavailable anonymous class; the body is reconstructed from
`DistilledChaosPotion.java:41` and `Havoc.java:31` and the provenance string
says so. Transmutation rides the live X-cost repetition loop; opcode 52 gained
two default-0 flag bits (cost-zero-for-turn, upgraded-copy) with Jack of All
Trades' byte-identity pinned by a named test; opcode 58–59 stayed unissued.

Stage D (Panache, The Bomb, Hand of Greed) — the owner-approved schema bump,
`SCHEMA_VERSION` 5 → 6 (design §11 entry recorded). `PowerSlot` widened 4 → 8
bytes with an `int16 counter`; `CombatState` 3928 → 4696 against the 8192
ceiling, with the combat-gold accumulator occupying former padding at zero
byte cost. Fixtures were regenerated with a **generalized** proof: the old→new
transformation derived mechanically from both compiled layouts, applied to the
old bytes, required byte-identical to the regenerated files — pass over all 20
fixtures / 112 records. Panache maps the oracle-visible countdown to `amount`
and stacked damage to `counter` (`PanachePower.java:30-67`: stack adds damage
only, start-of-turn resets the counter, pure-THORNS all-enemies at zero);
The Bomb is the first **instanced** power — apply always appends, and queued
reduce/remove carry a value key rather than a slot index because compaction
makes queued indices stale (regression-tested); `DAMAGE_GREED` = 57 runs the
normal damage pipeline then banks gold on the Java fatal gate
(`GreedAction.java:32-48`; halfDead/Minion structurally inert in Act 1, cited
at the site), settled exactly once through `gain_gold` at combat fold-back.
The translator normalizes `TheBomb<digits>` through its single power-id door
and imports the reflection-emitted `damage` field into `counter` for PANACHE
and THE_BOMB only. **Recorded deviation:** fold-back settlement runs before
`settle_stolen_gold`, over-crediting a thief only when a steal preceded a
Hand of Greed kill and the purse was below the clamp — carried as a new
obligation row rather than left as a comment.

Unused contingencies fuzz `MoveCat` 26 and `CardFlag` bit 15 are released back
to free rather than gapped: bit namespaces are scarce, nothing ever encoded
them, and the permanent-gap rule's "costs nothing" rationale does not hold for
bits. Both B3.2-inherited obligation rows are discharged. Final-tree WSL
Debug, leak-detecting ASan/UBSan and Release are green twice — once per stage
agent, once by the integrating orchestrator on the final tree; stale-count,
doc-link and whitespace checks clean.

<a id="b312"></a>

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

<a id="b313"></a>

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

<a id="b314"></a>

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

<a id="b315"></a>

### B3.15 `[x]` Monsters: slavers + Looter + Fungi Beast
**Deps:** B3.12 · **Provenance:** SlaverBlue/Red.java, Looter.java,
FungiBeast.java, EntanglePower.java, SporeCloudPower.java, ThieveryPower.java,
EscapeAction.java, MonsterGroup.java, SmokeBomb.java
**Deliverables:** registry entries: entangle (Red Slaver), Looter's
gold-steal + escape (combat-end-without-death path + stolen-gold return
rules), Fungi Beast Spore Cloud (on-death Vulnerable).
**Acceptance:** tier-2 per monster; escape terminal state distinct from kill
(reward implications tested at B4.5); on-death trigger ordering.

**Log:** landed in **two deliberate halves**, which is the most useful thing
about this entry.

**Half one** — `f24b8db`, merged at `243bf63`, landed in `06c4fa0` (citation fix
in `39876f0`): monster ids 23 `SLAVER_BLUE` / 24 `SLAVER_RED` / 25
`FUNGI_BEAST`, powers `ENTANGLE` = 73 and `SPORE_CLOUD` = 74, and the **first
dispatch site for `Hook::ON_DEATH`**. All three end `takeTurn` in a queued
`RollMoveAction`, and unlike Lagavulin / Slime Boss / Hexaghost all three
`getMove` overrides **read** the rolled `num` (thresholds 40, 75/55, 60), so the
fixtures pin the roll-driven move rather than a draw count. **Entangled is a
legality predicate, not a power hook** — `AbstractCard.hasEnoughEnergy:872-875`
refuses ATTACK-type cards outright, so the veto sits in `legal_actions` beside
Normality's and Velvet Choker's, after the Medical Kit / Blue Candle escape
hatches. **Spore Cloud's ordering bites**: `die()` latches `isDying` *before*
walking powers, so with two Fungi Beasts the **first** death releases 2
Vulnerable and the **last** releases none (four named tests). `SUICIDE`
deliberately does not dispatch `ON_DEATH` (`triggerRelics == false`,
`SuicideAction.java:29-36`). `SlaverRed.usedEntangle` is the first
`MonsterState.pad0` bit-flag user — no layout change, no schema bump.

**The Looter was deliberately withheld from half one**, and that judgement is
the entry's real content. It was not "too hard": the task isolated a single
missing predicate and refused to code around it. This engine's only liveness
signal was `hp > 0`; the game's is `isDying || isEscaping`
(`MonsterGroup.java:90-95,117-122`), so an **escaped monster is alive and out of
the fight** — a state the engine could not express. There was **no alternative
seam**: `pump_step` recomputes the phase from that predicate every iteration, so
a `COMBAT_OVER` written by an opcode is overwritten on the next step. Four
obligation rows that looked like four problems were therefore **one**.

**Half two** — `a2a60df`, merged at `6809671`, landed in `20825be`. A shared
`monster_dead_or_escaped()` + `kMonsterFlagEscaped` (0x8000). **Every** "in the
fight" read was converted against its Java citation, not just the two obvious
ones: `any_monster_alive` / `queue_monsters` / the step-5 forfeit gate
(`action_queue.cpp`), AoE fan-out (`interp.cpp`), `roll_random_target`
(`card_play.cpp`), `VAMPIRE_DAMAGE_ALL` (`interp_damage.cpp`), both end-of-round
monster walks (`power_hooks.cpp`), Spore Cloud's `battle_is_ending`, and the
target grid + `fill_result` (`advance.cpp`). **No `CombatState` field and no
`SCHEMA_VERSION` bump** — the stop-the-line scenario the brief warned about did
not materialise, and all 20 committed fixtures replay unchanged.

The Looter itself is monster id 26, `PowerId` 75 `THIEVERY`, `MonsterIntent` 13
`ESCAPE`, opcode 40 `ESCAPE`. Its escape **is reachable in Act 1** — unlike
B3.16's gremlin move 99 it needs no `escapeNext()`/`deathReact()` caller; it is
simply where its own `takeTurn` machine ends (Mug, Mug, a 50/50 into Smoke Bomb
or Lunge, then Escape on turn 4 or 5). `pad0` carries slashCount, which provably
equals the steal count. Stolen gold is accrued **unclamped** and settled as
`min(count × goldAmt, gold)`, proven equal to the game's per-steal clamps
whenever the thief is the combat's only gold movement — true for every Act-1
group (at most one thief anywhere, verified against `encounters.yaml`).
**Smoke Bomb's combat body** sets the *player's* escape flag exactly as
`SmokeBomb.use` sets `isEscaping`, and the pump ends the battle with monsters
alive and untouched.

**Un-parked by construction with no `run_advance.cpp` edit** — the gate at
`:273` asks `monster_init_fn` directly. Half one un-parked Blue Slaver, Red
Slaver, 2 Fungi Beasts and Exordium Wildlife; half two un-parked Looter and
Exordium Thugs, closing that obligation entirely.

**Fixture deviation, deliberate:** 32 seeds but rows run to each seed's escape
turn rather than a fixed 20, because a solo combat *ends* there — and the row
count itself pins the 50/50 (13 smoke-bomb / 19 lunge paths).

**Discharged:** enemy self-escape + stolen-gold return; Smoke Bomb's
combat-escape body; the un-park row; and **Pantograph's boss heal**, which was
found already implemented and tested — only its stale `DEFERRED` markers needed
retiring (`39876f0`). A deferral marker on live code is conventions §8's bug
signal inverted. **Handed on:** the run-layer half of escape (outcome enum, gold
settlement, `live_target`) — see the obligations table.

**Verified:** union green on debug / asan / release at integration-16 with zero
NOT_BUILT; manifest regenerated to cards 105 / powers 44 / monsters 25 /
relics 142 / potions 33 / encounters 20 / a20 20 / total 389; no committed
fixture or golden vector modified, deleted or renamed.

<a id="b316"></a>

### B3.16 `[x]` ∥ Monsters: gremlin gang
**Deps:** B3.12 · **Provenance:** GremlinWarrior/Thief/Fat/Tsundere/
Wizard.java; MonsterHelper.java:737-765
**Deliverables:** registry entries ×5 (Angry thorns, sneaky escape?, fat
weak, shield tsundere protect logic — native where the protect targeting
doesn't fit the table shape, per design §4.2), gang spawn order from B3.12.
**Acceptance:** tier-2 per gremlin; gang composition draws already covered by
B3.12 — here per-monster behavior incl. Tsundere's block-ally logic.
**Inherited:** un-park this gang — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4.
**Log:** Verified by running, not inferred (`tools/wsl_run.sh debug asan` from
the Windows host against this worktree, both presets PASS with zero failures;
`tools/check_stale_counts.sh` and `tools/check_doc_links.sh` both clean). Landed
as commit `574ded0`, merged at `12d179e`, integrated at `5c2cd83`. `MonsterId`
**16-20** (GREMLIN_WARRIOR / THIEF / FAT / TSUNDERE / WIZARD), `PowerId::ANGRY`
= 40, and `MonsterIntent::DEFEND` = 11. Tier-2 `gremlin_test`, 19 cases.
- **Six independent 32-seed × 20-turn XS128 fixtures**
  (`tests/fixtures/gen_gremlin_fixture.py`, a second implementation of the
  decompiled trees): one per class solo, plus a **4-gremlin battery** pinning
  the Tsundere's `aiRng` block-target pick.
- **Move 99 (ESCAPE) is unreachable in Act 1 and is deliberately unmodelled.**
  `escapeNext()` has no caller anywhere in the decompiled tree, and the only
  `deathReact()` call is `BanditBear.java:131` (an Act-2 group), so the escape
  branch of every gremlin `takeTurn` cannot be reached from the Exordium-only
  registry. **Both halves are recorded for whoever adds Act 2** — the
  `EscapeAction` body and the `deathReact`/`escapeNext` trigger land together or
  not at all.
- **The Tsundere's `aliveCount` counts itself**, so the "protect an ally" branch
  and the self-fallback are keyed off a count that includes the actor.
- **Angry's `damageAmount > 0` guard reads POST-block damage**
  (`AngryPower.onAttacked`), so a **fully-blocked hit grants no Strength** —
  pinned by its own case.
- **Fat is the only gremlin queueing a real `RollMoveAction`**; the others set
  their next move through a queued `SetMoveAction`.
- **Registering the five init fns un-parked the Gremlin Gang encounter** through
  `run_advance.cpp`'s data-driven `monster_init_fn(id) == nullptr` gate — **no
  edit to that file**.
- Provenance, all read in full: `GremlinWarrior.java:45-141`,
  `GremlinThief.java:44-128`, `GremlinFat.java:48-141`,
  `GremlinTsundere.java:49-134` (+ `GainBlockRandomMonsterAction.update:27-42`),
  `GremlinWizard.java:47-154`, `MonsterHelper.spawnGremlins`
  (`MonsterHelper.java:737-765`), `AngryPower.onAttacked`
  (`AngryPower.java:34-41`), and `AbstractMonster.java:705-715,765-775,908-913`.

<a id="b317"></a>

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

<a id="b318"></a>

### B3.18 `[x]` ∥ Elites: Gremlin Nob + Sentries
**Deps:** B3.12 · **Provenance:** GremlinNob.java (:67/72/92-93/133),
Sentry.java (Artifact, Dazed insertion, alternating pattern)
**Deliverables:** registry entries with A3/A8/A18 columns; Artifact power
(debuff negation — a general power, lands here); Nob's skill-anger trigger.
**Acceptance:** tier-2: Nob Anger triggers on skill plays only (A18 column
cited); Sentry alternating moves by position; 3-Sentry spawn from B3.12.
**Inherited:** un-park these elites — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4.
**Log:** Verified by running, not inferred (`tools/wsl_run.sh debug asan` from
the Windows host, both presets PASS; the integrated union at `5f96ec4` is
**641/641 ×3** across debug, asan and release, zero NOT_BUILT lines). Landed as
commit `3ce0467`, merged at `db9f6a7`. Appended `MonsterId` 12 GREMLIN_NOB / 13
SENTRY (both `enemy_type` ELITE) and `PowerId::ANGER` = 33, with native modules
`monster_gremlin_nob.cpp` / `monster_sentry.cpp` / `powers/power_anger.cpp` and
the tier-2 `elite_test` suite (20 named cases, each confirmed present by name in
`ctest -N` under both presets).
- **Artifact needed no new row.** It landed at B3.2 as power id 4, and its whole
  effect is the DEBUFF nullify already live at the APPLY_POWER site
  (`interp/interp_powers.cpp`; `ArtifactPower.java:34-44` +
  `ApplyPowerAction.java:131-138`), so Sentry's `usePreBattleAction` only grants
  the stack and draws no RNG (`Sentry.java:79-82`).
- **Three independent 32-seed × 20-turn fixtures** pin the Nob's A18 history
  tree (`GremlinNob.java:126-170` — forced first Bellow, then a pure history
  tree: `!lastMove(2) && !lastMoveBefore(2)` → Skull Bash, else `lastTwoMoves(1)`
  → Skull Bash, else Bull Rush; the drawn num is read only by the sub-A18
  `num < 33` test at :152) and the Sentry in an **even** and an **odd** slot
  (`Sentry.java:134-150` — the first move is keyed on
  `getMonsters().monsters.lastIndexOf(this) % 2 == 0`, then strict alternation,
  so the "3 Sentries" group opens Bolt / Beam / Bolt).
- **Registering the two init fns un-parked the Gremlin Nob and 3 Sentries
  encounters as a side effect.** The parking gate is
  `monster_init_fn(id) == nullptr` — it asks the dispatch switch directly — so
  **no shared code site was edited**. The ledger's un-park obligation row is
  narrowed accordingly, not deleted: the other groups still park.
- **Sentry's `damage()` override is spine animation only**
  (`Sentry.java:115-122`), so it is recorded as an **explicit empty**
  `on_monster_damaged` case rather than left to `default:` — conventions §8: a
  `default:` there turns a missing implementation into a silent no-op.
- Ascension columns, read in full: Nob `setHp` (85,90) at A8 else (82,86)
  (`GremlinNob.java:67-71`), rushDmg 16/14 + bashDmg 8/6 at A3 (:72-80), Bellow
  applying `AngerPower(this, 3)` at A18 else 2 (:92-96), every `takeTurn` case
  ending in an unconditional `RollMoveAction` (:112); Sentry `setHp` (39,45) at
  A8 else (38,42) (:62-66), beamDmg 10/9 at A3 (:67), dazedAmt 3/2 at A18 (:68),
  Bolt queueing `MakeTempCardInDiscardAction(new Dazed(), dazedAmt)` into the
  **discard** pile (:96). `AngerPower.java:39-45` fires on SKILL only, reached
  through the monster-power stage of the `UseCardAction` fan-out
  (`UseCardAction.java:60-65` — monsters last).
- Integrated manifest at `5f96ec4`, **regenerated** by
  `tools/registry_gen/gen.py` rather than summed — the three branches' own
  claimed totals of 234 / 251 / 232 were mutually inconsistent: cards 75 /
  powers 28 / monsters 14 / relics 65 / potions 33 / events 0 / encounters 20 /
  a20 20 / **total 255**.

<a id="b319"></a>

### B3.19 `[x]` ∥ Elite: Lagavulin
**Deps:** B3.12 · **Provenance:** Lagavulin.java (:77/82/83; asleep/stun/
metallicize wake logic)
**Deliverables:** registry entry (native AI per design §4.2 budget —
sleep-wake state machine), Metallicize power, the elite `Lagavulin(true)`
variant flag.
**Acceptance:** tier-2: wakes on damage or turn 3, debuff move cadence,
A18 −2 column; asleep block gain each turn.
**Inherited:** un-park this elite — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4.
**Log:** Verified by running, not inferred (`tools/wsl_run.sh debug asan`, both
presets PASS; the integrated union at `5f96ec4` is **641/641 ×3** across debug,
asan and release). Landed as commit `8396190`, merged at `cd3e7fa`. Appended
`MonsterId` 15 LAGAVULIN (`enemy_type` ELITE) with the native sleep/wake state
machine in `monster_lagavulin.{hpp,cpp}` — move selection is native because
`Lagavulin.getMove` never reads its `rollMove` argument, while the move effects
(Siphon Soul's Dexterity-then-Strength, the Strong Attack) stay registry data
with base/A3/A8/A18 columns. Tier-2 `lagavulin_test`, 8 named cases.
- **No new power id.** Metallicize was already registry id 5 carrying exactly
  the `at_end_of_turn_pre_card` BLOCK(=stack) binding this needs, and the
  generator's **duplicate-name check caught the attempt to re-add it**. It is
  now the **first MONSTER-owned power to bind an end-of-turn hook**;
  `power_hooks.cpp` already dispatched that hook over live monsters, so the only
  change there was the row's provenance and a stale comment
  (`MetallicizePower.java:19-42`).
- `MonsterIntent` gains **SLEEP = 9** and **STUN = 10** (append-only) — the
  telegraphs set at `Lagavulin.java:144/202/225`. **Off-limits edit accepted:**
  `tools/registry_gen/stsgen/vocab.py` gained the two names because the
  generator rejects an unknown intent. That namespace is now orchestrator-
  allocated — see the ledger's shared-namespace subsection.
- **`on_monster_damaged` now carries `hp_lost`.** The wake test is
  `currentHealth != previousHealth` (`Lagavulin.java:201`), so a hit the
  sleeping armour fully absorbs must **not** wake it. The B3.17 slime split
  interrupts read only resulting state and ignore the new argument.
- **Monster block never decays in this build**, which is what makes the armour
  stand at 8 / 16 / 24: `MonsterGroup.applyPreTurnLogic`
  (`MonsterGroup.java:98-105`) has exactly one caller,
  `MonsterStartTurnAction.java:22`, and that action is never constructed.
- **The Lagavulin encounter un-parked by construction** — the gate is
  `monster_init_fn(id) == nullptr`, so registering the init fn un-parks it with
  no shared code site edited.
- Provenance (`Lagavulin.java` read in full): :54-58 move bytes, :60-66
  STRONG_ATK_DMG / DEBUFF_AMT / ARMOR_AMT, :73-100 ctor (ELITE :75, `setHp` A8
  :77-81, attackDmg A3 :82, debuff A18 :83, the `!asleep` branch :88-91),
  :102-114 `usePreBattleAction`, :116-174 `takeTurn` (the third idle is the one
  turn that queues no `RollMoveAction`), :176-195 `changeState("OPEN")` and its
  `!isDying` guard, :197-210 `damage()`, :212-227 `getMove`. Also
  `MetallicizePower.java:19-42`, `AbstractCreature.java:548-553`,
  `MonsterGroup.java:98-105,290-304`, `ReducePowerAction.java:35-53`,
  `SetMoveAction.java:52-56`,
  `AbstractMonster.java:431-437,465-491,712-715,765-775`,
  `MonsterHelper.java:439-441` (the elite `Lagavulin(true)`) and :445-447 (the
  event's `Lagavulin(false)`, implemented as `lagavulin_init_awake`).
- Two obligations handed forward to the ledger's table: `lagavulin_init_awake`
  has **no production caller** (the event that would build `Lagavulin(false)`
  does not exist), and `combat_begin` / `enter_combat` prime turn 1 with an
  end-of-round pass **the game never runs** — measured here as a sleeping
  Lagavulin holding 16 block on turn 1 instead of 8.
- Integrated manifest at `5f96ec4` (regenerated, not summed): cards 75 / powers
  28 / monsters 14 / relics 65 / potions 33 / events 0 / encounters 20 / a20 20
  / **total 255**.

> **CORRECTION APPENDED 2026-07-27 — the 8 / 16 / 24 armour claim above is
> wrong; the sleeping armour holds at 8.** This Log is append-only, so the
> original bullet stands as written and this note supersedes it.
>
> The bullet reads *"Monster block never decays in this build, which is what
> makes the armour stand at 8 / 16 / 24: `MonsterGroup.applyPreTurnLogic`
> (`MonsterGroup.java:98-105`) has exactly one caller,
> `MonsterStartTurnAction.java:22`, and that action is never constructed."* The
> premise is a **decompiler artifact**, not a fact about the game.
> `AbstractRoom.endTurn` queues `MonsterStartTurnAction` from an anonymous inner
> class CFR dropped, leaving
> `addToBottom((AbstractGameAction)new /* Unavailable Anonymous Inner Class!! */)`
> at `AbstractRoom.java:409` — which is why a grep of the `.java` tree finds
> no constructor call.
>
> Grounded in bytecode instead, cited as `AbstractRoom.endTurn (bytecode
> AbstractRoom$1, javap) -- CFR-dropped anonymous class`. `javap -c` (JDK 8,
> against the game's own `desktop-1.0.jar`, read-only — nothing from it is
> committed) shows `AbstractRoom.endTurn` at offsets 167-178 constructing
> `AbstractRoom$1` and passing it to `GameActionManager.addToBottom`, and
> `AbstractRoom$1.update()` as, in order: `addToBot(new EndTurnAction())` (0-8),
> `addToBot(new WaitAction(1.2f))` (11-21), `if (!this$0.skipMonsterTurn)
> addToBot(new MonsterStartTurnAction())` (24-42), then
> `actionManager.monsterAttacksQueued = false` (45-51).
> `MonsterStartTurnAction.update()` calls
> `AbstractDungeon.getCurrRoom().monsters.applyPreTurnLogic()` (16-22).
>
> The consequence for this task's subject: those actions drain out of `actions`
> before `GameActionManager` can reach its `!monsterAttacksQueued` branch
> (`GameActionManager.java:303-307`), so `applyPreTurnLogic` — and its
> Barricade-gated `loseBlock()` — runs at the top of the monster's turn, one
> full phase **before** `applyEndOfTurnPowers` (`:331`) lets a sleeping
> Lagavulin's Metallicize grant its 8. Each round's tick therefore **replaces**
> the last rather than adding to it, and the armour a player sees is **8 on
> every sleeping turn**. The turn-1 defect this task actually fixed —
> `combat_begin` / `enter_combat` priming turn 1 with an end-of-round pass the
> game never runs, measured as 16 block on turn 1 instead of 8 — is
> unaffected and still correct.
>
> The block clear landed 2026-07-27 as non-task work; see the ledger's
> [Landed non-task work](stage-b-tasks.md#landed-non-task-work). The pin moved
> with it: `LagavulinSleep.ArmourGrowsEightEachRoundAndStopsWhenTheShellOpens`
> is now
> `LagavulinSleep.ArmourHoldsAtEightEachRoundAndIsGoneOnceTheShellOpens`, and
> `CombatStart.LagavulinArmourTicksOncePerCompletedRound` expects 8 on each of
> turns 2 and 3 rather than 16 and 24.

<a id="b320"></a>

### B3.20 `[x]` ∥ Boss: Slime Boss
**Deps:** B3.17 · **Provenance:** SlimeBoss.java (:89/94/125), Goop Spray /
split at half
**Deliverables:** registry entry (native AI), boss split (→ L slimes at
current HP), Slimed discard-pile insertion; boss-fight terminal only when all
descendants die.
**Acceptance:** tier-2: split threshold exact, children chain to B3.17
machinery; A4/A9/A19 columns cited.
**Log:** Done 2026-07-24. Added append-only `MonsterId` 11 `SLIME_BOSS`
and `MonsterIntent::STRONG_DEBUFF` 8 without opcode, schema, or state-layout
renumbering. The A20 implementation has fixed 150 HP with no monsterHpRng draw,
one ignored opening aiRng roll, the deterministic Goop Spray → Preparing → Slam
cycle, A19 five-Slimed discard insertion, and the exact-half split interrupt.
The native split preserves the Java CannotLose → Suicide → current-HP Spike L +
Acid L → CanLose order; the B3.17 descendant positioning was completed so the
fight terminates only after all four medium-slime descendants die.

Tier-2 coverage pins the A4/A9/A19 columns, fixed-HP and opening-roll behavior,
lethal versus exact-half split thresholds, the CannotLose window, current-HP
children, the full large-to-medium chain, terminal semantics, and a public
`advance()` Goop/Prep/Slam script. Provenance read:
`SlimeBoss.java:84-107,120-160,172-191`,
`MakeTempCardInDiscardAction.java:24-31,41-50`,
`SpawnMonsterAction.java:28-59`, `SuicideAction.java:21-36`,
`CannotLoseAction`/`CanLoseAction.java:12-15`, relevant
`AbstractMonster.java` methods, and the B3.17 Acid/Spike large-slime split
sources. Integrated manifest: cards 75 / powers 27 / monsters 11 / relics 65 /
potions 33 / encounters 20 / total 231. Verified by running, not inferred, in
WSL Ubuntu-2404: focused Slime Boss **6/6**, Large Slime **13/13**, RegistryGen
**17/17**; complete debug **521/521**; leak-detecting ASan/UBSan **521/521**.

<a id="b321"></a>

### B3.21 `[x]` ∥ Boss: The Guardian
**Deps:** B3.12 · **Provenance:** TheGuardian.java (:97-107/185; mode shift
thresholds 30/35/40, Sharp Hide)
**Deliverables:** registry entry (native AI: offensive/defensive mode state
machine keyed on damage-taken threshold), Mode Shift + Sharp Hide powers.
**Acceptance:** tier-2: mode flips at the exact cumulative-damage threshold
incl. threshold growth per cycle; Sharp Hide triggers on attack plays; A19
column.
**Inherited:** un-park this boss — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4. The translator's real `act_boss`
field also waits on a boss registry (deferred by B1.5/B4.3, no owner named).
**Log:** Verified by running, not inferred (`tools/wsl_run.sh debug asan` from
the Windows host, both presets PASS; `guardian_test`'s 17 cases were confirmed
**by name** in `ctest -N` on both presets before the green was trusted —
conventions §6; `tools/check_stale_counts.sh` and `tools/check_doc_links.sh`
clean). Landed as the WIP snapshot `8b237e8` plus its acceptance commit
`741d90f` (the snapshot had never been built; it compiled and passed on the
first try, so `741d90f` is verification and coverage, not a repair, and the
snapshot was **not** amended — conventions §2), merged at `47035a0`, integrated
at `56248c5`. `MonsterId` 21 THE_GUARDIAN with the native offensive/defensive
mode machine, powers `MODE_SHIFT` = 45 / `SHARP_HIDE` = 46, and `MonsterIntent`
`DEFEND` = 11 / `ATTACK_BUFF` = 12.
- **`PowerId` 47 is deliberately unused and must never be backfilled.** It was
  reserved by this batch and not needed; a later batch filling it would be an
  id renumber in all but name (conventions §5, design §4.4). B3.8 correctly
  appended from 48.
- **The mode threshold GROWS by 10 at every Defensive-Mode entry**
  (`TheGuardian.java:61,245`) — 40, 50, 60 … at A20. The test
  `GuardianModeShift.ThreeFlipsRequireFortyThenFiftyThenSixty` **drives three
  real flips** rather than computing the third, so a `dmgThresholdIncrease`
  applied once instead of per-flip fails rather than passes.
- **Mode state is NOT derived from `ModeShiftPower.amount`.** On the return to
  Offensive Mode, `isOpen` is set **synchronously** (`:262`) while the power and
  the Reset Threshold are only **queued** (`:254-255`). A player with Thorns or
  Flame Barrier reflects into exactly that window, where Java's `dmgTaken` and
  the power's amount diverge —
  `GuardianModeShift.ThornsReflectedIntoTheReopenGapIsDiscarded` was proven red
  without the gate: the Guardian flips at 49 instead of 50.
- **Found en route and fixed: Spot Weakness silently paid out nothing against a
  telegraphed `ATTACK_BUFF`.** Fixed in `op_spot_weakness`. The provenance is
  the game's own `AbstractMonster.setMove` "SET INCORRECTLY! REPORT TO DEV" list
  (`AbstractMonster.java:451-458`), which names exactly the four ATTACK-family
  intents as the ones that must carry a baseDamage. The gap was genuinely
  invisible before: with `ATTACK_BUFF` removed from the predicate,
  `GuardianSpotWeakness.AttackBuffTelegraphPaysOutStrength` fails while the
  pre-existing `CardUncommonSkillsSpotWeakness.StrengthOnlyAgainstAttackIntents`
  stays green.

<a id="b322"></a>

### B3.22 `[x]` ∥ Boss: Hexaghost
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
**Inherited:** un-park this boss — run-created combats currently consume the B3.12
composition draws and then park; deferred by B4.4.
**Log:** Verified by running, not inferred (`STS_JOBS=4 tools/wsl_run.sh debug
asan`, both presets PASS on the same total with zero failures; the new
`hexaghost_test` target confirmed present in `ctest -N` with its 19 named cases;
`tools/check_stale_counts.sh` and `tools/check_doc_links.sh` both clean). Landed
as commit `7232d01`, merged at `d9e9f6d`, integrated at `8235477`. `MonsterId`
22 HEXAGHOST — the last Act-1 boss.
- **The orb-modelling decision the ledger asked for: the orbs are not
  entities.** `HexaghostOrb` extends nothing, never joins the `MonsterGroup` and
  is never damaged (`HexaghostOrb.java:19-59`, constructed at
  `Hexaghost.java:136-143`). The only combat-relevant residue is the **scalar
  `orbActiveCount`** (`Hexaghost.java:93`), range 0-6, read solely by `getMove`.
  It therefore needs neither monster `misc` fields nor extra powers: three spare
  `MonsterState.flags` bits hold it, a fourth holds `burnUpgraded`, and the
  Divider base takes the whole `pad0` scratch byte. **No `CombatState` field is
  added, so there is no `SCHEMA_VERSION` bump and no fixture regeneration**
  (design §4.4).
- **Divider damage = `player.currentHealth / 12 + 1`** (`:151`, Java int
  division), assigned synchronously on the ACTIVATE turn and then spent as six
  separate hits on the next.
- **`getMove` never reads its rolled `num`**, as with Lagavulin and the Slime
  Boss, so the **draw count is pinned** rather than expressed as a fixture.
- **Inferno's `BurnIncreaseAction` upgrades the Burns already in the DISCARD
  then the DRAW pile — not the hand** — and the insert happens in
  `ShowCardAndAddToDiscardEffect`'s **constructor**.
- Added **no powers, no intents** (all four telegraphs were already in the
  vocabulary, so `MonsterIntent` 13-16 stay unallocated) and **no `cards.yaml`
  edit** (BURN already carried an upgraded program).
- **All three Act-1 BOSS encounters are now live**, which discharges this task's
  share of the B4.4 un-park obligation by construction (the gate is
  `monster_init_fn(id) == nullptr`) and unblocks the translator's real
  `act_boss`.

<a id="b323"></a>

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

<a id="b324"></a>

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

<a id="b325"></a>

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

<a id="b326"></a>

### B3.26 `[x]` ∥ Relics: rares + shop
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
**Inherited:** Odd Mushroom's ×1.25 Vulnerable branch — deferred by B3.25 (Paper Phrog's
×1.75 twin retired there). RARE + SHOP `pool_order` rows for the generic five-tier
initializer — deferred by B4.6.
**Log:** Verified by running, not inferred (`tools/wsl_run.sh debug asan` from
the Windows host, both presets PASS with zero failures; the new
`relic_rares_shop_test` target was confirmed present in `ctest -N` with its 54
cases before the green was trusted). Landed as commit `860ab73`, merged at
`114c47e`, integrated at `5c2cd83`. **46 relic rows, ids 66-111**: the 28
Ironclad-obtainable RARE relics, the 17 SHOP relics, and the SPECIAL-tier Odd
Mushroom. Two power rows, both relic-granted rather than card-granted — `BUFFER`
= 28 and `INTANGIBLE` = 29.
- **RARE + SHOP `pool_order` are the canonical PRE-shuffle
  `RelicLibrary.populateRelicPool` orders, recovered by INVERTING the JDK
  shuffle** on `relicRng.randomLong` draws #3 and #4 against **ten** live
  `b14_accept` captures — all ten agree. The method is **validated, not fitted**:
  the same inversion reproduces B3.25's already-committed UNCOMMON `pool_order`
  exactly.
- **Four deferred obligations discharged.**
  - Odd Mushroom's ×1.25 Vulnerable branch (`VulnerablePower.java:61-73`), in
    `interp/interp_damage.cpp` `at_damage_receive`.
  - **Calipers** (`GameActionManager.java:353-359`) — A3.1's structural
    `has_calipers = false` in `action_queue.cpp` is now live. Barricade's branch
    was left exactly as found; it is B3.8's.
  - **Ice Cream** (`EnergyManager.java:25-40`) — the start-of-turn recharge goes
    from A4.3's unconditional SET-to-constant to the Java's two branches:
    `setEnergy(base)` by default, `addEnergy(base)` capped at 999 with Ice
    Cream. **The no-relic path stays byte-identical to the SET it replaces**,
    proven by all 20 committed fixtures replaying zero-diff, with the Stage A
    energy tests green unmodified.
  - The **RARE + SHOP `pool_order` rows** themselves.
- New engine surfaces: `RelicHook::ON_BLOCK_BROKEN` = 16
  (`AbstractCreature.brokeBlock:159-167` — monster-only, fired from
  `decrementBlock`; Hand Drill); an **`at_turn_start_post_draw` relic dispatch
  phase**, wired behind the start-of-turn `DrawCardAction`
  (`GameActionManager.java:361-362`; Pocketwatch); and **one
  `heal_player_with_relics` seam** carrying Magic Flower's
  `MathUtils.round(amount * 1.5f)`, so that **Magic Flower cannot be forgotten
  at a heal site** — Burning Blood, Blood Vial, Pantograph, Meat on the Bone,
  Blood Potion, Bird-Faced Urn and the Lizard Tail revive all route through it.
- **Prismatic Shard is a deliberate no-op, and the deliberateness is the
  point.** Its row, `canSpawn` and pool membership are **exact and live** — it
  occupies a SHOP slot and consumes a `relicRng` draw — and only the
  cross-colour reward effect is inert. Owner decision, deferred to post-S1
  multi-character support. `relic_rares_shop_test` **asserts** the inertness of
  every such row, so implementing one fails there first.
- **Three defects found by re-deriving from the Java rather than porting an
  earlier WIP snapshot.** That snapshot (1) had **Brimstone's resolution order
  backwards** — successive `addToTop` reverses, so the *last* monster resolves
  first and the player's +2 lands last; (2) rewrote `op_lose_hp` through
  `op_damage` in a way that **moved monster-death dispatch and the split seam**;
  and (3) restructured the opening turn for two relics whose bodies it left
  deferred.

<a id="b327"></a>

### B3.27 `[x]` ∥ Relics: boss (Neow pool) + event-specials
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
**Inherited:** BOSS-tier `pool_order` rows for the five-tier initializer — deferred by
B4.6; once B3.26 **and** this task land, un-defer the translator's all-tier
`relicPools` mapping (storage has existed since B4.3).
**Log:** Verified by running, not inferred (`STS_JOBS=4 tools/wsl_run.sh debug
asan`, both presets PASS with zero failures; the new `relic_boss_special_test`
target confirmed present in `ctest -N` with its 29 cases;
`tools/check_stale_counts.sh` and `tools/check_doc_links.sh` both clean).
Landed as commit `5a9a541`, merged at `13ebacf`, integrated at `8235477`.
**31 relic rows**: the 22 Ironclad-obtainable BOSS relics (ids **112-133**) and
the 9 SPECIAL-tier relics an Ironclad can obtain in Act 1 (ids **134-142**). One
power row, applied by a boss relic rather than by a card — `CONFUSION` = 59.
- **BOSS `pool_order` recovered by inverting the JDK shuffle** on
  `relicRng.randomLong` draw #5 (`AbstractDungeon.java:1237-1241`) against ten
  live `b14_accept` captures — all ten agree. **Validated, not fitted:** the
  same inversion run against draws #1-#4 reproduces the already-committed
  COMMON, UNCOMMON, RARE and SHOP orders **id-for-id**.
- **SPECIAL scope was filtered against the S1 event set**, with every
  exclusion's gate recorded on its registry row rather than left as a silent
  omission.
- **Both inherited obligations discharged.** The BOSS `pool_order` rows — every
  tier is populated now, and `relicRng` is bit-identical either way because all
  five shuffle draws were always unconditional. And the translator's **all-tier
  `relicPools` un-deferral**: storage has existed since schema v3; the blocker
  was a *complete* `relics.yaml`, because `join_relic` is fail-loud and one
  unregistered `game_id` in any of the five arrays aborts the whole translation.
  Mapped **by name**, not by the oracle's key order. Windows-host corpus run:
  **10 files, 0 drift/error, and no relic in the unknown-id tally**.
- **Snecko Eye drove a new `AT_PRE_BATTLE` relic dispatch**
  (`applyPreCombatLogic`, `AbstractPlayer.java:1885-1890`), wired ahead of
  `begin_first_turn` so Confusion lands before the opening draw. Its per-draw
  `cardRandomRng` accounting is **pinned against an independently derived
  stream**: one `cardRandomRng.random(3)` per drawn card whose cost >= 0, in
  draw order, none for an X-cost or unplayable card, and the draw happens
  *before* the `cost != newCost` compare, so an unchanged cost still spends one.
- **`gain_gold` is now the single run-layer gold door**, carrying
  `AbstractPlayer.gainGold`'s Ectoplasm suppression
  (`AbstractPlayer.java:719-737`).
- **Deliberately inert, with ASSERTED inertness** (`relic_boss_special_test`
  fails first if one is implemented): the ten `energyMaster` relics and Snecko
  Eye's hand-size bump, Slaver's Collar, Warped Tongs, and five `onEquip`
  bodies. Each is deferred **whole** rather than half-done, because every
  partial would have desynced `miscRng` or `relicRng`.


---

## Phase B4 — The run layer (Gate G6 = M3)

<a id="b41"></a>

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

<a id="b42"></a>

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

<a id="b43"></a>

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

<a id="b44"></a>

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

<a id="b46"></a>

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

<a id="b49"></a>

### B4.9 `[x]` Rest sites
**Deps:** B4.4, B3.26 (Girya/Peace Pipe/Shovel options) · **Spec:** design
§5.6 · **Provenance:** RestOption.java:25; CampfireUI.java:81-107; smith
grid flow (read at task)
**Deliverables:** rest (30 % max-HP heal, the frozen no-ascension-effect
negative), smith (upgrade grid CHOOSE), relic-added options for implemented
relics, the fixed rest row (14) + no-rest-row-13 rule already in B4.2.
**Acceptance:** tier-2: heal amount, smith upgrade writes the upgrade bit
via registry rows; option availability matrix (no upgradable cards → no
smith).
**Inherited:** Eternal Feather (rest-room heal) — deferred by B3.25. The **Fusion
Hammer** and **Coffee Dripper** campfire-option locks — deferred by B3.27 (rows live,
bodies inert).
**Log:** Done 2026-07-26 on base `fb9bf137`. `rest_sites.hpp/.cpp` and the
transient `RunController` rest screen now implement the Java-order campfire
menu: Rest, Smith, then each owned relic's added option in relic acquisition
order. Disabled options remain visible but absent from `RunActionMask`. Smith
and Peace Pipe open master-deck-indexed CHOOSE grids; Smith updates the
existing `CardInstance.upgrade` count (including repeated Searing Blow
upgrades), while Toke excludes Ascender's Bane and removes through the
master-deck removal door. Girya increments its persistent relic counter through
three, and Shovel consumes exactly one relicRng tier roll followed by the
selected pool's front pop before opening a claimable relic reward.

Rest uses the Java float expression `(int)(maxHealth * 0.3f)` with no ascension
branch, clamps at max HP, and includes the two already-live registered
campfire hooks discovered in the completion audit: Regal Pillow adds 15 to the
heal, and Dream Catcher rolls `AbstractDungeon.getRewardCards()` after healing
and opens the card-pick screen directly. Dream Catcher reuses the existing
cardRng rarity/pity, no-duplicate, upgrade-draw, Question Card, Busted Crown,
master-deck acquisition, skip, and Singing Bowl machinery; selecting or
skipping returns directly to the map rather than fabricating an outer combat
reward claim. The rest flow is controller-transient: `RunState`, schema,
fixtures, goldens, registry namespaces, and the public combat `ActionMask`
remain unchanged. The run-only mask gained fixed-cap rest-menu and master-deck
grid rows, and the deterministic fuzz controller hash includes the rest screen.

Source methods read in full: `RestOption`, `CampfireUI.initializeButtons`,
`SmithOption`, `DigOption`, `TokeOption`, `LiftOption`, all five
`Campfire*Effect` classes, `Girya`, `PeacePipe`, `Shovel`, `RegalPillow`,
`DreamCatcher`, `EternalFeather`, `FusionHammer`, `CoffeeDripper`,
`CardGroup.getUpgradableCards/getPurgeableCards/getGroupWithoutBottledCards`,
`AbstractCard.canUpgrade`, and
`AbstractDungeon.returnRandomRelic/returnRandomRelicTier/getRewardCards`.
The decisive registered-hook lines are `CampfireSleepEffect.java:48-52`
(Regal Pillow) and `:61-76` (heal/onRest then Dream Catcher reward).
`EternalFeather.onEnterRoom` (`EternalFeather.java:29-35`) remains the
task-brief-directed whole deferral because it is a room-entry heal, not a Rest
option; Fusion Hammer and Coffee Dripper locks likewise remain whole-effect
deferrals. Both obligations were explicitly re-owned to unassigned follow-ups
in the live ledger rather than left owned by completed B4.9.

Tier-2 and directed regressions cover float-floor/no-ascension healing, cap and
RNG negatives, Regal Pillow, Dream Catcher's direct pick and skip flows, menu
order/availability, no-upgradeable Smith, stable upgrade writes, Peace Pipe's
purge door, Girya's cap/persistence, Shovel's tier-roll/front-pop order,
copied-controller mask/transition determinism, and RestRoom routing. The
pre-existing B4.2 fixed rest row 14 and no-rest-row-13 tests remain green.
Complete WSL Ubuntu-2404 suites are **debug 992/992, leak-detecting ASan/UBSan
992/992, release 992/992**; stale-count and documentation-link checks pass, and
fixture/golden/registry paths are byte-unchanged.

**Independent-audit fix-forward (supersedes the fixed-cap acceptance surface
above):** the original Dream Catcher mask advertised every offered card even
when `master_deck_count == kMasterDeckCap`; the shared deck-add door then
refused the selection, turning a mask-authorized action into the fuzzer's
forbidden no-progress transition. Ordinary combat card rewards had the same
latent edge. A shared `reward_take_card_legal` predicate now owns the open-item,
card-id/index and fixed-deck-cap checks for both screens. A full deck therefore
offers only Skip and, when owned, Singing Bowl; direct Dream Catcher still
returns to the map, while an ordinary skipped CARD item returns to its outer
reward screen and leaves Proceed available.

Shovel exposed the generic relic twin: RELIC claims were always advertised,
and `claim_reward` discarded the item even when `acquire_relic` returned
`RELIC_CAP_REACHED`. `relic_acquire_legal` now models invalid ids, the fixed
relic array and Circlet's exact exception (an existing Circlet may stack at a
full array until its signed counter itself caps). Reward-mask legality reads
that authority, and `claim_reward` also checks the actual acquisition result
before removing the item. A capped dug relic remains visible but unclaimable;
Proceed explicitly abandons the already-popped reward, preserving the game's
skip path and pool semantics.

Five regressions cover generic fixed-deck card masks/forced no-ops, ordinary
relic-cap preservation, Circlet stack/counter boundaries, Dream Catcher's
full-deck Skip and Singing Bowl paths, and Shovel's capped claim plus Proceed.
No persistent state, schema, RNG sequence, fixture, golden, registry namespace
or combat `ActionMask` changed. Final-tree WSL Debug, leak-detecting ASan/UBSan
and Release are each **997/997**; hygiene checks pass.

**Defensive-legality fix-forward (supersedes the generic-mask boundary
above):** the shared claim predicate previously fell through to `true` for
`RewardItemKind::NONE` and unknown byte values even though `claim_reward`
rejected both. A hand-built reward screen could therefore advertise a
guaranteed no-progress claim. `reward_claim_legal` now explicitly accepts only
GOLD, STOLEN_GOLD, POTION, RELIC and CARDS and rejects an over-cap screen before
indexing. A second shared predicate validates the exact no-open sentinel,
screen count/storage bounds, CARD kind and offer capacity before ordinary or
Dream Catcher masks expose Take, Skip or Singing Bowl. The take, skip and bowl
mutation paths read the same authority, so forced actions against malformed
transient state are non-corrupting no-ops rather than out-of-bounds accesses or
wrong-kind consumption.

Three regressions pin NONE/unknown generic claims, ordinary malformed open-card
indices/kinds/counts, and Dream Catcher's corresponding bounds. Final-tree WSL
Debug, leak-detecting ASan/UBSan and Release are each **1000/1000**; hygiene
checks pass. The old-base task branch deliberately leaves the fuzz campaign
build id for the integration commit to advance against the then-current master
identity.

**Integration note:** landing B4.9 changes run transitions, legal-action
enumeration and fuzz coverage. The fuzzer build identity therefore advances
from `schema5-b51fix2-cardgate1` to
`schema5-b51fix2-cardgate1-rest1`; summaries from before and after rest-site
support cannot be merged.

**Post-landing empty-offer audit:** structural CARD validity now requires an
offer count in `[1, kRewardCardCap]`, both before an outer reward can be claimed
and while its pick screen is open. Zero-card ordinary and Dream Catcher
regressions pin empty masks and inert forced Take/Skip/Singing Bowl actions;
the ordinary outer screen still exposes Proceed, while a malformed open screen
does not.

<a id="b47"></a>

### B4.7 `[ ]` Treasure rooms — implementation complete, oracle acceptance blocked

**Implementation.** `treasure_rooms.hpp/.cpp` owns the non-boss Act-1 chest
transaction. Room entry constructs the chest with exactly two `treasureRng`
wrapper calls: `getRandomChest`'s 50/33/17 size roll, then
`AbstractChest.randomizeReward`'s one shared 0..99 contents roll. That single
value drives both gold presence and the size-specific 75/25/0, 35/50/15, or
0/75/25 relic tier table (trap 16). Opening is a separate CHOOSE action:
before-hooks run first, optional gold uses the exact float
`GOLD_AMT*0.9f .. GOLD_AMT*1.1f` draw and Java rounding, the pre-rolled tier
front-pops through the existing relic-pool machinery, then after-hooks run.
The resulting `RewardScreen` uses the established claim/proceed path, so a
relic is acquired only on claim while an abandoned relic stays consumed from
its pool. Proceed can also skip an unopened chest without consuming any
open-time RNG.

**Lifecycle / replay namespace.** `RunPhase::TREASURE_ROOM` is append-only value
**8**. Value **7** is explicitly reserved for the independently-developed rest
site branch; compile-time and runtime checks pin the two values distinct.
`RunController` gained only a 4-byte transient POD chest descriptor — the
frozen `RunState`, schema version, fixtures and golden data did not change.
`RunActionMask` exposes open and skip, the fuzz move enumerator can replay both,
and the whole-controller content hash includes the descriptor as its own
triage region. The fixed map row 8 is asserted at run start, and entry/open/
claim/proceed plus entry/skip transitions are covered.

**Inherited hook audit — all three discharged.** The exact
`AbstractChest.open(false)` order is
`onChestOpen` acquisition pass → optional gold → base relic →
`onChestOpenAfter` acquisition pass.

- Matryoshka decrements once per non-boss open, consumes one
  `relicRng.randomBoolean(0.75f)`, inserts its COMMON/UNCOMMON relic before the
  base relic, and goes `2→1→-2`; a third chest consumes no draw.
- Cursed Key evaluates `returnRandomCurse()` before constructing
  `ShowCardAndObtainEffect`, so it always consumes one `cardRng` identity draw.
  Omamori then decrements and blocks the card if active; otherwise the curse
  routes through `add_card_to_master_deck`, firing Ceramic Fish, Darkstone
  Periapt, Du-Vu Doll and the other established obtain hooks. Multiple imported
  Cursed Keys preserve acquisition order.
- N'loth's Mask runs after base insertion, removes the **first** relic reward
  while leaving its pool pop consumed, and goes `1→-2`. Consequently a
  Matryoshka bonus is the relic removed when both are present. All three hooks
  are explicitly no-ops for a boss chest.

The source audit corrected an inherited ledger claim: chest gold does **not**
receive Golden Idol's 25% bonus. `RewardItem.applyGoldBonus`
(`RewardItem.java:110-129`) explicitly excludes `TreasureRoom`; ordinary
chests also have no potion reward for Sozu to block. Their remaining
non-combat shares are event-screen work, not treasure behavior.

**Provenance read in full:** `AbstractDungeon.getRandomChest`
(`AbstractDungeon.java:499-508`), `AbstractChest.randomizeReward/open`
(`AbstractChest.java:54-102`), all three size constructors,
`TreasureRoom`, `Matryoshka.onChestOpen`, `CursedKey.onChestOpen`,
`NlothsMask.onChestOpenAfter`, `AbstractRoom.addRelicToRewards` /
`removeOneRelicFromRewards`, `AbstractDungeon.returnRandomCurse`,
`CardLibrary.getCurse`, `ShowCardAndObtainEffect`, `Omamori`, and
`RewardItem.applyGoldBonus/claimReward`.

**Acceptance status.** Fourteen new named cases cover every 0..99 contents
roll for all sizes, all four threshold edges, trap 16, exact stream states,
gold, all tiers, pool consumption/acquisition, hook ordering/counters/gates,
fixed-row lifecycle, fuzz enumeration and hashing. The required live-game
spot-diff of at least two treasure floors remains blocked by the unresolved
frozen oracle/capture environment already recorded for the reward work. No
sim-only expectation is being substituted for that evidence and no drift is
sanctioned; the task deliberately remains `[ ]` until those captures can run.
The complete final-tree WSL matrix is **debug 1006/1006, leak-detecting
ASan/UBSan 1006/1006, release 1006/1006**.

**Defensive fix-forward (2026-07-26; supersedes the original capacity and
descriptor handling above).** Independent review found that the chest-local
reward insertion relied on a debug assertion: an imported state with enough
active duplicate Matryoshkas could write beyond the fixed eight-item reward
array in release. Every treasure insertion is now fallible and bounds-safe.
The public before/after hook seams and the complete open transaction use
copy-then-commit semantics, so a late capacity failure rolls back earlier
Cursed Key deck hooks, pool pops, counters, every RNG stream, and the reward
list. The ordinary room preflights Matryoshka + optional gold + base relic;
an unrepresentable open is absent from the action mask and a forced action is
a byte-stable no-op.

The same fix-forward makes `treasure_chest_open_legal` the single authority
used by the action mask, direct open, and run step. It accepts only SMALL /
MEDIUM / LARGE, COMMON / UNCOMMON / RARE, canonical boolean fields, and an
unopened descriptor; the step enters `COMBAT_REWARD` only after a successful
transaction. Four named regressions cover public-hook late rollback, seven
active Matryoshkas, exact-cap success, and malformed size/tier/boolean/opened
descriptors with whole-controller byte comparisons. The task remains `[ ]`
only for the already-recorded live-oracle spot-diff blocker.

**Second fix-forward — curse capacity + derived descriptor domain.** Two P1
fixes to the open authority. `cursed_key_obtain` discarded the result of
`add_card_to_master_deck`, so a full master deck silently lost the curse while
the open still reported success and committed later RNG, pool and reward
changes; it is now fallible, `dispatch_on_chest_open_impl` propagates the
failure, and `treasure_chest_open_legal` preflights the deck slots that
acquisition-ordered Cursed Keys consume, modelling first-match Omamori lookup
and per-block charge depletion with the same walk the mutation pass performs.
Separately, `exact_unopened_chest_descriptor` tested size and tier
independently and so accepted the Cartesian product, including the
non-constructible SMALL+RARE and LARGE+COMMON; the valid pairs are now derived
at compile time by enumerating all 100×100 wrapper rolls through
`treasure_chest_for_rolls`, with `static_assert`s pinning the derived table.
Five new named cases (four capacity/rollback plus an exhaustive
generator-agreement test) and an extension of
`EveryInvalidDescriptorIsMaskAndStepAtomic` to the two impossible pairs; all
six fail against the pre-fix source.

**Third fix-forward — P3 cleanup (2026-07-26).** `open_treasure_chest` lost its
vestigial `misc_rng` parameter. It was never read: the gold roll is
`treasureRng.random(GOLD_AMT*0.9f, GOLD_AMT*1.1f)` (`AbstractChest.java:72`)
and nothing else on the open path — neither hook pass, nor `returnRandomCurse`,
nor the pool front-pop — touches `miscRng`, which is first read later at claim
time by `acquire_relic`'s onEquip bodies. Confirmed against the Java before
removal, so this is dead API, not a missing draw; the signature comment now
records why the parameter is absent. The one engine call site is inside the
branch-new `RunPhase::TREASURE_ROOM` case, which master does not have, so the
change adds nothing to the pending merge's conflict surface.

**Document conflict resolved in the same change (conventions §4).** Design
§1.1 claimed the sapphire-key reward branch "never fires in S1", citing
`AbstractChest.java:95-96` — contradicting its own paragraph, which
establishes `Settings.isFinalActAvailable` as TRUE on the frozen
fully-unlocked profile (the reason `setEmeraldElite` fires, corrected at B4.1).
`hasSapphireKey` is cleared at dungeon reset (`CardCrawlGame.java:473`) and set
only by `ObtainKeyEffect` (`ObtainKeyEffect.java:74`), unreachable in Act 1, so
both conjuncts hold and every Act-1 chest open appends a `SAPPHIRE_KEY` reward
row linked to the base relic (`AbstractRoom.java:545-547`). No RNG draw is
involved and no C++ behavior changes. §1.1 was corrected with a **§11 v0.1.6**
change-log entry; the same stale reasoning was fixed in
`include/sts/engine/combat_rewards.hpp` and `PROTOCOL.md` §3.6 (dispositions
unchanged — only the reason was wrong). B4.7's pending oracle spot-diff
acceptance now states the expected extra row **and** that the capture must
claim the base **relic**, never the key: claiming the relic marks the linked
key row `isDone`/`ignoreReward` (`RewardItem.java:298-300`), while claiming the
key does the reverse (`:317-322`) and would cost the run its relic.

The complete WSL matrix at this revision is **debug 1015/1015, leak-detecting
ASan/UBSan 1015/1015, release 1015/1015** (`ctest -N` reports 1015 total on
each preset).

<a id="b415"></a>

### B4.15 `[x]` A20 run-setup modifiers + negative freezes
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
**Inherited:** A6 / A10 / A14 run-setup modifiers — B4.4 landed A11 only
(`potion_slot_count`) and names this task their literal owner.
**Log:** Verified by running, not inferred (`tools/wsl_run.sh debug asan` from
the Windows host, both presets PASS; the integrated union at `5f96ec4` is
**641/641 ×3** across debug, asan and release). Landed as commit `d13d29e`,
merged at `372168d`. `registry/a20.yaml` goes from empty to **one row per
ascension level 1..20**, `id == level`, each carrying scope, mechanic, a
provenance citation read in this task, and an S1 status of IMPLEMENTED or
N/A-FOR-S1-with-reason — machine-checked by
`A20Manifest.EveryRowCarriesScopeProvenanceAndAnS1Status`, alongside
`A20Manifest.ExactlyOneRowPerAscensionLevelWithIdEqualToLevel`. New target
`a20_modifiers_test`, 15 named cases confirmed present via `ctest -N -R '^A20'`
in **both** presets.
- **The application order is not the one this task's Deliverables line gave.**
  It is **A11 → (A5) → A14 → A6 → A10 → starting deck**.
  `AbstractPlayer.<init>` (`AbstractPlayer.java:211-213`) runs before the
  dungeon exists, so the potion-slot loss is first; then
  `dungeonTransitionSetup` (`AbstractDungeon.java:2562-2604`) runs the
  between-act heal (:2582-2586, a no-op at full HP but positioned ahead of the
  rest), then `decreaseMaxHealth(getAscensionMaxHPLoss())` (**:2591-2593** +
  `Ironclad.java:168-170` → 5, `AbstractCreature.java:211-223` clamping current
  HP), **then** `currentHealth = MathUtils.round(maxHealth * 0.9f)`
  (**:2594-2596**). A14's max-HP loss therefore **precedes** A6's 90 %-of-max
  rewrite, and the 90 % is taken of the **already-reduced** max: an
  ascension-20 Ironclad is **68/75, not 72/75**, matching the committed G4
  oracle capture (`skeleton_sample.jsonl`, IRONCLAD, ascension 20: max_hp 75,
  current_hp 68). Pinned by
  `A20RunSetup.A6RunsAfterA14SoNinetyPercentIsOfTheReducedMax`. The ledger's
  Deliverables wording was the losing document; the correction is recorded in
  the ledger's change log.
- **A second ordering fact no document recorded:** `AbstractDungeon.<init>`
  calls `dungeonTransitionSetup` (**:287**) *before* `initializeStarterDeck`
  (**:295-296**), so `masterDeck.addToTop(new AscendersBane())` (:2597-2600)
  appends the A10 curse to an **empty** master deck — **Ascender's Bane is
  index 0, ahead of the five Strikes**, which changes the combat-start shuffle
  input. (`CardGroup.addToTop`, `CardGroup.java:455-457`, is an ArrayList
  append; `addToBottom` :459-461 is the head insert.) Pinned by
  `A20RunSetup.A10PutsAscendersBaneAtMasterDeckIndexZero`.
- **The curse routes through `add_card_to_master_deck` (`run_deck.hpp`)** — the
  sanctioned master-deck door — not the starting-deck bulk write. Its
  `onObtainCard` pass is **provably empty** at run setup (no relic is equipped
  yet at that point in `run_begin`, and the game's own call site is a raw
  `CardGroup` insert that never reaches `obtainCard`), and that is pinned by a
  named test,
  `A20RunSetup.A10CurseGoesThroughTheDoorButTheObtainPassChangesNothing`, which
  would catch a Ceramic Fish / Darkstone Periapt / Egg regression.
- **Negative freeze.** The §6 negatives have no S1 mechanism to exercise, so
  they are frozen where adding one would leave a mark: a sweep of `run_begin`
  over ascension 0..20 asserting that **only** max HP, current HP, potion slots
  and the master deck move, plus registry tests pinning the row set and each
  written no-such-modifier statement with its citation. Citations: campfire heal
  (`RestOption.java:25`), potion-drop chance (`AbstractRoom.java:580-607`),
  normal and elite combat gold (`AbstractRoom.java:324` / :316), card-reward
  rarity (`AbstractDungeon.java:1597-1603`), the Act-1 upgraded-card no-op
  (`Exordium.java:107`), the Act-3-only double boss
  (`ProceedButton.java:100-104`, `AbstractMonster.java:1058-1060`).
- Also updated: `run_advance_test`'s base-sheet and `combat_begin`-equivalence
  tests for the moved sheet (the equivalence test now levels HP explicitly, so
  it stays about the combat-construction sequence), `registry_gen_test`'s a20
  and total manifest counts, and `advance.hpp`'s now-stale note claiming the
  run-setup modifiers are not applied.
- Integrated manifest at `5f96ec4` (regenerated by `tools/registry_gen/gen.py`,
  not summed): cards 75 / powers 28 / monsters 14 / relics 65 / potions 33 /
  events 0 / encounters 20 / a20 20 / **total 255**.

<a id="card-limbo"></a>

### card-limbo `[x]` ∥ Played-card limbo / queued `UseCardAction` filing
**Deferred by:** B3.10a · **Deps:** existing `CombatState.limbo` and action
queue · **Provenance:** `GameActionManager.getNextAction`,
`AbstractPlayer.playCard` / `useCard`, `UseCardAction`, `PlayTopCardAction`,
`UnlimboAction`, `DoubleTapPower.onUseCard`, `DeepBreath.use`,
`DrawCardAction.update`, `FiendFireAction.update`, `ExhaustCardEffect.update`,
`DiscardPileToTopOfDeckAction.update`, `CorruptionPower`,
`CardGroup.moveToDiscardPile` / `moveToExhaustPile` /
`resetCardBeforeMoving`, `PerfectedStrike`, and `MummifiedHand`.

**Log:** The B3.10a obligation was real: `AbstractPlayer.useCard` queues the
card's effects first and `UseCardAction` last, then removes the source from
hand into `cardInUse`; the source is in no observable pile while its own
effects resolve. The engine instead filed it immediately. That required local
Headbutt and Deep Breath exclusions, plus a Havoc lift-out/restore
compensation, and still failed the general Shrug It Off empty-draw case.
`resolve_card_play` now enters the existing bounded `CombatState.limbo`,
queues engine-emitted `USE_CARD`, and leaves discard / exhaust / POWER /
purge-poof filing, Strange Spoon's guarded RNG draw, exhaust reset, and
`onExhaust` dispatch to that action's real queue position. No third local
compensation was added; all three old ones were removed.

- **Ordering:** `GameActionManager.getNextAction` retains `cardQueue[0]`
  through `player.useCard` and removes it afterwards. The pump now does the
  same, so Double Tap's synchronous index-1 replay is promoted ahead of a
  previously queued play. Terminal combat still halts immediately, but a
  pending `USE_CARD` is resolved exactly first: lethal `DamageAction` calls
  `clearPostCombatActions`, whose allowlist deliberately retains
  `UseCardAction`, so Strange Spoon RNG and the filing-time `onExhaust` fan-out
  remain gameplay-visible. Only a terminal-cancelled limbo card which never
  acquired a filing action takes the no-RNG/no-hook fallback. Every other
  established stranded action is preserved.
- **Independent audit findings:** a hand-played Perfected Strike counts itself
  because `calculateCardDamage` precedes `hand.removeCard`; autoplay already
  in limbo does not. Mummified Hand excludes its source through the still-live
  `cardQueue` entry, not through early hand removal. A rejected first pass also
  exposed that `CardFlag::EXHAUST` had collapsed Java's permanent `exhaust`
  with `exhaustOnUseOnce`: Havoc/Corruption could make a Spoon-saved or Exhumed
  Strike exhaust on every later play. Append-only bit 8 is now
  `EXHAUST_ON_USE_ONCE` and is consumed after the filing decision, while
  intrinsic/Medical-Kit `EXHAUST` remains. All three distinctions are pinned.
- **Namespace:** `USE_CARD` is opcode **53**. Opcodes 49–52 remain the
  exclusive live B3.10b reservation; 41–44 are permanent gaps left by landed
  owners and were not backfilled. The generated/engine opcode equality is
  compile-time pinned. `SCHEMA_VERSION` remains 5.
- **Regression diagnosis:** the inherited implementation initially exposed
  four failures. The lethal-Strike fixture found a real terminal queue
  normalization defect and was fixed in engine state without editing the
  fixture. Two Strange Spoon tests asserted synchronously before the newly
  queued filing action, and the Chemical X test counted that legitimate
  trailing action; their assertion points were corrected without weakening
  the semantic checks.
- **Acceptance:** final-tree full Debug **940/940**, leak-detecting ASan/UBSan
  **940/940**, and Release **940/940**; `FixtureOracle`, all twelve `CardLimbo`,
  and the affected `RelicRaresShop` regressions included. `git diff --check`,
  documentation link/stale-count checks, and a clean diff under
  `tests/golden/` completed the handoff. No schema bump and no committed
  fixture/golden edit.
<a id="b51"></a>

### B5.1 `[x]` ∥ Sim self-replay fuzz soak
**Deps:** B4.4 · **Spec:** design §7.1(2); stage-a §2 (replay-twice memory
guard)
**Deliverables:** `tools/fuzz/` sim-side fuzzer: random-legal + heuristic
policies (design §3.3's E0 stand-ins, implemented here) over seed sweeps;
every run replayed twice, final-state hashes compared; assert/hash-mismatch
triage output with reproducers; overnight-runnable script.
**Acceptance:** ≥ 10M actions across ≥ 10k seeds, zero nondeterminism, zero
asserts, asan-clean sample (≥ 1 % of runs under asan); numbers recorded here.
**Log:** Verified by running, not inferred. The original acceptance evidence in
`cd397c5` was superseded by the fix-forward audit below: its sanitizer prefix
overlapped the main sweep, and the runner still had integrity holes. A second
fix-forward closed the remaining strict CLI/reproducer-reader findings and
versioned the changed binary's summary identity. After both audits,
`tools/wsl_run.cmd debug asan release` from the Windows host passed
**959/959** tests in every preset. The final replacement acceptance campaign
then ran
`tools/wsl_run.cmd --script tools/fuzz/soak.sh --main-bin
build/release/tools/fuzz/fuzz_soak --asan-bin
build/asan/tools/fuzz/fuzz_soak --out
/mnt/d/STS_BG_Mod/SpeedTheSpire-campaigns/fuzz --seeds 10000 --seed-start 1
--reps 5 --asan-percent 1 --jobs 12 --asan-jobs 8 --max-actions 4000 --label
b51_final2` and exited 0. Artifacts are intentionally outside the
repository at
`D:\STS_BG_Mod\SpeedTheSpire-campaigns\fuzz\b51_final2_20260726_152823`.
- **Release sweep:** 10,000 distinct sequential run seeds, all five policies,
  five policy seeds per `(run seed, policy)` = **250,000 cases**;
  **10,808,430 counted actions** (pass A only), 21,658,338 actions including
  replay passes, 500,977 engine runs, **0 failures**, 210.0 s. The 37
  action-cap endings are the configured 4,000-action safety limit, not asserts
  or illegal states; `no_legal_moves=0`, `livelock=0`, `no_progress=0`.
- **Sanitizer sample:** the next 100 run seeds (**10,001–10,100**, disjoint
  from the main interval), with the same five policies × five policy seeds =
  **2,500 / 250,000 cases = 1.00 %**; **110,484 counted actions**, 221,246
  including replay passes, 5,010 engine runs, **0 failures**, ASan/UBSan clean.
- `fuzz_core` owns deterministic policy selection, whole-controller content
  hashing (including transient run flow without hashing `string_view`
  addresses), the A/B policy replay and sampled literal-action C pass,
  livelock/legal-action findings, coverage accounting, and strict `STSFUZZ v1`
  parsing. Thin `fuzz_soak` / `fuzz_repro` executables provide campaign and
  one-case entry points.
- Mismatch triage is exercised at the process boundary on every test run:
  `FuzzTriage.DriverWritesActionableReproducerForInjectedMismatch` injects the
  first divergence at step 3, requires the minimal four-action reproducer, and
  hands it to standalone `fuzz_repro --regen`. Abort triage separately proves
  the already-flushed case journal can regenerate a literal reproducer.
- Coverage reports distinguish counted actions from replay work, name
  termination reasons, policy/room/move traffic, combat/floor depth, run-layer
  events, registry rows seen, and every category never reached. The kv form is
  additive across stable `--shard I/N` case partitions; the executable-level
  `FuzzDriver.SeedSweepWritesAMergeableSummary` test pins sweep expansion,
  persistence, and `--merge`.
- During final audit, the progress thread was found reading worker-owned
  `Coverage` objects while they were being mutated. That driver-only data race
  was removed by publishing a single atomic counted-action scalar; engine
  behavior, schema, and fixtures are unchanged.
- Fix-forward audit closed eight acceptance defects: targeted potion
  enumeration no longer emits an untargeted illegal duplicate; discard/exhaust
  choices enumerate their full pile capacities under a proven move bound; a
  legal immediate no-op is a distinct failing/reproducer outcome; sanitizer
  seeds are a disjoint ceiling-rounded interval and reports are never added to
  main totals; versioned summaries bind build/configuration/shard identity,
  require a complete non-overlapping shard set, validate failures and checked
  arithmetic, and reject missing or incompatible fields; artifact, merge,
  output-pipe, and report-write failures propagate nonzero; and the CLI,
  summary, and `STSFUZZ v1` parsers reject partial, zero-work, overflow,
  duplicate, malformed, and trailing inputs.
- Final parser closure rejects empty or duplicate policy lists, repeated
  `--policies`, and repeated or conflicting diagnostic injection switches.
  Its popcount check independently pins policy uniqueness. Failure records
  accept the end boundary only for failure kinds that can arise after the last
  appended action, including the legitimate zero-action `NO_LEGAL_MOVES`
  case; writer-to-reader tests cover every non-`NONE` kind. Summary build ids
  end in `schema5-b51fix2`, preventing reports from this binary from merging
  with the earlier audited build.

<a id="card-dead-target"></a>

### card-dead-target `[x]` — Queued-card dead-target revalidation

**Provenance:** `GameActionManager.getNextAction`,
`AbstractCard.cardPlayable` / `canUse`, `DoubleTapPower.onUseCard`, and
`UseCardAction`.

**Log:** A post-landing `card-limbo` audit found that `resolve_card_play`
skipped `GameActionManager.getNextAction`'s dequeue-time `canUse` call
(`GameActionManager.java:209-214`). `AbstractCard.cardPlayable` rejects an
enemy-target card whose selected monster is dying before any hook or `use()`
call (`AbstractCard.java:854-859,916-924`), while the engine instead fired
hooks, counted the play and ran its program against the dead target. The fix
resolves random targeting first, then cancels a dead selected target before
all hook/counter/effect/energy work. An autoplay already in limbo receives
only Java's no-trigger `UseCardAction` filing
(`GameActionManager.java:285-301`): a normal free autoplay discards, while
Double Tap's target-preserving purge copy (`DoubleTapPower.java:43-66`) lands
in no pile. Two regressions keep a second monster alive and pin no retargeting
plus exact filing, queue, energy, RNG, Rage and Double Tap behavior. No schema,
fixture/golden, registry namespace, opcode or frozen-design change.

**Acceptance:** final-tree Debug **950/950**, leak-detecting ASan/UBSan
**950/950**, and Release **950/950**; both focused `CardLimbo` regressions and
the unchanged fixture oracle were included. Documentation links, stale-count,
whitespace and golden/fixture safety checks passed before commit.

**Independent-audit fix-forward (supersedes the implementation and acceptance
claims above):** commit `9484f70` stopped only an hp-dead selected target and
therefore did not implement Java's full `canUse` call. It also collapsed
`ENEMY` with `SELF_AND_ENEMY`, permanently zeroed autoplay `cost_now`, lost
Double Tap X-cost `energyOnUse`, and terminal-flushed a queued autoplay without
the retained no-trigger `UseCardAction`'s Spoon/onExhaust/cleanup behavior.

The corrected path shares one full in-scope `canUse` authority with
`legal_actions`: unplayable STATUS/CURSE plus Medical Kit/Blue Candle, target
and all-monsters-dead `cardPlayable`, `turnHasEnded`, affordability with the
autoplay exception, Entangle, Velvet Choker, Normality, and Clash. Generated
`CardDef` preserves the exact target kind from existing YAML, so the
successful-gate post-hook null/dead/escaping suppression applies only to
`CardTarget.ENEMY`; an ordinary card stays in hand, a limbo autoplay is removed
without filing, and `SELF_AND_ENEMY` (Spot Weakness) proceeds. A failed gate
instead queues no-trigger filing, including terminal normalization, preserving
purge/POWER, Strange Spoon, discard/exhaust, `onExhaust`, and one-shot cleanup.

Autoplay is transiently inferred from limbo and never rewrites `cost_now`.
Draw-top X autoplay reads the unchanged energy at its front-queued dequeue; a
fresh Double Tap copy that is both purge-only and X-cost stores the original
energy in its otherwise-doomed row under a transient runtime flag. No
persistent card's `misc` is overwritten, and later ordinary plays pay normally.
The expanded regressions pin ordinary-hand retention, Havoc/Wound, Normality
and Velvet replay vetoes, escaped `ENEMY` versus `SELF_AND_ENEMY`, nonterminal
and terminal Spoon/onExhaust filing, ordinary replay cost, draw-top and Double
Tap X-cost energy, and random-target draw-before-gate/no-extra-draw ordering.
The earlier 950-test acceptance count is obsolete. Final-tree Debug,
leak-detecting ASan/UBSan, and Release are each **961/961**; the unchanged
`FixtureOracle` is included, and no fixture/golden file changed.

**Integration note:** this behavior change landed after B5.1's accepted soak.
The fuzzer build identity was therefore advanced from
`schema5-b51fix2` to `schema5-b51fix2-cardgate1`; summaries produced before
and after queued-play revalidation cannot be merged.

<a id="b45-oracle-preflight"></a>

### B4.5 oracle-capture preflight hardening `[x]` (non-task)

**Scope:** prevent recurrence of an invalid live capture; this is safety
tooling, not B4.5 acceptance. The external
`D:\STS_BG_Mod\_oracle_data\campaigns\b45_rewards` directory was preserved
unchanged.

**Incident:** the campaign driver attached and completed six seeds, but every
artifact header had `oracle_block_enabled: false` and no `game_state.oracle`.
The GUI had selected stock CommunicationMod; stock and fork share
`SpireConfig("CommunicationMod", ...)`, so successful child-process attachment
did not prove the fork loaded. The header's fork hash and game/mod fields also
did not prove runtime identity: the driver hashes its configured
`--fork-jar` path and writes static frozen-version constants. An independent
launch log showed environment drift. The observed 2022/MTS 3.30.3/BaseMod
5.56.0 stack remains evidence of drift, not a sanctioned replacement for the
frozen stack.

**Fix:** `campaign_driver.py` inspects the first in-dungeon dump before opening
either JSONL sidecar or advancing policy. A missing `game_state.oracle` records
a durable `fatal_environment_drift` status with seed/attempt/detail and exits
fatal. `orchestrator.py` recognizes that status both before launch and during a
launch, kills the game if needed, writes its timeline/summary, and exits
nonzero without relaunch. `validate_artifacts.py --require-oracle` rejects a
false header or missing block and requires the B4.5 pity pair plus complete
triples for the five reward RNG streams; default validation deliberately keeps
the B1.4 non-oracle compatibility behavior. The B4.5 runbook now gates the
campaign on a separate one-seed preflight, explicit fork-only launch-log
identity, frozen version checks, deployed-jar hash, fatal-free progress, and
strict oracle validation. README no longer describes stock/GUI launch as
equivalent for an oracle campaign.

**Tests:** Windows-host stdlib `unittest`
`tools/oracle_bridge/driver/test_oracle_campaign.py` covers missing-oracle
fail-fast before artifact creation, durable fatal progress, orchestrator
no-relaunch, validator default compatibility, strict false-header/missing-block
rejection, required pity/stream rejection, and strict acceptance. Full
debug/ASan/release, stale-count, and documentation-link checks were run before
commit. No schema, fixture, golden, registry, engine, vendored fork, or external
campaign artifact changed. The frozen environment choice remains blocked on
the owner, and B4.5 stays `[!]`.

<a id="b45-oracle-preflight-fix-forward"></a>

### B4.5 oracle-capture strict campaign fix-forward `[x]` (non-task)

**Independent-review blockers:** `--require-oracle` checked every in-game
action it encountered but did not require one to exist, so
header-plus-terminal and all-menu artifacts passed. Separately,
orchestrator `--fresh` removed only progress/heartbeat/manifest. A stale
successful run and timing pair could survive a later retry-exhausted seed;
the old validator globbed the run file without joining it to the current
campaign ledger, and the orchestrator treated `complete` with failed seeds as
success.

**Fix-forward:** strict `--campaign` treats `campaign_progress.json` and
`campaign_manifest.json` as one identity contract: status must be exactly
`complete`, failures empty, current seed cleared, ordered `seeds_done` exactly
equal to the unique non-empty `seed_list`, and both ledgers must agree.
Expected conventional run and timing names are derived from that ledger and
must match the directory bijectively. Each run header is joined on campaign
id, seed, attempt, policy, schema, and fork hash; its terminal is joined to the
done outcome/floor/action summary. Each timing header is joined on campaign
id, seed, attempt, and policy. Strict per-file validation now also requires at
least one in-game action carrying a valid oracle block. Default direct-file
and campaign validation retains the historical non-oracle behavior.

The driver refuses resume when campaign id, seed list, policy, fork hash, or
schema differs and emits `complete_with_failures`/nonzero after any exhausted
seed. The orchestrator checks requested seed identity before accepting
progress and returns nonzero for incomplete or failed completion. Explicit
`--fresh` removes only known control files, numbered launch logs, and the exact
requested seeds' run/timing names; unexpected files remain visible to strict
validation. Timing headers now carry seed and attempt. The B4.5 runbook makes
every preflight and reward attempt a new timestamped, preserved campaign id.

**Regression proof:** synthetic tests cover header-plus-terminal and all-menu
strict rejection, valid strict campaign identity, missing/extra/stale and
cross-campaign files, bounded fresh cleanup, resume mismatch, non-relaunch on
failed completion, and a two-launch retry-exhaustion path with a stale valid
run/timing pair that cannot be accepted. Full preset, Python, stale-count,
documentation-link, whitespace, and golden/fixture hygiene checks were run
before the separate fix-forward commit. No state schema, engine, registry,
fixture, golden, vendored fork, or external campaign artifact changed. The
frozen environment decision remains the live B4.5 stop line.

<a id="b45-oracle-preflight-second-fix-forward"></a>

### B4.5 strict oracle evidence second fix-forward `[x]` (non-task)

**Independent-review findings:** the first strict terminal join exposed a
pre-existing live-driver counter split: boss-reward claims incremented only a
local count, so the terminal and `seeds_done` summaries disagreed for ordinary
full-run termination. The same review reproduced five more gaps. A requested
seed mismatch was logged but accepted; duplicate/out-of-order terminals and
actions plus missing summaries could pass; a rooted or `..` campaign id let
`--fresh` escape the data root; timing validation ignored everything after its
header; and fork/schema resume mismatch was not durable or checked by the
orchestrator before accepting completed progress.

**Fix-forward:** boss-reward claim actions now return their updated count and
write corresponding timing marks, including the no-op recovery path. The
terminal marker remains an explicitly non-injected final-state observation:
action sequence is contiguous, terminal `seq` counts every action-shaped
record, while terminal/progress `actions` and timing marks count only injected
commands. A wrong dump seed is fatal before artifact creation or policy
advancement. Strict validation derives the long from the filename/header string
and requires header long/getLong/crosscheck, every in-game `game_state.seed`,
and every `oracle.seed` to agree.

Strict run grammar now requires exactly one final terminal, contiguous action
sequence, required terminal and done summaries, and consistent sequence/action
counts. Timing JSONL is parsed completely: one first header, marks only
thereafter, valid required fields and monotonic clocks, contiguous sequence,
and exact command/floor/screen correspondence to every injected artifact
action. Timing identity now includes campaign, seed, attempt, policy, schema,
driver, and fork hash.

The shared `campaign_paths.py` contract accepts only single-component campaign
ids and base-35-style seed names. Every driver/orchestrator campaign write and
delete resolves through absolute/real containment below `data_root`; rooted
paths, separators, dot segments, symlink escapes, and unsafe direct child names
fail closed. Resume identity mismatch is persisted as a non-retryable fatal
status. The orchestrator hashes the currently requested fork and joins
campaign/seed/policy/fork/schema before accepting even completed progress, and
any `complete_with_failures` status is nonzero regardless of list contents.

**Regression proof:** the Python suite now directly covers the normal
boss-reward strict campaign, wrong requested/dump/header/oracle seeds, duplicate
terminals, actions after terminal, sequence/count drift, missing terminal/done
summaries, malformed/duplicate/missing/drifted timing rows, path traversal at
both CLIs and cleanup, durable resume mismatch, current-fork completed-progress
rejection, and empty-list `complete_with_failures`. The historical default
validator remains compatible for non-strict artifacts. Full preset and hygiene
results are recorded by the separate fix-forward commit; no engine schema,
registry, fixture, golden, vendored fork, or external campaign artifact changed.
B4.5 remains `[!]` on the manual oracle capture and frozen-environment decision.

<a id="b45-oracle-preflight-redirect-fix-forward"></a>

### B4.5 redirected-child cleanup fix-forward `[x]` (non-task)

**Supersession:** independent post-review found that containment alone was not
enough: an expected direct-child name symlinked to unexpected evidence *inside*
the campaign passed the root check, and `--fresh` deleted the resolved target.
Campaign paths now retain the exact lexical child and reject every existing
symlink, junction, or Windows reparse redirect before cleanup, progress
temp/replace, heartbeat, run/timing, manifest, launch-log, summary, or strict
validation access. The cross-platform regression creates a real file symlink
when supported and proves cleanup, orchestration, strict validation, and direct
progress resume all fail explicitly while the link and target evidence survive.
Normal bounded cleanup and non-strict historical validation remain unchanged.
Verified on the Windows host: Python 34/34 and WSL Debug, ASan/UBSan, and
Release 980/980 each; stale-count, documentation-link, whitespace, and
golden/fixture/registry hygiene are clean.

<a id="b45-oracle-stack-repin"></a>

### B4.5 oracle runtime re-pin `[x]` (non-task) — 2026-07-26

**Owner decision, executed under conventions §4.** The frozen oracle runtime is
amended to **Slay the Spire `12-18-2022` (`[V2.3.4]`), ModTheSpire `3.30.3`,
BaseMod `5.56.0`**, replacing `11-30-2020` / `3.18.1` / unversioned. Frozen text
fixed inline in design §1.2 (and §2.4, §2.5, which restated it) and recorded in
that document's change log as **§11 v0.1.7**, following the v0.1.3 / v0.1.6 form.

**Nothing about the runtime changed — the label was wrong from the start.**
Design §1.2 *inferred* the game's patch date from CommunicationMod's declared
`sts_version`, i.e. read a property of upstream's mod manifest as a property of
this install. `CardCrawlGame.VERSION_NUM = "[V2.3.4] (12-18-2022)"`
(CardCrawlGame.java:125-126) in `D:\STS_BG_Mod\SlayTheSpireDecompiled` — the
canonical Java every `File.java:line` citation in these docs resolves against,
decompiled from *this install's* `sts-classes.jar` — shows the decompiled spec
and the captured runtime are the same build, and always were. No `11-30-2020`
build was ever installed. This retires the residual risk an amendment would
otherwise carry: there is no second build, and no cited line number moves.

**No prior entry in this archive is rewritten, and none needed to be.** The G4
gate corpus `b13_on20b`, B1.2 stream verification, the B1.3 strip-equivalence
A/B, B4.1's map goldens and the `oracle_gate_check` pass all stand as recorded.
The fork-hash citations at `:300`, `:587`, `:632`, `:635`, `:2374` and the G4
line in the ledger are **historical**: `04477E4E…B2C36636` genuinely was the
artifact hash when each was written, and that audit trail is what made the drift
discoverable. Likewise `tests/golden/map_paths/oracle_maps.txt` and
`tests/map_gen_test.cpp` correctly record which fork build produced their
goldens. Only forward-looking statements of the *current requirement* moved.

**Re-pinned artifacts.** `communicationmod-oracle/.../ModTheSpire.json` now
declares `sts_version 12-18-2022` / `mts_version 3.30.3`. That file is a runtime
manifest, not metadata, and the two fields differ: read out of the installed
`ModTheSpire.jar` with `javap`, `mts_version` is deserialized as a **semver** and
enforced as a hard *minimum* by both `Patcher.initializeMods` and
`Patcher.sideloadMods` (`ERROR: … requires ModTheSpire v… or greater!` plus a
modal `JOptionPane`, which under `--skip-launcher` is a hang), while
`sts_version` is only a `ModPanel` mod-select warning string that
`--skip-launcher` never reaches. So the change is load-inert against the
installed stack and additionally makes an MTS *downgrade* fail loudly. Recorded
in `PROTOCOL.md` §0.1, which keeps the four upstream-provenance rows verbatim —
they are true statements about upstream's artifact and were not edited.
Rebuilding gave sha256
**`7DC814AD240CBBD9100B2E8C92B6AA97B4ADFBED62FFED7961C6E5DE15884733`**,
`determinism: PASS`. Built with `-NoDeploy`: **the game install was not
written to, and the fork must be redeployed before the next capture.**

**The defect that hid the drift, fixed.** `campaign_driver.py` stamped
`GAME_STS_VERSION` / `GAME_MTS_VERSION` into every artifact header as static
constants, making headers unfalsifiable on precisely the field the decision
turned on. The driver now parses ModTheSpire's `Version Info:` / `Mod list:`
block out of the campaign's own highest-numbered `mts_launch<N>.log`, writes the
**observed** values plus a `version_source` and a `mods_loaded` map into the
header, and refuses through the existing `fatal_environment_drift` status with a
distinguishing `kind`: `stack_version_mismatch` (wrong versions, stock
`CommunicationMod` loaded beside the fork, or the fork absent),
`stack_unparseable`, or `stack_unobservable` (no launch log — the GUI-launch
case). The sanctioned values are now only ever *compared* against the log, never
copied into a header, and a test pins that property by patching the constants to
sentinels and requiring the header still to report the log.

The highest-numbered selection described above is the behavior that landed in
`51e8199`; independent review then proved it could select a stale log after a
non-fresh orchestrator-process resume. The separate fix-forward below preserves
this entry as the original acceptance record and supersedes that selection rule
with an exact per-launch binding.

**Retroactive check over the 15 preserved campaign directories:** all 12 real
captures parse as `12-18-2022 / 3.30.3 / 5.56.0` and pass; `b13_pilot_scripts`
and `b13_scripts20` are `--script-dir` inputs, not campaigns; and the only
campaign flagged is `b45_rewards` — the one already known to be invalid, caught
as `stack_unobservable` because its GUI launch wrote no log. The new check would
have refused it at capture time.

**Acceptance:** Python `test_oracle_campaign` **47/47** on the Windows host
(34 before, 13 added); WSL `debug`/`asan`/`release` green; `check_stale_counts.sh`,
`check_doc_links.sh` and `git diff --check` clean;
`build_fork.ps1 -CheckDeterminism -NoDeploy` reports `determinism: PASS`.
**B4.5 and B4.7 both remain open** — this clears the *environment* blocker only;
the live capture, which a human must launch, is still outstanding.

<a id="b45-oracle-stack-repin-fix-forward"></a>

### B4.5 runtime re-pin launch-binding fix-forward `[x]` (non-task)

**Independent-review blocker:** the re-pin driver chose the highest-numbered
`mts_launch<N>.log`, but `orchestrator.py` restarted its local launch counter at
zero on every process invocation and opened `mts_launch1.log` with truncating
mode. A non-fresh resume after a prior process had written logs 1–3 therefore
made the new child read stale log 3 while current log 1 was being rewritten.
The same persisted CommunicationMod config let a later GUI launch reuse the
stale valid log. Both paths defeated the capture-time observation contract and
could stamp a sanctioned header for a process the log did not describe.

**Fix-forward:** log indices now resume numerically above every preserved log,
each log is created exclusively and never overwritten, and the driver command
names that exact direct-child log. A one-use binding nonce is also inherited
through the orchestrator-launched game process; it is a process-identity nonce,
not a credential, and is omitted from diagnostics. A GUI launch inherits the
persisted command but not the nonce, so it refuses as `stack_unobservable`.
Existing symlinks, junctions, or reparse redirects at an owned-looking log name
fail before read or allocation. The artifact header still contains only the
observed version block and its source filename, never the sanctioned constants.
An in-progress ledger also binds `driver_version`, so this capture-logic
fix-forward cannot silently resume into artifacts produced by the prior driver.

**Regression proof:** the Python campaign suite covers the original non-fresh
stale-higher-log reproducer, GUI/persisted-config reuse without the inherited
nonce, redirected owned-looking logs with the target preserved, numeric
resume to the next append-only index, omission of the nonce from orchestrator
diagnostics, and a corrected sentinel regression that actually writes a header
while the sanctioned constants are patched. The two remaining forward source
comments that still called the decompile `11-30-2020` now name `12-18-2022`;
upstream manifests, archived capture headers, gate evidence, and golden fork
hashes remain untouched.

**Acceptance:** Windows-host Python `test_oracle_campaign` **51/51** and
`py_compile` clean; WSL Debug, leak-detecting ASan/UBSan, and Release each
**1039/1039**; stale-count, documentation-link, and whitespace checks clean.
`build_fork.ps1 -CheckDeterminism -NoDeploy` remains byte-identical at
**`7DC814AD240CBBD9100B2E8C92B6AA97B4ADFBED62FFED7961C6E5DE15884733`**.
The deployed game jar remains the old `04477E4E…B2C36636` build: no Steam/game
installation file was written. No schema, generated registry data, fixture,
golden, or external campaign artifact changed. B4.5 and B4.7 remain open on
their manual live captures.
<a id="b410"></a>

### B4.10 `[x]` Event framework + ?-room resolution
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
**Inherited:** the ?-room `eventRng` duplicate roll — deferred by B4.4. Un-defer the
translator's `eventList`/`shrineList`/`specialOneTimeEventList` membership bitsets
(RunState storage has existed since B4.3) — deferred by B1.5/B4.3, jointly with
B4.11-B4.13's `events.yaml` rows and the canonical list order. The event-screen half
of B3.27's inert boss/special relics (**Golden Idol** ×1.25 gold, **Sozu**'s potion
block, **Sacred Bark** potency, and the five deferred `onEquip` bodies — Pandora's
Box, Tiny House, Astrolabe, Empty Cage, Calling Bell) — deferred by B3.27, each
deferred whole because a partial would desync `miscRng` or `relicRng`.
**Log:** Done 2026-07-26 on task base `0b0a5c6`; registry-first commit
`1d015bf` added append-only `events.yaml` identities 1–31 in the three
canonical Java list orders. `event_framework.hpp/.cpp` owns the complete
framework transaction. Entering a ? commits exactly one `eventRng.nextFloat`
for `EventHelper.roll`; selection reconstructs a post-roll duplicate, consumes
the shrine split/index only on that local copy, and discards it. Named tests
pin byte-identical selection-stream state, the asymmetric 100-slot clamp, the
20-room float-exact trap-19 pity sequence, Tiny Chest's after-draw `==4`
force (including duplicate-import first-instance semantics), Juzu's ordering,
and leaving-shop suppression.

The three B4.3 membership bitsets now initialize in canonical Act-1 order,
including the profile-dependent NoteForYourself gate frozen to the audited
profile. Event and shrine candidates are rebuilt at draw time with their
floor/gold/curse/act gates; selection removes the persistent pool bit and sets
the cumulative `event_flags` identity bit. The translator maps all three
oracle remaining-list arrays through generated `game_id` joins, rejects
wrong-pool, duplicate and out-of-order content, tallies explicit tolerant-mode
unknowns without inventing an identity, and derives fired flags from removed
members. The A20 NoteForYourself absence is treated as never initialized, not
as fired.

The full run route now resolves ? into the real monster, shop, treasure or
event path. A resolved monster consumes the dynamic monster-list cursor rather
than rereading the static map room; shop/treasure reuse their existing
transitions. `AbstractRelic.onEnterRoom` runs against the original EventRoom
before replacement, so every held Ssserpent Head gains 50 and every unused Maw
Bank gains 12 through the Ectoplasm-aware gold door before roll/selection; a
low-gold Cleric regression proves Maw Bank changes eligibility before the
event draw. Maw Bank's other-room share, Meal Ticket's static and ?→Shop
entry heal, and event-created combat lifecycle/reward semantics are explicitly
re-owned in the live Deferred obligations table.

`RunPhase::EVENT_DIALOG = 9`, `MoveCat::EVENT_OPTION = 23`, and
`MoveCat::COUNT = 24` claim their reserved namespaces. The transient
`EventDialogState` lives in `RunController`; callbacks receive the controller
so native bodies can own floor RNG, reward/grid state and immediate
transitions, and `TRANSITIONED` prevents the framework from overwriting a
body-installed phase. A synthetic two-screen proof body pins conditional
options, disabled/invalid no-ops, ordinary completion and body-owned
transition. Fuzz enumeration emits only enabled choices under the new category,
the controller hash covers every event field, and the incompatible fuzz build
identity advances through `rest1-treasure1-event1`. All 31 native event bodies
remain deliberately null and park only after exact selection bookkeeping;
B4.11-B4.13 own them and transient event-screen translation.

Oracle acceptance uses compact post-roll triples copied from the preserved
`b14_accept2` live campaign: `STS00004` selects Scrap Ooze,
`STS00007` Living Wall, and `STS00008` Big Fish. Each regression pins the
selected identity, exact membership removal, fired bit and unchanged selection
stream. Source methods read in full include `EventHelper.roll/resetProbabilities`,
`EventRoom.onPlayerEntry`, `AbstractDungeon.nextRoomTransition/generateRoom/
generateEvent/getShrine/getEvent/initializeSpecialOneTimeEventList/
isNoteForYourselfAvailable`, Exordium's event/shrine initializers,
`Random(Long,int)`, `TinyChest`, `JuzuBracelet`, `SsserpentHead.onEnterRoom`,
`MawBank.onEnterRoom/onSpendGold`, `AbstractPlayer.gainGold/loseGold/isCursed`,
and `RelicLibrary.getRelic`.

Final-tree WSL Debug, leak-detecting ASan/UBSan and Release are each
**1083/1083**. A proportionate self-replay smoke soak is also clean: Release
500 cases / 23,046 counted actions and the disjoint ASan sample 5 cases / 144
counted actions, both with zero failures. `event_option` is intentionally
never production-legal in that soak while all native bodies remain parked;
its enumeration/hash/transition surface is covered by the synthetic directed
tests. Documentation links, stale-count and whitespace checks pass.
No `RunState`/`CombatState` schema, fixture, golden, or external oracle
artifact changed.

<a id="b411"></a>

### B4.11 `[x]` Exordium events I
**Deps:** B4.10 · **Provenance:** events/exordium: Big Fish, The Cleric,
Dead Adventurer, Golden Idol, Golden Wing, World of Goop (each read in full;
A15 branches per event)
**Deliverables:** the 6 events as native logic + `events.yaml` metadata
(conditions, option tables, A15 columns); Dead Adventurer's escalating
encounter, Golden Idol's relic+curse branches.
**Acceptance:** tier-2 per event (every option's state delta, A15 variants);
directed script per event.
**Inherited (shared with B4.12/B4.13, whichever first builds an event claim
screen):** the B3.27 event-screen relic shares — Golden Idol ×1.25 gold,
Sozu's potion block, Sacred Bark potency, and the five deferred `onEquip`
bodies (Pandora's Box, Tiny House, Astrolabe, Empty Cage, Calling Bell) —
see the Deferred obligations row. Event-created combats must stay Event-room
combats: they do not advance the ordinary monster-list cursor, and generic
combat reward assembly must not overwrite their event-defined rewards.
**Log:** Done 2026-07-26 from task base `27ed040`; registry-first commit
`b730b4e` added audited `implemented`, conditions, option screens and explicit
A15 metadata to the six rows, plus generated `EventDef` metadata and
`STS_REGISTRY_NATIVE_EVENTS`. Expanding that generated table declares and
dispatches every implemented handler, so marking a row implemented without
linking its native body fails the build rather than silently parking.

`events/exordium_events_i.cpp` implements all six Java dialog trees. Big Fish
uses integer one-third healing, `increaseMaxHp(5,true)`, and the
Regret-before-screenless-relic branch. The Cleric pins 35-gold quarter healing,
the 50/75 purify split and the no-purgeable/no-charge branch. Golden Idol pins
the fixed relic/Circlet duplicate path, Injury obtain door, 25/35 percent
damage and 8/10 percent max-HP loss. Golden Wing keeps the Java two-step
damage→prompt→purge flow and reads the master-deck cards' live upgraded base
damage for its ≥10 gate; the legacy starter-card rows whose generated upgraded
program still mirrors base are bridged explicitly for upgraded Bash and Pommel
Strike. World of Goop consumes its 20–50 / 35–75 constructor draw, clamps the
displayed loss to held gold, and preserves damage-before-gain ordering.

Dead Adventurer consumes one `miscRng.randomLong` into the exact JDK shuffle,
then one enemy roll; packs the shuffled reward order plus collected count in
its transient event state; ramps 25/35→50/60→75/85; pays successful
GOLD/NOTHING/RELIC results immediately; and, on failure, pre-seeds the random
25–35 gold then the uncollected suffix before entering 3 Sentries, Gremlin Nob
or awake Lagavulin. Focused tests independently reproduce the shuffle and all
three immediate reward kinds, and pin gold-row merging plus Golden Idol bonus
recomputation.

The shared `enter_event_combat` seam preserves all five already-advanced floor
streams, retains `RoomType::Event`, and supports the existing
`lagavulin_init_awake` production variant. Event battle-over assembly preserves
pre-seeded rows, appends the ordinary event potion roll and exactly one card
reward, adds no ordinary-room gold or Prayer Wheel row, and exits without
advancing `monster_cursor`. The reusable event master-deck grid exposes common
purge/upgrade/transformable legality and mutation doors while keeping
continuation in the owning event body; Cleric and Golden Wing use its purge
path now.

The translator's storage-less EVENT screen pass now registry-joins `event_id`
and type-checks option `disabled` and `choice_index`. Fuzzing adds the
append-only `EVENT_GRID = 24` category, enumerates only legal deck rows, hashes
the repurposed `EventDialogState::grid_kind` byte, and advances the incompatible
build identity to `eventgrid1`. The `EventDialogState` size remains 8 bytes and
the frozen `RunState`/`CombatState` schema is unchanged.

Independent review fixed two gameplay/integrity defects before integration.
The run-layer NORMAL event-damage transaction now follows the already-live
Torii → Tungsten Rod → lethal Lizard Tail portions of
`AbstractPlayer.damage`, including the owner-null distinction and the
EventRoom-phase rule that keeps Magic Flower from amplifying Lizard Tail's
revive. The generated `event_def` lookup now scans by identity instead of
dense-indexing `id - 1`, preserving the repository-wide rule that registry ids
may contain legal append-only gaps. Named regressions cover both changes; the
shared `MoveCat` ledger allocation and all six native bodies' Java method
citations were corrected in the same fix-forward.

Tier-2 coverage includes every dialog option, all shuffled Dead Adventurer
reward kinds and combats, all A15-changing constants, generic event combat
stream/reward/cursor semantics, awake Lagavulin, EVENT translation failures,
and fuzz enumeration/hash coverage. Final-tree WSL Debug, leak-detecting
ASan/UBSan and Release are each **1103/1103**; documentation links,
stale-count and whitespace checks are clean. No Steam/game deployment, fixture,
golden, external oracle artifact or schema changed.

<a id="b412"></a>

### B4.12 `[x]` Exordium events II

Done 2026-07-26 from task base `edfbce8d718cec7aebb4278e2e123e73d3159258`.
All five remaining Exordium event-list bodies are native and
generated-dispatch linked, with audited conditions, option screens and
explicit A15 metadata.

Liars Game retains the intro, first Agree confirmation, payout page and final
proceed as distinct states. Payout obtains Doubt before granting 175/150 gold.
The shared obtain door now implements `ShowCardAndObtainEffect`'s
first-owned-Omamori behavior, including the used-up-first-copy case; Ectoplasm
suppresses gold independently. Cursed Key now uses the same door without
changing its capacity preflight.

Living Wall exposes arbitrary-card Forget, Change and Grow grids. Change
removes the selected card first, consumes exactly one inclusive `miscRng`
draw from the same-color pool excluding that card identity, then obtains the
replacement through normal hooks. Generated RED, COLORLESS and CURSE
transform pools derive from registry metadata and self-complete as mandatory
colorless rows land; no colorless card is excluded or deferred.

Mushrooms registers the fixed three-Fungi EVENT encounter. Fight confirmation
draws `random(20,30)` gold before combat, pre-seeds GOLD then Odd
Mushroom/Circlet, preserves advanced floor streams and leaves the ordinary
monster cursor untouched; battle-over handling can add the ordinary potion
row and then adds exactly one card row. The alternate option heals one quarter
of max HP before obtaining Parasite.

Scrap Ooze applies current null-owner NORMAL damage before its roll, starts at
the displayed 25 percent chance (`random(99) >= 74`, 26 winning values), and
adds 10 percentage points plus one damage on failure. Its roll and relic
acquisition still occur after lethal damage, matching Java action ordering.
Shining Light applies player-owner NORMAL damage, always consumes one
`randomLong` for the exact JDK shuffle, and upgrades the first two shuffled
eligible cards even after lethal damage.

Tier-2 coverage exercises every option, changing A15 constants, exact RNG
boundaries and ordering, arbitrary grid selections, Omamori/Ectoplasm and
obtain hooks, event-combat rewards/streams/cursor behavior, and post-lethal
continuations. The focused suite and final WSL Debug, leak-detecting
ASan/UBSan and Release presets pass. Registry generation, documentation links,
stale-count and whitespace checks pass. No translator or fuzz update was
needed because the existing EVENT option/grid representation covers every new
screen; no schema, fixture, golden or external oracle artifact changed.

Java provenance: `Sssserpent.java:41-81`, `LivingWall.java:34-104`,
`Mushrooms.java:45-100`, `ScrapOoze.java:37-96`,
`ShiningLight.java:45-112`, `AbstractDungeon.java:852-878`,
`CardGroup.java:498-506`, `ShowCardAndObtainEffect.java:30-82`,
`Omamori.java:38-46`, and `MonsterHelper.java:389-600`.

<a id="b413"></a>

### B4.13 `[ ]` Shrines + one-time specials — code landed; blocked on the manual oracle capture

Done 2026-07-27 from task base `78a6a9e4ab5e51f8d165399cd4c016da52a0621e`. All
six Exordium shrines and all eight Act-1-reachable one-time specials are native
bodies with audited registry metadata and generated dispatch; the ledger entry
stays unchecked because its Acceptance names a Match-and-Keep dealing
spot-check against the live game, which cannot run here.

**Which specials are reachable, and the evidence for the six that are not.**
The authority is `AbstractDungeon.getShrine`'s per-key switch
(`AbstractDungeon.java:1886-1936`), evaluated against `id.equals("Exordium")`.
(The task brief cited `:1949-1980` for these gates; that range is
`getEvent`'s filter over the eleven ordinary *events* and does not mention a
single special. The ledger entry now carries the correction.) Eight rows enter
an Act-1 draw list and are implemented — Accursed Blacksmith, Bonfire
Elementals, Lab and WeMeetAgain fall through to the unconditional
`tmp.add(e)` at `:1935`; FaceTrader passes on `TheCity` **or** `Exordium`
(`:1904-1908`); Fountain of Cleansing needs `player.isCursed` (`:1889-1893`);
The Woman in Blue needs `gold >= 50` and carries **no act test at all**
(`:1924-1928`); and NoteForYourself is unconditional once
`isNoteForYourselfAvailable` has put it in the list at all (`:1351-1353`).
Six are excluded, each by an act gate that Act 1 cannot satisfy:

| Excluded | Gate (AbstractDungeon.java) | Why Act 1 fails it |
|---|---|---|
| Designer | `(TheCity or TheBeyond) and gold >= 75` (:1894-1898) | act gate; the gold half is irrelevant in Act 1 |
| Duplicator | `TheCity or TheBeyond` (:1899-1903) | act gate only |
| Knowing Skull | `TheCity and currentHealth > 12` (:1909-1913) | act gate |
| N'loth | `TheCity and relics.size() >= 2` (:1914-1918) | act gate |
| SecretPortal | `playtime >= 800s and TheBeyond` (:1929-1933) | act gate, **and** a wall-clock playtime the engine does not model |
| The Joust | `TheCity and gold >= 50` (:1919-1923) | act gate |

Two of those correct the brief's candidate list. **Duplicator is not Act-1
reachable** — it is `TheCity`/`TheBeyond` only. **N'loth is not either**: its
condition decompiles as `!id.equals("TheCity") && !id.equals("TheCity")`,
which is one test duplicated by the decompiler rather than a two-act
disjunction, so it is TheCity-only and the brief's "N'loth?" resolves to *no*.
Conversely **FaceTrader, which the brief did not name, IS reachable**, because
its gate is the `TheCity` **or** `Exordium` disjunction. All of this is
machine-checked rather than asserted: `one_time_specials_test` builds an Act-1
state that satisfies every non-act condition in the table (500 gold, 70 HP, a
cursed deck, two relics) and shows the six still absent from
`build_shrine_pool`, then walks the same state into acts 2 and 3 and shows
each reappearing — including FaceTrader's asymmetry, present in acts 1 and 2
and gone in act 3, and SecretPortal staying out even in TheBeyond because the
playtime half is unmodelled.

**RNG attribution, read per event rather than assumed.** It is not uniform,
and three streams are involved. Transmorgrifier bills **miscRng**
(`transformCard(c, false, AbstractDungeon.miscRng)`, `Transmogrifier.java:49`)
and is byte-identical to Living Wall's Change, so it reuses the shared
transform door. The Wheel of Change bills one `miscRng.random(0, 5)` for the
spin (`GremlinWheelGame.java:229`) and then **relicRng** through
`returnRandomScreenlessRelic` on the relic result; the
`MathUtils.random(-10, 10)` beside it is libGDX's global generator driving the
stop angle and is not a game stream. Match and Keep's constructor spends all
three: **cardRng** for the three `getCard(rarity)` pool reads and for every
`returnRandomCurse` (`CardLibrary.getCurse`, `CardLibrary.java:1022-1029`),
**shuffleRng** for `returnColorlessCard`'s `randomLong`
(`AbstractDungeon.java:1101`), and **miscRng** for the twelve-card board
shuffle. FaceTrader spends one `miscRng.randomLong` on the face shuffle and no
pool draw at all — the five faces are a hand-written list. We Meet Again
spends miscRng **three times and conditionally**: a `randomLong` for the
potion pick only when a potion is held, `random(50, min(gold, 150))` only at
50+ gold, and a `randomLong` for the card pick only when the deck holds a
non-BASIC non-CURSE card. Lab and The Woman in Blue bill **potionRng** through
`PotionHelper.getRandomPotion` — a **flat** draw over the 33-entry list, not
`returnRandomPotion`'s rarity-gated rejection sampling, which is a different
method and a different draw count. Golden Shrine, Purifier, Upgrade Shrine,
Accursed Blacksmith, Bonfire Elementals, Fountain of Cleansing and
NoteForYourself consume no stream at all, and tests assert the counters do not
move rather than leaving that implicit. `cardRandomRng` is billed by nothing
in this batch.

**Bodies.** Golden Shrine pays 100 (50 at A15) on Pray and, on Desecrate,
gains 275 gold **before** obtaining Regret — the opposite order to Liars Game,
so each is written the way its own Java writes it. Transmorgrifier, Purifier
and Upgrade Shrine are the reusable event grid's transform / purge / upgrade
doors with the Java's own greying-out (Upgrade Shrine's first button is
disabled with nothing upgradable). The Wheel of Change keeps the spin, the
result acknowledgement and the leave page as three distinct states because the
gold payout happens in `preApplyResult` at spin time and only the other five
results land in `applyResult`; the relic result opens an ordinary reward
screen and the card-removal result opens a purge grid, both leaving the run on
the map afterwards, matching the room going `COMPLETE` and `EventRoom.update`
ceasing to tick the event (`EventRoom.java:33-41`).

Match and Keep deals in the constructor — before any player input — six
identities (rare, uncommon, common, then a colorless uncommon or, at A15, a
second curse; then a curse; then `Ironclad.getStartCardForEvent`'s Bash),
duplicates each by `makeStatEquivalentCopy`, and JDK-shuffles all twelve. The
board is per-visit screen state, so it lives in the transient
`EventDialogState` beside `scratch*` rather than in the save-parity RunState;
the twelve slots are offered directly as dialog options, which fits the
existing `kEventOptionCap` of 12 and needed no new fuzz `MoveCat`. Five
attempts resolve pairs: a match removes both slots and obtains the chosen copy
through the Omamori-aware door, a miss flips both back, and the attempt is
spent either way. The relics' `onPreviewObtainCard` pass is the eggs' existing
documented deferral and is deliberately not run here: it changes only the
upgrade shown on the board, because a matched card is obtained through
`add_card_to_master_deck`, whose `onObtainCard` pass upgrades exactly the same
types — the resulting master deck is identical and no save-parity state
diverges.

Accursed Blacksmith's Rummage obtains Pain and then the fixed Warped Tongs
(owning the relic is live; its `shuffleRng`-consuming upgrade body remains its
own deferral). Bonfire Elementals pays by the offered card's **rarity** —
nothing for BASIC, 5 HP for COMMON/SPECIAL, a full heal for UNCOMMON, +10 max
HP and then a full heal for RARE, and Spirit Poop (Circlet if already owned)
for a CURSE — and pays **before** the removal, which is observable when the
offering is Parasite. Fountain of Cleansing walks the master deck backwards
removing every curse but the unpurgeable ones. Lab and The Woman in Blue hand
their potions over as reward-screen rows. NoteForYourself inserts its card at
master-deck **index 0** (`addToTop`, not append) after a hand-rolled
`onObtainCard` pass and before `onMasterDeckChange`, bypassing
`ShowCardAndObtainEffect` exactly as the Java does, then opens the give-away
grid over the deck including the card just taken. We Meet Again offers a held
potion, a gold sum and a non-BASIC non-CURSE card, greys out whichever it
could not construct, and buys one screenless relic for any of the three.

**Two recorded modelling decisions, both now carried as obligations rows.**
`returnColorlessCard` shuffles the persistent `colorlessCardPool.group` **in
place**; this port shuffles a local copy, which is unobservable in Act 1
(`transformCard`'s COLORLESS branch reads the untouched
`srcColorlessCardPool`) but would matter to a shop with colorless slots.
`NoteForYourself.initializeObtainCard` reads the cross-run player-profile
preferences `NOTE_CARD` and `NOTE_UPGRADE`; the engine pins the frozen audited
reference profile at the game's own documented defaults (Iron Wave, no
upgrade), the same pin `note_for_yourself_available` already takes for that
profile's ascension read. That pin is the one behaviour in this batch a live
capture could contradict without the Java being wrong, and the pending
capture must confirm it.

**Shared surfaces.** `EventDialogState` grew from 8 bytes to hold the card
board and a third and fourth `scratch` slot; it is transient RunController
state, so no schema, fixture or golden artifact changed, and the fuzz
controller hash — which hashes the struct wholesale — now has per-field
coverage for every new slot. The soak build identity gains a `shrines1`
segment because reproducers from earlier builds can no longer replay an event
floor. The four event translation units now share `event_common.hpp`, an
internal header holding the AbstractCreature/AbstractDungeon shims that had
been copied per-TU; `emit/events.py` additionally emits the registry `rarity`
column as `event_card_rarity`, which Bonfire Elementals, We Meet Again and
Match and Keep all need and `CardDef` does not carry — derived from the same
column the per-rarity pools are built from, so the two cannot drift. No
registry id, opcode, `ChoiceKind`, `RunPhase` or fuzz `MoveCat` was taken; the
events registry gained no row (all fourteen already existed and were flipped
to `implemented`). Nothing under `src/engine/interp/`, `card_play.cpp`, the
combat observation surface, the translator's combat-choice slices or any Neow
path was touched.

**Acceptance.** Tier-2 covers every implemented row's generated metadata
(native/implemented, screen and A15 change counts) and its linked body; every
option of every screen through the public `legal_actions` / `advance` API;
draw-count attribution named per stream, including Match and Keep's deal
against a bit-for-bit independent hand-derivation at both A0 and A15 and the
zero-draw events proved zero-draw; every A15 branch; all six Wheel results;
Bonfire's full rarity table; and the gate evidence above. WSL Debug,
leak-detecting ASan/UBSan and Release are all green — `ctest -N` in the built
tree is the source of truth for the target list. Registry generation,
`tools/check_stale_counts.sh`, `tools/check_doc_links.sh` and
`git diff --check` are clean.

Java provenance (each read in full): `GremlinMatchGame.java:55-92, 179-285`,
`GoldShrine.java:39-101`, `Transmogrifier.java:32-84`,
`PurificationShrine.java:31-81`, `UpgradeShrine.java:34-92`,
`GremlinWheelGame.java:84-313`, `AccursedBlacksmith.java:35-105`,
`Bonfire.java:38-152`, `FaceTrader.java:36-122`,
`FountainOfCurseRemoval.java:31-86`, `Lab.java:32-66`,
`NoteForYourself.java:36-106`, `WeMeetAgain.java:45-140`,
`WomanInBlue.java:41-111`, `AbstractDungeon.java:1340-1379` (the one-time list
and the NoteForYourself availability branches), `:1882-1942` (getShrine),
`:852-878` and `:998-1045` (transformCard and its pools), `:1100-1113`
(returnColorlessCard), `:1481-1517` (getCard), `:681-688`
(returnRandomScreenlessRelic), `CardLibrary.java:1022-1042` (getCurse),
`PotionHelper.java:164-172`, `AbstractPlayer.java:697-712` (loseGold),
`:1387-1502` (damage), `:2313-2324` (getRandomPotion),
`AbstractCreature.java:199-223, 386-421` (increaseMaxHp / decreaseMaxHealth /
heal), `Ironclad.java:107-110`, and `EventRoom.java:17-41`.
<a id="b414"></a>

### B4.14 `[ ]` Neow blessing — code landed, oracle capture outstanding

Done 2026-07-27 from task base
`78a6a9e4ab5e51f8d165399cd4c016da52a0621e`. The checkbox stays `[ ]`: the
acceptance's second leg is an oracle spot-diff of the Neow screen over ten or
more seeds, and that needs the live game, which only a human operator can
launch. The tier-2 leg and the directed tests are green in all three presets;
the capture is prepared and specified in
[b414_neow_spotdiff.md](../tools/oracle_bridge/driver/b414_neow_spotdiff.md),
following the B4.7 precedent.

`RunPhase::NEOW` was a documented stub that consumed one `CHOOSE` and walked
onto the map. It is now the whole blessing. `run_begin` rolls the four options
at run start off the event-scoped Neow stream, and the controller stays in that
phase through the option dialog, the card-reward screen, the master-deck grid
and the combat-reward screen that two thirds of the payouts open — the same
shape the rest site uses for Smith / Toke / Dream Catcher, and for the same
reason: those are screens of one room, not phases of the run. No new
`RunPhase`, no new `MoveCat`, no registry id, no opcode, no schema bump; the
one new mask field is `can_choose_neow_option[4]`.

Rolling at run start rather than on the first button press is an equivalence,
not a shortcut. `NeowEvent.rng` is `new Random(Settings.seed)` — trap 17 — a
fresh stream that nothing else in the game reads or writes, and the intro
screens ahead of `blessing()` consume only MathUtils flavour draws. The sim's
Neow phase therefore *is* the blessing screen, which is also the state an
oracle capture has to be taken in; the runbook says so explicitly, and the
capture can tell the two apart because the mod dumps `neowRng` as null before
the blessing exists. Five draws, in the order the four `NeowReward`
constructors run: category 0's pick, category 1's pick, category 2's
**drawback**, category 2's pick, and category 3's `random(0, 0)` over a
one-element list, which still advances the stream because `Random.random(int,
int)` increments unconditionally.

Category 2's drawback-first order is load-bearing twice over: it fixes the
stream, and it decides how long the list the reward is then picked from is.
Three of that category's seven entries are dropped by the drawback that was
already rolled — no second removal beside a curse, no 250 gold beside NO_GOLD,
and the 20 % max-HP gain is skipped by an early `break` under the 10 % max-HP
loss, which is why it is the last entry that disappears rather than a middle
one. The named test drives a hand-held stream in both orders and requires the
seeds to disagree, so the ordering is proved rather than asserted.

**The mini-blessing is not unlock-gated, and it is unreachable here.** The
branch is `bossCount == 0 && !Settings.isTestingNeow -> miniBlessing()`
(NeowEvent.java:178-183). `bossCount` consults the profile preference
`<CLASS>_SPIRITS` **only** on the `Settings.isStandardRun()` branch
(NeowEvent.java:75-80); otherwise it is `Settings.seedSet ? 1 : 0`, and
`isStandardRun()` is `!isDailyRun && !isTrial && !seedSet`
(Settings.java:633-634). Every seeded run — every simulated run, every oracle
capture — therefore gets `bossCount == 1` and the full four-option blessing
without the profile being read at all. That is stronger than the frozen
fully-unlocked assumption, and it does not go through `UnlockTracker`, which
the gate never touches. The two-option mini blessing is outside the model's
domain and is deliberately not encoded.

Three things CONTRADICT the design doc's one-line summary of §5.6, which said
payouts "consume NeowEvent.rng for cards but `relicRng`/`potionRng` pools for
relics/potions". Per conventions §4 the losing text is corrected inline in
§5.6 and recorded as design §11 v0.1.9 in this same commit. Each of the three
is pinned by a named test.

The **colorless blessings split their streams**. Their rarity rolls are
`NeowEvent.rng.randomBoolean(0.33f)`, but the card identities come from
`getColorlessCardFromPool`, which goes through `CardGroup.getRandomCard(true,
rarity)` — and that `true` means `AbstractDungeon.cardRng`, not the caller's
stream (CardGroup.java:509-524). The same method **sorts** its rarity-filtered
view before indexing it, and `AbstractCard.compareTo` compares cardID strings,
so the two colorless pools are emitted sorted by `game_id` and are order-exact:
they carry none of the interim CardLibrary-order deviation the RED reward pools
still carry. The `CURSE` drawback's card is a `cardRng` draw too
(`getCardWithoutRng(CURSE)` delegates to `returnRandomCurse` ->
`CardLibrary.getCurse()`, whose "WithoutRng" name is a false friend), and it
lands **after** the payout's own draws, because the Java obtains it one
`NeowReward.update` tick later; on a colorless option those two share `cardRng`
and the order is observable.

The **three-potion blessing moves `cardRng` and the card-pity counter**. It
adds three `PotionHelper.getRandomPotion()` rows — one ungated `potionRng` draw
each, no tier gate and none of trap 14's rejection sampling — and then opens
the combat reward screen, whose `setupItemReward` appends a full
`getRewardCards()` row for any room that is not a TreasureRoom or RestRoom and
whose event leaves `noCardsInRewards` false (CombatRewardScreen.java:72-96).
NeowRoom is such a room, so the row is rolled and only then deleted again
(NeowReward.java:273-283). Skipping that roll would have desynced `cardRng` for
the entire rest of the run while looking perfectly reasonable in review.

The **boss swap can never return Black Blood**. `loseRelic(relics.get(0))` runs
before the BOSS-pool draw (NeowReward.java:243-247), and `BlackBlood.canSpawn`
is `hasRelic("Burning Blood")` — the relic that was just removed. The front pop
is therefore rejected, consumed rather than returned, and the next entry is
taken; both pops are relicRng-free (trap 15), which the test asserts alongside
the pool count.

The rest of the payouts route through doors that already existed: every card
grant through `add_card_to_master_deck` (so the CURSE drawback meets a charged
Omamori exactly as an event curse does), every relic through `acquire_relic`
(so a War Paint blessing fires its acquisition-ordered `onEquip` off the
floor-scoped `miscRng`), gold through `gain_gold`, and the potion rows through
the ordinary claim path with its slot and Sozu gates. Two small doors were
added beside their twins rather than open-coded: `lose_gold`
(AbstractPlayer.java:697-717, whose `onSpendGold` fan-out is ShopRoom-gated and
whose `onLoseGold` has no S1 override) and `lose_relic`
(AbstractPlayer.java:2014-2031, order-preserving, with no S1 `onUnequip` body
to dispatch). `PotionHelper.getRandomPotion` was published beside
`return_random_potion` so both share one pool-index mapping.

The transform payouts encode the Java's two different orders as written: the
single transform draws, then removes, then obtains; the double removes **both**
rows before transforming either (NeowReward.java:157-173).

Translator: the **Neow `screen_state` slice, deferred by B1.5/B4.3, is
discharged**. Neow arrives as an `EVENT` screen carrying the hard-coded id
`"Neow Event"` (GameStateConverter.getEventState :343-355) — the one base-game
event with no static `ID` field. That sentinel is recognised rather than joined
through the event registry, deliberately: Neow belongs to no act's event,
shrine or special pool, so minting an `EventId` for it would put a non-pool
entry into the three membership bitsets that pool ids index. The option list
keeps the ordinary EVENT validation, the near-miss id `"Neow"` is still
refused, and the slice stores nothing — the same contract as the reward slices.

Fuzz: every Neow move enumerates under the existing `NEOW_PROCEED` category —
the four option buttons, the sub-screens, and the final press. One bucket for
five screens is a deliberate loss of coverage resolution inside floor 0;
`MoveCat` is a shared append-only namespace and splitting Neow finer is the
orchestrator's allocation to make, not a task's to take. The controller hash
gained `RunController::neow`, which it needed for the same reason it carries
`rest.screen`: a press that only opens a sub-screen moves no `RunState` byte,
and the soak reports an unchanged whole-controller hash as a `no_progress`
failure. It found this immediately, before any test did.

Registry generation gained three colorless pool views on the existing card
emitter: the two sorted rarity views `getColorlessCardFromPool` indexes, and
the unsorted whole-pool view `returnTrulyRandomColorlessCardFromAvailable`
indexes (which does carry the interim order deviation). They are derived from
the `color` / `rarity` / `type` / `game_id` columns that already exist, so they
complete themselves as colorless rows land; the shop's two colorless slots will
want the same two arrays.

Every run-loop suite that used to leave Neow with one bare `CHOOSE` now goes
through a `leave_neow` helper that forces the finished-payout screen first.
There is no such button in the game — every run takes one of the four — but
those tests are about the floor loop, and a real payout would move the streams,
the deck and the relic pools underneath them. The helper carries that reasoning
at its definition.

Final-tree WSL Debug, leak-detecting ASan/UBSan and Release are each
**1257/1257**. Registry generation, documentation links, stale-count and
whitespace checks are clean. No schema, fixture, golden, Steam/game deployment
or external oracle artifact changed.

Java provenance: `NeowEvent.java:49-121, 162-242, 288-378`,
`NeowReward.java:42-391`, `NeowRoom.java:14-21`, `Settings.java:633-634`,
`AbstractDungeon.java:660-850, 852-878, 923-1045, 1125-1129, 1135-1219,
1481-1536, 1579-1595`, `CardGroup.java:490-555`, `AbstractCard.java:2583-2584`,
`CardLibrary.java:1022-1042`, `PotionHelper.java:67-172`,
`CombatRewardScreen.java:72-100, 292-312`, `CardRewardScreen.java:261-301,
441-483`, `RewardItem.java:145-157`, `AbstractRoom.java:501-510, 569-571`,
`AbstractCreature.java:199-223`, `AbstractPlayer.java:697-734, 1545-1553,
2014-2041`, `BurningBlood.java:30`, `Omamori.java:18-19`,
`BlackBlood.java:33-36`, and `AbstractEvent.java:63`.

<a id="b414-readout"></a>

### B4.14 oracle spot-diff read-out `[x]` — 2026-07-27 (addendum to the entry above)

The blocked leg above is discharged. No new campaign was launched: the three
strict-validated campaigns already on disk carry **41 A20 Ironclad runs**, and
every one of them walks through Neow —
`b45_rewards_oracle_20260727T204809Z_claude01` (5),
`b45_rewards_oracle2_20260727T204809Z_claude01` (6) and
`b47_treasure_oracle_20260727T204809Z_claude01` (30). Nothing under the §7.3
data root was modified.

**The verdict: 35 of 41 seeds zero-diff on every checkpoint**, against an
acceptance bar of ten.

| Outcome | Seeds | Which |
|---|---|---|
| fully zero-diff | 35 | options + activation + post-choice |
| clean through ACQUISITION only | 4 | STS00045/46 (Empty Cage), STS00052/54 (Astrolabe) |
| clean through ACQUISITION only | 1 | STS00076 — out-of-combat potion discard |
| diverged | 1 | STS00068 — a relic-registry defect, not Neow's |

**The harness.** `replay_run_diff --neow`, a third mode on the committed B4.5
binary rather than a scratch main. It replays no prefix at all:
`run_begin(seed, 20)` **is** the blessing screen, so the whole read-out sits on
floor 0 and cannot depend on combat fidelity. Three checkpoints per seed:

- **OPTIONS**, at the four-button record. The four option MEANINGS are joined
  to the capture's localized labels through a table written in the RENDER
  direction — sim meaning to the string the game would have printed — because
  three labels interpolate a number (`Max HP +7`, `Lose 7 Max HP`, `Take 18
  damage`) and rendering checks those numbers as part of the same comparison,
  where a parse would have to discard them. Then the whole translated
  `RunState`.
- **ACTIVATION**, at the record immediately after the option is pressed: the
  drawback and payout have run. This is where a boss swap's acquisition is
  proved, and it is a checkpoint of its own precisely so that a deferred
  `onEquip` body cannot take the acquisition down with it.
- **POST-CHOICE**, at the first floor-0 map record, once the payout's
  sub-screen has resolved.

`neowRng` and `purge_cost` are **compared, not neutralized**, unlike in the
reward mode: every record this mode looks at is a floor-0 record and the oracle
block carries both there. The reward mode's comment claiming the fork emits
thirteen streams and no `neowRng` was simply wrong — it emits fourteen, and the
translator has mapped `neowRng` since B4.3. Its neutralization is still right,
for the real reason: `NeowEvent.rng` is event-scoped, so every later dump omits
the key and the translated value stays value-init. Corrected in place.

**The capture ran the payout table wide.** 18 distinct option labels were taken
across the 41 seeds, reaching 16 of the 19 payout types: 12 boss swaps, 4
common-relic, 4 remove-one, 2 each of colorless / three-potion / remove-two /
one-random-rare-card / three-enemy-kill / three-card-offer / transform-two /
rare-colorless, and one each of five more. That reaches all three traps the
runbook exists to catch. The colorless offers moved `cardRng` and not
only `neowRng`; the three-potion blessings moved `cardRng` **and** the card
pity, which is what proves `setupItemReward` rolls a reward row and then
deletes it; and the twelve boss swaps returned Philosopher's Stone x2, Empty
Cage x2, Astrolabe x2, and one each of Fusion Hammer, Black Star, Coffee
Dripper, Runic Cube, Runic Pyramid and Busted Crown — **no Black Blood, in
twelve draws**, which is the `loseRelic`-before-the-pop ordering.

**One real divergence, root-caused and fixed: the transform pool order.**
STS00055 and STS00057 both took category 2's "Transform 2 Cards", and all four
transformed identities differed while every stream, both pity counters, the
deck size and the upgrades agreed. The rarity of each result matched too, which
localizes it to the index-to-card map.

`AbstractDungeon.transformCard` -> `returnTrulyRandomCardFromAvailable`
(`:1016-1045`) builds its list from `commonCardPool` ++ `srcUncommonCardPool`
++ `srcRareCardPool`. **The first term is a LIVE pool; the other two are the
`src*` copies**, and `initializeCardPools` fills every copy with `addToBottom`
— `group.add(0, c)`, a PREPEND (`AbstractDungeon.java:1180-1199`;
`CardGroup.java:459-461`) — so each holds its rarity's library order REVERSED.
`transform_card` walked all three forwards. That reading is correct for the
first block, and it was invisible before B4.5 pinned the CardLibrary order,
because until then both candidate orders were equally arbitrary. Reversing the
two `src*` blocks reproduces the game **4 for 4** across two independent seeds:

| Seed | list index | was | is | game |
|---|---|---|---|---|
| STS00055 | 62 | Impervious | Barricade | Barricade |
| STS00055 | 60 | Brutality | Limit Break | Limit Break |
| STS00057 | 54 | Uppercut | Inflame | Inflame |
| STS00057 | 70 | Demon Form | Offering | Offering |

The comment that justified the old reading ("the `src*` copies are
byte-identical to the live pools in S1") is deleted rather than softened: it is
the exact claim B4.5's `addToBottom` finding disproved, and it survived only
because nothing indexed the two spellings differently until now.

**The remaining exclusions, each named rather than absorbed.** Four seeds took
a boss relic whose `onEquip` opens a master-deck grid the sim defers — Empty
Cage's two removals, Astrolabe's three transforms — so the capture shows a grid
the blessing did not open. The harness detects exactly that (`rc.neow.screen`
is not `GRID` while the artifact is on one) and stops with the relic named; the
ACQUISITION checkpoint before it is clean, which is the whole claim: the right
relic, off the right pool pop, with `relicRng` untouched and Burning Blood
gone. STS00076 took the three-potion blessing and then discarded a potion from
the reward screen — an out-of-combat discard the run layer has no verb for
(B1.6's obligations row, now narrowed to that one command).

**The one divergence that is not Neow's.** STS00068's common-relic blessing
handed over Centennial Puzzle, and exactly one field differs:
`relics[1].counter: -1 -> 0`. `CentennialPuzzle` gates its once-per-combat draw
on a **static `boolean usedThisCombat`** and never assigns `this.counter`
(`CentennialPuzzle.java:21, 33-49`), so `AbstractRelic`'s -1 default stands and
the capture reports -1. `relics.yaml` gives the row `initial_counter: 0` and
the native handler uses `slot.counter` as that flag. This is a relic-registry
defect that any acquisition from any source would show; Neow's own accounting
for that seed — pool pop, streams, gold, deck — is clean. Deliberately **not**
fixed here: the correct repair moves the flag off the persistent counter into
combat-scoped state, which is a combat-layer change with its own tests, and it
needs an owner who can also answer why the sim never resets the flag between
combats at all. Filed as an obligations row.

**A second finding, stated from the Java and not measured.**
`event_grid_transform_card` reaches the same
`returnTrulyRandomCardFromAvailable` list, and its generated pool
`kEventTransformRedPool` is built by walking `cards.yaml` rows in registry
order — neither the library order nor the mixed src shape. No capture in hand
exercises a Living Wall transform, so it is filed for B4.10/B4.11 rather than
changed blind.

**Tests.** `NeowCapture.TransformTwoReproducesTheCapturedIdentities` freezes
both seeds' four identities in CI, re-driving each from `run_begin` through the
capture's own option and grid indices — and asserting that the ROLLED option
really was Transform 2 Cards with the recorded drawback, so the vector cannot
decay into a forced one. `NeowGrid.TransformReadsTheSrcPoolsBackwards` pins the
list's shape at its three block boundaries without a seed, and
`NeowGrid.TransformCardDrawsOnceAndReplacesTheRow` now derives its expectation
through the same independent helper.

**Acceptance:** WSL Debug, leak-detecting ASan/UBSan and Release all green on
the same tree as the B4.8 read-out below; the counts and the stale-count /
doc-link checks are recorded there. No schema, fixture, golden, Steam/game
deployment or oracle artifact changed; the three campaign directories were read
only.

Java provenance: `AbstractDungeon.java:852-878, 998-1045, 1135-1219`,
`CardGroup.java:455-461`, `CentennialPuzzle.java:21, 33-49`,
`NeowReward.java:105-128, 190-307`.

<a id="b45"></a>

### B4.5 `[x]` Combat rewards — **oracle spot-diff PASSED; card-pool library order pinned**
**The acceptance's oracle leg ran and passed.** Two operator-launched campaigns,
`b45_rewards_oracle_20260727T204809Z_claude01` (seeds **STS00042, STS00043**,
STS00044/45/46) and `b45_rewards_oracle2_20260727T204809Z_claude01`
(**STS00048, STS00049, STS00051, STS00052**, STS00047/50), all strict-validated
with `--require-oracle`. Six of the eleven runs reached a combat reward screen;
the other five died in the floor-1 fight and carry none. Those six hold **13
combat reward screens**, and **every one of the 13 zero-diffs** — assembly and
claim, over the acceptance's whole field table: `gold`, `potions[]`,
`master_deck[]`, `card_blizz_randomizer`, `blizzard_potion_mod`, and
`cardRng` / `treasureRng` / `potionRng` / `relicRng`. Requirement was ≥ 3 runs;
6 runs and 13 screens is what ran. Translation of all eleven artifacts is `OK`
with zero unknown-field and zero unknown-id errors.

**The read-out is a committed binary, not a scratch main:**
`tools/oracle_bridge/replay/replay_run_diff`. Its default mode seeds the
simulator from the translated RunState on the last in-combat record, calls
`assemble_combat_rewards`, diffs the assembly against the next captured record,
then drives the artifact's own claim commands through a `COMBAT_REWARD`
`RunController` and diffs the post-claim `RunState` whole with
`diff_run_states`. Because it seeds from the capture rather than re-driving the
run, it is independent of combat fidelity — see the obligations table for what
its `--replay` whole-run mode does and does not cover.

**The card-pool library order is pinned, and it needed no guesswork.** The first
pass diffed clean on every stream and both pity counters while 8 of 13 claims
differed on exactly the picked card's identity — the predicted library-order
deviation. That order turns out to be **computable**: the game fills its pools
by walking `CardLibrary.cards`, a `HashMap<String, AbstractCard>`, whose
iteration order is a pure function of `String.hashCode`, the final capacity and
insertion order. `emit/cards.py` now computes it (`library_order_key`), and the
capture **checks** the computation: 27 offered card identities across 9
CARD_REWARD screens and 5 seeds are reproduced exactly, where the previous
registry-id order reproduced **0 of 27**. After regeneration all 13 screens
zero-diff including the deck column.

**Two of the six runs cannot be replayed end to end, and neither affects this
acceptance.** Their Neow boss-relic swap handed out **Philosopher's Stone**
(STS00042) and **Fusion Hammer** (STS00043), both on the deferred
`energyMaster` obligations row, so a whole-run `--replay` desyncs at the first
combat's energy; STS00052's swap handed out **Astrolabe**, whose `onEquip` is
likewise deferred. The reward read-out seeds from the captured post-combat state
and so is untouched by all three — their 7 reward screens zero-diff like the
rest. The whole-run replay also surfaced two genuine combat-layer gaps that are
**not** B4.5's and are recorded as new obligations rows: monster block is never
cleared at the monster's turn start, and Vulnerable/Weak never tick down.

**Environment blocker CLEARED 2026-07-26; the capture then ran on 2026-07-27.**
The owner
sanctioned the installed stack, and design §1.2 is amended accordingly: the
frozen runtime is **StS `12-18-2022` (`[V2.3.4]`) / ModTheSpire `3.30.3` /
BaseMod `5.56.0`** (design §11 v0.1.7). Nothing was downgraded and no prior
evidence is re-blessed — the `11-30-2020` label was a documentation error
inherited from *upstream* CommunicationMod's declared `sts_version`, and the
decompiled Java every `File.java:line` citation resolves against is itself
`12-18-2022` (`CardCrawlGame.VERSION_NUM`), so the spec and the captured
runtime were always the same build. BaseMod is pinned by **version** for the
first time; previously only workshop item `1605833019` was recorded, which is
why the runbook had to defer to an undefined "owner-approved frozen
installation".

That unblocked the environment; the operator then redeployed the fork and ran
the capture. Both campaigns' headers record the sanctioned stack observed from
their own bound `mts_launch1.log`, and
`fork_jar_sha256: 7DC814AD240CBBD9100B2E8C92B6AA97B4ADFBED62FFED7961C6E5DE15884733`
— the post-amendment build, so the redeploy did happen. **B4.7 still stays
open** on its own live capture.

**Safety hardening (non-acceptance):** the preserved `b45_rewards` artifacts
were rejected because the GUI loaded stock CommunicationMod and every header
has `oracle_block_enabled: false`; the launch environment also drifted from the
then-frozen game/ModTheSpire versions. The driver now makes a missing oracle
block on the first in-dungeon dump a fatal durable status before policy/artifact
acceptance, the orchestrator stops relaunching on that status, and the
validator/runbook require a distinct one-seed oracle preflight.
**Also fixed at the re-pin — the defect that hid the drift:** the artifact
header's `sts_version`/`mts_version` were *static constants*, so every artifact
ever written claimed the frozen stack no matter what launched. The driver now
parses the observed stack out of the exact append-only `mts_launch<N>.log`
allocated for its game process, writes that into the header with a
`version_source`, and refuses via the existing `fatal_environment_drift` status
on a mismatch, on an unparseable log, or when the log is not bound to this
orchestrator launch (including the GUI case). The filename is carried in the
driver command and a one-use binding nonce is inherited through the launched
game; resume continues above every preserved numeric log index, so neither an
old higher-numbered log nor persisted GUI config can become current evidence.
In-progress resume now also binds the driver revision, preventing mixed capture
logic under one ledger.
Applied retroactively to the 15 preserved campaign directories, the parser
accepts all 12 real captures and flags exactly `b45_rewards` — the one already
known to be invalid.
An independent second review then closed the remaining strict-evidence gaps:
normal boss-reward claims now propagate their action count; seed identity is
joined from request through every in-game/oracle record; run and timing JSONL
grammars are complete and bijective; campaign ids and resolved paths cannot
escape the data root; and resume identity includes the currently requested
fork/schema even for completed ledgers. These are capture-safety fixes only,
and they are what makes the campaigns above admissible as evidence.
See the [non-task archive log](stage-b-log.md#b45-oracle-preflight), the
[runtime re-pin entry](stage-b-log.md#b45-oracle-stack-repin), and its
[launch-binding fix-forward](stage-b-log.md#b45-oracle-stack-repin-fix-forward).

**Landed** — commit `4f0544a`, merged at `e222dc2`, landed in `e6ec9ce`:
`combat_rewards.{hpp,cpp}` (assembly at reward-screen open), the `RewardScreen`
phase in `RunController` (**transient — no new storage, no schema bump**, as the
Acceptance demanded), the CHOOSE claim flow, `kIronclad{Common,Uncommon,Rare}Pool`
in `emit/cards.py`, and the translator's reward slice — now **content-validated**,
where previously any `reward_type` name passed. Verified at integration that the
three new pools are correctly RED-gated: **20/36/16 unchanged** after B3.10a added
14 colorless cards to `cards.yaml`, which is the "confirm colorless is unreachable
from a combat reward" deliverable proven rather than argued.
**Three findings the brief and design §5.6 did not carry** — see the change log
for the frozen-doc ruling: **elite card-rarity widths are 10/40** (set in
`MonsterRoomElite`'s *constructor*, not by its `getCardRarity` override, which is
Elite-Swarm-only) and **boss rewards are unconditionally RARE**
(`MonsterRoomBoss.java:40-42`), so boss rewards also reset pity on every card and
never draw the upgrade boolean; **Prayer Wheel** grants a second plain-room card
reward (`CombatRewardScreen.java:89-94`), unmentioned anywhere; and **Smoke Bomb
consumes the battle-over draws** (gold, elite relic pop, potion roll + ratchet),
where B4.4 had modelled escape as a stream no-op — a fix-forward pinned by a
named test.
**Deps:** B4.4, B3.3-B3.9 (the RED reward pool) · **Spec:**
design §5.6 · **Provenance:** AbstractRoom.java:291-296, 314-325, 580-617,
108-109, 148-177; AbstractDungeon.java:1423-1498, 1597-1624
**Deps note** (amended 2026-07-25 from a read-only scout; every citation
verified — see the change log): **B3.10 and B3.11 removed.** The combat
card-reward pool is **RED-only** (`Ironclad.getCardPool` →
`CardLibrary.addRedCards`, `CardLibrary.java:1157`); colorless reaches the
player through the shop and Neow, and the only caller of
`getColorlessRewardCards()` is `RewardItem(CardColor)` →
`SensoryStone.java:121`, an Act-3 event. **B3.8 stays and is promoted from a
coverage dep to a hard mechanical blocker:** `cards.yaml` has **zero RARE
rows**, so `rareCardPool` would be empty and `getCard(RARE)` would index an
empty list — and RARE is reachable as soon as pity reaches 2.
**Deliverables:** gold rolls (boss=miscRng ±5 ×0.75@A13, elite/normal=
treasureRng — trap 18), potion drop (40 % + blizzardPotionMod ratchet, trap
family), card rewards (3 cards, `cardRng.random(99)+cardBlizzRandomizer`
against thresholds **`< 3` / `< 40`** — widths 3/37/60, `AbstractRoom.java:158,
167`, confirmed at `AbstractDungeon.java:1606-1615`; coding the widths as
thresholds is wrong by 3 points on every reward — pity reset/growth,
no-duplicate re-roll — read the dupe loop at task, upgrade chance 0 in Act 1
— **the `randomBoolean` draw still happens** (`Random.java:79-82`); only
`c.rarity != RARE` short-circuits it, `AbstractDungeon.java:1470`), **confirm
colorless is unreachable from a combat reward** with the citation above
(re-scoped from "colorless handling"), reward-screen CHOOSE flow incl. skip.
**Acceptance:** tier-2: pity dynamics across scripted reward sequences match
hand-derivation; stream attribution named tests (trap 13, 18); oracle
spot-diff: ≥ 3 bridge runs' reward screens zero-diff through the differ —
where "reward screens zero-diff" means the **post-claim `RunState`** (gold,
potions, deck, pity, counters). **No new storage and no schema bump.**
Diffing the *offer* would need tools-side differ work or `RunState` growth
(an unplanned `SCHEMA_VERSION` bump is stop-the-line, conventions §5) and is
explicitly **not** what this acceptance asks for.
**Inherited — four of five DISCHARGED:** Question Card / Singing Bowl / White
Beast Statue (B3.25) — implemented and tested. Reward-screen `screen_state`
translation (B1.5/B4.3) — done for the reward slice, and hardened: the
`reward_type` name is now enumerated and fails loud. The **master-deck door**
(hook audit) — every reward card obtains through `add_card_to_master_deck`, with
the requested Ceramic-Fish-gold guard test; `remove_master_deck_card`
legitimately gains no caller, because the game has no reward-screen removal.
**Busted Crown**'s reward count and the **Black Star** elite relic (B3.27) — live
at the combat-reward claim, as are Golden Idol ×1.25 and Sozu's potion block;
their chest/event-screen shares stay with those screens' owners.
**HANDED ON — DISCHARGED:** the CardLibrary library-order pin (B3.6). One
`emit/cards.py` change pinned every generated pool at once, as anticipated, and
the obligations row is struck.

<a id="b45-spotdiff-readout"></a>

### B4.5 oracle spot-diff read-out + pool library-order pin `[x]` — 2026-07-27

**What ran.** The operator launched two campaigns on the sanctioned stack and
both passed `validate_artifacts.py --require-oracle`:
`b45_rewards_oracle_20260727T204809Z_claude01` (STS00042, STS00043, and
STS00044/45/46, which died in the floor-1 fight) and
`b45_rewards_oracle2_20260727T204809Z_claude01` (STS00048, STS00049, STS00051,
STS00052, plus pre-reward STS00047/50). Every header carries
`oracle_block_enabled: true`, the observed `12-18-2022` / `3.30.3` / `5.56.0`
stack read out of its own bound `mts_launch1.log`, and the post-amendment fork
hash `7DC814AD…5884733`. All eleven artifacts translate `OK` with **zero
unknown-field and zero unknown-id errors**.

**The verdict, per reward screen.** Thirteen combat reward screens across the
six runs that reached one, and **all thirteen zero-diff**:

| Run | Reward screens (floor) | Assembly | Claim |
|---|---|---|---|
| STS00042 | 1, 4, 5 | 3/3 clean | 3/3 clean |
| STS00043 | 1, 2 | 2/2 clean | 2/2 clean |
| STS00048 | 1 | 1/1 clean | 1/1 clean |
| STS00049 | 1, 2 | 2/2 clean | 2/2 clean |
| STS00051 | 1, 3, 4 | 3/3 clean | 3/3 clean |
| STS00052 | 1, 4 | 2/2 clean | 2/2 clean |

"Assembly" compares `cardRng` / `treasureRng` / `potionRng` / `relicRng`,
`card_blizz_randomizer`, `blizzard_potion_mod` and the assembled item list
against the captured reward screen. "Claim" drives the artifact's own claim
commands and compares the **whole post-claim `RunState`** through
`diff_run_states` — which is where `gold`, `potions[]` and `master_deck[]` are
proved. Nine of the thirteen screens also had their card-pick screen opened in
the capture, so their three offered identities are compared too.

**The harness.** `tools/oracle_bridge/replay/replay_run_diff`, a committed tools
binary rather than a scratch main. Its default mode seeds the simulator from the
translated `RunState` of the last in-combat record, calls
`assemble_combat_rewards` with that record's floor-scoped `miscRng`, then parks a
`RunController` in `COMBAT_REWARD` on the captured post-assembly state to drive
the claims. Seeding from the capture instead of re-driving the run is
deliberate: it makes the reward read-out independent of combat fidelity, which
matters because three of the six runs cannot be re-driven at all (below).

Its `--replay` mode is the whole-run version — `run_begin(seed, 20)` plus a
screen-driven mapping of every `action_command` — and is the closest thing the
repo has to the B1.6 "seed a sim replay from any translated `RunState`" adapter
without being it: it re-drives the prefix rather than resuming from a mid-run
state, and it stops with an explicit reason at any room the run layer does not
model (shops, out-of-combat potion discards, a grid `cancel`). That mode is a
diagnosis tool, and it is what found everything in the next two paragraphs; the
B1.6 obligations row stays open, narrowed.

**Three runs cannot be re-driven end to end, for already-documented reasons.**
Their Neow boss-relic swap handed out **Philosopher's Stone** (STS00042) and
**Fusion Hammer** (STS00043) — both on the deferred `energyMaster` obligations
row, so the sim gives 3 energy where the game gives 4 and the first combat
diverges immediately — and **Astrolabe** (STS00052), whose `onEquip` is one of
the five deferred BOSS bodies, so its three-card transform grid never opens.
None of this touches the reward read-out.

**Two genuine combat-layer gaps found in passing** (new obligations rows, not
B4.5's): monster block is never cleared at the monster's turn start, so a Curl
Up block survives into the player's next turn and absorbs damage the game had
already discarded; and `Vulnerable` / `Weak` carry no `AT_END_OF_ROUND` binding,
so their durations never tick down. Both were caught on STS00051's two-Louse
floor-1 fight, where the game's Louse died to a Strike that the sim's survived
with 7 HP behind 3 stale block.

**The card-pool library order, computed rather than transcribed.** The first
read-out diffed clean on every stream and both pity counters while 8 of the 13
claims differed on exactly one field — the picked card's identity — which is the
signature the runbook predicted for the CardLibrary-order deviation. The order
turned out not to need empirical recovery at all. The game fills its pools by
walking `CardLibrary.cards`, a `HashMap<String, AbstractCard>` keyed by cardID
(`CardLibrary.java:409`, written only by `add()` at `:954`), and a Java HashMap's
iteration order is a pure function of the keys, the map's final capacity and
insertion order:

    bucket = (h ^ (h >>> 16)) & (capacity - 1),   h = String.hashCode(cardID)

with ties inside a bucket broken by insertion order, which Java 8's lo/hi resize
split preserves across every rehash. `CardLibrary.initialize()` inserts 370
cards, so the capacity settles at 512; each `addXCards()` call inserts in
alphabetical order of the Java class name, which is the cardID with its
separators removed bar ten rows. `emit/cards.py` now computes exactly that
(`library_order_key`), and the resulting order **reproduces 27 of 27** captured
offer identities across 9 CARD_REWARD screens and 5 seeds. The previous
registry-id order reproduced **0 of 27**. Regenerating and re-running the
read-out takes the deck column to zero as well.

**Two orders fall out of the Java, and conflating them would have been the
subtle way to get this wrong.** `CardGroup.addToTop` is `group.add(c)` — an
APPEND despite the name (`CardGroup.java:455-457`) — so `commonCardPool` /
`uncommonCardPool` / `rareCardPool`, which `getCard(rarity)` reads, are in plain
library order. But `initializeCardPools` copies each into its `src*` twin with
`addToBottom`, which is `group.add(0, c)` — a PREPEND (`:459-461`) — so every
`src*` pool holds its rarity's library order REVERSED, and
`returnTrulyRandomCardInCombat` concatenates them common ++ uncommon ++ rare
(`AbstractDungeon.java:944-978`). `src_combat_order` in the emitter encodes that
second shape for `kIroncladAttackPool`, `kIroncladSkillPool`,
`kIroncladCombatPool`, `kColorlessCombatPool` and `kColorlessPool`; the three
reward pools use the plain order. `kColorlessUncommonPool` /
`kColorlessRarePool` are untouched — `getRandomCard(useRng, rarity)`
`Collections.sort()`s its filtered view, so those were always order-exact.

**The curse pool is pinned too, at a second capacity.** `CardLibrary.getCurse`
(`:1043-1050`) walks the separate `curses` map, whose 14 entries settle at
capacity 32 rather than 512. STS00048's Neow "obtain a curse" drew index 2 and
the game handed over Shame; the computed order puts Shame at index 2, where the
old order had Doubt. One data point, but it is the rule confirmed at a capacity
the 27 offers never exercise.

**Tests.** `card_pool_library_order_test` freezes the capture evidence in CI:
the 15 COMMON and 12 UNCOMMON offer identities as `(pool, index, card)` rows,
the curse index, each reward pool's endpoints, the reversed rarity-major `src*`
relationship derived structurally from the reward pools, the B3.11
type-filtered-subsequence invariant re-checked under the new order, and the
membership counts the reorder must not disturb. Three existing tests pinned the
old ORDER and were updated knowingly, keeping their membership assertions:
`RestSites.DreamCatcherOpensDirectCardPickAfterHealing` (same three draw
indices, three new names), `CardUncommonSkillsRegistry.IroncladAttackPoolMembership`
(its two endpoint pins are now library positions, not id extremes), and
`CardColorlessRaresChrysalis.GeneratedCopiesAreZeroCostPermanently` (seed 77 →
78, because 77's three indices now land on three zero-cost skills, which would
have made the zero-cost assertion vacuous).

**Acceptance:** WSL Debug, leak-detecting ASan/UBSan and Release each
**1322/1322**. Stale-count and documentation-link checks clean. No schema,
fixture, golden, Steam/game deployment or oracle artifact changed; the two
campaign directories were read only.

Java provenance: `CardLibrary.java:409, 424-445, 465-908, 949-956, 1043-1050,
1056-1063, 1142-1161`; `AbstractDungeon.java:944-1014, 1135-1219, 1481-1498`;
`CardGroup.java:455-461, 502-524`; `Ironclad.java:138-150`;
`MonsterGroup.java:98-104, 290-304`.
<a id="b48"></a>

### B4.8 `[ ]` Shop — code landed, oracle capture outstanding

Done 2026-07-27 from task base
`516f13640d6cc3f45556bef5c1d9558bf6fa4f97`. The checkbox stays `[ ]`: the
acceptance's third leg is an oracle spot-diff of a shop floor, and that needs
the live game, which only a human operator can launch. The other two legs are
green — the draw-order pin and the purge ramp — and so is a bonus vector that
needed no new capture at all (below). The capture is prepared and specified in
[b48_shop_spotdiff.md](../tools/oracle_bridge/driver/b48_shop_spotdiff.md),
following the B4.7 / B4.14 precedent.

**The dependency override is recorded on purpose.** B4.8's `Deps:` names B4.5,
which is `[!]` — code landed, capture-blocked. The project owner explicitly
authorised starting B4.8 ahead of B4.5's acceptance on 2026-07-27, with the
capture pipeline running concurrently. Nothing in the shop reads B4.5's blocked
leg: what a merchant needs from the reward layer is the relic-pool draw, the
`acquire_relic` door, the potion-slot inventory and the master-deck obtain
door, and all four are landed.

#### The sixteen draws

A shop is built once, on room entry, in one uninterrupted sequence across three
streams, and nothing about it is a player decision — so `generate_shop` is a
single call and the CHOOSE flow above it only spends gold. The order, which is
the thing this task exists to get right:

| # | Stream | Consumer |
|---|---|---|
| — | `cardRng` x12 | five colored identities (a rarity roll plus a type-filtered pool index each) then two colourless — ATTACK, ATTACK, SKILL, SKILL, POWER, then colourless UNCOMMON and RARE |
| 1–5 | `merchantRng` | the five colored price jitters, `random(0.9f, 1.1f)` |
| 6–7 | `merchantRng` | the two colourless price jitters |
| 8 | `merchantRng` | the sale slot, `random(0, 4)` — always a COLORED slot |
| 9, 11 | `merchantRng` | relic slot 0 and slot 1 tier rolls, `random(99)` against 48 / 82 |
| 10, 12, 13 | `merchantRng` | the three relic price jitters, `random(0.95f, 1.05f)` |
| — | `potionRng` | three `returnRandomPotion` identities (a tier roll each plus trap-14 rejection sampling) |
| 14–16 | `merchantRng` | the three potion price jitters, interleaved one per identity |

Slot 2 is **always** the SHOP tier and rolls no tier (`ShopScreen.java:365`),
which is why a fresh shop is sixteen `merchantRng` draws and not seventeen.
Relic and potion identities cost `merchantRng` nothing: the pools are END-popped
(trap 15) and the potion identity is `potionRng`'s.

`ShopDrawOrder.SixteenMerchantDrawsInTheJavaOrderForAFixedState` replays all
three streams beside the engine for a fixed state, compares every price and
every id, and then asserts the three post-build stream states are byte-identical
to the replay's. The table above is repeated as a comment at both the header and
the test.

#### A recorded capture already reproduces a whole merchant

The strongest single check here needed no new capture. Run `STS00008` of the
b13 twenty-seed sweep contains a SHOP_SCREEN at floor 3 with full prices, and
its `oracle` block carries the pre-entry stream triples and the three relic
pools. `ShopCapture.B13Seed1790050543758Floor3MatchesTheRecordedMerchant`
rebuilds that shop from exactly those inputs and matches it entry for entry:
Pummel (on sale, 43) / Iron Wave 59 / Armaments 59 / Rage 89 / Rupture 85,
Finesse 99 and Secret Weapon 206, Question Card 268 / Blood Vial 172 / Medical
Kit 161, Strength Potion 54 / Duplication Potion 85 / Flex Potion 55, purge 75
— plus the post-build `cardRng` 9→21, `merchantRng` 0→16, `potionRng` 3→10, raw
state included.

That is not the acceptance leg (one shop, from another task's campaign, no
purchase), but it settled a question the decompiled source could not.
`AbstractCard.getPrice`, `AbstractRelic.getPrice` and `AbstractPotion.getPrice`
are switches over the game's NESTED enums, and `sts-classes.jar` carries no
inner classes at all — CFR emitted them as `$SwitchMap[...]` indices with no
constant names, so the index→tier mapping is not recoverable from the tree. The
value multisets are; the capture supplies the assignment for every tier a shop
can offer (card COMMON 50 / UNCOMMON 75 / RARE 150; relic COMMON 150 /
UNCOMMON 250 / SHOP 150; potion COMMON 50 / UNCOMMON 75). RARE and BOSS relics
are the switch's remaining 300/300 pair and are interchangeable there, so the
shop's answer is the same either way.

#### Pricing is a pipeline, and the stages do not commute

Card prices TRUNCATE (`(int)`), item prices ROUND (`MathUtils.round`), the
colourless x1.2 is INSIDE the truncation, the sale halving is an INTEGER divide
of the already-truncated price, and each discount is a round of an
already-rounded number. `ShopPricing.ColorlessBumpIsInsideTheTruncation`
derives the same jitter both ways and requires the engine to match the first.

The discount tail runs in the Java's order: A16 x1.1 with `affectPurge=false`,
then The Courier x0.8, then the Membership Card x0.5, then Smiling Mask's flat
50. Two consequences are reproduced rather than corrected. **Ascension never
moves the purge cost** — the only A16 call passes `affectPurge=false`, so an
A20 shop's stock is 10 % dearer and its removal service is not. And **at init a
Membership Card overwrites The Courier's purge discount instead of compounding
with it**, because each `applyDiscount` recomputes `actualPurgeCost` from
`purgeCost` rather than from the previous call's output
(`ShopScreen.java:340-358`) — while `purgeCard`'s tail spells the same case as
a single round of `0.8f * 0.5f`. The two call sites disagree in the game, so
the port has two functions, `shop_purge_cost_at_init` and
`shop_purge_cost_after_purge`, and a named test pins both (75 → 38 at init;
100 → 40 after the ramp).

#### A latent run-setup bug the purge ramp uncovered

`run_begin` never initialised `RunState.purge_cost`. The field existed (B4.3
front-loaded the storage) and nothing had ever read it, so a value-initialised
run would have opened its first merchant offering card removal for **zero
gold** — and then ramped from 25, not 100. `ShopScreen.purgeCost` is a STATIC
in the game, reset only by the dungeon reset that precedes a new run
(`CardCrawlGame.java:478` → `ShopScreen.java:241-244`), which is precisely why
that reset exists. It is now spelled in `run_begin` beside the other
`dungeonTransitionSetup` fields, with the reasoning at the site. A failing test
found it, not a review: the purge-flow test expected 100 and got 25.

#### Nine new generated pools, and why they do NOT deviate

The five colored slots go through `getCardFromPool(rarity, type, useRng)`
(`AbstractDungeon.java:1538-1577`) → `CardGroup.getRandomCard(type, useRng)`
(`CardGroup.java:539-552`), which filters the rarity pool by CardType,
**`Collections.sort`s the filtered view**, and only then indexes it with
`cardRng`. `emit/cards.py` therefore gains nine arrays,
`kIronclad{Common,Uncommon,Rare}{Attack,Skill,Power}Pool`, sorted by `game_id`
— and because that sort is the game's own, they are **ORDER-EXACT**: they do
not inherit the registry-id-order deviation the four unsorted pools carry,
exactly as the two colourless rarity views do not. The four existing sort keys
were not touched. A shop card-id mismatch in the spot-diff is therefore a real
divergence, which the runbook says explicitly.

`kIroncladCommonPowerPool` is deliberately **EMPTY** — the Ironclad has no
common POWER — and that emptiness is load-bearing: it is what makes
`getCardFromPool`'s `retVal == null && type == POWER` branch recurse to the
next rarity up, spending NO draw on the empty pool, because `getRandomCard`
returns null before it indexes anything. A `static_assert` pins that view empty
and the other eight non-empty, which is what makes the Java's two other exits
(the non-POWER fallthrough *down* the rarity ladder, and COMMON's fallthrough
into the CURSE pool) provably unreachable rather than merely untested.
`ShopDrawOrder.PowerSlotSkipsTheEmptyCommonViewWithoutSpendingADraw` proves the
one-draw accounting, and that the card's OWN rarity — UNCOMMON, not the COMMON
that was rolled — is what it gets priced as, which is what
`ShopScreen.java:253` reads.

#### Relic hooks

Four are live, each cited at its body. **Maw Bank** is used up by the first coin
spent in any shop: `AbstractPlayer.loseGold` fires every relic's `onSpendGold`
when the current room is a ShopRoom, ahead of the `amount > 0` test, so the
fan-out lives in the `lose_gold` door itself behind an `in_shop` flag — the
place the door's own comment had already reserved for it — and
`MawBank.setCounter(-2)` is the used-up encoding B4.10's entry share already
reads. **Membership Card** bought in a shop re-prices the rest of that same
shop, because `StoreRelic.purchaseRelic` obtains the relic and only then calls
`applyDiscount(0.5f, true)`. **Smiling Mask** pins the purge cost to 50 the
moment it is bought. **Meal Ticket** heals 15 on entering a shop.

Four relics can never be STOCKED — Maw Bank, Smiling Mask, The Courier and Old
Coin all AND their floor gate with `!(getCurrRoom() instanceof ShopRoom)`, and
the merchant is built after `setCurrMapNode`. That is RNG-visible, not
cosmetic: a closed gate makes the end-pop discard that id and pop another, so
the pool moves. A named test stacks all three of the offenders that can reach a
COMMON/UNCOMMON pool end and requires none of them on a shelf.

The eggs' `onPreviewObtainCard` also runs, over every stocked card, as the shop
is built (`ShopScreen.java:258-260, 268-270`; each egg forwards the preview
straight to its own `onObtainCard`), so an egg owner's merchant DISPLAYS its
matching cards upgraded and the instance bought is the upgraded one. `ShopSlot`
carries an `upgrade` byte for it.

#### Meal Ticket, and why the ?→Shop share is free

`justEnteredRoom` fires AFTER the ?-roll has replaced the room object and after
`setCurrMapNode` (`AbstractDungeon.java:1763-1789`), so by the time it runs a
?→Shop and a static ShopRoom are the same room. `on_player_entry` therefore
dispatches the fan-out for every non-Event room and, for a `?`, only after
resolving — at which point the SHOP branch recurses into the ordinary Shop
entry and picks the dispatch up on the way through. Both paths are named tests.
The heal is out of combat, so Magic Flower's `onPlayerHeal` (combat-only,
`MagicFlower.java:31-37`) cannot scale it; the fan-out is named rather than
written, as `rest_apply_heal` already does.

#### The Courier: half landed, half BLOCKED — and the blocker is real

Its `x0.8` discount and its purge branch are live, because both are inside
`ShopScreen.init` and `purgeCard`, which this task implements in full. Its
RESTOCK is not, and the reason is not scheduling. `ShopScreen.purchaseCard`'s
replacement draws `getCardFromPool(rollRarity(), type, false)`, and
`useRng=false` means `MathUtils.random` — libGDX's **unseeded global**, not
`cardRng` (`ShopScreen.java:615-617`). The replacement card's identity has no
reproducible answer from a seed at all. The rarity roll ahead of it, and the
relic and potion restocks, ARE seeded and could be encoded; landing half a
relic's behaviour behind a hard blocker would be worse than naming the blocker,
so the obligation row now carries it and the runbook asks the operator to
capture a Courier shop specifically, to measure what the restock costs the
seeded streams.

#### A ledger correction, and a doc correction

The deliverables line said "5 colored + 2 colorless w/ 0.3 rare chance". There
is no such roll: the two colourless slots are FIXED UNCOMMON then RARE
(`Merchant.java:84-85`). `AbstractDungeon.colorlessRareChance` is read only by
The Courier's colourless restock (`ShopScreen.java:601`) — the half that is
blocked. Recorded in the ledger block rather than silently dropped.

Separately, B4.13's `colorlessCardPool`-shuffled-in-place obligation named "the
shop's two colorless slots" as the future consumer that would observe the
persisted order. The shop now exists and does **not** observe it:
`getColorlessCardFromPool` reaches `CardGroup.getRandomCard(true, rarity)`,
which filters into a local list and sorts it before indexing, discarding the
source order on every read. That row's forward-looking half is corrected in
place; it stays open only against a future reader of the unsorted whole-pool
view.

#### Run-layer shape

`RunPhase::SHOP = 10` and fuzz `MoveCat::SHOP = 26` (`COUNT` 26→27), both
pre-authorised in the ledger's allocation table and both recorded there.
`RunController` gains a `ShopState` — transient, like `RewardScreen`,
`TreasureChest`, `RestSiteState` and `NeowState`, because the game rebuilds a
whole merchant from `(seed, merchantRng.counter)` on reload. It is added to the
fuzz controller hash for the same reason `rest.screen` and `neow` are: opening
the removal grid moves no `RunState` byte, and an unchanged whole-controller
hash is a `NO_PROGRESS` failure to the soak.

Shop slots are FIXED, not compacted. The game removes a bought row from its
list, so the game's indices shift; nothing observable depends on those indices
(no RNG and no relic hook reads a shop list position — `StoreRelic.slot` is a
render x-offset), and a `sold` flag keeps a row's CHOOSE arg0 stable for the
whole visit, which a shifting index could not. The CHOOSE layout is one dense
index space: 0–4 colored, 5–6 colourless, 7–9 relics, 10–12 potions, 13 the
purge service, `kChooseProceed` to leave. The removal grid is modal — while it
is up, only its own master-deck rows are legal, the shape the rest site's
Smith/Toke grids already use.

Every purchase is a transaction: a false return means the purchase was not
legal and both arguments are byte-stable. A bought card is APPENDED, because
`CardGroup.addToTop` is `group.add(c)` (`CardGroup.java:455-457`) and the shop
reaches the master deck through `FastCardObtainEffect` → `Soul.obtain` →
`masterDeck.addToTop`.

#### Translator

The SHOP_SCREEN slice is discharged on the B4.11/B4.14 terms: potion ids join
through the registry, every `price` on a card / relic / potion row is
type-checked (both shared parsers gained that check, so the reward slice
benefits too), `purge_cost` must be an integer and `purge_available` a boolean.
It stays STORAGE-LESS deliberately — translation outputs
`RunState`/`CombatState`, a merchant is derived state, and the one piece the
game does persist already has a `RunState` field fed from the oracle block.
Three named tests: the happy path, an unknown potion id, and the three
type-check refusals.

#### An out-of-scope defect found and reported, not fixed

`add_card_to_master_deck_top` (`run_deck.hpp`) inserts at master-deck index 0
under a comment reading "CardGroup.addToTop — the card lands at master-deck
INDEX 0". That reading is inverted: `addToTop` is `group.add(c)`, an APPEND, and
it is `addToBottom` that inserts at 0 (`CardGroup.java:455-461`). The repo's own
Ascender's-Bane-at-index-0 reasoning only works under the append reading. The
single caller is Note For Yourself (`one_time_specials.cpp`), whose Java also
appends (`NoteForYourself.java:56-66`), so a run that takes that event puts the
saved card in the wrong deck position — and master-deck ORDER is observable,
because grids and event boards index it positionally. Not fixed here: it is
another task's landed code and its tests, and B4.8 does not touch that path
(the shop's own purchase goes through the appending door). Surfaced to the
orchestrator.

#### Acceptance

Final-tree WSL Debug, leak-detecting ASan/UBSan and Release each ran the full
suite green, 1348 tests per preset, of which 30 are the new `shop_test` and
three are the new translator cases. `tools/check_stale_counts.sh`,
`tools/check_doc_links.sh` and `git diff --check` are clean. No schema, no
fixture, no golden, no registry id, no opcode, no Steam/game deployment and no
external oracle artifact changed; the four existing generated pool sort keys
were not touched.

Java provenance: `ShopScreen.java:99-136, 130-244, 246-292, 294-305, 340-428,
452-490, 592-672, 928-978`, `Merchant.java:38-97`, `ShopRoom.java:22-77`,
`StoreRelic.java:24-120`, `StorePotion.java:19-101`,
`AbstractCard.java:1915-1937, 2583-2584`, `AbstractRelic.java:173-201`,
`AbstractPotion.java:381-394`, `AbstractDungeon.java:699-753, 825-850,
1481-1620, 1687-1813`, `AbstractRoom.java:108-110, 148-186`,
`CardGroup.java:455-461, 490-552`, `AbstractPlayer.java:697-737, 1545-1553`,
`AbstractCreature.java:386-421`, `CardCrawlGame.java:465-482`,
`MealTicket.java:17-49`, `MawBank.java:16-64`, `SmilingMask.java:131-162`,
`Courier.java:181-212`, `MembershipCard.java:229-255`,
`MagicFlower.java:31-37`, `MoltenEgg2.java:35-55`,
`FastCardObtainEffect.java:19-56`, and `Soul.java:145-156`.

<a id="b48-readout"></a>

### B4.8 oracle spot-diff read-out `[x]` — 2026-07-27 (addendum to the entry above)

The blocked leg above is discharged, from a campaign taken for a different
task. `b47_treasure_oracle_20260727T204809Z_claude01` walks thirty A20 runs
under `random-legal`, and three of them reached a merchant — **STS00054,
STS00057, STS00074** — which is exactly the three-seed bar the runbook sets. No
new capture was launched and nothing under the §7.3 data root was modified.

**Those three runs hold five merchants**, because two of them enter two shop
rooms, and the set turned out to cover more than a purpose-built capture would
have been aimed at:

| Run | Floor | What the capture shows |
|---|---|---|
| STS00054 | 2 | entered and left without opening the screen — streams and pools only |
| STS00054 | 7 | full shelf; buys Havoc (59) and an Explosive Potion (56) |
| STS00057 | 5 | full shelf; purges a card for 75, ramping the run cost to 100 |
| STS00074 | 3 | full shelf; buys a Skill Potion (57) and Havoc (54) |
| STS00074 | 5 | full shelf; broke at 17 gold, buys nothing |

**All five zero-diff.** Every card id, relic id, potion id and price on the four
visible shelves; the sale slot on each; every purge cost and `purge_available`;
and on all five, `merchantRng` +16 exactly, `cardRng` +12-or-more (two of the
five spent a dedupe re-roll) and `potionRng` +3-or-more (trap-14 rejection
sampling took as many as 13 extra draws), against the first in-room record,
with the five relic-pool orders and `cardBlizzRandomizer` compared alongside. Then the whole `RunState` after every
purchase. Three visits walk end to end clean; two stop **after every purchase
has been verified**, at an out-of-combat potion discard the run layer has no
verb for.

The floor-2 merchant of STS00054 is worth keeping in the count rather than
dropping as uninteresting: the run built a shop and left without opening it, so
there is no shelf to compare — but the sixteen `merchantRng` draws, the twelve
`cardRng` draws, the twelve `potionRng` draws and the three end-pops all
happened, and all of them match. A merchant the player never looks at is still
a merchant the RNG paid for.

**The harness.** `replay_run_diff --shop`, a fourth mode on the same committed
binary. Per visit it does two things, in the B4.5 shape:

- **STOCK.** Seed a `RunState` from the capture's PRE-ENTRY record (the last
  one before the map `choose` that entered the room), call `generate_shop`, and
  compare the result against the captured `SHOP_SCREEN` and against the first
  IN-ROOM record. Seeding from before the entry is what makes the build itself
  the thing under test.
- **PURCHASES.** Restart from the first in-room record's `RunState` — which
  already carries the room-entry bookkeeping the merchant build is not
  responsible for, notably the `eventRng` draw a `?`-resolved shop costs — and
  walk the visit, diffing the whole `RunState` at every subsequent in-room
  record.

Two mechanism details are recorded in the runbook because they are not obvious
from the Java. A shop `choose i` indexes the game's `choice_list`, which is the
AFFORDABLE unsold rows by lowercased display name and renumbers after every
purchase; the harness joins that name back to the captured shelf rather than
re-deriving the affordability filter, so the read-out keeps measuring the
merchant instead of measuring a model of the menu. And the sale slot is
inferred from the capture alone — the screen carries no sale flag, so the mode
takes the colored slot with the smallest price/base ratio (base from the row's
own `rarity`) and requires both that it be `shop.sale_index` and that the ratio
really be a halving. Price equality across all seven cards would already imply
the sale slot; this makes the check independent of the simulator's answer.

The purge grid needed the same buffering the Neow read-out needed: a one-pick
grid selects on `choose`, commits on `proceed`, and `cancel` clears the
selection, and STS00057 uses all three (it picked, cancelled, picked, cancelled,
picked, confirmed). The harness accumulates picks and flushes them when the
capture confirms — which is why `grid cancel`, one of the rooms `--replay`
stops at, is now handled here.

**The traps.** Trap 1 and trap 2 both confirmed: every first shop offered
removal at exactly 75, STS00057's purge ramped the run-persistent cost to 100,
and no A20 shelf moved the purge cost while every stock price carried the x1.1.
Trap 5 held on all five merchants (sixteen `merchantRng` draws, never
seventeen), and trap 3's `cardBlizzRandomizer` never moved across a build.
Trap 4 was not exercised — none of the five popped one of the four shop-gated
relics — so it stays on `ShopDrawOrder.ShopRelicDrawsSeeTheInShopCanSpawnGate`
alone. No card-id mismatch occurred anywhere, which is what the game_id-sorted
shop pools predict. The Courier was owned by none of the three runs, so its
restock row is untouched.

**Test.** STS00074's floor-3 merchant is frozen as
`ShopCapture.B47Seed1790050543999Floor3MatchesTheRecordedMerchantAndItsPurchases`
— the sibling of the b13 vector, and the first that exercises spending: the
Skill Potion into the first free slot at 128 → 71, then Havoc appended at
71 → 17, with the removal service untouched by either.

**Acceptance:** WSL Debug, leak-detecting ASan/UBSan and Release each
1358/1358. `tools/check_stale_counts.sh` and `tools/check_doc_links.sh` clean.
No schema, fixture, golden, registry id, opcode, Steam/game deployment or
oracle artifact changed; the campaign directories were read only. This commit
also carries the B4.14 read-out above, including the `transform_card` fix,
which is why the count moved from the entry above's.

Java provenance: `ShopScreen.java:130-244, 246-292, 340-428, 592-672, 969-978`,
`Merchant.java:57-97`, `ShopRoom.java:43-77`, `StoreRelic.java:36-120`,
`StorePotion.java:33-101`, `AbstractDungeon.java:1538-1620, 1785-1789`,
`CardGroup.java:498-552`.

---

<a id="b47-readout"></a>
## B4.7 oracle spot-diff read-out — both captured treasure floors zero-diff (2026-07-28)

Read out with the new `replay_run_diff --treasure` mode (branch
`replay-readout-modes`), which mirrors `--shop`: seed a `RunState` from ONE
captured record, drive the module, diff — no prefix replay, so combat fidelity
is irrelevant. Per room: CONSTRUCTION off the pre-entry record (`++rs.floor`
first, trap 7; for a `?` node, `dispatch_event_room_entry_relics` +
`event_room_roll` which must yield TREASURE; then `roll_treasure_chest`;
`treasureRng` +2 exactly — the size roll and `randomizeReward`'s shared
contents roll — every other stream and all five relic pools unmoved, size vs
`screen_state.chest_type`); OPEN off the CHEST record (`open_treasure_chest`,
whole-`RunState` diff against the post-open record, so the optional gold draw
and the pool front-pop are proved, plus reward rows vs the first in-room
COMBAT_REWARD screen with the key row accounted for); CLAIM (a `RunController`
parked in COMBAT_REWARD driven through the capture's own claim commands,
whole-`RunState` diffed against every later in-room record).

Verbatim verdicts:

```
CHEST    OK   STS00052 floor=5 MediumChest tier=RARE gold=no treasureRng 2->4 entered via a ? node (eventRng +1 first)
WALK     OK   STS00052 floor=5 5 in-room records compared, chest skipped
CHEST    OK   STS00054 floor=9 SmallChest tier=COMMON gold=no treasureRng 3->5 entered via a 'T' node
OPEN     OK   STS00054 floor=9 seq=116 rows=[RELIC] treasureRng 5->5; capture carries the expected trailing SAPPHIRE_KEY row
CLAIM    KEY  STS00054 floor=9 seq=119: the capture claimed the SAPPHIRE_KEY row, abandoning the linked base relic (RewardItem.java:317-322)
WALK     OK   STS00054 floor=9 5 in-room records compared, chest opened
--- 2 file(s): 2 treasure room(s), construction clean 2, 1 opened (1 clean) / 1 skipped, in-room walks clean 2 (+0 partial), 0 divergence(s) ---
```

A sweep of all 161 oracle-carrying runs confirms these are the ONLY two
treasure rooms in the corpus. Between them: both entry routes (a `?` that
rolled TREASURE and a static `T` node — the mode's own first pass conflated
these and misreported a missing eventRng draw as an engine divergence, now a
named trap in the runbook), Small+Medium, COMMON+RARE, the open and the skip.

**The expected shape held, and the claim went the other way (design §11
v0.1.6).** STS00054's open carries the predicted extra trailing `SAPPHIRE_KEY`
row after the base relic (`isFinalActAvailable && !hasSapphireKey`,
`AbstractChest.java:95-96`; `AbstractRoom.addSapphireKey` `:545-547`), linked
in both directions (`RewardItem.java:86-93`). It is elided, but narrowly — a
key row on a non-treasure screen, one that is not trailing or not linked, two
of them, or a MISSING one is a divergence, except the two legitimate absences
(the run already holds a key; N'loth's Mask's `removeOneRelicFromRewards` took
relic and key together, `AbstractRoom.java:549-559`). What the ledger did NOT
predict is that the capture's `random-legal` policy claimed the **key**, not
the relic: `RewardItem.java:317-322` then marks the linked base relic
`isDone`/`ignoreReward`, so the run ABANDONED a Bag of Preparation it had
already popped — common pool 32 → 31 at `relicRng` unmoved, `relics` still
`[Astrolabe]` on the next floor — and the read-out reproduces exactly that.
Both directions frozen in
`RewardClaimMapping.ClaimingTheBaseRelicMapsThroughTheElidedIndexSpace` /
`.ClaimingTheKeyAbandonsTheLinkedRelic`. **One honest gap:** neither chest
rolled gold, so `Math.round(treasureRng.random(GOLD_AMT*0.9f, GOLD_AMT*1.1f))`
is unexercised by a live capture and stays on tier-2.

The same branch's `--event` mode read out **88 of 88 captured ?-room
sightings** (87 zero-diff + 1 clean but for the known B1.3 obtain race,
recognised narrowly; 0 diverged) across 18 distinct events in all three pools
— recorded under B4.13's block in the ledger, since that task stays open on
Match and Keep. Full tables and traps:
[`b47_treasure_spotdiff.md`](../tools/oracle_bridge/driver/b47_treasure_spotdiff.md)
§8a/§8b. Suite on the branch: debug/asan/release PASS, 0 failed of 1411
(`ctest -N`). The capture-id join is `event_id`, NOT `event_name` — for six of
the eighteen ids they differ (the game's misspelling `Transmorgrifier` is the
*id*); `EventJoin.UnknownNameFailsLoud` pins that a name is refused.

---

<a id="b413-readout"></a>
## B4.13 oracle read-outs — the arrival sweep and the Match and Keep deal (2026-07-28)

The task's ledger block, moved here on landing. **Acceptance was:** tier-2 per
event; transform draw-stream attribution named tests; Match-and-Keep's card
dealing vs oracle spot-check. The first two landed with the code (see
[#b413](#b413)); the third closed in two halves:

**ARRIVAL half — 88 of 88 captured ?-room sightings zero-diff.**
`replay_run_diff --event` (branch `replay-readout-modes`) over all 161
oracle-carrying runs of the ten campaigns reads out, per sighting, the
onEnterRoom fan-out, the one committed `eventRng` draw, `generate_event`'s
byte-identical stream, the capture-id → `EventId` join (the join is
`event_id`, NOT the display `event_name` — they differ for six of the
eighteen ids, and the game's misspelling `Transmorgrifier` is the id), and
the WHOLE arrival `RunState` (three pity floats, three membership bitsets,
`event_flags`, gold, every relic counter): 88 sightings, 87 zero-diff plus 1
clean but for the known B1.3 obtain race (recognised narrowly), 0 diverged,
across 18 distinct events, with Fountain of Cleansing's `isCursed` and The
Cleric's `gold >= 35` gates observed live on filtered pools.

**DEAL half — Match-and-Keep's card dealing vs oracle spot-check, 6 of 6
sightings zero-diff.** Match and Keep is the only Act-1 body that spends RNG
in its *constructor* (`GremlinMatchGame.java:55-61`, run at
`EventRoom.onPlayerEntry`), so the deal happens AT ARRIVAL — the `--event`
mode now runs the body's own `on_enter` before the arrival diff and compares
what it dealt. Over campaign `b4x_greedy_pilot_20260728T041406Z_claude01`
(six k3-prescanned seeds, 6/6 fired the event, both matched and unmatched
branches): all six **DEAL OK** — `cardRng` +5 (the three `getCard(rarity)`
draws and BOTH `returnRandomCurse` calls) folded into the whole-`RunState`
arrival diff, `miscRng` +1 (the board shuffle's `randomLong`, `:58`), and
`shuffleRng` compared explicitly. The board comparison is honest about what
a capture exposes: labels key by SCREEN POSITION via the fork patch's
`(i%4) + 4*(i%3)` table (`placeCards`, `:280-281`) with six of twelve slots
moved; the choice list is compacted to face-down cards; a matched pair's
identity never appears on screen. So the read-out is **30 screen positions
named by a capture and compared position-for-position, 30 attempt outcomes
reproduced as pair predicates, 60 grid rounds walked** through the engine's
own deal, plus the kept-card multiset — the only witness for a matched
pair's identity and the only thing that settles the fifth attempt. Floor
streams are DERIVED as `floor_stream(seed, floor)`, never copied from the
capture, so the unmoved `shuffleRng` is that formula checked live. The
pre-existing 88-sighting corpus re-runs byte-identical. **One honest gap
remains and is out of S1's capture reach:** every capture is A20, so
`initializeCards` takes the `ascensionLevel >= 15` branch (`:66-71`) and
draws a second curse; the `< 15` branch — the only `shuffleRng` consumer and
the only path through `draw_colorless_uncommon` — stays on its tier-2 test
and would need a sub-A15 capture, as would NoteForYourself's pool
membership. Read-out shape and per-sighting tables:
[`b47_treasure_spotdiff.md`](../tools/oracle_bridge/driver/b47_treasure_spotdiff.md)
§8c; frozen in `tests/replay_mk_board_test.cpp`.

NoteForYourself's NOTE_CARD/NOTE_UPGRADE profile pin was discharged
separately by direct reference-profile inspection (no `NOTE_*` key in any
preferences file; the event is unreachable at A20 in both game and sim) —
recorded in the ledger's Landed non-task work.

---

<a id="b55"></a>

### B5.5 `[x]` Throughput floors
**Deps:** G6 · **Spec:** design §8(4); InitialPlan §0.2 floors
**Deliverables:** benchmark additions: full-combat/sec/core (random policy)
and full-run/sec whole-machine on `bench_advance`'s pattern; methodology
notes (random-policy stand-in for 25-sim MCTS, per design §8).
**Acceptance:** release-preset numbers recorded here: ≥ 50k combat
steps/sec/core, ≥ 300 combats/sec/core, ≥ 0.4 runs/sec whole-machine — or a
stop-the-line design-doc amendment with profiling evidence (fast-but-wrong
is death; slow-but-honest gets a Stage C plan).
**Log:** Verified by running, not inferred. `bench_throughput` adds two
complete-trajectory measurements beside the existing mask-reusing
`bench_advance` batch measurement:

- `BM_RandomPolicyFullCombatPerCore` includes fresh combat construction,
  legal-mask construction, uniformly random action selection, and every
  engine step through win or loss. `items_per_second` is completed
  combats/sec/core, with `combat_steps` as an active-work cross-check.
- `BM_RandomPolicyFullA20RunWholeMachine` uses one worker per logical CPU,
  includes `run_begin`, the fuzz soak's canonical run-level move enumerator
  and random policy, and every engine step through `RunPhase::RUN_OVER`.
  Victory and death are both complete terminal trajectories. A
  `ROOM_UNIMPLEMENTED` state, empty legal mask, or action cap calls
  `SkipWithError`; the acceptance wrapper then sees no rate and fails.
  Workers cycle the exact 1,000-seed/policy-stream corpus accepted by the G6
  random-policy soak, making this a fixed performance workload rather than a
  new content sweep.
- Google Benchmark sums both counters and elapsed time across workers, so its
  unadjusted multithread rate is a per-worker average. The benchmark
  compensates from the library's source-defined aggregation rule by
  multiplying each worker's counts by the registered worker count before
  setting the rate. The reported A20 rate is therefore a whole-machine total.
- The frozen design says to measure raw random-policy full runs until the
  25-simulation MCTS harness exists. The rate is therefore not divided by 25:
  random policy is the named temporary stand-in, not a claim that one
  trajectory reproduces future search-tree reuse, branching, snapshot
  traffic, or neural-network inference. `benchmarks/README.md` records that
  boundary so Stage C can replace the surrogate honestly.

`benchmarks/run_throughput.sh` is the reproducible release floor check. It
runs each benchmark separately, requires exactly one
`items_per_second` result, normalizes the SI suffix, and exits nonzero below
any frozen floor. This is an absolute one-build check; no A/B delta was
claimed, so the interleaved `tools/bench_ab.sh` comparison was not applicable.

**Release acceptance**, Windows host command
`tools\wsl_run.cmd --script benchmarks/run_throughput.sh`, on the 8-core /
16-thread 5800X3D:

- combat batch: **27,163,500 steps/sec/core** (floor 50,000);
- complete random-policy combats: **84,624.2 combats/sec/core**, with
  **1,998,800 active combat steps/sec** (floor 300);
- complete random-policy A20 runs: **204,749 runs/sec whole-machine**, with
  **9,608,930 run steps/sec** across 16 workers (floor 0.4).

All three floors passed. No measured full run hit `ROOM_UNIMPLEMENTED`, the
action cap, or an empty legal mask; any one would have suppressed its result
and failed the wrapper. Final benchmark-enabled
`tools\wsl_run.cmd debug asan -DSTS_BUILD_BENCHMARKS=ON` and
`tools\wsl_run.cmd release -DSTS_BUILD_BENCHMARKS=ON` passed every registered
test under each preset, including the fixture oracle; the release numbers
above came only from the release preset. No engine mechanic, state schema,
registry id/opcode, fixture, golden file, or external artifact changed.
