# Opt-in ASan/UBSan for hunting the "silent state corruption" failure mode
# called out in InitialPlan.md A.1 before it reaches a training run.
function(sts_enable_sanitizers target)
    if(STS_ENABLE_SANITIZERS)
        target_compile_options(${target} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endif()
endfunction()
