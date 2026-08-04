// T0.7 -- the omniscient-boundary check, tested as a TOOL.
//
// tools/check_omniscient_boundary.sh is the enforcement point for the
// information boundary: training-facing code may not reach the full-state
// observation surface (include/sts/engine/omniscient_observation.hpp). A grep
// guard that never fires is indistinguishable from one whose pattern stopped
// matching, so the negative control is a standing test rather than something
// somebody tried once by hand: the script is run against three committed
// fixture directories (tests/fixtures/omniscient_boundary/) and must
//   * accept `clean/`    -- a PublicView-only actor,
//   * REJECT `violation/`-- an actor that includes the omniscient header and
//                           calls its encoder, and
//   * accept `hatched/`  -- a comment that names the other surface with the
//                           documented `omniscient-boundary-ok` token.
//
// The script's `--scan DIR` mode is used deliberately: it needs no git, so this
// runs identically under WSL (where git cannot read a linked worktree's
// `gitdir: D:/...`, conventions §6) and on Windows. The script's DEFAULT mode
// is the git-side repo sweep, which is CI's job, not a test's.
//
// std::system() goes through tests/host_shell.hpp, the only sanctioned call
// site (conventions §8): quoting, the null device and "which bash" all differ
// between hosts, and each has cost this project real time.
//
// This file deliberately spells the boundary token many times. That is not a
// violation: the checked set is the training trees plus the denylist (see the
// script header), and a test of the guard is neither.

#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "host_shell.hpp"

namespace {

using sts::testing::bash_program;
using sts::testing::run_shell;
using sts::testing::shell_quote;

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Run the check against one fixture directory, returning its exit status.
// Output is captured so a failing expectation can show why.
int run_check(const std::string& fixture, std::string* log_text) {
    const std::string log = std::string(STS_OMNISCIENT_SCRATCH) + "/" + fixture + ".log";
    const std::string command = bash_program() +
                                shell_quote(STS_OMNISCIENT_CHECK) + " --scan " +
                                shell_quote(std::string(STS_OMNISCIENT_FIXTURES) + "/" +
                                            fixture) +
                                " > " + shell_quote(log) + " 2>&1";
    const int rc = run_shell(command);
    *log_text = read_text(log);
    return rc;
}

class OmniscientBoundary : public ::testing::Test {
protected:
    void SetUp() override {
        if (bash_program().empty()) {
            GTEST_SKIP() << "no host bash found (STS_TEST_BASH empty) -- the "
                            "boundary check is a shell script";
        }
    }
};

// A training-facing file that reads only PublicView passes.
TEST_F(OmniscientBoundary, AcceptsAPublicViewOnlyActor) {
    std::string log;
    EXPECT_EQ(run_check("clean", &log), 0) << log;
}

// THE NEGATIVE CONTROL: a training-facing file that reaches the omniscient
// surface fails, and the report names the offending file.
TEST_F(OmniscientBoundary, RejectsAnActorThatReachesTheOmniscientSurface) {
    std::string log;
    EXPECT_NE(run_check("violation", &log), 0) << log;
    EXPECT_NE(log.find("leaky_actor.cpp"), std::string::npos)
        << "the report must name the file that crossed the boundary:\n"
        << log;
}

// The documented escape hatch keeps prose about the boundary legal.
TEST_F(OmniscientBoundary, AcceptsAHatchedContrastingComment) {
    std::string log;
    EXPECT_EQ(run_check("hatched", &log), 0) << log;
}

// An unusable invocation must be an error (2), never a silent pass: a check
// that "passes" because its argument was wrong is worse than no check.
TEST_F(OmniscientBoundary, FailsLoudlyOnABadArgument) {
    const std::string log = std::string(STS_OMNISCIENT_SCRATCH) + "/badarg.log";
    const std::string command = bash_program() + shell_quote(STS_OMNISCIENT_CHECK) +
                                " --scan " +
                                shell_quote(std::string(STS_OMNISCIENT_FIXTURES) +
                                            "/no_such_directory") +
                                " > " + shell_quote(log) + " 2>&1";
    EXPECT_NE(run_shell(command), 0) << read_text(log);
}

}  // namespace
