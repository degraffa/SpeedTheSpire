// Running a child process from a test, on both hosts.
//
// WHY THIS HEADER EXISTS (conventions.md §7, the rule of two). `std::system()`
// is not portable in the way its signature suggests: on POSIX it hands the
// string to `/bin/sh`, on Windows to `cmd.exe /C`, and the two disagree about
// quoting, about the null device, and about whether `bash` exists at all. That
// disagreement has now been paid for twice:
//
//   1. registry_gen_test.cpp -- all 9 RegistryGen cases failed on Windows with
//      a generator exit status of 1 and the message "The filename, directory
//      name, or volume label syntax is incorrect." It was diagnosed and fixed
//      IN PLACE, with a long comment, in that one file.
//   2. fuzz_test.cpp -- six tests (FuzzTriage x3, FuzzDriver x1, FuzzRunner x1
//      here, Translator.RoundTripDeterministic being unrelated) failed on
//      `win-debug` with the SAME cmd.exe message, because the file had its own
//      private `shell_quote` that emitted POSIX single quotes.
//
// The second occurrence is what §7 calls a failure of documentation. So the
// knowledge lives here, once, and both call sites use it.
//
// --- the three rules ------------------------------------------------------
//
// QUOTING. cmd.exe does not understand `'`. It uses `"`, and it adds a rule
// with no POSIX analogue: when the command string starts with a quote, cmd
// strips the FIRST and the LAST quote character in the whole string and treats
// what remains as the command. Any command line whose first token is a quoted
// path (they all are here -- build paths can contain spaces) therefore arrives
// torn apart. The documented fix is an extra enclosing pair
// (`cmd /C ""a b" "c d""`): the outer pair is what gets stripped, leaving the
// inner quoting intact. `run_shell()` applies it. It is deliberately NOT
// applied on POSIX, where /bin/sh would read the extra quotes as part of the
// program name.
//
// NULL DEVICE. `>/dev/null` under cmd.exe tries to create a file at `\dev\null`
// and fails; the redirect, not the program, is what returns non-zero. In an
// `EXPECT_NE(..., 0)` test that failure is INVISIBLE -- the assertion passes for
// the wrong reason and the program under test never ran. Use `kNullDevice`.
//
// BASH. `bash` on the Windows PATH is WSL's bash (C:\Windows\System32\bash.exe),
// which cannot execute a script named by a `D:\...` path. Git for Windows' bash
// can. CMake picks the right one and passes it as STS_TEST_BASH; `bash_program()`
// returns it, or an empty string when the host has none, which callers must
// treat as a skip with a named reason rather than a failure.

#ifndef STS_TESTS_HOST_SHELL_HPP
#define STS_TESTS_HOST_SHELL_HPP

#include <cstdlib>
#include <string>

namespace sts::testing {

// The host's discard sink for a shell redirect.
#ifdef _WIN32
inline constexpr const char* kNullDevice = "NUL";
#else
inline constexpr const char* kNullDevice = "/dev/null";
#endif

// Quote one argument for the host shell.
inline std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    // cmd.exe: `"` is the only quote it knows. A literal `"` inside an argument
    // has no portable escape here and no call site needs one, so it is dropped
    // deliberately rather than silently mis-quoted.
    std::string out = "\"";
    for (const char c : value) {
        if (c != '"') out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

// Run a command line through the host shell and return its exit status.
// This is the ONLY sanctioned std::system() call site in tests/.
inline int run_shell(const std::string& command) {
#ifdef _WIN32
    const std::string wrapped = "\"" + command + "\"";
    return std::system(wrapped.c_str());
#else
    return std::system(command.c_str());
#endif
}

// A bash that can run an in-tree .sh script, already quoted and with a trailing
// space, e.g. `"C:\Program Files\Git\bin\bash.exe" `. Empty when the host has
// none -- callers GTEST_SKIP with the reason rather than failing.
inline std::string bash_program() {
#ifdef STS_TEST_BASH
    const std::string p = STS_TEST_BASH;
    if (p.empty()) return {};
    return shell_quote(p) + " ";
#else
    return "bash ";
#endif
}

}  // namespace sts::testing

#endif  // STS_TESTS_HOST_SHELL_HPP
