// How a combat STARTS -- the turn-1 block, and the guard that both
// combat-construction paths keep running the same one.
//
// THE DEFECT THIS PINS. Turn 1 used to be primed by setting turn_has_ended and
// letting getNextAction's step-6 branch (GameActionManager.java:329-366) do the
// setup. That branch opens with monsters.applyEndOfTurnPowers()
// (GameActionManager.java:331 -> MonsterGroup.java:290-304 ->
// AbstractCreature.applyEndOfTurnTriggers, AbstractCreature.java:547-553), so
// every end-of-round hook fired once before the player had taken a turn.
//
// The game cannot reach that branch on turn 1. AbstractRoom.update
// (AbstractRoom.java:236-243) sets turnHasEnded = true and in the very next line
// queues GainEnergyAndEnableControlsAction, whose update clears it again
// (GainEnergyAndEnableControlsAction.java:35); the action queue is never empty
// while that item is pending, so getNextAction takes step 1 and the step-6 test
// is false by the time it is reached. The turn-1 block does the start-of-turn
// work itself, and it has no applyEndOfTurnPowers line.
//
// THE REPRODUCER. A sleeping Lagavulin's usePreBattleAction gives it 8 block AND
// a MetallicizePower(8) (Lagavulin.java:102-114, ARMOR_AMT :66), and
// MetallicizePower binds atEndOfTurnPreEndTurnCards (MetallicizePower.java:38-42)
// -- an end-of-round hook, on a power that exists before the player acts. So a
// spurious end-of-round pass during combat construction is directly visible as 16
// block on turn 1 instead of 8. From turn 2 on the armour HOLDS at 8: the monster
// phase's applyPreTurnLogic clears the block before Metallicize re-grants it, so
// each tick replaces the previous one rather than adding to it.
//
// Provenance (read in full): AbstractRoom.update (AbstractRoom.java:229-258);
// GainEnergyAndEnableControlsAction.java:22-38; GameActionManager.getNextAction
// (GameActionManager.java:303-366) + clear() (:420-435) + field initialisers
// (:74-76); MonsterGroup.applyEndOfTurnPowers (MonsterGroup.java:290-304);
// AbstractCreature.applyEndOfTurnTriggers (AbstractCreature.java:547-553);
// MetallicizePower.java:19-42; Lagavulin.java:60-66,102-114,116-174,212-227.
//
// THE SECOND DEFECT THIS PINS, same shape, other end of the sequence. The
// combat-start block and step 6 also disagree about the post-draw POWER pass.
// AbstractRoom's turn-1 block calls applyStartOfTurnRelics (:253),
// applyStartOfTurnPostDrawRelics (:254), applyStartOfTurnCards (:255),
// applyStartOfTurnPowers (:256) and applyStartOfTurnOrbs (:257) -- and there is
// NO applyStartOfTurnPostDrawPowers line among them. The game's only call to it
// is GameActionManager.java:363, in the step-6 branch (grep over the whole
// reference tree: two occurrences, the other being the definition,
// AbstractCreature.applyStartOfTurnPostDrawPowers, AbstractCreature.java:
// 541-545). Note that the relic and power halves are NOT a pair: the relic half
// really is on both sides, which is why only the power half is gated.
//
// THE REPRODUCER IS NOT REACHABLE THROUGH THE RUN LAYER, and that is precisely
// why the divergence survived. The only two powers that bind
// atStartOfTurnPostDraw are Brutality (BrutalityPower.java:34-39 -- draw amount,
// then lose that much HP) and Demon Form (DemonFormPower.java:32-36 -- gain
// amount Strength), and both are applied only by playing their card, so neither
// can be on the player when the turn-1 block runs. The two tests below therefore
// CONSTRUCT the state with the power already present, which is the only way to
// observe it, and count the triggers across turns 1, 2 and 3.

#include "sts/engine/action_queue.hpp"

#include <cstdint>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "sts/engine/advance.hpp"
#include "sts/engine/cards.hpp"
#include "sts/engine/combat_state.hpp"
#include "sts/engine/encounters.hpp"
#include "sts/engine/interp.hpp"  // Opcode / ChoiceKind (the union order pin)
#include "sts/engine/run_advance.hpp"
#include "sts/engine/powers.hpp"
#include "sts/engine/run_state.hpp"  // kMapCols
#include "sts/engine/types.hpp"

#ifndef STS_SOURCE_ROOT
#error "STS_SOURCE_ROOT must be defined by tests/CMakeLists.txt"
#endif

namespace sts::engine {
namespace {

constexpr uint8_t kA20 = 20;
constexpr int64_t kSeed = 12345;

// ARMOR_AMT (Lagavulin.java:66) -- the pre-battle block, the Metallicize stack,
// and therefore the size of one armour tick.
constexpr int16_t kArmor = 8;

const Action kProceed = make_action(ActionVerb::CHOOSE);

void step(RunController& rc, Action a) {
    StepResult res{};
    advance(std::span<RunController>(&rc, 1), std::span<const Action>(&a, 1),
            std::span<StepResult>(&res, 1));
}

// Leave Neow with floor 0 otherwise untouched. There is no such button in the
// game -- every run takes one of the four blessings -- but these tests are
// about the floor loop, and a blessing payout moves streams, the master deck
// and the relic pools underneath them. Forcing the finished-payout screen and
// pressing the map button exercises exactly the transition the last Neow press
// makes; the blessing itself is neow_test's subject.
void leave_neow(RunController& rc) {
    rc.neow.screen = static_cast<uint8_t>(NeowScreen::DONE);
    step(rc, kProceed);
}

uint8_t first_start_column(const RunController& rc) {
    RunActionMask m{};
    legal_actions(rc, m);
    for (uint8_t x = 0; x < kMapCols; ++x) {
        if (m.can_choose_node[x]) return x;
    }
    ADD_FAILURE() << "no legal start column";
    return 0;
}

// Walk the REAL run path (run_begin -> map pick -> onPlayerEntry -> enter_combat)
// into a floor-1 combat against `encounter`. Substituting the encounter key is
// the only shortcut: it puts an arbitrary encounter in front of the genuine
// combat-construction path instead of hunting a seed whose map happens to open
// on the elite we need.
RunController enter_floor_one_combat(std::string_view encounter) {
    RunController rc = run_begin(kSeed, kA20);
    rc.lists.monster_list[0] = encounter;
    leave_neow(rc);
    step(rc, make_action(ActionVerb::CHOOSE, first_start_column(rc)));
    EXPECT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT))
        << "floor-1 room did not build a combat for encounter " << encounter;
    return rc;
}

int16_t monster_power_amount(const CombatState& s, uint8_t mi, PowerId id) {
    const MonsterState& m = s.monsters[mi];
    for (uint8_t i = 0; i < m.power_count; ++i) {
        if (m.powers[i].power_id == static_cast<uint16_t>(id)) {
            return m.powers[i].amount;
        }
    }
    return -1;
}

// =============================================================================
// The regression test: an end-of-round power present at combat start
// =============================================================================

TEST(CombatStart, SleepingLagavulinHasOnlyItsPreBattleArmourOnTurnOne) {
    RunController rc = enter_floor_one_combat("Lagavulin");
    const CombatState& s = rc.combat;

    ASSERT_EQ(s.monster_count, 1);
    ASSERT_EQ(s.monsters[0].monster_id, static_cast<uint16_t>(MonsterId::LAGAVULIN));
    EXPECT_EQ(monster_power_amount(s, 0, PowerId::METALLICIZE), kArmor)
        << "usePreBattleAction must have applied Metallicize before turn 1";

    // GainBlockAction(this, this, ARMOR_AMT) and nothing else. A second tick here
    // means the combat-start priming ran the end-of-round pass.
    EXPECT_EQ(s.monsters[0].block, kArmor)
        << "the turn-1 block (AbstractRoom.java:236-258) has no "
           "applyEndOfTurnPowers line -- Metallicize must not have triggered yet";
    EXPECT_EQ(s.turn, 1);
    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
}

TEST(CombatStart, LagavulinArmourTicksOncePerCompletedRound) {
    RunController rc = enter_floor_one_combat("Lagavulin");
    ASSERT_EQ(rc.combat.monsters[0].block, kArmor);

    // Each END_TURN closes a round: sentinel -> monster turn (asleep: IDLE) ->
    // step 6, whose applyEndOfTurnPowers pass is where Metallicize belongs.
    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.turn, 2);
    EXPECT_EQ(rc.combat.monsters[0].block, kArmor)
        << "one tick, not two: applyPreTurnLogic cleared the pre-battle 8 at the "
           "start of the monster's turn, then Metallicize granted 8 again";

    step(rc, make_action(ActionVerb::END_TURN));
    ASSERT_EQ(rc.phase, static_cast<uint8_t>(RunPhase::COMBAT));
    EXPECT_EQ(rc.combat.turn, 3);
    EXPECT_EQ(rc.combat.monsters[0].block, kArmor);
}

// =============================================================================
// The regression test: a post-draw start-of-turn PLAYER power at combat start
// =============================================================================

// A combat assembled directly in the state, so a power can already be on the
// player when begin_first_turn runs. The run layer cannot produce that for
// either post-draw power (both need their card played), so this is the only way
// the extra dispatch is observable at all.
CombatState make_constructed_combat() {
    CombatState s{};
    s.player_hp = 70;
    s.player_max_hp = 80;
    s.monster_count = 1;
    s.monsters[0].monster_id = static_cast<uint16_t>(MonsterId::JAW_WORM);
    s.monsters[0].hp = 100;
    s.monsters[0].max_hp = 100;
    s.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    // Deep enough that no turn below reshuffles: 3 turns x (5 + at most 1) < 20.
    for (int i = 0; i < 20; ++i) {
        const CardDef* def = card_def(CardId::STRIKE);
        EXPECT_NE(def, nullptr);
        const auto pi = static_cast<CardPoolIndex>(i);
        s.card_pool[pi].card_id = static_cast<uint16_t>(CardId::STRIKE);
        s.card_pool[pi].cost_now = card_cost(*def, 0);
        s.card_pool[pi].flags = card_flags(*def, 0);
        s.draw[s.draw_count++] = pi;
    }
    return s;
}

void give_player_power(CombatState& s, PowerId id, int16_t amount) {
    s.player_powers[s.player_power_count] =
        PowerSlot{static_cast<uint16_t>(id), amount, 0, 0};
    ++s.player_power_count;
}

const PowerSlot* player_power(const CombatState& s, PowerId id) {
    for (uint8_t i = 0; i < s.player_power_count; ++i) {
        if (s.player_powers[i].power_id == static_cast<uint16_t>(id)) {
            return &s.player_powers[i];
        }
    }
    return nullptr;
}

// Close a round: the end-turn sentinel, then drain through step 6 (no monster AI,
// so nothing else touches the player).
void end_turn(CombatState& s) {
    add_card_to_queue_bottom(s, make_end_turn_sentinel());
    pump(s, default_monster_turn);
}

// Demon Form counts its own triggers: Strength is a running total, so the stack
// after N turns IS the number of times the post-draw pass ran. Base amount 2
// (DemonForm.java:27).
TEST(CombatStart, DemonFormGrantsNoStrengthOnTurnOne) {
    CombatState s = make_constructed_combat();
    give_player_power(s, PowerId::DEMON_FORM, 2);

    begin_first_turn(s);

    ASSERT_EQ(s.turn, 1);
    ASSERT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.hand_count, static_cast<uint8_t>(kStartOfTurnDrawCount));
    EXPECT_EQ(player_power(s, PowerId::STRENGTH), nullptr)
        << "the turn-1 block (AbstractRoom.java:253-257) has no "
           "applyStartOfTurnPostDrawPowers line -- Demon Form must not have "
           "triggered before the player has acted";

    // Turn 2 is the FIRST trigger: step 6 does carry the line
    // (GameActionManager.java:363).
    end_turn(s);
    ASSERT_EQ(s.turn, 2);
    const PowerSlot* str = player_power(s, PowerId::STRENGTH);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->amount, 2) << "exactly one post-draw trigger by turn 2";

    end_turn(s);
    ASSERT_EQ(s.turn, 3);
    ASSERT_NE(player_power(s, PowerId::STRENGTH), nullptr);
    EXPECT_EQ(player_power(s, PowerId::STRENGTH)->amount, 4)
        << "one trigger per completed round thereafter";
}

// The other binder, and a different mechanism: Brutality queues DrawCardAction
// then LoseHPAction (BrutalityPower.java:34-39), so its trigger shows in the hand
// size and the HP, not in a power stack. Base amount 1 (Brutality.java:31).
TEST(CombatStart, BrutalityNeitherDrawsNorCostsHpOnTurnOne) {
    CombatState s = make_constructed_combat();
    give_player_power(s, PowerId::BRUTALITY, 1);
    const int16_t hp_before = s.player_hp;

    begin_first_turn(s);

    ASSERT_EQ(s.turn, 1);
    EXPECT_EQ(s.hand_count, static_cast<uint8_t>(kStartOfTurnDrawCount))
        << "turn 1 draws gameHandSize and nothing more "
           "(AbstractRoom.java:242, :253-257)";
    EXPECT_EQ(s.player_hp, hp_before)
        << "no post-draw power pass on turn 1 means no HP_LOSS on turn 1";

    end_turn(s);
    ASSERT_EQ(s.turn, 2);
    EXPECT_EQ(s.hand_count, static_cast<uint8_t>(kStartOfTurnDrawCount + 1))
        << "step 6's post-draw pass draws Brutality's extra card";
    EXPECT_EQ(s.player_hp, static_cast<int16_t>(hp_before - 1))
        << "and charges exactly one HP for it, once";
}

// =============================================================================
// The two combat-construction paths must agree
// =============================================================================

// The observable turn-1 contract. combat_begin (advance.cpp) and enter_combat
// (run_advance.cpp) build different decks against different encounters, so the
// states differ everywhere else -- these are the fields the shared turn-1 block
// owns.
struct Priming {
    int32_t turn = 0;
    uint8_t phase = 0;
    uint8_t turn_has_ended = 0;
    uint8_t monster_attacks_queued = 0;
    int16_t player_energy = 0;
    uint8_t hand_count = 0;
    uint8_t cards_played_this_turn = 0;
    uint8_t action_count = 0;
    uint8_t pre_turn_count = 0;
    uint8_t card_queue_count = 0;
    uint8_t monster_queue_count = 0;

    bool operator==(const Priming&) const = default;
};

Priming priming_of(const CombatState& s) {
    Priming p;
    p.turn = static_cast<int32_t>(s.turn);
    p.phase = s.phase;
    p.turn_has_ended = s.turn_has_ended;
    p.monster_attacks_queued = s.monster_attacks_queued;
    p.player_energy = s.player_energy;
    p.hand_count = s.hand_count;
    p.cards_played_this_turn = s.cards_played_this_turn;
    p.action_count = s.action_count;
    p.pre_turn_count = s.pre_turn_count;
    p.card_queue_count = s.card_queue_count;
    p.monster_queue_count = s.monster_queue_count;
    return p;
}

Priming expected_priming() {
    Priming p;
    p.turn = 1;                      // clear(): turn = 1 (GameActionManager.java:432)
    p.phase = static_cast<uint8_t>(CombatPhase::WAITING_ON_USER);
    p.turn_has_ended = 0;            // GainEnergyAndEnableControlsAction.java:35
    p.monster_attacks_queued = 1;    // field initialiser (GameActionManager.java:76)
    p.player_energy = kIroncladBaseEnergy;
    p.hand_count = static_cast<uint8_t>(kStartOfTurnDrawCount);
    return p;                        // every queue drained, nothing played yet
}

TEST(CombatStart, CombatBeginAndEnterCombatPrimeTurnOneIdentically) {
    // The M1 skeleton deck (stage-a design §9) -- combat_begin's own scenario.
    std::vector<CardId> deck;
    for (int i = 0; i < 5; ++i) deck.push_back(CardId::STRIKE);
    for (int i = 0; i < 4; ++i) deck.push_back(CardId::DEFEND);
    deck.push_back(CardId::BASH);
    deck.push_back(CardId::SHRUG_IT_OFF);
    deck.push_back(CardId::POMMEL_STRIKE);
    const CombatState direct = combat_begin(kSeed, 1, std::span<const CardId>(deck));
    const RunController rc = enter_floor_one_combat("Jaw Worm");

    EXPECT_EQ(priming_of(direct), expected_priming());
    EXPECT_EQ(priming_of(rc.combat), expected_priming());
    EXPECT_EQ(priming_of(direct), priming_of(rc.combat))
        << "combat_begin and enter_combat must not disagree about how a combat "
           "starts";
}

// =============================================================================
// ... and must keep agreeing BY CONSTRUCTION
// =============================================================================
//
// The behavioural test above can only compare what both paths can reach, and the
// encounter combat_begin builds (a lone Jaw Worm) binds no end-of-round hook --
// so it would stay green if one path were fixed and the other left priming by
// hand. This is the guard that actually fails in that case: both paths must
// delegate to the one shared turn-1 block, and neither may re-grow its own.
// (conventions.md §7: automate the check, don't re-document the trap.)

std::string read_source(const char* rel) {
    const std::string path = std::string(STS_SOURCE_ROOT) + "/" + rel;
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "cannot read " << path;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The two combat-construction paths, and the shape each must not re-grow.
constexpr const char* kConstructionSources[] = {
    "src/engine/advance.cpp",       // combat_begin
    "src/engine/run_advance.cpp",   // enter_combat
};

TEST(CombatStart, BothConstructionPathsDelegateToTheSharedTurnOneBlock) {
    for (const char* rel : kConstructionSources) {
        const std::string src = read_source(rel);
        EXPECT_NE(src.find("begin_first_turn("), std::string::npos)
            << rel << " no longer calls begin_first_turn.\n"
            << "  Both combat-construction paths must start a combat through the\n"
            << "  same function, or they will disagree about turn 1 -- which is\n"
            << "  how the end-of-round-on-turn-1 defect stayed live in one of\n"
            << "  them. See action_queue.hpp's begin_first_turn declaration.";
    }
}

// THE HOOK-ORDER PIN AT THE SHARED BLOCK ITSELF (G6 campaign 2 spot-diff §8.0).
// AbstractRoom's turn-1 block fires applyStartOfCombatLogic -- every relic's
// atBattleStart -- at :245, BEFORE applyStartOfTurnRelics at :253, and with the
// opening DrawCardAction already queued (:242). Because begin_first_turn is the
// one function both construction paths call (the test above), the
// AT_BATTLE_START dispatch must live inside its turn-1 block -- NOT be bolted
// on afterwards by one caller, which is exactly how the run layer inverted
// Stone Calendar's counter by one turn for a whole fight (STS00683, both
// campaigns) while combat_begin dispatched the hook not at all. This constructs
// the state directly (combat_begin's own entry never carries relics, so the
// shared block is the only place its half of the defect can be observed) and
// pins the visible consequence of the Java order: counter == 1 when control
// first reaches the player.
TEST(CombatStart, SharedTurnOneBlockRunsAtBattleStartBeforeAtTurnStart) {
    CombatState s = make_constructed_combat();
    // StoneCalendar at its out-of-combat counter (StoneCalendar.java:65-68
    // onVictory latches -1; a fresh pickup carries AbstractRelic's -1 too).
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::STONE_CALENDAR),
                            int16_t{-1}};
    s.relic_count = 1;

    begin_first_turn(s);

    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.hand_count, static_cast<uint8_t>(kStartOfTurnDrawCount));
    EXPECT_EQ(s.relics[0].counter, 1)
        << "atBattleStart's counter = 0 (AbstractRoom.java:245) must precede "
           "turn 1's atTurnStart ++ (:253): the wrong order reads 0, the Java "
           "reads 1 (the capture's value at STS00683 seq 141)";
}

// THE UNION PIN (final-integrate). Gambling Chip's atTurnStartPostDraw body is
// addToBot (GamblingChip.java:36-43), dispatched from the shared block's
// post-draw relic line (AbstractRoom.java:254) -- so its optional discard screen
// must open on the hand AS EXTENDED by every atBattleStart draw queued before
// it: Bag of Preparation's addToBot DrawCardAction(2) (BagOfPreparation.java:
// 30-35, fired at :245) sits between the opening draw and the chip's action.
// The Java's queue at combat start is [Draw(gameHandSize), ..., BagDraw(2),
// GamblingChipAction], so the screen shows 7 cards, not 5. An engine that
// dispatched AT_BATTLE_START after the turn-1 pump (the pre-merge run-layer
// shape) would block the chip's screen on a 5-card hand and only then draw
// Bag's 2 -- the observable difference between the two orders that the Stone
// Calendar counter test above cannot see.
TEST(CombatStart, GamblingChipScreenOpensOnTheBagOfPreparationExtendedHand) {
    CombatState s = make_constructed_combat();
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::BAG_OF_PREPARATION),
                            int16_t{-1}};
    s.relics[1] = RelicSlot{static_cast<uint16_t>(RelicId::GAMBLING_CHIP),
                            int16_t{-1}};  // -1 unset == not activated
    s.relic_count = 2;

    begin_first_turn(s);

    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.hand_count, static_cast<uint8_t>(kStartOfTurnDrawCount + 2))
        << "Bag of Preparation's 2 must be in hand BEFORE the chip's screen "
           "blocks the pump (queue order Draw(5), BagDraw(2), GamblingChip)";
    ASSERT_GE(s.action_count, uint8_t{1})
        << "the chip's optional screen must be open (blocked at queue front)";
    const ActionQueueItem front =
        s.action_queue[s.action_head % kActionQueueCap];
    EXPECT_EQ(front.opcode, static_cast<uint16_t>(Opcode::CHOOSE_CARD));
    EXPECT_EQ(choose_kind_from_flags(front.flags),
              ChoiceKind::HAND_TO_DISCARD_THEN_DRAW);
    EXPECT_NE(s.relics[1].counter, -1) << "activated = true, once per combat";
}

// THE SECOND UNION PIN (final-integrate fix-forward): Red Skull's entry
// decider composes with the reordered shared block. RedSkull$1 (the addToBot
// at RedSkull.java:38, recovered from desktop-1.0.jar --
// tools/oracle_bridge/driver/redskull_capture_runbook.md) re-tests
// player.isBloodied when it RESOLVES, at the bottom of the battle-start
// drain, AFTER Blood Vial's heal has settled (BloodVial.java:33 addToTop in
// the Java; synchronous-at-dispatch here -- earlier still). Entering at
// exactly half HP therefore grants nothing, spends no Artifact charge, and
// leaves the latch armed -- through the REAL turn-1 block, with the decider
// item riding behind the opening draw exactly where :38's addToBot puts it.
TEST(CombatStart, RedSkullEntryDeciderResolvesAfterHealsInTheSharedBlock) {
    CombatState s = make_constructed_combat();
    s.player_hp = 40;
    s.player_max_hp = 80;           // 80 <= 80 -> bloodied at the hook...
    give_player_power(s, PowerId::ARTIFACT, 1);
    s.relics[0] = RelicSlot{static_cast<uint16_t>(RelicId::RED_SKULL),
                            int16_t{0}};
    s.relics[1] = RelicSlot{static_cast<uint16_t>(RelicId::BLOOD_VIAL),
                            int16_t{0}};
    s.relic_count = 2;

    begin_first_turn(s);

    EXPECT_EQ(s.phase, static_cast<uint8_t>(CombatPhase::WAITING_ON_USER));
    EXPECT_EQ(s.player_hp, 42) << "Blood Vial settled during the drain";
    EXPECT_EQ(player_power(s, PowerId::STRENGTH), nullptr)
        << "42/80 > half when RedSkull$1 decides -> no grant, no -3/+3 pair";
    ASSERT_NE(player_power(s, PowerId::ARTIFACT), nullptr);
    EXPECT_EQ(player_power(s, PowerId::ARTIFACT)->amount, 1)
        << "no -3 was ever queued, so no Artifact charge is spent";
    EXPECT_EQ(s.relics[0].counter, 0) << "latch stays armed";
}

TEST(CombatStart, NeitherConstructionPathHandRollsTurnOnePriming) {
    // The exact assignments the old hand-rolled priming used. Their return is the
    // regression: setting turn_has_ended before a pump routes combat start
    // through getNextAction step 6 and its applyEndOfTurnPowers pass.
    constexpr const char* kForbidden[] = {
        "turn_has_ended = 1",
        "monster_attacks_queued = 1",
    };
    for (const char* rel : kConstructionSources) {
        const std::string src = read_source(rel);
        for (const char* shape : kForbidden) {
            EXPECT_EQ(src.find(shape), std::string::npos)
                << rel << " primes turn 1 by hand ('" << shape << "').\n"
                << "  Turn-1 priming belongs to begin_first_turn (action_queue.cpp)\n"
                << "  alone; a second copy is free to drift from the first.";
        }
    }
}

}  // namespace
}  // namespace sts::engine
