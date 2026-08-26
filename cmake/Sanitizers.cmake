# Opt-in ASan/UBSan for hunting the "silent state corruption" failure mode
# called out in InitialPlan.md A.1 before it reaches a training run.
#
# BOTH SANITIZERS WORK NATIVELY ON WINDOWS UNDER clang-cl. That is measured, not
# assumed, and it corrects an earlier assessment in this port which said UBSan
# was unavailable on Windows and that WSL would therefore stay mandatory for the
# asan preset. With clang-cl 22.1.8:
#
#   ASan  -> heap-buffer-overflow caught, with a full symbolised report
#   UBSan -> "runtime error: signed integer overflow: 2147483647 + 1 cannot be
#            represented in type 'int'"
#
# cl.exe is a different story, and part of why clang-cl was chosen: it accepts
# /fsanitize=address but silently IGNORES /fsanitize=undefined (warning D9002,
# exit code 0), so an MSVC build would look instrumented while having no UBSan
# at all.
#
# Two Windows-specific mechanics, both hard failures if missed:
#
#  - /RTC1 is in CMake's default MSVC-style Debug flags and is incompatible with
#    ASan. It is stripped rather than worked around.
#  - The ASan runtime is a DLL (clang_rt.asan_dynamic-x86_64.dll) that ships with
#    LLVM and is NOT on PATH. Without it every instrumented binary dies at
#    startup with 0xC0000135 (STATUS_DLL_NOT_FOUND), which ctest reports as a
#    discovery failure rather than anything mentioning a DLL.
function(sts_enable_sanitizers target)
    if(NOT STS_ENABLE_SANITIZERS)
        return()
    endif()
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # clang-cl spellings: /Zi for debug info, /Oy- to keep frame pointers.
        # The GNU -g / -fno-omit-frame-pointer are not accepted by this driver.
        # -fno-sanitize-recover=undefined: UBSan's DEFAULT is print-and-
        # continue, and ctest never fails on a recoverable diagnostic -- the
        # event_flags_hi id-32 shift underflow (event_framework.cpp) printed
        # "shift exponent 4294967295 is too large" on every asan-preset run
        # while its test reported OK. Non-recoverable UBSan aborts the test
        # instead, which is the only mode under which "asan preset green"
        # actually asserts UB-freedom.
        target_compile_options(${target} PUBLIC
            -fsanitize=address,undefined -fno-sanitize-recover=undefined
            /Zi /Oy-)

        # LINKING IS NOT SYMMETRIC WITH THE POSIX CASE, and -fsanitize= on the
        # link line does NOT work here. With an MSVC-style driver CMake emits
        # target_link_options AFTER `/link`, i.e. they go to lld-link rather than
        # to the clang-cl driver -- so the driver never learns a sanitizer is in
        # use and never adds its runtime. The build then dies with:
        #
        #   lld-link: error: undefined symbol: __asan_shadow_memory_dynamic_address
        #   lld-link: error: undefined symbol: __ubsan_handle_type_mismatch_v1
        #
        # Confirmed by reproducing both shapes by hand: the flag after /link
        # fails exactly like the above, the explicit runtime libraries below
        # link clean. Note the UBSan handlers resolve out of the ASan runtime --
        # clang's combined asan_dynamic carries them, so there is no separate
        # ubsan_standalone to add when both sanitizers are on together.
        #
        # x86_64 is hard-coded because this project is x64-only; a future arch
        # would need the matching runtime name.
        target_link_options(${target} PUBLIC
            "/libpath:${STS_SANITIZER_LIB_DIR}"
            clang_rt.asan_dynamic-x86_64.lib
            clang_rt.asan_dynamic_runtime_thunk-x86_64.lib
            /wholearchive:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
    else()
        # -fno-sanitize-recover=undefined: same rationale as the clang-cl
        # branch above -- recoverable UBSan diagnostics scroll past ctest.
        target_compile_options(${target} PUBLIC
            -fsanitize=address,undefined -fno-sanitize-recover=undefined
            -fno-omit-frame-pointer -g)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    endif()
endfunction()

# Called once from the top level rather than per target: both actions are
# build-wide, and the file copy would otherwise repeat ~45 times.
function(sts_prepare_sanitizer_runtime)
    if(NOT STS_ENABLE_SANITIZERS OR NOT WIN32)
        return()
    endif()

    # clang-cl's ASan runtime is built against the RELEASE CRT and the driver
    # refuses the debug one outright:
    #
    #   clang-cl: error: invalid argument '-MDd' not allowed with
    #   '-fsanitize=address'
    #
    # so a Debug + ASan build has to use /MD. (cl.exe's ASan does support /MDd;
    # clang-cl's does not.) This costs the CRT debug heap, which ASan supersedes
    # anyway -- that is the whole point of the preset. Set as a cache variable
    # before any target exists, and it also keeps gtest consistent, which is
    # already forced onto the DLL CRT by gtest_force_shared_crt.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
    message(STATUS "Sanitizers: CRT forced to /MD (clang-cl ASan rejects /MDd)")

    foreach(_v CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG
               CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_RELWITHDEBINFO)
        if(${_v} MATCHES "/RTC1")
            string(REPLACE "/RTC1" "" ${_v} "${${_v}}")
            set(${_v} "${${_v}}" CACHE STRING "" FORCE)
            message(STATUS "Sanitizers: stripped /RTC1 from ${_v} (incompatible with ASan)")
        endif()
    endforeach()

    # Located relative to the compiler so it tracks whichever LLVM is in use,
    # rather than a hard-coded install path.
    get_filename_component(_llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_llvm_root "${_llvm_bin}" DIRECTORY)
    file(GLOB_RECURSE _asan_dlls
        "${_llvm_root}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-*.dll")
    if(_asan_dlls)
        list(GET _asan_dlls 0 _asan_dll)
        file(COPY "${_asan_dll}" DESTINATION "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
        message(STATUS "Sanitizers: staged ${_asan_dll}")
        # Same directory holds the import/thunk .libs that sts_enable_sanitizers
        # must name explicitly on the link line (see the comment there).
        get_filename_component(_asan_dir "${_asan_dll}" DIRECTORY)
        set(STS_SANITIZER_LIB_DIR "${_asan_dir}" CACHE INTERNAL
            "Directory holding the clang_rt sanitizer runtime libraries")
    else()
        message(WARNING
            "Sanitizers: could not find clang_rt.asan_dynamic-*.dll under "
            "${_llvm_root}. Instrumented binaries will fail at startup with "
            "0xC0000135; put the LLVM windows runtime directory on PATH.")
    endif()
endfunction()
