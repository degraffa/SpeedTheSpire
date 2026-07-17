#include "sts/diff/replay.hpp"

#include "sts/engine/advance.hpp"

namespace sts::diff {

using engine::Action;
using engine::CardId;
using engine::CombatState;

std::vector<CardId> skeleton_deck() {
    std::vector<CardId> deck;
    deck.reserve(12);
    for (int i = 0; i < 5; ++i) deck.push_back(CardId::STRIKE);
    for (int i = 0; i < 4; ++i) deck.push_back(CardId::DEFEND);
    deck.push_back(CardId::BASH);
    deck.push_back(CardId::SHRUG_IT_OFF);
    deck.push_back(CardId::POMMEL_STRIKE);
    return deck;
}

void replay(int64_t seed, int32_t floor, std::span<const CardId> deck,
            std::span<const Action> actions,
            std::vector<CombatState>& states_out) {
    states_out.clear();
    states_out.reserve(actions.size() + 1);

    CombatState s = engine::combat_begin(seed, floor, deck);
    states_out.push_back(s);  // out[0] = initial state, before any action

    for (const Action a : actions) {
        engine::StepResult res{};
        std::span<CombatState> ss(&s, 1);
        std::span<const Action> aa(&a, 1);
        std::span<engine::StepResult> rr(&res, 1);
        engine::advance(ss, aa, rr);
        states_out.push_back(s);  // out[k] = state after actions[k-1]
    }
}

void replay_skeleton(int64_t seed, std::span<const Action> actions,
                     std::vector<CombatState>& states_out) {
    const std::vector<CardId> deck = skeleton_deck();
    replay(seed, kSkeletonFloor, std::span<const CardId>(deck), actions, states_out);
}

}  // namespace sts::diff
