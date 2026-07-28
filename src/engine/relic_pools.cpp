// Relic dungeon pools + RunState acquisition. See relic_pools.hpp for the Java
// provenance of the pool mechanics themselves.
//
// The per-relic PICKUP behaviour that used to live here as hand-written switches
// (a 24-case canSpawn gate and a 5-case onEquip switch) is now GENERATED from
// registry/relics.yaml `pickup:`. Each row that overrides a surface contributes
// one X(<RelicId>, <handler>) entry to STS_REGISTRY_RELIC_CAN_SPAWN /
// STS_REGISTRY_RELIC_ON_EQUIP (generated relic_table.hpp, reached via
// sts/engine/relics.hpp); this file expands each list twice -- once for the
// extern declarations, once for the id -> handler switch -- exactly as
// relic_hooks.cpp does for combat bodies. The handlers live in per-tier
// translation units under src/engine/relics/.
//
// The point is that filling in a tier no longer edits this file: each tier adds
// registry rows, a relic_pickup_<tier>.cpp and a CMakeLists line. This was the
// last shared per-relic switch on the pickup side, so two people bringing up
// different tiers no longer contend for one source line.
//
// Because each handler is odr-used by the switch below, a row that LISTS a
// surface but whose body nobody wrote is an undefined reference at link time
// rather than a silently missing case. That matters most for canSpawn: the old
// `default: return true` answered "no gate" for a forgotten relic, which is not
// a no-op but a changed relicRng draw order.
//
// That link error is also why relic_can_spawn_fn and relic_on_equip_fn carry NO
// static_assert on kRelicsCount, while other deliberate-subset switches in this
// file do. Their case lists are expanded from the generated X-macros, so they
// track the registry automatically and a missing body already fails the build --
// a strictly stronger guarantee than a count that a reader can re-bless. Adding
// a count assertion there would only add a number to keep up to date.

#include "sts/engine/relic_pools.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <span>

#include "relics/relic_pickup.hpp"  // RelicCanSpawnSig/Fn, RelicOnEquipSig/Fn
#include "sts/engine/cards.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/registry/manifest.hpp"  // generated kRelicsCount / kCardsCount

namespace sts::engine {
namespace {

// Deliberately 5 of the 8 generated tiers: STARTER, SPECIAL and EVENT relics
// have no dungeon pool, so `default: -1` means "not poolable" and is load-bearing.
//
// Note what this switch is keyed to. Adding a RELIC cannot invalidate it -- only
// adding a TIER can, and a new tier would fall silently into the default. So the
// pin here is on the generated RelicTier enum, not on a manifest row count: the
// count assertions elsewhere in this file guard the row-count axis, and this one
// guards the tier axis. Two other switches ask the same question and must be
// revisited with it: the empty-pool fallback ladders in return_random_relic_key
// and return_end_random_relic_key below.
int pool_index(RelicTier tier) noexcept {
    static_assert(static_cast<int>(RelicTier::EVENT) == 7 &&
                      kRelicTierCount == 5,
                  "new RelicTier: decide whether it gets a dungeon pool. If it "
                  "does, it needs a RelicPool slot, a bump of kRelicTierCount, "
                  "and a case in BOTH empty-pool fallback ladders "
                  "(return_random_relic_key / return_end_random_relic_key); if "
                  "it does not, it belongs in this default with the other "
                  "non-poolable tiers.");
    switch (tier) {
        case RelicTier::COMMON: return static_cast<int>(RelicPool::COMMON);
        case RelicTier::UNCOMMON: return static_cast<int>(RelicPool::UNCOMMON);
        case RelicTier::RARE: return static_cast<int>(RelicPool::RARE);
        case RelicTier::SHOP: return static_cast<int>(RelicPool::SHOP);
        case RelicTier::BOSS: return static_cast<int>(RelicPool::BOSS);
        default: return -1;
    }
}

RelicId remove_front(RunState& rs, int p) noexcept {
    const uint8_t count = rs.relic_pool_count[p];
    assert(count > 0);
    const RelicId id = static_cast<RelicId>(rs.relic_pools[p][0]);
    for (uint8_t i = 1; i < count; ++i) {
        rs.relic_pools[p][i - 1] = rs.relic_pools[p][i];
    }
    rs.relic_pools[p][count - 1] = 0;
    rs.relic_pool_count[p] = static_cast<uint8_t>(count - 1);
    return id;
}

RelicId remove_end(RunState& rs, int p) noexcept {
    const uint8_t count = rs.relic_pool_count[p];
    assert(count > 0);
    const uint8_t at = static_cast<uint8_t>(count - 1);
    const RelicId id = static_cast<RelicId>(rs.relic_pools[p][at]);
    rs.relic_pools[p][at] = 0;
    rs.relic_pool_count[p] = at;
    return id;
}

void remove_owned_from_pools(RunState& rs) noexcept {
    for (uint8_t owned = 0; owned < rs.relic_count; ++owned) {
        const uint16_t id = rs.relics[owned].relic_id;
        for (int p = 0; p < kRelicTierCount; ++p) {
            const uint8_t count = rs.relic_pool_count[p];
            for (uint8_t i = 0; i < count; ++i) {
                if (rs.relic_pools[p][i] != id) {
                    continue;
                }
                for (uint8_t j = static_cast<uint8_t>(i + 1); j < count; ++j) {
                    rs.relic_pools[p][j - 1] = rs.relic_pools[p][j];
                }
                rs.relic_pools[p][count - 1] = 0;
                rs.relic_pool_count[p] = static_cast<uint8_t>(count - 1);
                break;
            }
        }
    }
}

// (upgrade_random_cards -- WarPaint/Whetstone's shared body -- moved to
// relics/relic_pickup.hpp when those two onEquip handlers left this file; more
// than one tier's TU needs it, so it is a shared inline helper rather than a
// file-local static, mirroring heal_player in relics/relic_native.hpp.)

}  // namespace

void initialize_relic_pools(RunState& rs) noexcept {
    static_assert(sts::registry::manifest::kRelicsCount == 142,
                  "new relic: it joins its tier's dungeon pool at its "
                  "pool_order, which fixes the relicRng shuffle order for that "
                  "tier. Confirm the row's tier is poolable, that pool_order is "
                  "contiguous and unique within the tier, and that the tier "
                  "still fits kRelicPoolCap.");
    for (int p = 0; p < kRelicTierCount; ++p) {
        std::fill_n(rs.relic_pools[p], kRelicPoolCap, uint16_t{0});
        rs.relic_pool_count[p] = 0;
    }

    for (const RelicDef* def : kRelicDefs) {
        if (def->pool_order < 0) {
            continue;
        }
        const int p = pool_index(def->tier);
        assert(p >= 0 && def->pool_order < kRelicPoolCap);
        rs.relic_pools[p][def->pool_order] = static_cast<uint16_t>(def->id);
        const uint8_t needed = static_cast<uint8_t>(def->pool_order + 1);
        if (rs.relic_pool_count[p] < needed) {
            rs.relic_pool_count[p] = needed;
        }
    }

    for (int p = 0; p < kRelicTierCount; ++p) {
        JdkRandom jdk(random_long(rs.relic_rng));
        jdk_shuffle(std::span<uint16_t>(rs.relic_pools[p],
                                       rs.relic_pool_count[p]), jdk);
    }

    if (rs.floor >= 1) {
        remove_owned_from_pools(rs);
    }
}

RelicTier return_random_relic_tier(RunState& rs) noexcept {
    return relic_tier_for_roll(random(rs.relic_rng, 0, 99));
}

void fill_campfire_relic_count(const RunState& rs,
                               RelicSpawnContext& ctx) noexcept {
    // Girya.canSpawn (Girya.java:538-549), PeacePipe.canSpawn (:862-873) and
    // Shovel.canSpawn (:1024-1035) each walk player.relics counting instances of
    // PeacePipe, Shovel or Girya and spawn only while that count is < 2. The scan
    // is identical in all three, so it is done once here and the three predicates
    // read the number.
    uint8_t n = 0;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        const uint16_t id = rs.relics[i].relic_id;
        if (id == static_cast<uint16_t>(RelicId::GIRYA) ||
            id == static_cast<uint16_t>(RelicId::PEACE_PIPE) ||
            id == static_cast<uint16_t>(RelicId::SHOVEL)) {
            ++n;
        }
    }
    ctx.campfire_relic_count = n;
}

void fill_boss_spawn_gates(const RunState& rs, RelicSpawnContext& ctx) noexcept {
    // Ectoplasm.canSpawn (Ectoplasm.java:50-53) reads AbstractDungeon.actNum;
    // BlackBlood.canSpawn (BlackBlood.java:33-36) reads
    // player.hasRelic("Burning Blood"). Both are pure predicates over the
    // context, so the RunState reads happen once, here.
    ctx.act = rs.act;
    ctx.has_burning_blood = false;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id ==
            static_cast<uint16_t>(RelicId::BURNING_BLOOD)) {
            ctx.has_burning_blood = true;
            break;
        }
    }
}

void fill_deck_spawn_gates(const RunState& rs, RelicSpawnContext& ctx) noexcept {
    // Keyed to CARDS, not relics: the gates are a scan over the master deck, and
    // both of their hard-coded card facts live here rather than in the registry.
    // Checked for the sixteen red RARE rows: none is CardRarity.BASIC (the BASIC
    // set is still exactly Strike/Defend/Bash), and five of them ARE POWER-type
    // (Barricade, Brutality, Corruption, Demon Form, Juggernaut), which only
    // widens the already-live Bottled Tornado gate.
    // Checked for the fourteen colorless UNCOMMON rows, and the answer to BOTH
    // questions below is NO: every one is CardRarity.UNCOMMON (their ctors, e.g.
    // BandageUp.java:26 / SwiftStrike.java:30) so the hard-coded BASIC set is
    // still exactly {STRIKE, DEFEND, BASH}, and every one is CardType.SKILL or
    // CardType.ATTACK -- there is no POWER among them -- so Bottled Tornado's
    // rarity-agnostic type scan is unchanged too. Neither gate moved; the number
    // is not merely bumped.
    // Checked for the four colorless RARE rows added alongside this comment
    // (Apotheosis, Master of Strategy, Sadistic Nature, Thinking Ahead): the
    // BASIC answer is NO for all four (their ctors pass CardRarity.RARE, so
    // the hard-coded BASIC set stays exactly {STRIKE, DEFEND, BASH}), but the
    // POWER answer is YES for Sadistic Nature (SadisticNature.java:26,
    // CardType.POWER) -- the same "only widens the already-live gate" shape
    // the red RARE batch's five POWER cards set, since ctx.deck_has_power is a
    // plain type scan with no rarity or color clause (CardHelper.hasCardType,
    // :80-86). The registry now has 9 POWER-type cards; the gate was already
    // live and stays live.
    // Checked for the three colorless RARE rows added alongside this comment
    // (Secret Technique, Secret Weapon, Violence): BOTH answers are NO. All
    // three ctors pass CardRarity.RARE (SecretTechnique.java:25 /
    // SecretWeapon.java:25 / Violence.java:25), so the hard-coded BASIC set is
    // still exactly {STRIKE, DEFEND, BASH}; and all three are CardType.SKILL,
    // so the POWER-type count stays at 9 and Bottled Tornado's gate is
    // untouched. Neither gate moved; the number below is not merely bumped.
    // Checked for the five colorless RARE rows added alongside this comment
    // (Chrysalis, Magnetism, Mayhem, Metamorphosis, Transmutation). BASIC: NO
    // for all five -- every ctor passes CardRarity.RARE (Chrysalis.java:26,
    // Magnetism.java:26, Mayhem.java:26, Metamorphosis.java:26,
    // Transmutation.java:26), so the hard-coded BASIC set stays exactly
    // {STRIKE, DEFEND, BASH}. POWER: YES for TWO of them -- Magnetism.java:26
    // and Mayhem.java:26 both pass CardType.POWER (Chrysalis, Metamorphosis and
    // Transmutation are CardType.SKILL). That is the same "only widens the
    // already-live gate" shape the red RARE batch's five POWER cards and
    // Sadistic Nature set: ctx.deck_has_power is a plain type scan with NO
    // rarity and no color clause (CardHelper.hasCardType, :80-86), so two more
    // POWER rows can only make an already-satisfiable predicate satisfiable in
    // more decks. The registry now has 11 POWER-type cards; the gate was
    // already live and stays live. And the follow-up question these two raise
    // has a definite answer: Bottled Tornado does not SELECT the bottled card
    // here -- canSpawn (:93-95) only asks whether ANY POWER is in the master
    // deck -- so a COLORLESS POWER changes the predicate's inputs and nothing
    // else in this file.
    // Checked for the three colorless RARE rows added alongside this comment
    // (Hand of Greed, Panache, The Bomb). BASIC: NO for all three -- every ctor
    // passes CardRarity.RARE (HandOfGreed.java:26, Panache.java:26,
    // TheBomb.java:26), so the hard-coded BASIC set stays exactly
    // {STRIKE, DEFEND, BASH}. POWER: YES for exactly ONE of them --
    // Panache.java:26 passes CardType.POWER (Hand of Greed is CardType.ATTACK
    // and The Bomb is CardType.SKILL). Answering the gate's question rather than
    // bumping the number: ctx.deck_has_power is a plain type scan with NO rarity
    // and no color clause (CardHelper.hasCardType, :80-86), so one more POWER row
    // can only make an already-satisfiable predicate satisfiable in more decks --
    // the same "only widens the already-live gate" shape the red RARE batch's
    // five POWER cards, Sadistic Nature, and Magnetism/Mayhem set. The registry
    // now has 12 POWER-type cards; the gate was already live and stays live.
    // Checked again for the two colorless UNCOMMON rows that closed the 92-111
    // block (Forethought, Purity). BASIC: NO -- both ctors pass
    // CardRarity.UNCOMMON (Forethought.java:25, Purity.java:25), so the
    // hard-coded BASIC set stays exactly {STRIKE, DEFEND, BASH}. POWER: NO for
    // either -- both pass CardType.SKILL at the same lines -- so Bottled
    // Tornado's rarity-agnostic type scan sees no new input at all and the count
    // of POWER-type rows stays at 12. Neither gate moved.
    // Checked again for CURSE_OF_THE_BELL (Wave-C track 2, CardId 127). BASIC:
    // NO -- the ctor passes CardRarity.SPECIAL (CurseOfTheBell.java:24), so the
    // hard-coded BASIC set stays exactly {STRIKE, DEFEND, BASH}. POWER: NO --
    // the same ctor passes CardType.CURSE, so Bottled Tornado's type scan sees
    // no new input and the POWER-row count stays at 12. Neither gate moved. (A
    // curse also cannot satisfy the ATTACK/SKILL bottle gates below, which
    // branch on those two types only.)
    static_assert(sts::registry::manifest::kCardsCount == 127,
                  "new card: is it CardRarity.BASIC? The BASIC set is hard-coded "
                  "below as exactly {STRIKE, DEFEND, BASH}, and a fourth basic "
                  "row would wrongly satisfy the Bottled Flame/Lightning "
                  "non-basic gates. Is it a POWER? Then it also feeds Bottled "
                  "Tornado's rarity-agnostic gate.");
    // BottledFlame/BottledLightning.canSpawn (each :93-99): any master-deck card
    // of the wanted type whose rarity != BASIC. The only BASIC red rows are
    // Strike/Defend/Bash (Strike_Red.java/Defend_Red.java/Bash.java ctors --
    // CardRarity.BASIC), so non-basic == not one of those three ids.
    // BottledTornado.canSpawn (:93-95) is NOT that shape: it delegates to
    // CardHelper.hasCardType(POWER) (CardHelper.java:80-86), a plain type scan
    // over masterDeck.group with NO rarity clause at all. The POWER gate is
    // therefore computed BEFORE the BASIC skip below -- folding it into the
    // post-`continue` body would model a rarity filter the Java does not have.
    // The two are indistinguishable today (no BASIC red row is a POWER), but the
    // faithful structure is the one that stays correct if that ever changes.
    // The registry has 12 POWER-type cards -- the eight red ones (Combust/Dark
    // Embrace/Evolve/Feel No Pain/Fire Breathing/Inflame/Metallicize/Rupture)
    // plus the four colorless (Sadistic Nature, Magnetism, Mayhem, Panache) --
    // so this gate is live.
    ctx.deck_has_nonbasic_attack = false;
    ctx.deck_has_nonbasic_skill = false;
    ctx.deck_has_power = false;
    for (uint16_t i = 0; i < rs.master_deck_count; ++i) {
        const CardId cid = static_cast<CardId>(rs.master_deck[i].card_id);
        const CardDef* def = card_def(cid);
        if (def == nullptr) {
            continue;
        }
        if (def->type == CardType::POWER) {
            ctx.deck_has_power = true;  // rarity-agnostic, per CardHelper
        }
        if (cid == CardId::STRIKE || cid == CardId::DEFEND ||
            cid == CardId::BASH) {
            continue;  // CardRarity.BASIC
        }
        if (def->type == CardType::ATTACK) {
            ctx.deck_has_nonbasic_attack = true;
        } else if (def->type == CardType::SKILL) {
            ctx.deck_has_nonbasic_skill = true;
        }
    }
}

// --- canSpawn: generated per-relic predicate dispatch -------------------------
//
// Each handler spells its Java canSpawn body IN FULL, including the
// `Settings.isEndless ||` disjunct where the Java has one. The switch this
// replaced instead hoisted a single `if (ctx.endless) return true;` between two
// case blocks, which forced the Bottled trio (whose bodies have no isEndless
// clause at all) to be special-cased ahead of it, and got MawBank/SmilingMask/
// Courier wrong in Endless -- their Java is `(isEndless || floor <= 48) && !shop`,
// so endless bypasses the floor clause but never the shop clause. Per-relic
// bodies make that structural question disappear. `endless` has no producer in
// the engine today (RelicSpawnContext defaults it false and nothing but tests
// sets it), so no reachable state changes.

#define STS_RELIC_CAN_SPAWN_DECL(ID, FN) extern RelicCanSpawnSig FN;
STS_REGISTRY_RELIC_CAN_SPAWN(STS_RELIC_CAN_SPAWN_DECL)
#undef STS_RELIC_CAN_SPAWN_DECL

RelicCanSpawnFn relic_can_spawn_fn(RelicId id) noexcept {
    switch (id) {
#define STS_RELIC_CAN_SPAWN_CASE(ID, FN) \
    case RelicId::ID:                    \
        return &FN;
        STS_REGISTRY_RELIC_CAN_SPAWN(STS_RELIC_CAN_SPAWN_CASE)
#undef STS_RELIC_CAN_SPAWN_CASE
        default:
            return nullptr;  // no canSpawn override on this row
    }
}

bool relic_can_spawn(RelicId id, RelicSpawnContext ctx) noexcept {
    const RelicCanSpawnFn fn = relic_can_spawn_fn(id);
    // AbstractRelic.canSpawn's base implementation is `return true` -- a relic
    // without an override always spawns.
    return fn == nullptr || fn(ctx);
}

RelicId return_random_relic_key(RunState& rs, RelicTier tier,
                                RelicSpawnContext ctx) noexcept {
    const int p = pool_index(tier);
    if (p < 0) {
        return RelicId::NONE;
    }
    if (rs.relic_pool_count[p] == 0) {
        switch (tier) {
            case RelicTier::COMMON:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::UNCOMMON:
                return return_random_relic_key(rs, RelicTier::RARE, ctx);
            case RelicTier::RARE:
                return RelicId::CIRCLET;
            case RelicTier::SHOP:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::BOSS:
                // Java returns the key "Red Circlet", but RelicLibrary does not
                // register RedCirclet; getRelic therefore defaults to Circlet.
                return RelicId::CIRCLET;
            default:
                return RelicId::NONE;
        }
    }
    const RelicId id = remove_front(rs, p);
    return relic_can_spawn(id, ctx)
               ? id
               : return_end_random_relic_key(rs, tier, ctx);
}

RelicId return_end_random_relic_key(RunState& rs, RelicTier tier,
                                    RelicSpawnContext ctx) noexcept {
    const int p = pool_index(tier);
    if (p < 0) {
        return RelicId::NONE;
    }
    if (rs.relic_pool_count[p] == 0) {
        switch (tier) {
            case RelicTier::COMMON:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::UNCOMMON:
                return return_random_relic_key(rs, RelicTier::RARE, ctx);
            case RelicTier::RARE:
                return RelicId::CIRCLET;
            case RelicTier::SHOP:
                return return_random_relic_key(rs, RelicTier::UNCOMMON, ctx);
            case RelicTier::BOSS:
                return RelicId::CIRCLET;
            default:
                return RelicId::NONE;
        }
    }
    // The Java boss "end" path is intentionally front-pop too.
    const RelicId id = tier == RelicTier::BOSS ? remove_front(rs, p)
                                                : remove_end(rs, p);
    return relic_can_spawn(id, ctx)
               ? id
               : return_end_random_relic_key(rs, tier, ctx);
}

bool relic_acquire_legal(const RunState& rs, RelicId id) noexcept {
    if (relic_def(id) == nullptr) {
        return false;
    }
    if (id == RelicId::CIRCLET) {
        for (uint8_t i = 0; i < rs.relic_count; ++i) {
            const RelicSlot& slot = rs.relics[i];
            if (slot.relic_id ==
                static_cast<uint16_t>(RelicId::CIRCLET)) {
                return slot.counter != std::numeric_limits<int16_t>::max();
            }
        }
    }
    return rs.relic_count < kRelicCap;
}

RelicAcquireResult acquire_relic(RunState& rs, RngStream& misc_rng,
                                 RelicId id) noexcept {
    const RelicDef* def = relic_def(id);
    if (def == nullptr) {
        return RelicAcquireResult::INVALID_ID;
    }

    if (id == RelicId::CIRCLET) {
        for (uint8_t i = 0; i < rs.relic_count; ++i) {
            RelicSlot& slot = rs.relics[i];
            if (slot.relic_id != static_cast<uint16_t>(RelicId::CIRCLET)) {
                continue;
            }
            if (slot.counter == std::numeric_limits<int16_t>::max()) {
                return RelicAcquireResult::COUNTER_CAP_REACHED;
            }
            ++slot.counter;
            return RelicAcquireResult::CIRCLET_STACKED;
        }
    }

    if (rs.relic_count >= kRelicCap) {
        return RelicAcquireResult::RELIC_CAP_REACHED;
    }

    RelicSlot& slot = rs.relics[rs.relic_count++];
    slot.relic_id = static_cast<uint16_t>(id);
    slot.counter = def->initial_counter;

    // AbstractRelic.instantObtain/obtain (AbstractRelic.java:219-291) append in
    // acquisition order and THEN call onEquip, so the handler sees its own slot
    // already present and counter-seeded.
    const RelicOnEquipFn fn = relic_on_equip_fn(id);
    if (fn != nullptr) {
        fn(rs, misc_rng, slot);
    }
    return RelicAcquireResult::ACQUIRED;
}

bool lose_relic(RunState& rs, RelicId id) noexcept {
    // AbstractPlayer.loseRelic (AbstractPlayer.java:2014-2031). The Java loop
    // calls onUnequip on every matching copy and keeps the LAST match as the
    // one to remove; no S1 relic overrides onUnequip, so only the removal is
    // written (see the header note).
    const auto raw = static_cast<uint16_t>(id);
    int last = -1;
    for (uint8_t i = 0; i < rs.relic_count; ++i) {
        if (rs.relics[i].relic_id == raw) {
            last = static_cast<int>(i);
        }
    }
    if (last < 0) {
        return false;  // !hasRelic -> early false, nothing touched.
    }
    for (uint8_t i = static_cast<uint8_t>(last + 1); i < rs.relic_count; ++i) {
        rs.relics[static_cast<uint8_t>(i - 1)] = rs.relics[i];
    }
    --rs.relic_count;
    rs.relics[rs.relic_count] = RelicSlot{};
    return true;
}

// --- onEquip: generated per-relic action dispatch -----------------------------
//
// An onEquip that only initializes the relic's OWN counter (HappyFlower/
// TinyChest/Sundial `this.counter = 0`) is already modelled by the row's
// initial_counter and is deliberately not a handler; nor is a purely cosmetic
// one (Circlet.onEquip is `this.flash()`). See the `pickup:` documentation in
// registry/relics.yaml for that rule.

#define STS_RELIC_ON_EQUIP_DECL(ID, FN) extern RelicOnEquipSig FN;
STS_REGISTRY_RELIC_ON_EQUIP(STS_RELIC_ON_EQUIP_DECL)
#undef STS_RELIC_ON_EQUIP_DECL

RelicOnEquipFn relic_on_equip_fn(RelicId id) noexcept {
    switch (id) {
#define STS_RELIC_ON_EQUIP_CASE(ID, FN) \
    case RelicId::ID:                   \
        return &FN;
        STS_REGISTRY_RELIC_ON_EQUIP(STS_RELIC_ON_EQUIP_CASE)
#undef STS_RELIC_ON_EQUIP_CASE
        default:
            return nullptr;  // no onEquip override on this row
    }
}

}  // namespace sts::engine
