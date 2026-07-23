// Combat-fixture generator -- the INDEPENDENT reference simulator that
// produces the expected per-action state traces for the ~20 scripted Jaw Worm
// fights (design doc §9). The derivation notes
// (tests/golden/combat_fixtures/derivation_notes.md) narrate the arithmetic:
// it re-derives every expected CombatState from FIRST PRINCIPLES (the design-doc
// §5.5 damage pipeline, the five cards' effect table, the pile mechanics, and
// Jaw Worm's move effects), NOT by calling the production engine's gameplay
// code. It is a second, from-scratch implementation of the same frozen spec, so
// when fixture_oracle_test replays the same (seed, actions) through the real
// engine and diffs, an engine/spec divergence shows up as a diff rather than the
// two silently agreeing (the same differential-testing discipline as the Python
// Jaw Worm oracle and the hand-traced integration state).
//
// INDEPENDENCE CONSTRAINT: this file does
// NOT call combat_begin / advance / pump / queue_card_play / resolve_card_play /
// execute_opcode / compute_damage / draw_cards / jaw_worm_take_turn or any other
// production GAMEPLAY function to decide what a state "should" be. The only
// engine code it reuses is:
//   * the tier-1 RNG primitives (from_seed / floor_stream / random / random_long
//     / JdkRandom / jdk_shuffle) -- the JVM-golden-tested oracle layer. Using
//     them to compute the deck shuffle / reshuffle permutations is exactly what
//     piles_test does as its "independent oracle"; they are not the code under
//     test.
//   * the Jaw Worm fixture (tests/fixtures/jaw_worm_fixture.tsv) -- the
//     independently-derived oracle for the monster's move sequence + HP roll +
//     ai_rng/monster_hp_rng progression. Consuming it (not re-deriving the RNG
//     decision tree) is the sanctioned exception.
//   * the CombatState struct + write_trace (the data type and the fixture-file
//     writer) -- storage, not gameplay logic.
//
// SPEC re-derived here from first principles (cited inline):
//   * Cards (cards.hpp / Strike_Red/Defend_Red/Bash/ShrugItOff/PommelStrike.java):
//       Strike 1E: DAMAGE 6.            Defend 1E: BLOCK 5.
//       Bash   2E: DAMAGE 8, Vulnerable 2 (same target, damage BEFORE Vulnerable).
//       Shrug  1E: BLOCK 8, DRAW 1.     Pommel 1E: DAMAGE 9, DRAW 1.
//   * Damage pipeline (DamageInfo.applyPowers, design doc §5.5): float accumulate,
//       floor ONCE at end. Player attack into monster = floor(base * 1.5^[Vuln]).
//       Monster attack into player = base + monsterStrength (player carries no
//       Str/Weak/Vuln in the skeleton, and the monster carries no Weak). Then the
//       target's block absorbs, remainder hits hp (clamped >= 0).
//   * Jaw Worm A20 effects (JawWorm.takeTurn, JawWorm.java:120-146):
//       Chomp: 12 dmg.  Bellow: +5 Strength (self) then +9 block (self).
//       Thrash: 7 dmg then +5 block (self). Strength/Vulnerable never decay in
//       the skeleton (no power-decay hook; established by cards_test), and monster
//       block persists (no monster-turn block-loss modeled -- cards_test).
//   * Pump turn boundary (action_queue.cpp start_of_turn / pump_step): a monster
//       attack resolves BEFORE the player's block is decayed, so this turn's block
//       absorbs it; then start-of-turn zeroes player block, refills energy to 3,
//       ++turn, and draws 5. DiscardAtEndOfTurnAction first moves the ordinary
//       (non-ethereal) hand to discard. If the player dies on the
//       monster's turn, pump halts at COMBAT_OVER: start-of-turn does NOT run, and
//       any of the monster's still-queued effects after the lethal hit are not
//       applied -- so every death fixture is arranged to kill on a SINGLE-effect
//       move (Chomp) / single-effect card (Strike) to leave the queues empty.
//   * combat_begin (advance.cpp): card_pool = deck order; all 5 floor streams =
//       floor_stream(run_seed, floor) = from_seed(run_seed + floor); one
//       shuffle_rng.random_long() seeds the JDK Fisher-Yates over [0..11]; Jaw
//       Worm rolls HP (monster_hp_rng) + decision #1 (ai_rng); then start-of-turn
//       draws the opening 5 hand.
//
// SEED CONVENTION: each fight's run_seed is chosen as (fixture_seed - kFloor) so
// that floor_stream(run_seed, kFloor) == from_seed(fixture_seed) -- i.e. the
// combat's ai_rng / monster_hp_rng land exactly on the fixture's rN stream,
// letting us reuse that fixture's move sequence + rng end-states directly.
//
// USAGE (regen; run from the build, paths injected as compile definitions):
//   gen_combat_fixtures            -> writes every *.trace under STS_FIXTURE_OUT_DIR
//   gen_combat_fixtures --dump NAME-> prints the human-readable turn-by-turn trace
//                                     for one fixture (used to author/verify scripts)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <span>

#include "sts/engine/combat_state.hpp"
#include "sts/engine/rng_jdk.hpp"
#include "sts/engine/rng_stream.hpp"
#include "sts/engine/types.hpp"

#include "sts/diff/trace.hpp"

#ifndef STS_JAW_FIXTURE_DIR
#error "STS_JAW_FIXTURE_DIR must be defined (tests/fixtures)"
#endif
#ifndef STS_FIXTURE_OUT_DIR
#error "STS_FIXTURE_OUT_DIR must be defined (tests/golden/combat_fixtures)"
#endif

using namespace sts::engine;

namespace {

// The skeleton fight's fixed floor (matches diff::kSkeletonFloor / replay).
constexpr int32_t kFloor = 1;

// --- Deck / card facts (deck order == replay.cpp skeleton_deck) --------------
// pool 0..4 Strike(1E), 5..8 Defend(1E), 9 Bash(2E), 10 Shrug(1E), 11 Pommel(1E).
constexpr int kDeckSize = 12;
CardId pool_card_id(int idx) {
    if (idx < 5) return CardId::STRIKE;
    if (idx < 9) return CardId::DEFEND;
    if (idx == 9) return CardId::BASH;
    if (idx == 10) return CardId::SHRUG_IT_OFF;
    return CardId::POMMEL_STRIKE;
}
uint8_t pool_cost(int idx) { return (idx == 9) ? 2 : 1; }  // Bash costs 2
const char* pool_short(int idx) {
    switch (pool_card_id(idx)) {
        case CardId::STRIKE: return "Strike";
        case CardId::DEFEND: return "Defend";
        case CardId::BASH: return "Bash";
        case CardId::SHRUG_IT_OFF: return "Shrug";
        case CardId::POMMEL_STRIKE: return "Pommel";
        default: return "?";
    }
}

// Jaw Worm A20 stat constants (monster_jaw_worm.hpp; JawWorm.java ascension>=17).
constexpr int kChompDmg = 12;
constexpr int kThrashDmg = 7, kThrashBlock = 5;
constexpr int kBellowStr = 5, kBellowBlock = 9;
constexpr uint8_t kMoveChomp = 1, kMoveBellow = 2, kMoveThrash = 3;

// --- Jaw Worm fixture (jaw_worm_fixture.tsv) parsing ------------------------

struct TurnRow {
    uint8_t move = 0;
    uint64_t ai_s0 = 0, ai_s1 = 0;
    int32_t ai_counter = 0;
};
struct SeedData {
    std::string label;
    int64_t seed = 0;   // this is the Jaw Worm fixture seed (== from_seed arg)
    int hp = 0;
    uint64_t hp_s0 = 0, hp_s1 = 0;
    int32_t hp_counter = 0;
    std::vector<TurnRow> turns;  // turns[k] = turn (k+1): executed move + ai_rng after roll
};

std::vector<SeedData> load_jaw_fixture() {
    std::vector<SeedData> cases;
    std::ifstream in(std::string(STS_JAW_FIXTURE_DIR) + "/jaw_worm_fixture.tsv");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kind;
        std::getline(ss, kind, '\t');
        if (kind == "seed") {
            SeedData c;
            std::string v;
            std::getline(ss, c.label, '\t');
            std::getline(ss, v, '\t');
            c.seed = std::stoll(v);
            cases.push_back(std::move(c));
        } else if (kind == "hp") {
            SeedData& c = cases.back();
            std::string hp, s0, s1, ctr;
            std::getline(ss, hp, '\t'); std::getline(ss, s0, '\t');
            std::getline(ss, s1, '\t'); std::getline(ss, ctr, '\t');
            c.hp = std::stoi(hp); c.hp_s0 = std::stoull(s0);
            c.hp_s1 = std::stoull(s1); c.hp_counter = std::stoi(ctr);
        } else if (kind == "turn") {
            SeedData& c = cases.back();
            TurnRow r;
            std::string k, mv, s0, s1, ctr;
            std::getline(ss, k, '\t'); std::getline(ss, mv, '\t');
            std::getline(ss, s0, '\t'); std::getline(ss, s1, '\t');
            std::getline(ss, ctr, '\t');
            r.move = static_cast<uint8_t>(std::stoi(mv));
            r.ai_s0 = std::stoull(s0); r.ai_s1 = std::stoull(s1);
            r.ai_counter = std::stoi(ctr);
            c.turns.push_back(r);
        }
    }
    return cases;
}

const SeedData& find_seed(const std::vector<SeedData>& all, const std::string& label) {
    for (const auto& c : all) if (c.label == label) return c;
    std::fprintf(stderr, "unknown seed label %s\n", label.c_str());
    std::exit(2);
}

// Intent id, matching MonsterIntent in monster_jaw_worm.hpp by value (defined
// locally to keep this generator off the gameplay headers): NONE=0, ATTACK=1
// (Chomp), DEFEND_BUFF=2 (Bellow), ATTACK_DEFEND=3 (Thrash).
constexpr int kIntentAttack = 1, kIntentDefendBuff = 2, kIntentAttackDefend = 3;
int intent_of(uint8_t move) {
    if (move == kMoveChomp) return kIntentAttack;
    if (move == kMoveBellow) return kIntentDefendBuff;
    return kIntentAttackDefend;  // Thrash
}

// --- The independent reference simulator ------------------------------------

struct RefSim {
    const SeedData* fx = nullptr;

    // player
    int hp = 80, max_hp = 80, block = 0, energy = 0;
    int cards_played = 0;
    int turn = 0;
    CombatPhase phase = CombatPhase::NONE;

    // monster
    int m_hp = 0, m_maxhp = 0, m_block = 0;
    std::vector<std::pair<uint16_t, int>> m_powers;  // (PowerId, amount), append order
    int m_intent = 0;
    uint8_t mv_hist[3] = {0, 0, 0};
    int monster_turns_done = 0;

    // piles: pool indices. draw.back() == top of draw pile.
    std::vector<int> hand, draw, discard;

    // rng
    RngStream monster_hp_rng{}, ai_rng{}, shuffle_rng{}, card_random_rng{}, misc_rng{};

    int turn_has_ended = 0, monster_attacks_queued = 1;

    int m_strength() const {
        for (auto& p : m_powers)
            if (p.first == static_cast<uint16_t>(PowerId::STRENGTH)) return p.second;
        return 0;
    }
    bool monster_alive() const { return m_hp > 0; }
    bool player_alive() const { return hp > 0; }

    void apply_power_to_monster(PowerId id, int amount) {
        const uint16_t pid = static_cast<uint16_t>(id);
        for (auto& p : m_powers)
            if (p.first == pid) { p.second += amount; return; }
        m_powers.emplace_back(pid, amount);
    }

    // draw_n: pull `n` cards from draw top into hand, honoring the up-front hand
    // cap (min(n, 10 - hand)) and the empty-pile reshuffle (piles.cpp draw_cards).
    void draw_n(int n) {
        if (static_cast<int>(hand.size()) >= kHandCap) return;
        int cap = kHandCap - static_cast<int>(hand.size());
        if (n > cap) n = cap;
        for (int i = 0; i < n; ++i) {
            if (draw.empty()) {
                reshuffle();
                if (draw.empty()) break;  // both piles empty
            }
            hand.push_back(draw.back());
            draw.pop_back();
        }
    }

    // reshuffle: one shuffle_rng.random_long() seeds a JDK Fisher-Yates over the
    // discard, then the shuffled discard is appended onto the (empty) draw pile
    // in the SAME order -> draw.back() stays the top (piles.cpp).
    void reshuffle() {
        if (discard.empty()) return;
        const int64_t seed = random_long(shuffle_rng);
        JdkRandom jr(seed);
        std::vector<uint8_t> tmp(discard.begin(), discard.end());
        jdk_shuffle(std::span<uint8_t>(tmp.data(), tmp.size()), jr);
        for (uint8_t v : tmp) draw.push_back(v);
        discard.clear();
    }

    void begin(const SeedData& seed) {
        fx = &seed;
        RngStream base = from_seed(seed.seed);  // == floor_stream(run_seed, kFloor)
        monster_hp_rng = ai_rng = shuffle_rng = card_random_rng = misc_rng = base;

        // deck shuffle: one shuffle_rng.random_long() -> JDK Fisher-Yates.
        std::vector<uint8_t> d(static_cast<size_t>(kDeckSize));
        for (int i = 0; i < kDeckSize; ++i) d[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
        const int64_t js = random_long(shuffle_rng);
        JdkRandom jr(js);
        jdk_shuffle(std::span<uint8_t>(d.data(), d.size()), jr);
        draw.assign(d.begin(), d.end());  // draw[kDeckSize-1] == top

        // monster init: one monster_hp_rng draw (HP), one ai_rng draw (decision
        // #1, forced Chomp). Both streams start identical and take one draw, so
        // both land on the fixture's hp-row (s0,s1,counter=1).
        m_hp = m_maxhp = seed.hp;
        monster_hp_rng = RngStream{seed.hp_s0, seed.hp_s1, seed.hp_counter, 0};
        ai_rng = RngStream{seed.hp_s0, seed.hp_s1, 1, 0};
        mv_hist[0] = kMoveChomp; mv_hist[1] = 0; mv_hist[2] = 0;
        m_intent = kIntentAttack;

        // start of turn 1: energy 3, ++turn, block decay, draw opening 5.
        energy = 3;
        turn = 1;
        cards_played = 0;
        block = 0;
        turn_has_ended = 0;
        monster_attacks_queued = 1;
        draw_n(5);
        phase = CombatPhase::WAITING_ON_USER;
    }

    // Land `raw` post-pipeline damage on the monster (block absorbs first).
    // Returns true if the monster is now dead.
    bool hit_monster(int raw) {
        int dmg = raw;
        if (dmg >= m_block) { dmg -= m_block; m_block = 0; }
        else { m_block -= dmg; dmg = 0; }
        m_hp -= dmg;
        if (m_hp < 0) m_hp = 0;
        return m_hp <= 0;
    }
    bool hit_player(int raw) {
        int dmg = raw;
        if (dmg >= block) { dmg -= block; block = 0; }
        else { block -= dmg; dmg = 0; }
        hp -= dmg;
        if (hp < 0) hp = 0;
        return hp <= 0;
    }

    bool monster_vulnerable() const {
        for (auto& p : m_powers)
            if (p.first == static_cast<uint16_t>(PowerId::VULNERABLE)) return p.second > 0;
        return false;
    }
    // player attack: floor(base * 1.5) if monster Vulnerable, else base.
    int player_attack_damage(int base) const {
        if (monster_vulnerable()) {
            float v = static_cast<float>(base) * 1.5f;
            return static_cast<int>(static_cast<double>(v) + 16384.0) - 16384;  // MathUtils.floor
        }
        return base;
    }

    void play_card(int hand_index, int target /*monster slot, always 0*/) {
        (void)target;
        // A terminal state absorbs no further actions (the engine's advance()
        // pumps a COMBAT_OVER state to an immediate no-op). Fixtures never script
        // past the killing action, so this only guards --dump exploration.
        if (phase == CombatPhase::COMBAT_OVER) return;
        // Out-of-range hand index: queue_card_play returns false and the pump does
        // nothing (card_play.cpp) -- mirror that no-op instead of reading past the
        // hand (a mis-scripted index must be caught by the diff, not by UB here).
        if (hand_index < 0 || hand_index >= static_cast<int>(hand.size())) return;
        const int pool = hand[static_cast<size_t>(hand_index)];
        const CardId id = pool_card_id(pool);

        ++cards_played;
        hand.erase(hand.begin() + hand_index);
        discard.push_back(pool);
        energy -= pool_cost(pool);

        // Effects in addToBot order. On a monster kill, the pump halts before any
        // LATER effect of the same card resolves -- so kills must be single-effect
        // (Strike). We assert that here to catch a mis-scripted multi-effect kill.
        auto killed_note = [&](bool dead, const char* card) {
            if (dead) {
                // Strike is the only single-DAMAGE attack; a Bash/Pommel kill would
                // leave a trailing Vulnerable/Draw item queued -> a real diff.
                if (id != CardId::STRIKE) {
                    std::fprintf(stderr,
                        "FIXTURE BUG: %s killed the monster on a non-final effect "
                        "(would leave a queued item); use Strike for the killing blow\n",
                        card);
                    std::exit(3);
                }
                phase = CombatPhase::COMBAT_OVER;
            }
        };
        switch (id) {
            case CardId::STRIKE:
                killed_note(hit_monster(player_attack_damage(6)), "Strike");
                break;
            case CardId::DEFEND:
                block += 5;
                break;
            case CardId::BASH: {
                bool dead = hit_monster(player_attack_damage(8));  // damage BEFORE Vuln
                if (dead) { killed_note(true, "Bash"); break; }
                apply_power_to_monster(PowerId::VULNERABLE, 2);
                break;
            }
            case CardId::SHRUG_IT_OFF:
                block += 8;
                draw_n(1);
                break;
            case CardId::POMMEL_STRIKE: {
                bool dead = hit_monster(player_attack_damage(9));
                if (dead) { killed_note(true, "Pommel"); break; }
                draw_n(1);
                break;
            }
            default: break;
        }
        if (phase != CombatPhase::COMBAT_OVER) phase = CombatPhase::WAITING_ON_USER;
    }

    void end_turn() {
        if (phase == CombatPhase::COMBAT_OVER) return;  // see play_card guard
        // DiscardAtEndOfTurnAction follows the card-queue sentinel. This fixture
        // deck has no ethereal or Retain cards, so every hand card moves from the
        // top (tail) to discard before the monster turn, matching piles.cpp.
        for (auto it = hand.rbegin(); it != hand.rend(); ++it) {
            discard.push_back(*it);
        }
        hand.clear();
        // sentinel -> turn_has_ended, clear monster_attacks_queued; step 4 requeues
        // and step 5 runs the monster turn.
        turn_has_ended = 1;
        monster_attacks_queued = 1;  // net effect after step 3 clears + step 4 sets

        const int M = monster_turns_done + 1;
        const uint8_t move = fx->turns[static_cast<size_t>(M - 1)].move;

        // (a) ROLL happens first inside take_turn (before the effects resolve):
        //     advance ai_rng + move_history + intent to decision #(M+1).
        const TurnRow& row = fx->turns[static_cast<size_t>(M - 1)];
        ai_rng = RngStream{row.ai_s0, row.ai_s1, row.ai_counter, 0};
        const uint8_t dec_next = fx->turns[static_cast<size_t>(M)].move;  // decision #(M+1)
        mv_hist[2] = (M >= 2) ? fx->turns[static_cast<size_t>(M - 2)].move : 0;
        mv_hist[1] = move;
        mv_hist[0] = dec_next;
        m_intent = intent_of(dec_next);
        monster_turns_done = M;

        // (b) Execute the move's effects (Strength read live from m_powers).
        bool player_dead = false;
        if (move == kMoveChomp) {
            player_dead = hit_player(kChompDmg + m_strength());
        } else if (move == kMoveBellow) {
            apply_power_to_monster(PowerId::STRENGTH, kBellowStr);
            m_block += kBellowBlock;
        } else if (move == kMoveThrash) {
            player_dead = hit_player(kThrashDmg + m_strength());
            if (!player_dead) m_block += kThrashBlock;
            // If a Thrash is LETHAL, its trailing block item is left queued -> a
            // real diff. Death fixtures must kill on a Chomp (single effect).
            if (player_dead) {
                std::fprintf(stderr,
                    "FIXTURE BUG: lethal Thrash leaves a queued block item; arrange "
                    "the player to die on a Chomp turn instead\n");
                std::exit(3);
            }
        }

        if (player_dead) {
            phase = CombatPhase::COMBAT_OVER;  // start-of-turn does NOT run
            return;
        }

        // start-of-turn: reset counters, energy, block decay, ++turn, draw 5.
        cards_played = 0;
        energy = 3;
        turn_has_ended = 0;
        ++turn;
        block = 0;                 // player block decays AFTER the monster's hit
        monster_attacks_queued = 1;
        draw_n(5);
        phase = CombatPhase::WAITING_ON_USER;
    }

    CombatState snapshot() const {
        CombatState s{};
        // card pool (deck order, never mutates).
        for (int i = 0; i < kDeckSize; ++i) {
            s.card_pool[i].card_id = static_cast<uint16_t>(pool_card_id(i));
            s.card_pool[i].cost_now = pool_cost(i);
        }
        s.phase = static_cast<uint8_t>(phase);
        s.turn = static_cast<uint16_t>(turn);
        s.player_hp = static_cast<int16_t>(hp);
        s.player_max_hp = static_cast<int16_t>(max_hp);
        s.player_block = static_cast<int16_t>(block);
        s.player_energy = static_cast<int16_t>(energy);
        s.cards_played_this_turn = static_cast<uint8_t>(cards_played);
        s.player_power_count = 0;

        for (size_t i = 0; i < hand.size(); ++i) s.hand[i] = static_cast<uint8_t>(hand[i]);
        s.hand_count = static_cast<uint8_t>(hand.size());
        for (size_t i = 0; i < draw.size(); ++i) s.draw[i] = static_cast<uint8_t>(draw[i]);
        s.draw_count = static_cast<uint8_t>(draw.size());
        for (size_t i = 0; i < discard.size(); ++i) s.discard[i] = static_cast<uint8_t>(discard[i]);
        s.discard_count = static_cast<uint8_t>(discard.size());

        s.monster_count = 1;
        s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
        s.monsters[0].hp = static_cast<int16_t>(m_hp);
        s.monsters[0].max_hp = static_cast<int16_t>(m_maxhp);
        s.monsters[0].block = static_cast<int16_t>(m_block);
        s.monsters[0].move_history[0] = mv_hist[0];
        s.monsters[0].move_history[1] = mv_hist[1];
        s.monsters[0].move_history[2] = mv_hist[2];
        s.monsters[0].intent = static_cast<uint8_t>(m_intent);
        s.monsters[0].power_count = static_cast<uint8_t>(m_powers.size());
        for (size_t i = 0; i < m_powers.size(); ++i) {
            s.monsters[0].powers[i].power_id = m_powers[i].first;
            s.monsters[0].powers[i].amount = static_cast<int16_t>(m_powers[i].second);
        }

        s.turn_has_ended = static_cast<uint8_t>(turn_has_ended);
        s.monster_attacks_queued = static_cast<uint8_t>(monster_attacks_queued);

        s.monster_hp_rng = monster_hp_rng;
        s.ai_rng = ai_rng;
        s.shuffle_rng = shuffle_rng;
        s.card_random_rng = card_random_rng;
        s.misc_rng = misc_rng;
        return s;
    }
};

// --- Fixture scripts ---------------------------------------------------------

struct ScriptAction {
    ActionVerb verb;
    uint8_t hand_index;  // for PLAY_CARD
    uint8_t target;      // monster slot (always 0 in the skeleton)
};
ScriptAction Play(uint8_t h) { return {ActionVerb::PLAY_CARD, h, 0}; }
ScriptAction End() { return {ActionVerb::END_TURN, 0, 0}; }

struct Fixture {
    std::string name;
    std::string seed_label;
    std::vector<ScriptAction> actions;
    std::string coverage;  // human note for the derivation doc
};

std::vector<Fixture> all_fixtures();

// --- Verbose dump (script authoring / review aid) ---------------------------

void print_state(const RefSim& g, const char* tag) {
    std::printf("  [%s] turn=%d phase=%d hp=%d/%d blk=%d en=%d cpt=%d | mon hp=%d/%d blk=%d",
                tag, g.turn, static_cast<int>(g.phase), g.hp, g.max_hp, g.block, g.energy,
                g.cards_played, g.m_hp, g.m_maxhp, g.m_block);
    std::printf(" pow[");
    for (auto& p : g.m_powers) std::printf("%u:%d ", p.first, p.second);
    std::printf("] mv[%u,%u,%u] intent=%d\n", g.mv_hist[0], g.mv_hist[1], g.mv_hist[2], g.m_intent);
    std::printf("       hand{");
    for (int h : g.hand) std::printf("%s%d ", pool_short(h), h);
    std::printf("} draw(top..bot){");
    for (auto it = g.draw.rbegin(); it != g.draw.rend(); ++it) std::printf("%s%d ", pool_short(*it), *it);
    std::printf("} disc{");
    for (int d : g.discard) std::printf("%s%d ", pool_short(d), d);
    std::printf("} shufctr=%d aictr=%d\n", g.shuffle_rng.counter, g.ai_rng.counter);
}

void run_dump(const Fixture& f, const std::vector<SeedData>& all) {
    RefSim g;
    g.begin(find_seed(all, f.seed_label));
    std::printf("== %s (seed %s) : %s ==\n", f.name.c_str(), f.seed_label.c_str(), f.coverage.c_str());
    print_state(g, "init");
    int idx = 0;
    for (const ScriptAction& a : f.actions) {
        if (a.verb == ActionVerb::PLAY_CARD) {
            std::printf("  -> action %d PLAY hand[%d]=%s\n", idx, a.hand_index,
                        pool_short(g.hand[a.hand_index]));
            g.play_card(a.hand_index, a.target);
        } else {
            std::printf("  -> action %d END_TURN\n", idx);
            g.end_turn();
        }
        print_state(g, "post");
        ++idx;
    }
}

bool write_fixture(const Fixture& f, const std::vector<SeedData>& all) {
    const SeedData& sd = find_seed(all, f.seed_label);
    const int64_t run_seed = sd.seed - kFloor;  // floor_stream(run_seed,kFloor)==from_seed(sd.seed)

    RefSim g;
    g.begin(sd);

    std::vector<CombatState> states;
    std::vector<Action> actions;
    states.push_back(g.snapshot());  // record[0] = initial

    for (const ScriptAction& a : f.actions) {
        if (a.verb == ActionVerb::PLAY_CARD) {
            actions.push_back(make_action(ActionVerb::PLAY_CARD, a.hand_index, a.target, 0));
            g.play_card(a.hand_index, a.target);
        } else {
            actions.push_back(make_action(ActionVerb::END_TURN, 0, 0, 0));
            g.end_turn();
        }
        states.push_back(g.snapshot());
    }

    const std::string path = std::string(STS_FIXTURE_OUT_DIR) + "/" + f.name + ".trace";
    const bool ok = sts::diff::write_trace(
        path, run_seed,
        std::span<const Action>(actions.data(), actions.size()),
        std::span<const CombatState>(states.data(), states.size()));
    if (!ok) {
        std::fprintf(stderr, "write_trace failed for %s\n", path.c_str());
        return false;
    }
    std::printf("wrote %s (%zu actions, %zu records, run_seed=%lld)\n",
                path.c_str(), actions.size(), states.size(), static_cast<long long>(run_seed));
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<SeedData> all = load_jaw_fixture();
    if (all.size() != 32) {
        std::fprintf(stderr, "jaw fixture parse failed (got %zu seeds)\n", all.size());
        return 1;
    }
    const std::vector<Fixture> fixtures = all_fixtures();

    if (argc >= 3 && std::strcmp(argv[1], "--dump") == 0) {
        for (const auto& f : fixtures)
            if (f.name == argv[2]) { run_dump(f, all); return 0; }
        std::fprintf(stderr, "no fixture named %s\n", argv[2]);
        return 2;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--dumpall") == 0) {
        for (const auto& f : fixtures) run_dump(f, all);
        return 0;
    }

    int ok = 0;
    for (const auto& f : fixtures) ok += write_fixture(f, all) ? 1 : 0;
    std::printf("generated %d/%zu fixtures\n", ok, fixtures.size());
    return (ok == static_cast<int>(fixtures.size())) ? 0 : 1;
}

// ============================================================================
// The 20 scripted fights. Each is (seed, action list); the coverage string is
// mirrored in tests/golden/combat_fixtures/derivation_notes.md with the full
// turn-by-turn arithmetic. Hand indices are the LIVE index at the moment of the
// play (a play shifts later cards down; start-of-turn appends drawn cards at the
// end of the hand). Authored/verified with --dump.
// ============================================================================

namespace {
std::vector<Fixture> all_fixtures() {
    std::vector<Fixture> f;

    // 01 -- Strike basics: one Strike into the monster, then the monster Chomps.
    f.push_back({"fixt01_r0_strike", "r0",
                 {Play(1), End()},
                 "Strike (6 dmg); monster Chomp 12 into an unblocked player"});

    // 02 -- Defend absorbs a Chomp.
    f.push_back({"fixt02_r0_defend", "r0",
                 {Play(0), End()},
                 "Defend (+5 block); Chomp 12 partially absorbed by block 5"});

    // 03 -- Pommel Strike (draws) then a Strike.
    f.push_back({"fixt03_r2_pommel", "r2",
                 {Play(0), Play(0), End()},
                 "Pommel 9 (+draw), Strike 6; Chomp 12"});

    // 04 -- Shrug It Off (block + draw), Chomp partly absorbed.
    f.push_back({"fixt04_r7_shrug", "r7",
                 {Play(0), End()},
                 "Shrug It Off (+8 block, draw 1); Chomp 12 mostly absorbed"});

    // 05 -- Bash applies Vulnerable, then a Strike gets the x1.5 bonus.
    f.push_back({"fixt05_r5_bash_vuln", "r5",
                 {Play(1), Play(0), End()},
                 "Bash 8 + Vulnerable 2; Strike into Vulnerable = 9; Chomp 12"});

    // 06 -- Shrug It Off + Pommel Strike, both drawing; then Chomp vs block.
    f.push_back({"fixt06_r3_shrug_pommel", "r3",
                 {Play(4), Play(2), End()},
                 "Shrug (+8 block, draw), Pommel 9 (draw); Chomp 12 vs block 8"});

    // 07 -- Bash + Strike into Vulnerable, different seed/HP.
    f.push_back({"fixt07_r13_bash_strike", "r13",
                 {Play(4), Play(0), End()},
                 "Bash 8 + Vulnerable; Strike into Vulnerable = 9; Chomp 12"});

    // 08 -- Two-turn fight, heavy blocking; monster Bellow on turn 2 (0 damage).
    f.push_back({"fixt08_r19_block_two_turns", "r19",
                 {Play(0), Play(0), Play(0), End(), Play(0), Play(0), End()},
                 "Defend/Defend/Strike, Chomp partly blocked; turn 2 into a Bellow"});

    // 09 -- Bash then Pommel into Vulnerable (13 dmg).
    f.push_back({"fixt09_r6_bash_pommel_vuln", "r6",
                 {Play(4), Play(0), End()},
                 "Bash 8 + Vulnerable; Pommel into Vulnerable = 13; Chomp 12"});

    // 10 -- Shrug + Defend + Strike (block stacking) then Chomp fully absorbed.
    f.push_back({"fixt10_r8_block_stack", "r8",
                 {Play(1), Play(0), End()},
                 "Shrug 8 + Defend 5 = block 13; Chomp 12 fully absorbed"});

    // 11 -- Pommel + Shrug (both draw) on another seed.
    f.push_back({"fixt11_r10_draws", "r10",
                 {Play(1), Play(0), End()},
                 "Pommel 9 (draw), Shrug 8 (draw); Chomp 12 vs block 8"});

    // 12 -- Bash + Pommel into Vulnerable, longer (2 turns).
    f.push_back({"fixt12_r15_bash_pommel", "r15",
                 {Play(3), Play(2), End(), Play(0), End()},
                 "Bash + Pommel into Vulnerable; two turns, monster Bellow turn 2"});

    // 13 -- Bash + Strike + Strike into Vulnerable; monster low.
    f.push_back({"fixt13_r21_triple", "r21",
                 {Play(2), Play(0), Play(2), End()},
                 "Bash 8 + Vuln, Strike 9, Pommel 13 (into Vuln); Chomp 12"});

    // 14 -- Pommel then Strikes.
    f.push_back({"fixt14_r23_pommel_strikes", "r23",
                 {Play(1), Play(1), Play(1), End()},
                 "Pommel 9, Strike 6, Strike 6; Chomp 12"});

    // 15 -- Mixed skill/attack, two turns.
    f.push_back({"fixt15_r30_mixed_two_turns", "r30",
                 {Play(1), Play(0), End(), Play(0), Play(0), End()},
                 "Shrug + Strike, then two more; monster Chomp/Thrash across turns"});

    // 16 -- MONSTER DEATH: Bash for Vulnerable turn 1, finish with Strikes on
    // turn 2 (killing blow is a single-effect Strike so no queued item lingers).
    f.push_back({"fixt16_r29_monster_death", "r29",
                 {Play(0), Play(0), End(), Play(1), Play(1), Play(3)},
                 "MONSTER DEATH: Bash+Strike into Vuln, then 3 Strikes; Strike kills"});

    // 17 -- PLAYER DEATH: never block; Bellow-stacked Strength escalates the
    // monster's attacks until a Chomp (single effect) kills on turn 9.
    f.push_back({"fixt17_r4_player_death", "r4",
                 {End(), End(), End(), End(), End(), End(), End(), End(), End()},
                 "PLAYER DEATH: no blocking; Strength-boosted Chomp kills on turn 9"});

    // 18 -- RESHUFFLE + Bellow-Strength/Vulnerable OVERLAP: Bash (Vuln) turn 1,
    // play through turns so the 7-card draw pile empties and the discard
    // reshuffles (shuffle_rng draws a 2nd time), while a turn-2 Bellow gives the
    // still-Vulnerable monster Strength (both powers live at once).
    f.push_back({"fixt18_r0_reshuffle_overlap", "r0",
                 {Play(3), Play(0), End(),
                  Play(0), Play(0), Play(0), End(),
                  Play(0), End()},
                 "RESHUFFLE + Bellow-Strength & Vulnerable overlap on the monster"});

    // 19 -- Longer blocking fight on another seed (exercises repeated Defends and
    // multi-turn monster damage with Strength growth).
    f.push_back({"fixt19_r9_long_block", "r9",
                 {Play(1), Play(0), End(), Play(0), Play(0), End(), Play(0), End()},
                 "Multi-turn Defends vs escalating monster (Thrash/Bellow)"});

    // 20 -- Bash + attacks over two turns into a Vulnerable, Strength-buffed foe.
    f.push_back({"fixt20_r18_bash_two_turns", "r18",
                 {Play(1), Play(0), End(), Play(0), Play(0), End()},
                 "Bash Vuln turn 1; attacks turn 2 while monster gains Strength"});

    return f;
}
}  // namespace
