# Fail loudly at compile time, not months later in a training run (InitialPlan.md Part 3).
function(sts_set_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Woverloaded-virtual>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endfunction()
