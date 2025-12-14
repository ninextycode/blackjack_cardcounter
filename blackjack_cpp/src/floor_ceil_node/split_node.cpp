#include "floor_ceil_node.h"

#include <stdexcept>

using namespace std;

namespace blackjack {

SplitNode::SplitNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int max_hand_size_full_enum,
    int dealer_sim_depth,
    SimAlgo sim_algo,
    AbstractBJTreeNode* parent
) :
    FloorCeilNode(
        bj_round,
        shoe,
        max_hand_size_full_enum,
        dealer_sim_depth,
        sim_algo,
        parent
    ),
    first_hand_idx_(bj_round_.active_hand_idx)
{
    // The first hand of the split is set to <card>2 hand with zero value
    // to simplify the tree only the second one is considered, it's value is doubled
    // use 2 because it will never give 21 and "stand" will always be a legal action
    bj_round_.takeCard(2);  // placeholder card, not accounted in the shoe
    first_hand_idx_ = bj_round_.active_hand_idx;
    bj_round_.hand_bets[first_hand_idx_] = 0;
}

void SplitNode::createChild(
    const BJRound& child_bj_round,
    const ProbabilisticRankShoe& child_shoe,
    const TransitionEvent& event,
    double prob
) {
    BJStage stage = child_bj_round.getStage();
    
    shared_ptr<AbstractBJTreeNode> child;
    
    if (stage == BJStage::PLAYER_ACTION) {
        // Split has value less than 21
        child = make_shared<DecisionNode>(
            child_bj_round, child_shoe,
            max_hand_size_full_enum_,
            dealer_sim_depth_,
            sim_algo_,
            this
        );
    } else if (stage == BJStage::DEALER_CARD) {
        // Split has value of 21
        double value = runDealerSim(child_bj_round, child_shoe);
        child = make_shared<ValueNode>(value, this);
    } else {
        throw runtime_error("Unexpected stage in SplitNode child creation.");
    }
    
    children_.push_back(child);
    children_prob_.push_back(prob);
    children_events_.push_back(event);
}

bool SplitNode::convertFromSampleToFull() {
    return false; // Full sample on first build_children already
}

void SplitNode::buildChildren() {
    auto card_probabilities = shoe_.getRankValueProbabilities(nullopt);
    
    for (int card = 2; card <= 11; ++card) {
        double prob = card_probabilities.at(card);
        if (prob <= 0.0) {
            continue;
        }
        
        ProbabilisticRankShoe child_shoe(shoe_);
        child_shoe.burnRankValue(card);

        BJRound child_bj_round = bj_round_.copy();
        child_bj_round.takeCard(card);
        
        // Stand on the first hand - it already got a placeholder card and its value is set to zero
        child_bj_round.takeAction(PlayerAction::STAND);

        createChild(child_bj_round, child_shoe, card, prob);
    }
    has_built_children_ = true;
}

void SplitNode::computeNodeValue() {
    // Complete expected value, double it as this node corresponds to a pair
    computeChanceNodeValue();
    value_ = 2.0 * value_;
    computeCeilValue();
    computeFloorValue();
}

void SplitNode::computeCeilValue() {
    computeChanceNodeCeilValue();
    ceil_value_ = 2.0 * ceil_value_;
}

void SplitNode::computeFloorValue() {
    computeChanceNodeFloorValue();
    floor_value_ = 2.0 * floor_value_;
}

} // namespace blackjack