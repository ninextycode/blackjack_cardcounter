#include "floor_ceil_node.h"

#include <stdexcept>
#include <utility>
#include "dealer_sim.h"
#include <iostream>

using namespace std;

namespace blackjack {

AbstractFloorCeilNode::AbstractFloorCeilNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int max_hand_size_full_enum,
    int dealer_sim_depth,
    SimAlgo sim_algo,
    AbstractBJTreeNode* parent
) :
    AbstractBJTreeNode(bj_round, shoe, parent),
    max_hand_size_full_enum_(max_hand_size_full_enum),
    dealer_sim_depth_(dealer_sim_depth),
    sim_algo_(sim_algo),
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

void AbstractFloorCeilNode::createChild(
    const BJRound& child_bj_round,
    const ProbabilisticRankShoe& child_shoe,
    const TransitionEvent& event,
    double prob
) {
    auto child = make_shared<AbstractFloorCeilNode>(
        child_bj_round,
        child_shoe,
        max_hand_size_full_enum_,
        dealer_sim_depth_,
        sim_algo_,
        this
    );
    children_.push_back(child);
    children_prob_.push_back(prob);
    children_events_.push_back(event);
}

double AbstractFloorCeilNode::runDealerSim(const BJRound& bj_round, const ProbabilisticRankShoe& shoe) {
    bool simulation_for_last_hand = true;  // way to handle splits

    if (sim_algo_ == SimAlgo::COMBO) {
        bool verbose = false;
        return runDealerCardsSimulationCombo(
            bj_round, shoe, 1,
            dealer_sim_depth_,
            verbose,
            simulation_for_last_hand
        );
    } else if (sim_algo_ == SimAlgo::RECURSIVE) {
        return runDealerCardsSimulationRecursive(
            bj_round, shoe, 1,
            dealer_sim_depth_,
            simulation_for_last_hand
        );
    } else {
        throw runtime_error("Unknown sim_algo. Use COMBO or RECURSIVE.");
    }
}

void AbstractFloorCeilNode::rebuildChildren() {
    AbstractBJTreeNode::rebuildChildren();
    ceil_value_ = 0.0;
    floor_value_ = 0.0;
}

double AbstractFloorCeilNode::getCeilValue() const {
    if (!has_completed_tree_) {
        throw runtime_error("Ceil value not computed.");
    }
    return ceil_value_;
}

double AbstractFloorCeilNode::getFloorValue() const {
    if (!has_completed_tree_) {
        throw runtime_error("Floor value not computed.");
    }
    return floor_value_;
}

double AbstractFloorCeilNode::getCurrentValueGap() const {
    return getCeilValue() - getFloorValue();
}

void AbstractFloorCeilNode::computeFloorValue() {
    throw runtime_error("computeFloorValue must be implemented by subclass");
}

void AbstractFloorCeilNode::computeCeilValue() {
    throw runtime_error("computeCeilValue must be implemented by subclass");
}

void AbstractFloorCeilNode::computeChanceNodeCeilValue() {
    if (!has_built_children_) {
        throw runtime_error("Cannot get ceil value before building children.");
    }
    double ceil_val = 0.0;
    for (size_t i = 0; i < children_.size(); ++i) {
        double child_ceil = children_[i]->getCeilValue();
        ceil_val += children_prob_[i] * child_ceil;
    }
    ceil_value_ = ceil_val;
}

void AbstractFloorCeilNode::computeChanceNodeFloorValue() {
    if (!has_built_children_) {
        throw runtime_error("Cannot get floor value before building children.");
    }
    double floor_val = 0.0;
    for (size_t i = 0; i < children_.size(); ++i) {
        double child_floor = children_[i]->getFloorValue();
        floor_val += children_prob_[i] * child_floor;
    }
    floor_value_ = floor_val;
}

void AbstractFloorCeilNode::computeNodeValue() {
    AbstractBJTreeNode::computeNodeValue();
    computeCeilValue();
    computeFloorValue();
}

void AbstractFloorCeilNode::buildChildrenDealerCard() {
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

void AbstractFloorCeilNode::buildChildDealerBlackjack() {
    auto possible_ranks_opt = bj_round_.getPossibleNextCardRanks();
    if (!possible_ranks_opt.has_value() || possible_ranks_opt.value().empty()) {
        throw runtime_error("No possible ranks for dealer blackjack");
    }
    
    int rank_value = possible_ranks_opt.value()[0];
    ProbabilisticRankShoe shoe_copy(shoe_);
    shoe_copy.burnRankValue(rank_value);
    
    BJRound bj_round_copy(bj_round_);
    bj_round_copy.takeCard(rank_value);
    
    createChild(bj_round_copy, shoe_copy, rank_value, 1.0);
}

void AbstractFloorCeilNode::buildChildrenPlayerCard() {
    throw runtime_error("buildChildrenPlayerCard must be implemented by subclass");
}

pair<bool, bool> AbstractFloorCeilNode::convertToFullUpToDepth(optional<int> depth) {
    if (!treeCompleted()) {
        throw runtime_error(
            "Cannot convert player card sample to full enum in an incomplete tree."
        );
    }

    // strict inequality is intentional - 
    // the depth 0 is valid - convert only self to full
    if (depth.has_value() && depth.value() < 0) {
        return make_pair(false, false);
    }
    
    bool children_changed = false;
    bool is_final = true;

    BJStage stage = bj_round_.getStage();
    
    if (stage == BJStage::DEALER_CARD || stage == BJStage::ROUND_OVER) {
        return make_pair(false, true);
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
        
        AbstractFloorCeilNode* fc_child = dynamic_cast<AbstractFloorCeilNode*>(ch.get());
        if (fc_child == nullptr) {
            continue;  // TODO should it throw an error?
        }
        
        // If there is a new child that doesn't have a tree - build the tree
        if (!fc_child->treeCompleted()) {
            fc_child->buildTree();
            children_changed = true;
        }

        optional<int> child_depth = depth.has_value() \
            ? make_optional(depth.value() - 1) \
            : nullopt;

        auto [child_changed, child_is_final] = fc_child->convertToFullUpToDepth(child_depth);
        if (child_changed) {
            children_changed = true;
        }
        if (!child_is_final) {
            is_final = false;
        }
    }

    bool value_changed = false;
    if (children_changed) {
        recomputeTreeValue();
        value_changed = true;
    }
    
    return make_pair(value_changed, is_final);
}

pair<bool, bool> AbstractFloorCeilNode::convertToFullUpToGap(double value_gap) {
    int depth = 0;
    bool any_value_changed = false;
    // Otherwise, will be set in the loop
    
    double current_gap = getCeilValue() - getFloorValue(); 
    bool is_final = (current_gap == 0.0);
    
    while (current_gap > value_gap) {
        auto [this_value_changed, this_is_final] = convertToFullUpToDepth(depth);
        current_gap = getCeilValue() - getFloorValue(); 
        if (this_value_changed) {
            any_value_changed = true;
        }
        is_final = this_is_final;
        if (is_final) {
            break;
        }
        depth += 1;
    }
    
    return make_pair(any_value_changed, is_final);
}

bool AbstractFloorCeilNode::convertFromSampleToFull() {
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

// Factory function
shared_ptr<AbstractFloorCeilNode> buildNonFinalRootNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int max_hand_size_full_enum,
    int dealer_sim_depth,
    SimAlgo sim_algo
) {
    BJStage stage = bj_round.getStage();
    if (stage == BJStage::DEALER_CARD || stage == BJStage::ROUND_OVER) {
        throw runtime_error("buildNonFinalRootNode: round is in its final stage");
    }

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
