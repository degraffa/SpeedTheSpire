// Twin-fixture container. See twin_fixture.hpp for the format, the
// recipe-not-a-state-dump rationale, and the refusal rules.

#include "sts/twin/twin_fixture.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

#include "sts/engine/advance.hpp"
#include "sts/engine/twin.hpp"
#include "sts/engine/types.hpp"

namespace sts::twin {

namespace {

// One case's fixed-size prefix, laid out to 40 bytes with no padding so it can
// be written as raw bytes deterministically.
struct CaseHeader {
    int64_t run_seed;
    uint64_t policy_seed;
    int64_t twin_seed;
    uint32_t step_index;
    uint32_t action_count;
    uint8_t ascension;
    uint8_t policy;
    uint8_t run_phase;
    uint8_t pad[5];
};

static_assert(sizeof(CaseHeader) == 40,
              "TwinCase header must be 40 bytes with no padding");

template <typename T>
void put(std::ofstream& out, const T& v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
bool get(std::ifstream& in, T& v) {
    in.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(in);
}

}  // namespace

TwinFixtureHeader current_header(uint32_t case_count) noexcept {
    TwinFixtureHeader h{};
    std::memcpy(h.magic, kTwinFixtureMagic, sizeof(h.magic));
    h.format_version = kTwinFixtureFormat;
    h.engine_schema_version = engine::SCHEMA_VERSION;
    h.public_view_version = engine::PUBLIC_VIEW_VERSION;
    h.public_view_size = static_cast<uint32_t>(sizeof(engine::PublicView));
    h.run_controller_size =
        static_cast<uint32_t>(sizeof(engine::RunController));
    h.case_count = case_count;
    h.reserved = 0;
    return h;
}

bool write_twin_fixture(const std::string& path,
                        const std::vector<TwinCase>& cases,
                        std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot open for writing: " + path;
        return false;
    }
    put(out, current_header(static_cast<uint32_t>(cases.size())));
    for (const TwinCase& c : cases) {
        if (c.actions.size() != c.step_index) {
            error = "case action list does not match its step index";
            return false;
        }
        CaseHeader ch{};
        ch.run_seed = c.run_seed;
        ch.policy_seed = c.policy_seed;
        ch.twin_seed = c.twin_seed;
        ch.step_index = c.step_index;
        ch.action_count = static_cast<uint32_t>(c.actions.size());
        ch.ascension = c.ascension;
        ch.policy = c.policy;
        ch.run_phase = c.run_phase;
        put(out, ch);
        if (!c.actions.empty()) {
            out.write(reinterpret_cast<const char*>(c.actions.data()),
                      static_cast<std::streamsize>(c.actions.size() *
                                                   sizeof(uint32_t)));
        }
        put(out, c.view);
    }
    out.flush();
    if (!out) {
        error = "write failed: " + path;
        return false;
    }
    return true;
}

bool read_twin_fixture(const std::string& path, TwinFixtureHeader& header,
                       std::vector<TwinCase>& cases, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open for reading: " + path;
        return false;
    }
    if (!get(in, header)) {
        error = "truncated header: " + path;
        return false;
    }
    const TwinFixtureHeader want = current_header(header.case_count);
    if (std::memcmp(header.magic, want.magic, sizeof(want.magic)) != 0) {
        error = "not a twin fixture (bad magic): " + path;
        return false;
    }
    // Refuse-on-mismatch, one named reason each -- a silent reinterpretation of
    // a stale fixture is exactly what plan T1.2's loaders forbid.
    if (header.format_version != want.format_version) {
        error = "twin fixture format version mismatch";
        return false;
    }
    if (header.engine_schema_version != want.engine_schema_version) {
        error = "engine SCHEMA_VERSION mismatch -- regenerate the fixture";
        return false;
    }
    if (header.public_view_version != want.public_view_version) {
        error = "PUBLIC_VIEW_VERSION mismatch -- regenerate the fixture";
        return false;
    }
    if (header.public_view_size != want.public_view_size ||
        header.run_controller_size != want.run_controller_size) {
        error = "struct size mismatch -- regenerate the fixture";
        return false;
    }

    cases.clear();
    cases.reserve(header.case_count);
    for (uint32_t i = 0; i < header.case_count; ++i) {
        CaseHeader ch{};
        if (!get(in, ch)) {
            error = "truncated case header";
            return false;
        }
        if (ch.action_count != ch.step_index) {
            error = "case action count disagrees with its step index";
            return false;
        }
        TwinCase c;
        c.run_seed = ch.run_seed;
        c.policy_seed = ch.policy_seed;
        c.twin_seed = ch.twin_seed;
        c.step_index = ch.step_index;
        c.ascension = ch.ascension;
        c.policy = ch.policy;
        c.run_phase = ch.run_phase;
        c.actions.resize(ch.action_count);
        if (ch.action_count != 0) {
            in.read(reinterpret_cast<char*>(c.actions.data()),
                    static_cast<std::streamsize>(ch.action_count *
                                                 sizeof(uint32_t)));
            if (!in) {
                error = "truncated action list";
                return false;
            }
        }
        if (!get(in, c.view)) {
            error = "truncated PublicView payload";
            return false;
        }
        cases.push_back(std::move(c));
    }
    // Trailing bytes are a corrupt file, not a longer one.
    in.peek();
    if (!in.eof()) {
        error = "trailing bytes after the last case";
        return false;
    }
    return true;
}

bool rebuild_twin_case(const TwinCase& c, engine::RunController& truth,
                       engine::RunController& twin) noexcept {
    if (c.actions.size() != c.step_index) {
        return false;
    }
    truth = engine::run_begin(c.run_seed, c.ascension);
    for (uint32_t bits : c.actions) {
        engine::Action a{bits};
        engine::StepResult res{};
        engine::advance(std::span<engine::RunController>(&truth, 1),
                        std::span<const engine::Action>(&a, 1),
                        std::span<engine::StepResult>(&res, 1));
    }
    twin = engine::make_hidden_twin(truth, c.twin_seed);
    return true;
}

std::size_t verify_twin_fixture(const std::vector<TwinCase>& cases,
                                std::string& report) {
    std::size_t failed = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const TwinCase& c = cases[i];
        engine::RunController truth;
        engine::RunController twin;
        char line[256];
        if (!rebuild_twin_case(c, truth, twin)) {
            std::snprintf(line, sizeof(line), "case %zu: malformed\n", i);
            report += line;
            ++failed;
            continue;
        }
        if (truth.phase != c.run_phase) {
            std::snprintf(line, sizeof(line),
                          "case %zu: replay reached phase %d, fixture says %d\n",
                          i, static_cast<int>(truth.phase),
                          static_cast<int>(c.run_phase));
            report += line;
            ++failed;
            continue;
        }
        engine::PublicView got{};
        engine::encode_public_view(truth, got);
        if (std::memcmp(&got, &c.view, sizeof(engine::PublicView)) != 0) {
            const engine::PublicViewDiff d =
                engine::public_view_first_difference(c.view, got);
            std::snprintf(line, sizeof(line),
                          "case %zu: replayed view differs from the stored one "
                          "at byte %zu (PublicView.%s)\n",
                          i, d.offset, d.field);
            report += line;
            ++failed;
            continue;
        }
        engine::PublicView twin_view{};
        engine::encode_public_view(twin, twin_view);
        if (std::memcmp(&twin_view, &c.view, sizeof(engine::PublicView)) != 0) {
            const engine::PublicViewDiff d =
                engine::public_view_first_difference(c.view, twin_view);
            std::snprintf(line, sizeof(line),
                          "case %zu: TWIN view differs from the stored one at "
                          "byte %zu (PublicView.%s) -- a leak\n",
                          i, d.offset, d.field);
            report += line;
            ++failed;
        }
    }
    return failed;
}

}  // namespace sts::twin
