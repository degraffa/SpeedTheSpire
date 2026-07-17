#pragma once

// The game's per-stream RNG wrapper: a RandomXS128 (xorshift128+) paired with
// an int32 draw counter. Every stream the game holds (monsterRng, aiRng,
// shuffleRng, mapRng, ...) is one of these; every gameplay roll goes through a
// wrapper method on it. This header re-expresses each of those methods as a
// free function over a 24-byte POD RngStream, building directly on
// rng_xs128.hpp.
//
// Provenance: com.megacrit.cardcrawl.random.Random (Random.java, whole file),
// read from D:\STS_BG_Mod\SlayTheSpireDecompiled. Each wrapper below cites the
// Java method it re-expresses.
//
// The one-draw invariant (design doc §3.2): EVERY wrapper method increments
// `counter` by exactly 1 and consumes exactly one `next_long()` from the
// underlying xorshift128+ engine (the sole physical exception is the ~1e-16
// rejection retry inside next_long(n), which is internal to the primitive and
// does not change the "one wrapper call == one logical draw" accounting). This
// is why the game's counter-based save/restore works and why our copy-based
// restore (below) is exact.
//
// Trap list entries this file exists to satisfy (docs/stage-a-design.md §10):
//   3.  random(int) bounds are INCLUSIVE: random(range) == nextInt(range + 1).
//   7.  Floor-scoped streams reseed as Random(seed + floorNum) using the floor
//       number AFTER its increment (AbstractDungeon.java:1747-1751); see
//       floor_stream() below.

#include <bit>
#include <cstdint>
#include <type_traits>

#include "sts/engine/rng_xs128.hpp"

namespace sts::engine {

// In-state representation of one game RNG stream (design doc §3.6).
//
// `s0`/`s1` are the raw xorshift128+ state (RandomXS128's seed0/seed1);
// `counter` mirrors the game's Random.counter (number of wrapper draws taken),
// kept only for save-file compatibility and oracle diffing. `pad` makes the
// struct a clean 24 bytes with deterministic padding so byte-hashing a
// value-initialized state is stable (design doc §4.1).
//
// Restore/fork of a stream is a plain struct copy -- `RngStream a = b;` -- NOT
// a replay. The from_seed_counter() helper below exists only to reproduce the
// game's Random(seed, N) constructor for oracle diffing, not as a restore path.
struct RngStream {
    uint64_t s0;
    uint64_t s1;
    int32_t counter;
    int32_t pad;
};

static_assert(std::is_trivially_copyable_v<RngStream>,
              "RngStream must be trivially copyable (design doc §3.6/§4.1: "
              "snapshot = memcpy, restore = copy)");
static_assert(sizeof(RngStream) == 24,
              "RngStream must be exactly 24 bytes (design doc §3.6)");

namespace detail {

// Rehydrate a RandomXS128 from a stream's raw state (no scramble).
[[nodiscard]] constexpr RandomXS128 as_xs128(const RngStream& s) noexcept {
    return RandomXS128(s.s0, s.s1);
}

// Write an advanced RandomXS128's state back into the stream and bump the
// wrapper counter by one -- the shared tail of every wrapper method, enforcing
// the one-draw invariant in one place.
constexpr void commit(RngStream& s, const RandomXS128& r) noexcept {
    s.s0 = r.state0();
    s.s1 = r.state1();
    ++s.counter;
}

}  // namespace detail

// --- Construction ---------------------------------------------------------

// new Random(Long seed) (Random.java:24-26): fresh RandomXS128 scramble of the
// seed, counter = 0.
[[nodiscard]] constexpr RngStream from_seed(int64_t seed) noexcept {
    const RandomXS128 r(seed);
    return RngStream{r.state0(), r.state1(), 0, 0};
}

// --- Wrapper methods (one logical draw each) ------------------------------
// Every function below is a thin wrapper: rehydrate -> one primitive draw ->
// commit (store state + counter++). Overload names mirror Random.java exactly.

// random(int range) (Random.java:53-56). TRAP 3: nextInt(range + 1) -- the
// bound is INCLUSIVE, so random(999) yields 0..999.
[[nodiscard]] constexpr int32_t random(RngStream& s, int32_t range) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const int32_t v = r.next_int(range + 1);
    detail::commit(s, r);
    return v;
}

// random(int start, int end) (Random.java:58-61): inclusive on both ends.
[[nodiscard]] constexpr int32_t random(RngStream& s, int32_t start, int32_t end) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const int32_t v = start + r.next_int(end - start + 1);
    detail::commit(s, r);
    return v;
}

// random(long range) (Random.java:63-66): (long)(nextDouble() * range).
[[nodiscard]] constexpr int64_t random(RngStream& s, int64_t range) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const int64_t v =
        static_cast<int64_t>(r.next_double() * static_cast<double>(range));
    detail::commit(s, r);
    return v;
}

// random(long start, long end) (Random.java:68-71):
// start + (long)(nextDouble() * (end - start)).
[[nodiscard]] constexpr int64_t random(RngStream& s, int64_t start, int64_t end) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const int64_t v =
        start +
        static_cast<int64_t>(r.next_double() * static_cast<double>(end - start));
    detail::commit(s, r);
    return v;
}

// randomLong() (Random.java:73-76): raw nextLong().
[[nodiscard]] constexpr int64_t random_long(RngStream& s) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const int64_t v = r.next_long();
    detail::commit(s, r);
    return v;
}

// randomBoolean() (Random.java:78-81).
[[nodiscard]] constexpr bool random_boolean(RngStream& s) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const bool v = r.next_boolean();
    detail::commit(s, r);
    return v;
}

// randomBoolean(float chance) (Random.java:83-86): nextFloat() < chance.
[[nodiscard]] constexpr bool random_boolean(RngStream& s, float chance) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const bool v = r.next_float() < chance;
    detail::commit(s, r);
    return v;
}

// random() (Random.java:88-91): bare nextFloat(), in [0, 1).
[[nodiscard]] constexpr float random(RngStream& s) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const float v = r.next_float();
    detail::commit(s, r);
    return v;
}

// random(float range) (Random.java:93-96): nextFloat() * range. The multiply
// is float-precision, matching the Java source exactly.
[[nodiscard]] constexpr float random(RngStream& s, float range) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const float v = r.next_float() * range;
    detail::commit(s, r);
    return v;
}

// random(float start, float end) (Random.java:98-101):
// start + nextFloat() * (end - start), all float-precision.
[[nodiscard]] constexpr float random(RngStream& s, float start, float end) noexcept {
    RandomXS128 r = detail::as_xs128(s);
    const float v = start + r.next_float() * (end - start);
    detail::commit(s, r);
    return v;
}

// --- Counter restore (oracle-diffing only, NOT the primary restore path) --

// new Random(Long seed, int counter) (Random.java:28-33): fresh seed, then
// `counter` replays of random(999). This reproduces the GAME's constructor
// behavior for byte-diffing against counter_restore_<seed>.bin -- it is NOT
// how the sim restores a stream. The primary restore/fork is a plain struct
// copy (`RngStream a = b;`), which lands on the identical engine state because
// of the one-draw invariant, without any replay.
[[nodiscard]] constexpr RngStream from_seed_counter(int64_t seed, int32_t n) noexcept {
    RngStream s = from_seed(seed);
    for (int32_t i = 0; i < n; ++i) {
        (void)random(s, 999);
    }
    return s;
}

// setCounter(int) (Random.java:42-51): replays randomBoolean() to advance the
// counter. Different draw values from random(999), but the SAME number of
// next_long() consumptions, so the resulting engine state is identical under
// the one-draw invariant. Provided as a consistency-check counterpart to
// from_seed_counter(); like it, this is oracle-diffing scaffolding, not a
// restore path. Precondition (matching the Java guard): n >= 0.
[[nodiscard]] constexpr RngStream from_seed_set_counter(int64_t seed, int32_t n) noexcept {
    RngStream s = from_seed(seed);
    for (int32_t i = 0; i < n; ++i) {
        (void)random_boolean(s);
    }
    return s;
}

// --- Floor / act stream derivation ----------------------------------------

// Floor-scoped stream seed (design doc §3.4; AbstractDungeon.java:1747-1751).
// TRAP 7: the floor number is the value AFTER its increment. All five
// floor-scoped streams (monsterHpRng, aiRng, shuffleRng, cardRandomRng,
// miscRng) share this identical seed formula `run_seed + floor`, so this one
// helper constructs any of them -- callers just call it once per stream name
// at floor-transition time. Counter starts at 0.
[[nodiscard]] constexpr RngStream floor_stream(int64_t run_seed,
                                               int32_t floor_after_increment) noexcept {
    return from_seed(run_seed + static_cast<int64_t>(floor_after_increment));
}

// Act-scoped mapRng seed (design doc §3.4; Exordium/TheCity/TheBeyond/
// TheEnding constructors, per tools/golden_capture/README.md's correction).
// `act_num` is 1/2/3/4; the per-act multiplier is 1/100/200/300 respectively,
// and the seed offset is `act_num * mult[act_num]`, giving the four offsets
// +1, +200, +600, +1200. Counter starts at 0.
[[nodiscard]] constexpr RngStream map_stream(int64_t run_seed, int32_t act_num) noexcept {
    // Indexed 1..4; index 0 is unused padding so act_num maps directly.
    constexpr int64_t kActMult[5] = {0, 1, 100, 200, 300};
    const int64_t offset =
        static_cast<int64_t>(act_num) * kActMult[act_num];
    return from_seed(run_seed + offset);
}

}  // namespace sts::engine
