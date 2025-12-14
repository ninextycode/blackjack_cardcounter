#include "floor_ceil_node.h"

#include <algorithm>
#include <stdexcept>
#include <cmath>

using namespace std;

namespace blackjack {

HitNode::HitNode(
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
    rank_probabilities_(),
    cards_bust_(),
    cards_21_(),
    cards_not_sampled_(),
    cards_sampled_(),
    p_bust_(0.0),
    p_21_(0.0),
    max_child_value_(static_cast<double>(bj_round_.bet_unit)),
    min_child_value_(-static_cast<double>(bj_round_.bet_unit))
{
    rank_probabilities_ = shoe_.getRankValueProbabilities(nullopt);
    initValues();
}

void HitNode::createChild(
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
        this
    );
    children_.push_back(child);
    children_prob_.push_back(prob);
    children_events_.push_back(event);
}

void HitNode::initValues() {
    const auto& hand = bj_round_.player_hands[static_cast<size_t>(bj_round_.active_hand_idx)];
    
    for (int c = 2; c <= 11; ++c) {
        double p = rank_probabilities_.at(c);
        if (p <= 0.0) {
            continue;
        }
        
        ValueOnlyHand hand_copy = hand;
        hand_copy.add_value(c);
        
        if (hand_copy.is_bust()) {
            p_bust_ += p;
            cards_bust_.push_back(c);
        } else if (hand_copy.get_best_value().value() == 21) {
            p_21_ += p;
            cards_21_.push_back(c);
        } else {
            cards_not_sampled_.push_back(c);
        }
    }
    
    int hand_size = hand.size();
    if (hand_size <= max_hand_size_full_enum_) {
        // Hand is small enough - use full enumeration for all remaining cards
        cards_sampled_ = cards_not_sampled_;
        cards_not_sampled_.clear();
    } else {
        // Hand is large - sample only one card, rest remain unsampled
        if (!cards_not_sampled_.empty()) {
            int sample_card = shoe_.sampleRank(cards_not_sampled_);
            auto it = find(cards_not_sampled_.begin(), cards_not_sampled_.end(), sample_card);
            if (it != cards_not_sampled_.end()) {
                cards_not_sampled_.erase(it);
                cards_sampled_.push_back(sample_card);
            }
        }
    }
}

void HitNode::computeNodeValue() {
    double nodes_with_value_cum_prob = 0.0;
    vector<double> values;
    vector<double> probs;
    
    for (size_t i = 0; i < children_.size(); ++i) {
        int ch_card = get<int>(children_events_[i]);
        double ch_prob = children_prob_[i];
        
        // Check if this card is in cards_not_sampled
        bool is_not_sampled = find(cards_not_sampled_.begin(), cards_not_sampled_.end(), ch_card) 
                              != cards_not_sampled_.end();
        if (is_not_sampled) {
            continue;
        }
        
        nodes_with_value_cum_prob += ch_prob;
        values.push_back(children_[i]->getValue());
        probs.push_back(ch_prob);
    }
    
    double weighted_sum = 0.0;
    for (size_t i = 0; i < values.size(); ++i) {
        weighted_sum += probs[i] * values[i];
    }
    value_ = weighted_sum / nodes_with_value_cum_prob;
    
    computeChanceNodeCeilValue();
    computeChanceNodeFloorValue();
}

bool HitNode::canAddSample() const {
    return !cards_not_sampled_.empty();
}

bool HitNode::addSample() {
    if (!canAddSample()) {
        return false;
    }
    
    int sample_card = shoe_.sampleRank(cards_not_sampled_);
    auto it = find(cards_not_sampled_.begin(), cards_not_sampled_.end(), sample_card);
    if (it != cards_not_sampled_.end()) {
        cards_not_sampled_.erase(it);
        cards_sampled_.push_back(sample_card);
    }
    
    ProbabilisticRankShoe shoe_copy(shoe_);
    shoe_copy.burnRankValue(sample_card);
    BJRound bj_round_copy = bj_round_.copy();
    bj_round_copy.takeCard(sample_card);
    
    auto new_child = make_shared<DecisionNode>(
        bj_round_copy, shoe_copy,
        max_hand_size_full_enum_,
        dealer_sim_depth_,
        sim_algo_,
        this
    );

    // Find and replace the old child
    for (size_t i = 0; i < children_events_.size(); ++i) {
        if (holds_alternative<int>(children_events_[i]) &&
            get<int>(children_events_[i]) == sample_card) {
            children_[i]->parent_ = nullptr;
            children_[i] = new_child;
            break;
        }
    }
    
    return true;
}

bool HitNode::convertFromSampleToFull() {
    if (cards_not_sampled_.empty()) {
        return false;
    }
    
    for (int c : cards_not_sampled_) {
        ProbabilisticRankShoe shoe_copy(shoe_);
        shoe_copy.burnRankValue(c);
        BJRound bj_round_copy = bj_round_.copy();
        bj_round_copy.takeCard(c);
        
        auto new_child = make_shared<DecisionNode>(
            bj_round_copy, shoe_copy,
            max_hand_size_full_enum_,
            dealer_sim_depth_,
            sim_algo_,
            this
        );
        
        // Find and replace the old child
        for (size_t i = 0; i < children_events_.size(); ++i) {
            if (holds_alternative<int>(children_events_[i]) &&
                get<int>(children_events_[i]) == c) {
                children_[i]->parent_ = nullptr;
                children_[i] = new_child;
                break;
            }
        }
    }

    cards_not_sampled_.clear();
    return true;
}

void HitNode::buildChildren() {
    BJStage stage = bj_round_.getStage();
    if (stage != BJStage::PLAYER_CARD) {
        throw runtime_error(
            "HitNode can only build children for PLAYER_CARD stage."
        );
    }

    // Build children for cards that result in 21
    for (int c : cards_21_) {
        ProbabilisticRankShoe child_shoe(shoe_);
        child_shoe.burnRankValue(c);
        BJRound child_bj_round = bj_round_.copy();
        child_bj_round.takeCard(c);
        
        double value_21 = runDealerSim(child_bj_round, child_shoe);
        auto child = make_shared<ValueNode>(value_21, this);
        double p = rank_probabilities_.at(c);
        addChild(child, c, p);
        
        // Update max_child_value with value of 21 node
        max_child_value_ = value_21;
    }

    // Build children for bust cards
    for (int c : cards_bust_) {
        auto child = make_shared<ValueNode>(-static_cast<double>(bj_round_.bet_unit), this);
        double p = rank_probabilities_.at(c);
        addChild(child, c, p);
    }

    // Build children for sampled cards
    for (int c : cards_sampled_) {
        ProbabilisticRankShoe child_shoe(shoe_);
        child_shoe.burnRankValue(c);
        BJRound child_bj_round = bj_round_.copy();
        child_bj_round.takeCard(c);
        
        auto child = make_shared<DecisionNode>(
            child_bj_round, child_shoe,
            max_hand_size_full_enum_,
            dealer_sim_depth_,
            sim_algo_,
            this
        );
        double p = rank_probabilities_.at(c);
        addChild(child, c, p);
    }
    
    // Build placeholder children for not-sampled cards
    for (int c : cards_not_sampled_) {
        auto child = make_shared<FloorCeilValueNode>(
            min_child_value_, max_child_value_, this
        );
        double p = rank_probabilities_.at(c);
        addChild(child, c, p);
    }
    
    // Verify probabilities sum to approximately 1
    double prob_sum = 0.0;
    for (double p : children_prob_) {
        prob_sum += p;
    }
    if (abs(1.0 - prob_sum) >= 1e-7) {
        throw runtime_error("Children probabilities do not sum to 1");
    }
    
    has_built_children_ = true;
}

} // namespace blackjack
