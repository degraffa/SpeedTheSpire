// S2.44 -- the tier-4 S2 distributional family (s2-design §6, S2-G2 item 6).
//
// ONE pre-registered Holm-Bonferroni family of 13 hypotheses at family-wise
// alpha 0.01, sitting ALONGSIDE B5.3's analytic family rather than inside it
// (README.md "Pre-registered S2 act-2/3 family" is the authority on the
// registration; this file is its implementation). The registration -- the
// hypothesis list, the alpha, the correction and the sample sizes -- is frozen
// BEFORE any campaign run, exactly as B5.3 froze its own.
//
// Every OBSERVED count comes out of the engine's own entry points
// (generate_monster_lists, assemble_combat_rewards, init_event_pools /
// reinit_act_event_pools / generate_event, initialize_relic_pools /
// roll_boss_chest, encode_public_view) driven from deterministic seeds, so a
// rerun of the same `--seeds` reproduces every number. Every EXPECTED
// distribution comes from s2_expect.hpp, which reads registry data and the
// cited Java rules and never calls the code under test.
//
// REPLICATE BEFORE FLAGGING (the pre-registered two-stage rule, README). A row
// Holm-REJECTED at stage one triggers exactly ONE confirmatory replicate of the
// whole campaign on the pre-registered replicate seed block
// (kReplicateSeedSalt), judged at the SAME per-row threshold; the row is
// finally rejected only if BOTH stages reject, and otherwise reports
// RETAINED-AFTER-REPLICATE with both p-values. A row retained at stage one is
// final and is never re-examined. The rule applies uniformly to the family and
// to the controls.
//
// POWER IS ASSERTED, NOT ASSUMED (the T0.6 sampler-family precedent): four
// deliberately-wrong samplers run through the identical chi-square machinery
// and MUST be rejected -- in BOTH stages -- at the family's strictest Holm
// threshold. They are accounted separately (a mutant is a control, not a
// hypothesis about the engine) and the run fails if any of them survives.
//
// A CONFIRMED rejection in the real family is a stop-the-line divergence, never
// a tuning signal: no alpha, no sample size, no seed block, no pooling rule and
// no expectation is adjusted after seeing a result.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sts/dist_check/s2_expect.hpp"
#include "sts/dist_check/stats.hpp"
#include "sts/engine/boss_chest.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/public_view.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/relics.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/run_state.hpp"

namespace {
using namespace sts::engine;
using sts::dist_check::ChiSquareResult;
using sts::dist_check::s2::PoolRow;
using sts::registry::EncounterPool;

// --- Pre-registered constants (frozen before the first campaign run) ---------

constexpr int kMinimumSeeds = 10000;   // the B5.3 floor, unchanged
constexpr int kDefaultSeeds = 20000;   // the B5.3 acceptance scale, unchanged
constexpr double kFamilyAlpha = 0.01;  // the B5.3 family-wise alpha, unchanged
constexpr int kFamilySize = 13;        // pre-registered hypothesis count
// The event chain is two draws over a freshly built pool pair, so it is far
// cheaper per observation than a list generation; the multiplier is what keeps
// the conditioned cells (a specific shrine redrawn after the act crossing) at
// three-figure expected counts rather than double figures.
constexpr int kEventChainsPerSeed = 10;

// The two ascension bands `card_upgraded_chance` keys on (s2-design §2.5).
constexpr int kLowAscension = 11;
constexpr int kHighAscension = 20;

// The fixed event-sampling context. Every gate of build_event_pool /
// build_shrine_pool is pinned OPEN or CLOSED by these values, so both acts'
// filtered pool sizes are constants rather than per-seed random variables --
// which is what makes the two event hypotheses closed-form.
constexpr int32_t kEventGold = 500;      // >= 35 / 50 / 75: every gold gate open
constexpr int16_t kEventHp = 60;         // > 12: Knowing Skull open
constexpr int16_t kEventMaxHp = 80;      // hp/maxHp = 0.75 > 0.5: Moai closed
constexpr uint16_t kEventAct1Floor = 10; // > 6: Dead Adventurer / Mushrooms open
constexpr uint16_t kEventAct2Floor = 30; // row 12 >= 8: Colosseum open
constexpr uint8_t kEventAscension = 20;  // >= 15: Note For Yourself absent

// THE CONFIRMATORY REPLICATE'S SEED SALT, XORed into every sweep base to
// produce the second stage's seed block.
//
// FIXED AND DOCUMENTED BEFORE ANY REPLICATE WAS EVER RUN, and DERIVED rather
// than chosen: it is the ASCII bytes 'S' '2' '4' '4' in the high word, so each
// sweep's replicate block is that sweep's own block with a constant offset in
// bits 32-63 and its low 32 bits untouched. There is exactly one such constant
// for this task id, so there is nothing here that could have been shopped.
//
// Disjointness is by construction, not by inspection: every stage-one base is
// below 2^32 and every sweep is far shorter than the distance from its base to
// 2^32, so no `base + i` carries into bit 32. The two stages therefore cannot
// overlap, and two sweeps whose stage-one blocks are disjoint have disjoint
// replicate blocks as well.
constexpr int64_t kReplicateSeedSalt = INT64_C(0x5332343400000000);

using CellTable = std::pair<std::vector<uint64_t>, std::vector<double>>;

// One stage's worth of raw counts. Scoring happens in main, off these tables,
// so that stage one and the replicate go through byte-identical statistics.
struct Campaign {
    int seeds = kDefaultSeeds;
    int64_t salt = 0;  // 0 == stage one, kReplicateSeedSalt == the replicate
    std::map<std::string, CellTable> tables;         // name -> (observed, law)
    std::map<std::string, CellTable> mutant_tables;  // control name -> same
    std::vector<std::string> exact_failures;
};

void exact(Campaign& c, bool condition, std::string message) {
    if (!condition &&
        std::find(c.exact_failures.begin(), c.exact_failures.end(), message) ==
            c.exact_failures.end()) {
        c.exact_failures.push_back(std::move(message));
    }
}

// chi_square's own shape/sample diagnostics do not carry the hypothesis name,
// and a family this size is unreadable without it: a table built at the wrong
// width is a registration bug, not a divergence, and must say which row it is.
void check_shape(std::string_view name, const std::vector<uint64_t>& observed,
                 const std::vector<double>& probabilities) {
    if (observed.size() == probabilities.size() && !observed.empty()) return;
    std::cerr << "dist_check_s2: " << name << " has " << observed.size()
              << " observed cells against " << probabilities.size()
              << " expected\n";
    throw std::invalid_argument("dist_check_s2: hypothesis table shape");
}

void add_test(Campaign& c, std::string name,
              const std::vector<uint64_t>& observed,
              const std::vector<double>& probabilities) {
    check_shape(name, observed, probabilities);
    c.tables.emplace(std::move(name), CellTable{observed, probabilities});
}

void add_mutant(Campaign& c, std::string name,
                const std::vector<uint64_t>& observed,
                const std::vector<double>& probabilities) {
    check_shape(name, observed, probabilities);
    c.mutant_tables.emplace(std::move(name), CellTable{observed, probabilities});
}

int index_of_key(std::span<const PoolRow> rows, std::string_view key) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].key == key) return static_cast<int>(i);
    }
    return -1;
}

// --- H1..H6 + H7/H8: the act-2/3 pool draw ----------------------------------

// One act's list-generation observations. Everything below reads the SAME
// generate_monster_lists call per seed, so the weak/strong/elite/boss cells of
// one act are drawn from one stream position sequence -- which is the dependence
// Holm is chosen to tolerate (README).
struct ActPoolCounts {
    std::vector<uint64_t> weak_pair;
    std::vector<uint64_t> first_strong;
    std::vector<uint64_t> elite_pair;
    std::vector<uint64_t> boss_pair;
    std::vector<uint64_t> mutant_first_strong;  // exclusion-blind control
};

void act_pool_sweep(Campaign& c, int act, ActPoolCounts& out) {
    const std::vector<PoolRow> weak =
        sts::dist_check::s2::pool_rows(act, EncounterPool::WEAK);
    const std::vector<PoolRow> strong =
        sts::dist_check::s2::pool_rows(act, EncounterPool::STRONG);
    const std::vector<PoolRow> elite =
        sts::dist_check::s2::pool_rows(act, EncounterPool::ELITE);
    const std::vector<PoolRow> boss =
        sts::dist_check::s2::pool_rows(act, EncounterPool::BOSS);
    const std::size_t nw = weak.size();
    const std::size_t ns = strong.size();
    const std::size_t ne = elite.size();
    const std::size_t nb = boss.size();

    out.weak_pair.assign(nw * nw, 0);
    out.first_strong.assign(nw * ns, 0);
    out.elite_pair.assign(ne * ne, 0);
    out.boss_pair.assign(nb * nb, 0);
    out.mutant_first_strong.assign(nw * ns, 0);

    // Acts 2 and 3 draw exactly two weak entries, so index 1 IS the last weak
    // entry generateExclusions keys on (weak_segment_for_act).
    exact(c, weak_segment_for_act(act) == 2,
          "act 2/3 weak segment is not two entries");
    exact(c, nb == 3u, "act boss pool is not three rows");

    // Streams are disjoint per act so the two acts' sweeps cannot alias.
    // Stage-one bases; the replicate stage is the same block XOR the
    // pre-registered salt (see kReplicateSeedSalt).
    const int64_t base = (act == 2 ? INT64_C(0x2000001) : INT64_C(0x3000001)) ^
                         c.salt;
    for (int i = 0; i < c.seeds; ++i) {
        const int64_t seed = base + static_cast<int64_t>(i);
        RngStream rng = from_seed(seed);
        MonsterLists lists{};
        generate_monster_lists(act, rng, lists);

        exact(c, lists.monster_list_count == monster_list_len_for_act(act),
              "generated monster list length disagreed with the act's segments");
        exact(c, lists.elite_list_count == kEliteSegment,
              "generated elite list length disagreed with generateElites(10)");
        exact(c, lists.boss_list_count == kMaxBossList,
              "generated boss list did not contain three keys");

        const int w0 = index_of_key(weak, lists.monster_list[0]);
        const int w1 = index_of_key(weak, lists.monster_list[1]);
        const int s0 = index_of_key(strong, lists.monster_list[2]);
        const int e0 = index_of_key(elite, lists.elite_list[0]);
        const int e1 = index_of_key(elite, lists.elite_list[1]);
        const int b0 = index_of_key(boss, lists.boss_list[0]);
        const int b1 = index_of_key(boss, lists.boss_list[1]);
        exact(c, w0 >= 0 && w1 >= 0 && s0 >= 0 && e0 >= 0 && e1 >= 0 &&
                     b0 >= 0 && b1 >= 0,
              "generated encounter key fell outside its act's registry pool");
        if (w0 < 0 || w1 < 0 || s0 < 0 || e0 < 0 || e1 < 0 || b0 < 0 ||
            b1 < 0) {
            continue;
        }
        ++out.weak_pair[static_cast<std::size_t>(w0) * nw +
                        static_cast<std::size_t>(w1)];
        ++out.first_strong[static_cast<std::size_t>(w1) * ns +
                           static_cast<std::size_t>(s0)];
        ++out.elite_pair[static_cast<std::size_t>(e0) * ne +
                         static_cast<std::size_t>(e1)];
        ++out.boss_pair[static_cast<std::size_t>(b0) * nb +
                        static_cast<std::size_t>(b1)];

        // MUTANT M1 (act 2 only, registered below): the same last-weak entry,
        // but the first strong is drawn WITHOUT populateFirstStrongEnemy's
        // exclusion-rejection loop -- one unconditioned roll off a private
        // stream. Its support therefore covers the excluded pairs the real law
        // forbids.
        if (act == 2) {
            RngStream mutant_rng = from_seed(seed ^ INT64_C(0x5EEDBEEF));
            double total = 0.0;
            for (const PoolRow& row : strong) total += row.weight;
            const double roll =
                static_cast<double>(random(mutant_rng, 1.0f)) * total;
            double cursor = 0.0;
            std::size_t pick = ns - 1;
            for (std::size_t k = 0; k < ns; ++k) {
                cursor += strong[k].weight;
                if (roll < cursor) {
                    pick = k;
                    break;
                }
            }
            ++out.mutant_first_strong[static_cast<std::size_t>(w1) * ns + pick];
        }
    }
}

// The uniform ordered-pair law over `n` distinct shuffled entries: the diagonal
// is impossible, every off-diagonal cell is 1/(n*(n-1)). Collections.shuffle of
// the act's three-key bossList makes all 3! orders equally likely
// (Exordium.java:206 / TheCity.java:180 / TheBeyond.java:174), so the head pair
// is a uniform ordered pair.
std::vector<double> ordered_pair_law(std::size_t n) {
    std::vector<double> out(n * n, 0.0);
    const double p = 1.0 / (static_cast<double>(n) * static_cast<double>(n - 1));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i != j) out[i * n + j] = p;
        }
    }
    return out;
}

// --- H8: the A20 double boss, read off the PUBLIC surface -------------------
//
// s2-design §4.4: goToDoubleBoss fights bossList[0] and then bossList[1] of the
// SAME Act-3 shuffle -- not a re-draw -- and MonsterRoomBoss.onPlayerEntry's pop
// is what makes bossList[1] public once the player is standing in the second
// room. The engine's rendering of that is encode_public_view's
// `second_boss_reserved` beside `boss_prefix[0]` (public_view.cpp:405-422),
// gated on `act == kActBeyond && boss_cursor >= 1` (it read `kFinalAct` until
// S3.32 moved that constant to 4; the gate has always meant Act 3, the only act
// with a double boss). Sampling the PAIR off that
// surface tests the shuffle and the conditioning together; a second boss that
// repeated the first would land on the impossible diagonal.
void double_boss_sweep(Campaign& c, std::vector<uint64_t>& act3_pair,
                       std::vector<uint64_t>& mutant_pair) {
    const std::vector<PoolRow> boss3 =
        sts::dist_check::s2::pool_rows(3, EncounterPool::BOSS);
    const std::size_t nb = boss3.size();
    act3_pair.assign(nb * nb, 0);
    mutant_pair.assign(nb * nb, 0);

    // Registry ids for the act's boss rows: what encode_public_view emits.
    std::vector<uint8_t> ids(nb, 0);
    for (const auto& e : sts::registry::kEncounters) {
        if (e.act == 3 && e.pool == EncounterPool::BOSS) {
            const int idx = index_of_key(boss3, e.game_id);
            if (idx >= 0) ids[static_cast<std::size_t>(idx)] = e.id;
        }
    }
    const auto index_of_id = [&](uint16_t id) {
        for (std::size_t i = 0; i < nb; ++i) {
            if (ids[i] == id) return static_cast<int>(i);
        }
        return -1;
    };

    // RunController and PublicView are large PODs; one heap instance each,
    // reset per iteration, keeps the sweep allocation-free.
    auto rc = std::make_unique<RunController>();
    auto view = std::make_unique<PublicView>();

    for (int i = 0; i < c.seeds; ++i) {
        const int64_t seed =
            (INT64_C(0x3B0551) ^ c.salt) + static_cast<int64_t>(i);
        RngStream rng = from_seed(seed);
        MonsterLists lists{};
        generate_monster_lists(3, rng, lists);

        *rc = RunController{};
        rc->phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
        rc->run.act = 3;
        rc->run.ascension = 20;
        rc->lists = lists;
        // boss_cursor advances when a boss room is LEFT, so 1 in the final act
        // IS "inside (or past) the second boss room" -- the only state an A20
        // run can reach with the run still going.
        rc->boss_cursor = 1;
        encode_public_view(*rc, *view);

        const int first = index_of_id(view->boss_prefix[0]);
        const int second = index_of_id(view->second_boss_reserved);
        exact(c, first >= 0 && second >= 0,
              "public view reported an act-3 boss id outside the registry pool");
        if (first < 0 || second < 0) continue;
        ++act3_pair[static_cast<std::size_t>(first) * nb +
                    static_cast<std::size_t>(second)];

        // MUTANT M4: the second boss REPEATS the first (a re-draw of
        // bossList[0] rather than the reserved bossList[1]).
        ++mutant_pair[static_cast<std::size_t>(first) * nb +
                      static_cast<std::size_t>(first)];

        // The Act-2 negative (s2-design §4.4: no double-boss branch exists
        // outside TheBeyond). Same controller, act 2 -- the reserved slot must
        // stay 0, which is an EXACT assertion, not a frequency one.
        *rc = RunController{};
        rc->phase = static_cast<uint8_t>(RunPhase::MAP_CHOICE);
        rc->run.act = 2;
        rc->run.ascension = 20;
        rc->lists = lists;
        rc->boss_cursor = 1;
        encode_public_view(*rc, *view);
        exact(c, view->second_boss_reserved == 0,
              "public view reserved a second boss outside the final act");
    }
}

// --- H9/H10: per-act cardUpgradedChance -------------------------------------
//
// s2-design §2.5 / AbstractDungeon.java:1469-1477. The randomBoolean draw is
// REAL for every non-RARE reward card in EVERY act -- Act 1's 0.0f chance makes
// the branch never fire without making the draw disappear -- so the number of
// non-RARE offers, and the cardRng advance, are identical across acts and
// ascensions for one seed. That is what makes the two ascension bands exactly
// equal-sized samples and the 4-cell law below well-formed rather than a table
// with random margins.
RewardCardRarity rarity_of(CardId id, bool& known) {
    known = true;
    for (const CardId candidate : sts::registry::kIroncladCommonPool) {
        if (candidate == id) return RewardCardRarity::COMMON;
    }
    for (const CardId candidate : sts::registry::kIroncladUncommonPool) {
        if (candidate == id) return RewardCardRarity::UNCOMMON;
    }
    for (const CardId candidate : sts::registry::kIroncladRarePool) {
        if (candidate == id) return RewardCardRarity::RARE;
    }
    known = false;
    return RewardCardRarity::COMMON;
}

// One (act, ascension) reward assembly for one seed: the non-RARE offer count,
// how many of them came back upgraded, and the cardRng advance it cost.
struct UpgradeSample {
    int non_rare = 0;
    int upgraded = 0;
    int32_t card_rng_advance = 0;
};

UpgradeSample one_reward(Campaign& c, int64_t seed, int act, int ascension) {
    RunState rs{};
    rs.act = static_cast<uint8_t>(act);
    rs.ascension = static_cast<uint8_t>(ascension);
    rs.card_rng = from_seed(seed);
    rs.potion_rng = from_seed(seed + 0x100000);
    rs.treasure_rng = from_seed(seed + 0x200000);
    rs.card_blizz_randomizer = static_cast<int16_t>(kCardBlizzStartOffset);
    rs.blizzard_potion_mod = 0;
    RngStream misc = from_seed(seed + 1);
    const int32_t before = rs.card_rng.counter;

    RewardScreen rewards{};
    assemble_combat_rewards(rs, misc, RoomType::Monster, RewardOutcome::KILLED,
                            rewards);
    UpgradeSample out;
    out.card_rng_advance = rs.card_rng.counter - before;
    for (int j = 0; j < rewards.count; ++j) {
        if (static_cast<RewardItemKind>(rewards.items[j].kind) !=
            RewardItemKind::CARDS) {
            continue;
        }
        const RunRewardItem& item = rewards.items[j];
        for (int k = 0; k < item.card_count; ++k) {
            bool known = false;
            const RewardCardRarity rarity =
                rarity_of(static_cast<CardId>(item.card_ids[k]), known);
            exact(c, known,
                  "reward card identity not in a cited red rarity pool");
            if (rarity == RewardCardRarity::RARE) {
                exact(c, item.card_upgrades[k] == 0,
                      "a RARE reward card came back upgraded");
                continue;
            }
            ++out.non_rare;
            if (item.card_upgrades[k] != 0) ++out.upgraded;
        }
    }
    return out;
}

// Cells: [low-band not upgraded, low-band upgraded, high-band not upgraded,
// high-band upgraded]. Each band contributes exactly half the observations by
// construction (see above), so the law is 0.5 * the band's Bernoulli split.
std::vector<double> upgrade_law(int act) {
    const double low =
        static_cast<double>(card_upgraded_chance(act, kLowAscension));
    const double high =
        static_cast<double>(card_upgraded_chance(act, kHighAscension));
    return {0.5 * (1.0 - low), 0.5 * low, 0.5 * (1.0 - high), 0.5 * high};
}

void upgrade_sweep(Campaign& c, std::vector<uint64_t>& act2,
                   std::vector<uint64_t>& act3) {
    act2.assign(4, 0);
    act3.assign(4, 0);
    for (int i = 0; i < c.seeds; ++i) {
        const int64_t seed =
            (INT64_C(0x0C4D0001) ^ c.salt) + static_cast<int64_t>(i);
        const UpgradeSample a1 = one_reward(c, seed, 1, kHighAscension);
        const UpgradeSample a2lo = one_reward(c, seed, 2, kLowAscension);
        const UpgradeSample a2hi = one_reward(c, seed, 2, kHighAscension);
        const UpgradeSample a3lo = one_reward(c, seed, 3, kLowAscension);
        const UpgradeSample a3hi = one_reward(c, seed, 3, kHighAscension);

        // The §2.5 claim, as an EXACT check rather than a frequency one: Act 1's
        // 0.0f chance never fires, and the draw is consumed anyway, so the
        // cardRng advance and the non-RARE count are identical everywhere.
        exact(c, a1.upgraded == 0,
              "an Act-1 reward card was upgraded at cardUpgradedChance 0.0");
        exact(c, a1.non_rare == a2lo.non_rare &&
                     a1.non_rare == a2hi.non_rare &&
                     a1.non_rare == a3lo.non_rare && a1.non_rare == a3hi.non_rare,
              "the non-RARE offer count changed with act/ascension");
        exact(c, a1.card_rng_advance == a2lo.card_rng_advance &&
                     a1.card_rng_advance == a2hi.card_rng_advance &&
                     a1.card_rng_advance == a3lo.card_rng_advance &&
                     a1.card_rng_advance == a3hi.card_rng_advance,
              "the cardRng advance changed with act/ascension (the upgrade "
              "randomBoolean must be consumed in every act)");

        act2[0] += static_cast<uint64_t>(a2lo.non_rare - a2lo.upgraded);
        act2[1] += static_cast<uint64_t>(a2lo.upgraded);
        act2[2] += static_cast<uint64_t>(a2hi.non_rare - a2hi.upgraded);
        act2[3] += static_cast<uint64_t>(a2hi.upgraded);
        act3[0] += static_cast<uint64_t>(a3lo.non_rare - a3lo.upgraded);
        act3[1] += static_cast<uint64_t>(a3lo.upgraded);
        act3[2] += static_cast<uint64_t>(a3hi.non_rare - a3hi.upgraded);
        act3[3] += static_cast<uint64_t>(a3hi.upgraded);
    }
}

// --- H11/H12: the one-time pool's cross-act asymmetry ------------------------
//
// event_framework.hpp's ACT-CROSSING ASYMMETRY block (AbstractDungeon
// .dungeonTransitionSetup :2576-2577 + the new dungeon's constructor):
//   eventList   cleared and rebuilt with the NEW act's ids;
//   shrineList  cleared and rebuilt -- ALL SIX SHRINES RETURN;
//   specialOneTimeEventList carried BY IDENTITY -- an Act-1 draw stays gone.
// The two hypotheses are the two halves of that, sampled through the same two
// generate_event calls.

enum class DrawClass { EVENT, SHRINE, SPECIAL, NONE };

DrawClass classify(uint16_t id) {
    if (id == 0) return DrawClass::NONE;
    if (id >= kShrineListFirstId &&
        id < kShrineListFirstId + kShrineListCount) {
        return DrawClass::SHRINE;
    }
    if (id >= kSpecialListFirstId &&
        id < kSpecialListFirstId + kSpecialListCount) {
        return DrawClass::SPECIAL;
    }
    return DrawClass::EVENT;
}

void seat_event_context(RunState& rs, int act) {
    rs.act = static_cast<uint8_t>(act);
    rs.ascension = kEventAscension;
    rs.floor = act == 1 ? kEventAct1Floor : kEventAct2Floor;
    rs.gold = kEventGold;
    rs.hp = kEventHp;
    rs.max_hp = kEventMaxHp;
    rs.relic_count = 2;  // N'loth's `relics.size() >= 2` gate, open
}

struct EventPoolSizes {
    int act1_shrine = 0;              // six shrines + six act-1 specials
    int act2_event = 0;               // the full thirteen-row TheCity list
    int act2_shrine_after_shrine = 0; // shrines restored, specials intact
    int act2_shrine_after_special = 0;// shrines restored, one special gone
};

void event_sweep(Campaign& c, std::vector<uint64_t>& shrine_return,
                 std::vector<uint64_t>& special_depletion,
                 std::vector<uint64_t>& mutant_depletion,
                 EventPoolSizes& sizes) {
    shrine_return.assign(3, 0);
    special_depletion.assign(3, 0);
    mutant_depletion.assign(3, 0);
    sizes = EventPoolSizes{};

    uint16_t tmp[kShrineListCount + kSpecialListCount];
    const int64_t chains =
        static_cast<int64_t>(c.seeds) * kEventChainsPerSeed;
    for (int64_t i = 0; i < chains; ++i) {
        const int64_t seed = (INT64_C(0xE7E0001) ^ c.salt) + i;
        RunState rs{};
        seat_event_context(rs, 1);
        rs.event_rng = from_seed(seed);
        init_event_pools(rs);
        if (i == 0) {
            sizes.act1_shrine = build_shrine_pool(
                rs, tmp, kShrineListCount + kSpecialListCount);
        }
        const uint16_t first = generate_event(rs);
        const DrawClass first_class = classify(first);
        exact(c, first_class != DrawClass::NONE,
              "the act-1 event pools were empty in the pinned context");
        if (first_class == DrawClass::NONE) continue;

        // ONE ?-ROOM TICK BETWEEN THE TWO DRAWS. generate_event reads a
        // THROWAWAY copy of rs.event_rng and leaves the stream byte-identical
        // (event_framework.cpp:525-531) -- the run layer's own +1 counter comes
        // from EventHelper.roll in nextRoomTransition (AbstractDungeon.java
        // :1766), not from generateEvent. Without that tick the second draw
        // replays the FIRST draw's split roll verbatim, so a chain conditioned
        // on an act-1 shrine could only ever take the shrine branch again. That
        // is what the first run of this family reported as a 3.8e4 chi-square
        // on both event rows; it was the harness, not the engine.
        (void)random(rs.event_rng);

        // The act crossing: rs.act advances FIRST, then the event/shrine halves
        // are rebuilt and the special half is deliberately untouched.
        RunState mutant = rs;
        seat_event_context(rs, 2);
        reinit_act_event_pools(rs);
        if (i == 0) {
            sizes.act2_event = build_event_pool(rs, tmp, kEventListMaxCount);
        }
        if (sizes.act2_shrine_after_special == 0 &&
            first_class == DrawClass::SPECIAL) {
            sizes.act2_shrine_after_special = build_shrine_pool(
                rs, tmp, kShrineListCount + kSpecialListCount);
        }
        if (sizes.act2_shrine_after_shrine == 0 &&
            first_class == DrawClass::SHRINE) {
            sizes.act2_shrine_after_shrine = build_shrine_pool(
                rs, tmp, kShrineListCount + kSpecialListCount);
        }
        const uint16_t second = generate_event(rs);
        const DrawClass second_class = classify(second);
        exact(c, second_class != DrawClass::NONE,
              "the act-2 event pools were empty in the pinned context");
        if (second_class == DrawClass::NONE) continue;

        const std::size_t cell =
            second == first ? 0u
                            : (second_class == DrawClass::EVENT ? 2u : 1u);
        if (first_class == DrawClass::SHRINE) {
            ++shrine_return[cell];
        } else if (first_class == DrawClass::SPECIAL) {
            ++special_depletion[cell];

            // MUTANT M2: the act crossing rebuilds the SPECIAL half too --
            // init_event_pools instead of reinit_act_event_pools, i.e. the
            // "one-time pool resets per act" reading the header exists to
            // refute. Same stream, same context, same classification.
            seat_event_context(mutant, 2);
            init_event_pools(mutant);
            const uint16_t mutant_second = generate_event(mutant);
            const DrawClass mutant_class = classify(mutant_second);
            if (mutant_class != DrawClass::NONE) {
                ++mutant_depletion[
                    mutant_second == first
                        ? 0u
                        : (mutant_class == DrawClass::EVENT ? 2u : 1u)];
            }
        }
    }
}

// Cells: [redrew the same id, drew another shrine-pool member, drew an act-2
// event]. `shrine_pool` is the act-2 shrine-branch pool size in the pinned
// context. The split is generateEvent's `rng.random(1.0f) < shrineChance`
// (AbstractDungeon.java:1866) and the index draw is uniform over the filtered
// list (:1937 / :1986).
std::vector<double> crossing_law(int shrine_pool, bool same_id_reachable) {
    const double q = static_cast<double>(kShrineChance);
    const double n = static_cast<double>(shrine_pool);
    if (!same_id_reachable) return {0.0, q, 1.0 - q};
    return {q / n, q * (n - 1.0) / n, 1.0 - q};
}

// --- H13: the canSpawn gate's pool cursor ------------------------------------
//
// boss_chest.hpp: BossChest's constructor pops three BOSS-tier relics at ROOM
// ENTRY, and for BOSS tier BOTH returnRandomRelicKey (:792-798) and the
// canSpawn-rejection reroute returnEndRandomRelicKey (:739-745) are `remove(0)`
// -- so a rejection costs the pool an entry PERMANENTLY and yields no draw. At
// Act 2 with Burning Blood held, exactly two of the 22 boss rows are gated
// (Ectoplasm's `actNum <= 1`, Black Blood's `hasRelic("Burning Blood")`), so
// the chest's pool consumption is a front scan over a shuffled pool -- three
// spawnable keys plus however many blocked ones stood in front of them.
// The two act-2 boss-chest strata, which differ ONLY in how many rows the gate
// closes -- and which together cover both act-2 canSpawn bodies:
//   * `keeps_burning_blood`: the fresh Ironclad. Ectoplasm is gated (its
//     `actNum <= 1`, s2-design §5 trap 9); Black Blood is NOT, because its gate
//     is `hasRelic("Burning Blood")` -- holding the starter is what LETS it
//     spawn. One blocked row.
//   * the Neow boss-swap line, which traded Burning Blood away at floor 0: both
//     Ectoplasm and Black Blood are gated. Two blocked rows.
// Disjoint seed ranges keep the two strata independent, and each contributes
// exactly `seeds` observations, so the stratified law below is a plain 0.5/0.5
// mixture rather than a table with random margins.
struct CanSpawnStratum {
    bool keeps_burning_blood = true;
    int64_t seed_base = 0;
    int blocked = 0;
};

void can_spawn_stratum(Campaign& c, const CanSpawnStratum& stratum,
                       std::vector<uint64_t>& consumed, int& pool_size,
                       int& blocked_count) {
    const int boss_pool = static_cast<int>(RelicPool::BOSS);
    for (int i = 0; i < c.seeds; ++i) {
        RunState rs{};
        rs.act = 2;
        rs.floor = 33;
        rs.relic_rng = from_seed((stratum.seed_base ^ c.salt) +
                                 static_cast<int64_t>(i));
        if (stratum.keeps_burning_blood) {
            rs.relics[0].relic_id =
                static_cast<uint16_t>(RelicId::BURNING_BLOOD);
            rs.relic_count = 1;
        }
        initialize_relic_pools(rs);

        const int before = rs.relic_pool_count[boss_pool];
        if (i == 0) {
            pool_size = before;
            blocked_count = 0;
            RelicSpawnContext ctx{};
            fill_boss_spawn_gates(rs, ctx);
            for (int k = 0; k < before; ++k) {
                const auto id =
                    static_cast<RelicId>(rs.relic_pools[boss_pool][k]);
                if (!relic_can_spawn(id, ctx)) ++blocked_count;
            }
        }
        exact(c, rs.relic_pool_count[boss_pool] == pool_size,
              "the boss relic pool size varied between seeds");

        const BossChestState chest = roll_boss_chest(rs);
        const int spent = before - rs.relic_pool_count[boss_pool];
        exact(c, spent >= kBossChestOfferCount,
              "the boss chest consumed fewer pool entries than it offered");
        for (int k = 0; k < kBossChestOfferCount; ++k) {
            exact(c,
                  chest.relics[k] != static_cast<uint16_t>(RelicId::ECTOPLASM),
                  "Ectoplasm was offered by an act-2 boss chest");
            exact(c, stratum.keeps_burning_blood ||
                         chest.relics[k] !=
                             static_cast<uint16_t>(RelicId::BLACK_BLOOD),
                  "Black Blood was offered without Burning Blood held");
        }
        const std::size_t k = static_cast<std::size_t>(
            std::clamp(spent - kBossChestOfferCount, 0, stratum.blocked));
        ++consumed[k];
    }
}

// Cells: the `blocked+1` consumption outcomes of the Burning-Blood stratum,
// then those of the swapped stratum. Each stratum contributes exactly half the
// observations, so the law is 0.5 * its front-scan law.
void can_spawn_sweep(Campaign& c, std::vector<uint64_t>& consumed,
                     std::vector<uint64_t>& mutant_consumed,
                     std::vector<double>& law, int& pool_size) {
    // The two bases must be further apart than `seeds`, or the strata share
    // pool shuffles and the "independent" in the comment above is a lie: the
    // first pair tried here were 12,079 apart, so at the 20,000-seed acceptance
    // scale 40 % of the swapped stratum replayed the held stratum's shuffle.
    const CanSpawnStratum held{true, INT64_C(0xB055C0DE), 1};
    const CanSpawnStratum swapped{false, INT64_C(0x5A11C0DE), 2};
    consumed.clear();
    mutant_consumed.clear();
    law.clear();
    pool_size = 0;

    for (const CanSpawnStratum& stratum : {held, swapped}) {
        std::vector<uint64_t> counts(
            static_cast<std::size_t>(stratum.blocked) + 1, 0);
        int size = 0;
        int blocked = 0;
        can_spawn_stratum(c, stratum, counts, size, blocked);
        exact(c, size == 22,
              "the boss relic pool was not the 22-row registry pool");
        exact(c, blocked == stratum.blocked,
              "the act-2 boss pool did not gate the expected number of rows");
        pool_size = size;
        const std::vector<double> stratum_law =
            sts::dist_check::s2::front_scan_blocked_law(
                size - stratum.blocked, stratum.blocked, kBossChestOfferCount);
        // MUTANT M3: the canSpawn rejection PUTS THE RELIC BACK, so the pool
        // never pays for a gated row and the consumption is always exactly the
        // three offers -- every observation lands in the stratum's k == 0 cell.
        const uint64_t stratum_total =
            std::accumulate(counts.begin(), counts.end(), uint64_t{0});
        for (std::size_t k = 0; k < counts.size(); ++k) {
            consumed.push_back(counts[k]);
            law.push_back(0.5 * stratum_law[k]);
            mutant_consumed.push_back(k == 0 ? stratum_total : 0);
        }
    }
}

// --- Driver ------------------------------------------------------------------

void print_usage() {
    std::cerr << "usage: dist_check_s2 [--seeds N]\n";
}

bool parse_args(int argc, char** argv, Campaign& campaign) {
    bool saw_seeds = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--seeds" && i + 1 < argc) {
            if (saw_seeds) {
                std::cerr << "dist_check_s2: repeated --seeds\n";
                return false;
            }
            saw_seeds = true;
            const std::string_view value(argv[++i]);
            int parsed = 0;
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} ||
                end != value.data() + value.size()) {
                std::cerr << "dist_check_s2: invalid --seeds value\n";
                return false;
            }
            campaign.seeds = parsed;
        } else {
            print_usage();
            return false;
        }
    }
    if (campaign.seeds < kMinimumSeeds) {
        std::cerr << "dist_check_s2: --seeds must be >= " << kMinimumSeeds
                  << " (tier-4 frozen scope)\n";
        return false;
    }
    return true;
}

// One STAGE of the campaign: every sweep, into raw count tables. Scoring is
// deliberately NOT done here -- stage one and the confirmatory replicate differ
// only in `c.salt`, and must go through byte-identical statistics afterwards.
void run_campaign(Campaign& campaign) {
    ActPoolCounts act2{};
    ActPoolCounts act3{};
    act_pool_sweep(campaign, 2, act2);
    act_pool_sweep(campaign, 3, act3);

    const std::vector<PoolRow> weak2 =
        sts::dist_check::s2::pool_rows(2, EncounterPool::WEAK);
    const std::vector<PoolRow> weak3 =
        sts::dist_check::s2::pool_rows(3, EncounterPool::WEAK);
    const std::vector<PoolRow> elite2 =
        sts::dist_check::s2::pool_rows(2, EncounterPool::ELITE);
    const std::vector<PoolRow> elite3 =
        sts::dist_check::s2::pool_rows(3, EncounterPool::ELITE);

    add_test(campaign, "s2.encounter.act2_weak_pair", act2.weak_pair,
             sts::dist_check::s2::consecutive_pair_law(weak2));
    add_test(campaign, "s2.encounter.act2_first_strong_given_last_weak",
             act2.first_strong, sts::dist_check::s2::first_strong_joint_law(2));
    add_test(campaign, "s2.encounter.act2_elite_pair", act2.elite_pair,
             sts::dist_check::s2::consecutive_pair_law(elite2));
    add_test(campaign, "s2.encounter.act3_weak_pair", act3.weak_pair,
             sts::dist_check::s2::consecutive_pair_law(weak3));
    add_test(campaign, "s2.encounter.act3_first_strong_given_last_weak",
             act3.first_strong, sts::dist_check::s2::first_strong_joint_law(3));
    add_test(campaign, "s2.encounter.act3_elite_pair", act3.elite_pair,
             sts::dist_check::s2::consecutive_pair_law(elite3));
    add_test(campaign, "s2.boss.act2_shuffle_pair", act2.boss_pair,
             ordered_pair_law(std::size_t{3}));

    std::vector<uint64_t> act3_pair;
    std::vector<uint64_t> mutant_boss_pair;
    double_boss_sweep(campaign, act3_pair, mutant_boss_pair);
    add_test(campaign, "s2.boss.act3_double_boss_public_pair", act3_pair,
             ordered_pair_law(std::size_t{3}));

    std::vector<uint64_t> upgrade2;
    std::vector<uint64_t> upgrade3;
    upgrade_sweep(campaign, upgrade2, upgrade3);
    add_test(campaign, "s2.reward.card_upgraded_act2", upgrade2,
             upgrade_law(2));
    add_test(campaign, "s2.reward.card_upgraded_act3", upgrade3,
             upgrade_law(3));

    std::vector<uint64_t> shrine_return;
    std::vector<uint64_t> special_depletion;
    std::vector<uint64_t> mutant_depletion;
    EventPoolSizes pools{};
    event_sweep(campaign, shrine_return, special_depletion, mutant_depletion,
                pools);
    // The pinned context's pool sizes are EXACT expectations, derived from the
    // per-key act gates of getShrine (:1886-1936) and getEvent (:1946-1982) and
    // asserted rather than measured: six shrines + six act-1-eligible specials;
    // the full thirteen-row act-2 event list; six shrines + eleven
    // act-2-eligible specials, less the one an act-1 SPECIAL draw consumed.
    exact(campaign, pools.act1_shrine == 12,
          "the pinned act-1 shrine-branch pool was not six shrines + six "
          "specials");
    exact(campaign, pools.act2_event == 13,
          "the pinned act-2 event pool was not the full thirteen-row list");
    exact(campaign, pools.act2_shrine_after_shrine == 17,
          "the pinned act-2 shrine-branch pool after a shrine draw was not six "
          "restored shrines + eleven specials");
    exact(campaign, pools.act2_shrine_after_special == 16,
          "the pinned act-2 shrine-branch pool after a special draw was not "
          "six shrines + ten surviving specials");
    add_test(campaign, "s2.event.shrine_returns_after_act_crossing",
             shrine_return, crossing_law(17, true));
    add_test(campaign, "s2.event.special_one_time_depletes_run_wide",
             special_depletion, crossing_law(16, false));

    std::vector<uint64_t> consumed;
    std::vector<uint64_t> mutant_consumed;
    std::vector<double> front_scan;
    int boss_pool_size = 0;
    can_spawn_sweep(campaign, consumed, mutant_consumed, front_scan,
                    boss_pool_size);
    add_test(campaign, "s2.relic.boss_chest_can_spawn_front_scan", consumed,
             front_scan);

    // The negative controls, scored against the SAME expectations through the
    // SAME chi-square, and required to be rejected at the family's strictest
    // Holm threshold.
    add_mutant(campaign, "mutant.first_strong_ignores_exclusions",
               act2.mutant_first_strong,
               sts::dist_check::s2::first_strong_joint_law(2));
    add_mutant(campaign, "mutant.double_boss_repeats_first_boss",
               mutant_boss_pair, ordered_pair_law(std::size_t{3}));
    add_mutant(campaign, "mutant.special_one_time_returns_next_act",
               mutant_depletion, crossing_law(16, false));
    add_mutant(campaign, "mutant.can_spawn_rejection_returns_relic",
               mutant_consumed, front_scan);
}

}  // namespace


int main(int argc, char** argv) {
    // --- Stage one: the registered seed blocks ------------------------------
    Campaign stage_one;
    if (!parse_args(argc, argv, stage_one)) return 2;
    stage_one.salt = 0;
    run_campaign(stage_one);

    const auto score = [](const std::map<std::string, CellTable>& tables) {
        std::vector<ChiSquareResult> out;
        out.reserve(tables.size());
        for (const auto& [name, cells] : tables) {
            out.push_back(sts::dist_check::chi_square(name, cells.first,
                                                      cells.second));
        }
        return out;
    };
    const std::vector<ChiSquareResult> tests = score(stage_one.tables);
    const std::vector<ChiSquareResult> mutants = score(stage_one.mutant_tables);

    bool flagged = false;
    std::cout << "dist_check_s2 seeds=" << stage_one.seeds
              << " event_chains=" << stage_one.seeds * kEventChainsPerSeed
              << " stochastic_hypotheses=" << tests.size()
              << " family_alpha=" << kFamilyAlpha
              << " correction=Holm-Bonferroni+replicate\n";
    if (tests.size() != static_cast<std::size_t>(kFamilySize)) {
        std::cout << "FLAG registration: the family is pre-registered at "
                  << kFamilySize << " hypotheses\n";
        flagged = true;
    }

    const auto stage_one_holm =
        sts::dist_check::holm_bonferroni(tests, kFamilyAlpha);
    // The controls are not part of the Holm family (they say nothing about the
    // engine); each is judged against the family's STRICTEST threshold, then
    // put through the same two-stage rule so the rule's power claim is tested
    // under the rule itself.
    const double strictest =
        kFamilyAlpha / static_cast<double>(tests.size());
    std::vector<sts::dist_check::HolmDecision> mutant_stage_one;
    mutant_stage_one.reserve(mutants.size());
    for (const ChiSquareResult& mutant : mutants) {
        mutant_stage_one.push_back(sts::dist_check::HolmDecision{
            mutant.name, mutant.p_value, strictest,
            mutant.p_value <= strictest});
    }

    // --- The confirmatory replicate ----------------------------------------
    //
    // Run ONLY when something rejected at stage one. In practice the four
    // controls reject by construction, so it runs on every campaign; the point
    // of the guard is the contract, which confirm_by_replicate enforces per
    // row: a row RETAINED at stage one is final and is never re-examined.
    const bool needs_replicate =
        std::any_of(stage_one_holm.begin(), stage_one_holm.end(),
                    [](const auto& d) { return d.rejected; }) ||
        std::any_of(mutant_stage_one.begin(), mutant_stage_one.end(),
                    [](const auto& d) { return d.rejected; });

    Campaign replicate;
    replicate.seeds = stage_one.seeds;
    replicate.salt = kReplicateSeedSalt;
    if (needs_replicate) {
        run_campaign(replicate);
        std::cout << "replicate stage ran: seed blocks are the stage-one blocks"
                  << " XOR the pre-registered salt\n";
    }

    // Captured by POINTER, by value: a lambda returning a lambda that captured
    // its own reference parameter by reference would dangle the moment the
    // outer call returned.
    const auto replicate_p = [](const std::map<std::string, CellTable>* from) {
        return [from](const std::string& name) {
            const auto cells = from->find(name);
            if (cells == from->end()) {
                throw std::invalid_argument(
                    "dist_check_s2: no replicate table for " + name);
            }
            return sts::dist_check::chi_square(name, cells->second.first,
                                               cells->second.second)
                .p_value;
        };
    };
    const auto family_final = sts::dist_check::confirm_by_replicate(
        stage_one_holm, replicate_p(&replicate.tables));
    const auto mutant_final = sts::dist_check::confirm_by_replicate(
        mutant_stage_one, replicate_p(&replicate.mutant_tables));

    // --- Report -------------------------------------------------------------
    std::cout << std::scientific << std::setprecision(6);
    for (const auto& verdict : family_final) {
        const auto it =
            std::find_if(tests.begin(), tests.end(),
                         [&](const ChiSquareResult& test) {
                             return test.name == verdict.name;
                         });
        const char* label = verdict.rejected ? "FLAG "
                            : verdict.replicated ? "RETAINED-AFTER-REPLICATE "
                                                 : "PASS ";
        std::cout << label << verdict.name << " n=" << it->sample_count
                  << " chi2=" << it->statistic
                  << " df=" << it->degrees_of_freedom
                  << " p=" << verdict.stage_one_p;
        if (verdict.replicated) {
            std::cout << " replicate_p=" << verdict.replicate_p;
        }
        std::cout << " holm=" << verdict.threshold << '\n';
        if (verdict.replicated) {
            // Both a confirmed flag and a retained-after-replicate row are
            // worth reading cell by cell: the first is a divergence to chase,
            // the second is the α-tail draw that the rule caught.
            const auto cells = stage_one.tables.find(verdict.name);
            if (cells != stage_one.tables.end()) {
                for (std::size_t i = 0; i < cells->second.first.size(); ++i) {
                    std::cout << "     cell[" << i << "] observed="
                              << cells->second.first[i] << " expected="
                              << static_cast<double>(it->sample_count) *
                                     cells->second.second[i]
                              << '\n';
                }
            }
        }
        flagged = flagged || verdict.rejected;
    }

    for (const auto& verdict : mutant_final) {
        std::cout << (verdict.rejected ? "CONTROL-REJECTED "
                                       : "CONTROL-SURVIVED ")
                  << verdict.name << " p=" << verdict.stage_one_p;
        if (verdict.replicated) {
            std::cout << " replicate_p=" << verdict.replicate_p;
        }
        std::cout << " holm=" << verdict.threshold << '\n';
        if (!verdict.rejected) {
            flagged = true;
            stage_one.exact_failures.push_back(
                "negative control survived the two-stage rule: " +
                verdict.name);
        }
    }

    // Exact failures are NOT subject to the replicate rule -- they are support
    // and invariant assertions, not frequency claims, so one is a divergence
    // wherever it occurs.
    for (const std::string& failure : stage_one.exact_failures) {
        std::cout << "FLAG exact: " << failure << '\n';
        flagged = true;
    }
    for (const std::string& failure : replicate.exact_failures) {
        std::cout << "FLAG exact (replicate stage): " << failure << '\n';
        flagged = true;
    }

    if (flagged) {
        std::cout << "RESULT FLAGGED -- stop-the-line divergence\n";
        return 1;
    }
    std::cout << "RESULT PASS\n";
    return 0;
}
