// Guard: execution-plan task ids must not re-enter src/ or include/.
//
// The engine's comments used to be written in terms of the build plan -- "B3.6
// (DrawCardAction.update:69-73): while the player has No Draw ..." -- so reading
// them required knowing which task did what. 344 such references were removed;
// this test is what stops them coming back one commit at a time.
//
// WHAT COUNTS AS A TASK ID: `A<digits>.<digits>` or `B<digits>.<digits>` at a
// word boundary (B3.6, B4.14, A4.1). Section references use `§`, and the game's
// ascension notation has no dot (A19, A7), so neither collides.
//
// THE EXCEPTION MECHANISM is deliberately narrow and deliberately visible: a
// TASK-ID-ALLOWED-BEGIN / TASK-ID-ALLOWED-END comment pair, where BEGIN must
// carry a `:` and a written reason. It is not a blanket opt-out --
//   * an unterminated block fails,
//   * a block longer than kMaxAllowBlockLines fails (no file-wide opt-out),
//   * a BEGIN with no stated reason fails,
//   * and the TOTAL number of allowlisted ids is capped at kMaxAllowedIds, so
//     widening the exception is an edit to THIS file that a reviewer will see.
//
// The one allowlisted site today is schema.hpp's SCHEMA_VERSION log, where each
// bump names the change that forced it -- the only thing that answers "will this
// old trace still load?".

#include <gtest/gtest.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Total ids the allowlist may cover across the whole tree. Raising this is a
// deliberate, reviewable act -- that is the point of the number being here.
constexpr int kMaxAllowedIds = 8;
// A single allowlist block may not swallow more than this many lines.
constexpr int kMaxAllowBlockLines = 40;

constexpr const char* kBegin = "TASK-ID-ALLOWED-BEGIN";
constexpr const char* kEnd = "TASK-ID-ALLOWED-END";

bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Find a task id in `line` starting at or after `from`. Returns npos if none.
// Shape: word boundary, 'A' or 'B', digit+, '.', digit+.
std::size_t find_task_id(const std::string& line, std::size_t from,
                         std::string* out) {
    for (std::size_t i = from; i < line.size(); ++i) {
        if (line[i] != 'A' && line[i] != 'B') {
            continue;
        }
        if (i > 0 && is_word_char(line[i - 1])) {
            continue;  // trailing part of an identifier, e.g. kFooB3
        }
        std::size_t j = i + 1;
        const std::size_t digits_start = j;
        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) {
            ++j;
        }
        if (j == digits_start || j >= line.size() || line[j] != '.') {
            continue;
        }
        ++j;
        const std::size_t frac_start = j;
        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) {
            ++j;
        }
        if (j == frac_start) {
            continue;
        }
        if (out != nullptr) {
            *out = line.substr(i, j - i);
        }
        return i;
    }
    return std::string::npos;
}

bool scanned_extension(const fs::path& p) {
    const std::string e = p.extension().string();
    return e == ".cpp" || e == ".hpp" || e == ".h" || e == ".txt";
}

struct Violation {
    std::string where;  // "path:line"
    std::string id;
    std::string text;
};

struct ScanResult {
    std::vector<Violation> violations;
    std::vector<std::string> structural;  // malformed allowlist blocks
    int allowed_ids = 0;
};

void scan_file(const fs::path& file, const fs::path& root, ScanResult& r) {
    std::ifstream in(file);
    ASSERT_TRUE(in.good()) << "cannot read " << file.string();

    const std::string rel = fs::relative(file, root).generic_string();
    std::string line;
    int lineno = 0;
    bool in_block = false;
    int block_start = 0;

    while (std::getline(in, line)) {
        ++lineno;

        if (line.find(kEnd) != std::string::npos) {
            if (!in_block) {
                r.structural.push_back(rel + ":" + std::to_string(lineno) +
                                       ": TASK-ID-ALLOWED-END with no BEGIN");
            }
            in_block = false;
            continue;
        }
        if (line.find(kBegin) != std::string::npos) {
            if (in_block) {
                r.structural.push_back(rel + ":" + std::to_string(lineno) +
                                       ": nested TASK-ID-ALLOWED-BEGIN");
            }
            // A reason is mandatory: "...BEGIN: <why>".
            const std::size_t at = line.find(kBegin);
            const std::string tail = line.substr(at + std::string(kBegin).size());
            if (tail.find(':') == std::string::npos ||
                tail.find_first_not_of(": \t") == std::string::npos) {
                r.structural.push_back(
                    rel + ":" + std::to_string(lineno) +
                    ": TASK-ID-ALLOWED-BEGIN must state a reason after ':'");
            }
            in_block = true;
            block_start = lineno;
            continue;
        }

        if (in_block && lineno - block_start > kMaxAllowBlockLines) {
            r.structural.push_back(
                rel + ":" + std::to_string(block_start) +
                ": TASK-ID-ALLOWED block exceeds " +
                std::to_string(kMaxAllowBlockLines) +
                " lines -- the exception must be narrow, not file-wide");
            in_block = false;
        }

        std::string id;
        std::size_t pos = 0;
        while ((pos = find_task_id(line, pos, &id)) != std::string::npos) {
            if (in_block) {
                ++r.allowed_ids;
            } else {
                r.violations.push_back(
                    Violation{rel + ":" + std::to_string(lineno), id, line});
            }
            pos += id.size();
        }
    }

    if (in_block) {
        r.structural.push_back(rel + ":" + std::to_string(block_start) +
                               ": TASK-ID-ALLOWED-BEGIN never closed");
    }
}

ScanResult scan_tree() {
    const fs::path root(STS_SOURCE_ROOT);
    ScanResult r;
    for (const char* dir : {"src", "include"}) {
        const fs::path base = root / dir;
        EXPECT_TRUE(fs::is_directory(base)) << base.string() << " not found";
        for (const auto& e : fs::recursive_directory_iterator(base)) {
            if (e.is_regular_file() && scanned_extension(e.path())) {
                scan_file(e.path(), root, r);
            }
        }
    }
    return r;
}

const char* kFixHint =
    "\n"
    "  Execution-plan task ids do not belong in src/ or include/. A comment must\n"
    "  be readable without knowing the build plan.\n"
    "\n"
    "  TO FIX: rewrite the comment to explain the MECHANISM, not which task\n"
    "  introduced it. If the comment cites Java provenance\n"
    "  (\"ClassName.method (File.java:line)\"), that citation is the durable part --\n"
    "  keep it and drop the id prefix.\n"
    "\n"
    "  Genuinely need to keep one? Wrap it in a documented allowlist block:\n"
    "      // TASK-ID-ALLOWED-BEGIN: <why this id earns its keep>\n"
    "      ...\n"
    "      // TASK-ID-ALLOWED-END\n"
    "  See include/sts/engine/schema.hpp for the one existing use, and read the\n"
    "  header of tests/no_task_ids_test.cpp before adding another.\n";

TEST(NoTaskIds, SourceAndHeadersCarryNoExecutionPlanIds) {
    const ScanResult r = scan_tree();

    std::string msg;
    for (const auto& v : r.violations) {
        msg += "\n  " + v.where + ": task id '" + v.id + "' in:\n      " + v.text;
    }
    EXPECT_TRUE(r.violations.empty())
        << r.violations.size() << " execution-plan task id(s) found:" << msg
        << "\n"
        << kFixHint;
}

TEST(NoTaskIds, AllowlistBlocksAreWellFormed) {
    const ScanResult r = scan_tree();

    std::string msg;
    for (const auto& s : r.structural) {
        msg += "\n  " + s;
    }
    EXPECT_TRUE(r.structural.empty())
        << "malformed TASK-ID-ALLOWED block(s):" << msg << "\n"
        << kFixHint;
}

TEST(NoTaskIds, AllowlistStaysNarrow) {
    const ScanResult r = scan_tree();

    EXPECT_LE(r.allowed_ids, kMaxAllowedIds)
        << "the task-id allowlist now covers " << r.allowed_ids
        << " ids, over the cap of " << kMaxAllowedIds << ".\n"
        << "  The exception exists for a handful of change-narrative comments,\n"
        << "  not as a migration path. Remove ids rather than raising the cap;\n"
        << "  if the cap genuinely must move, say why in the commit message.\n";
    // Also catch the opposite drift: if the allowlisted sites are deleted, the
    // mechanism is unused and the next reader should delete it rather than
    // inherit dead machinery.
    EXPECT_GT(r.allowed_ids, 0)
        << "no allowlisted task ids remain -- consider deleting the\n"
        << "  TASK-ID-ALLOWED mechanism and this test's allowlist handling.";
}

}  // namespace
