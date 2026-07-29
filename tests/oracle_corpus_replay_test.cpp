#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "host_shell.hpp"

namespace {

using sts::testing::kNullDevice;
using sts::testing::run_shell;
using sts::testing::shell_quote;

std::string command(const char* scratch, bool inject) {
    std::string value =
        shell_quote(STS_PYTHON_EXECUTABLE) + " " +
        shell_quote(STS_CORPUS_SMOKE_PY) + " --archive " +
        shell_quote(STS_CORPUS_ARCHIVE) + " --manifest " +
        shell_quote(STS_CORPUS_MANIFEST) + " --replay-bin " +
        shell_quote(STS_REPLAY_BIN) + " --scratch " + shell_quote(scratch);
    if (inject) value += " --inject-divergence";
    value += " >";
    value += kNullDevice;
    value += " 2>&1";
    return value;
}

TEST(OracleCorpusReplay, FiftySeedCorpusReplaysZeroDiff) {
    EXPECT_EQ(run_shell(command(STS_CORPUS_SCRATCH "/clean", false)), 0);
}

TEST(OracleCorpusReplay, InjectedSyntheticDivergenceFailsLoud) {
    EXPECT_NE(run_shell(command(STS_CORPUS_SCRATCH "/injected", true)), 0);
}

}  // namespace
