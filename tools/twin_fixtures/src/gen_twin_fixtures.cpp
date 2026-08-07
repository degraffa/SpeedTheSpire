// gen_twin_fixtures -- writes tests/golden/twin_fixtures/twins_v1.bin.
//
// Run manually to regenerate the committed fixture, exactly like
// tools/fixture_gen/gen_combat_fixtures (the precedent this follows):
//
//     cmake --build --preset win-debug --target gen_twin_fixtures
//     build/win-debug/bin/gen_twin_fixtures
//
// WHAT IT HARVESTS. One case per reachable RunPhase, plus a second case for the
// phases where a second is cheap, taken from the SAME fuzz run loop the twin
// suite sweeps with (`fuzz::run_case` + `StepObserver`, so no policy loop is
// forked -- fuzz_run.hpp). For each selected state the case records the recipe
// (case id + the action prefix that reaches it + a twin seed) and the
// `PublicView` both twins encode to.
//
// It VERIFIES before it writes: every case is rebuilt from its own recipe and
// must reproduce the stored view, and the twin must too. A generator that can
// emit a fixture nobody can replay is worse than no generator.

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "sts/engine/public_view.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/twin.hpp"
#include "sts/fuzz/case_id.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/headless.hpp"
#include "sts/fuzz/policy.hpp"
#include "sts/twin/twin_fixture.hpp"

namespace {

using sts::engine::RunPhase;

// Which call index of the observer first landed in each phase, for one case.
struct PhaseWalk {
    std::map<uint8_t, uint32_t> first_call;  // phase -> observer call index
    uint32_t calls = 0;
};

void walk(const sts::engine::RunController& rc, void* ctx) noexcept {
    PhaseWalk& w = *static_cast<PhaseWalk*>(ctx);
    w.first_call.emplace(rc.phase, w.calls);
    ++w.calls;
}

constexpr uint8_t kA20 = 20;

const RunPhase kWanted[] = {
    RunPhase::NEOW,        RunPhase::MAP_CHOICE,     RunPhase::COMBAT,
    RunPhase::COMBAT_REWARD, RunPhase::REST_SITE,    RunPhase::TREASURE_ROOM,
    RunPhase::EVENT_DIALOG, RunPhase::SHOP,          RunPhase::RUN_OVER,
};

// RunPhase::BOSS_TREASURE (S2.11) is DELIBERATELY ABSENT, and this note is the
// reason rather than an oversight. The boss chest carries the second
// masked-contents trap -- an unopened chest's three offers are drawn at room
// entry and hidden until the open action -- so it is exactly the shape this
// gate exists for. But reaching it requires WINNING the act-1 boss at ascension
// 20, and none of the four scripted policies above does that on any seed in the
// sweep below; adding it to kWanted only makes the generator print "no case
// harvested for phase 11" on every run, which is a stale warning, not coverage.
//
// The invariance it would have gated is covered DIRECTLY instead, by
// tests/boss_chest_test.cpp's AnUnopenedChestsTwinEncodesIdentically and
// AnOpenedChestsTwinKeepsTheOffers, which build the state by aiming
// next_room_transition_boss_chest at it rather than by hoping a random policy
// arrives. Put it back here the moment a boss-beating scripted policy exists
// (the TE.1 survival-cohort seam is where one would land).

}  // namespace

int main() {
    sts::fuzz::make_crashes_headless();

    std::vector<sts::twin::TwinCase> cases;
    std::map<uint8_t, int> harvested;

    const sts::fuzz::PolicyKind policies[] = {
        sts::fuzz::PolicyKind::RANDOM, sts::fuzz::PolicyKind::GREEDY_DAMAGE,
        sts::fuzz::PolicyKind::HOARD_GOLD,
        sts::fuzz::PolicyKind::ALWAYS_EVENT};

    // Two cases per phase is enough for an invariance gate and keeps the
    // committed file small; the sweep in tests/twin_test.cpp is where volume
    // lives.
    constexpr int kPerPhase = 2;

    for (sts::fuzz::PolicyKind p : policies) {
        for (int64_t seed = 1; seed <= 60; ++seed) {
            PhaseWalk w;
            sts::fuzz::StepObserver obs;
            obs.fn = &walk;
            obs.ctx = &w;

            sts::fuzz::CaseId id;
            id.run_seed = seed;
            id.ascension = kA20;
            id.policy = p;
            id.policy_seed = static_cast<uint64_t>(seed) * 1000003ull + 17ull;

            sts::fuzz::CaseResult result;
            sts::fuzz::run_case(id, sts::fuzz::RunLimits{}, nullptr, result,
                                /*verify_repro=*/false, sts::fuzz::Inject{},
                                obs);

            for (RunPhase want : kWanted) {
                const uint8_t ph = static_cast<uint8_t>(want);
                if (harvested[ph] >= kPerPhase) continue;
                const auto it = w.first_call.find(ph);
                if (it == w.first_call.end()) continue;

                uint32_t prefix = it->second;
                if (prefix > result.trajectory.size()) {
                    prefix = static_cast<uint32_t>(result.trajectory.size());
                }

                sts::twin::TwinCase c;
                c.run_seed = id.run_seed;
                c.policy_seed = id.policy_seed;
                c.ascension = id.ascension;
                c.policy = static_cast<uint8_t>(id.policy);
                c.step_index = prefix;
                c.twin_seed = 0x7'1E'D0 + seed * 31 + static_cast<int64_t>(ph);
                for (uint32_t i = 0; i < prefix; ++i) {
                    c.actions.push_back(result.trajectory[i].bits);
                }

                sts::engine::RunController truth;
                sts::engine::RunController twin;
                if (!sts::twin::rebuild_twin_case(c, truth, twin)) continue;
                if (truth.phase != ph) continue;  // replay did not land there
                c.run_phase = truth.phase;
                sts::engine::encode_public_view(truth, c.view);

                cases.push_back(std::move(c));
                ++harvested[ph];
            }
        }
    }

    std::string report;
    const std::size_t failed = sts::twin::verify_twin_fixture(cases, report);
    if (failed != 0) {
        std::fprintf(stderr, "%zu case(s) failed verification:\n%s", failed,
                     report.c_str());
        return 1;
    }

    const std::string path = std::string(STS_TWIN_FIXTURE_OUT) + "/twins_v1.bin";
    std::string error;
    if (!sts::twin::write_twin_fixture(path, cases, error)) {
        std::fprintf(stderr, "write failed: %s\n", error.c_str());
        return 1;
    }

    std::printf("wrote %zu twin cases to %s\n", cases.size(), path.c_str());
    for (const auto& kv : harvested) {
        std::printf("  phase %d: %d case(s)\n", static_cast<int>(kv.first),
                    kv.second);
    }
    for (RunPhase want : kWanted) {
        if (harvested[static_cast<uint8_t>(want)] == 0) {
            std::fprintf(stderr,
                         "WARNING: no case harvested for phase %d\n",
                         static_cast<int>(want));
            return 1;
        }
    }
    return 0;
}
