#include "floor_ceil_node.h"

#include <algorithm>
#include <stdexcept>
#include <numeric>

using namespace std;

namespace blackjack {

FloorCeilNode::FloorCeilNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int max_hand_size_full_enum,
    int dealer_sim_depth,
    SimAlgo sim_algo,
    AbstractBJTreeNode* parent,
    int n_splits_happened
) :
    AbstractBJTreeNode(bj_round, shoe, parent),
    max_hand_size_full_enum_(max_hand_size_full_enum),
    dealer_sim_depth_(dealer_sim_depth),
    sim_algo_(sim_algo),
    n_splits_happened_(n_splits_happened),
    active_hand_size_(nullopt),
    ceil_value_(0.0),
    floor_value_(0.0)
{
    // each brach is independent
    this->shoe_.resetSampler();

    const auto& hands = bj_round_.player_hands;
    int hand_idx = bj_round_.active_hand_idx;
    if (hand_idx >= 0 && hand_idx < static_cast<int>(hands.size())) {
        active_hand_size_ = hands[static_cast<size_t>(hand_idx)].size();
    }
}

void FloorCeilNode::createChild(
    const BJRound& child_bj_round,
    const ProbabilisticRankShoe& child_shoe,
    const TransitionEvent& event,
    double prob
) {
    auto child = make_shared<FloorCeilNode>(
        child_bj_round,
        child_shoe,
        max_hand_size_full_enum_,
        dealer_sim_depth_,
        sim_algo_,
        this,
        n_splits_happened_
    );
    children_.push_back(child);
    children_prob_.push_back(prob);
    children_events_.push_back(event);
}

double FloorCeilNode::runDealerSim(const BJRound& bj_round, const ProbabilisticRankShoe& shoe) {
    if (sim_algo_ == SimAlgo::COMBO) {
        return runDealerCardsSimulationCombo(
            bj_round, shoe, 0,
            dealer_sim_depth_
        );
    } else if (sim_algo_ == SimAlgo::RECURSIVE) {
        return runDealerCardsSimulationRecursive(
            bj_round, shoe, 1,
            dealer_sim_depth_
        );
    } else {
        throw runtime_error("Unknown sim_algo. Use COMBO or RECURSIVE.");
    }
}

void FloorCeilNode::rebuildChildren() {
    AbstractBJTreeNode::rebuildChildren();
    ceil_value_ = 0.0;
    floor_value_ = 0.0;
}

double FloorCeilNode::getCeilValue() const {
    if (!has_completed_tree_) {
        throw runtime_error("Ceil value not computed.");
    }
    return ceil_value_;
}

double FloorCeilNode::getFloorValue() const {
    if (!has_completed_tree_) {
        throw runtime_error("Floor value not computed.");
    }
    return floor_value_;
}

void FloorCeilNode::computeFloorValue() {
    throw runtime_error("computeFloorValue must be implemented by subclass");
}

void FloorCeilNode::computeCeilValue() {
    throw runtime_error("computeCeilValue must be implemented by subclass");
}

void FloorCeilNode::computeChanceNodeCeilValue() {
    if (!has_built_children_) {
        throw runtime_error("Cannot get ceil value before building children.");
    }
    double ceil_val = 0.0;
    for (size_t i = 0; i < children_.size(); ++i) {
        // Try to get ceil value from child
        FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(children_[i].get());
        ValueNode* v_child = dynamic_cast<ValueNode*>(children_[i].get());
        FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(children_[i].get());
        
        double child_ceil;
        if (fc_child != nullptr) {
            child_ceil = fc_child->getCeilValue();
        } else if (v_child != nullptr) {
            child_ceil = v_child->getCeilValue();
        } else if (fcv_child != nullptr) {
            child_ceil = fcv_child->getCeilValue();
        } else {
            child_ceil = children_[i]->getValue();
        }
        ceil_val += children_prob_[i] * child_ceil;
    }
    ceil_value_ = ceil_val;
}

void FloorCeilNode::computeChanceNodeFloorValue() {
    if (!has_built_children_) {
        throw runtime_error("Cannot get floor value before building children.");
    }
    double floor_val = 0.0;
    for (size_t i = 0; i < children_.size(); ++i) {
        // Try to get floor value from child
        FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(children_[i].get());
        ValueNode* v_child = dynamic_cast<ValueNode*>(children_[i].get());
        FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(children_[i].get());
        
        double child_floor;
        if (fc_child != nullptr) {
            child_floor = fc_child->getFloorValue();
        } else if (v_child != nullptr) {
            child_floor = v_child->getFloorValue();
        } else if (fcv_child != nullptr) {
            child_floor = fcv_child->getFloorValue();
        } else {
            child_floor = children_[i]->getValue();
        }
        floor_val += children_prob_[i] * child_floor;
    }
    floor_value_ = floor_val;
}

void FloorCeilNode::computeNodeValue() {
    AbstractBJTreeNode::computeNodeValue();
    computeCeilValue();
    computeFloorValue();
}

void FloorCeilNode::buildChildrenDealerCard() {
    if (bj_round_.dealerExpectsToShowBlackjack()) {
        buildChildDealerBlackjack();
    } else {
        // Run dealer simulation and create a value node
        double value = runDealerSim(bj_round_, shoe_);
        auto sim_node = make_shared<ValueNode>(value, this);
        children_.push_back(sim_node);
        children_prob_.push_back(1.0);
        children_events_.push_back(0); // placeholder event
    }
    has_built_children_ = true;
}

void FloorCeilNode::buildChildDealerBlackjack() {
    auto possible_ranks_opt = bj_round_.getPossibleNextCardRanks();
    if (!possible_ranks_opt.has_value() || possible_ranks_opt.value().empty()) {
        throw runtime_error("No possible ranks for dealer blackjack");
    }
    
    int rank_value = possible_ranks_opt.value()[0];
    ProbabilisticRankShoe shoe_copy(shoe_);
    shoe_copy.burnRankValue(rank_value);
    
    auto bj_round_copy = bj_round_.copy();
    bj_round_copy.takeCard(rank_value);
    
    createChild(bj_round_copy, shoe_copy, rank_value, 1.0);
}

void FloorCeilNode::buildChildrenPlayerCard() {
    throw runtime_error("buildChildrenPlayerCard must be implemented by subclass");
}

bool FloorCeilNode::convertToFullUpToDepth(int depth) {
    if (!treeCompleted()) {
        throw runtime_error(
            "Cannot convert player card sample to full enum in an incomplete tree."
        );
    }

    if (depth < 0) {
        return false;
    }
    
    bool children_changed = false;

    BJStage stage = bj_round_.getStage();
    
    if (stage == BJStage::DEALER_CARD || stage == BJStage::ROUND_OVER) {
        return false;
    }

    // Convert itself
    if (stage == BJStage::PLAYER_CARD) {
        bool changed_children_self = convertFromSampleToFull();
        if (changed_children_self) {
            children_changed = true;
        }
    }

    // Convert children
    for (auto& ch : children_) {
        ValueNode* v_child = dynamic_cast<ValueNode*>(ch.get());
        DoubleNode* d_child = dynamic_cast<DoubleNode*>(ch.get());
        
        if (v_child != nullptr || d_child != nullptr) {
            continue;
        }
        
        FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(ch.get());
        if (fc_child == nullptr) {
            continue;
        }
        
        // If there is a new child that doesn't have a tree - build the tree
        if (!fc_child->treeCompleted()) {
            fc_child->buildTree();
            children_changed = true;
        }

        bool child_changed = fc_child->convertToFullUpToDepth(depth - 1);
        if (child_changed) {
            children_changed = true;
        }
    }

    if (children_changed) {
        recomputeTreeValue();
        return true;
    }
    return false;
}

bool FloorCeilNode::convertFromSampleToFull() {
    throw runtime_error("convertFromSampleToFull must be implemented by subclass");
}

// Factory function
shared_ptr<AbstractBJTreeNode> buildRootNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int max_hand_size_full_enum,
    int dealer_sim_depth,
    SimAlgo sim_algo
) {
    BJStage stage = bj_round.getStage();

    if (stage == BJStage::DEALER_CHECK_BJ) {
        // dealer checks blackjack with ten
        // insurance not offered
        return make_shared<DealerCheckBJNode>(
            bj_round,
            shoe,
            max_hand_size_full_enum,
            false, // took_insurance
            false, // insurance_offered
            dealer_sim_depth,
            sim_algo
        );
    } else if (stage == BJStage::DEALER_CARD &&
               bj_round.player_hands.size() == 1 &&
               bj_round.player_hands[0].is_natural_blackjack()) {
        // player has blackjack, insurance not offered - go to dealer card immediately
        return make_shared<ValueNode>(
            static_cast<double>(bj_round.bet_unit) * bj_round.rules_->natural_blackjack_payout
        );
    } else {
        // insurance or a normal game node
        return make_shared<DecisionNode>(
            bj_round,
            shoe,
            max_hand_size_full_enum,
            dealer_sim_depth,
            sim_algo
        );
    }
}

} // namespace blackjack
