// Encounter framework implementation -- composition resolution (miscRng) and
// the pool draw (monsterRng). See encounters.hpp for the full provenance and the
// design/scope rationale. Every draw form / order here mirrors the cited Java.

#include "sts/engine/encounters.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "sts/engine/rng_jdk.hpp"     // JdkRandom, jdk_shuffle (Collections.shuffle)
#include "sts/engine/rng_stream.hpp"

namespace sts::engine {

using sts::registry::CompOp;
using sts::registry::CompStep;
using sts::registry::EncounterDef;
using sts::registry::EncounterPool;
using sts::registry::kEncounters;
using sts::registry::kMaxCompRefs;

// --- Composition resolution (miscRng) ---------------------------------------

ResolvedGroup resolve_composition(const EncounterDef& enc, RngStream& misc) noexcept {
    ResolvedGroup g{};
    const auto push = [&](std::string_view m) noexcept {
        if (g.count < kMonsterCap) {
            g.members[g.count++] = m;
        }
    };

    for (uint8_t si = 0; si < enc.step_count; ++si) {
        const CompStep& s = enc.program[si];
        switch (s.op) {
            case CompOp::EMIT:
                push(s.refs[0]);
                break;
            case CompOp::BOOL: {
                // getLouse / getSlaver / Large Slime: one randomBoolean(); true
                // takes if_true (refs[0]).
                const bool b = random_boolean(misc);
                push(b ? s.refs[0] : s.refs[1]);
                break;
            }
            case CompOp::SEQ_BOOL: {
                // spawnSmallSlimes: one randomBoolean() picks a whole sequence.
                const bool b = random_boolean(misc);
                if (b) {
                    for (uint8_t i = 0; i < s.count; ++i) push(s.refs[i]);
                } else {
                    for (uint8_t i = 0; i < s.aux; ++i) {
                        push(s.refs[static_cast<std::size_t>(s.count) + i]);
                    }
                }
                break;
            }
            case CompOp::PICK: {
                // bottomGet* : EAGER-construct every choice (a BOOL choice draws
                // its coin NOW, during ArrayList build), THEN one random(0,n-1)
                // selects which constructed monster to keep. The non-selected
                // choices' coin draws are still consumed -- that is the whole point
                // of reproducing the list-build order.
                std::array<std::string_view, kMaxCompRefs> cand{};
                const uint8_t n = s.count;
                for (uint8_t i = 0; i < n; ++i) {
                    const std::size_t r0 = static_cast<std::size_t>(2 * i);
                    if ((s.choice_bool & (1u << i)) != 0u) {
                        const bool b = random_boolean(misc);
                        cand[i] = b ? s.refs[r0] : s.refs[r0 + 1];
                    } else {
                        cand[i] = s.refs[r0];
                    }
                }
                const int32_t sel =
                    random(misc, 0, static_cast<int32_t>(n) - 1);  // random(0, size-1)
                push(cand[static_cast<uint8_t>(sel)]);
                break;
            }
            case CompOp::POOL: {
                // spawnGremlins / spawnManySmallSlimes: draw-without-replacement,
                // each draw random(remaining-1) == nextInt(remaining), then
                // ArrayList.remove(index) shifts the tail down.
                std::array<std::string_view, kMaxCompRefs> pool{};
                uint8_t remaining = s.ref_count;
                for (uint8_t i = 0; i < remaining; ++i) pool[i] = s.refs[i];
                for (uint8_t k = 0; k < s.count; ++k) {
                    const int32_t idx =
                        random(misc, static_cast<int32_t>(remaining) - 1);
                    push(pool[static_cast<uint8_t>(idx)]);
                    for (uint8_t j = static_cast<uint8_t>(idx);
                         j + 1 < remaining; ++j) {
                        pool[j] = pool[j + 1];
                    }
                    --remaining;
                }
                break;
            }
        }
    }
    return g;
}

bool resolve_encounter(std::string_view key, RngStream& misc,
                       ResolvedGroup& out) noexcept {
    const EncounterDef* enc = sts::registry::encounter_by_game_id(key);
    if (enc == nullptr) {
        return false;
    }
    out = resolve_composition(*enc, misc);
    return true;
}

// --- Pool draw (monsterRng) -------------------------------------------------

namespace {

struct PoolEntry {
    std::string_view key;
    float weight;
};

// Build one pool from the encounter table: collect (act,pool) rows in id order
// (kEncounters is id-sorted), stable-sort ASCENDING by weight, then normalize
// (divide each by the total). Mirrors MonsterInfo.normalizeWeights exactly --
// Collections.sort (stable) BEFORE the sum/divide, and the sum runs in the SORTED
// order (float addition is order-sensitive), TRAP 1.
uint8_t build_pool(int32_t act, EncounterPool pool,
                   std::array<PoolEntry, 16>& out) noexcept {
    uint8_t n = 0;
    for (const auto& e : kEncounters) {
        if (e.act == act && e.pool == pool && n < out.size()) {
            out[n++] = PoolEntry{e.game_id, e.weight};
        }
    }
    std::stable_sort(out.begin(), out.begin() + n,
                     [](const PoolEntry& a, const PoolEntry& b) noexcept {
                         return a.weight < b.weight;  // Float.compare ascending
                     });
    float total = 0.0f;
    for (uint8_t i = 0; i < n; ++i) total += out[i].weight;
    for (uint8_t i = 0; i < n; ++i) out[i].weight /= total;
    return n;
}

// MonsterInfo.roll: walk the sorted+normalized pool accumulating currentWeight;
// return the first entry with roll < currentWeight (MonsterInfo.java:40-47).
std::string_view roll_pool(std::span<const PoolEntry> pool, float r) noexcept {
    float cur = 0.0f;
    for (const auto& e : pool) {
        cur += e.weight;
        if (r < cur) {
            return e.key;
        }
    }
    // Java returns "ERROR" if float rounding leaves the last band just under the
    // roll; fall back to the last entry (draw count is unaffected either way).
    return pool.empty() ? std::string_view{} : pool.back().key;
}

bool contains(std::span<const std::string_view> xs, std::string_view v) noexcept {
    for (std::string_view x : xs) {
        if (x == v) return true;
    }
    return false;
}

// AbstractDungeon.populateMonsterList (AbstractDungeon.java:1064-1095). Appends
// `n` entries to `list` (shared, so `count` may start non-zero for the strong
// pass): no immediate repeat, and -- unless `elites` -- no A-B-A. A rejected roll
// (--i) still consumed its monsterRng.random() draw.
void populate_monster_list(std::span<std::string_view> list, uint8_t& count,
                           std::span<const PoolEntry> pool, int32_t n,
                           bool elites, RngStream& mrng) noexcept {
    for (int32_t i = 0; i < n; ++i) {
        if (count == 0) {
            list[count++] = roll_pool(pool, random(mrng));
            continue;
        }
        const std::string_view to_add = roll_pool(pool, random(mrng));
        if (to_add != list[count - 1]) {
            if (!elites && count > 1 && to_add == list[count - 2]) {
                --i;  // A-B-A rejected (non-elite only)
                continue;
            }
            list[count++] = to_add;
            continue;
        }
        --i;  // immediate repeat rejected
    }
}

// AbstractDungeon.populateFirstStrongEnemy (AbstractDungeon.java:1057-1062): roll
// until the result is not excluded, then append. Rejection loop -- each roll one
// monsterRng.random() draw.
void populate_first_strong(std::span<std::string_view> list, uint8_t& count,
                           std::span<const PoolEntry> pool,
                           std::span<const std::string_view> exclusions,
                           RngStream& mrng) noexcept {
    std::string_view m = roll_pool(pool, random(mrng));
    while (contains(exclusions, m)) {
        m = roll_pool(pool, random(mrng));
    }
    list[count++] = m;
}

}  // namespace

void generate_monster_lists(int32_t act, RngStream& mrng,
                            MonsterLists& out) noexcept {
    out = MonsterLists{};
    std::array<PoolEntry, 16> pool{};

    // generateWeakEnemies(3): populateMonsterList(3, elites=false).
    const uint8_t nw = build_pool(act, EncounterPool::WEAK, pool);
    populate_monster_list(out.monster_list, out.monster_list_count,
                          {pool.data(), nw}, 3, /*elites=*/false, mrng);

    // generateStrongEnemies(12): generateExclusions() keyed on the 3rd weak
    // monster (monster_list.last()), then populateFirstStrongEnemy(exclusions),
    // then populateMonsterList(12).
    const uint8_t ns = build_pool(act, EncounterPool::STRONG, pool);
    std::span<const std::string_view> excl{};
    if (out.monster_list_count > 0) {
        const sts::registry::EncounterDef* last =
            sts::registry::encounter_by_game_id(
                out.monster_list[out.monster_list_count - 1]);
        if (last != nullptr) {
            excl = std::span<const std::string_view>(last->excludes.data(),
                                                     last->exclude_count);
        }
    }
    populate_first_strong(out.monster_list, out.monster_list_count,
                          {pool.data(), ns}, excl, mrng);
    populate_monster_list(out.monster_list, out.monster_list_count,
                          {pool.data(), ns}, 12, /*elites=*/false, mrng);

    // generateElites(10): populateMonsterList(10, elites=true) into the SEPARATE
    // elite list (no A-B-A rule).
    const uint8_t ne = build_pool(act, EncounterPool::ELITE, pool);
    populate_monster_list(out.elite_list, out.elite_list_count,
                          {pool.data(), ne}, 10, /*elites=*/true, mrng);

    // initializeBoss (fully-unlocked else-branch): add the three bosses in id
    // order, then Collections.shuffle(new Random(monsterRng.randomLong())). The
    // shuffle is the JDK-LCG Fisher-Yates (jdk_shuffle == Collections.shuffle,
    // golden-tested). boss_list[0] is the act boss (setBoss(bossList.get(0))).
    for (const auto& e : kEncounters) {
        if (e.act == act && e.pool == EncounterPool::BOSS &&
            out.boss_list_count < out.boss_list.size()) {
            out.boss_list[out.boss_list_count++] = e.game_id;
        }
    }
    if (out.boss_list_count > 1) {
        const int64_t seed = random_long(mrng);
        JdkRandom jr(seed);
        jdk_shuffle(std::span<std::string_view>(out.boss_list.data(),
                                                out.boss_list_count),
                    jr);
    }
}

}  // namespace sts::engine
