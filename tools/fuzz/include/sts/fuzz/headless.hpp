#pragma once

// Make this process crash QUIETLY, so a crash is an exit status and not a
// dialog box waiting for a mouse.
//
// A crash is a normal outcome for the soak -- the in-flight journal exists
// precisely so an assert, a segv or an ASan abort still leaves actionable
// triage, and `--inject-abort` exercises that path on every build. On Linux
// `abort()` dies and the parent reads the status. On Windows the CRT and
// Windows Error Reporting both want to tell somebody first, and on a headless
// box (or inside `ctest`) nobody is there: the child sits at 0% CPU forever and
// every parent waiting on it waits forever.
//
// That is not hypothetical. `FuzzTriage.AbortLeavesAnActionableInFlightJournal`
// hung the whole `win-debug` suite this way until the process was killed by
// hand. It had been invisible only because the test's POSIX-quoted command line
// never launched the binary at all (tests/host_shell.hpp); fixing the quoting is
// what first let the abort actually happen.
//
// WHY THIS IS ITS OWN TRANSLATION UNIT rather than three lines in main().
// `<windows.h>` defines `VOID` as an object-like macro (`winnt.h:449`,
// `#define VOID void`), and the generated registry has a `CardId::VOID` -- the
// game's Void status card. Including windows.h anywhere that also sees
// `sts/registry/ids.hpp` turns that enumerator into `void` and produces five
// errors that name the registry rather than the include. NOMINMAX handles
// min/max; there is no switch for the rest of windows.h's vocabulary, and the
// registry's id namespace is exactly the kind that collides with it. So the
// Windows header is confined to one file that includes no engine header, and
// the rest of the tool sees only this declaration.

namespace sts::fuzz {

// Idempotent, and a no-op on non-Windows hosts. Call once, first thing in main.
void make_crashes_headless() noexcept;

}  // namespace sts::fuzz
