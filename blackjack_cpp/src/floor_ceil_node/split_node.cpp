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
    original_bj_round_(bj_round_)
{
    BJStage stage = original_bj_round_.getStage();
    if (stage != BJStage::PLAYER_ACTION) {
        throw runtime_error("SplitNode requires PLAYER_ACTION stage");
    }
    
    auto available_actions = bj_round_.getAvailableActions();
    bool split_available = false;
    for (const auto& a : available_actions) {
        if (a == PlayerAction::SPLIT) {
            split_available = true;
            break;
        }
    }
    if (!split_available) {
        throw runtime_error("SPLIT action not available in SplitNode");
    }

    // Build a new bj_round object - only first player card, waiting for the second
    // On player getting "2-card 21" after split, dealer has to keep hitting
    // Set ignore_player_natural_blackjack to true (no_natural_bj_on_split)
    BJRules subtree_rules = *bj_round_.rules_;
    subtree_rules.no_natural_bj_on_split = true;
    subtree_rules.ignore_player_natural_blackjack = true;
    
    // Create new round with modified rules using shared_ptr
    auto subtree_rules_ptr = make_shared<const BJRules>(subtree_rules);
    BJRound subtree_bj_round(subtree_rules_ptr);
    subtree_bj_round.startRound(bj_round_.bet_unit);
    subtree_bj_round.takeCard(bj_round_.player_hands[0].values()[0]);
    
    bj_round_ = subtree_bj_round;
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
            this,
            n_splits_happened_
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
    auto card_probabilities = shoe_.get_rank_value_probabilities(nullopt);
    
    for (int card = 2; card <= 11; ++card) {
        double prob = card_probabilities.at(card);
        if (prob <= 0.0) {
            continue;
        }
        
        ProbabilisticRankShoe child_shoe(shoe_);
        child_shoe.burnRankValue(card);

        BJRound child_bj_round = bj_round_.copy();
        child_bj_round.takeCard(card);
        child_bj_round.takeCard(original_bj_round_.dealer_hand.values()[0]);

        BJStage stage = child_bj_round.getStage();
        if (stage == BJStage::PLAYER_OFFERED_INSURANCE) {
            if (original_bj_round_.insurance_bet > 0) {
                child_bj_round.takeAction(PlayerAction::TAKE_INSURANCE);
            } else {
                child_bj_round.takeAction(PlayerAction::REFUSE_INSURANCE);
            }
        }

        if (child_bj_round.getStage() == BJStage::DEALER_CHECK_BJ) {
            // Split action would only be possible if dealer does not have blackjack
            child_bj_round.takeAction(DealerAction::CONFIRM_NO_BLACKJACK);
        }

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
