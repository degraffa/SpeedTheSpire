// The ONLY file in this tool that includes <windows.h>. See headless.hpp for
// why that isolation is load-bearing (windows.h's `VOID` macro vs the
// registry's `CardId::VOID`). Deliberately includes no engine or registry
// header, and nothing includes this file.

#include "sts/fuzz/headless.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#endif

namespace sts::fuzz {

void make_crashes_headless() noexcept {
#ifdef _WIN32
    // Clear the abort message AND the fault report: with _CALL_REPORTFAULT off,
    // abort() terminates instead of handing the process to Windows Error
    // Reporting.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // The hard-error / GPF / missing-device boxes a segv or a bad path would
    // otherwise raise. Neither call changes the exit status a parent observes,
    // which is the property the crash-triage tests assert on.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                   SEM_NOOPENFILEERRORBOX);
#endif
}

}  // namespace sts::fuzz
