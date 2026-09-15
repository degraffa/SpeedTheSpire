// pv8_leak_probe -- the REAL-RUN leak witness for PublicView v8 (ledger T0.8).
//
// WHY A PROGRAM AND NOT A TEST. The 2026-09-03 owner directive (conventions §1)
// retires unit tests as acceptance: the marker of truth is a real run. The GT0
// leak gates (tests/twin_test.cpp, tests/tripwire_test.cpp) are exactly the
// suites that directive says are no longer written, extended or run -- so the
// property they assert has to be re-established by something that IS run. This
// probe is that something, and it is strictly broader than the suite it stands
// in for: the T0.5 sweep visited 10,852 states, this walks millions.
//
// WHAT IT PROVES, in one sentence: over every decision of a large A20 sweep,
// `encode_public_view(state)` and `encode_public_view(make_hidden_twin(state))`
// are BYTE-IDENTICAL -- so nothing v8 added is a function of hidden state --
// AND each new v8 field is witnessed non-zero somewhere, so the publication is
// real rather than dead code that trivially cannot leak.
//
// Those are two different failures and the probe reports both. A field nobody
// ever populates passes a twin comparison for free (the audit doc's own
// warning: "a field this table forgets is twin-invariant"), which is why the
// witness counts below are part of the acceptance and not decoration.
//
// THE DIRECTED COHORT. Two of the four new surfaces are not reachable by any
// scripted policy in this tree:
//   * the boss chest's EQUIP_ITEM_REWARD screen needs the Act-1 boss KILLED at
//     A20 *and* Tiny House or Calling Bell picked out of three offers. This is
//     the same unreachability gen_twin_fixtures.cpp records for
//     RunPhase::BOSS_TREASURE as a whole, and its resolution is the same one:
//     aim a construction at the state instead of hoping a random policy
//     arrives (tests/boss_chest_test.cpp's `at_boss_chest`, and the standalone
//     probe precedent in docs/verification/s3-62-terminal-clear-repair.md).
//   * Dream Catcher's pick needs the relic AND a slept rest site.
// Both are built here from public entry points, and the twin comparison run on
// them is the same one the sweep runs. The report separates swept states from
// directed ones so neither is mistaken for the other.
//
// RUN IT (foreground, one preset):
//     tools/win_build.cmd win-release --target pv8_leak_probe
//     build/win-release/bin/pv8_leak_probe.exe [runs-per-policy]
//
// Exit 0 == zero byte differences and every new field witnessed.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "sts/engine/boss_chest.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/rest_sites.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/twin.hpp"
#include "sts/fuzz/case_id.hpp"
#include "sts/fuzz/fuzz_run.hpp"
#include "sts/fuzz/headless.hpp"
#include "sts/fuzz/policy.hpp"

namespace {

using sts::engine::Action;
using sts::engine::ActionVerb;
using sts::engine::BossChestScreen;
using sts::engine::NeowScreen;
using sts::engine::PublicView;
using sts::engine::PvCardOfferSource;
using sts::engine::PvClaimRowSource;
using sts::engine::RestScreen;
using sts::engine::RunController;
using sts::engine::RunPhase;

constexpr uint8_t kA20 = 20;
constexpr int kPhaseSlots = 32;

// The four v8 surfaces, plus the per-kind campfire coverage. Each must be
// witnessed > 0 or the probe fails: an unpopulated field cannot leak, so a
// green twin comparison over it means nothing.
enum WitnessId {
    kWCardOfferNeow = 0,
    kWCardOfferDream = 1,
    kWCardOfferBoss = 2,
    kWClaimRowsBoss = 3,
    kWRestKinds = 4,
    kWitnessCount = 5,
};

const char* const kWitnessName[kWitnessCount] = {
    "card_offer (Neow CARD_REWARD)",
    "card_offer (Dream Catcher)",
    "card_offer (boss chest EQUIP_ITEM_REWARD)",
    "claim_rows (boss chest EQUIP_ITEM_REWARD)",
    "rest_option_kind (campfire menu)",
};

struct Probe {
    uint64_t states = 0;
    uint64_t directed_states = 0;
    uint64_t phase_states[kPhaseSlots] = {};
    uint64_t witness[kWitnessCount] = {};
    uint64_t rest_kind_seen = 0;  // bit i == PvRestOptionKind i was published
    uint64_t diffs = 0;
    // A reproducible digest of every truth view the probe encoded, in visit
    // order: two runs of this binary over the same sweep must print the same
    // number. That is what makes "the probe was run" a checkable claim rather
    // than a line in a report.
    uint64_t digest = 0xcbf2'9ce4'8422'2325ull;
    std::string first_diff;
    // Twin seeds are a pure function of the visit index, so the whole probe is
    // deterministic (make_hidden_twin draws from a SamplerRng and consumes no
    // engine stream -- twin.hpp).
    int64_t next_twin_seed = 0x5'17'00'0Bll;
};

void fold(Probe& p, uint64_t h) noexcept {
    p.digest ^= h;
    p.digest *= 0x1000'0000'01b3ull;
}

// The one comparison this program exists to make.
void visit(Probe& p, const RunController& rc, const char* origin) noexcept {
    PublicView truth{};
    sts::engine::encode_public_view(rc, truth);

    const int64_t twin_seed = p.next_twin_seed++;
    const RunController twin_rc = sts::engine::make_hidden_twin(rc, twin_seed);
    PublicView twin{};
    sts::engine::encode_public_view(twin_rc, twin);

    ++p.states;
    if (rc.phase < kPhaseSlots) {
        ++p.phase_states[rc.phase];
    }
    fold(p, sts::engine::public_hash(truth));

    // --- v8 witnesses -------------------------------------------------------
    if (truth.card_offer_active != 0) {
        switch (static_cast<PvCardOfferSource>(truth.card_offer_source)) {
            case PvCardOfferSource::NEOW:
                ++p.witness[kWCardOfferNeow];
                break;
            case PvCardOfferSource::DREAM_CATCHER:
                ++p.witness[kWCardOfferDream];
                break;
            case PvCardOfferSource::BOSS_CHEST:
                ++p.witness[kWCardOfferBoss];
                break;
            case PvCardOfferSource::NONE:
                break;
        }
    }
    if (truth.claim_rows_active != 0 &&
        static_cast<PvClaimRowSource>(truth.claim_rows_source) ==
            PvClaimRowSource::BOSS_CHEST) {
        ++p.witness[kWClaimRowsBoss];
    }
    if (truth.rest_option_count != 0) {
        ++p.witness[kWRestKinds];
        for (int i = 0; i < truth.rest_option_count && i < sts::engine::kRestOptionCap;
             ++i) {
            const uint8_t k = truth.rest_option_kind[i];
            if (k < 64) p.rest_kind_seen |= (1ull << k);
        }
    }

    // --- the leak comparison ------------------------------------------------
    const sts::engine::PublicViewDiff d =
        sts::engine::public_view_first_difference(truth, twin);
    if (!d.equal) {
        ++p.diffs;
        if (p.first_diff.empty()) {
            char buf[512];
            std::snprintf(buf, sizeof buf,
                          "%s: phase %d, byte %zu, member `%s` (twin seed %lld)",
                          origin, static_cast<int>(rc.phase), d.offset, d.field,
                          static_cast<long long>(twin_seed));
            p.first_diff = buf;
        }
    }
}

// --- the directed cohort -----------------------------------------------------

void step(RunController& rc, Action a) noexcept {
    sts::engine::StepResult res{};
    sts::engine::advance(std::span<RunController>(&rc, 1),
                         std::span<const Action>(&a, 1),
                         std::span<sts::engine::StepResult>(&res, 1));
}

Action choose(uint8_t arg0) noexcept {
    return sts::engine::make_action(ActionVerb::CHOOSE, arg0);
}

// tests/boss_chest_test.cpp's `at_boss_chest`, reproduced here so the probe is
// standalone: a run parked in the boss chest without walking a whole act, via
// the same public transition the boss reward's proceed takes.
RunController at_boss_chest(int64_t seed) noexcept {
    RunController rc = sts::engine::run_begin(seed, kA20);
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, choose(sts::engine::kChooseProceed));
    rc.run.floor = static_cast<uint16_t>(sts::engine::kActFloorSpan - 1);
    rc.room_type = static_cast<uint8_t>(sts::engine::RoomType::Boss);
    rc.cur_x = static_cast<uint8_t>(sts::engine::kBossCol);
    rc.combat_outcome =
        static_cast<uint8_t>(sts::engine::RunCombatOutcome::KILLED);
    sts::engine::next_room_transition_boss_chest(rc);
    return rc;
}

// Drive the boss chest to EQUIP_ITEM_REWARD with `relic` (Tiny House or Calling
// Bell -- the only two BOSS-tier on_equip_screen bodies that build a claimable
// screen) and probe every state on the way, plus the open card pick that the
// screen's CARDS row leads to.
//
// The offer is OVERWRITTEN rather than fished for: which three relics the pool
// hands out is not what this probe is measuring, and the chest is already
// `seen` by the time anything is published, which is the state resample_hidden
// treats as a pure copy (resample.cpp's opened-chest note).
bool directed_boss_chest(Probe& p, int64_t seed, sts::engine::RelicId relic,
                         std::string& why) noexcept {
    RunController rc = at_boss_chest(seed);
    if (rc.phase != static_cast<uint8_t>(RunPhase::BOSS_TREASURE)) {
        why = "the chest transition did not park in BOSS_TREASURE";
        return false;
    }
    rc.run.boss_chest.relics[0] = static_cast<uint16_t>(relic);
    visit(p, rc, "directed/boss-chest-closed");
    ++p.directed_states;

    step(rc, choose(sts::engine::kChooseOpenChest));
    visit(p, rc, "directed/boss-chest-relic-select");
    ++p.directed_states;

    step(rc, choose(0));  // pick offer 0 -- the relic installed above
    // Calling Bell's onEquip asks for a CONFIRMATION grid first
    // (RelicEquipScreen::GRID_CONFIRM_CALLING_BELL -- relic_pickup_boss.cpp),
    // which is choice-free and offers Proceed; Tiny House goes straight to the
    // claim screen. Stepping through the grid is what makes the two relics
    // reach the same screen, and the grid state itself is worth a comparison.
    if (rc.run.boss_chest.screen ==
        static_cast<uint8_t>(BossChestScreen::EQUIP_GRID)) {
        visit(p, rc, "directed/boss-chest-confirm-grid");
        ++p.directed_states;
        step(rc, choose(sts::engine::kChooseProceed));
    }
    if (rc.run.boss_chest.screen !=
        static_cast<uint8_t>(BossChestScreen::EQUIP_ITEM_REWARD)) {
        why = "the pick did not open EQUIP_ITEM_REWARD";
        return false;
    }
    visit(p, rc, "directed/boss-chest-item-reward");
    ++p.directed_states;

    // The screen's CARDS row opens the pick screen, which is the third
    // card_offer source. Tiny House's rows are GOLD, POTION, CARDS; Calling
    // Bell's are three relics and no card row, so a miss here is not a failure.
    for (uint8_t i = 0; i < rc.rewards.count; ++i) {
        if (static_cast<sts::engine::RewardItemKind>(rc.rewards.items[i].kind) !=
            sts::engine::RewardItemKind::CARDS) {
            continue;
        }
        step(rc, choose(i));
        if (rc.rewards.open_card_item != sts::engine::kNoOpenCardReward) {
            visit(p, rc, "directed/boss-chest-card-pick");
            ++p.directed_states;
        }
        break;
    }
    return true;
}

// A rest site with Dream Catcher owned, slept. The relic is seated directly in
// the public relic list rather than acquired through the pool: the probe is
// measuring the SCREEN, and an acquire would consume a stream and change which
// run this is without changing the state shape under test.
bool directed_dream_catcher(Probe& p, const RunController& rest_site,
                            std::string& why) noexcept {
    RunController rc = rest_site;
    if (rc.run.relic_count >= sts::engine::kRelicCap) {
        why = "no relic slot free on the captured rest-site run";
        return false;
    }
    bool owned = false;
    for (uint8_t i = 0; i < rc.run.relic_count; ++i) {
        owned = owned || rc.run.relics[i].relic_id ==
                             static_cast<uint16_t>(sts::engine::RelicId::DREAM_CATCHER);
    }
    if (!owned) {
        sts::engine::RelicSlot& s = rc.run.relics[rc.run.relic_count++];
        s = sts::engine::RelicSlot{};
        s.relic_id = static_cast<uint16_t>(sts::engine::RelicId::DREAM_CATCHER);
    }
    rc.rest.screen = static_cast<uint8_t>(RestScreen::MENU);
    visit(p, rc, "directed/rest-menu-with-dream-catcher");
    ++p.directed_states;

    const sts::engine::RestMenu menu = sts::engine::build_rest_menu(rc.run);
    for (uint8_t i = 0; i < menu.count; ++i) {
        if (static_cast<sts::engine::RestOptionKind>(menu.entries[i].kind) !=
                sts::engine::RestOptionKind::REST ||
            !menu.entries[i].usable) {
            continue;
        }
        step(rc, choose(i));
        if (rc.rest.screen != static_cast<uint8_t>(RestScreen::DREAM_CATCHER)) {
            why = "sleeping did not open the Dream Catcher pick";
            return false;
        }
        visit(p, rc, "directed/dream-catcher-pick");
        ++p.directed_states;
        return true;
    }
    why = "the captured rest site offered no usable Rest button";
    return false;
}

// The sweep also harvests one rest-site controller for the Dream Catcher
// construction above, so the directed cohort is built from a REAL run rather
// than a hand-assembled RunState.
constexpr std::size_t kRestSiteHarvest = 8;

struct Harvest {
    Probe* probe = nullptr;
    // SEVERAL rest sites, not one: each carries a different master deck, so
    // the Dream Catcher offer rolled off it differs, and one lucky state
    // cannot be mistaken for coverage.
    std::vector<RunController> rest_sites;
    uint64_t seen_rest_sites = 0;
};

void harvest_observer(const RunController& rc, void* ctx) noexcept {
    Harvest& h = *static_cast<Harvest*>(ctx);
    visit(*h.probe, rc, "sweep");
    if (rc.phase != static_cast<uint8_t>(RunPhase::REST_SITE) ||
        rc.rest.screen != static_cast<uint8_t>(RestScreen::MENU)) {
        return;
    }
    // Spread the sample across the whole sweep rather than taking the first
    // eight states of the first run.
    const uint64_t n = h.seen_rest_sites++;
    if (h.rest_sites.size() < kRestSiteHarvest && (n % 211ull) == 0ull) {
        h.rest_sites.push_back(rc);
    }
}

const char* phase_name(int ph) noexcept {
    switch (static_cast<RunPhase>(ph)) {
        case RunPhase::NONE: return "NONE";
        case RunPhase::NEOW: return "NEOW";
        case RunPhase::MAP_CHOICE: return "MAP_CHOICE";
        case RunPhase::COMBAT: return "COMBAT";
        case RunPhase::COMBAT_REWARD: return "COMBAT_REWARD";
        case RunPhase::REST_SITE: return "REST_SITE";
        case RunPhase::TREASURE_ROOM: return "TREASURE_ROOM";
        case RunPhase::EVENT_DIALOG: return "EVENT_DIALOG";
        case RunPhase::SHOP: return "SHOP";
        case RunPhase::ROOM_UNIMPLEMENTED: return "ROOM_UNIMPLEMENTED";
        case RunPhase::RUN_OVER: return "RUN_OVER";
        default: return "BOSS_TREASURE/other";
    }
}

}  // namespace

int main(int argc, char** argv) {
    sts::fuzz::make_crashes_headless();

    // Runs PER POLICY; five policies, so the default is 20,000 A20 runs.
    int64_t per_policy = 4000;
    if (argc > 1) {
        per_policy = std::strtoll(argv[1], nullptr, 10);
        if (per_policy <= 0) per_policy = 4000;
    }

    const sts::fuzz::PolicyKind policies[] = {
        sts::fuzz::PolicyKind::RANDOM,      sts::fuzz::PolicyKind::GREEDY_DAMAGE,
        sts::fuzz::PolicyKind::GREEDY_BLOCK, sts::fuzz::PolicyKind::HOARD_GOLD,
        sts::fuzz::PolicyKind::ALWAYS_EVENT};
    const unsigned policy_count =
        static_cast<unsigned>(sizeof policies / sizeof policies[0]);

    Probe probe;
    Harvest harvest;
    harvest.probe = &probe;

    uint64_t runs = 0;
    for (unsigned pi = 0; pi < policy_count; ++pi) {
        for (int64_t seed = 1; seed <= per_policy; ++seed) {
            sts::fuzz::CaseId id;
            id.run_seed = seed;
            id.ascension = kA20;
            id.policy = policies[pi];
            id.policy_seed = static_cast<uint64_t>(seed) * 1000003ull +
                             uint64_t{pi} * 7919ull + 17ull;

            sts::fuzz::StepObserver obs;
            obs.fn = &harvest_observer;
            obs.ctx = &harvest;

            sts::fuzz::CaseResult result;
            sts::fuzz::run_case(id, sts::fuzz::RunLimits{}, nullptr, result,
                                /*verify_repro=*/false, sts::fuzz::Inject{},
                                obs);
            ++runs;
        }
        std::printf("  policy %d done: %llu states so far\n", pi,
                    static_cast<unsigned long long>(probe.states));
        std::fflush(stdout);
    }

    const uint64_t swept_states = probe.states;

    // --- directed cohort ----------------------------------------------------
    int directed_failures = 0;
    std::string why;
    for (int64_t seed : {12345ll, 777ll, 202609ll}) {
        if (!directed_boss_chest(probe, seed, sts::engine::RelicId::TINY_HOUSE,
                                 why)) {
            std::fprintf(stderr, "directed boss chest (Tiny House, seed %lld): %s\n",
                         static_cast<long long>(seed), why.c_str());
            ++directed_failures;
        }
        if (!directed_boss_chest(probe, seed,
                                 sts::engine::RelicId::CALLING_BELL, why)) {
            std::fprintf(stderr,
                         "directed boss chest (Calling Bell, seed %lld): %s\n",
                         static_cast<long long>(seed), why.c_str());
            ++directed_failures;
        }
    }
    if (harvest.rest_sites.empty()) {
        std::fprintf(stderr, "directed Dream Catcher: no rest site harvested\n");
        ++directed_failures;
    }
    for (const RunController& site : harvest.rest_sites) {
        if (!directed_dream_catcher(probe, site, why)) {
            std::fprintf(stderr, "directed Dream Catcher: %s\n", why.c_str());
            ++directed_failures;
        }
    }

    // --- report -------------------------------------------------------------
    std::printf("\n=== pv8_leak_probe ===\n");
    std::printf("PUBLIC_VIEW_VERSION  %u\n",
                static_cast<unsigned>(sts::engine::PUBLIC_VIEW_VERSION));
    std::printf("sizeof(PublicView)   %zu\n", sizeof(PublicView));
    std::printf("A20 runs             %llu (%d policies x %lld seeds)\n",
                static_cast<unsigned long long>(runs), policy_count,
                static_cast<long long>(per_policy));
    std::printf("states compared      %llu (%llu swept + %llu directed)\n",
                static_cast<unsigned long long>(probe.states),
                static_cast<unsigned long long>(swept_states),
                static_cast<unsigned long long>(probe.directed_states));
    std::printf("view digest          0x%016llx\n",
                static_cast<unsigned long long>(probe.digest));
    std::printf("\nstates per phase:\n");
    for (int ph = 0; ph < kPhaseSlots; ++ph) {
        if (probe.phase_states[ph] == 0) continue;
        std::printf("  %-22s %llu\n", phase_name(ph),
                    static_cast<unsigned long long>(probe.phase_states[ph]));
    }
    std::printf("\nv8 field witnesses (each must be > 0):\n");
    int missing = 0;
    for (int w = 0; w < kWitnessCount; ++w) {
        std::printf("  %-44s %llu%s\n", kWitnessName[w],
                    static_cast<unsigned long long>(probe.witness[w]),
                    probe.witness[w] == 0 ? "   <-- NOT WITNESSED" : "");
        if (probe.witness[w] == 0) ++missing;
    }
    std::printf("  campfire kinds published (PvRestOptionKind bitset) 0x%llx\n",
                static_cast<unsigned long long>(probe.rest_kind_seen));
    std::printf("\ntwin byte differences %llu\n",
                static_cast<unsigned long long>(probe.diffs));
    if (!probe.first_diff.empty()) {
        std::printf("  first: %s\n", probe.first_diff.c_str());
    }
    std::printf("\nbyte-classification tiling of RunController is a build-time\n"
                "static_assert in include/sts/engine/byte_class.hpp, so this\n"
                "binary existing is the evidence for it.\n");

    const bool ok = probe.diffs == 0 && missing == 0 && directed_failures == 0;
    std::printf("\n%s\n", ok ? "PROBE GREEN" : "PROBE RED");
    return ok ? 0 : 1;
}
