# diff_harness (A6.1)

The differential-testing harness for the simulator: a **trace file format**, a
**field-by-field `CombatState` differ**, a **reproducer emitter**, and an
**oracle adapter interface** (design doc §8, §9). It is the permanent Stage B
artifact that A6.2 (20 scripted Jaw Worm fixtures) and the eventual
CommunicationMod live oracle build on — only the oracle *adapter* is
provisional; the trace format and differ never change.

Unlike `tools/golden_capture/` (a Windows-host JVM tool with no CMake wiring),
this is a normal C++ target in the WSL/CMake build: a static library
`diff_harness` linked by the `differ_test` gtest binary (and any future CLI).

## Components

- **`trace.hpp` / `trace.cpp`** — trace file I/O. A trace is the on-disk form of
  "(seed, action[]) → state after every action".
- **`differ.hpp` / `differ.cpp`** — `diff_states(expected, actual)` → a list of
  named `FieldDiff{field_name, expected_repr, actual_repr}`.
- **`reproducer.hpp` / `reproducer.cpp`** — `(seed, action-prefix)` save/load.
- **`oracle.hpp` / `oracle.cpp`** — `OracleAdapter` interface +
  `FixtureFileOracleAdapter` (loads trace files as fixtures). The
  CommunicationMod adapter is a documented Stage B stub, not implemented here.
- **`replay.hpp` / `replay.cpp`** — drives `combat_begin` + `advance()` to
  produce the snapshot sequence a trace/fixture records or a reproducer replays.

## Trace file format (v0, design doc §8)

Little-endian, single-platform (this is a debug artifact, not a cross-platform
wire format — the producer and consumer are the same WSL x86-64 build).

```
TraceHeader (24 bytes, no padding):
  char     magic[4]        = {'S','T','S','0'}
  uint32_t schema_version  = SCHEMA_VERSION      (loaders REFUSE a mismatch)
  uint32_t state_size      = sizeof(CombatState) (second safety check)
  uint32_t record_count
  int64_t  seed                                  (extension of §8's container;
                                                  needed to make a trace replayable)

record[record_count], each:
  CombatState state   (raw memcpy of sizeof(CombatState) bytes)
  uint32_t    action  (the Action.bits that produced this state; 0 for record[0])
  uint32_t    aux     (bit 0 = terminal, i.e. phase == COMBAT_OVER; bits 1..31 = 0)
```

**Record convention:** `record[0]` is the initial state from `combat_begin`
(action `0`); `record[k]` (k ≥ 1) is the state after `actions[k-1]` (action ==
`actions[k-1].bits`). A trace of N actions has N+1 records. `write_trace`
therefore requires `states.size() == actions.size() + 1`.

`read_trace` refuses (returns `false`, never loads garbage) any file whose
magic, `schema_version`, or `state_size` does not match the current build.

## Reproducer file format (v1)

Plain text, human-readable and hand-editable:

```
STSREPRO v1
seed <int64 decimal>
actions <count decimal>
<Action.bits decimal>   # <decoded verb + args, ignored by the parser>
...
```

The deck and floor are **not** stored — for M1 they are the fixed skeleton
constants (`replay.hpp`'s `skeleton_deck()` at `kSkeletonFloor`). Stage B extends
this additively (a `deck …` line) behind a bumped version string.

## Differ semantics

- **Fast path:** if `hash_state(expected) == hash_state(actual)` the states are
  byte-identical (value-init/padding-deterministic, design doc §4.1), so
  `diff_states` returns an empty report without walking any field.
- **Semantic, not byte, diff:** piles/queues are compared over their live
  `[0, count)` range (ring buffers walked via their `head` cursor), never the
  stale scratch bytes past `count`; internal ring cursors (`action_tail` …) and
  padding are not compared. Two logically-equal states reached via different
  push/pop rotations therefore diff empty even if their raw hashes differ. The
  raw hash (`state_hash.hpp`) remains the byte-exact equality oracle.
- **Named fields:** every reported field has a debuggable name — `player.hp`,
  `monsters[0].powers[1].amount`, `hand[3]`, `ai_rng.counter` — and enum values
  render by name (`VULNERABLE(2)`). RNG streams are named individually so a
  divergence is attributable to the specific stream (InitialPlan's "RNG
  divergence anywhere is a stop-the-line bug").

## Fixture oracle

The fixture file format **is** the trace format: A6.2 produces fixtures with
`write_trace`, and `FixtureFileOracleAdapter::load_fixture` consumes them via
`read_trace`. `query(seed, prefix)` finds the fixture whose seed matches, checks
`prefix` is a prefix of the fixture's recorded actions, and returns the recorded
state after that prefix (`false` on unknown seed / divergent / too-long prefix).

## Build & test

Built as part of the normal pipeline (WSL Ubuntu-2404):

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

The `differ_test` gtest binary is the acceptance suite (synthetic divergence in
each field group is caught and named; trace/reproducer round-trips; schema-version
rejection).
