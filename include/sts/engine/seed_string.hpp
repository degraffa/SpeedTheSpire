#pragma once

// SeedHelper string<->long conversion (game seed strings, e.g. "2QIJMIKEYSYQ8").
//
// Provenance: com.megacrit.cardcrawl.helpers.SeedHelper (SeedHelper.java, whole
// file). See docs/stage-a-design.md §3.5 and §10 trap 6.
//
// Trap 6 (the entire point of this header): long->string treats the seed as
// UNSIGNED 64-bit (Java's `Long.toUnsignedString`); string->long accumulates
// into a SIGNED 64-bit with natural two's-complement overflow wrap (Java
// `long total` arithmetic, which always wraps silently). Encode is unsigned,
// decode is signed-wrapping -- that asymmetry is intentional in the game and
// must be replicated exactly, not "fixed".

#include <cstdint>
#include <string>
#include <string_view>

namespace sts::engine {

// Display alphabet: 35 characters, digits 0-9 then A-Z with 'O' omitted
// (SeedHelper.java:13 `CHARACTERS`). 'O' typed by a user is folded to '0'
// before lookup (see seed_valid_character / seed_from_string below).
inline constexpr std::string_view kSeedAlphabet = "0123456789ABCDEFGHIJKLMNPQRSTUVWXYZ";
inline constexpr std::size_t kSeedAlphabetSize = kSeedAlphabet.size();  // 35
static_assert(kSeedAlphabetSize == 35);

// Sentinel returned by seed_valid_character for an input with no mapping.
// The alphabet contains only digits/uppercase letters, so NUL never
// collides with a legitimate mapped character (Java returns `null` here --
// SeedHelper.java:50).
inline constexpr char kSeedInvalidChar = '\0';

namespace detail {

constexpr char AsciiUpper(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

// CHARACTERS.indexOf(c) -- returns -1 (not npos) to match Java semantics,
// including the arithmetic that follows in getLong when the char is absent.
constexpr int AlphabetIndexOf(char c) {
    for (std::size_t i = 0; i < kSeedAlphabetSize; ++i) {
        if (kSeedAlphabet[i] == c) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace detail

// SeedHelper.getString (SeedHelper.java:62-74): base-35 encode, seed read as
// UNSIGNED 64-bit (`Long.toUnsignedString`). The game does this via
// BigInteger; a plain uint64_t is sufficient here since the divisor (35)
// never risks overflow in the intermediate arithmetic. The loop is
// `while (leftover != 0)`, so seed 0 never enters it and getString(0) is the
// empty string -- NOT "0". This is a deliberate, verified game behavior
// (see tests/golden/seedhelper.txt row `RT 0`), not an oversight to "fix".
inline std::string seed_to_string(std::int64_t seed) {
    std::uint64_t leftover = static_cast<std::uint64_t>(seed);
    std::string digits_lsb_first;
    while (leftover != 0) {
        auto remainder = static_cast<std::size_t>(leftover % kSeedAlphabetSize);
        leftover /= kSeedAlphabetSize;
        digits_lsb_first.push_back(kSeedAlphabet[remainder]);
    }
    // Java builds the string via `bldr.insert(0, c)` per digit, i.e. each new
    // (more significant) digit is prepended -- equivalent to appending in
    // least-significant-first order and reversing once at the end.
    return std::string(digits_lsb_first.rbegin(), digits_lsb_first.rend());
}

// SeedHelper.getValidCharacter (SeedHelper.java:42-51): uppercase the input,
// fold 'O' -> '0', then accept only if the result is in CHARACTERS. Java
// returns `null` for anything else; we return kSeedInvalidChar. Note this is
// a strict char-level restatement of the Java (which is String-typed to
// support IME/clipboard input of arbitrary length, but is only ever invoked
// on the codebase's call sites with single-character input).
inline char seed_valid_character(char c) {
    char upper = detail::AsciiUpper(c);
    if (upper == 'O') {
        upper = '0';
    }
    return (detail::AlphabetIndexOf(upper) != -1) ? upper : kSeedInvalidChar;
}

// SeedHelper.getLong (SeedHelper.java:76-89): base-35 decode. Sterilization
// step is inline in getLong itself, not a call to sterilizeString:
// `seedStr = seedStr.toUpperCase().replaceAll("O", "0")` (line 78) -- both
// 'O' and lowercase 'o' (uppercased first) become '0'. Accumulation is into
// Java's signed `long total`, which wraps silently on overflow (two's
// complement, no UB in Java, unlike raw signed overflow in C++). We therefore
// accumulate in uint64_t (wraparound is well-defined) and reinterpret to
// int64_t only at the return -- since C++20 that conversion is guaranteed to
// select the congruent-mod-2^64 value, i.e. the identical bit pattern Java's
// JIT would have produced. This is trap 6's decode half: SIGNED-wrapping,
// deliberately asymmetric with the UNSIGNED encode above.
//
// A character absent from the alphabet yields index -1 in Java (with a
// diagnostic println, no exception) and that -1 still participates in the
// running total; we replicate that arithmetic rather than rejecting the
// input, since the game itself does not.
inline std::int64_t seed_from_string(std::string_view seed_str) {
    std::uint64_t total = 0;
    for (char raw : seed_str) {
        char upper = detail::AsciiUpper(raw);
        if (upper == 'O') {
            upper = '0';
        }
        int remainder = detail::AlphabetIndexOf(upper);
        // Sign-extend (not zero-extend): Java's `total += (long) remainder`
        // widens the -1 int to a -1 long before the (wrapping) add.
        auto remainder_u64 = static_cast<std::uint64_t>(static_cast<std::int64_t>(remainder));
        total = total * static_cast<std::uint64_t>(kSeedAlphabetSize) + remainder_u64;
    }
    return static_cast<std::int64_t>(total);
}

}  // namespace sts::engine
