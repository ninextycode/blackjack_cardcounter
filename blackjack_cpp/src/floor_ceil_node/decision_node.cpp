#include "blackjack_round.h"
#include "floor_ceil_node.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace std;

namespace blackjack {

DecisionNode::DecisionNode(
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
    meaningful_actions_()
{
    // Initialize meaningful_actions from available actions
    // Split exclusion is handled by the game round itself
    meaningful_actions_ = bj_round_.getAvailableActions();
}

void DecisionNode::createChild(
    const BJRound& child_bj_round,
    const ProbabilisticRankShoe& child_shoe,
    const TransitionEvent& event,
    double prob
) {
    // Default implementation - creates DecisionNode
    // Specific child types are created in buildChildrenPlayerAction
    auto child = make_shared<DecisionNode>(
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

void DecisionNode::rebuildChildren() {
    AbstractFloorCeilNode::rebuildChildren();
    meaningful_actions_ = bj_round_.getAvailableActions();
}

void DecisionNode::computeFloorValue() {
    if (meaningful_actions_.size() > 1) {
        // Get the highest floor value among all possible actions
        double max_floor = -1e18;
        for (const auto& action : meaningful_actions_) {
            // Find child index for this action
            size_t child_idx = 0;
            for (size_t i = 0; i < children_events_.size(); ++i) {
                if (holds_alternative<PlayerAction>(children_events_[i]) &&
                    get<PlayerAction>(children_events_[i]) == action) {
                    child_idx = i;
                    break;
                }
            }
            
            double child_floor;
            child_floor = children_[child_idx]->getFloorValue();
            
            if (child_floor > max_floor) {
                max_floor = child_floor;
            }
        }
        floor_value_ = max_floor;
    } else {
        auto* child = getDecisionChoiceChild();
        floor_value_ = child->getFloorValue();
    }
}

void DecisionNode::computeCeilValue() {
    if (meaningful_actions_.size() > 1) {
        // Get the highest ceil value among all possible actions
        double max_ceil = -1e18;
        for (const auto& action : meaningful_actions_) {
            // Find child index for this action
            size_t child_idx = 0;
            for (size_t i = 0; i < children_events_.size(); ++i) {
                if (holds_alternative<PlayerAction>(children_events_[i]) &&
                    get<PlayerAction>(children_events_[i]) == action) {
                    child_idx = i;
                    break;
                }
            }
            
            double child_ceil;
            child_ceil = children_[child_idx]->getCeilValue();
            
            if (child_ceil > max_ceil) {
                max_ceil = child_ceil;
            }
        }
        ceil_value_ = max_ceil;
    } else {
        auto* child = getDecisionChoiceChild();
        ceil_value_ = child->getCeilValue();
    }
}

optional<PlayerAction> DecisionNode::getDecisionChoice() const {
    if (meaningful_actions_.size() != 1) {
        return nullopt;
    }
    return meaningful_actions_[0];
}

AbstractBJTreeNode* DecisionNode::getDecisionChoiceChild() {
    if (meaningful_actions_.size() != 1) {
        throw runtime_error("Decision choice is not yet made (multiple actions still possible).");
    }
    PlayerAction action = meaningful_actions_[0];
    
    for (size_t i = 0; i < children_events_.size(); ++i) {
        if (holds_alternative<PlayerAction>(children_events_[i]) &&
            get<PlayerAction>(children_events_[i]) == action) {
            return children_[i].get();
        }
    }
    throw runtime_error("Decision choice child not found.");
}

bool DecisionNode::hasDecided() const {
    return meaningful_actions_.size() == 1;
}

vector<pair<PlayerAction, AbstractBJTreeNode*>> DecisionNode::getPossibleActionChildren() {
    vector<pair<PlayerAction, AbstractBJTreeNode*>> result;
    for (const auto& action : meaningful_actions_) {
        for (size_t i = 0; i < children_events_.size(); ++i) {
            if (holds_alternative<PlayerAction>(children_events_[i]) &&
                get<PlayerAction>(children_events_[i]) == action) {
                result.push_back({action, children_[i].get()});
                break;
            }
        }
    }
    return result;
}

void DecisionNode::computeActionNodeValue() {
    if (meaningful_actions_.empty()) {
        throw runtime_error("No possible actions remaining to evaluate.");
    }

    // Identify the best-valued action among those still possible
    vector<double> values;
    vector<size_t> child_indices;
    
    for (const auto& action : meaningful_actions_) {
        for (size_t i = 0; i < children_events_.size(); ++i) {
            if (holds_alternative<PlayerAction>(children_events_[i]) &&
                get<PlayerAction>(children_events_[i]) == action) {
                child_indices.push_back(i);
                values.push_back(children_[i]->getValue());
                break;
            }
        }
    }

    auto max_it = max_element(values.begin(), values.end());
    size_t best_idx_in_possible = static_cast<size_t>(distance(values.begin(), max_it));
    size_t best_child_idx = child_indices[best_idx_in_possible];

    // Clear probabilities for all actions, then select the best possible action
    for (size_t i = 0; i < children_prob_.size(); ++i) {
        children_prob_[i] = 0.0;
    }
    children_prob_[best_child_idx] = 1.0;
    value_ = values[best_idx_in_possible];
}

void DecisionNode::computeNodeValue() {
    computeActionNodeValue();
    updatePossibleActions();
    computeFloorValue();
    computeCeilValue();
}

void DecisionNode::updatePossibleActions() {
    // Get floor and ceil values for each possible action
    vector<double> floor_values;
    vector<double> ceil_values;
    
    for (const auto& action : meaningful_actions_) {
        for (size_t i = 0; i < children_events_.size(); ++i) {
            if (holds_alternative<PlayerAction>(children_events_[i]) &&
                get<PlayerAction>(children_events_[i]) == action) {
                
                // Use interface to get floor/ceil values
                double child_floor = children_[i]->getFloorValue();
                double child_ceil = children_[i]->getCeilValue();
                
                floor_values.push_back(child_floor);
                ceil_values.push_back(child_ceil);
                break;
            }
        }
    }
    
    // Find the maximum floor value among all possible actions
    double max_floor = *max_element(floor_values.begin(), floor_values.end());
    
    // Exclude actions whose ceiling is below the max floor
    vector<PlayerAction> new_meaningful_actions;
    for (size_t i = 0; i < meaningful_actions_.size(); ++i) {
        if (ceil_values[i] >= max_floor) {
            new_meaningful_actions.push_back(meaningful_actions_[i]);
        }
    }
    
    meaningful_actions_ = new_meaningful_actions;
}

void DecisionNode::buildChildren() {
    BJStage stage = bj_round_.getStage();
    
    if (stage != BJStage::PLAYER_ACTION &&
        stage != BJStage::PLAYER_OFFERED_EARLY_SURRENDER &&
        stage != BJStage::PLAYER_OFFERED_INSURANCE) {
        throw runtime_error(
            "DecisionNode can only build children for player decision stage."
        );
    }

    if (stage == BJStage::PLAYER_OFFERED_INSURANCE) {
        buildChildrenInsurance();
    } else {
        buildChildrenPlayerAction();
    }
    has_built_children_ = true;
}

void DecisionNode::buildChildrenPlayerAction() {
    for (const auto& a : meaningful_actions_) {
        BJRound bj_round_child(bj_round_);
        ProbabilisticRankShoe shoe_copy(shoe_);

        if (a == PlayerAction::SPLIT) {
            // The first hand of the split is set to <card>2 hand with zero value
            // to simplify the tree only the second one is considered, it's value is doubled
            // this is handled inside SplitNode
            auto child = make_shared<SplitNode>(
                bj_round_child, shoe_copy,
                max_hand_size_full_enum_,
                dealer_sim_depth_,
                sim_algo_,
                this
            );
            addChild(child, PlayerAction::SPLIT, 0.0);
        }
        else if (a == PlayerAction::HIT) {
            bj_round_child.takeAction(PlayerAction::HIT);
            auto child = make_shared<HitNode>(
                bj_round_child, shoe_copy,
                max_hand_size_full_enum_,
                dealer_sim_depth_,
                sim_algo_,
                this
            );
            addChild(child, PlayerAction::HIT, 0.0);
        }
        else if (a == PlayerAction::STAND) {
            bj_round_child.takeAction(PlayerAction::STAND);
            double value = runDealerSim(bj_round_child, shoe_copy);
            auto child = make_shared<ValueNode>(value, this);
            addChild(child, PlayerAction::STAND, 0.0);
        }
        else if (a == PlayerAction::DOUBLE) {
            bj_round_child.takeAction(PlayerAction::DOUBLE);
            auto child = make_shared<DoubleNode>(
                bj_round_child, shoe_copy,
                this,
                dealer_sim_depth_,
                sim_algo_
            );
            addChild(child, PlayerAction::DOUBLE, 0.0);
        }
        else if (a == PlayerAction::DECLINE_EARLY_SURRENDER) {
            bj_round_child.takeAction(PlayerAction::DECLINE_EARLY_SURRENDER);
            auto child = make_shared<DecisionNode>(
                bj_round_child, shoe_copy,
                max_hand_size_full_enum_,
                dealer_sim_depth_,
                sim_algo_,
                this
            );
            addChild(child, PlayerAction::DECLINE_EARLY_SURRENDER, 0.0);
        }
        else if (a == PlayerAction::SURRENDER) {
            double value = -static_cast<double>(bj_round_child.bet_unit) + 
                           static_cast<double>(bj_round_child.bet_unit) * bj_round_child.rules_->surrender_payout;
            auto child = make_shared<ValueNode>(value, this);
            addChild(child, PlayerAction::SURRENDER, 0.0);
        }
        else {
            throw runtime_error("Unexpected player action");
        }
    }
}

void DecisionNode::buildChildrenInsurance() {
    BJRound bj_round_child(bj_round_);
    bj_round_child.takeAction(PlayerAction::REFUSE_INSURANCE);
    
    auto accept_child = make_shared<DealerCheckBJNode>(
        bj_round_child, shoe_,
        max_hand_size_full_enum_,
        true,  // took_insurance
        true,  // insurance_offered
        dealer_sim_depth_,
        sim_algo_,
        this
    );
    
    auto decline_child = make_shared<DealerCheckBJNode>(
        bj_round_child, shoe_,
        max_hand_size_full_enum_,
        false, // took_insurance
        true,  // insurance_offered
        dealer_sim_depth_,
        sim_algo_,
        this
    );

    accept_child->buildChildren();
    decline_child->buildChildren();

    // Share the no-BJ subtree between both children
    accept_child->children_[accept_child->dealer_no_bj_child_idx_] = 
        decline_child->children_[decline_child->dealer_no_bj_child_idx_];
    
    addChild(accept_child, PlayerAction::TAKE_INSURANCE, 0.0);
    addChild(decline_child, PlayerAction::REFUSE_INSURANCE, 0.0);
}

pair<bool, bool> DecisionNode::convertToFullUpToDepth(optional<int> depth) {
    if (!treeCompleted()) {
        throw runtime_error(
            "Cannot convert DecisionNode to full enum in an incomplete tree."
        );
    }

    if (is_full_tree_finished_) {
        return make_pair(false, true);
    }
    if (depth.has_value() && depth.value() <= full_tree_finished_up_to_depth_) {
        return make_pair(false, false);
    }
    
    bool children_changed = false;
    bool is_final = true;

    if (bj_round_.getStage() == BJStage::PLAYER_OFFERED_INSURANCE) {
        auto [changed, final] = convertBjCheckChildrenToFullUpToDepth(depth);
        children_changed = changed;
        is_final = final;
    } else {
        auto [changed, final] = convertPossibleChildrenToFullUpToDepth(depth);
        children_changed = changed;
        is_final = final;
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

pair<bool, bool> DecisionNode::convertBjCheckChildrenToFullUpToDepth(optional<int> depth) {
    // Special case - update the downstream round tree where dealer does not have bj
    // both insurance children share the same no-BJ subtree
    DealerCheckBJNode* child = dynamic_cast<DealerCheckBJNode*>(children_[0].get());
    if (child == nullptr) {
        return make_pair(false, true);
    }
    
    auto& dealer_no_bj_round_tree = child->children_[child->dealer_no_bj_child_idx_];
    
    ValueNode* v_node = dynamic_cast<ValueNode*>(dealer_no_bj_round_tree.get());
    if (v_node != nullptr) {
        // Case where player has bj but dealer does not - no subtree to expand
        return make_pair(false, true);
    }
    
    AbstractFloorCeilNode* fc_node = dynamic_cast<AbstractFloorCeilNode*>(dealer_no_bj_round_tree.get());
    if (fc_node == nullptr) {
        return make_pair(false, true);
    }
    
    optional<int> child_depth = depth.has_value() ? make_optional(depth.value() - 2) : nullopt;
    auto [round_child_changed, child_is_final] = fc_node->convertToFullUpToDepth(child_depth);
    if (round_child_changed) {
        if (!hasDecided()) {
            for (auto& ch : children_) {
                ch->recomputeTreeValue();
            }
        } else {
            getDecisionChoiceChild()->recomputeTreeValue();
        }
        return make_pair(true, child_is_final);
    }
    return make_pair(false, child_is_final);
}

pair<bool, bool> DecisionNode::convertPossibleChildrenToFullUpToDepth(optional<int> depth) {
    bool children_changed = false;
    bool is_final = true;
    
    for (const auto& action : meaningful_actions_) {
        for (size_t i = 0; i < children_events_.size(); ++i) {
            if (holds_alternative<PlayerAction>(children_events_[i]) &&
                get<PlayerAction>(children_events_[i]) == action) {
                
                ValueNode* v_child = dynamic_cast<ValueNode*>(children_[i].get());
                DoubleNode* d_child = dynamic_cast<DoubleNode*>(children_[i].get());
                
                if (v_child != nullptr || d_child != nullptr) {
                    break;
                }
                
                AbstractFloorCeilNode* fc_child = dynamic_cast<AbstractFloorCeilNode*>(children_[i].get());
                if (fc_child != nullptr) {
                    optional<int> child_depth = depth.has_value() ? make_optional(depth.value() - 1) : nullopt;
                    auto [child_changed, child_is_final] = fc_child->convertToFullUpToDepth(child_depth);
                    if (child_changed) {
                        children_changed = true;
                    }
                    if (!child_is_final) {
                        is_final = false;
                    }
                }
                break;
            }
        }
    }
    return make_pair(children_changed, is_final);
}

pair<bool, bool> DecisionNode::convertDecisionChildToFullUpToDepth(optional<int> depth) {
    auto* child = getDecisionChoiceChild();
    
    ValueNode* v_child = dynamic_cast<ValueNode*>(child);
    DoubleNode* d_child = dynamic_cast<DoubleNode*>(child);
    
    if (v_child != nullptr || d_child != nullptr) {
        return make_pair(false, true);
    }
    
    AbstractFloorCeilNode* fc_child = dynamic_cast<AbstractFloorCeilNode*>(child);
    if (fc_child != nullptr) {
        optional<int> child_depth = depth.has_value() ? make_optional(depth.value() - 1) : nullopt;
        return fc_child->convertToFullUpToDepth(child_depth);
    }
    return make_pair(false, true);
}


pair<bool, bool> DecisionNode::convertToFullUpToDecision() {
    bool children_changed = false;
    bool is_final = true;

    int depth = 0;
    while (meaningful_actions_.size() > 1) {
        auto [changed, final] = convertToFullUpToDepth(depth);
        if (changed) { children_changed = true; }
        if (final) { is_final = true; break; }
        depth += 1;
    }

    return make_pair(children_changed, is_final);
}

} // namespace blackjack
