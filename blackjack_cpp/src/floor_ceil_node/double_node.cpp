#include "floor_ceil_node.h"

#include <stdexcept>

using namespace std;

namespace blackjack {

DoubleNode::DoubleNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    AbstractBJTreeNode* parent,
    int dealer_sim_depth,
    SimAlgo sim_algo
) :
    FloorCeilNode(
        bj_round,
        shoe,
        0, // max_hand_size_full_enum not used for DoubleNode
        dealer_sim_depth,
        sim_algo,
        parent,
        0  // n_splits_happened not tracked for DoubleNode
    )
{
}

void DoubleNode::createChild(
    const BJRound& child_bj_round,
    const ProbabilisticRankShoe& child_shoe,
    const TransitionEvent& event,
    double prob
) {
    // DoubleNode creates ValueNode children
    (void)child_bj_round;
    (void)child_shoe;
    (void)event;
    (void)prob;
    // Actual child creation happens in buildChildren
}

void DoubleNode::buildChildren() {
    auto rank_prob = shoe_.getRankValueProbabilities(nullopt);
    
    for (int card = 2; card <= 11; ++card) {
        double p = rank_prob.at(card);
        if (p <= 0.0) {
            continue;
        }
        
        const auto& active_hand = bj_round_.player_hands[static_cast<size_t>(bj_round_.active_hand_idx)];
        ValueOnlyHand player_hand = active_hand;
        player_hand.add_value(card);
        
        double value;
        if (player_hand.is_bust()) {
            value = -2.0 * static_cast<double>(bj_round_.bet_unit);
        } else {
            BJRound bj_round_copy = bj_round_.copy();
            ProbabilisticRankShoe shoe_copy(shoe_);
            bj_round_copy.takeCard(card);
            shoe_copy.burnRankValue(card);
            
            double sim_value = runDealerSim(bj_round_copy, shoe_copy);
            value = 2.0 * sim_value; // double bet
        }
        
        auto child = make_shared<ValueNode>(value, this);
        children_.push_back(child);
        children_prob_.push_back(p);
        children_events_.push_back(card);
    }
    
    has_built_children_ = true;

    // Compute value immediately for DoubleNode
    computeChanceNodeValue();
    has_completed_tree_ = true;
}

void DoubleNode::computeNodeValue() {
    // Value already computed in buildChildren
}

double DoubleNode::getCeilValue() const {
    return getValue();
}

double DoubleNode::getFloorValue() const {
    return getValue();
}

} // namespace blackjack
