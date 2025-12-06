#include "floor_ceil_node.h"

#include <stdexcept>

using namespace std;

namespace blackjack {

DealerCheckBJNode::DealerCheckBJNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int max_hand_size_full_enum,
    bool took_insurance,
    bool insurance_offered,
    int dealer_sim_depth,
    SimAlgo sim_algo,
    AbstractBJTreeNode* parent,
    int n_splits_happened
) :
    FloorCeilNode(
        bj_round,
        shoe,
        max_hand_size_full_enum,
        dealer_sim_depth,
        sim_algo,
        parent,
        n_splits_happened
    ),
    dealer_bj_child_idx_(0),
    dealer_no_bj_child_idx_(1),
    insurance_offered_(insurance_offered),
    took_insurance_(took_insurance),
    insurance_bet_(static_cast<double>(bj_round_.bet_unit) / 2.0)
{
    // bj_round state should always reject insurance to avoid 2 identical trees
    // game rules should be such that dealer checks blackjack
    if (!bj_round_.rules_->dealer_checks_blackjack) {
        throw runtime_error("DealerCheckBJNode requires dealer_checks_blackjack rule");
    }
    if (bj_round_.insurance_bet != 0) {
        throw runtime_error("DealerCheckBJNode expects insurance_bet == 0 in bj_round");
    }
}

void DealerCheckBJNode::createChild(
    const BJRound& child_bj_round,
    const ProbabilisticRankShoe& child_shoe,
    const TransitionEvent& event,
    double prob
) {
    auto child = make_shared<DecisionNode>(
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

double DealerCheckBJNode::getDealerBlackjackChance() const {
    auto rank_prob = shoe_.get_rank_value_probabilities(nullopt);
    
    if (bj_round_.dealer_hand.size() != 1) {
        throw runtime_error("Expected dealer hand size 1 for blackjack chance calculation");
    }
    
    int dealer_upcard = bj_round_.dealer_hand.values()[0];
    if (dealer_upcard != 10 && dealer_upcard != 11) {
        throw runtime_error("Dealer upcard must be 10 or 11 for blackjack check");
    }
    
    if (dealer_upcard == 10) {
        return rank_prob.at(11);
    } else {
        return rank_prob.at(10);
    }
}

void DealerCheckBJNode::buildChildren() {
    BJRound bj_round_child = bj_round_.copy();
    
    const auto& active_hand = bj_round_.player_hands[static_cast<size_t>(bj_round_.active_hand_idx)];
    bool player_has_bj = active_hand.is_natural_blackjack();

    double insurance_bet = static_cast<double>(bj_round_.bet_unit) / 2.0;
    double player_value_dealer_bj;
    
    if (player_has_bj) {
        // insurance_bet * (1 + insurance_payout) + bet_unit - (bet_unit + insurance_bet)
        double player_value_dealer_bj_insurance = bj_round_.rules_->insurance_payout * insurance_bet;
        // bet_unit - bet_unit = 0
        double player_value_dealer_bj_no_insurance = 0.0;
        
        if (took_insurance_) {
            player_value_dealer_bj = player_value_dealer_bj_insurance;
        } else {
            player_value_dealer_bj = player_value_dealer_bj_no_insurance;
        }
    } else {
        // insurance_bet * (1 + insurance_payout) - (bet_unit + insurance_bet)
        double player_value_dealer_bj_insurance = 
            insurance_bet * bj_round_.rules_->insurance_payout - static_cast<double>(bj_round_.bet_unit);
        // -(bet_unit)
        double player_value_dealer_bj_no_insurance = -static_cast<double>(bj_round_.bet_unit);
        
        if (took_insurance_) {
            player_value_dealer_bj = player_value_dealer_bj_insurance;
        } else {
            player_value_dealer_bj = player_value_dealer_bj_no_insurance;
        }
    }

    auto dealer_bj_value_node = make_shared<ValueNode>(player_value_dealer_bj, this);

    BJRound bj_round_no_bj_child = bj_round_child.copy();
    ProbabilisticRankShoe shoe_copy_no_bj(shoe_);
    bj_round_no_bj_child.takeAction(DealerAction::CONFIRM_NO_BLACKJACK);
    shoe_copy_no_bj.lockDealerCardNotTen();

    shared_ptr<AbstractBJTreeNode> dealer_no_bj_node;
    if (player_has_bj) {
        dealer_no_bj_node = make_shared<ValueNode>(
            static_cast<double>(bj_round_.bet_unit) * bj_round_.rules_->natural_blackjack_payout,
            this
        );
    } else {
        dealer_no_bj_node = make_shared<DecisionNode>(
            bj_round_no_bj_child, shoe_copy_no_bj,
            max_hand_size_full_enum_,
            dealer_sim_depth_,
            sim_algo_,
            this,
            n_splits_happened_
        );
    }

    double p_blackjack = getDealerBlackjackChance();
    double p_no_blackjack = 1.0 - p_blackjack;

    if (dealer_bj_child_idx_ == 0 && dealer_no_bj_child_idx_ == 1) {
        addChild(dealer_bj_value_node, DealerAction::CONFIRM_BLACKJACK, p_blackjack);
        addChild(dealer_no_bj_node, DealerAction::CONFIRM_NO_BLACKJACK, p_no_blackjack);
    } else if (dealer_bj_child_idx_ == 1 && dealer_no_bj_child_idx_ == 0) {
        addChild(dealer_no_bj_node, DealerAction::CONFIRM_NO_BLACKJACK, p_no_blackjack);
        addChild(dealer_bj_value_node, DealerAction::CONFIRM_BLACKJACK, p_blackjack);
    } else {
        throw runtime_error("Invalid child index configuration in DealerCheckBJNode.");
    }
    
    has_built_children_ = true;
}

void DealerCheckBJNode::computeNodeValue() {
    if (!took_insurance_) {
        // Simple chance node
        computeChanceNodeValue();
    } else {
        // Dealer_blackjack node value is correct
        // no_blackjack decision tree node value should be reduced by insurance amount
        vector<double> node_values;
        for (const auto& ch : children_) {
            node_values.push_back(ch->getValue());
        }
        node_values[static_cast<size_t>(dealer_no_bj_child_idx_)] -= insurance_bet_;
        
        value_ = 0.0;
        for (size_t i = 0; i < children_prob_.size(); ++i) {
            value_ += children_prob_[i] * node_values[i];
        }
    }
    computeCeilValue();
    computeFloorValue();
}

void DealerCheckBJNode::computeCeilValue() {
    if (!took_insurance_) {
        computeChanceNodeCeilValue();
    } else {
        vector<double> ceil_values;
        for (const auto& ch : children_) {
            FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(ch.get());
            ValueNode* v_child = dynamic_cast<ValueNode*>(ch.get());
            FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(ch.get());
            
            if (fc_child != nullptr) {
                ceil_values.push_back(fc_child->getCeilValue());
            } else if (v_child != nullptr) {
                ceil_values.push_back(v_child->getCeilValue());
            } else if (fcv_child != nullptr) {
                ceil_values.push_back(fcv_child->getCeilValue());
            } else {
                ceil_values.push_back(ch->getValue());
            }
        }
        ceil_values[static_cast<size_t>(dealer_no_bj_child_idx_)] -= insurance_bet_;
        
        ceil_value_ = 0.0;
        for (size_t i = 0; i < children_prob_.size(); ++i) {
            ceil_value_ += children_prob_[i] * ceil_values[i];
        }
    }
}

void DealerCheckBJNode::computeFloorValue() {
    if (!took_insurance_) {
        computeChanceNodeFloorValue();
    } else {
        vector<double> floor_values;
        for (const auto& ch : children_) {
            FloorCeilNode* fc_child = dynamic_cast<FloorCeilNode*>(ch.get());
            ValueNode* v_child = dynamic_cast<ValueNode*>(ch.get());
            FloorCeilValueNode* fcv_child = dynamic_cast<FloorCeilValueNode*>(ch.get());
            
            if (fc_child != nullptr) {
                floor_values.push_back(fc_child->getFloorValue());
            } else if (v_child != nullptr) {
                floor_values.push_back(v_child->getFloorValue());
            } else if (fcv_child != nullptr) {
                floor_values.push_back(fcv_child->getFloorValue());
            } else {
                floor_values.push_back(ch->getValue());
            }
        }
        floor_values[static_cast<size_t>(dealer_no_bj_child_idx_)] -= insurance_bet_;
        
        floor_value_ = 0.0;
        for (size_t i = 0; i < children_prob_.size(); ++i) {
            floor_value_ += children_prob_[i] * floor_values[i];
        }
    }
}

bool DealerCheckBJNode::convertToFullUpToDepth(int depth) {
    if (insurance_offered_) {
        // In the case of insurance, tree expansion and value update should be handled by
        // decision node
        throw runtime_error(
            "Cannot convert DealerCheckBJNode with insurance to full enumeration directly."
        );
    } else {
        // Case where dealer checks for bj but insurance is not offered
        return FloorCeilNode::convertToFullUpToDepth(depth);
    }
}

} // namespace blackjack
