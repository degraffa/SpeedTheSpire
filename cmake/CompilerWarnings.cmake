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
# -Wconversion (the lossy-narrowing half) is deliberately left as a warning even
# though a fresh three-preset build emits none of it today. Promoting a flag is
# a commitment to keep it at zero, and -Wconversion fires on ordinary integer
# narrowing all over a codebase built from uint8_t/int16_t state fields -- so
# that commitment deserves its own decision rather than riding along with this
# one. If it is still at zero when someone next measures, promoting it is a
# one-word change here.
function(sts_set_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Woverloaded-virtual -Werror=sign-conversion>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endfunction()
