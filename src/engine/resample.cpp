// resample_hidden -- belief-sampler bodies. See resample.hpp for the contract,
// the sampler-private-randomness rule, and what "public" means here. Every
// function in this file is one row of the training-plan §2.4 table; the row it
// implements is named in its comment.

#include "sts/engine/resample.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "sts/engine/boss_chest.hpp"        // BossChestState / kBossChestOfferCount
#include "sts/engine/encounters.hpp"        // continue_monster_lists / condition_boss_list
#include "sts/engine/knowledge.hpp"
#include "sts/engine/map_rooms.hpp"         // RoomType (the consumed-prefix rule)
#include "sts/engine/monster_dispatch.hpp"  // kMonsterAscension
#include "sts/engine/treasure_rooms.hpp"    // treasure_chest_for_rolls / ChestSize
#include "sts/registry/event_table.hpp"     // EventId::MATCH_AND_KEEP
#include "sts/registry/monster_table.hpp"   // monster_def / MonsterRollTiming

namespace sts::engine {

namespace {

// --- Sampler-private primitives ---------------------------------------------
//
// The ONLY doors from this file to randomness. They take a SamplerRng, so no
// overload here can be handed an engine stream by accident.

[[nodiscard]] int32_t draw(SamplerRng& r, int32_t lo, int32_t hi) noexcept {
    return random(r.stream, lo, hi);
}

[[nodiscard]] int64_t draw_long(SamplerRng& r) noexcept {
    return random_long(r.stream);
}

// A stream the state will own from here on: fresh, at counter 0, seeded from
// the sampler. "Fresh stream" in the §2.4 table always means exactly this.
[[nodiscard]] RngStream fresh_stream(SamplerRng& r) noexcept {
    return from_seed(draw_long(r));
}

// Uniform Fisher-Yates over [0, n). `random(s, 0, i)` is inclusive on both ends
// (rng_stream.hpp), so partner selection covers the whole unfixed span.
template <typename T>
void shuffle_span(T* first, int n, SamplerRng& r) noexcept {
    for (int i = n - 1; i > 0; --i) {
        const int j = draw(r, 0, i);
        std::swap(first[static_cast<std::size_t>(i)],
                  first[static_cast<std::size_t>(j)]);
    }
}

}  // namespace

// --- Row: draw-pile order ----------------------------------------------------

void resample_draw_order(CombatState& s, const KnowledgeState& k,
                         SamplerRng& rng) noexcept {
    const int n = static_cast<int>(s.draw_count);
    if (n < 2) {
        return;  // nothing to permute
    }
    if (k.full_order != 0) {
        return;  // REVEAL_DRAW_ORDER: the whole order is public
    }

    // Position p is counted FROM THE TOP; the top of the pile is the LAST array
    // element (piles.hpp convention), so position p lives at index n-1-p.
    const auto index_of_position = [n](int p) noexcept {
        return static_cast<std::size_t>(n - 1 - p);
    };

    // A chain entry is only a constraint while the card is genuinely in the
    // pile. Defensive: a stale entry would otherwise pin a phantom position.
    const auto in_pile = [&s, n](CardPoolIndex pi) noexcept {
        for (int i = 0; i < n; ++i) {
            if (s.draw[static_cast<std::size_t>(i)] == pi) {
                return true;
            }
        }
        return false;
    };

    const int exact = std::min<int>(static_cast<int>(k.exact_prefix), n);

    // The chain members BELOW the exact prefix: known relative order, unknown
    // positions. They must come out in this order, top-first.
    std::array<CardPoolIndex, kKnowledgeChainCap> ordered{};
    int ordered_count = 0;
    for (int i = exact; i < static_cast<int>(k.chain_count); ++i) {
        if (in_pile(k.chain[static_cast<std::size_t>(i)])) {
            ordered[static_cast<std::size_t>(ordered_count++)] =
                k.chain[static_cast<std::size_t>(i)];
        }
    }

    const auto is_ordered_member = [&ordered, ordered_count](
                                       CardPoolIndex pi) noexcept {
        for (int i = 0; i < ordered_count; ++i) {
            if (ordered[static_cast<std::size_t>(i)] == pi) {
                return true;
            }
        }
        return false;
    };

    // Build the permutable window (positions [exact, n)) as a slot list: one
    // MARKER per relative-order member, then every genuinely free card.
    // Shuffling the list and then re-labelling the markers left-to-right with
    // the chain order gives each admissible arrangement exactly m! preimages,
    // hence a UNIFORM draw over the interleavings -- which is the contract's
    // declared posterior (knowledge.hpp).
    struct Slot {
        CardPoolIndex card;
        uint8_t is_marker;
    };
    std::array<Slot, kDrawCap> window{};
    int window_count = 0;
    for (int i = 0; i < ordered_count; ++i) {
        window[static_cast<std::size_t>(window_count++)] = Slot{0, 1};
    }
    for (int p = exact; p < n; ++p) {
        const CardPoolIndex pi = s.draw[index_of_position(p)];
        if (!is_ordered_member(pi)) {
            window[static_cast<std::size_t>(window_count++)] = Slot{pi, 0};
        }
    }

    shuffle_span(window.data(), window_count, rng);

    int next_ordered = 0;
    for (int w = 0; w < window_count; ++w) {
        Slot& slot = window[static_cast<std::size_t>(w)];
        if (slot.is_marker != 0) {
            slot.card = ordered[static_cast<std::size_t>(next_ordered++)];
        }
        s.draw[index_of_position(exact + w)] = slot.card;
    }
}

// --- Row: monster HP / construction rolls ------------------------------------

void resample_monster_construction_rolls(CombatState& s,
                                         const KnowledgeState& k,
                                         SamplerRng& rng) noexcept {
    for (uint8_t i = 0; i < s.monster_count && i < kMonsterCap; ++i) {
        MonsterState& m = s.monsters[i];
        if (m.monster_id == static_cast<uint16_t>(MonsterId::NONE)) {
            continue;
        }
        if (k.monster_roll_known[i] != 0) {
            continue;  // telegraphed once: public forever
        }
        const sts::registry::MonsterDef* def =
            sts::registry::monster_def(static_cast<MonsterId>(m.monster_id));
        if (def == nullptr) {
            continue;
        }
        // Only a def that DECLARES a constructor-time monsterHpRng roll parks
        // one in pad0 (monster_louse.cpp). Every other monster's pad0 is
        // per-type scratch that is not an unrevealed RNG realization, so the
        // registry gate is what keeps this from corrupting it.
        for (uint8_t ri = 0; ri < def->roll_count; ++ri) {
            const sts::registry::MonsterRollDef& roll = def->rolls[ri];
            if (roll.timing !=
                    sts::registry::MonsterRollTiming::CONSTRUCTOR_AFTER_HP ||
                roll.stream != sts::registry::MonsterRollStream::MONSTER_HP) {
                continue;
            }
            m.pad0 = static_cast<uint8_t>(draw(rng,
                                               roll.min(kMonsterAscension),
                                               roll.max(kMonsterAscension)));
            break;
        }
    }
}

// --- Row: relic-pool remainders ----------------------------------------------

void resample_relic_pool_remainders(RunState& rs, SamplerRng& rng) noexcept {
    // CONTRACT COARSENING (plan §2.6d) -- pop-time canSpawn.
    //
    // returnRandomRelicKey pops the front, and on a canSpawn failure the popped
    // relic is CONSUMED anyway and the call reroutes to an end-pop
    // (relic_pools.cpp). The player never sees the rejected relic, so the
    // remaining MULTISET is not "initial pool minus observed acquisitions" and
    // is not derivable from public history at all. Reconstructing its posterior
    // would mean enumerating rejection histories per tier.
    //
    // The declared contract instead treats the remaining multiset as public and
    // re-permutes only its ORDER. That is exactly right for the quantity search
    // actually consumes -- what comes out of the next pop, which under a
    // uniformly shuffled pool is uniform over the remainder -- and it is the
    // reason this row permutes the STORED window rather than rebuilding one:
    // the stored window is the only place the true remainder exists.
    for (int p = 0; p < kRelicTierCount; ++p) {
        const int count = std::min<int>(rs.relic_pool_count[p], kRelicPoolCap);
        shuffle_span(rs.relic_pools[p], count, rng);
    }
}

// --- Row: boss-chest offers (the head of the same hidden draw) ---------------
//
// Put an UNOPENED chest's three offers back on the FRONT of the BOSS pool, so
// the pool row above permutes offers-and-remainder as the one window they came
// from. See the call site in resample_hidden for why they cannot be resampled
// independently of the pool.
//
// A Circlet offer is the EXHAUSTED-POOL fallback ("Red Circlet" ->
// RelicLibrary's Circlet, relic_pools.cpp:386-389) and was never a pool member,
// so putting it back would invent an entry. Those offers are left alone and the
// redraw below reproduces them anyway: a pool that was empty stays empty.
void restore_boss_chest_offers(RunState& rs, BossChestState& chest) noexcept {
    constexpr int p = static_cast<int>(RelicPool::BOSS);
    for (int i = kBossChestOfferCount - 1; i >= 0; --i) {
        const auto id = static_cast<RelicId>(chest.relics[i]);
        if (id == RelicId::NONE || id == RelicId::CIRCLET) {
            continue;
        }
        if (rs.relic_pool_count[p] >= kRelicPoolCap) {
            return;  // cannot restore without dropping an entry: leave as is
        }
        for (int k = rs.relic_pool_count[p]; k > 0; --k) {
            rs.relic_pools[p][k] = rs.relic_pools[p][k - 1];
        }
        rs.relic_pools[p][0] = chest.relics[i];
        ++rs.relic_pool_count[p];
    }
}

// Re-run BossChest's constructor loop against the freshly permuted pool. Going
// back through return_random_relic_key rather than reading three slots is what
// keeps the particle's canSpawn behaviour faithful -- a rejected relic is still
// popped and consumed (AbstractDungeon.java:804-806), so the particle's pool
// cursor moves exactly as a real one would.
void redraw_boss_chest_offers(RunState& rs, BossChestState& chest) noexcept {
    for (int i = 0; i < kBossChestOfferCount; ++i) {
        RelicSpawnContext ctx{};
        ctx.floor = rs.floor;
        fill_boss_spawn_gates(rs, ctx);
        chest.relics[i] = static_cast<uint16_t>(
            return_random_relic_key(rs, RelicTier::BOSS, ctx));
    }
}

// --- Row: mid-event hidden state (Match & Keep board) ------------------------

void resample_match_and_keep_board(EventDialogState& es,
                                   SamplerRng& rng) noexcept {
    if (es.event_id !=
        static_cast<uint16_t>(sts::registry::EventId::MATCH_AND_KEEP)) {
        return;
    }
    // A slot is REVEALED when its pair was matched and left the board
    // (`taken`), or when it is the card currently face up (`scratch1`, the
    // chosenCard index; -1 for none -- shrines.cpp).
    //
    // CONTRACT COARSENING (plan §2.6d): a human also remembers the identities
    // of cards flipped on FAILED attempts, which is most of the game's skill.
    // The engine records no such history (EventBoardCard has no `seen` bit), so
    // the declared contract pins only currently-visible reveals. That is a
    // strictly WIDER belief than reality, which is sound -- it makes the agent
    // pessimistic about its own memory, never optimistic about hidden cards.
    // Narrowing it is a KnowledgeState change (a per-slot seen bit) plus a
    // matching pin here, not a sampler-only fix.
    std::array<EventBoardCard, kMatchBoardSize> hidden{};
    std::array<int, kMatchBoardSize> hidden_slot{};
    int hidden_count = 0;
    for (int i = 0; i < kMatchBoardSize; ++i) {
        if (es.board[i].taken != 0 || es.scratch1 == static_cast<int16_t>(i)) {
            continue;
        }
        hidden_slot[static_cast<std::size_t>(hidden_count)] = i;
        hidden[static_cast<std::size_t>(hidden_count)] = es.board[i];
        ++hidden_count;
    }
    shuffle_span(hidden.data(), hidden_count, rng);
    for (int i = 0; i < hidden_count; ++i) {
        es.board[hidden_slot[static_cast<std::size_t>(i)]] =
            hidden[static_cast<std::size_t>(i)];
    }
}

// --- Row: current-visit chest (size public, contents hidden) -----------------

void resample_treasure_chest_contents(TreasureChest& chest,
                                      SamplerRng& rng) noexcept {
    if (chest.opened != 0 ||
        chest.size == static_cast<uint8_t>(ChestSize::NONE)) {
        return;  // opened: contents are public. NONE: no live chest.
    }
    // AbstractChest.randomizeReward is ONE d100 (treasure_rooms.hpp), and the
    // size band it is read against is public, so the conditional law of the
    // contents given the size is a fresh uniform roll interpreted in that band.
    // The size roll itself is NOT redrawn -- a band representative is fed in so
    // treasure_chest_for_rolls stays the single authority on the thresholds.
    const int32_t band_representative =
        chest.size == static_cast<uint8_t>(ChestSize::SMALL)    ? 0
        : chest.size == static_cast<uint8_t>(ChestSize::MEDIUM) ? 50
                                                                : 83;
    const TreasureChest rolled =
        treasure_chest_for_rolls(band_representative, draw(rng, 0, 99));
    chest.relic_tier = rolled.relic_tier;
    chest.has_gold = rolled.has_gold;
}

// --- The whole table ---------------------------------------------------------

void resample_hidden(RunController& rc, SamplerRng& rng) noexcept {
    RunState& rs = rc.run;

    // Row "Floor-stream derivation, future acts": a FRESH FAKE RUN SEED per
    // particle. Every engine consultation of a floor stream reads the state's
    // own run_seed (floor_stream(rs.run_seed, floor) in next_room_transition),
    // so replacing it here is what makes every future floor's reseed regenerate
    // a FAKE future automatically -- no ambient true seed exists to leak
    // through. map_stream() for future acts is the same pattern, uncalled in
    // S1 and inherited by S2.
    rs.run_seed = draw_long(rng);

    // Row "?-rooms, future rewards/shops/chests/potions": fresh streams. The
    // pity counters (card_blizz_randomizer, blizzard_potion_mod, the three
    // event-pity floats) and the membership bitsets ride on RunState and are
    // PUBLIC, so they are deliberately untouched below.
    rs.monster_rng = fresh_stream(rng);
    rs.event_rng = fresh_stream(rng);
    rs.merchant_rng = fresh_stream(rng);
    rs.card_rng = fresh_stream(rng);
    rs.treasure_rng = fresh_stream(rng);
    rs.relic_rng = fresh_stream(rng);
    rs.potion_rng = fresh_stream(rng);
    rs.neow_rng = fresh_stream(rng);

    // Row "mapRng": fresh per particle. The act map itself is public and stays;
    // what must not be shared across particles is the LIVE stream, because the
    // emerald-elite entry buff is a mid-run mapRng draw and copying the true
    // stream would fuse that draw across the whole belief.
    rs.map_rng = fresh_stream(rng);

    // Rows "Future intents" (aiRng) and "In-combat randomness"
    // (cardRandomRng / miscRng), plus shuffleRng and monsterHpRng, which are the
    // same kind of source. These are INDEPENDENT fresh streams rather than
    // floor_stream(rs.run_seed, floor): the true streams have already been
    // consumed part-way through this floor, so re-deriving them from the fake
    // seed would hand the particle a stream REWOUND to its floor-entry state --
    // a different (and wrong) distribution from "the unknown continuation".
    // The next floor transition reseeds all five from the fake run seed, which
    // is where the fake-future derivation takes over.
    rc.combat.monster_hp_rng = fresh_stream(rng);
    rc.combat.ai_rng = fresh_stream(rng);
    rc.combat.shuffle_rng = fresh_stream(rng);
    rc.combat.card_random_rng = fresh_stream(rng);
    rc.combat.misc_rng = fresh_stream(rng);

    // Rows "Draw-pile order" and "Monster HP / construction rolls". Both are
    // combat-scoped, and KnowledgeState is explicitly "stale-but-unread outside
    // COMBAT" (run_advance.hpp), so they run only while a combat is live --
    // reading a stale chain against a folded-back combat would pin nonsense.
    if (rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
        resample_draw_order(rc.combat, rc.knowledge, rng);
        resample_monster_construction_rolls(rc.combat, rc.knowledge, rng);
    }

    // Row "Encounter-list suffix": CONDITION, DON'T REROLL. The consumed prefix
    // is public (plan §2.1), and so is the entry the CURRENT room is using --
    // the player is looking at those monsters. The cursor advances on room EXIT
    // (next_room_transition), so the observed length is the cursor plus one
    // while standing in a room of that kind. rc.room_type is the RESOLVED room
    // kind, which is what makes a ?-room that rolled MONSTER count here.
    //
    // ACT 4 IS A PURE COPY, NOT A CONDITION (S3.51, s3-design §7's "Act 4's
    // content is public and constant" fact). `act4_crossing` fills all three
    // lists to `kActEndingListLen` in ONE step (run_advance.cpp) -- there is no
    // monsterRng suffix left to continue, and `build_pool(4, ...)` legitimately
    // returns an EMPTY pool for every one of the three EncounterPool kinds
    // (encounters.yaml's two Act-4 rows are `weight: 0.0`, outside every pool).
    // Calling continue_monster_lists/condition_boss_list here anyway is not
    // merely redundant: `monster_keep`/`elite_keep` are the CURSOR (0 before
    // the room is entered), while `monster_target`/`elite_target` are already
    // `kActEndingListLen` from construction, so `[keep, target)` looks
    // unfulfilled and `populate_monster_list` draws from the empty pool --
    // `roll_pool` on an empty span returns `kNoEncounterKey` (no UB, but WRONG:
    // it overwrites the real "Shield and Spear"/"The Heart" entries with NONE
    // in a particle taken before the elite/boss room is entered, e.g. at the
    // Act-4 rest or shop). The fixed lists are public from the crossing, so a
    // twin must carry them unchanged -- the same "pure copy" treatment already
    // given to the map, the shop stock and the Neow options below.
    if (rs.act == kFinalAct) {
        // condition_boss_list's PERMUTATION would be harmless in isolation
        // (every Act-4 boss_list entry is the identical "The Heart" id), but
        // skipping it too keeps this branch a single, checkable statement: at
        // Act 4 nothing about `rc.lists` is hidden, so nothing here draws.
    } else {
        const RoomType room = static_cast<RoomType>(rc.room_type);
        const uint8_t monster_keep = static_cast<uint8_t>(
            rc.monster_cursor + (room == RoomType::Monster ? 1 : 0));
        const uint8_t elite_keep = static_cast<uint8_t>(
            rc.elite_cursor + (room == RoomType::Elite ? 1 : 0));
        continue_monster_lists(static_cast<int32_t>(rs.act), rng.stream,
                               monster_keep, elite_keep, rc.lists);
        // The boss list conditions on the public prefix. That is boss_list[0]
        // (named on the map from act start) in every ordinary run -- and ONE
        // MORE at the A20 double boss, where entering the second Act-3 boss
        // room reveals boss_list[1] to the player as surely as entering any
        // other room reveals its encounter. Same shape as the two prefixes
        // above: the cursor, plus one while standing in a room of that kind.
        const uint8_t boss_keep = static_cast<uint8_t>(
            rc.boss_cursor + (room == RoomType::Boss ? 1 : 0));
        condition_boss_list(rc.lists, rng.stream, boss_keep);
    }

    // Row "Mid-event hidden state": pin the flips, permute the rest. Guarded on
    // the phase so a stale board left in the transient struct after the event
    // finished is not stirred (it is unread, but a particle should differ from
    // the truth only where the belief says it may).
    if (rc.phase == static_cast<uint8_t>(RunPhase::EVENT_DIALOG)) {
        resample_match_and_keep_board(rc.event, rng);
    }

    // Chest SIZE is public and copied; the unopened chest's CONTENTS are the
    // hidden half (plan §2.1's TreasureChest note).
    if (rc.phase == static_cast<uint8_t>(RunPhase::TREASURE_ROOM)) {
        resample_treasure_chest_contents(rc.treasure_chest, rng);
    }

    // Row "Boss-chest offers", which must run BEFORE the pool row below.
    //
    // An UNOPENED boss chest has already front-popped three relics out of the
    // BOSS pool (BossChest.java:35-39, at room entry) and the player has not
    // seen them. So the offers are hidden state of exactly the same kind as the
    // residual pool -- and they are the same DRAW: the three offers are the head
    // of the pre-pop pool order, and the remainder is its tail. Resampling them
    // separately would be incoherent (a particle could offer a relic its own
    // pool still contains), so this puts the three back on the FRONT, lets the
    // pool row below permute the whole window, and then re-runs the three pops
    // through return_random_relic_key -- which is what keeps canSpawn rejection
    // and its extra pop faithful in the particle instead of hand-rolled.
    //
    // The same CONTRACT COARSENING the pool row declares applies here, one step
    // earlier: the multiset is treated as public and only its order is redrawn.
    //
    // An OPENED chest (`seen`) is a pure copy -- the offers are on screen, and
    // PublicView carries them; a twin that redrew them would fail the invariance
    // the whole T0.5 suite is built on.
    const bool boss_chest_hidden =
        rc.phase == static_cast<uint8_t>(RunPhase::BOSS_TREASURE) &&
        rc.run.boss_chest.seen == 0 &&
        rc.run.boss_chest.relics[0] != static_cast<uint16_t>(RelicId::NONE);
    if (boss_chest_hidden) {
        restore_boss_chest_offers(rs, rc.run.boss_chest);
    }

    // Row "Relic-pool remainders".
    resample_relic_pool_remainders(rs, rng);

    if (boss_chest_hidden) {
        redraw_boss_chest_offers(rs, rc.run.boss_chest);
    }

    // Rows that are PURE COPIES, listed so the table is visibly complete:
    // current-visit shop stock (rc.shop), Neow options (rc.neow), the act map
    // (rs.map), the live reward screen (rc.rewards), the rest-site menu
    // (rc.rest), the pending-bottle overlay, master deck, relics, potions,
    // gold/hp/floor/act/ascension, the pity counters and the membership
    // bitsets, `run.victory_kind` / `run.act4_floor_base` (S3.51: both are
    // written once, from an observed event, never redrawn), and -- ONLY at
    // Act 4 (S3.51, the `if (rs.act == kFinalAct)` branch above) -- the whole
    // of `rc.lists`. Every one of them is public, so the sampler must not
    // touch it -- and does not.
}

RunController resample_hidden(const RunController& rc,
                              int64_t sampler_seed) noexcept {
    RunController particle = rc;
    SamplerRng rng = sampler_rng_from_seed(sampler_seed);
    resample_hidden(particle, rng);
    return particle;
}

}  // namespace sts::engine
