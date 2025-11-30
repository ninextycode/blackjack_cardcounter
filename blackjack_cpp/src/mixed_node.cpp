#include "mixed_node.h"

#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>
#include <set>

using namespace std;

namespace blackjack {

// NodeIterator implementation
NodeIterator::NodeIterator(AbstractBJTreeNode* root) {
    if (root != nullptr) {
        queue_.push_back({0, root});
    }
}

bool NodeIterator::hasNext() {
    return !queue_.empty();
}

NodeLevel NodeIterator::next() {
    if (queue_.empty()) {
        throw runtime_error("No more nodes in iterator");
    }
    NodeLevel current = queue_.front();
    queue_.pop_front();
    
    for (auto &child : current.node->children_) {
        queue_.push_back({current.level + 1, child.get()});
    }
    
    return current;
}

// MixedNode implementation
MixedNode::MixedNode(
    const BJRound &bj_round,
    const ProbabilisticRankShoe &shoe,
    int max_hand_size_full_enum,
    int player_card_initial_samples,
    size_t n_dealer_sim_runs,
    AbstractBJTreeNode *parent
) :
    AbstractBJTreeNode(bj_round, shoe, parent),
    hand_size_full_enum_limit_(max_hand_size_full_enum),
    n_dealer_sim_runs_(n_dealer_sim_runs),
    player_card_initial_samples_(player_card_initial_samples),
    active_hand_size_(nullopt)
{
    if (n_dealer_sim_runs_ == 0) {
        throw invalid_argument("n_dealer_sim_runs must be greater than 0");
    }

    // Calculate active hand size
    const auto &hands = bj_round_.player_hands;
    int hand_idx = bj_round_.active_hand_idx;
    if (hand_idx >= 0 && hand_idx < static_cast<int>(hands.size())) {
        active_hand_size_ = hands[static_cast<size_t>(hand_idx)].size();
    }
}

bool MixedNode::isPastThreeInitialCards() const {
    bool has_dealer_card = bj_round_.dealer_hand.size() > 0;
    bool has_player_two_cards = 
        bj_round_.player_hands.size() > 1 ||
        bj_round_.player_hands[0].size() >= 2;
    return has_dealer_card && has_player_two_cards;
}

void MixedNode::createChild(
    const BJRound &child_bj_round,
    const ProbabilisticRankShoe &child_shoe,
    const TransitionEvent &event,
    double prob
) {
    auto child = make_shared<MixedNode>(
        child_bj_round,
        child_shoe,
        hand_size_full_enum_limit_,
        player_card_initial_samples_,
        n_dealer_sim_runs_,
        this
    );
    children_.push_back(child);
    children_prob_.push_back(prob);
    children_events_.push_back(event);
}

void MixedNode::buildChildrenPlayerCard() {
    if (active_hand_size_.has_value() && 
        active_hand_size_.value() > hand_size_full_enum_limit_) 
    {
        // Perform sampling
        for (int i = 0; i < player_card_initial_samples_; ++i) {
            addPlayerCardSampleImpl();
        }
    } else {
        // Do full enumeration
        buildFullChildrenPlayerCard();
    }
}

void MixedNode::buildFullChildrenPlayerCard() {
    auto possible_ranks_opt = bj_round_.getPossibleNextCardRanks();
    
    vector<int> possible_values;
    if (possible_ranks_opt.has_value()) {
        possible_values = possible_ranks_opt.value();
        sort(possible_values.begin(), possible_values.end());
        possible_values.erase(
            unique(possible_values.begin(), possible_values.end()),
            possible_values.end()
        );
    } else {
        for (int v = 2; v <= 11; ++v) {
            possible_values.push_back(v);
        }
    }

    auto card_value_probabilities = shoe_.get_rank_value_probabilities(possible_values);

    for (int rv : possible_values) {
        double p = card_value_probabilities.at(rv);
        if (p <= 0.0) {
            continue;
        }

        auto bj_round_copy = bj_round_.copy();
        ProbabilisticRankShoe shoe_copy(shoe_);
        bj_round_copy.takeCard(rv);
        shoe_copy.burnRankValue(rv);

        createChild(bj_round_copy, shoe_copy, rv, p);
    }
}

void MixedNode::buildChildrenDealerCard() {
    if (bj_round_.dealerExpectsToShowBlackjack()) {
        buildChildDealerBlackjack();
    } else {
        runDealerCardsSimulations();
    }
    has_built_children_ = true;
}

void MixedNode::buildChildDealerBlackjack() {
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

void MixedNode::runDealerCardsSimulations() {
    vector<double> values;
    values.reserve(n_dealer_sim_runs_);

    for (size_t i = 0; i < n_dealer_sim_runs_; ++i) {
        auto bj_round_copy = bj_round_.copy();
        ProbabilisticRankShoe shoe_copy(shoe_);

        // Simulate dealer cards until round over
        while (bj_round_copy.getStage() != BJStage::ROUND_OVER) {
            auto possible_values = bj_round_copy.getPossibleNextCardRanks();
            int card = shoe_copy.sampleAndBurnRank(possible_values);
            bj_round_copy.takeCard(card);
        }

        // Collect results
        values.push_back(static_cast<double>(bj_round_copy.getPlayerValue()));
    }

    double mean_value = accumulate(values.begin(), values.end(), 0.0) / 
                        static_cast<double>(values.size());
    
    auto sim_node = make_shared<SimulationResultNode>(mean_value, this);
    children_.push_back(sim_node);
    children_prob_.push_back(1.0);
}

bool MixedNode::addPlayerCardSample() {
    if (!treeCompleted()) {
        throw runtime_error("Cannot add player card sample to incomplete tree.");
    }

    MixedNode* updated_node = nullptr;
    NodeIterator iter(this);
    
    while (iter.hasNext()) {
        NodeLevel nl = iter.next();
        
        // Check if it's a MixedNode
        MixedNode* mixed = dynamic_cast<MixedNode*>(nl.node);
        if (mixed == nullptr) {
            continue;
        }
        
        if (mixed->bj_round_.getStage() != BJStage::PLAYER_CARD) {
            continue;
        }

        bool added_to_node = mixed->addPlayerCardSampleImpl();
        if (added_to_node) {
            updated_node = mixed;
            break;
        }
    }

    if (updated_node == nullptr) {
        return false;
    }

    // Recompute values up from updated_node to this
    AbstractBJTreeNode* node = updated_node;
    while (node != this) {
        node->recomputeTreeValue();
        node = node->parent_;
    }
    this->recomputeTreeValue();
    return true;
}

bool MixedNode::addPlayerCardSampleImpl() {
    if (bj_round_.getStage() != BJStage::PLAYER_CARD) {
        throw runtime_error("Can only add player card sample at PLAYER_CARD stage");
    }

    // Build set of old values
    set<int> old_values_set;
    for (const auto &event : children_events_) {
        if (holds_alternative<int>(event)) {
            old_values_set.insert(get<int>(event));
        }
    }

    // Get possible ranks
    auto possible_ranks_opt = bj_round_.getPossibleNextCardRanks();
    vector<int> possible_values;
    if (possible_ranks_opt.has_value()) {
        possible_values = possible_ranks_opt.value();
    } else {
        for (int v = 2; v <= 11; ++v) {
            possible_values.push_back(v);
        }
    }

    // Find new values
    vector<int> new_values;
    for (int rv : possible_values) {
        if (old_values_set.find(rv) == old_values_set.end()) {
            new_values.push_back(rv);
        }
    }

    if (new_values.empty()) {
        return false;
    }

    // Sample a card
    auto shoe_sample = shoe_;
    int card = shoe_sample.sampleRank(new_values);

    // Calculate new probabilities while conditioning on existing samples
    // but before burning the sampled card
    set<int> all_sampled_values = old_values_set;
    all_sampled_values.insert(card);
    vector<int> all_sampled_vec(all_sampled_values.begin(), all_sampled_values.end());
    
    auto new_probabilities = shoe_sample.get_rank_value_probabilities(all_sampled_vec);
    
    // Burn the card and create child
    shoe_sample.burnRankValue(card);
    auto bj_round_copy = bj_round_.copy();
    bj_round_copy.takeCard(card);
    
    createChild(bj_round_copy, shoe_sample, card, 1.0);

    // Update all probabilities
    children_prob_.clear();
    for (const auto &event : children_events_) {
        if (holds_alternative<int>(event)) {
            int rv = get<int>(event);
            children_prob_.push_back(new_probabilities.at(rv));
        } else {
            children_prob_.push_back(0.0);
        }
    }

    return true;
}

bool MixedNode::convertToFullNextLayer() {
    if (!treeCompleted()) {
        throw runtime_error(
            "Cannot convert player card sample to full enum in an incomplete tree."
        );
    }

    BJStage stage = bj_round_.getStage();
    bool children_changed = false;

    if (stage == BJStage::ROUND_OVER || stage == BJStage::DEALER_CARD) {
        return false;
    }
    else if (stage != BJStage::PLAYER_CARD) {
        for (auto &ch : children_) {
            MixedNode* mixed = dynamic_cast<MixedNode*>(ch.get());
            if (mixed != nullptr) {
                bool child_changed = mixed->convertToFullNextLayer();
                if (child_changed) {
                    children_changed = true;
                }
            }
        }
    }
    else {  // PLAYER_CARD
        // If there can be an extension from sample to full enumeration here - do it here
        // If not, go to children
        bool self_children_added = convertFromSampleToFull();
        if (!self_children_added) {
            for (auto &ch : children_) {
                MixedNode* mixed = dynamic_cast<MixedNode*>(ch.get());
                if (mixed != nullptr) {
                    bool child_changed = mixed->convertToFullNextLayer();
                    if (child_changed) {
                        children_changed = true;
                    }
                }
            }
        } else {
            children_changed = true;
        }
    }

    if (children_changed) {
        recomputeTreeValue();
        return true;
    }
    return false;
}

bool MixedNode::convertFromSampleToFull() {
    if (bj_round_.getStage() != BJStage::PLAYER_CARD) {
        throw runtime_error("Can only convert at PLAYER_CARD stage");
    }

    // Rebuild children with full enumeration
    vector<int> old_values;
    for (const auto &event : children_events_) {
        if (holds_alternative<int>(event)) {
            old_values.push_back(get<int>(event));
        }
    }

    auto possible_ranks_opt = bj_round_.getPossibleNextCardRanks();
    vector<int> possible_values;
    if (possible_ranks_opt.has_value()) {
        possible_values = possible_ranks_opt.value();
    } else {
        for (int v = 2; v <= 11; ++v) {
            possible_values.push_back(v);
        }
    }

    set<int> old_values_set(old_values.begin(), old_values.end());
    vector<int> new_values;
    for (int rv : possible_values) {
        if (old_values_set.find(rv) == old_values_set.end()) {
            new_values.push_back(rv);
        }
    }

    if (new_values.empty()) {
        return false;
    }

    auto new_probabilities = shoe_.get_rank_value_probabilities(possible_values);

    for (int new_rank_value : new_values) {
        ProbabilisticRankShoe shoe_sample(shoe_);
        shoe_sample.burnRankValue(new_rank_value);

        auto bj_round_copy = bj_round_.copy();
        bj_round_copy.takeCard(new_rank_value);
        
        createChild(bj_round_copy, shoe_sample, new_rank_value, 0.0);
    }

    // Update all probabilities
    children_prob_.clear();
    for (int rv : old_values) {
        children_prob_.push_back(new_probabilities.at(rv));
    }
    for (int rv : new_values) {
        children_prob_.push_back(new_probabilities.at(rv));
    }

    return true;
}

bool MixedNode::singleNodeFromSampleToFull() {
    if (!treeCompleted()) {
        throw runtime_error(
            "Cannot convert player card sample to full enum in an incomplete tree."
        );
    }

    // BFS: find the most shallow PLAYER_CARD MixedNode that can be converted
    MixedNode* updated_node = nullptr;
    NodeIterator iter(this);
    
    while (iter.hasNext()) {
        NodeLevel nl = iter.next();
        
        MixedNode* mixed = dynamic_cast<MixedNode*>(nl.node);
        if (mixed == nullptr) {
            continue;
        }
        
        if (mixed->bj_round_.getStage() != BJStage::PLAYER_CARD) {
            continue;
        }
        
        bool added = mixed->convertFromSampleToFull();
        if (added) {
            updated_node = mixed;
            break;
        }
    }

    if (updated_node == nullptr) {
        return false;
    }

    // Recompute values up from updated_node to self
    AbstractBJTreeNode* n = updated_node;
    while (n != this) {
        n->recomputeTreeValue();
        n = n->parent_;
    }
    this->recomputeTreeValue();
    return true;
}

bool MixedNode::convertToFullUpToDepth(int depth) {
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

    if (stage != BJStage::DEALER_CARD && stage != BJStage::ROUND_OVER) {
        for (auto &ch : children_) {
            MixedNode* mixed = dynamic_cast<MixedNode*>(ch.get());
            if (mixed != nullptr) {
                bool child_changed = mixed->convertToFullUpToDepth(depth - 1);
                if (child_changed) {
                    children_changed = true;
                }
            }
        }
    }

    if (stage == BJStage::PLAYER_CARD) {
        bool added_children_self = convertFromSampleToFull();
        if (added_children_self) {
            children_changed = true;
        }
    }

    if (children_changed) {
        recomputeTreeValue();
        return true;
    }
    return false;
}

} // namespace blackjack
