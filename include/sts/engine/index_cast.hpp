#pragma once

// as_index: the single place a signed index expression becomes a container
// subscript.
//
// WHY THIS EXISTS
//
// Standard containers subscript by an unsigned size_type, so `v[i]` with an
// `int i` converts signed -> unsigned. -Wsign-conversion reports that, and
// cmake/CompilerWarnings.cmake promotes it to an error, because the conversion
// is exactly the shape of a defect this project cannot afford: if the value is
// negative it does not trap, it wraps -- static_cast<std::size_t>(-1) is
// 18446744073709551615 -- and the subscript lands arbitrarily far out of
// bounds.
//
// The obvious way to silence the diagnostic is to write the static_cast at each
// site, which suppresses the warning AND keeps the bug: a cast is precisely the
// thing that makes the wrap silent. as_index does the conversion once, behind a
// debug assert, so silencing the diagnostic also buys back the check the
// diagnostic was asking for. It compiles to nothing under NDEBUG.
//
// Use it for values that are *supposed* to be non-negative, and whose
// non-negativity you have checked. It is not a way to handle an index that can
// legitimately be negative -- an "index" with a sentinel -1 wants a branch on
// the sentinel before it ever reaches a subscript.
//
// WHERE IT LIVES (conventions.md §6): include/sts/engine/ is the engine's
// published surface, for headers consumed from outside the library. Every
// consumer today is outside sts_engine -- four test binaries and the two
// benchmark executables, zero engine sources -- which is exactly the case §6
// says belongs here rather than beside the .cpp files in src/engine/.

#include <cassert>
#include <cstddef>
#include <type_traits>

namespace sts::engine {

template <typename T>
[[nodiscard]] constexpr std::size_t as_index(T v) noexcept {
    static_assert(std::is_integral_v<T> && std::is_signed_v<T>,
                  "as_index converts a SIGNED index to a subscript; an already-"
                  "unsigned index needs no conversion -- pass it through");
    // Debug/ASan builds stop here; release compiles the check away. A firing
    // assert means the caller's non-negativity reasoning was wrong, which is a
    // bug at the caller, not something to paper over here.
    assert(v >= 0 && "as_index: negative index would wrap to a huge size_t");
    return static_cast<std::size_t>(v);
}

}  // namespace sts::engine
