// The floating-point contract, verified rather than asserted.
//
// cmake/StsFloatingPoint.cmake pins -ffp-contract=off (and the MSVC/clang-cl
// equivalents) because this simulator's damage, block and RNG-derived floats
// must be bit-identical across compilers and platforms. That file explains WHY;
// this one is the control that proves the flags actually took effect on the
// toolchain in front of you, instead of trusting a compiler manual.
//
// The failure mode being guarded is silent and severe. `a + b * c` fused into a
// single FMA rounds once instead of twice, so it produces a DIFFERENT float.
// The project has two genuinely fusable sites:
//
//   src/engine/interp/interp_damage.cpp:52   dmg + amount * strength_mult
//   include/sts/engine/rng_stream.hpp:173    start + next_float() * (end - start)
//
// Measured with GCC 13.3 on the reference box: `-O2` emits mulss+addss, but
// `-O2 -march=native` emits vfmadd231ss. Baseline x86-64 has no FMA
// instruction, which is the only reason the committed fixtures and golden
// vectors were not captured under contraction -- accidental safety that a
// one-line `-march=native` would remove, changing damage numbers with no test
// anywhere going red. This test is that missing red light.
//
// TRAP THIS TEST AVOIDS IN ITSELF: a contraction probe whose inputs no longer
// distinguish fused from unfused arithmetic still passes, forever, while
// testing nothing -- the same class of lie as conventions.md §6's "ctest exits
// 0 on an empty test set". So every case here first asserts that its inputs DO
// distinguish the two forms (via an explicit std::fma, which is a real fused
// multiply-add by definition) and only then asserts which form the compiler
// chose. If the discriminating power is ever lost, the test fails loudly rather
// than becoming vacuous.

#include <cmath>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "sts/engine/rng_stream.hpp"

namespace {

// Bit-level equality: float == is not the assertion we want (it would accept a
// signed zero mismatch, and it reads as an approximate claim when this is a
// byte-identity claim).
[[nodiscard]] uint32_t bits(float f) noexcept {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// `volatile` is doing real work here, in two places, and neither is cargo cult:
//   - on the inputs, so the compiler cannot constant-fold the whole expression
//     at compile time (constant folding is required to be exact, which would
//     mask contraction in the generated code);
//   - on the intermediate product, so the multiply's result is forced to a
//     32-bit float store before the add and CANNOT be fused, giving us a
//     reference "unfused" value computed by the same compiler.
struct Probe {
    float fused;    // rounded once  -- what an FMA produces
    float unfused;  // rounded twice -- what the IEEE-754 expression means
    float actual;   // whatever the compiler emitted for `a + b * c`
};

Probe probe(float a_in, float b_in, float c_in) {
    volatile float va = a_in, vb = b_in, vc = c_in;
    const float a = va, b = vb, c = vc;

    volatile float product = b * c;  // forced to float precision
    const float unfused = a + product;

    const float fused = std::fmaf(b, c, a);

    // The expression under test, in the shape the engine actually writes it.
    const float actual = a + b * c;

    return Probe{fused, unfused, actual};
}

// Inputs chosen so that b*c has a nonzero low tail that survives the add.
// b = c = 1 + 3*2^-23: the exact product is 1 + 6*2^-23 + 9*2^-46, whose tail
// is discarded by the float multiply but retained by an FMA, and adding -1
// promotes that tail to ~2 ulps of the result.
//
// The 3 is load-bearing and was found the hard way: the obvious 1-ulp
// perturbation (b = c = 1 + 2^-23, or 128 + 2^-16) does NOT work. Its tail
// lands exactly on a half-ulp boundary of the difference, so round-half-to-even
// takes the fused result back to the unfused one and the probe silently stops
// discriminating. That is precisely the vacuous-pass failure the
// InputsActuallyDistinguish* guards exist to catch -- and they did catch it, on
// the first run of this file. Verify any replacement constant the same way
// before trusting it.
constexpr float kOnePlus3Ulp = 1.0f + 3.0f * 0x1p-23f;

TEST(FpContract, InputsActuallyDistinguishFusedFromUnfused) {
    // Guards the test itself: if this ever fails, every assertion below has
    // become vacuous and the probe inputs need replacing -- it does NOT mean
    // the contract is fine.
    const Probe p = probe(-1.0f, kOnePlus3Ulp, kOnePlus3Ulp);
    EXPECT_NE(bits(p.fused), bits(p.unfused))
        << "fp_contract_test can no longer tell a fused multiply-add from an "
           "unfused one; its inputs are stale and it is testing nothing.";
}

TEST(FpContract, MultiplyAddIsNotContracted) {
    const Probe p = probe(-1.0f, kOnePlus3Ulp, kOnePlus3Ulp);
    EXPECT_EQ(bits(p.actual), bits(p.unfused))
        << "`a + b * c` was contracted into an FMA. The damage pipeline and "
           "rng_stream random(start,end) both have this shape, so damage "
           "numbers and RNG-derived floats will diverge from the committed "
           "fixtures and from every other toolchain. Check that "
           "cmake/StsFloatingPoint.cmake ran (configure prints 'FP contract: "
           "...') and that no -march/-ffast-math/-Ofast was added downstream "
           "of it.";
}

// A second, independent witness three decades up the exponent range, so the
// result does not rest on one hand-derived constant. Same 3-ulp shape for the
// same reason: 128 + 3*2^-16 squared against -16384 leaves a 2.25-ulp tail,
// where the 1-ulp form leaves exactly 0.5 ulp and rounds back to even.
TEST(FpContract, MultiplyAddIsNotContractedSecondWitness) {
    constexpr float kOneTwentyEightPlus3Ulp = 128.0f + 3.0f * 0x1p-16f;
    const Probe p =
        probe(-16384.0f, kOneTwentyEightPlus3Ulp, kOneTwentyEightPlus3Ulp);
    ASSERT_NE(bits(p.fused), bits(p.unfused)) << "second witness is stale";
    EXPECT_EQ(bits(p.actual), bits(p.unfused));
}

// The real call site, not a synthetic one: rng_stream.hpp:173 is
// `start + next_float() * (end - start)`. Contraction there would move every
// float drawn from a seeded stream. This drives it through the actual engine
// API so a refactor of that function stays covered.
TEST(FpContract, RngStreamRandomRangeIsNotContracted) {
    bool distinguished = false;

    for (uint64_t seed = 1; seed <= 64; ++seed) {
        sts::engine::RngStream s{};
        s.s0 = seed * 0x9E3779B97F4A7C15ULL;
        s.s1 = ~seed;
        s.counter = 0;

        // Re-derive the same draw twice from an identical stream state: once
        // through the engine, once by hand with the product forced to float.
        sts::engine::RngStream a = s;
        const float via_engine =
            sts::engine::random(a, 3.0f, 3.0f + kOnePlus3Ulp);

        sts::engine::RngStream b = s;
        const float raw = sts::engine::random(b);  // bare next_float()
        volatile float product = raw * ((3.0f + kOnePlus3Ulp) - 3.0f);
        const float by_hand = 3.0f + product;

        const float fused =
            std::fmaf(raw, (3.0f + kOnePlus3Ulp) - 3.0f, 3.0f);
        if (bits(fused) != bits(by_hand)) distinguished = true;

        EXPECT_EQ(bits(via_engine), bits(by_hand))
            << "seed " << seed
            << ": rng_stream random(start,end) does not match the unfused "
               "float expression it is specified to compute";

        // Same stream, same number of draws consumed, either way.
        EXPECT_EQ(a.counter, b.counter);
    }

    EXPECT_TRUE(distinguished)
        << "none of the 64 probe seeds produced a draw where fusing would "
           "change the result; this test is no longer discriminating.";
}

}  // namespace
