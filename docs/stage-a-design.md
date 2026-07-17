# Stage A Design — Frozen Decisions (v0.1)

**Status:** frozen. This document is the Stage A deliverable from
[InitialPlan.md](../InitialPlan.md) §Stage A. Every decision below is binding for
Stages B/C. Amendments require evidence (a diff-harness divergence, a profiling
result, or a source-reading correction) and a version bump of this document.

Everything cited here was read from the actual game code, not from memory or
wikis. Line numbers refer to the decompiled reference tree described in §1.

---

## 1. Reference source (provenance base)

The behavioral specification is the game's own bytecode, decompiled once:

- **Class tree:** `D:\STS_BG_Mod\StSBoardGameCoopMod\ModCopy\SlayTheSpireSource` (3,102 `.class` files, includes libGDX under `com/badlogic`)
- **Jar of the same:** `D:\STS_BG_Mod\sts-classes.jar` (usable as a JVM classpath for golden-vector capture, §3.7)
- **Decompiled Java:** `D:\STS_BG_Mod\SlayTheSpireDecompiled` (CFR 0.152, 3,095 files)

**Provenance convention (frozen):** every registry entry and every engine
subsystem carries a `provenance` string of the form
`ClassName.method (File.java:line)` pointing into the decompiled tree. When the
diff harness finds a divergence, re-reading the cited Java is debugging step 1
(InitialPlan §A.6). None of the decompiled code is committed to this repo, and
no Java is transliterated — it is a specification we re-express.

## 2. Language: C++20 (deviation from InitialPlan's Rust lean, recorded)

InitialPlan §A.1 leaned Rust "unless you are materially more fluent in C++."
The repo is scaffolded as C++20/CMake and that is now frozen. The Rust
rationale was memory-safety economics; we buy that back explicitly:

- `asan`/`ubsan` preset is CI-gated alongside debug/release from day one.
- All state containers are fixed-capacity arrays with asserted bounds (§4.1) —
  no raw pointer arithmetic in rule code; the effect interpreter indexes, never
  points.
- Determinism doubles as memory-corruption detection: the fuzz farm (Stage B)
  replays every run twice and compares final state hashes; a silent stray write
  that survives asan shows up as a replay mismatch.

## 3. RNG subsystem (frozen)

This is the correctness cornerstone. Three generators must be bit-exact, not
one:

### 3.1 `RandomXS128` (xorshift128+)

Provenance: `com.badlogic.gdx.math.RandomXS128` (RandomXS128.java, whole file).

```
state: uint64 s0, s1
nextLong():  t = s0; s = s1; s0 = s;
             t ^= t << 23; s1 = t ^ s ^ (t >> 17) ^ (s >> 26);  // logical shifts
             return s1 + s
setSeed(k):  s0 = murmur3(k == 0 ? INT64_MIN : k); s1 = murmur3(s0)
murmur3(x):  x ^= x>>33; x *= 0xFF51AFD7ED558CCD; x ^= x>>33;
             x *= 0xC4CEB9FE1A85EC53; x ^= x>>33; return x
nextInt(n)   = (int)nextLong(n)
nextLong(n): loop { bits = nextLong() >>> 1; v = bits % n }
             until bits - v + (n-1) >= 0    // rejection sampling — keep the loop
nextDouble() = (nextLong() >>> 11) * 2^-53
nextFloat()  = (float)((nextLong() >>> 40) * 2^-24)   // double mult, then narrow
nextBoolean()= nextLong() & 1
```

Notes that matter: the multiplier constants are the decompiler's
`-49064778989728563` / `-4265267296055464877` reinterpreted as the unsigned
hex above; seed `0` maps to `INT64_MIN` before scrambling; the `nextLong(n)`
rejection loop must be kept even though it fires with probability ~1e-16.

### 3.2 The game's `Random` wrapper (stream + counter)

Provenance: `com.megacrit.cardcrawl.random.Random` (Random.java, whole file).

Every stream is `{ RandomXS128 rng; int32 counter }`. Every public method
increments `counter` by 1 and consumes **exactly one** `nextLong` (the sole
exception is the ~1e-16 rejection case above). This invariant is why the
game's own save/restore-by-counter scheme works, and we adopt the API surface
verbatim:

| Game method | Semantics |
|---|---|
| `random(int bound)` | `nextInt(bound + 1)` — **inclusive** bound |
| `random(int lo, int hi)` | `lo + nextInt(hi - lo + 1)` — inclusive |
| `random(long bound)` | `(long)(nextDouble() * bound)` |
| `random(long lo, long hi)` | `lo + (long)(nextDouble() * (hi - lo))` |
| `randomLong()` | `nextLong()` |
| `randomBoolean()` | `nextBoolean()` |
| `randomBoolean(float p)` | `nextFloat() < p` |
| `random()` / `random(float…)` | `nextFloat()` scaled |

Restore semantics: the game rebuilds a stream as
`Random(seed, counter)` = fresh seed, then `counter` × `random(999)` replay
(Random.java:28). `setCounter` replays `randomBoolean()` instead
(Random.java:42) — different values, same number of `nextLong` calls, so the
resulting engine state is identical. **Our representation skips replay
entirely:** a stream is stored as `{ uint64 s0, s1; int32 counter }` (24 B) —
restore is a copy, fork is a copy. The counter is kept only for save-file
compatibility and oracle diffing.

### 3.3 `java.util.Random` LCG + `Collections.shuffle` — also load-bearing

Every deck/pool shuffle in the game is
`Collections.shuffle(list, new java.util.Random(shuffleRng.randomLong()))` —
CardGroup.java:561-567, AbstractDungeon.java:1101 and 1237-1241. So we must
also implement, bit-exactly:

- the JDK LCG: `seed = (seed ^ 0x5DEECE66D) & ((1<<48)-1)`, then
  `next(bits)`: `seed = (seed * 0x5DEECE66D + 0xB) & ((1<<48)-1); return (int)(seed >>> (48-bits))`,
  with `nextInt(bound)`'s JDK power-of-two / rejection logic;
- `Collections.shuffle`'s exact Fisher–Yates: for `i = size-1 … 1`,
  `swap(list[i], list[rnd.nextInt(i+1)])`.

One stream draw (`randomLong`) is consumed per shuffle; the shuffle itself
consumes JDK draws, not stream draws.

### 3.4 Stream inventory and lifecycle

Provenance: AbstractDungeon.java:149-161 (declarations), 398-412
(`generateSeeds`), 425-432 (`loadSeeds` — the persisted set), 1747-1751
(per-floor reseed), Exordium/TheCity/TheBeyond/TheEnding `.java` (mapRng),
NeowEvent.java:289/363.

| Stream | Lifecycle | Seeding | Used for |
|---|---|---|---|
| `monsterRng` | run-scoped | `seed`, counter persists | encounter rolls |
| `eventRng` | run-scoped | `seed` | ?-room resolution, event rolls |
| `merchantRng` | run-scoped | `seed` | shop stock/prices |
| `cardRng` | run-scoped | `seed` | reward card pools, rarity, upgrades |
| `treasureRng` | run-scoped | `seed` | chest contents |
| `relicRng` | run-scoped | `seed` | relic pool shuffles & rolls |
| `potionRng` | run-scoped | `seed` | potion drops & identity |
| `monsterHpRng` | **floor-scoped** | `seed + floorNum` | monster max-HP rolls |
| `aiRng` | **floor-scoped** | `seed + floorNum` | monster move selection |
| `shuffleRng` | **floor-scoped** | `seed + floorNum` | deck shuffles (via JDK LCG) |
| `cardRandomRng` | **floor-scoped** | `seed + floorNum` | in-combat card randomness (random targets, discoveries, Snecko) |
| `miscRng` | **floor-scoped** | `seed + floorNum` | everything else |
| `mapRng` | act-scoped | `seed + actNum × {1, 100, 200, 300}` for acts 1/2/3/4 | map generation |
| Neow's `rng` | event-scoped | fresh `Random(seed)` | Neow blessing options |

The floor-scoped reseed happens in `AbstractDungeon.nextRoomTransition`
(AbstractDungeon.java:1747-1751) **after** `floorNum` is incremented — this is
the mechanism behind "unrelated actions on one floor don't perturb the next
floor" and it is exactly what makes determinized MCTS forking cheap for
combat: all five combat-relevant streams are functions of `(seed, floor)` plus
their in-combat counters, all held inside `CombatState`.

Quirk recorded for Stage B: `?`-rooms roll on a **duplicate** of `eventRng`
restored from `(seed, counter)` and commit the duplicate back only when a real
event happens (AbstractDungeon.java:1766-1770, EventRoom.java:28). Under the
one-draw invariant the duplicate's engine state equals the original's, so the
sim just rolls `eventRng` directly; the branch only matters for save-loading,
which the sim doesn't have.

Pity counters riding on these streams, both persisted in the save file and
therefore fields of `RunState`: `cardBlizzRandomizer` (rare/uncommon reward
bias: starts at 5, added to the rarity roll, self-adjusting —
AbstractDungeon.java:1599/2773, SaveFile.java:252) and
`AbstractRoom.blizzardPotionMod` (±10 % potion-drop ratchet —
AbstractRoom.java:584-606, SaveFile.java:226).

### 3.5 Seed format

Provenance: SeedHelper.java (whole file). Display alphabet is the 35-character
string `0123456789ABCDEFGHIJKLMNPQRSTUVWXYZ` (no `O`; `O` input maps to `0`).
String→long is plain base-35 accumulation into a signed 64-bit with natural
overflow wrap; long→string treats the seed as **unsigned**. Both directions
implemented exactly; a seed string copied from the desktop game must address
the identical run.

### 3.6 In-state representation

`RngStream = { uint64 s0, s1; int32 counter; int32 _pad }` — 24 B, POD.
`RunState` holds all run-scoped streams plus `mapRng`; `CombatState` holds the
five floor-scoped streams. Forking a determinization = memcpy of the state;
pinning the shuffle while sampling AI = copy `shuffleRng`, reroll `aiRng`.
No stream ever lives outside a state struct (InitialPlan §0.1's
"constructible from bytes alone").

### 3.7 Acceptance tests (gate every commit — verification tier 1)

Golden vectors are captured by a **tiny JVM harness run against
`sts-classes.jar` directly** — no game launch, no CommunicationMod needed for
tier 1: a ~50-line Java program instantiates `RandomXS128`,
`com.megacrit.cardcrawl.random.Random`, and `java.util.Random` +
`Collections.shuffle`, and dumps streams to files under `tests/golden/`.
Frozen test set:

1. `RandomXS128`: first 10k `nextLong` for seeds {0, 1, -1, INT64_MIN,
   INT64_MAX, "SPIRE" as base-35, 32 randoms} — bit-exact.
2. Wrapper methods: 1k draws of each method/overload per seed — bit-exact,
   counter checked.
3. Counter-restore equivalence: state after N mixed draws == state after
   `Random(seed, N)` replay.
4. JDK LCG + shuffle: shuffled permutations of 1..N for N ∈ {5, 10, 71, 128}
   across the seed battery — element-exact.
5. Floor derivation: streams for `(seed, floor 1..55)` and `mapRng` for all
   four act constants.
6. `SeedHelper` round-trips including negative longs and `O`→`0`.

## 4. State structs (frozen shape, sizes budgeted not byte-frozen)

### 4.1 Principles

Trivially copyable, no pointers, no heap, fixed capacities with counts;
`static_assert(std::is_trivially_copyable_v<T>)` and size ceilings on every
state type. Overflow of any capacity is a hard assert (fail loudly). All
cross-references are indices, never pointers. Snapshot = `memcpy`; hash =
xxh3 over the struct bytes with padding zero-initialized (states are
value-initialized before use so padding compares equal).

### 4.2 `CombatState` — budget ≤ 4 KB

| Group | Contents | Capacity |
|---|---|---|
| header | schema version, phase, turn, flags | — |
| player | hp/maxHp/block/energy, stance id, counters (cardsPlayedThisTurn, …) | — |
| powers (player) | `{power_id: u16, amount: i16}` | 24 |
| card instances | `{card_id: u16, upgrade: u8, cost_now: u8, flags: u16, misc: u16}` — one pool, piles reference it | 160 |
| piles | hand / draw / discard / exhaust / limbo as index lists + counts | 10 / 128 / 128 / 128 / 8 |
| monsters | `{hp, maxHp, block, move history ×3, intent, flags}` + 24 powers each | 5 |
| action queue | §5, fixed ring `{opcode: u16, src: u8, tgt: u8, amount: i32, flags: u32}` | 64 |
| card/monster queues | `CardQueueItem`/`MonsterQueueItem` equivalents | 16 / 5 |
| RNG | the 5 floor-scoped streams (§3.6) | 120 B |

Estimated ~3.3 KB. The 160-instance card pool (deck ≤ 128 plus generated
Status/temp cards) is the riskiest cap; the assert data from Stage B fuzzing
will tell us if it needs to grow before Stage C freezes bytes.

### 4.3 `RunState` — budget ≤ 8 KB

Master deck (128 × card instance), relics (`{relic_id: u16, counter: i16}` ×
40, **order = acquisition order**, which is trigger order), potions × 5, gold,
hp/maxHp, ascension, act, floor, map (7×15 node grid: `{room_type: u8,
edges: u8}`), boss ids, keys, event/shop one-shot flags, the 8 run-scoped
streams + `mapRng` (§3.4), `cardBlizzRandomizer`, `blizzardPotionMod`, and the
run seed itself. `CombatState` is *derived* from `RunState` at combat start
and folded back at combat end; they never alias.

### 4.4 Layout policy

AoS (array-of-states) now; the interpreter is written against accessor
functions so a Stage C SoA re-bucketing (or the CUDA port's SoA) does not
touch rule code. Exact byte offsets are frozen at the *end* of Stage B (they
become schema v1); until then the schema version stamps guard every trajectory.

## 5. Action queue (frozen semantics, from `GameActionManager`)

Provenance: GameActionManager.java (whole file, esp. `getNextAction`
lines 185-367).

### 5.1 Structures

Four queues + flags, replicated as fixed rings in `CombatState`:

- `actions` — main list. `addToBottom` appends, `addToTop` prepends
  (lines 96-100, 139-143). Both are no-ops outside combat phase.
- `preTurnActions` — `addToTurnStart` prepends (line 145).
- `cardQueue` — pending card plays; a **null-card item is the end-turn
  sentinel** that triggers the end-of-turn sequence (line 198-199).
  "Front-of-queue" insertion goes to index **1**, not 0, when non-empty
  (line 102-108) — the currently-resolving card stays at head.
- `monsterQueue` — monsters awaiting their turn, plus flag
  `monsterAttacksQueued`.

### 5.2 `getNextAction` priority order (the heart of resolution ordering)

1. `actions` non-empty → pop front, execute.
2. else `preTurnActions` non-empty → pop front, execute.
3. else `cardQueue` non-empty → resolve head card play (see 5.3), or the
   end-turn sentinel.
4. else if `!monsterAttacksQueued` → set it, enqueue all monsters
   (`queueMonsters`) unless `skipMonsterTurn`.
5. else `monsterQueue` non-empty → pop head monster; if alive (or halfDead):
   `takeTurn()` then `applyTurnPowers()`.
6. else if `turnHasEnded` and monsters alive → **start-of-turn sequence**:
   monsters' end-of-turn powers → reset per-turn counters →
   player `applyStartOfTurnRelics` → `applyStartOfTurnPreDrawCards` →
   `applyStartOfTurnCards` → `applyStartOfTurnPowers` →
   `applyStartOfTurnOrbs` → `turn++` → block decay (skip if Barricade/Blur;
   Calipers = lose 15) → queue `DrawCardAction(gameHandSize)` →
   `applyStartOfTurnPostDrawRelics` → `applyStartOfTurnPostDrawPowers` →
   enable end turn. (lines 329-366)
7. else → control returns to the player (WAITING_ON_USER).

The engine's step function is: apply player action → pump this loop until
WAITING_ON_USER or combat end. Pure-presentation actions (SetAnimation, VFX,
SFX, ShakeScreen, ShowMoveName, IntentFlash, Wait) are **not emitted at all**;
the frozen rule for in/out scope: an action is in scope iff it mutates
gameplay state or its queue position can reorder something that does.

### 5.3 Card-play hook order (lines 214-249)

When a card play resolves from `cardQueue` (and unless `dontTriggerOnUseCard`):
player powers `onPlayCard` → each monster's powers `onPlayCard` → relics
`onPlayCard` (acquisition order) → stance → blights → hand cards
`onPlayCard` → discard pile cards → draw pile cards → counters increment →
`player.useCard(...)`. Random-target cards roll
`getRandomMonster(cardRandomRng)` at *dequeue* time (line 212). A queued
enemy-target card whose target died is exhausted from limbo without effect
(lines 265-280).

### 5.4 End-of-turn sequence (sentinel path, lines 369-377)

Room `applyEndOfTurnRelics` → `applyEndOfTurnPreCardPowers` → orbs trigger →
each hand card `triggerOnEndOfTurnForPlayingCard` (Burns/Regret/Decay queue
their card plays here) → stance `onEndOfTurn`. Hand discard happens via the
queued actions that follow, then monster turn starts via step 4.

### 5.5 Damage pipeline (frozen as the DAMAGE opcode semantics)

Provenance: DamageInfo.java:35-100. Damage accumulates in **`float`** and is
floored (`MathUtils.floor`) once at the end, clamped ≥ 0. Order for a
player-owned attack: owner powers `atDamageGive` (Strength +N) → stance
`atDamageGive` → target powers `atDamageReceive` (Vulnerable ×1.5) → owner
powers `atDamageFinalGive` → target powers `atDamageFinalReceive`. For
monster-owned attacks the stance hook is `atDamageReceive` (player side) and
sits after target `atDamageReceive`. Power iteration order = power list
order = application order. C++ must use `float` intermediates to reproduce
rounding exactly — integer shortcuts are forbidden in the damage opcode.

Monster max HP roll: `monsterHpRng.random(minHp, maxHp)` inclusive
(AbstractMonster.java:765-775).

## 6. Rules-as-data

Registry lives in `registry/` as YAML, one file per domain (`cards.yaml`,
`relics.yaml`, `powers.yaml`, `monsters.yaml`), each entry carrying
`provenance`. A CMake codegen step turns the registry into `constexpr` opcode
tables compiled into the engine — the YAML is never parsed at runtime.

Effect programs are sequences of `{op, target, amount, flags}` over an opcode
set that starts minimal for the skeleton: `DAMAGE`, `BLOCK`, `APPLY_POWER`,
`DRAW`, `GAIN_ENERGY`, `SHUFFLE_IN`, `EXHAUST`, `ROLL_MOVE`. Monster AI is
data too: move tables with roll thresholds + history constraints
(`lastMove`/`lastTwoMoves`), matching the universal `getMove(aiRng.random(99))`
shape — genuinely bespoke logic gets a named native branch (budget ~15 %).

## 7. Batch API (the only public API, per D0.1)

```cpp
namespace sts {
// combat skeleton signature; run-level advance lands with S1
struct Action { uint32_t bits; };  // verb:8 | arg0:8 | arg1:8 | arg2:8
// verbs: PLAY_CARD(hand_idx, target), END_TURN, USE_POTION(slot, target),
//        CHOOSE(option_idx)  — one enum, all phases
void advance(std::span<CombatState> states,
             std::span<const Action> actions,
             std::span<StepResult> results);   // heterogeneous batch, no sync
void legal_actions(const CombatState&, ActionMask& out);
void encode_observation(const CombatState&, ObsBuffer& out); // in-engine, D0.3
}
```

Single-state convenience wrappers exist in tests only. `StepResult` carries
terminal flag, reward fields, and an observation view — no allocation
anywhere in the loop.

## 8. Trajectory schema v0

Schema = the state structs themselves (they are POD): a trajectory file is
`{magic 'STS0', schema_version u32, state_size u32, record[]}` where a record
is `{RunState-or-CombatState bytes, action u32, aux}`. Serialization is
`memcpy` (meets the ≤ 1 µs contract trivially); loaders refuse mismatched
`schema_version`. The version constant lives in one header and **must** be
bumped by any struct edit — enforced by a static hash of field layout checked
in CI (a `static_assert` on `sizeof` per struct plus a golden layout test).

## 9. Walking skeleton (M1 scope)

One combat, no run layer: **Ironclad vs. Jaw Worm**, A20 stats.

**Enemy — Jaw Worm** (JawWorm.java): HP 42-46 at A7+ (40-44 below); A17+
tables: Chomp 12, Bellow +5 Str/9 block, Thrash 7 + 5 block. Move logic
(getMove, lines 149-184): first turn forced Chomp; then roll d100 on `aiRng`:
`<25` Chomp (unless last move was Chomp → 56.25 % Bellow else Thrash), `<55`
Thrash (unless last two → 35.7 % Chomp else Bellow), else Bellow (unless last
→ 41.6 % Chomp else Thrash). Exercises: aiRng, move history, Strength,
enemy block.

**Five cards:** Strike (6 dmg), Defend (5 block), Bash (8 dmg + Vulnerable 2,
2-cost), Shrug It Off (8 block, draw 1), Pommel Strike (9 dmg, draw 1).
Together they exercise: the damage pipeline with Strength and Vulnerable
stacking, block, mid-turn draw with empty-draw-pile reshuffle (shuffleRng +
JDK LCG), targeting, and multi-cost cards. Deck = 5× Strike, 4× Defend,
1× Bash, 1× Shrug It Off, 1× Pommel Strike (real Ironclad starter shape with
the two additions).

**Diff harness, wired end to end now:** the trace format and differ are the
permanent Stage B artifacts; only the oracle adapter is provisional.
A trace is `(seed, action[]) → state snapshot after every action`, states
compared field-by-field with named-field diff output and auto-saved
`(seed, action-prefix)` reproducer. M1 oracle = the JVM golden harness (§3.7)
for all RNG streams **plus** hand-derived combat fixtures computed from the
cited Java for ~20 scripted Jaw Worm fights across seeds. The
CommunicationMod live oracle replaces the fixture adapter in Stage B without
touching the differ.

**M1 exit (from InitialPlan milestone table):** skeleton passes the diff
harness on the 5-card micro-game; tier-1 RNG suite green across the seed
battery; `advance()` batch smoke benchmark runs (no perf target yet).

### Build order (each step lands with its tests)

Execution tracking, task granularity, and acceptance commands live in
[stage-a-tasks.md](stage-a-tasks.md); the summary below is the shape only.

1. `RandomXS128` + wrapper + JDK LCG/shuffle + `SeedHelper` + JVM capture
   harness → tier-1 suite green.
2. `CombatState`/`RunState` structs + static asserts + hash + snapshot test.
3. Action-queue pump with the §5.2 ordering, `END_TURN` only (no cards) —
   Jaw Worm takes turns, move history works, aiRng counters match fixtures.
4. Effect interpreter + 5 cards + damage pipeline → registry unit tests
   (tier 2) for every entry.
5. Batch `advance()` + legal-action mask + observation encoder stub.
6. Differ + trace format + fixture oracle → M1 acceptance run.

## 10. Bit-exactness trap list (accumulated; each is a test)

1. Damage math is `float` accumulation floored once — no integer shortcuts.
2. Shuffles route through `java.util.Random`, not xorshift.
3. `random(int)` bounds are **inclusive** (`nextInt(range+1)`).
4. `nextLong(n)` rejection loop exists; do not "simplify" to modulo.
5. Seed 0 scrambles as `INT64_MIN`.
6. Seed strings are unsigned in long→string, signed-wrapping in string→long.
7. Floor reseed uses the floor number **after** increment.
8. Relic trigger order is acquisition order (list order everywhere).
9. `cardQueue` front-insertion is index 1 when busy.
10. Random targets roll at dequeue, not enqueue.
11. `nextFloat` narrows a double product — do the multiply in double, then
    truncate to float.

## 11. Deliberately deferred to Stage B

Event pool/removal bookkeeping, shop pricing formulas, Neow option tables,
map-generation internals (mapRng consumption order), A20 modifier table
(registry entries with provenance, incl. Ironclad A14+ starting-HP change —
numbers not restated here without a source read), potion identity rolls,
CommunicationMod fork patch list (which hidden counters to expose: all 13
stream counters + `cardBlizzRandomizer` + `blizzardPotionMod`).

## 12. Change log

Non-freezing amendments (additive storage fixes and source-vs-paraphrase
corrections found during Stage A execution). None change frozen mechanics; each
cites the provenance that outranks the losing document (§1 precedence:
decompiled Java > this design doc > the task ledger).

- **A2.2 — RunState stream count (§3.4 vs §4.3).** §4.3's prose "8 run-scoped
  streams + mapRng" over-counts §3.4's authoritative 7-row run-scoped inventory
  (monsterRng, eventRng, merchantRng, cardRng, treasureRng, relicRng,
  potionRng). §3.4 wins: `RunState` carries 7 run-scoped `RngStream`s +
  `map_rng` = 8 stream fields. §4.3's wording left as an imprecise count; not a
  mechanics change.

- **A3.1 — `preTurnActions` storage gap (§4.2 table vs §5.1).** §4.2's capacity
  table lists only two queue rows ("action queue", "card/monster queues"),
  under-specifying §5.1's FOUR-queue structure; A2.2 therefore allocated
  `CombatState` storage for only `action_queue`, `card_queue`, `monster_queue`
  and omitted `preTurnActions`. §5.1 wins: A3.1 added a `pre_turn_actions` ring
  (capacity 16, same `ActionQueueItem` element type as the main ring) plus a
  `turn_has_ended` bookkeeping flag. Additive only (new fields, no renames);
  `sizeof(CombatState)` 3312 → 3504 B, still ≤ 4096. A future §4.2 table row
  should list `preTurnActions` explicitly.

- **A3.1 — `monsterAttacksQueued` reset placement (getNextAction).** The
  decompiled `GameActionManager.java` initializes `monsterAttacksQueued = true`
  and re-sets it `true` at line 304 but NEVER sets it `false` anywhere in the
  reference tree (grep-confirmed: 3 occurrences, one file). Taken literally the
  flag can never re-open getNextAction step 4, so monsters would be queued at
  most once — not the game's observable per-turn behavior, so this decompiled
  copy is missing the reset. The sim clears the flag when the turn *ends* (the
  end-turn sentinel, §5.2 step 3), priming step 4 to queue monsters exactly once
  and leaving it set through the whole next player turn; the §5.2-step-6
  start-of-turn sequence does NOT touch it. This matches step 6's actual
  enumeration in the Java (`cardsPlayedThisTurn`, `turnHasEnded`, discard/damage
  counters cleared — not `monsterAttacksQueued`; GameActionManager.java:333,
  349-351) and keeps the invariant "monster_attacks_queued is true throughout
  the player's turn" so A4.3 card-play pumps never spuriously fire step 4. Not a
  frozen-mechanics change (the observable ordering — player turn, then one
  monster turn per round — is preserved).

- **A4.2 — hand-size-cap rule (task-ledger prose correction).** The Stage A
  task ledger's A4.2 deliverable line described the hand-size-10 overflow as
  "drawn card goes to discard". `DrawCardAction.update()`
  (DrawCardAction.java:92-97) does NOT draw-then-discard: before drawing any
  card it caps the amount once via `if (amount + hand.size() > 10) amount += 10 -
  (amount + hand.size())`, i.e. `amount = min(amount, 10 - hand.size())`, so
  overflowing cards are never drawn (and `AbstractPlayer.draw()`, :1657-1665,
  refuses outright when `hand.size() == 10`). A4.2 (`piles.cpp::draw_cards`)
  implements the cap-before-draw rule. This design doc's §9 skeleton scope never
  stated the wrong rule — only the ledger's deliverable prose did — so this is a
  ledger-prose correction recorded here for durability; no frozen mechanics
  change. Java (§1 precedence) outranks the ledger.
