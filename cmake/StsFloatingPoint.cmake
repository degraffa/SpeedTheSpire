# The project's floating-point contract -- pinned, not inherited from whatever
# the compiler happens to default to.
#
# WHY THIS EXISTS
#
# This simulator's damage and block pipelines are deliberately float, because
# the Java they re-express is float (DamageInfo.java:35-100, MathUtils.java:217).
# Storage is fixed-width integer; the *arithmetic* is float on purpose. Every
# float operation in src/engine and include/sts is `+ - * /`, a cast, or an
# exact bit operation -- there is not a single transcendental call in either
# tree (no sqrt/pow/exp/log/sin/...; java_round_f in map_rooms.hpp is integer
# significand-shifting plus one exact `(double)a + 0.5`). IEEE-754 specifies
# every one of those exactly, so bit-identical results across compilers and
# platforms are *achievable* -- but only if no compiler is allowed to rewrite
# the expressions.
#
# The one rewrite that matters here is CONTRACTION: fusing `a + b * c` into a
# single FMA, which rounds once instead of twice and therefore produces a
# different float. GCC and Clang both default to `-ffp-contract=fast`, which
# licenses exactly that. The project has two genuinely fusable sites:
#
#   src/engine/interp/interp_damage.cpp:52   dmg + amount * strength_mult
#   include/sts/engine/rng_stream.hpp:173    start + next_float() * (end - start)
#
# The second is the dangerous one: both operands are arbitrary floats, so the
# intermediate product really does round, and a fused form really would differ.
#
# WHY IT IS A NO-OP TODAY, AND WHY THAT IS NOT A REASON TO SKIP IT
#
# Measured on this box with GCC 13.3, `float f(float a, float b, float c) {
# return a + b * c; }`:
#
#   g++ -O2                        -> mulss + addss     (no FMA)
#   g++ -O2 -march=native          -> vfmadd231ss       (fused!)
#   g++ -O2 -march=native -ffp-contract=off -> vmulss + vaddss
#
# The project sets no -march anywhere, so the baseline x86-64 target has no FMA
# instruction to fuse into and contraction cannot bite. The committed fixtures
# and golden vectors are therefore NOT captured under contraction. But that
# safety is accidental: it rests on nobody ever adding `-march=native`,
# `-march=haswell`, `-Ofast`, or `-ffast-math` -- each of which is a one-line,
# entirely plausible "optimisation" that would silently change damage numbers
# and RNG-derived floats while every existing test kept passing on the machine
# that made the change and failing nowhere until a fixture was regenerated.
#
# So this file converts "accidentally safe" into "safe by contract", which is
# also the precondition for the Windows port: cross-toolchain bit-identity is a
# claim you can only make about a pinned FP model.
#
# WHY GLOBAL (add_compile_options), UNLIKE sts_set_warnings
#
# CompilerWarnings.cmake documents, correctly, that -Werror must NOT be hoisted
# to add_compile_options because it reaches the FetchContent dependencies and
# google-benchmark compiles with its own -Werror, so third-party sources become
# hard errors. That reasoning is specific to *promotions to error*. It does not
# transfer here: none of the flags below can turn anything into an error, and
# there is a positive reason to reach every target.
#
# Half the float arithmetic in this project lives in headers
# (rng_stream.hpp, map_rooms.hpp, run_advance.hpp). Header-only code is compiled
# with the *consumer's* flags, so a per-target PRIVATE option would have to be
# repeated on every test, tool and benchmark that includes those headers, and a
# new target that forgot it would silently get contraction back. That is the
# failure mode conventions.md §7 says to remove structurally rather than
# document. Applying the contract to the whole directory tree makes it
# unforgettable, and applying it to gtest as well costs nothing.
#
# The flags land in CMAKE_CXX_FLAGS' successor position on the command line
# (directory options come after CMAKE_CXX_FLAGS), so they also *override* an
# -ffast-math someone adds via -DCMAKE_CXX_FLAGS rather than being overridden
# by it. Last flag wins for all of these.
#
# VERIFICATION, NOT ASSERTION
#
# Nothing here is trusted on the strength of a compiler manual. tests/
# fp_contract_test.cpp computes a fused and an unfused form of the same
# expression at runtime, proves the chosen inputs actually distinguish the two
# (so the test cannot quietly become vacuous), and asserts the compiler produced
# the unfused one. If a future flag change re-enables contraction on any
# toolchain, that test goes red on the spot. It is the reason the MSVC branch
# below can be written from the documentation and still be believed.

# Deliberately plain if() rather than generator expressions: the genex form of
# the frontend-variant query ($<CXX_COMPILER_FRONTEND_VARIANT:...>) needs CMake
# 3.30, this project requires 3.21 and WSL ships 3.28. The *variable*
# CMAKE_CXX_COMPILER_FRONTEND_VARIANT has existed since 3.14, so it is both the
# portable and the more readable choice. It is what distinguishes clang-cl
# (ID Clang, frontend MSVC) from ordinary clang (ID Clang, frontend GNU) -- a
# distinction this file cannot get wrong, because the two take different flags.
function(sts_apply_fp_contract)
    set(_flags)

    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # --- MSVC-style drivers (cl.exe and clang-cl) -----------------------
        # /fp:precise is the safe model; /fp:except- keeps FP exception
        # semantics out of the optimiser (we never read the FP status word).
        # MSVC's contraction behaviour under /fp:precise has changed across
        # toolset versions and is not something to take on trust from a
        # document -- which is why clang-cl also gets an exact contraction
        # control, and why fp_contract_test is the thing that actually decides
        # whether the contract holds on a given Windows toolchain. If that test
        # fails under cl.exe, the answer is /fp:strict (correct, slower), not a
        # weaker assertion.
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            list(APPEND _flags /fp:precise /fp:except-)
        endif()
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            # clang-cl gives access to the exact contraction control that cl.exe
            # has no equivalent for -- but ONLY through the /clang: escape
            # hatch. The bare GNU spelling is NOT accepted:
            #
            #   clang-cl: error: unknown argument ignored in clang-cl:
            #   '-ffp-contract=off' [-Werror,-Wunknown-argument]
            #
            # Measured with clang-cl 22.1.8. An earlier revision of this file
            # asserted the opposite from documentation, and the build caught it
            # -- because clang-cl promotes unknown arguments to errors. Had it
            # been a plain warning the flag would have been silently dropped,
            # contraction left on, and the build would have looked clean. That
            # is what fp_contract_test is for.
            #
            # /fp:precise is deliberately NOT passed alongside this, for two
            # reasons that only show up on a real build:
            #
            #  - It is not sufficient. clang maps /fp:precise to
            #    -ffp-model=precise, which sets -ffp-contract=ON: fusing is
            #    permitted within a single statement, and `a + b * c` is a
            #    single statement. Measured on an FMA-capable target with
            #    clang-cl 22.1.8:
            #       /fp:precise                          -> vfmadd231ss
            #       /fp:precise /clang:-ffp-contract=off -> vmulss + vaddss
            #       /fp:fast                             -> vfmadd231ss
            #    (the default x86-64 target has no FMA instruction, so the
            #    difference is invisible until someone adds -march.)
            #
            #  - Passing both is a hard error, not a redundancy:
            #       clang-cl: error: overriding '-ffp-model=precise' option
            #       with '-ffp-contract=off' [-Werror,-Woverriding-option]
            #
            # clang's default FP model is already precise (no fast-math), so
            # contraction is the only thing that needs saying.
            list(APPEND _flags /clang:-ffp-contract=off)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang|IntelLLVM)$")
        # --- GCC / Clang with the GNU driver --------------------------------
        # -ffp-contract=off is the load-bearing one. The rest deny the
        # reassociation and value-changing transforms that -ffast-math (or
        # -Ofast) would enable, so that adding either upstream of these flags
        # cannot change results.
        list(APPEND _flags
            -ffp-contract=off
            -fno-fast-math
            -fno-unsafe-math-optimizations
            -fno-associative-math
            -fno-reciprocal-math
            -fno-finite-math-only
            -fsigned-zeros)
        # -fexcess-precision=standard forbids keeping a float intermediate in a
        # wider register. On x86-64 SSE2 there is no excess precision to begin
        # with, so this is inert today; it is here because it is exactly the
        # trap that would appear if anything ever targeted x87. Clang only
        # learned the flag in 17, hence the GNU-only guard rather than a shared
        # list.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND _flags -fexcess-precision=standard)
        endif()
    else()
        message(WARNING
            "sts_apply_fp_contract: no floating-point contract is known for "
            "compiler '${CMAKE_CXX_COMPILER_ID}' (frontend "
            "'${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}'). Results may not be "
            "bit-identical to the reference build. fp_contract_test will tell "
            "you whether contraction is actually enabled.")
    endif()

    if(_flags)
        add_compile_options(${_flags})
        message(STATUS "FP contract: ${_flags}")
    endif()
endfunction()
