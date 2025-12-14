#include "floor_ceil_node.h"

#include <algorithm>
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
    FloorCeilNode(
        bj_round,
        shoe,
        max_hand_size_full_enum,
        dealer_sim_depth,
        sim_algo,
        parent
    ),
    possible_actions_()
{
    // Initialize possible_actions from available actions
    // Split exclusion is handled by the game round itself
    possible_actions_ = bj_round_.getAvailableActions();
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
    FloorCeilNode::rebuildChildren();
    possible_actions_ = bj_round_.getAvailableActions();
}

void DecisionNode::computeFloorValue() {
    if (possible_actions_.size() > 1) {
        // Get the highest floor value among all possible actions
        double max_floor = -1e18;
        for (const auto& action : possible_actions_) {
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
            FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(children_[child_idx].get());
            ValueNode* v_child = dynamic_cast<ValueNode*>(children_[child_idx].get());
            FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(children_[child_idx].get());
            
            if (fc_child != nullptr) {
                child_floor = fc_child->getFloorValue();
            } else if (v_child != nullptr) {
                child_floor = v_child->getFloorValue();
            } else if (fcv_child != nullptr) {
                child_floor = fcv_child->getFloorValue();
            } else {
                child_floor = children_[child_idx]->getValue();
            }
            
            if (child_floor > max_floor) {
                max_floor = child_floor;
            }
        }
        floor_value_ = max_floor;
    } else {
        auto* child = getDecisionChoiceChild();
        FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(child);
        ValueNode* v_child = dynamic_cast<ValueNode*>(child);
        FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(child);
        
        if (fc_child != nullptr) {
            floor_value_ = fc_child->getFloorValue();
        } else if (v_child != nullptr) {
            floor_value_ = v_child->getFloorValue();
        } else if (fcv_child != nullptr) {
            floor_value_ = fcv_child->getFloorValue();
        } else {
            floor_value_ = child->getValue();
        }
    }
}

void DecisionNode::computeCeilValue() {
    if (possible_actions_.size() > 1) {
        // Get the highest ceil value among all possible actions
        double max_ceil = -1e18;
        for (const auto& action : possible_actions_) {
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
            FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(children_[child_idx].get());
            ValueNode* v_child = dynamic_cast<ValueNode*>(children_[child_idx].get());
            FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(children_[child_idx].get());
            
            if (fc_child != nullptr) {
                child_ceil = fc_child->getCeilValue();
            } else if (v_child != nullptr) {
                child_ceil = v_child->getCeilValue();
            } else if (fcv_child != nullptr) {
                child_ceil = fcv_child->getCeilValue();
            } else {
                child_ceil = children_[child_idx]->getValue();
            }
            
            if (child_ceil > max_ceil) {
                max_ceil = child_ceil;
            }
        }
        ceil_value_ = max_ceil;
    } else {
        auto* child = getDecisionChoiceChild();
        FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(child);
        ValueNode* v_child = dynamic_cast<ValueNode*>(child);
        FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(child);
        
        if (fc_child != nullptr) {
            ceil_value_ = fc_child->getCeilValue();
        } else if (v_child != nullptr) {
            ceil_value_ = v_child->getCeilValue();
        } else if (fcv_child != nullptr) {
            ceil_value_ = fcv_child->getCeilValue();
        } else {
            ceil_value_ = child->getValue();
        }
    }
}

optional<PlayerAction> DecisionNode::getDecisionChoice() const {
    if (possible_actions_.size() != 1) {
        return nullopt;
    }
    return possible_actions_[0];
}

AbstractBJTreeNode* DecisionNode::getDecisionChoiceChild() {
    if (possible_actions_.size() != 1) {
        throw runtime_error("Decision choice is not yet made (multiple actions still possible).");
    }
    PlayerAction action = possible_actions_[0];
    
    for (size_t i = 0; i < children_events_.size(); ++i) {
        if (holds_alternative<PlayerAction>(children_events_[i]) &&
            get<PlayerAction>(children_events_[i]) == action) {
            return children_[i].get();
        }
    }
    throw runtime_error("Decision choice child not found.");
}

bool DecisionNode::hasDecided() const {
    return possible_actions_.size() == 1;
}

vector<pair<PlayerAction, AbstractBJTreeNode*>> DecisionNode::getPossibleActionChildren() {
    vector<pair<PlayerAction, AbstractBJTreeNode*>> result;
    for (const auto& action : possible_actions_) {
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
    if (possible_actions_.empty()) {
        throw runtime_error("No possible actions remaining to evaluate.");
    }

    // Identify the best-valued action among those still possible
    vector<double> values;
    vector<size_t> child_indices;
    
    for (const auto& action : possible_actions_) {
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
    
    for (const auto& action : possible_actions_) {
        for (size_t i = 0; i < children_events_.size(); ++i) {
            if (holds_alternative<PlayerAction>(children_events_[i]) &&
                get<PlayerAction>(children_events_[i]) == action) {
                
                FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(children_[i].get());
                ValueNode* v_child = dynamic_cast<ValueNode*>(children_[i].get());
                FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(children_[i].get());
                
                double child_floor, child_ceil;
                if (fc_child != nullptr) {
                    child_floor = fc_child->getFloorValue();
                    child_ceil = fc_child->getCeilValue();
                } else if (v_child != nullptr) {
                    child_floor = v_child->getFloorValue();
                    child_ceil = v_child->getCeilValue();
                } else if (fcv_child != nullptr) {
                    child_floor = fcv_child->getFloorValue();
                    child_ceil = fcv_child->getCeilValue();
                } else {
                    child_floor = children_[i]->getValue();
                    child_ceil = children_[i]->getValue();
                }
                
                floor_values.push_back(child_floor);
                ceil_values.push_back(child_ceil);
                break;
            }
        }
    }
    
    // Find the maximum floor value among all possible actions
    double max_floor = *max_element(floor_values.begin(), floor_values.end());
    
    // Exclude actions whose ceiling is below the max floor
    vector<PlayerAction> new_possible_actions;
    for (size_t i = 0; i < possible_actions_.size(); ++i) {
        if (ceil_values[i] >= max_floor) {
            new_possible_actions.push_back(possible_actions_[i]);
        }
    }
    
    possible_actions_ = new_possible_actions;
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
    for (const auto& a : possible_actions_) {
        BJRound bj_round_child = bj_round_.copy();
        ProbabilisticRankShoe shoe_copy(shoe_);

        if (a == PlayerAction::SPLIT) {
            // The first hand of the split is set to <card>2 hand with zero value
            // to simplify the tree only the second one is considered, it's value is doubled
            // this is handled inside SplitNode
            bj_round_child.takeAction(PlayerAction::SPLIT);
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
    BJRound bj_round_child = bj_round_.copy();
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

pair<bool, bool> DecisionNode::convertToFullUpToDepth(int depth) {
    if (!treeCompleted()) {
        throw runtime_error(
            "Cannot convert DecisionNode to full enum in an incomplete tree."
        );
    }

    if (depth < 0) {
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
    
    if (children_changed) {
        recomputeTreeValue();
    }
    
    return make_pair(children_changed, is_final);
}

pair<bool, bool> DecisionNode::convertBjCheckChildrenToFullUpToDepth(int depth) {
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
    
    FloorCeilNode* fc_node = dynamic_cast<FloorCeilNode*>(dealer_no_bj_round_tree.get());
    if (fc_node == nullptr) {
        return make_pair(false, true);
    }
    
    auto [round_child_changed, child_is_final] = fc_node->convertToFullUpToDepth(depth - 2);
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

pair<bool, bool> DecisionNode::convertPossibleChildrenToFullUpToDepth(int depth) {
    bool children_changed = false;
    bool is_final = true;
    
    for (const auto& action : possible_actions_) {
        for (size_t i = 0; i < children_events_.size(); ++i) {
            if (holds_alternative<PlayerAction>(children_events_[i]) &&
                get<PlayerAction>(children_events_[i]) == action) {
                
                ValueNode* v_child = dynamic_cast<ValueNode*>(children_[i].get());
                DoubleNode* d_child = dynamic_cast<DoubleNode*>(children_[i].get());
                
                if (v_child != nullptr || d_child != nullptr) {
                    break;
                }
                
                FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(children_[i].get());
                if (fc_child != nullptr) {
                    auto [child_changed, child_is_final] = fc_child->convertToFullUpToDepth(depth - 1);
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

pair<bool, bool> DecisionNode::convertDecisionChildToFullUpToDepth(int depth) {
    auto* child = getDecisionChoiceChild();
    
    ValueNode* v_child = dynamic_cast<ValueNode*>(child);
    DoubleNode* d_child = dynamic_cast<DoubleNode*>(child);
    
    if (v_child != nullptr || d_child != nullptr) {
        return make_pair(false, true);
    }
    
    FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(child);
    if (fc_child != nullptr) {
        return fc_child->convertToFullUpToDepth(depth - 1);
    }
    return make_pair(false, true);
}

} // namespace blackjack
