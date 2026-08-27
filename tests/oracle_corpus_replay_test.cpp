#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "host_shell.hpp"

namespace {

using sts::testing::kNullDevice;
using sts::testing::run_shell;
using sts::testing::shell_quote;

std::string command(const char* archive, const char* manifest,
                    const char* entries, const char* scratch, bool inject) {
    std::string value =
        shell_quote(STS_PYTHON_EXECUTABLE) + " " +
        shell_quote(STS_CORPUS_SMOKE_PY) + " --archive " +
        shell_quote(archive) + " --manifest " +
        shell_quote(manifest) + " --replay-bin " +
        shell_quote(STS_REPLAY_BIN) + " --expect-entries " + entries +
        " --scratch " + shell_quote(scratch);
    if (inject) value += " --inject-divergence";
    value += " >";
    value += kNullDevice;
    value += " 2>&1";
    return value;
}

std::string act1(const char* scratch, bool inject) {
    return command(STS_CORPUS_ARCHIVE, STS_CORPUS_MANIFEST, "50", scratch,
                   inject);
}

std::string three_act(const char* scratch, bool inject) {
    return command(STS_THREE_ACT_ARCHIVE, STS_THREE_ACT_MANIFEST, "5", scratch,
                   inject);
}

TEST(OracleCorpusReplay, FiftySeedCorpusReplaysZeroDiff) {
    EXPECT_EQ(run_shell(act1(STS_CORPUS_SCRATCH "/clean", false)), 0);
}

TEST(OracleCorpusReplay, InjectedSyntheticDivergenceFailsLoud) {
    EXPECT_NE(run_shell(act1(STS_CORPUS_SCRATCH "/injected", true)), 0);
}

// S2.46. The curated Acts 1-3 corpus: five whole three-act A20 captures, two
// of them completed double-boss victories over different first bosses. This is
// the only place the committed suite replays the Act-2 boss chest, the
// act-2->3 transition, an Act-3 boss kill and the A20 double-boss COMPLETE
// handoff against a real capture rather than a constructed state.
TEST(OracleCorpusReplay, ThreeActCorpusReplaysZeroDiff) {
    EXPECT_EQ(run_shell(three_act(STS_CORPUS_SCRATCH "/three_act", false)), 0);
}

TEST(OracleCorpusReplay, ThreeActInjectedSyntheticDivergenceFailsLoud) {
    EXPECT_NE(
        run_shell(three_act(STS_CORPUS_SCRATCH "/three_act_injected", true)),
        0);
}

}  // namespace
