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
function(sts_set_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Woverloaded-virtual -Werror=sign-conversion -Werror=conversion>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endfunction()
