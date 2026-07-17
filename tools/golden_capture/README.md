# golden_capture

Windows-host-only JVM tool for task A0.1 (see
[`docs/stage-a-tasks.md`](../../docs/stage-a-tasks.md) and
[`docs/stage-a-design.md`](../../docs/stage-a-design.md) §3.7). It runs a
small Java program against `D:\STS_BG_Mod\sts-classes.jar` to capture
bit-exact golden vectors for the game's three RNG primitives
(`RandomXS128`, the `com.megacrit.cardcrawl.random.Random` wrapper, and the
JDK LCG + `Collections.shuffle`), plus the floor/act stream-derivation
formulas and `SeedHelper` string↔long conversion. These vectors are the
tier-1 oracle that `A1.1`–`A1.4`'s C++ gtest binaries byte-compare against.

This tool is **not** built via CMake/WSL — it only ever runs on the Windows
host, against the desktop `javac`/`java` (25.0.1) on `PATH`.

No decompiled Java or anything extracted from `sts-classes.jar` is committed
here — only this harness's own source and the golden `.bin`/`.txt` files it
produces under `tests/golden/` (small, binary via `.gitattributes`).

## Regenerating

```powershell
pwsh tools/golden_capture/regen.ps1
```

or with non-default jar locations:

```powershell
pwsh tools/golden_capture/regen.ps1 -StsClassesJar "D:\path\to\sts-classes.jar" -DesktopJar "D:\path\to\desktop-1.0.jar"
```

The script:
1. Wipes and recreates `tools/golden_capture/build/` and `tests/golden/`.
2. Compiles `src/GoldenCapture.java` against `sts-classes.jar`.
3. Runs it, writing every golden file into `tests/golden/`.

Every golden file is a pure function of `GoldenCapture.java`'s source (fixed
seed battery, fixed call sequences) plus the game bytecode, so **running the
script twice must produce byte-identical output** — verified for this task
via `Get-FileHash -Algorithm SHA256` over all 306 output files, compared
between two consecutive runs (zero diffs).

### Why two jars?

`com.megacrit.cardcrawl.random.Random` has a `static final Logger logger =
LogManager.getLogger(...)` field, so simply loading the class throws
`NoClassDefFoundError: org/apache/logging/log4j/LogManager` unless log4j
classes are resolvable — `sts-classes.jar` does not bundle them. The real
desktop game jar (`desktop-1.0.jar`, from a normal Steam install) is a fat
jar that does bundle log4j, so it's added to the classpath purely to satisfy
that static initializer. Nothing is extracted from either jar; both are only
ever `-cp` entries for a JVM process that exits immediately after writing
files.

## Provenance

Every class/behavior this harness exercises was read from the decompiled
tree at `D:\STS_BG_Mod\SlayTheSpireDecompiled` before writing
`GoldenCapture.java` (see the file header comment for the full citation
list): `RandomXS128.java` (whole file), `Random.java` (whole file),
`SeedHelper.java` (whole file), `CardGroup.java:561-567`,
`AbstractDungeon.java:1747-1751` and `:2562-2563`, `Exordium.java:56`,
`TheCity.java:46`, `TheBeyond.java:44`, `TheEnding.java:49`.

One correction to the design doc's paraphrase, confirmed by reading all four
act dungeon constructors (not just Exordium's): `mapRng`'s seed is
`Settings.seed + actNum * mult` where `actNum` is 1/2/3/4 for
Exordium/TheCity/TheBeyond/TheEnding (incremented in
`AbstractDungeon.dungeonTransitionSetup` before the act constructor runs) and
`mult` is a **per-class** constant 1/100/200/300 — i.e. the design doc's
"`seed + actNum × {1, 100, 200, 300}`" is actually two different quantities
multiplied together, not one lookup table indexed by actNum alone. The
resulting four offsets (`seed+1`, `seed+200`, `seed+600`, `seed+1200`) match
what `stage-a-design.md` §3.4 intends either way, so no design-doc change was
needed — recorded here since it's easy to misread.

## Binary format

All multi-byte binary values are **big-endian** (Java `DataOutputStream`'s
native order — chosen only because it required no manual byte-swapping code;
the C++ readers written in `A1.1`–`A1.4` must match). Text files are UTF-8,
LF line endings, tab-separated fields.

A `seed_battery.txt` manifest (label → signed decimal seed value, one per
line) is also written; it's not one of the six frozen categories but exists
so filenames' `<seed>` labels are traceable back to actual `long` values.

### Seed battery (labels used in every filename below)

Per §3.7: `{0, 1, -1, INT64_MIN, INT64_MAX, "SPIRE" as base-35, 32 "random"
seeds}` — labelled `0`, `1`, `-1`, `MIN`, `MAX`, `SPIRE`, `r0`..`r31`. The 32
"random" seeds are generated deterministically by
`new java.util.Random(42).nextLong()` called 32 times — fixed purely so the
battery itself is reproducible; this has nothing to do with any in-game RNG
class.

### 1. `xs128_<seed>.bin`

First 10,000 `RandomXS128(seed).nextLong()` values, raw `int64` BE, no
header. File size = 80,000 bytes.

### 2. `wrapper_<seed>.bin`

Ten blocks, one per `com.megacrit.cardcrawl.random.Random` method/overload,
**each block uses a fresh `Random(seed)` stream** (so a block is
independently replayable without needing to replay earlier blocks). Block
layout:

```
header: { method_id: int32, arg0: int64, arg1: int64 }
records[1000]: { counter_after: int32, result_bits: int64 }
```

Method-id table (args are the fixed, canonical arguments this harness always
passes — see `methodArg0`/`methodArg1` in the source):

| id | method | arg0 | arg1 |
|----|--------|------|------|
| 0 | `random(int range)` | range=999 | — |
| 1 | `random(int start, int end)` | start=-500 | end=500 |
| 2 | `random(long range)` | range=1e12 | — |
| 3 | `random(long start, long end)` | start=-5e11 | end=5e11 |
| 4 | `randomLong()` | — | — |
| 5 | `randomBoolean()` | — | — |
| 6 | `randomBoolean(float chance)` | chance=0.5f | — |
| 7 | `random()` | — | — |
| 8 | `random(float range)` | range=100.0f | — |
| 9 | `random(float start, float end)` | start=-50.0f | end=50.0f |

Float-typed args/results are stored as their raw IEEE-754 bit pattern
(`Float.floatToRawIntBits`), **zero-extended** into the `int64` slot (these
are bit patterns, not values — read the low 32 bits and
`bit_cast<float>`). `result_bits` for `int`/`long`/`boolean` methods is the
value itself (sign-extended for `int`, verbatim for `long`, `0`/`1` for
`boolean`).

### 3. `counter_restore_<seed>.bin`

Tests design doc §3.2's replay-equivalence claim directly:

```
{ N: int32 }
{ direct:            s0: int64, s1: int64, counter: int32 }   // N mixed drawOnce() calls, method ids 0..9 cycling
{ ctor_replay:        s0: int64, s1: int64, counter: int32 }   // new Random(seed, N)          -- replays N x random(999)
{ set_counter_replay: s0: int64, s1: int64, counter: int32 }   // new Random(seed); .setCounter(N) -- replays N x randomBoolean()
```

`N = 137` (fixed). Verified for every battery seed: all three blocks'
`(s0, s1, counter)` are identical — e.g. for seed `0`, all three read
`s0=8512813718599753174, s1=-4598977676146024964, counter=137`.

### 4. `jdk_shuffle_<seed>_<n>.bin`

`n ∈ {5, 10, 71, 128}`. `Collections.shuffle(1..n, new java.util.Random(seed))`
— the battery seed feeds `java.util.Random` **directly** (not via
`shuffleRng.randomLong()`), per task instructions, to isolate the JDK LCG +
shuffle algorithm from the game's stream-draw layer (confirmed against
`CardGroup.java:561-567` that the game always constructs the JDK `Random`
this way, just fed a stream-drawn `long` instead of a raw seed in the real
game). Content: `n` × `int32` BE, the shuffled permutation of `1..n`.

### 5. `floor_derive_<seed>.bin`

```
for floor in 1..55:
  for stream in [monsterHpRng, aiRng, shuffleRng, cardRandomRng, miscRng]:
    { s0: int64, s1: int64 }   // state of new Random(seed + floor) -- pre-draw
for act in [1, 2, 3, 4]:
  { s0: int64, s1: int64 }     // state of new Random(seed + mapOffset[act]); offsets = {1, 200, 600, 1200}
```

File size = `(55*5 + 4) * 16` = 4,464 bytes. Confirmed for every battery
seed: **all 5 floor streams are bit-identical at floor entry** (e.g. floor 1,
seed `0`: every stream reads `s0=-5451962507482445012,
s1=9038243705893100514`) — they're all constructed from the literal same
seed value per `AbstractDungeon.java:1747-1751`, and only diverge once
combat starts drawing from them at different rates. Worth knowing before
writing the C++ side, since it looks like a bug until you've read the
source.

### 6. `seedhelper.txt`

Plain text, tab-separated. Three row kinds:
- `RT\t<label>\t<original_long>\t<string>\t<roundtrip_long>\t<match 1|0>` —
  one per battery seed (covers `0`, negatives, `MIN`, `MAX`). `getString(0)`
  is the empty string (`SeedHelper.getString`'s loop never executes for
  `leftover == 0`) — the `RT` row for label `0` has an empty `<string>`
  field, so it parses as `RT\t0\t0\t\t0\t1` (adjacent tabs).
- `OTEST\t<label>\t<original_long>\t<string_with_0>\t<string_with_O>\t<getLong(string_with_O)>\t<match 1|0>`
  — one row: takes the first battery string containing a `0` digit,
  substitutes one `0`→`O`, and confirms `getLong` maps it back to the same
  original long (`SeedHelper.java:78`'s `.replaceAll("O", "0")`).
- `VALIDCHAR\t<input_char>\t<mapped_char_or_NULL>` — four rows exercising
  `SeedHelper.getValidCharacter` (`O`→`0`, `o`→`0` [uppercased first], `0`→`0`,
  `!`→`NULL`).

All `match` columns are `1` for every row in the current capture — confirmed
by inspection, not just assumed.
