# Fail loudly at compile time, not months later in a training run (InitialPlan.md Part 3).
#
# WHY -Werror=sign-conversion, AND WHY PROJECT-WIDE
#
# A signed->unsigned conversion that changes a value is, in a bit-exact
# simulator, the exact class of defect the whole project exists to avoid: it
# does not crash, it produces a different number. Left as a warning it is
# invisible, because a normal build prints hundreds of lines and nobody diffs
# them -- and an incremental build prints none at all, since the file that would
# warn is not recompiled.
#
# The promotion covers tests and benchmarks as well as the engine. A test is
# where a wrong index silently turns a real divergence into a passing assertion,
# so exempting tests would exempt the place the mistake is most expensive. The
# whole tree was clean at promotion time (every site was an index counter over a
# container of matching size, so no conversion could change a value), which is
# what made a project-wide promotion cheap rather than a migration.
#
# HOW, and the trap it avoids: this must stay inside sts_set_warnings, which is
# applied per target and lands PRIVATE. Do NOT hoist it to -DCMAKE_CXX_FLAGS or
# add_compile_options: those reach the FetchContent dependencies too, and
# google-benchmark compiles with its own -Werror, so third-party sources become
# hard errors and the build dies before link. CI configures every preset with
# -DSTS_BUILD_BENCHMARKS=ON, so that failure mode reddens every job at once.
#
# -Wconversion (the lossy-narrowing half) is promoted on the same terms, as a
# separate decision rather than a ride-along: a fresh three-preset build emitted
# none of it, and the owner accepted the standing commitment that promotion
# implies. It matters more here than in most codebases -- state lives in
# uint8_t/int16_t fields, so an ordinary-looking int assignment is exactly how a
# value silently changes, which is the defect class this simulator exists to
# avoid. Expect it to bite when new state fields land; the fix is an explicit
# cast at the narrowing site (or as_index() in index_cast.hpp for the
# signed->subscript case), never a suppression.
#
# WHY clang-cl GETS ITS OWN BRANCH -- AND WHY -Wall IS ABSENT FROM IT
#
# clang-cl reports CXX_COMPILER_ID "Clang", so the GNU list used to apply to it
# verbatim. That looks right and is badly wrong: in the clang-cl driver the
# dash-form `-Wall` does NOT mean clang's -Wall, it means -Weverything.
# Measured with clang-cl 22.1.8 -- this project's own warning list and a bare
# -Weverything produce an identical set of categories on the same file, and the
# first Windows build emitted 965 -Wunsafe-buffer-usage, 759 -Wpadded, 152
# -Wold-style-cast and 115 -Wdisabled-macro-expansion. A warning surface nobody
# can read is a warning surface nobody reads, which is the same argument this
# file makes above for promoting the conversion pair to errors.
#
# The MSVC-spelled /W4 is what clang-cl maps to clang's -Wall -Wextra, so the
# Windows surface below is the intended one rather than an approximation. The
# remaining warnings keep their GNU spellings, which the clang-cl driver accepts
# normally -- it is specifically -Wall that is overloaded.
#
# WHAT IS PRESERVED ON WINDOWS
#
# All of it. -Wconversion and -Wsign-conversion -- the two deliberately promoted
# to errors here, and the two cl.exe has no equivalent for at any /W level --
# are fully supported by clang-cl and verified to fire AS ERRORS:
#
#   error: implicit conversion changes signedness: 'int' to 'unsigned int'
#          [-Werror,-Wsign-conversion]
#   error: implicit conversion loses integer precision: 'int' to 'short'
#          [-Werror,-Wimplicit-int-conversion]
#
# That is the whole reason clang-cl was chosen over cl.exe. The cl.exe branch is
# kept only so a stray MSVC configure still gets /W4; it is NOT an endorsed
# configuration, because adopting it would silently discard the conversion gate.
function(sts_set_warnings target)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
       AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang-cl: /W4 == clang's -Wall -Wextra. Do NOT add -Wall here.
        target_compile_options(${target} PRIVATE
            /W4 -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Woverloaded-virtual
            -Werror=sign-conversion -Werror=conversion)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Woverloaded-virtual -Werror=sign-conversion -Werror=conversion>)
    endif()
endfunction()
