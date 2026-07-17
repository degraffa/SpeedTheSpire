#pragma once

// Bit-exact, header-only reimplementation of libGDX's RandomXS128
// (xorshift128+), the bottom-most RNG primitive the game builds everything
// else on top of (com.megacrit.cardcrawl.random.Random wraps this; the JDK
// LCG used for Collections.shuffle is a separate, unrelated generator seeded
// *from* a draw of this one — see rng_jdk.hpp for that).
//
// Provenance: com.badlogic.gdx.math.RandomXS128 (RandomXS128.java, whole
// file), read from D:\STS_BG_Mod\SlayTheSpireDecompiled. Every method below
// cites the Java method it re-expresses; on any conflict between this file
// and docs/stage-a-design.md §3.1, the Java (re-read) wins per the Stage A
// ledger's precedence rule.
//
// Trap list entries this file exists to satisfy (docs/stage-a-design.md §10):
//   4.  nextLong(n)'s rejection-sampling loop must be kept, not simplified
//       to a single modulo draw, even though the reject branch fires with
//       probability ~1e-16 for typical bounds.
//   5.  Seed 0 scrambles as INT64_MIN, not as 0.
//   11. nextFloat() narrows a DOUBLE product to float; the multiply itself
//       must happen in double precision, then get truncated/rounded to
//       float — doing the multiply directly in float space rounds
//       differently for some inputs.

#include <cassert>
#include <cstdint>
#include <utility>

namespace sts::engine {

// Bit-exact port of com.badlogic.gdx.math.RandomXS128.
//
// State is the raw (s0, s1) pair from the Java class (there named seed0/
// seed1); no other members. Trivially copyable by construction (two
// uint64_t's, no pointers), which matters later when this gets embedded in
// RngStream (A1.3) and the fixed-capacity state structs (A2.x).
class RandomXS128 {
public:
    // Deterministic default: equivalent to RandomXS128(0). (The Java class's
    // no-arg constructor seeds from `new java.util.Random().nextLong()`,
    // i.e. wall-clock entropy — not reproducible and not needed for a
    // headless sim where every stream is always explicitly seeded.)
    constexpr RandomXS128() noexcept { set_seed(0); }

    // RandomXS128(long seed) (RandomXS128.java:19-21).
    constexpr explicit RandomXS128(int64_t seed) noexcept { set_seed(seed); }

    // RandomXS128(long seed0, long seed1) (RandomXS128.java:23-25) — direct
    // state construction, bypassing the murmur3 scramble.
    constexpr RandomXS128(uint64_t s0, uint64_t s1) noexcept : s0_(s0), s1_(s1) {}

    // setSeed(long) (RandomXS128.java:94-97). Trap 5: seed 0 maps to
    // Long.MIN_VALUE *before* scrambling, not to 0.
    constexpr void set_seed(int64_t seed) noexcept {
        const uint64_t seed_bits =
            (seed == 0) ? kInt64MinBits : static_cast<uint64_t>(seed);
        const uint64_t new_s0 = murmur3(seed_bits);
        s0_ = new_s0;
        s1_ = murmur3(new_s0);
    }

    // setState(long, long) (RandomXS128.java:99-102) — raw state write, no
    // scramble.
    constexpr void set_state(uint64_t s0, uint64_t s1) noexcept {
        s0_ = s0;
        s1_ = s1;
    }

    // getState(int) (RandomXS128.java:104-106) returned one word selected by
    // index; we just expose the pair directly (24-byte RngStream in A1.3
    // wraps this the same way the game's counter wrapper does).
    [[nodiscard]] constexpr std::pair<uint64_t, uint64_t> get_state() const noexcept {
        return {s0_, s1_};
    }
    [[nodiscard]] constexpr uint64_t state0() const noexcept { return s0_; }
    [[nodiscard]] constexpr uint64_t state1() const noexcept { return s1_; }

    // nextLong() (RandomXS128.java:27-35). The core xorshift128+ step; every
    // other draw in this class and in the game's Random wrapper (A1.3)
    // bottoms out in exactly one call to this.
    constexpr int64_t next_long() noexcept {
        uint64_t s1 = s0_;
        const uint64_t s0 = s1_;
        s0_ = s0;
        s1 ^= s1 << 23;
        s1_ = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);
        return static_cast<int64_t>(s1_ + s0);
    }

    // nextInt() (RandomXS128.java:42-45): narrowing (int) cast of nextLong().
    constexpr int32_t next_int() noexcept {
        return static_cast<int32_t>(next_long());
    }

    // nextInt(int) (RandomXS128.java:47-50): (int)nextLong(n).
    constexpr int32_t next_int(int32_t n) noexcept {
        return static_cast<int32_t>(next_long(static_cast<int64_t>(n)));
    }

    // nextLong(long) (RandomXS128.java:52-62). Trap 4: this rejection-
    // sampling loop is load-bearing and must NOT be simplified to a plain
    // `nextLong() % n` — see RngXs128Trap.RejectionLoopNotSimplifiedToModulo
    // in tests/rng_xs128_test.cpp, which forces the reject branch and proves
    // the returned value differs from the single-draw modulo shortcut.
    //
    // The Java computes `bits - value + (n - 1) < 0` in signed 64-bit
    // arithmetic and relies on silent two's-complement overflow to detect
    // the out-of-range case. Signed overflow is undefined behavior in C++,
    // so the same wraparound is reproduced explicitly via uint64_t
    // arithmetic before the final sign-bit test — this is bit-for-bit
    // equivalent to Java's semantics, not an approximation of them.
    constexpr int64_t next_long(int64_t n) noexcept {
        assert(n > 0 && "RandomXS128::next_long(n): n must be positive");
        const uint64_t un = static_cast<uint64_t>(n);
        uint64_t bits;
        int64_t value;
        for (;;) {
            bits = static_cast<uint64_t>(next_long()) >> 1;
            value = static_cast<int64_t>(bits % un);
            const uint64_t check =
                bits - static_cast<uint64_t>(value) + (un - 1);
            if (static_cast<int64_t>(check) >= 0) {
                break;
            }
        }
        return value;
    }

    // nextDouble() (RandomXS128.java:64-67).
    constexpr double next_double() noexcept {
        const uint64_t bits = static_cast<uint64_t>(next_long()) >> 11;
        return static_cast<double>(bits) * kNormDouble;
    }

    // nextFloat() (RandomXS128.java:69-72). Trap 11: the Java source is
    // `(float)((double)(nextLong() >>> 40) * NORM_FLOAT)` — the multiply
    // happens in double precision (NORM_FLOAT is a double constant) and only
    // the final result is narrowed to float. Multiplying in float precision
    // directly rounds differently for some inputs, so the intermediate must
    // stay double.
    constexpr float next_float() noexcept {
        const uint64_t bits = static_cast<uint64_t>(next_long()) >> 40;
        const double product = static_cast<double>(bits) * kNormFloat;
        return static_cast<float>(product);
    }

    // nextBoolean() (RandomXS128.java:74-77).
    constexpr bool next_boolean() noexcept {
        return (next_long() & 1L) != 0;
    }

private:
    // Long.MIN_VALUE's bit pattern (0x8000000000000000), used verbatim as
    // murmur3's input per trap 5 — written as the unsigned hex rather than
    // a negated literal to sidestep INT64_MIN's well-known "can't negate the
    // positive literal" footgun.
    static constexpr uint64_t kInt64MinBits = 0x8000000000000000ULL;

    // murmurHash3(long) (RandomXS128.java:108-115). The two multiplier
    // constants are the decompiler's signed decimal literals
    // -49064778989728563L / -4265267296055464877L reinterpreted as the
    // unsigned hex below (same bit pattern, unsigned arithmetic makes the
    // multiply/xor chain well-defined without any UB in C++).
    static constexpr uint64_t murmur3(uint64_t x) noexcept {
        x ^= x >> 33;
        x *= 0xFF51AFD7ED558CCDULL;
        x ^= x >> 33;
        x *= 0xC4CEB9FE1A85EC53ULL;
        x ^= x >> 33;
        return x;
    }

    // NORM_DOUBLE = (double)1.110223E-16f (RandomXS128.java:10). The Java
    // field is declared as a FLOAT literal ("1.110223E-16f") that then gets
    // implicitly promoted to double — it is NOT the exact double value of
    // 2^-53 (1.1102230246251565e-16), it's whatever binary32 value the
    // decimal literal rounds to, widened. Both Java and C++ round decimal
    // float literals to the nearest representable binary32 value per
    // IEEE-754, so parsing the identical literal text here reproduces the
    // identical bit pattern.
    static constexpr float kNormDoubleF = 1.110223e-16f;
    static constexpr double kNormDouble = static_cast<double>(kNormDoubleF);

    // NORM_FLOAT = 5.960464477539063E-8 (RandomXS128.java:11) — this one IS
    // declared as a double literal in the Java source (exactly 2^-24), so no
    // float-rounding subtlety applies here.
    static constexpr double kNormFloat = 5.960464477539063e-8;

    uint64_t s0_ = 0;
    uint64_t s1_ = 0;
};

}  // namespace sts::engine
