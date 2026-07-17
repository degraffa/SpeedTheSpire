#pragma once

// java.util.Random's 48-bit linear congruential generator, plus
// java.util.Collections.shuffle's exact Fisher-Yates algorithm, reimplemented
// bit-for-bit in C++.
//
// Provenance: every deck/pool shuffle in the game routes through
// Collections.shuffle(list, new java.util.Random(someLong)) --
// CardGroup.java:561-567 (see also AbstractDungeon.java:1101, 1237-1241).
// The JDK LCG algorithm itself is not in the decompiled tree (it's a JDK
// class, not a game class); it is specified precisely and treated as stable
// public API by the java.util.Random javadoc, and is transcribed here per
// docs/stage-a-design.md §3.3 (trap list §10 item 2: shuffles route through
// this LCG, not xorshift128+ / RandomXS128).
//
// This header is dependency-free (no gtest, no game headers).

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace sts::engine {

// Bit-exact reimplementation of java.util.Random's LCG (the parts the game
// actually exercises: seeding, next(bits), nextInt(), nextInt(bound)).
class JdkRandom {
public:
    JdkRandom() : JdkRandom(0) {}

    explicit JdkRandom(int64_t seed) { set_seed(seed); }

    // java.util.Random.setSeed(long): seed = (seed ^ 0x5DEECE66DL) & mask48.
    void set_seed(int64_t seed) {
        seed_ = (static_cast<uint64_t>(seed) ^ kMultiplier) & kMask48;
    }

    // java.util.Random.next(int bits):
    //   seed = (seed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
    //   return (int)(seed >>> (48 - bits));
    // seed_ is unsigned, so the right shift below is already the required
    // *logical* (unsigned) shift of the 48-bit value -- do not use a signed
    // type here or the shift semantics would differ from Java's `>>>`.
    int32_t next(int bits) {
        seed_ = (seed_ * kMultiplier + kIncrement) & kMask48;
        const uint64_t shifted = seed_ >> (48 - bits);
        // Narrowing cast: for bits<=32 the value fits in 32 bits and this
        // truncation is exactly Java's `(int)` narrowing conversion of a
        // long (well-defined modulo-2^32 truncation as of C++20).
        return static_cast<int32_t>(static_cast<uint32_t>(shifted));
    }

    // java.util.Random.nextInt(): return next(32).
    int32_t next_int() { return next(32); }

    // java.util.Random.nextInt(int bound): exact classic algorithm --
    // power-of-two fast path plus the general rejection-loop path. This has
    // been stable JDK spec (not merely implementation) since Java 1.2;
    // implemented exactly per docs/stage-a-design.md §3.3, no "simplification"
    // to e.g. plain modulo.
    int32_t next_int(int32_t bound) {
        if ((bound & -bound) == bound) {
            // bound is a power of 2.
            return static_cast<int32_t>(
                (static_cast<int64_t>(bound) * static_cast<int64_t>(next(31))) >> 31);
        }
        int32_t bits;
        int32_t val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
    }

private:
    // 0x5DEECE66DL: both the LCG multiplier and the fixed XOR mask used to
    // scramble the seed on construction/reseeding.
    static constexpr uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr uint64_t kIncrement = 0xBULL;
    static constexpr uint64_t kMask48 = (1ULL << 48) - 1;

    uint64_t seed_ = 0;
};

// java.util.Collections.shuffle's exact Fisher-Yates, specialized to the
// game's usage (an index/id list backed by a random-access container):
// for i = size-1 down to 1: swap(list[i], list[rnd.nextInt(i + 1)]).
template <typename T>
void jdk_shuffle(std::span<T> data, JdkRandom& rng) {
    if (data.size() < 2) {
        return;
    }
    for (std::size_t i = data.size() - 1; i > 0; --i) {
        const auto j = static_cast<std::size_t>(rng.next_int(static_cast<int32_t>(i + 1)));
        std::swap(data[i], data[j]);
    }
}

}  // namespace sts::engine
