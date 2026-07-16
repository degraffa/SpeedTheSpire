# StockfishTheSpire

Headless, high-performance Slay the Spire simulator + training program. See
[InitialPlan.md](InitialPlan.md) for the full design and rationale; this file
only covers building and running what exists so far.

## Building

Developed against a Linux toolchain (WSL) to match the eventual sim-node
target. Requires CMake ≥ 3.21 and a C++20 compiler (GCC ≥ 12 or Clang ≥ 15),
plus Ninja. Dependencies (GoogleTest, Google Benchmark) are fetched
automatically by CMake at configure time — no package manager required.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release` (optimized, LTO) and `asan` (ASan/UBSan enabled).

## Layout

- `include/sts/`, `src/engine/` — the simulator engine (library `sts_engine`).
- `tests/` — GoogleTest unit tests; run via `ctest`.
- `benchmarks/` — Google Benchmark microbenchmarks.
- `registry/` — rules-as-data YAML/CSV registry (card/relic/power definitions).
- `tools/oracle_bridge/` — CommunicationMod-driven differential-testing harness.
