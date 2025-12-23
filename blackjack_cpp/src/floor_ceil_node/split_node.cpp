#include "floor_ceil_node.h"

#include <stdexcept>
#include <utility>
#include <omp.h>
#include <iostream>

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
    AbstractFloorCeilNode(
        bj_round,
        shoe,
        max_hand_size_full_enum,
        dealer_sim_depth,
        sim_algo,
        parent
    ),
    first_hand_idx_(bj_round_.active_hand_idx)
{
    // Use fakeSplit to set up the split state
    // This gives first hand card 2 (placeholder) and stands, moves to second hand
    bj_round_.startFakeSplit();
    first_hand_idx_ = bj_round_.active_hand_idx - 1;  // First hand is now at previous index
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

        BJRound child_bj_round(bj_round_);
        // first hand is already set to <card>2 hand with zero value
        // here we take the second card for the second hand
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

pair<bool, bool> SplitNode::convertToFullUpToDepth(optional<int> depth) {
    if (!treeCompleted()) {
        throw runtime_error(
            "Cannot convert player card sample to full enum in an incomplete tree."
        );
    }

    if (is_full_tree_finished_) {
        return make_pair(false, true);
    }
    if (depth.has_value() && depth.value() <= full_tree_finished_up_to_depth_) {
        return make_pair(false, false);
    }

    // Pre-build vector of DecisionNodes to parallelize over
    vector<shared_ptr<DecisionNode>> decision_children;
    
    for (size_t i = 0; i < children_.size(); ++i) {
        shared_ptr<DecisionNode> d_child = dynamic_pointer_cast<DecisionNode>(children_[i]);
        if (d_child != nullptr) {
            decision_children.push_back(d_child);
        }
    }

    vector<bool> child_changed(decision_children.size(), false);
    vector<bool> child_is_final(decision_children.size(), false);

    optional<int> child_depth = depth.has_value() ? make_optional(depth.value() - 1) : nullopt;

    // #pragma omp parallel for schedule(dynamic) if(!omp_in_parallel()) 
    for (size_t i = 0; i < decision_children.size(); ++i) {
        auto [i_child_changed, i_child_is_final] = 
            decision_children[i]->convertToFullUpToDepth(child_depth);
        child_changed[i] = i_child_changed;
        child_is_final[i] = i_child_is_final;
    }

    bool children_changed = false;
    for (bool changed : child_changed) {
        if (changed) {
            children_changed = true;
            break;
        }
    }
    
    bool is_final = true;
    for (bool final : child_is_final) {
        if (!final) {
            is_final = false;
            break;
        }
    }

    bool value_changed = false;
    if (children_changed) {
        recomputeTreeValue();
        value_changed = true;
    }

    if (is_final) {
        is_full_tree_finished_ = true;
    }
    if (depth.has_value() && depth.value() > full_tree_finished_up_to_depth_) {
        full_tree_finished_up_to_depth_ = depth.value();
    }

    return make_pair(value_changed, is_final);
}



} // namespace blackjack
