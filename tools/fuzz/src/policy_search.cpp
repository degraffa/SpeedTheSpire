// SIM_SEARCH / SIM_SEARCH_SKIP / SIM_SEARCH_HOLD / SIM_SEARCH_KEYS -- the
// sim-consulting scripted policy (S2.V2), plus its two one-rule variants and
// S3.22's key-seeking one.
//
// WHY THIS EXISTS. s2-design §6's driver-risk paragraph sanctions exactly one
// escalation when the TE.1 survival family cannot reach the S2-G2 depth bars:
// "a sim-consulting scripted driver (shallow rollout/1-ply lookahead using the
// engine itself -- deterministic, weight-free, still TE.1-class behind the
// same seam)". S2.43's breadth wave measured the trigger (0 Act-2 boss fights
// in 2,000 A20 attempts under b1.7.0; sim-side E0 reach past Act 1
// structurally 0), so this file is that driver's DECISION BODY. seed_scan runs
// it at scale to select (seed, policy, policy_seed) triples and emits each
// selected line as a scripted artifact (sts/planner/script.hpp) that the live
// script-following policy replays verbatim.
//
// THE TWO HALVES:
//
//   * COMBAT decisions are a bounded 1-PLY SEARCH over engine snapshots.
//     RunController is trivially copyable by contract (run_advance.hpp), so a
//     candidate move is evaluated by copying the controller, advancing the
//     copy through the candidate plus a deterministic threat-aware static
//     completion of the REST OF THE FIGHT (complete_combat below), and
//     scoring the outcome with an integer evaluation -- effectively "which
//     move ends this fight with the most HP and potions". This is the
//     sanctioned "1-ply lookahead + shallow rollout": one searched ply, a
//     fixed scripted completion, an exact engine preview. The preview is
//     deliberately OMNISCIENT (it sees the copy's draws and monster rolls) --
//     the artifact is a scripted LINE for a capture to replay, not an agent
//     playing under the information contract; GT0's player-information layer
//     is not consulted and not affected.
//
//   * ONE NAMED-MECHANIC RULE INSIDE COMBAT, CARRIED BY A THIRD KIND: the
//     CURIOSITY HOLD (S2.V2's Awakened One discharge, 2026-08-27), charged only
//     by PolicyKind::SIM_SEARCH_HOLD. It is written in R4's never-take
//     tradition -- a checkable criterion that NAMES the mechanic it defends
//     against (Curiosity) -- and it was MEASURED HARMFUL: 5 Awakened One kills
//     against SIM_SEARCH's 22 on the same 1,929-fight grid. It lives here as
//     the executable form of that measurement, not as a default. See "the
//     Curiosity hold" band below for the Java derivation and the A/B.
//
//   * MAP-NODE and EVENT-OPTION decisions consult the sim the same way at
//     floor scale (rollout_floor_and_eval): resolve the rest of the floor
//     behind the candidate with the deterministic completions and score the
//     run (HP, max HP, gold, relics, potions, curses). The remaining
//     RUN-LAYER decisions (rewards, card picks, rest, shop, boss chest) are
//     the b1.7.0 survival heuristics ported from
//     tools/oracle_bridge/driver/greedy_policy.py -- the R1 deck-gate
//     (widened by a standout clause from Act 2), the R2 potion discipline
//     (folded into the search's evaluation), the ACT_PROFILES overlays, and
//     the R4 boss-relic rule with its five-relic never-take list. Each band
//     cites the Python constant it ports; the Python module header carries
//     the captured-campaign evidence behind every rule.
//
// DETERMINISM (the acceptance bar). Every score is integer arithmetic over
// state the engine snapshots; candidate enumeration order is enumerate_moves'
// fixed order; every bound (rollout step budget) is a constant hit in a
// deterministic order; the ONLY stochastic input is the shared one-draw
// tie-break from PolicyRng, exactly one draw per decision like every other
// heuristic policy (policy.cpp policy_pick). No wall clock, no floats, no
// global state -- so a case is a pure function of its CaseId on every
// platform, which `seed_scan --verify-determinism` asserts.

#include "sts/fuzz/policy.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "sts/engine/cards.hpp"
#include "sts/engine/combat_rewards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/event_framework.hpp"
#include "sts/engine/interp.hpp"
#include "sts/engine/map_gen.hpp"
#include "sts/engine/map_rooms.hpp"
#include "sts/engine/neow.hpp"
#include "sts/engine/potions.hpp"
#include "sts/engine/relic_pools.hpp"
#include "sts/engine/resample.hpp"
#include "sts/engine/run_advance.hpp"
#include "sts/engine/shop.hpp"

namespace sts::fuzz {

using engine::Action;
using engine::ActionVerb;
using engine::CardId;
using engine::RelicId;
using engine::resample_hidden;
using engine::RunActionMask;
using engine::RunController;
using engine::RunPhase;
using engine::sampler_rng_from_seed;
using engine::SamplerRng;
using engine::StepResult;
using sts::registry::CardTargetKind;
using sts::registry::CardType;
using sts::registry::Opcode;

namespace {

// --- ACT_PROFILES (greedy_policy.py b1.7.0) ---------------------------------
//
// Per-act overlays over the same numeric constants; act 1 is the module
// constants unchanged (the Python table deliberately has no key 1). The HP
// fractions are integer percentages so no float enters a decision.

struct ActProfile {
    int map_elite;           // MAP_ELITE / ACT_PROFILES[act]["MAP_ELITE"]
    int deck_attack_target;  // DECK_ATTACK_TARGET
    int deck_size_cap;       // DECK_SIZE_CAP
    int potion_low_hp_pct;   // POTION_LOW_HP_FRACTION * 100
};

// NOTE ON MAP_ELITE HERE vs THE PYTHON. Map-node choices in this policy are
// decided by the ONE-FLOOR ROLLOUT (the sim previews the actual fight), so
// the MAP_ELITE band matters only inside nested completions (e.g. the Neow
// rollout's first-floor walk). The act-1 450 keeps the elite door open there;
// the acts-2/3 200 is deliberate -- the rollout, not the band, is what
// decides whether a deep elite is worth fighting. Both settings were measured
// on the 500-seed tuning scan (2026-08-27) before freezing.
[[nodiscard]] constexpr ActProfile act_profile(int act) noexcept {
    if (act >= 3) return {200, 14, 35, 60};
    if (act == 2) return {200, 12, 28, 50};
    return {450, 10, 20, 40};
}

// greedy_policy.py map bands (survival-inverted vs the E0 fuzz heuristics).
constexpr int kMapBoss = 700;
constexpr int kMapNonCombat = 600;
constexpr int kMapMonster = 400;
constexpr int kMapUnknown = 300;
constexpr int kEliteAppetiteHpPct = 60;  // ELITE_APPETITE_HP_FRACTION

// Reward bands (REWARD_* / CARD_REWARD_* / BOSS_RELIC_*).
constexpr int kRewardRelic = 900;
constexpr int kRewardGold = 850;
constexpr int kRewardPotion = 800;
constexpr int kRewardProceed = 100;
constexpr int kRewardCardClosed = 60;
constexpr int kRewardCardTake = 780;
constexpr int kCardTakeOpen = 1000;
constexpr int kCardSkip = 900;
constexpr int kCardSing = 30;
constexpr int kCardRankMax = 200;

constexpr int kBossRelicTake = 900;
constexpr int kBossRelicSkip = 200;      // take cohort: between takeable and avoid
constexpr int kBossRelicSkipWins = 500;  // skip cohort: above every pick
constexpr int kBossRelicAvoid = 100;
constexpr int kBossChestOpen = 100;
constexpr int kBossChestProceed = 5;

// Rest bands (REST_* + REST_HP_FRACTION).
constexpr int kRestPreferred = 700;
constexpr int kRestSecondary = 500;
constexpr int kRestOther = 300;
constexpr int kRestRecall = 250;
constexpr int kRestHpPct = 70;
// The public agent's pre-boss gate (see "the public agent's campfire").
constexpr int kPreBossRestHpPct = 90;

constexpr int kTreasureOpen = 900;
constexpr int kTreasureSkip = 100;
constexpr int kEventNeutral = 500;
constexpr int kShopLeave = 600;
constexpr int kShopOther = 100;
constexpr int kGridPick = 500;
constexpr int kGridCancel = 300;
constexpr int kPotionOutOfCombat = 20;
constexpr int kNeowChoice = 500;
constexpr int kDefaultProceed = 400;

// --- S3.22's key-seeking bands (SIM_SEARCH_KEYS only) ------------------------
//
// Four constants and two HP gates, all dead under every other kind. The bands
// are chosen to DOMINATE THEIR OWN SCREEN and nothing else: a key row beats the
// 900 relic row on a reward screen, RECALL beats the 700 pre-boss rest on a
// campfire menu. Nothing here competes across screens, because run_move_score
// bands only ever compete within one screen's legal set.
//
// K1. The key rows. EMERALD_KEY is free (it is a free-standing row appended
// after the relic, MonsterRoomElite.java:94-98). SAPPHIRE_KEY is NOT: claiming
// it silently destroys the chest's relic row unrewarded (RewardItem.java:
// 317-326), which is why SIM_SEARCH scores both rows kRewardCardClosed and this
// kind is a separate kind rather than a repair. One constant for both, because
// the two rows never co-occur on a screen (one comes from a burning elite, the
// other from a chest).
constexpr int kKeyRowClaim = 1200;
// K2. RECALL, above kRestPreferred (700). The button is offered only while
// !hasRubyKey (CampfireUI.java:94-96, rest_sites.cpp:203), so "exactly one
// campfire on RECALL" is enforced by the GAME, not by a counter here: the
// option stops existing the moment it is taken.
constexpr int kKeyRecall = 750;
// The two HP gates. Both are "while HP allows" read as a percentage of max HP,
// the same currency hp_at_or_below_pct already speaks. Below the gate the rule
// is not merely weakened, it is OFF -- the move scores exactly what SIM_SEARCH
// scores it, so a hurt run behaves identically to the baseline.
constexpr int kKeySeekEliteHpPct = 60;  // K3: the burning elite is a FIGHT
constexpr int kKeyRecallHpPct = 50;     // K2/K4: recall costs the heal
// K3/K4. Map appetite, in the run-evaluation currency the one-floor rollout
// scores in (kEvalPlayerHp == 300 per HP), added to a map candidate's rolled-out
// score. 30,000 = 100 player HP for the burning elite -- enough to outweigh the
// fight's expected HP cost, and far too small to outweigh the rollout's
// kEvalDefeat, so a line that DIES on the elite inside the rollout is still
// rejected. The chest and campfire bonuses are smaller because those rooms are
// free: they only need to break the tie against another non-combat room.
constexpr int64_t kKeyMapEmerald = 30000;
constexpr int64_t kKeyMapEmeraldPath = 8000;
constexpr int64_t kKeyMapChest = 15000;
constexpr int64_t kKeyMapRest = 10000;
// The nested-completion band for the burning elite (see the map arm of
// run_move_score): inside a rollout there is no second rollout to consult, so
// the elite node needs a band, and it sits above kMapBoss (700).
constexpr int kMapEmeraldSeek = 800;

// Which kinds seek keys. SIM_SEARCH / _SKIP / _HOLD / _BLIND never do, so
// their trajectories are bit-for-bit what they were before S3.22.
// SIM_SEARCH_KEYS_DEEP (S3.61) seeks keys identically to SIM_SEARCH_KEYS --
// it differs only in the boss-floor ply depth, gated separately below.
[[nodiscard]] constexpr bool kind_seeks_keys(PolicyKind kind) noexcept {
    return kind == PolicyKind::SIM_SEARCH_KEYS ||
           kind == PolicyKind::SIM_SEARCH_KEYS_DEEP;
}

// S3.61: which kinds get the deepened (3-ply) boss-floor search and the
// widened turn window. Gated separately from `kind_seeks_keys` so a future
// non-key-seeking deep kind could reuse `rollout_boss_deep_and_eval` without
// also picking up K1-K4.
// The A20 public-information agent (SIM_SEARCH_PIMC). Every rule that exists
// to make the public agent stronger is gated on this, never on SIM_SEARCH, so
// the existing kinds stay byte-identical baselines.
[[nodiscard]] constexpr bool is_public_agent(PolicyKind kind) noexcept {
    return kind == PolicyKind::SIM_SEARCH_PIMC;
}

// Particles per decision for SIM_SEARCH_PIMC: STS_PIMC_K, default 16, clamped
// to [1, kPimcMaxWorlds]. Read once per process.
constexpr int kPimcDefaultWorlds = 16;
constexpr int kPimcMaxWorlds = 256;
[[nodiscard]] int pimc_worlds() noexcept {
    static const int k = [] {
        const char* e = std::getenv("STS_PIMC_K");
        int v = e != nullptr ? std::atoi(e) : kPimcDefaultWorlds;
        if (v < 1) v = 1;
        if (v > kPimcMaxWorlds) v = kPimcMaxWorlds;
        return v;
    }();
    return k;
}

[[nodiscard]] constexpr bool kind_deepens_boss(PolicyKind kind) noexcept {
    return kind == PolicyKind::SIM_SEARCH_KEYS_DEEP;
}

// --- evaluation weights (combat search) --------------------------------------
//
// Integer evaluation of a rolled-out state (the rollout horizon is the END of
// the fight, or a bound). The RATIOS are what matters: one point of player HP
// is worth three of
// monster HP (a20 fights are races, but not reckless ones), a live monster is
// worth ten player HP (killing removes all its future turns), and a held
// potion is worth ~13 player HP while R2 says hold -- the same discipline
// greedy_policy's POTION_HOLD encodes, moved into the evaluation where the
// search can weigh drinking against the damage it prevents.
constexpr int64_t kEvalPlayerHp = 300;
constexpr int64_t kEvalMonsterHp = 100;
constexpr int64_t kEvalLiveMonster = 3000;
constexpr int64_t kEvalPotionHeld = 4000;
constexpr int64_t kEvalPotionHeldSpendable = 800;
constexpr int64_t kEvalCombatOver = 1'000'000'000;
constexpr int64_t kEvalDefeat = -1'000'000'000'000;
constexpr int64_t kEvalVictory = 1'000'000'000'000;

// Rollout budget per candidate: enough for the remainder of a long fight
// under the static completion (plays + choices + end-turns); a pathological
// loop or a genuine stalemate exhausts it deterministically and the state is
// scored as-is by the ongoing-combat formula (which penalizes every monster
// still alive, so a stalemate rates far below any win).
constexpr int kRolloutBudget = 400;
constexpr uint16_t kRolloutTurnCap = 20;  // see complete_combat

// --- the Curiosity hold (S2.V2's Awakened One discharge) ---------------------
//
// THE ONE COMBAT RULE THAT NAMES A MECHANIC. S2.V2's reach report closed with
// exactly one unmet S2-G2 item-3 cell: 0 kills in 553 Awakened-One-first Act-3
// boss fights, against 3/585 on the {Time Eater, Donu and Deca} pair. The
// mechanism is not a lottery, it is a rule of this fight:
//
//   CuriosityPower.onUseCard (CuriosityPower.java:42-47) --
//       `if (card.type == AbstractCard.CardType.POWER) { flash();
//        addToBot(new ApplyPowerAction(this.owner, this.owner,
//                 new StrengthPower(this.owner, this.amount), this.amount)); }`
//   -- so every POWER card the PLAYER plays hands the power's OWNER `amount`
//   Strength. The owner is the Awakened One itself
//   (AwakenedOne.usePreBattleAction, AwakenedOne.java:146 grants
//   `new CuriosityPower(this, 2)` at ascension >= 19, :149 grants 1 below it),
//   which is why the two Cultists of encounters.yaml id 58 do NOT enter this
//   rule: Curiosity is on the boss and grants Strength to the boss.
//
//   IT IS PAID FOR THE WHOLE FIGHT, NOT THE PHASE. The Rebirth purge
//   (AwakenedOne.java:302-308) removes Curiosity by name -- so phase 2 stops
//   TAXING -- but Strength is a BUFF and is not in the purge list, so every
//   stack phase 1 bought is still on the boss through phase 2's 320 HP
//   (A_9_STAGE_2_HP, :81, restored by changeState("REBIRTH") :214,:225).
//
//   AND IT IS MULTIPLIED BY THE MULTI-HIT MOVES. Strength is per HIT: phase 1's
//   SOUL_STRIKE is SS_AMT = 4 hits (:89, takeTurn case 2 loops four DamageActions
//   :169-172) and phase 2's TACKLE is TACKLE_AMT = 3 (:100, :199-203). Four is
//   the widest, so one Curiosity stack is worth up to `amount * 4` extra damage
//   on a single boss turn.
//
// THE PRICE THE RULE CHARGES. The hypothesis this rule encodes is that the
// search cannot price the tax itself, because it is priced only inside the
// rollout's own horizon: complete_combat stops at kRolloutTurnCap turns, while
// the fight carries 320 + 320 HP across two phases at A>=9
// (:80-81,:110-114,:214) with 15 Regenerate a turn from A19 (:145). On that
// hypothesis the uncounted part is what compounds, so the hold prices ONE more
// horizon of the tax:
//
//     penalty = amount * kCuriosityMaxHits * kRolloutTurnCap * kEvalPlayerHp
//
// which at the A20 amount of 2 is 2*4*20*300 = 48,000 -- above any ongoing-combat
// score difference (a whole 80 HP player bar is 24,000; a live monster is 3,000),
// and far BELOW kEvalCombatOver (1e9). That ordering is the rule's shape: hold
// Powers, unless playing this one actually ENDS the fight.
//
// AND THE HYPOTHESIS IS FALSE, WHICH IS WHY THIS IS A SEPARATE POLICY KIND.
// SIM_SEARCH's preview is an EXACT, OMNISCIENT engine advance and Curiosity is
// a native power the engine applies inside it, so a rollout that reaches the
// end of the fight -- and these fights end well inside twenty turns -- already
// pays the whole tax. The penalty above therefore double-counts a cost the
// evaluation has already charged, and it suppresses Powers the deck needs. That
// is not a guess: on a paired 110-seed x 1,024 policy-seed grid (112,640 rows
// each, 1,929 Awakened One boss fights each) SIM_SEARCH killed the boss 22
// times and SIM_SEARCH_HOLD 5. The rule is kept, addressable and tested, as the
// executable form of that measurement; the S2-G2 cohort schedules from
// SIM_SEARCH. docs/verification/s2v2-sim-reach.md §6 has the numbers.
//
// THE CRITERION IS "A LIVE MONSTER OWNS CURIOSITY", not "the encounter is
// Awakened One". It is checkable from the board, it is exactly the condition
// under which the mechanic can fire, and it turns itself OFF at the Rebirth --
// phase 2 no longer taxes. Nothing else in the registry ever applies
// PowerId::CURIOSITY (powers.yaml id 108: its only applier is
// AwakenedOne.usePreBattleAction), so even under SIM_SEARCH_HOLD the rule
// cannot change a decision in any other combat -- pinned by
// SimSearchCuriosityHold.NeverFiresOutsideAnAwakenedOneFight.
constexpr int kCuriosityMaxHits = 4;  // SS_AMT (AwakenedOne.java:89)

// Which kinds charge the hold. SIM_SEARCH and SIM_SEARCH_SKIP never do, so
// every one of their decisions is byte-identical to the pre-rule engine.
[[nodiscard]] constexpr bool kind_holds_powers(PolicyKind kind) noexcept {
    return kind == PolicyKind::SIM_SEARCH_HOLD;
}

// The Strength ONE power play would hand a live monster right now: the stack
// amount of the first live monster's CURIOSITY, else 0 (the rule is off).
[[nodiscard]] int curiosity_tax(const engine::CombatState& cs) noexcept {
    for (int i = 0; i < cs.monster_count && i < engine::kMonsterCap; ++i) {
        const engine::MonsterState& mo = cs.monsters[i];
        if (mo.monster_id == 0 || mo.hp <= 0) continue;
        for (uint8_t p = 0; p < mo.power_count && p < engine::kPowerCap; ++p) {
            if (mo.powers[p].power_id ==
                    static_cast<uint16_t>(engine::PowerId::CURIOSITY) &&
                mo.powers[p].amount > 0) {
                return mo.powers[p].amount;
            }
        }
    }
    return 0;
}

// Does this candidate play a POWER-type card out of hand? (The only card type
// CuriosityPower.onUseCard reacts to, :43.)
[[nodiscard]] bool move_plays_a_power(const RunController& rc,
                                      const Move& m) noexcept {
    if (m.cat != MoveCat::PLAY_CARD && m.cat != MoveCat::PLAY_CARD_TARGET) {
        return false;
    }
    const uint8_t slot = engine::action_arg0(m.action);
    if (slot >= rc.combat.hand_count) return false;
    const engine::CardInstance& ci = rc.combat.card_pool[rc.combat.hand[slot]];
    const engine::CardDef* def =
        engine::card_def(static_cast<CardId>(ci.card_id));
    return def != nullptr && def->type == CardType::POWER;
}

[[nodiscard]] int64_t curiosity_penalty_for(const engine::CombatState& cs,
                                            const RunController& rc,
                                            const Move& m) noexcept {
    const int tax = curiosity_tax(cs);
    if (tax <= 0 || !move_plays_a_power(rc, m)) return 0;
    return static_cast<int64_t>(tax) * kCuriosityMaxHits *
           static_cast<int64_t>(kRolloutTurnCap) * kEvalPlayerHp;
}

// --- small helpers -----------------------------------------------------------

[[nodiscard]] bool hp_at_or_below_pct(const RunController& rc, int pct) noexcept {
    const int hp = rc.phase == static_cast<uint8_t>(RunPhase::COMBAT)
                       ? rc.combat.player_hp
                       : rc.run.hp;
    return static_cast<int64_t>(hp) * 100 <=
           static_cast<int64_t>(rc.run.max_hp) * pct;
}

[[nodiscard]] int potion_count(const engine::RunState& rs) noexcept {
    int n = 0;
    for (int i = 0; i < engine::kPotionCap; ++i) {
        if (rs.potions[i] != 0) ++n;
    }
    return n;
}

[[nodiscard]] bool belt_is_full(const engine::RunState& rs) noexcept {
    int n = 0;
    for (int i = 0; i < engine::kPotionCap; ++i) {
        if (rs.potions[i] != 0) ++n;
    }
    return n >= rs.potion_slots;
}

// R2 (greedy_policy.potion_worth_spending): the contexts in which a potion is
// SPENDABLE -- boss/elite rooms, any combat from act 3 on
// (POTION_HIGH_STAKES_FROM_ACT), low HP per the act profile, or a full belt.
[[nodiscard]] bool potion_worth_spending(const RunController& rc) noexcept {
    const auto room = static_cast<engine::RoomType>(rc.room_type);
    if (room == engine::RoomType::Boss || room == engine::RoomType::Elite) {
        return true;
    }
    if (rc.run.act >= 3 &&
        (room == engine::RoomType::Monster || room == engine::RoomType::Event)) {
        return true;
    }
    if (hp_at_or_below_pct(rc, act_profile(rc.run.act).potion_low_hp_pct)) {
        return true;
    }
    return belt_is_full(rc.run);
}

// --- card ranking (run layer) ------------------------------------------------
//
// greedy_policy._card_reward_rank ported onto the registry tables: static
// damage + block sums at the instance's upgrade level, the ATTACK bonus
// (CARD_RANK_ATTACK_BONUS), the AOE bonus, curses at zero, clamped to
// CARD_RANK_MAX so the take band stays bounded.
struct StaticCardScore {
    int damage = 0;
    int block = 0;
};

[[nodiscard]] StaticCardScore static_card_score(const engine::CardDef& def,
                                                uint8_t upgrade) noexcept {
    StaticCardScore sc;
    const engine::CardEffectView v = engine::card_effect_steps(def, upgrade);
    for (uint8_t i = 0; i < v.count; ++i) {
        const engine::CardEffectStep& st = v.steps[i];
        switch (st.op) {
            case Opcode::DAMAGE:
            case Opcode::DAMAGE_STR_MULT:
            case Opcode::DAMAGE_PER_STRIKE:
            case Opcode::DAMAGE_UPGRADE_SCALE:
            case Opcode::DAMAGE_RAMPAGE:
                sc.damage += st.amount;
                break;
            case Opcode::BLOCK:
                sc.block += st.amount;
                break;
            default:
                break;
        }
    }
    return sc;
}

[[nodiscard]] int card_rank(uint16_t card_id, uint8_t upgrade) noexcept {
    const engine::CardDef* def =
        engine::card_def(static_cast<CardId>(card_id));
    if (def == nullptr) return 0;
    if (def->type == CardType::CURSE || def->type == CardType::STATUS) return 0;
    // POWERS rank at a flat 110 -- a deliberate extension of the Python rank
    // (whose damage/block sum scores every power 0, so the b1.7.0 driver
    // never takes one): a scaling deck is what pays an Act-2/3 boss, and 110
    // sits above the plain-attack band (a 20-damage attack ranks 80) while a
    // genuinely big attack still outranks it.
    if (def->type == CardType::POWER) return 110;
    const StaticCardScore sc = static_card_score(*def, upgrade);
    int rank = sc.damage * 2 + sc.block * 3;
    if (engine::card_target_kind(*def, upgrade) == CardTargetKind::ALL_ENEMY) {
        rank += sc.damage;  // AOE: the split/multi fights double the payload
    }
    if (def->type == CardType::ATTACK) rank += 15;
    if (rank < 0) rank = 0;
    if (rank > kCardRankMax) rank = kCardRankMax;
    return rank;
}

// The best rank among a CARDS reward row's offer -- the second clause of the
// widened R1 gate below.
[[nodiscard]] int best_offered_rank(const engine::RunRewardItem& item) noexcept {
    int best = 0;
    for (uint8_t j = 0; j < item.card_count && j < engine::kRewardCardCap; ++j) {
        const int r = card_rank(item.card_ids[j], item.card_upgrades[j]);
        if (r > best) best = r;
    }
    return best;
}

// R1's gate (greedy_policy.wants_card_reward), WIDENED by one clause. The
// Python gate is a function of the deck alone: short of attacks AND under the
// size cap (both act-resolved). This port adds: a STANDOUT offer (best
// offered rank >= kStandoutRank -- a power, or a genuinely big attack) is
// taken even when the attack quota is met, because a deck of plain attacks
// demonstrably cannot pay an Act-2 boss. The two-screens invariant that
// motivated deck-only gating is preserved BY CONSTRUCTION with the wider
// input: the claim row and the pick screen it opens read the same
// rc.rewards item, which cannot change between the two decisions, so the
// open is still always followed by a take and the row is always retired.
constexpr int kStandoutRank = 100;

[[nodiscard]] bool wants_card_from(const RunController& rc,
                                   const engine::RunRewardItem* item) noexcept {
    const ActProfile p = act_profile(rc.run.act);
    const int deck = rc.run.master_deck_count;
    if (deck <= 0 || deck >= p.deck_size_cap) return false;
    int attacks = 0;
    for (int i = 0; i < deck && i < engine::kMasterDeckCap; ++i) {
        const engine::CardDef* def = engine::card_def(
            static_cast<CardId>(rc.run.master_deck[i].card_id));
        if (def != nullptr && def->type == CardType::ATTACK) ++attacks;
    }
    if (attacks < p.deck_attack_target) return true;
    // The standout clause opens from Act 2: an Act-1 deck wants to stay lean
    // (the measured Act-1 boss reach dropped ~7 points when standouts were
    // taken from floor 1), while an Act-2/3 deck needs the scaling.
    return rc.run.act >= 2 && item != nullptr &&
           best_offered_rank(*item) >= kStandoutRank;
}

// The open card-pick item, or nullptr.
[[nodiscard]] const engine::RunRewardItem* open_card_item(
    const RunController& rc) noexcept {
    if (rc.rewards.open_card_item == engine::kNoOpenCardReward ||
        rc.rewards.open_card_item >= engine::kRewardItemCap) {
        return nullptr;
    }
    return &rc.rewards.items[rc.rewards.open_card_item];
}

// A1 (greedy_policy.elite_map_value): the raised elite appetite applies only
// while the run is above ELITE_APPETITE_HP_FRACTION; hurt runs keep the
// Python's avoidance value (MAP_ELITE = 200, kept here as the floor).
constexpr int kMapEliteAvoid = 200;

[[nodiscard]] int elite_map_value(const RunController& rc) noexcept {
    const int raised = act_profile(rc.run.act).map_elite;
    if (raised <= kMapEliteAvoid) return raised;
    if (hp_at_or_below_pct(rc, kEliteAppetiteHpPct)) {
        return kMapEliteAvoid;
    }
    return raised;
}

// R4's never-take list (greedy_policy.BOSS_RELIC_NEVER_TAKE): the five BOSS
// relics that each invalidate a rule this policy family owns -- Sozu (R2 has
// no potions left to decide), Runic Dome (R3's attacker count reads zero
// forever), Snecko Eye (the cost columns stop describing the hand), Pandora's
// Box (R1 counts a rewritten deck), Calling Bell (the modal three-relic
// screen). The criterion is "names a rule", which is checkable.
[[nodiscard]] bool boss_relic_is_takeable(uint16_t relic_id) noexcept {
    switch (static_cast<RelicId>(relic_id)) {
        case RelicId::SOZU:
        case RelicId::RUNIC_DOME:
        case RelicId::SNECKO_EYE:
        case RelicId::PANDORAS_BOX:
        case RelicId::CALLING_BELL:
            return false;
        default:
            return true;
    }
}

// Destination room type for a MAP_NODE move (same derivation as policy.cpp's
// map_move_room: at MAP_CHOICE the destination row is the current floor).
[[nodiscard]] engine::RoomType map_dest_room(const RunController& rc,
                                             const Move& m) noexcept {
    if (m.cat == MoveCat::MAP_BOSS) return engine::RoomType::Boss;
    const int col = engine::action_arg0(m.action);
    // act_floor_base_of, NOT act_floor_base(act) -- Act 4's base is the
    // A20-dependent byte the crossing wrote (52 at A20, 51 below it), so the
    // int overload's flat 51 reads the Act-4 map one row too deep at A20.
    // Behaviourally inert on the lines measured so far (every Act-4 row this
    // engine builds offers exactly ONE map node, so the valuation cannot
    // change the pick -- proved by re-emitting S3.61's two Act-4 lines across
    // this change: identical final_hash, identical action list), but it is the
    // same off-by-one S3.62 caught in the script emitter and it is wrong here
    // for the same reason.
    const int row = static_cast<int>(rc.run.floor) -
                    engine::act_floor_base_of(rc.run);
    if (row < 0 || row >= engine::kMapRows || col < 0 ||
        col >= engine::kMapCols) {
        return engine::RoomType::None;
    }
    return static_cast<engine::RoomType>(
        rc.run.map[engine::run_state_map_index(col, row)].room_type);
}

// Is this map candidate's DESTINATION the act's burning-elite node? The engine's
// own predicate (`on_emerald_elite_node`, run_advance.hpp) answers the question
// for the node the controller is STANDING on; a map choice is made one room
// early, so the same comparison is made against the candidate's (col, row) in
// the same coordinate space map_dest_room uses -- at MAP_CHOICE the destination
// row is `floor - act_floor_base_of(rs)`, which is exactly `run_cur_row + 1` once
// the move is taken. kNoEmeraldNode means the act placed no burning elite (the
// key is already held, or the act has no elites).
[[nodiscard]] bool map_dest_is_emerald_node(const RunController& rc,
                                            const Move& m) noexcept {
    if (m.cat != MoveCat::MAP_NODE) return false;
    if (rc.emerald_x == engine::kNoEmeraldNode) return false;
    const int col = engine::action_arg0(m.action);
    const int row = static_cast<int>(rc.run.floor) -
                    engine::act_floor_base_of(rc.run);
    return col == static_cast<int>(rc.emerald_x) &&
           row == static_cast<int>(rc.emerald_y);
}

// Can the burning-elite node still be REACHED from a candidate destination?
//
// WHY THIS EXISTS AND IS NOT A STEERING HEURISTIC. The emerald node sits at one
// (column, row); a map choice moves one row at a time along real edges, so an
// opportunistic "is the candidate the emerald node" preference only ever fires
// on the single floor below it, and on most maps the run has already walked
// into a column that cannot reach it. The map is a 15x7 DAG whose edges are two
// bits per node (map_gen.hpp kEdgeLeft/Center/Right), so exact forward
// reachability is a row-by-row frontier propagation over at most 15 rows of 7
// bits -- cheaper than one rollout step and, unlike a "move towards the column"
// heuristic, it is never wrong.
//
// It is a CONSERVATIVE test: Wing Boots lets the run jump to an unconnected
// node (run_advance.cpp:2467-2490), which can only ADD reachability, so a false
// negative costs an opportunity and a false positive is impossible.
[[nodiscard]] bool node_reaches_emerald(const RunController& rc, int col,
                                        int row) noexcept {
    if (rc.emerald_x == engine::kNoEmeraldNode) return false;
    const int ex = static_cast<int>(rc.emerald_x);
    const int ey = static_cast<int>(rc.emerald_y);
    if (row > ey || row < 0 || row >= engine::kMapRows) return false;
    if (col < 0 || col >= engine::kMapCols) return false;
    uint8_t frontier = static_cast<uint8_t>(1u << col);
    for (int y = row; y < ey; ++y) {
        uint8_t next = 0;
        for (int x = 0; x < engine::kMapCols; ++x) {
            if ((frontier & static_cast<uint8_t>(1u << x)) == 0) continue;
            const uint8_t e =
                rc.run.map[engine::run_state_map_index(x, y)].edges;
            if ((e & engine::kEdgeLeft) != 0 && x > 0) {
                next = static_cast<uint8_t>(next | (1u << (x - 1)));
            }
            if ((e & engine::kEdgeCenter) != 0) {
                next = static_cast<uint8_t>(next | (1u << x));
            }
            if ((e & engine::kEdgeRight) != 0 && x + 1 < engine::kMapCols) {
                next = static_cast<uint8_t>(next | (1u << (x + 1)));
            }
        }
        frontier = next;
        if (frontier == 0) return false;
    }
    return (frontier & static_cast<uint8_t>(1u << ex)) != 0;
}

// --- the public agent's path rule: avoid elites --------------------------------
//
// WHY. The map decision is a one-room rollout, which cannot see that a column
// funnels into an elite two rooms later -- and at A20 an Act-1 elite is where
// the PIMC agent's runs end: in the dev-seed trace 219 of its 298 pre-boss
// deaths were in an elite room (72 of them the first elite row, floor 6), and
// relic count at the boss did not predict a boss kill at all (33% / 36% / 33%
// for 2 / 3 / 4+ relics). An elite is a large risk paid for a reward this
// policy cannot cash before the Act-1 boss. So the public agent plans over the
// PUBLIC map (room types and edges are on screen; what a ? or a fight holds
// is not) and walks the path with the FEWEST elites to the act's last row;
// the one-room rollout then chooses among the columns that tie on that count.
// Measured on the 500-seed dev screen: 133 -> 171 Act-1 boss kills, with boss
// reach 75% -> 86%.
//
// The penalty is per elite and dominates any rollout difference -- including
// a rollout death (kEvalDefeat averaged over the particles is at most 1e12),
// because the immediate room's death in a greedy-completion particle is a far
// weaker signal than an elite on every remaining path.
constexpr int64_t kElitePathPenalty = 10'000'000'000'000;

// The minimum number of Elite rooms on any map path from each node through
// the act's last row (the node's own room included). Exact backward DP over
// the 15x7 DAG (map_gen.hpp's two-bit edges, the same walk
// node_reaches_emerald does forward); a node with no way forward counts as
// unreachable. Wing Boots can only ADD paths, so a count is an upper bound,
// never an undercount.
constexpr int kPathUnreachable = engine::kMapRows + 1;
using MapGrid = int[engine::kMapRows][engine::kMapCols];

// Calls `visit(nx)` for each in-grid child column of node (x, y).
template <typename Visit>
void for_each_child(const RunController& rc, int x, int y,
                    Visit&& visit) noexcept {
    const uint8_t e = rc.run.map[engine::run_state_map_index(x, y)].edges;
    if ((e & engine::kEdgeLeft) != 0 && x > 0) visit(x - 1);
    if ((e & engine::kEdgeCenter) != 0) visit(x);
    if ((e & engine::kEdgeRight) != 0 && x + 1 < engine::kMapCols) visit(x + 1);
}

[[nodiscard]] engine::RoomType node_room(const RunController& rc, int x,
                                         int y) noexcept {
    return static_cast<engine::RoomType>(
        rc.run.map[engine::run_state_map_index(x, y)].room_type);
}

void min_elite_table(const RunController& rc, MapGrid& best) noexcept {
    for (int y = engine::kMapRows - 1; y >= 0; --y) {
        for (int x = 0; x < engine::kMapCols; ++x) {
            const engine::RoomType rt = node_room(rc, x, y);
            if (rt == engine::RoomType::None) {
                best[y][x] = kPathUnreachable;
                continue;
            }
            const int self = rt == engine::RoomType::Elite ? 1 : 0;
            if (y == engine::kMapRows - 1) {
                best[y][x] = self;
                continue;
            }
            int next = kPathUnreachable;
            for_each_child(rc, x, y, [&](int nx) {
                if (best[y + 1][nx] < next) next = best[y + 1][nx];
            });
            best[y][x] = next >= kPathUnreachable ? kPathUnreachable
                                                  : next + self;
        }
    }
}

[[nodiscard]] int64_t public_path_penalty(PolicyKind kind,
                                          const RunController& rc,
                                          const Move& m) noexcept {
    if (!is_public_agent(kind) || m.cat != MoveCat::MAP_NODE) return 0;
    const int col = engine::action_arg0(m.action);
    const int row =
        static_cast<int>(rc.run.floor) - engine::act_floor_base_of(rc.run);
    if (row < 0 || row >= engine::kMapRows || col < 0 ||
        col >= engine::kMapCols) {
        return 0;
    }
    MapGrid elites;
    min_elite_table(rc, elites);
    return static_cast<int64_t>(elites[row][col]) * kElitePathPenalty;
}

// --- the public agent's path value: expected boss HP over the public map -----
//
// WHY. The elite rule above fixes WHICH elites to fight (as few as the map
// allows), but among the paths that tie on it the one-room rollout still
// walks blind: it cannot see that a column leads into three hallway fights at
// 20% HP, or that the other one reaches a campfire first -- and 97 of the 161
// pre-boss deaths left in the dev trace were hallway fights entered at a
// median ~15% HP, 62 of them with no alternative left on the map. Boss
// outcomes say what the walk should buy instead: in the same trace the
// Act-1 boss kill rate climbs ~1.1 points per HP at the boss door (12% below
// 40 HP, 49% above 70), and with each campfire on the way (31% / 41% / 47% /
// 68% for 1 / 2 / 3 / 4 mid-act campfires) and the first shop (35% -> 46%).
//
// HOW. A deterministic HP walk over the PUBLIC map -- room types and edges
// only; what a ? or a fight holds is hidden and enters as a dev-trace mean.
// Per room: a hallway fight costs kPathHallwayLossHp, an elite
// kPathEliteLossHp, a ? room gains kPathEventGainHp, a campfire heals
// rest_heal_amount under the same gate public_rest_wanted applies (else it
// smiths, worth kPathSmithValueHp), a shop is worth kPathShopValueHp. A fight
// entered below kPathRiskSpan times its mean loss survives with odds falling
// linearly to zero at 0 HP. A path's value is its survival odds times (the
// HP left at the boss door above kPathBossHpFloor plus the rooms' worth), and
// a node's value is its best continuation's, restricted to the continuations
// that keep the act's minimum elite count -- the elite rule decides first.
// The values are an exact backward DP over (row, column, HP).
//
// The candidate's OWN room is still the particles' rollout (what it actually
// does to the run, death included); the walk starts from each particle's HP
// after it, and replaces the rollout's flat per-HP term (kEvalPlayerHp per
// point) with the same weight on the walk's expected boss HP. Measured on the
// 1000 dev seeds against the elite rule alone: 346 -> 384 Act-1 boss kills
// (boss reach 84% -> 88%). Three extensions of the same walk were screened on
// 500 seeds and left out as no better: deciding rest-vs-smith by the walk
// (200 -> 203), adding the walk to event options (200 -> 191), and pricing
// elites INTO the walk (relic worth 8 / 15 / 25 HP, no elite rule) in place
// of the elite rule (203 / 205 / 197 -- more elites, more relics, a higher
// boss win rate and a lower boss reach that cancel out).
constexpr int kPathHallwayLossHp = 8;   // dev-trace mean, floors 5-15
constexpr int kPathEliteLossHp = 25;    // dev-trace mean over survivors
constexpr int kPathEventGainHp = 2;     // dev-trace mean: +2.4..+3.2
constexpr int kPathRiskSpan = 3;        // 3x the mean loss: ~the p95 loss
constexpr int kPathBossHpFloor = 20;    // ~no boss kill below this
constexpr int kPathSmithValueHp = 4;    // one upgrade, in boss-door HP
constexpr int kPathShopValueHp = 6;     // one shop visit, in boss-door HP
constexpr int kPathHpCap = 255;         // table bound; max HP is clamped to it
constexpr int32_t kPathMilli = 1000;    // values and odds are fixed-point
constexpr int32_t kPathDead = -1;       // no continuation from this cell

using PathRow = int32_t[engine::kMapCols][kPathHpCap + 1];

// The expected boss-door value (fixed-point HP) of STANDING on (col, row) --
// its room already resolved -- with `hp` left, over the best continuation
// that keeps the minimum elite count. `elites` is min_elite_table's output.
[[nodiscard]] int64_t path_hp_value(const RunController& rc,
                                    const MapGrid& elites, int col, int row,
                                    int hp) noexcept {
    const int max_hp = rc.run.max_hp < kPathHpCap ? rc.run.max_hp : kPathHpCap;
    const auto clamp_hp = [max_hp](int h) {
        return h < 1 ? 1 : (h > max_hp ? max_hp : h);
    };
    const auto boss_door = [](int h) -> int32_t {
        return h > kPathBossHpFloor ? (h - kPathBossHpFloor) * kPathMilli : 0;
    };
    hp = clamp_hp(hp);
    if (row >= engine::kMapRows - 1) return boss_door(hp);
    // Coffee Dripper / Fusion Hammer (which veto a campfire button) are
    // deliberately NOT modelled: vetoing the walk's heal / smith with them
    // measured 200 -> 195 on the 500-seed screen, i.e. no better than this.
    const int heal = engine::rest_heal_amount(rc.run);
    // odds (fixed-point) of surviving a fight with mean loss `mean` at h HP.
    const auto survive = [](int h, int mean) -> int32_t {
        const int safe = mean * kPathRiskSpan;
        return h >= safe ? kPathMilli : h * kPathMilli / safe;
    };
    // next = values of ENTERING row r+1, cur = row r being filled.
    PathRow buf_a;
    PathRow buf_b;
    int32_t(*next)[kPathHpCap + 1] = buf_a;
    int32_t(*cur)[kPathHpCap + 1] = buf_b;
    for (int r = engine::kMapRows - 1; r > row; --r) {
        for (int x = 0; x < engine::kMapCols; ++x) {
            const engine::RoomType rt = node_room(rc, x, r);
            const int self = rt == engine::RoomType::Elite ? 1 : 0;
            for (int h = 0; h <= max_hp; ++h) {
                if (rt == engine::RoomType::None ||
                    elites[r][x] >= kPathUnreachable) {
                    cur[x][h] = kPathDead;
                    continue;
                }
                int32_t odds = kPathMilli;
                int after = h;
                int worth = 0;
                switch (rt) {
                    case engine::RoomType::Monster:
                        odds = survive(h, kPathHallwayLossHp);
                        after = h - kPathHallwayLossHp;
                        break;
                    case engine::RoomType::Elite:
                        odds = survive(h, kPathEliteLossHp);
                        after = h - kPathEliteLossHp;
                        break;
                    case engine::RoomType::Event:
                        after = h + kPathEventGainHp;
                        break;
                    case engine::RoomType::Shop:
                        worth = kPathShopValueHp;
                        break;
                    case engine::RoomType::Rest: {
                        const int gate = r == engine::kMapRows - 1
                                             ? kPreBossRestHpPct
                                             : kRestHpPct;
                        if (h * 100 <= max_hp * gate) {
                            after = h + heal;
                        } else {
                            worth = kPathSmithValueHp;
                        }
                        break;
                    }
                    default:
                        break;
                }
                after = clamp_hp(after);
                int32_t cont = kPathDead;
                if (r == engine::kMapRows - 1) {
                    cont = boss_door(after);
                } else {
                    for_each_child(rc, x, r, [&](int nx) {
                        if (elites[r + 1][nx] + self != elites[r][x]) return;
                        if (next[nx][after] > cont) cont = next[nx][after];
                    });
                }
                cur[x][h] =
                    cont == kPathDead
                        ? kPathDead
                        : static_cast<int32_t>(
                              static_cast<int64_t>(odds) *
                              (cont + worth * kPathMilli) / kPathMilli);
            }
        }
        int32_t(*swap)[kPathHpCap + 1] = next;
        next = cur;
        cur = swap;
    }
    // `next` now holds row + 1: the standing node's best continuation.
    const int self = node_room(rc, col, row) == engine::RoomType::Elite ? 1 : 0;
    int32_t best = kPathDead;
    for_each_child(rc, col, row, [&](int nx) {
        if (elites[row + 1][nx] + self != elites[row][col]) return;
        if (next[nx][hp] > best) best = next[nx][hp];
    });
    return best == kPathDead ? 0 : best;
}

// The walk's term for one rolled-out map candidate (zero for every other
// kind, and for a particle that did not come back to a map choice in the same
// act -- dead, or past the boss): the rollout's flat HP term swapped for the
// expected boss-door HP of the best continuation from where it stands.
[[nodiscard]] int64_t public_path_hp_term(PolicyKind kind,
                                          const RunController& rc,
                                          const Move& m,
                                          const RunController& after) noexcept {
    if (!is_public_agent(kind) || m.cat != MoveCat::MAP_NODE) return 0;
    if (after.phase != static_cast<uint8_t>(RunPhase::MAP_CHOICE) ||
        after.run.act != rc.run.act) {
        return 0;
    }
    const int col = engine::action_arg0(m.action);
    const int row =
        static_cast<int>(rc.run.floor) - engine::act_floor_base_of(rc.run);
    if (row < 0 || row >= engine::kMapRows || col < 0 ||
        col >= engine::kMapCols) {
        return 0;
    }
    MapGrid elites;
    min_elite_table(rc, elites);
    const int64_t door = path_hp_value(after, elites, col, row, after.run.hp);
    return door * kEvalPlayerHp / kPathMilli -
           static_cast<int64_t>(after.run.hp) * kEvalPlayerHp;
}

// K3/K4 (S3.22): the bounded key appetite added to a map candidate's ROLLED-OUT
// score in sim_search_pick. Zero for every other kind, for every key already
// held, and below each rule's HP gate. It is added AFTER the rollout rather
// than folded into run_layer_eval on purpose: the rollout keeps pricing the
// fight (and death) exactly as the baseline does, and this term is a clearly
// bounded preference laid on top of that valuation rather than a change to it.
[[nodiscard]] int64_t key_map_bonus(PolicyKind kind, const RunController& rc,
                                    const Move& m) noexcept {
    if (!kind_seeks_keys(kind)) return 0;
    if (m.cat != MoveCat::MAP_NODE) return 0;  // the boss edge is never a key
    const uint8_t keys = rc.run.keys;
    const bool want_emerald = (keys & engine::kKeyEmerald) == 0 &&
                              !hp_at_or_below_pct(rc, kKeySeekEliteHpPct);
    if (map_dest_is_emerald_node(rc, m)) {
        return want_emerald ? kKeyMapEmerald : 0;
    }
    if (want_emerald) {
        const int col = engine::action_arg0(m.action);
        const int row = static_cast<int>(rc.run.floor) -
                        engine::act_floor_base_of(rc.run);
        // The APPROACH band: this candidate keeps the burning elite reachable.
        // Much smaller than standing on it, because it only has to break a tie
        // between two otherwise comparable columns -- an approach that costs
        // real HP in the rollout is still refused.
        if (node_reaches_emerald(rc, col, row)) return kKeyMapEmeraldPath;
    }
    switch (map_dest_room(rc, m)) {
        case engine::RoomType::Treasure:
            // The sapphire row is appended to every chest open while the key is
            // unheld (AbstractChest.java:95-97 -> AbstractRoom.java:545-547).
            return (keys & engine::kKeySapphire) == 0 ? kKeyMapChest : 0;
        case engine::RoomType::Rest:
            return (keys & engine::kKeyRuby) == 0 &&
                           !hp_at_or_below_pct(rc, kKeyRecallHpPct)
                       ? kKeyMapRest
                       : 0;
        default:
            return 0;
    }
}

// Grid-pick direction: does this master-deck grid want the WORST card (purge /
// transform) or the BEST (upgrade / duplicate)?
enum class GridWants : uint8_t { WORST, BEST };

[[nodiscard]] int grid_pick_score(const RunController& rc, uint8_t deck_index,
                                  GridWants wants) noexcept {
    if (deck_index >= rc.run.master_deck_count ||
        deck_index >= engine::kMasterDeckCap) {
        return kGridPick;
    }
    const engine::CardInstance& ci = rc.run.master_deck[deck_index];
    const int rank = card_rank(ci.card_id, ci.upgrade);
    return wants == GridWants::BEST ? kGridPick + rank
                                    : kGridPick + (kCardRankMax - rank);
}

// --- the public agent's shop (SIM_SEARCH_PIMC only) --------------------------
//
// WHY THIS EXISTS. The b1.7.0 band buys nothing (kShopLeave beats every row),
// and a trace of the PIMC agent over the dev seeds reached its Act-1 boss
// carrying ~270 unspent gold and ~1 potion on average. Gold is worth nothing
// at a boss; what it buys on the way there is. So the public agent shops.
//
// HOW. Every purchasable row is public -- the shelves, their prices and the
// purge cost are on screen -- and each gets a value in POINTS (roughly player
// HP-equivalents of fight strength). The screen then solves the whole basket:
// the subset of rows that maximizes total value minus the gold's own worth
// (kShopGoldPerPoint), under the gold on hand and the free potion slots, and
// buys that basket's most valuable row. The next decision re-solves over what
// is left, so a purchase that changes the picture (a Courier restock, a purge
// that ramps its own cost) is priced in. The basket is solved exhaustively:
// the shop has at most kShopPlanMaxRows rows, so 2^14 subsets is a hard
// ceiling, and the usual affordable set is a handful of rows.
//
// The values are a small hand table, not a measurement of each item -- the
// coarse ordering (a curse purge and a boss-fight potion over a Strike purge,
// most relics over most potions per row, dead-weight relics never) is what
// the gold-to-boss-kill effect comes from. Measured over the 1000 dev seeds
// against the buy-nothing band: 190 -> 242 Act-1 boss kills. The potion level
// was screened both ways on 500 seeds (every potion +15 points: 166 -> 157;
// -6 points: 166 -> 146), so potions stay level with a Strike purge and below
// a relic.
constexpr int kShopBuy = 700;            // a planned row: above kShopLeave
constexpr int kShopGoldPerPoint = 25;    // gold's own worth when kept
constexpr int kShopPlanMaxRows = engine::kShopItemCount + 1;  // + the purge

// Potions: what one drink is worth in the boss / elite fight R2 spends it in.
constexpr int kShopPotionDefault = 12;
constexpr int kShopPotionStrong = 16;  // a whole-fight effect (Str, Ritual...)
constexpr int kShopPotionRevive = 25;  // Fairy in a Bottle: a second life
constexpr int kShopPotionWeak = 4;     // random / setup-dependent effects

[[nodiscard]] int shop_potion_value(uint16_t potion_id) noexcept {
    using engine::PotionId;
    switch (static_cast<PotionId>(potion_id)) {
        case PotionId::FAIRY_POTION:
            return kShopPotionRevive;
        case PotionId::STRENGTH_POTION:
        case PotionId::CULTIST_POTION:
        case PotionId::HEART_OF_IRON:
        case PotionId::ENTROPIC_BREW:
        case PotionId::FEAR_POTION:
            return kShopPotionStrong;
        case PotionId::GAMBLERS_BREW:
        case PotionId::ELIXIR:
        case PotionId::LIQUID_MEMORIES:
        case PotionId::SNECKO_OIL:
            return kShopPotionWeak;
        case PotionId::SMOKE_BOMB:
            return 0;  // escapes a fight; a boss cannot be escaped
        default:
            return kShopPotionDefault;
    }
}

// Relics: one flat value with a dead-weight list. The dead list is "does
// nothing for THIS policy": gold relics (the agent now spends its gold),
// campfire options the rest band never takes (Girya / Peace Pipe / Shovel),
// the Singing Bowl (kCardSing loses to every take), and the shelf-changers
// whose payoff is more shopping or off-colour cards.
constexpr int kShopRelicValue = 20;

[[nodiscard]] int shop_relic_value(uint16_t relic_id) noexcept {
    switch (static_cast<RelicId>(relic_id)) {
        case RelicId::MAW_BANK:
        case RelicId::OLD_COIN:
        case RelicId::CERAMIC_FISH:
        case RelicId::SMILING_MASK:
        case RelicId::GIRYA:
        case RelicId::PEACE_PIPE:
        case RelicId::SHOVEL:
        case RelicId::SINGING_BOWL:
        case RelicId::PRISMATIC_SHARD:
        case RelicId::MEMBERSHIP_CARD:
        case RelicId::THE_COURIER:
        case RelicId::MEDICAL_KIT:
        case RelicId::ORRERY:
        case RelicId::CHEMICAL_X:
        case RelicId::DARKSTONE_PERIAPT:
        case RelicId::DU_VU_DOLL:
        case RelicId::BLUE_CANDLE:
        case RelicId::TINY_CHEST:
            return 0;
        default:
            return kShopRelicValue;
    }
}

// Purge targets: a removable curse, then a basic Strike, then a basic Defend.
// The grid pick and the purge row's value read the SAME function, so the
// service is only bought when the card it will remove is worth removing.
constexpr int kPurgeCurse = 30;
constexpr int kPurgeStrike = 12;
constexpr int kPurgeStrikeUpgraded = 8;
constexpr int kPurgeDefend = 4;
constexpr int kPurgeDefendUpgraded = 2;

[[nodiscard]] int purge_target_value(const engine::CardInstance& ci) noexcept {
    const engine::CardDef* def =
        engine::card_def(static_cast<CardId>(ci.card_id));
    if (def == nullptr) return 0;
    if (def->type == CardType::CURSE) return kPurgeCurse;
    switch (static_cast<CardId>(ci.card_id)) {
        case CardId::STRIKE:
            return ci.upgrade != 0 ? kPurgeStrikeUpgraded : kPurgeStrike;
        case CardId::DEFEND:
            return ci.upgrade != 0 ? kPurgeDefendUpgraded : kPurgeDefend;
        default:
            return 0;
    }
}

[[nodiscard]] int best_purge_value(const engine::RunState& rs) noexcept {
    int best = 0;
    for (uint16_t i = 0; i < rs.master_deck_count && i < engine::kMasterDeckCap;
         ++i) {
        if (!engine::master_card_purgeable_unbottled(rs.master_deck[i])) {
            continue;
        }
        const int v = purge_target_value(rs.master_deck[i]);
        if (v > best) best = v;
    }
    return best;
}

// Cards: none bought yet (the reward-pick layer owns deck building).
[[nodiscard]] int shop_card_value(uint16_t /*card_id*/,
                                  uint8_t /*upgrade*/) noexcept {
    return 0;
}

struct ShopRow {
    uint8_t arg;    // the CHOOSE arg0 that buys it
    int price;
    int value;      // points
    bool potion;    // occupies a belt slot
};

// The CHOOSE arg0 of the row to buy next, or kChooseProceed to leave.
[[nodiscard]] uint8_t shop_next_buy(const RunController& rc) noexcept {
    const engine::RunState& rs = rc.run;
    const engine::ShopState& sh = rc.shop;
    ShopRow rows[kShopPlanMaxRows];
    int n = 0;
    const auto add = [&](uint8_t arg, int price, int value, bool potion) {
        if (value <= 0 || n >= kShopPlanMaxRows) return;
        rows[n++] = ShopRow{arg, price, value, potion};
    };
    for (uint8_t i = 0; i < engine::kShopColoredCount; ++i) {
        if (engine::shop_buy_card_legal(rs, sh, i, false)) {
            add(static_cast<uint8_t>(engine::kChooseShopColoredBase + i),
                sh.colored[i].price,
                shop_card_value(sh.colored[i].id, sh.colored[i].upgrade), false);
        }
    }
    for (uint8_t i = 0; i < engine::kShopColorlessCount; ++i) {
        if (engine::shop_buy_card_legal(rs, sh, i, true)) {
            add(static_cast<uint8_t>(engine::kChooseShopColorlessBase + i),
                sh.colorless[i].price,
                shop_card_value(sh.colorless[i].id, sh.colorless[i].upgrade),
                false);
        }
    }
    for (uint8_t i = 0; i < engine::kShopRelicCount; ++i) {
        if (engine::shop_buy_relic_legal(rs, sh, i)) {
            add(static_cast<uint8_t>(engine::kChooseShopRelicBase + i),
                sh.relics[i].price, shop_relic_value(sh.relics[i].id), false);
        }
    }
    for (uint8_t i = 0; i < engine::kShopPotionCount; ++i) {
        if (engine::shop_buy_potion_legal(rs, sh, i)) {
            add(static_cast<uint8_t>(engine::kChooseShopPotionBase + i),
                sh.potions[i].price, shop_potion_value(sh.potions[i].id), true);
        }
    }
    if (engine::shop_purge_legal(rs, sh)) {
        add(engine::kChooseShopPurge, sh.actual_purge_cost,
            best_purge_value(rs), false);
    }
    int free_slots = 0;
    if (engine::shop_potion_slot_available(rs)) {
        for (uint8_t i = 0; i < rs.potion_slots && i < engine::kPotionCap; ++i) {
            if (rs.potions[i] == 0) ++free_slots;
        }
    }
    uint32_t best_mask = 0;
    int64_t best_net = 0;
    const uint32_t subsets = 1u << static_cast<uint32_t>(n);
    for (uint32_t mask = 1; mask < subsets; ++mask) {
        int64_t gold = 0;
        int64_t net = 0;
        int potions = 0;
        for (int i = 0; i < n; ++i) {
            if ((mask & (1u << static_cast<uint32_t>(i))) == 0) continue;
            gold += rows[i].price;
            net += static_cast<int64_t>(rows[i].value) * kShopGoldPerPoint -
                   rows[i].price;
            if (rows[i].potion) ++potions;
        }
        if (gold > rs.gold || potions > free_slots) continue;
        if (net > best_net) {
            best_net = net;
            best_mask = mask;
        }
    }
    uint8_t pick = engine::kChooseProceed;
    int pick_value = 0;
    for (int i = 0; i < n; ++i) {
        if ((best_mask & (1u << static_cast<uint32_t>(i))) == 0) continue;
        if (rows[i].value > pick_value) {
            pick_value = rows[i].value;
            pick = rows[i].arg;
        }
    }
    return pick;
}

[[nodiscard]] int public_shop_score(const RunController& rc,
                                    const Move& m) noexcept {
    const uint8_t arg0 = engine::action_arg0(m.action);
    if (rc.shop.screen ==
        static_cast<uint8_t>(engine::ShopScreenKind::PURGE_GRID)) {
        if (arg0 == engine::kChooseCancelGrid) return kShopOther;
        if (arg0 >= rc.run.master_deck_count) return kShopOther;
        return kShopBuy + purge_target_value(rc.run.master_deck[arg0]);
    }
    if (arg0 == engine::kChooseProceed) return kShopLeave;
    return arg0 == shop_next_buy(rc) ? kShopBuy : kShopOther;
}

// --- the public agent's out-of-combat potions --------------------------------
//
// The inherited band scores an out-of-combat drink kPotionOutOfCombat (20),
// "below any screen move" -- but at a MAP_CHOICE the other candidates carry
// ROLLOUT scores, which go negative whenever a particle dies (or a path is
// priced down), and then the drink outranks every node: a Blood Potion was
// being drunk at full-ish HP on the way into an elite. The public agent
// decides a drink on its own merits instead, at a magnitude no rollout score
// reaches, so the decision never depends on how the map happens to score:
//   * Fruit Juice: drink at once -- +5 max HP is permanent and frees a slot.
//   * Entropic Brew: drink while another slot is empty -- it fills them.
//   * anything else (Blood Potion): hold; it does the same in combat, where it
//     can be timed against the damage.
constexpr int64_t kPotionDrinkNow = 1'000'000'000'000'000;
constexpr int64_t kPotionHoldForCombat = -1'000'000'000'000'000;

[[nodiscard]] int64_t public_out_of_combat_potion_score(
    const RunController& rc, const Move& m) noexcept {
    const uint8_t slot = engine::action_arg0(m.action);
    if (slot >= engine::kPotionCap) return kPotionHoldForCombat;
    switch (static_cast<engine::PotionId>(rc.run.potions[slot])) {
        case engine::PotionId::FRUIT_JUICE:
            return kPotionDrinkNow;
        case engine::PotionId::ENTROPIC_BREW:
            return potion_count(rc.run) < rc.run.potion_slots
                       ? kPotionDrinkNow
                       : kPotionHoldForCombat;
        default:
            return kPotionHoldForCombat;
    }
}

// --- the public agent's campfire ----------------------------------------------
//
// The inherited bands TIE at the pre-boss campfire: REST scores kRestPreferred
// there up to 90% HP, while SMITH scores kRestPreferred above the 70% gate, so
// between 70% and 90% the one-draw tie-break flips a coin between healing and
// upgrading before the boss. The public agent decides it: one gate per
// campfire -- 90% before the boss, kRestHpPct elsewhere -- with REST above it
// and SMITH below, so exactly one of the two leads. (Both gates were screened
// at other values -- 80% pre-boss, 55% / 80% elsewhere -- and none beat these.)

[[nodiscard]] bool public_rest_wanted(const RunController& rc) noexcept {
    const bool pre_boss = static_cast<int>(rc.run.floor) ==
                          engine::act_floor_base_of(rc.run) + 15;
    return hp_at_or_below_pct(rc, pre_boss ? kPreBossRestHpPct : kRestHpPct);
}

// --- the public agent's Neow ---------------------------------------------------
//
// Neow's four options are on screen, and option 3 is always the boss-relic
// swap (category 3 has exactly one entry): Burning Blood out, one random boss
// relic in -- most of which are an extra energy every turn. The one-floor
// rollout cannot price that (relic count is unchanged and the payoff is in
// every later fight), so it took the swap on 29% of the dev seeds. Forcing the
// swap on the 500-seed dev screen: 170 -> 191 Act-1 boss kills (boss reach
// 87% -> 83%, boss win rate 39% -> 46%). So the public agent always swaps and
// skips the Neow rollouts altogether.
constexpr int kNeowBossSwap = 600;  // above kNeowChoice

// --- run-layer scoring (the greedy_policy.py port) ---------------------------

[[nodiscard]] int neow_move_score(PolicyKind kind, const RunController& rc,
                                  const Move& m) noexcept {
    const engine::NeowState& n = rc.neow;
    const uint8_t arg0 = engine::action_arg0(m.action);
    switch (static_cast<engine::NeowScreen>(n.screen)) {
        case engine::NeowScreen::BLESSING:
            if (is_public_agent(kind) && arg0 < engine::kNeowOptionCount &&
                n.option_type[arg0] ==
                    static_cast<uint8_t>(engine::NeowRewardType::BOSS_RELIC)) {
                return kNeowBossSwap;
            }
            // No captured evidence ranks the four blessings (the Python's
            // _score_event has no opinion that reaches Neow), so they tie and
            // the one-draw tie-break picks -- which is also what gives one
            // seed several distinct scripted lines across policy seeds.
            return kNeowChoice;
        case engine::NeowScreen::CARD_REWARD: {
            if (arg0 == engine::kChooseSkipCard) return kCardSkip;
            if (arg0 == engine::kChooseSing) return kCardSing;
            const engine::RunRewardItem* item = open_card_item(rc);
            if (!wants_card_from(rc, item)) return 10;  // CARD_REWARD_TAKE
            if (item != nullptr && arg0 < engine::kRewardCardCap) {
                return kCardTakeOpen +
                       card_rank(item->card_ids[arg0],
                                 item->card_upgrades[arg0]);
            }
            return kCardTakeOpen;
        }
        case engine::NeowScreen::GRID: {
            const auto mode = static_cast<engine::NeowGridMode>(n.grid_mode);
            if (arg0 == engine::kChooseProceed) return kDefaultProceed;
            const GridWants wants =
                mode == engine::NeowGridMode::UPGRADE ? GridWants::BEST
                                                      : GridWants::WORST;
            return grid_pick_score(rc, arg0, wants);
        }
        case engine::NeowScreen::ITEM_REWARD:
            if (arg0 == engine::kChooseProceed) return kRewardProceed;
            return kRewardPotion;  // the three Neow potions: claim, then leave
        case engine::NeowScreen::DONE:
            return kDefaultProceed;
    }
    return kNeowChoice;
}

[[nodiscard]] int reward_claim_score(PolicyKind kind, const RunController& rc,
                                     const Move& m) noexcept {
    const uint8_t arg0 = engine::action_arg0(m.action);
    // The pending-bottle overlay replaces the claim mask with a mandatory
    // 1-pick master-deck grid (run_advance.hpp): bottle the BEST card.
    if (rc.pending_bottle != 0) {
        return grid_pick_score(rc, arg0, GridWants::BEST);
    }
    if (arg0 >= rc.rewards.count || arg0 >= engine::kRewardItemCap) {
        return kRewardCardClosed;
    }
    switch (static_cast<engine::RewardItemKind>(rc.rewards.items[arg0].kind)) {
        case engine::RewardItemKind::RELIC:
            return kRewardRelic;
        case engine::RewardItemKind::GOLD:
        case engine::RewardItemKind::STOLEN_GOLD:
            return kRewardGold;
        case engine::RewardItemKind::POTION:
            return kRewardPotion;
        case engine::RewardItemKind::CARDS:
            // R1: open the row only when the (widened) gate says take, so the
            // open is always followed by a take and the row is retired.
            return wants_card_from(rc, &rc.rewards.items[arg0])
                       ? kRewardCardTake
                       : kRewardCardClosed;
        case engine::RewardItemKind::EMERALD_KEY:
        case engine::RewardItemKind::SAPPHIRE_KEY:
            // K1 (S3.22). The key-seeking kind claims the row above everything
            // else on the screen; every other kind keeps the "do not pick" this
            // arm scored before, spelled out rather than fallen through: taking
            // the sapphire key costs the chest relic, which no weight here
            // knows, so it is a POLICY decision and not a scoring-table repair.
            return kind_seeks_keys(kind) ? kKeyRowClaim : kRewardCardClosed;
        case engine::RewardItemKind::NONE:
            break;
    }
    return kRewardCardClosed;
}

[[nodiscard]] int take_card_score(const RunController& rc,
                                  const Move& m) noexcept {
    const engine::RunRewardItem* item = open_card_item(rc);
    if (!wants_card_from(rc, item)) return 10;  // CARD_REWARD_TAKE: skip wins
    const uint8_t j = engine::action_arg0(m.action);
    if (item != nullptr && j < engine::kRewardCardCap) {
        return kCardTakeOpen +
               card_rank(item->card_ids[j], item->card_upgrades[j]);
    }
    return kCardTakeOpen;
}

[[nodiscard]] int boss_chest_pick_score(const RunController& rc, PolicyKind kind,
                                        const Move& m) noexcept {
    const engine::BossChestState& chest = rc.run.boss_chest;
    const uint8_t arg0 = engine::action_arg0(m.action);
    switch (static_cast<engine::BossChestScreen>(chest.screen)) {
        case engine::BossChestScreen::RELIC_SELECT: {
            if (kind == PolicyKind::SIM_SEARCH_SKIP) return kBossRelicAvoid;
            if (arg0 < engine::kBossChestOfferCount &&
                boss_relic_is_takeable(chest.relics[arg0])) {
                return kBossRelicTake;
            }
            return kBossRelicAvoid;
        }
        case engine::BossChestScreen::EQUIP_GRID:
            // Astrolabe / Empty Cage / Pandora grids act on purgeable cards:
            // spend the picks on the WORST cards.
            return grid_pick_score(rc, arg0, GridWants::WORST);
        case engine::BossChestScreen::EQUIP_ITEM_REWARD:
            if (arg0 == engine::kChooseSkipCard) return kCardSkip;
            if (arg0 == engine::kChooseSing) return kCardSing;
            if (rc.rewards.open_card_item != engine::kNoOpenCardReward) {
                return take_card_score(rc, m);
            }
            return reward_claim_score(kind, rc, m);
        case engine::BossChestScreen::CLOSED:
        case engine::BossChestScreen::DONE:
            break;
    }
    return kBossRelicAvoid;
}

// Preference for one move OUTSIDE combat -- the greedy_policy.py band port.
// Bands only ever compete within one screen's legal set, exactly as in the
// Python (a map choose and a shop choose are never candidates together).
[[nodiscard]] int run_move_score(PolicyKind kind, const RunController& rc,
                                 const Move& m) noexcept {
    switch (m.cat) {
        case MoveCat::NEOW_PROCEED:
            return neow_move_score(kind, rc, m);
        case MoveCat::MAP_NODE:
        case MoveCat::MAP_BOSS: {
            const engine::RoomType r = map_dest_room(rc, m);
            // K3, the NESTED-COMPLETION half. The outer map decision is made by
            // the one-floor rollout in sim_search_pick (which adds the bonuses
            // below); this band is what a rollout's own map walk uses, where
            // there is no second rollout to consult.
            if (kind_seeks_keys(kind) && r == engine::RoomType::Elite &&
                map_dest_is_emerald_node(rc, m) &&
                (rc.run.keys & engine::kKeyEmerald) == 0 &&
                !hp_at_or_below_pct(rc, kKeySeekEliteHpPct)) {
                return kMapEmeraldSeek;
            }
            switch (r) {
                case engine::RoomType::Boss:
                    return kMapBoss;
                case engine::RoomType::Monster:
                    return kMapMonster;
                case engine::RoomType::Elite:
                    return elite_map_value(rc);
                case engine::RoomType::Event:
                case engine::RoomType::Shop:
                case engine::RoomType::Rest:
                case engine::RoomType::Treasure:
                    return kMapNonCombat;
                default:
                    return kMapUnknown;
            }
        }
        case MoveCat::REWARD_CLAIM:
        // S3.52 split the two key rows (EMERALD_KEY / SAPPHIRE_KEY) out of
        // REWARD_CLAIM into their own MoveCat (policy.hpp), purely for
        // coverage accounting -- reward_claim_score's K1 arm (S3.22) already
        // switches on the CLAIMED ITEM'S kind, not on `m.cat`, so routing
        // REWARD_CLAIM_KEY through the SAME call preserves the exact score
        // K1 assigned before the split existed. This is load-bearing: without
        // this arm a key row would silently fall to `default: break;` below
        // and score 0, which would make SIM_SEARCH_KEYS never actually claim
        // a key it is scored to prefer.
        case MoveCat::REWARD_CLAIM_KEY:
            return reward_claim_score(kind, rc, m);
        case MoveCat::REWARD_TAKE_CARD:
            return take_card_score(rc, m);
        case MoveCat::REWARD_SKIP_CARD:
            return kCardSkip;
        case MoveCat::REWARD_SING:
            return kCardSing;
        case MoveCat::REWARD_PROCEED:
            return kRewardProceed;
        case MoveCat::REST: {
            // The pre-boss campfire (the act's row-14 floor) heals unless the
            // run is already nearly full: the boss fight is where the HP goes.
            // act_floor_base_of for the same reason as map_dest_room. Inert
            // in Act 4 either way -- that act has four rows, no row-14
            // campfire, so neither 51+15 nor 52+15 is a floor it can reach --
            // but the file must not carry two spellings of the same base.
            const bool pre_boss =
                static_cast<int>(rc.run.floor) ==
                engine::act_floor_base_of(rc.run) + 15;
            if (is_public_agent(kind)) {
                return public_rest_wanted(rc) ? kRestPreferred : kRestSecondary;
            }
            if (pre_boss && hp_at_or_below_pct(rc, 90)) return kRestPreferred;
            return hp_at_or_below_pct(rc, kRestHpPct) ? kRestPreferred
                                                      : kRestSecondary;
        }
        case MoveCat::SMITH:
            if (is_public_agent(kind)) {
                return public_rest_wanted(rc) ? kRestSecondary : kRestPreferred;
            }
            return hp_at_or_below_pct(rc, kRestHpPct) ? kRestSecondary
                                                      : kRestPreferred;
        case MoveCat::LIFT:
        case MoveCat::TOKE:
        case MoveCat::DIG:
            return kRestOther;
        case MoveCat::RECALL:
            // K2 (S3.22): take the ruby key while HP allows. Below the gate the
            // band is SIM_SEARCH's unchanged 250, so a hurt run heals instead --
            // and the button survives, because taking it is the only thing that
            // removes it.
            return kind_seeks_keys(kind) &&
                           !hp_at_or_below_pct(rc, kKeyRecallHpPct)
                       ? kKeyRecall
                       : kRestRecall;
        case MoveCat::SMITH_CARD:
            if (engine::action_arg0(m.action) == engine::kChooseCancelGrid) {
                return kGridCancel;
            }
            // Upgrade the BEST card (a deliberate sharpening of the Python's
            // uniform SELECT_PICK: the sim knows the registry rank).
            return grid_pick_score(rc, engine::action_arg0(m.action),
                                   GridWants::BEST);
        case MoveCat::TOKE_CARD:
            if (engine::action_arg0(m.action) == engine::kChooseCancelGrid) {
                return kGridCancel;
            }
            return grid_pick_score(rc, engine::action_arg0(m.action),
                                   GridWants::WORST);
        case MoveCat::TREASURE_OPEN:
            return kTreasureOpen;
        case MoveCat::TREASURE_SKIP:
            return kTreasureSkip;
        case MoveCat::EVENT_OPTION:
            // The Python's word buckets read CommunicationMod's English labels,
            // which the sim deliberately does not carry -- options tie at
            // EVENT_NEUTRAL and the tie-break explores, which is also what
            // varies event routes across policy seeds.
            return kEventNeutral;
        case MoveCat::EVENT_GRID: {
            const auto gk = static_cast<engine::EventGridKind>(
                rc.event.grid_kind);
            const GridWants wants =
                gk == engine::EventGridKind::UPGRADE ||
                        gk == engine::EventGridKind::DUPLICATE
                    ? GridWants::BEST
                    : GridWants::WORST;
            return grid_pick_score(rc, engine::action_arg0(m.action), wants);
        }
        case MoveCat::SHOP:
            if (is_public_agent(kind)) return public_shop_score(rc, m);
            // Buy nothing, leave (SHOP_ROOM_LEAVE > every buy): the b1.7.0
            // driver's measured behaviour, and the reason no scripted line
            // ever needs a purchase matched against a live shelf.
            if (engine::action_arg0(m.action) == engine::kChooseProceed) {
                return kShopLeave;
            }
            if (engine::action_arg0(m.action) == engine::kChooseCancelGrid) {
                return kShopLeave - 10;
            }
            return kShopOther;
        case MoveCat::BOSS_CHEST_OPEN:
            // Open ONCE, in both cohorts. A skipped pick is a REVERSIBLE
            // screen close that re-advertises `open` (boss_chest.hpp), so a
            // stateless preference that reopens after a skip is an unbounded
            // 2-cycle -- the exact shape the Python driver breaks with its
            // _run_boss_chest sequencing. Here `boss_chest.seen` (the reveal
            // bit, latched on the first open) is the state that breaks it:
            // the SKIP cohort skips the seen offers and leaves, and the TAKE
            // cohort only reaches a re-advertised `open` when all three
            // offers were never-take relics -- and then leaves too (the same
            // outcome as the Python's take cohort on an all-never-take
            // chest). First measured as two floor-17 LIVELOCK rows in the
            // first 400-row smoke scan of this policy.
            if (rc.run.boss_chest.seen) return 1;  // below BOSS_CHEST_PROCEED
            return kBossChestOpen;
        case MoveCat::BOSS_CHEST_PICK:
            return boss_chest_pick_score(rc, kind, m);
        case MoveCat::BOSS_CHEST_SKIP:
            return kind == PolicyKind::SIM_SEARCH_SKIP ? kBossRelicSkipWins
                                                       : kBossRelicSkip;
        case MoveCat::BOSS_CHEST_PROCEED:
            return kBossChestProceed;
        case MoveCat::USE_POTION:
        case MoveCat::USE_POTION_TARGET:
            return kPotionOutOfCombat;  // out-of-combat use: below any screen move
        case MoveCat::ACT4_MAP_CHOICE:
            // TheEnding's map offers exactly one legal node (or the boss edge
            // alone) per row (s3-design §4.2, generate_special_map) -- there
            // is never a competing candidate to prefer, so any constant score
            // picks the same move. kMapNonCombat is reused rather than adding
            // a constant for a screen with no real choice.
            return kMapNonCombat;
        case MoveCat::SPIRE_HEART_DIALOG:
            // The dialog's one-option menu (spire_heart.cpp): scored like any
            // other single-option event.
            return kEventNeutral;
        default:
            break;
    }
    return 0;
}

// --- combat search -----------------------------------------------------------

// Static in-combat rank used for the ROLLOUT COMPLETION only: play the
// highest static-value card, never a potion, END_TURN when nothing is left,
// confirm choice screens promptly. The searched candidate is what varies; the
// completion is a fixed script so every candidate is completed the same way.
// `threat` flips the play weights to the block-first shape -- the same
// under-attack switch greedy_policy.py runs off the intent banner, derived
// here by the sim probe in complete_combat below.
//
// `hold_powers` is the Curiosity hold applied to the COMPLETION, so the tail
// every candidate is completed with is the tail the policy will actually play.
// Without it the preview would tax itself with power plays the real line then
// never makes, and every candidate's score would be measured against a fight
// that cannot happen. A held power ranks BELOW END_TURN (1) rather than being
// removed: the completion then ends the turn instead, which is exactly the
// hold. It is false in every combat where no live monster owns Curiosity, so
// the completion is byte-identical to before outside that one fight.
[[nodiscard]] int completion_rank(const RunController& rc, const Move& m,
                                  bool threat,
                                  bool hold_powers = false) noexcept {
    switch (m.cat) {
        case MoveCat::PLAY_CARD:
        case MoveCat::PLAY_CARD_TARGET: {
            const uint8_t slot = engine::action_arg0(m.action);
            if (slot >= rc.combat.hand_count) return 1000;
            const engine::CardInstance& ci =
                rc.combat.card_pool[rc.combat.hand[slot]];
            const engine::CardDef* def =
                engine::card_def(static_cast<CardId>(ci.card_id));
            if (def == nullptr) return 1000;
            if (hold_powers && def->type == CardType::POWER) return 0;
            const StaticCardScore sc = static_card_score(*def, ci.upgrade);
            return threat ? 1000 + sc.damage + sc.block * 4
                          : 1000 + sc.damage * 4 + sc.block;
        }
        case MoveCat::CHOICE_CONFIRM:
            return 600;
        case MoveCat::COMBAT_CHOOSE:
            return 500;
        case MoveCat::END_TURN:
            return 1;
        default:
            return 0;  // potions and run-layer verbs: never in a completion
    }
}

void apply_one(RunController& rc, Action a) noexcept;  // defined below

// The threat-aware static completion of the CURRENT fight, shared by both
// rollouts. Once per turn it probes the incoming damage exactly -- copy the
// state, end the turn on the copy, read the HP delta (the sim IS the intent
// banner, with none of the banner's display caveats) -- and plays block-first
// while anything is actually going to hit. Returns the steps consumed.
[[nodiscard]] int complete_combat(PolicyKind kind, RunController& sim,
                                 int budget,
                                 uint16_t turn_cap = kRolloutTurnCap) noexcept {
    int steps = 0;
    bool threat = false;
    uint32_t probed_turn = UINT32_MAX;
    // The Curiosity hold, evaluated ONCE at entry and thereafter only while it
    // is on. Nothing applies PowerId::CURIOSITY mid-combat (its only applier is
    // usePreBattleAction), so the tax can only ever turn OFF -- at the Rebirth
    // purge -- and a completion that entered without it can never acquire it.
    // Under SIM_SEARCH / SIM_SEARCH_SKIP the flag is a compile-time false and
    // this costs nothing at all.
    bool hold_powers = kind_holds_powers(kind) && curiosity_tax(sim.combat) > 0;
    // The step budget alone does not bound COST: one END_TURN advance pumps
    // the whole monster phase, and a ten-monster spawner fight makes that the
    // expensive unit (measured: >4 s per searched action inside STS126146's
    // ten-slot Act-2 stalemate, because every candidate's rollout paid ~80
    // such monster phases). So the completion is also TURN-capped: a
    // fight that has not resolved kRolloutTurnCap turns past the entry point
    // is scored as it stands by the ongoing-combat formula, which penalizes
    // every monster still alive -- fair across candidates, and bounded.
    const uint16_t turn0 = sim.combat.turn;
    while (steps < budget &&
           sim.phase == static_cast<uint8_t>(RunPhase::COMBAT) &&
           static_cast<uint16_t>(sim.combat.turn - turn0) <= turn_cap) {
        RunActionMask mask;
        engine::legal_actions(sim, mask);
        Move moves[kMoveCap];
        const size_t n = enumerate_moves(sim, mask, moves, kMoveCap);
        if (n == 0) break;
        if (sim.combat.turn != probed_turn && mask.combat.can_end_turn) {
            RunController probe = sim;
            apply_one(probe, engine::make_action(ActionVerb::END_TURN));
            const int hp_after =
                probe.phase == static_cast<uint8_t>(RunPhase::COMBAT)
                    ? probe.combat.player_hp
                    : probe.run.hp;
            threat = hp_after < sim.combat.player_hp;
            probed_turn = sim.combat.turn;
        }
        if (hold_powers) hold_powers = curiosity_tax(sim.combat) > 0;
        size_t best_i = 0;
        int best_rank = 0;
        for (size_t i = 0; i < n; ++i) {
            const int r = completion_rank(sim, moves[i], threat, hold_powers);
            if (i == 0 || r > best_rank) {
                best_rank = r;
                best_i = i;
            }
        }
        apply_one(sim, moves[best_i].action);
        ++steps;
    }
    return steps;
}

struct EvalWeights {
    int64_t potion_held;  // 0 while R2 says the fight is worth spending in
};

// --- run-layer evaluation (map / event 1-ply) --------------------------------
//
// What a resolved floor is worth. Used by the ONE-FLOOR ROLLOUT below, which
// is how map-node and event-option decisions consult the sim instead of a
// label heuristic (the Python's event word-buckets read CommunicationMod's
// English labels, which the sim deliberately does not carry). Weights are in
// the combat evaluation's HP-equivalent currency: a relic ~10 HP, 100 gold
// ~6 HP, a curse ~-5 HP, a potion ~5 HP, max HP at a third of live HP.
constexpr int64_t kRunEvalMaxHp = 100;
constexpr int64_t kRunEvalGold = 20;
constexpr int64_t kRunEvalRelic = 3000;
constexpr int64_t kRunEvalPotion = 1500;
constexpr int64_t kRunEvalCurse = -1500;

[[nodiscard]] int64_t run_layer_eval(const RunController& rc) noexcept {
    if (rc.phase == static_cast<uint8_t>(RunPhase::RUN_OVER)) {
        return engine::run_is_victory(rc) ? kEvalVictory
                                          : kEvalDefeat + rc.run.floor;
    }
    int64_t curses = 0;
    for (uint16_t i = 0;
         i < rc.run.master_deck_count && i < engine::kMasterDeckCap; ++i) {
        const engine::CardDef* def = engine::card_def(
            static_cast<CardId>(rc.run.master_deck[i].card_id));
        if (def != nullptr && def->type == CardType::CURSE) ++curses;
    }
    return static_cast<int64_t>(rc.run.hp) * kEvalPlayerHp +
           static_cast<int64_t>(rc.run.max_hp) * kRunEvalMaxHp +
           static_cast<int64_t>(rc.run.gold) * kRunEvalGold +
           static_cast<int64_t>(rc.run.relic_count) * kRunEvalRelic +
           static_cast<int64_t>(potion_count(rc.run)) * kRunEvalPotion +
           curses * kRunEvalCurse;
}

[[nodiscard]] int64_t eval_state(const RunController& rc,
                                 const EvalWeights& w) noexcept {
    const auto phase = static_cast<RunPhase>(rc.phase);
    if (phase == RunPhase::RUN_OVER) {
        return engine::run_is_victory(rc)
                   ? kEvalVictory
                   : kEvalDefeat + rc.run.floor;  // die later > die sooner
    }
    const int64_t potions =
        static_cast<int64_t>(potion_count(rc.run)) * w.potion_held;
    if (phase != RunPhase::COMBAT) {
        // Combat over, run continues (reward screen, boss chest, event page,
        // act transition): strictly better than any ongoing state.
        return kEvalCombatOver + static_cast<int64_t>(rc.run.hp) * kEvalPlayerHp +
               potions;
    }
    const engine::CombatState& cs = rc.combat;
    int64_t monster_hp = 0;
    int64_t live = 0;
    for (int i = 0; i < cs.monster_count && i < engine::kMonsterCap; ++i) {
        const engine::MonsterState& mo = cs.monsters[i];
        if (mo.monster_id == 0 || mo.hp <= 0) continue;
        ++live;
        monster_hp += mo.hp;
    }
    return static_cast<int64_t>(cs.player_hp) * kEvalPlayerHp + potions -
           monster_hp * kEvalMonsterHp - live * kEvalLiveMonster;
}

void apply_one(RunController& rc, Action a) noexcept {
    StepResult res{};
    engine::advance({&rc, 1}, {&a, 1}, {&res, 1});
}

// Roll the copy out to the END OF THE COMBAT (or the budget) with the static
// completion, then evaluate. The horizon is the combat's outcome rather than
// the next turn boundary on purpose: a one-turn horizon was measured first
// and it cannot see a power's payoff, a potion's fight-scale value, or the
// difference between "blocked this hit" and "won the fight with 30 HP" -- the
// candidate under evaluation is one searched ply, and everything after it is
// this fixed deterministic completion, so every candidate is completed the
// same way and the comparison stays a pure function of the state.
// The per-decision advance budget is bounded as CANDIDATES x BUDGET <=
// kMoveBudgetProduct: a nine-monster spawner fight offers ~60 candidate
// moves, and sixty full-length rollouts per decision, times a few hundred
// stalemated decisions, is minutes inside one scan row (measured on
// STS126146's Act-2 line before this bound). Fewer candidates keep the full
// rollout; a huge legal set trades rollout length for breadth,
// deterministically, as a pure function of the candidate count.
constexpr int kMoveBudgetProduct = 24 * kRolloutBudget;

[[nodiscard]] int rollout_budget_for(size_t n) noexcept {
    if (n <= 24) return kRolloutBudget;
    const int b = static_cast<int>(kMoveBudgetProduct / static_cast<int>(n));
    return b < 64 ? 64 : b;
}

[[nodiscard]] int64_t rollout_and_eval(PolicyKind kind, RunController& sim,
                                       int budget,
                                       const EvalWeights& w) noexcept {
    (void)complete_combat(kind, sim, budget);
    return eval_state(sim, w);
}

// BOSS-FIGHT DEEPENING: a second searched ply, boss rooms only. The measured
// motivation is stage-2A of the S2.V2 tuning scans: 46 Act-3 boss fights
// reached, ZERO won under the 1-ply search -- the fights the S2-G2 depth
// bars are actually about are precisely where one searched ply plus a static
// tail misorders long setups. In a boss room each candidate's score is the
// BEST over the next decision's candidates (each completed statically), i.e.
// a bounded 2-ply. The inner breadth is capped deterministically; boss rooms
// are rare in scan volume, so the cost stays contained where it matters.
constexpr size_t kBossInnerBreadth = 12;
// The inner (second-ply) rollouts run on a shorter leash than the outer
// ones: cost per decision is bounded by n * kBossInnerBreadth *
// kBossInnerRollout advances -- a scan tool needs a hard per-decision
// ceiling, because the deepening multiplies whatever the fight offers. A
// truncated inner rollout is scored by the ongoing-combat formula, which
// penalizes every monster still alive, so the comparison stays fair across
// candidates.
constexpr int kBossInnerRollout = 120;
// The inner rollout is also TURN-capped far below the outer one: the unit
// that costs is the END_TURN monster phase (wide boards make it the dominant
// term), and outer x inner x turns is the number of those one decision pays.
constexpr uint16_t kBossInnerTurnCap = 6;
constexpr uint16_t kBossDeepTurns = 32;  // 2-ply only through the opening
constexpr uint16_t kSearchTurns = 32;    // past this: static rank only

[[nodiscard]] int64_t rollout_boss_and_eval(PolicyKind kind, RunController& sim,
                                            const EvalWeights& w) noexcept {
    if (sim.phase != static_cast<uint8_t>(RunPhase::COMBAT)) {
        return eval_state(sim, w);
    }
    RunActionMask mask;
    engine::legal_actions(sim, mask);
    Move inner[kMoveCap];
    size_t n = enumerate_moves(sim, mask, inner, kMoveCap);
    if (n == 0) return eval_state(sim, w);
    if (n > kBossInnerBreadth) n = kBossInnerBreadth;
    int64_t best = kEvalDefeat;
    for (size_t j = 0; j < n; ++j) {
        RunController sim2 = sim;
        apply_one(sim2, inner[j].action);
        (void)complete_combat(kind, sim2, kBossInnerRollout, kBossInnerTurnCap);
        // The SECOND searched ply pays the Curiosity hold too. The Awakened One
        // is a boss room with three live records at the bell (two Cultists and
        // the boss), so this deepening IS active in the fight the rule exists
        // for; leaving the inner ply untaxed would let a candidate score itself
        // on a power play the outer ply is forbidden to make.
        const int64_t s =
            eval_state(sim2, w) -
            (kind_holds_powers(kind)
                 ? curiosity_penalty_for(sim.combat, sim, inner[j])
                 : 0);
        if (s > best) best = s;
    }
    return best;
}

// 2-ply only while the OUTER candidate set is small enough that the product
// of the two plies stays inside the same per-decision ceiling, and only
// against a small board: a spawner fight that happens to carry the Boss room
// type (and any future boss with a wide board) multiplies the two plies into
// minutes per decision, and a wide board is exactly where the extra ply buys
// the least (measured on STS126146's ten-monster Act-2 line).
constexpr size_t kBossDeepMaxCandidates = 12;
constexpr int kBossDeepMaxLiveMonsters = 4;

[[nodiscard]] int live_monster_count(const engine::CombatState& cs) noexcept {
    int live = 0;
    for (int i = 0; i < cs.monster_count && i < engine::kMonsterCap; ++i) {
        if (cs.monsters[i].monster_id != 0 && cs.monsters[i].hp > 0) ++live;
    }
    return live;
}

// --- S3.61's boss-floor DEEPENING (SIM_SEARCH_KEYS_DEEP only) ---------------
//
// A bounded THIRD ply. `rollout_boss_deep_and_eval` mirrors
// `rollout_boss_and_eval` exactly (same structure: enumerate, apply, score,
// keep the best) except each of ITS candidates is finished by one more
// searched decision instead of a single static `complete_combat` -- the
// budgets shrink at each level so the product (n outer x kDeepInnerBreadth x
// kDeepInner2Breadth x completion cost) stays a bounded multiple of the
// existing 2-ply's cost rather than exploding. It is reached only through
// `kind_deepens_boss`, so SIM_SEARCH_KEYS keeps calling
// `rollout_boss_and_eval` unchanged and its own scan output stays
// byte-identical (checked by sha256 in docs/verification/s3-61-reach.md, the
// SIM_SEARCH_KEYS invariance precedent S3.22 §5 set for SIM_SEARCH).
constexpr size_t kDeepInnerBreadth = 6;    // ply-2 breadth (vs 12, kBossInnerBreadth)
constexpr size_t kDeepInner2Breadth = 3;   // ply-3 breadth
constexpr int kDeepInnerRollout = 90;      // ply-2's own completion, if it ends combat
constexpr int kDeepInner2Rollout = 60;     // ply-3's completion budget
constexpr uint16_t kDeepInnerTurnCap = 6;
constexpr uint16_t kDeepInner2TurnCap = 4;
// The search's turn window on a boss floor for SIM_SEARCH_KEYS_DEEP, in place
// of kBossDeepTurns/kSearchTurns (32 both). The Heart's kit (Beat of Death,
// the Invincible HP-floor reset, buffCount stacking) is built to run long, so
// a search that gives up its ply past turn 32 -- every other kind's fixed
// behaviour -- would degrade to static rank for most of the fight that
// matters. 60 is double the existing window, chosen as the cheapest widening
// that still bounds worst-case cost to a small multiple of what a boss fight
// already pays under the existing 2-ply.
constexpr uint16_t kDeepSearchTurns = 60;

[[nodiscard]] int64_t rollout_boss_deep_and_eval(PolicyKind kind, RunController& sim,
                                                 const EvalWeights& w) noexcept {
    if (sim.phase != static_cast<uint8_t>(RunPhase::COMBAT)) {
        return eval_state(sim, w);
    }
    RunActionMask mask;
    engine::legal_actions(sim, mask);
    Move inner[kMoveCap];
    size_t n = enumerate_moves(sim, mask, inner, kMoveCap);
    if (n == 0) return eval_state(sim, w);
    if (n > kDeepInnerBreadth) n = kDeepInnerBreadth;
    int64_t best = kEvalDefeat;
    for (size_t j = 0; j < n; ++j) {
        RunController sim2 = sim;
        apply_one(sim2, inner[j].action);
        int64_t s2;
        if (sim2.phase == static_cast<uint8_t>(RunPhase::COMBAT)) {
            // ply 3: one more searched decision, each candidate finished by a
            // short static rollout -- this is the "deeper" the kind is named
            // for, in place of ply 2's single static completion.
            RunActionMask mask2;
            engine::legal_actions(sim2, mask2);
            Move inner2[kMoveCap];
            size_t n2 = enumerate_moves(sim2, mask2, inner2, kMoveCap);
            if (n2 == 0) {
                s2 = eval_state(sim2, w);
            } else {
                if (n2 > kDeepInner2Breadth) n2 = kDeepInner2Breadth;
                int64_t best2 = kEvalDefeat;
                for (size_t k = 0; k < n2; ++k) {
                    RunController sim3 = sim2;
                    apply_one(sim3, inner2[k].action);
                    (void)complete_combat(kind, sim3, kDeepInner2Rollout, kDeepInner2TurnCap);
                    const int64_t s3 =
                        eval_state(sim3, w) -
                        (kind_holds_powers(kind)
                             ? curiosity_penalty_for(sim2.combat, sim2, inner2[k])
                             : 0);
                    if (s3 > best2) best2 = s3;
                }
                s2 = best2;
            }
        } else {
            (void)complete_combat(kind, sim2, kDeepInnerRollout, kDeepInnerTurnCap);
            s2 = eval_state(sim2, w);
        }
        const int64_t s =
            s2 - (kind_holds_powers(kind)
                      ? curiosity_penalty_for(sim.combat, sim, inner[j])
                      : 0);
        if (s > best) best = s;
    }
    return best;
}

// --- the one-floor rollout (map / event 1-ply) -------------------------------
//
// Resolve the rest of THIS floor -- the fight the node turns into, the event
// pages behind the option, the rewards behind them -- with the deterministic
// completions (static rank inside combat, the heuristic bands outside it),
// then score the run with run_layer_eval. The horizon is "standing on the
// next map choice" (or the boss chest / a terminal), so a map candidate is
// judged by what the run looks like AFTER the room it names, an event option
// by what it actually does to the run, and a fight's rewards are claimed by
// the completion before the state is scored. This is what replaces the
// Python's map-symbol bands and label word-buckets: the sim is consulted, per
// the design's sanction, instead of a guess about what a room might contain.
//
// THE HORIZON IS ONE ROOM LATER THAN IT READS for the inherited kinds: `floor0`
// is taken AFTER the candidate move, and a map move has already advanced the
// floor (an event option has not left its floor at all), so the map choice
// that ends the candidate's room is on `floor0` and does NOT stop the loop --
// the rollout walks one more room, picked by the map BANDS, which prefer an
// elite over a hallway fight above 60% HP. The public agent stops at the FIRST
// map choice (`room_horizon`), so a candidate is judged by its own room, not
// by whatever elite the band would walk into next. SIM_SEARCH and the other
// kinds keep the two-room horizon their measured baselines were taken under.
constexpr int kFloorRolloutBudget = 600;

[[nodiscard]] int64_t rollout_floor_and_eval(PolicyKind kind,
                                             RunController& sim,
                                             bool room_horizon = false) noexcept {
    const uint16_t floor0 = sim.run.floor;
    const uint8_t act0 = sim.run.act;
    for (int step = 0; step < kFloorRolloutBudget; ++step) {
        const auto phase = static_cast<RunPhase>(sim.phase);
        if (phase == RunPhase::RUN_OVER ||
            phase == RunPhase::ROOM_UNIMPLEMENTED ||
            phase == RunPhase::NONE || phase == RunPhase::BOSS_TREASURE) {
            break;
        }
        if (phase == RunPhase::MAP_CHOICE &&
            (room_horizon || sim.run.floor != floor0 ||
             sim.run.act != act0)) {
            break;  // the floor is resolved: the next choice is the horizon
        }
        if (phase == RunPhase::COMBAT) {
            const int used =
                complete_combat(kind, sim, kFloorRolloutBudget - step);
            step += used > 0 ? used - 1 : 0;
            if (used == 0) break;
            continue;
        }
        RunActionMask mask;
        engine::legal_actions(sim, mask);
        Move moves[kMoveCap];
        const size_t n = enumerate_moves(sim, mask, moves, kMoveCap);
        if (n == 0) break;
        size_t best_i = 0;
        int best_rank = 0;
        for (size_t i = 0; i < n; ++i) {
            const int r = run_move_score(kind, sim, moves[i]);
            if (i == 0 || r > best_rank) {
                best_rank = r;
                best_i = i;
            }
        }
        apply_one(sim, moves[best_i].action);
    }
    return run_layer_eval(sim);
}

}  // namespace

int sim_search_curiosity_tax(const engine::CombatState& cs) noexcept {
    return curiosity_tax(cs);
}

int64_t sim_search_curiosity_penalty(const RunController& rc,
                                     const Move& m) noexcept {
    if (rc.phase != static_cast<uint8_t>(RunPhase::COMBAT)) return 0;
    return curiosity_penalty_for(rc.combat, rc, m);
}

size_t sim_search_pick(PolicyKind kind, const RunController& rc,
                       const Move* moves, size_t n, PolicyRng& rng) noexcept {
    const bool in_combat = rc.phase == static_cast<uint8_t>(RunPhase::COMBAT);
    if (n > kMoveCap) n = kMoveCap;  // enumerate_moves already guarantees this

    // SIM_SEARCH_BLIND: THE ONE SUBSTITUTION (docs/verification/
    // sim-search-blind.md). Every other kind rolls candidates out over `rc`
    // itself -- the TRUE controller, hidden draw order and all. This kind
    // instead rolls every candidate out over `rollout_world`, a
    // `resample_hidden` TWIN of `rc` drawn exactly ONCE here, before the
    // candidate loop, so every candidate THIS DECISION sees the SAME
    // resampled world (common random numbers -- candidates differ only in the
    // move applied, never in which particle they were compared under). The
    // seed is a fresh `PolicyRng::next()` draw: `rng` is the policy's own
    // stream (never an engine stream), so this draw is SAMPLER-PRIVATE and
    // costs the engine zero draws (resample.hpp's header note), and the case
    // stays a pure function of its CaseId. `rollout_world` equals `rc` for
    // every other kind, i.e. every "RunController sim = rc" rollout snapshot
    // below reads `rollout_world` and the substitution is a no-op there.
    RunController rollout_world = rc;
    if (kind == PolicyKind::SIM_SEARCH_BLIND) {
        SamplerRng sampler_rng = sampler_rng_from_seed(static_cast<int64_t>(rng.next()));
        resample_hidden(rollout_world, sampler_rng);
    }

    // One score per candidate. Combat: 1-ply + scripted completion over an
    // engine snapshot. Run layer: the greedy_policy.py band port. The scores
    // live in a fixed stack array (kMoveCap entries) so each candidate is
    // simulated exactly once and the body stays heap-free and noexcept.
    int64_t scores[kMoveCap];
    // R2's discipline as a HOLD VALUE: a potion is nearly free to spend in a
    // boss/elite/low-HP fight (but not literally free -- a drink that buys
    // nothing is still a waste) and expensive to spend anywhere else.
    const EvalWeights w{in_combat && potion_worth_spending(rc)
                            ? kEvalPotionHeldSpendable
                            : kEvalPotionHeld};
    // The Curiosity hold's trigger for THIS decision, read once off the board.
    // False for SIM_SEARCH / SIM_SEARCH_SKIP always, and for SIM_SEARCH_HOLD in
    // every combat except an Awakened One's phase 1.
    const bool hold_powers =
        in_combat && kind_holds_powers(kind) && curiosity_tax(rc.combat) > 0;
    // S3.61: SIM_SEARCH_KEYS_DEEP widens the search's turn window from
    // kSearchTurns/kBossDeepTurns (32 both) to kDeepSearchTurns (60), but ONLY
    // on a boss floor -- every other kind, and DEEP itself off a boss floor,
    // keeps exactly the old window, which is what the SIM_SEARCH invariance
    // check (docs/verification/s3-61-reach.md §5) and SIM_SEARCH_KEYS's own
    // fixed-range sha256 both certify unmoved.
    const bool boss_floor =
        rc.room_type == static_cast<uint8_t>(engine::RoomType::Boss);
    const bool deepens = kind_deepens_boss(kind) && boss_floor;
    const uint16_t search_turn_window = deepens ? kDeepSearchTurns : kSearchTurns;
    const uint16_t boss_ply_window = deepens ? kDeepSearchTurns : kBossDeepTurns;
    // T2.2s: optional hand choices must make finite progress. CHOOSE toggles
    // the selected suffix back out of the selection; a bounded rollout can
    // strictly prefer that reversal from BOTH sides and never press CONFIRM.
    // Keep the engine's legal surface intact, but make this policy's selection
    // monotone: additions remain searchable and CONFIRM remains available.
    // This applies only to the actual decision, not simulated future choices;
    // at most hand_count additions can occur before only CONFIRM (or a finite
    // potion use) remains. The one-draw tie-break below is unchanged.
    // enumerate_moves emits CHOICE_CONFIRM only for this optional screen.
    // Reuse that already-computed legal surface instead of rebuilding a mask
    // on every combat decision. The selected count uses the same queue-front
    // accessor that legal_actions uses to expose its hand suffix.
    bool monotone_choice = false;
    if (in_combat && rc.combat.action_count != 0) {
        for (size_t i = 0; i < n; ++i) {
            if (moves[i].cat == MoveCat::CHOICE_CONFIRM) {
                monotone_choice = true;
                break;
            }
        }
    }
    const unsigned selected = monotone_choice
        ? engine::choose_selected_count(
              rc.combat.action_queue[rc.combat.action_head].flags)
        : 0u;
    const unsigned unselected = static_cast<unsigned>(rc.combat.hand_count) -
        (selected <= rc.combat.hand_count ? selected : 0u);
    // The Neow blessing is rolled out for every kind but the public agent,
    // which scores it by its band (see "the public agent's Neow").
    const bool neow_rolls_out = !is_public_agent(kind);
    const auto score_all = [&](const RunController& world, int64_t* out) noexcept {
    for (size_t i = 0; i < n; ++i) {
        if (monotone_choice && moves[i].cat == MoveCat::COMBAT_CHOOSE &&
            engine::action_arg0(moves[i].action) >= unselected) {
            out[i] = INT64_MIN;
            continue;
        }
        if (!in_combat && is_public_agent(kind) &&
            (moves[i].cat == MoveCat::USE_POTION ||
             moves[i].cat == MoveCat::USE_POTION_TARGET)) {
            out[i] = public_out_of_combat_potion_score(rc, moves[i]);
            continue;
        }
        if (in_combat) {
            // THE TURN RAMP -- a hard cost ceiling per fight, encoded in
            // state so it stays deterministic. The 2-ply deepening runs
            // only through a boss fight's opening (kBossDeepTurns), and a
            // fight that drags past kSearchTurns is a stalemate the search
            // cannot win however hard it looks -- the policy degrades to
            // the static completion rank there and lets the run reach its
            // action cap or its livelock detector cheaply instead of
            // multiplying rollouts into a dead fight. SIM_SEARCH_KEYS_DEEP
            // keeps the ply open longer on a boss floor (search_turn_window),
            // because that is precisely where a long fight (the Heart) is
            // still worth searching past turn 32.
            if (rc.combat.turn > search_turn_window) {
                out[i] = completion_rank(rc, moves[i], false, hold_powers);
            } else {
                RunController sim = world;  // trivially-copyable snapshot
                apply_one(sim, moves[i].action);
                out[i] =
                    boss_floor && rc.combat.turn <= boss_ply_window &&
                            n <= kBossDeepMaxCandidates &&
                            live_monster_count(rc.combat) <=
                                kBossDeepMaxLiveMonsters
                        ? (deepens
                               ? rollout_boss_deep_and_eval(kind, sim, w)  // 3-ply
                               : rollout_boss_and_eval(kind, sim, w))      // 2-ply
                        : rollout_and_eval(kind, sim, rollout_budget_for(n), w);
                // Hand-select screens: CONFIRM breaks exact evaluation TIES
                // (+1 is far below any real difference; one HP is worth
                // 300), so an indifferent toggle can never outrank plain
                // progress. Strictly preferred select/deselect oscillation
                // is prevented by the monotone actual-choice guard above;
                // the historical S2.V2 reach measurements retain their pin.
                if (moves[i].cat == MoveCat::CHOICE_CONFIRM) out[i] += 1;
            }
            // The Curiosity hold on the SEARCHED ply (SIM_SEARCH_HOLD only).
            // Subtracted after the rollout rather than folded into it, because
            // it prices the tail the rollout's turn cap was assumed not to see
            // -- the band above the helpers, and why that assumption is wrong.
            if (hold_powers) {
                out[i] -= curiosity_penalty_for(rc.combat, rc, moves[i]);
            }
        } else if (moves[i].cat == MoveCat::MAP_NODE ||
                   moves[i].cat == MoveCat::MAP_BOSS ||
                   moves[i].cat == MoveCat::EVENT_OPTION ||
                   (neow_rolls_out && moves[i].cat == MoveCat::NEOW_PROCEED &&
                    rc.phase == static_cast<uint8_t>(RunPhase::NEOW) &&
                    rc.neow.screen ==
                        static_cast<uint8_t>(engine::NeowScreen::BLESSING))) {
            // Map nodes and event options are the two run-layer screens whose
            // consequences the heuristic bands can only guess at; consult the
            // sim instead (the one-floor rollout above). Every other screen
            // keeps its greedy_policy.py band -- rest/smith and card-take
            // value lives in state the run evaluation cannot see (upgrades,
            // deck quality), so a rollout would systematically misjudge them.
            RunController sim = world;
            apply_one(sim, moves[i].action);
            out[i] = rollout_floor_and_eval(kind, sim, is_public_agent(kind));
            // K3/K4 (S3.22): the key appetite, on the OUTER map decision only.
            // Zero for every kind but SIM_SEARCH_KEYS, so this line cannot move
            // an existing trajectory.
            out[i] += key_map_bonus(kind, rc, moves[i]);
            // The public agent's elite-avoiding path rule (zero otherwise).
            out[i] -= public_path_penalty(kind, rc, moves[i]);
            // ...and its HP walk over the remaining map (zero otherwise).
            out[i] += public_path_hp_term(kind, rc, moves[i], sim);
        } else {
            out[i] = run_move_score(kind, rc, moves[i]);
        }
    }
    };
    // Does any candidate's score consult a rollout world? (Exactly the two
    // rollout branches of score_all.) A screen scored by bands alone needs no
    // particles at all.
    bool consults_world = false;
    if (in_combat) {
        consults_world = rc.combat.turn <= search_turn_window;
    } else {
        for (size_t i = 0; i < n && !consults_world; ++i) {
            consults_world =
                moves[i].cat == MoveCat::MAP_NODE ||
                moves[i].cat == MoveCat::MAP_BOSS ||
                moves[i].cat == MoveCat::EVENT_OPTION ||
                (neow_rolls_out && moves[i].cat == MoveCat::NEOW_PROCEED &&
                 rc.phase == static_cast<uint8_t>(RunPhase::NEOW) &&
                 rc.neow.screen ==
                     static_cast<uint8_t>(engine::NeowScreen::BLESSING));
        }
    }
    if (!is_public_agent(kind) || !consults_world) {
        score_all(rollout_world, scores);
    } else {
        // SIM_SEARCH_PIMC: average over K particles. The K seeds are drawn up
        // front so the policy stream advances by exactly K + 1 draws per
        // decision; every candidate is scored against the same K worlds.
        // Scores are summed and divided once at the end, so a band score that
        // does not depend on the world comes back exact.
        const int k_worlds = pimc_worlds();
        int64_t world_scores[kMoveCap];
        bool min_seen[kMoveCap] = {};
        for (size_t i = 0; i < n; ++i) scores[i] = 0;
        for (int wk = 0; wk < k_worlds; ++wk) {
            RunController world = rc;
            SamplerRng sampler_rng =
                sampler_rng_from_seed(static_cast<int64_t>(rng.next()));
            resample_hidden(world, sampler_rng);
            score_all(world, world_scores);
            for (size_t i = 0; i < n; ++i) {
                if (world_scores[i] == INT64_MIN) {
                    min_seen[i] = true;
                } else {
                    scores[i] += world_scores[i];
                }
            }
        }
        for (size_t i = 0; i < n; ++i) {
            scores[i] = min_seen[i] ? INT64_MIN : scores[i] / k_worlds;
        }
    }

    // RUN-LAYER NO-OP GUARD. The legal mask can advertise a move whose
    // advance() is a documented no-op -- the live witness is Drug Dealer's
    // two-pick grid, where the FIRST-picked card stays advertised for the
    // second pick and re-picking it `return CONTINUE`s unchanged
    // (city_events_i.cpp drug_dealer_choose). A uniform-random policy walks
    // out of that corner by luck; a deterministic argmax repeats its top
    // choice forever and ends the run as run_case's NO_PROGRESS finding
    // (first hit: STS90069 / sim_search / policy-seed 0, step 216). So before
    // the tie-break, every maximal run-layer candidate is proven to MUTATE
    // the controller on a snapshot (memcpy copy, one advance, memcmp) and a
    // non-mutating one is demoted; the loop re-runs on the next band until a
    // mutating candidate exists or every candidate is demoted (then the run
    // ends as the honest NO_PROGRESS it is). Deterministic: the test is a
    // pure function of the state, and the demotion order is the score order.
    if (!in_combat) {
        for (;;) {
            int64_t m = scores[0];
            for (size_t i = 1; i < n; ++i) {
                if (scores[i] > m) m = scores[i];
            }
            bool any_mutates = false;
            bool any_at_max = false;
            for (size_t i = 0; i < n; ++i) {
                if (scores[i] != m) continue;
                any_at_max = true;
                // Deliberately `rc`, not `rollout_world`: this is a structural
                // check of whether the REAL game's advance() is a no-op for
                // this action (the Drug Dealer re-pick corner above), not part
                // of the rollout scoring the twin substitution covers.
                RunController sim = rc;
                apply_one(sim, moves[i].action);
                if (std::memcmp(&sim, &rc, sizeof(RunController)) != 0) {
                    any_mutates = true;
                } else {
                    scores[i] = INT64_MIN;
                }
            }
            if (any_mutates || !any_at_max || m == INT64_MIN) break;
        }
    }

    int64_t best = scores[0];
    size_t ties = 1;
    for (size_t i = 1; i < n; ++i) {
        if (scores[i] > best) {
            best = scores[i];
            ties = 1;
        } else if (scores[i] == best) {
            ++ties;
        }
    }
    // One rng draw per decision regardless of tie count (policy_pick's rule).
    uint32_t pick = rng.below(static_cast<uint32_t>(ties));
    for (size_t i = 0; i < n; ++i) {
        if (scores[i] == best) {
            if (pick == 0) return i;
            --pick;
        }
    }
    return 0;  // unreachable
}

}  // namespace sts::fuzz
