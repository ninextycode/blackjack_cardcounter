#include "tree_walker.h"
#include "abstract_node.h"
#include "actions.h"
#include "blackjack_round.h"
#include "floor_ceil_node.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#include <iostream>

using namespace std;

namespace blackjack {




TreeWalker TreeWalker::buildInitialTreeWalker(
    pair<int, int> player_cards,
    int dealer_card,
    const BJRules& rules,
    RankCount shoe_rank_count,
    int max_hand_size_full_enum,
    int dealer_sim_depth,
    SimAlgo sim_algo,
    int bet_unit,
    bool initial_cards_burned
) {
    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    bj_round.startRound(bet_unit);
    bj_round.takeCard(player_cards.first);
    bj_round.takeCard(player_cards.second);
    bj_round.takeCard(dealer_card);

    ProbabilisticRankShoe shoe(shoe_rank_count, RandomSampler::createNextSampler());
    if (!initial_cards_burned) {
        shoe.burnRankValue(player_cards.first);
        shoe.burnRankValue(player_cards.second);
        shoe.burnRankValue(dealer_card);
    }
    
    auto root_node = buildNonFinalRootNode(
        bj_round,
        shoe,
        max_hand_size_full_enum,
        dealer_sim_depth,
        sim_algo
    );

    return TreeWalker(root_node);
}




TreeWalker::TreeWalker(
    shared_ptr<AbstractFloorCeilNode> root_node
) :
    current_node_(root_node),
    common_shoe_(root_node->shoe_),
    max_hand_size_full_enum_(root_node->max_hand_size_full_enum_),
    dealer_sim_depth_(root_node->dealer_sim_depth_),
    sim_algo_(root_node->sim_algo_),
    is_split_pending_(false),
    reference_round_(root_node->bj_round_)
{
    current_node_->buildTree();
    convertToFullUpToDecision();
}

void TreeWalker::convertToFullUpToDecision() {    
    if (current_node_ == nullptr) {
        throw runtime_error("TreeWalker: Cannot convert to full up to decision in finished state - current node is nullptr"); 
    }
    // make sure every child is built, not just a sample
    // this also avoids FloorCeilValueNode placeholders as children
    current_node_->convertToFullUpToDepth(0);  

    shared_ptr<DecisionNode> current_node_decision_cast = \
        dynamic_pointer_cast<DecisionNode>(current_node_);
    if (current_node_decision_cast != nullptr) {
        current_node_decision_cast->convertToFullUpToDecision();
    }
}

void TreeWalker::tightenValueEstimateGap(double relative_gap) {
    if (current_node_ == nullptr) { return; }
    current_node_->convertToFullUpToGap(relative_gap * reference_round_.bet_unit);
}

bool TreeWalker::needCard() const {
    if (finished()) { return false; }
    if (is_split_pending_) { return true; }
    // cannot use needCard() here because if player actins are done
    // round needs dealer card - not what we want in this case
    return current_node_->bj_round_.getStage() == BJStage::PLAYER_CARD;
}


bool TreeWalker::needPlayerAction() const {
    if (finished()) { return false; }
    return current_node_->bj_round_.needPlayerAction();
}


bool TreeWalker::needDealerAction() const {
    if (finished()) { return false; }
    return current_node_->bj_round_.needDealerAction();
}


bool TreeWalker::finished() const {
    return current_node_ == nullptr;
}


vector<PlayerAction> TreeWalker::getAvailablePlayerActions() const {
    return current_node_->bj_round_.getAvailableActions();
}


vector<DealerAction> TreeWalker::getAvailableDealerActions() const {
    BJStage stage = current_node_->bj_round_.getStage();
    if (stage == BJStage::DEALER_CHECK_BJ) {
        return { DealerAction::CONFIRM_BLACKJACK, DealerAction::CONFIRM_NO_BLACKJACK };
    } else {
        return {};
    }
}


vector<int> TreeWalker::getPossibleNextCardRanks() const {
    auto possible_ranks_opt = current_node_->bj_round_.getPossibleNextCardRanks();
    if (!possible_ranks_opt.has_value()) {
        return {2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    } else {
        return possible_ranks_opt.value();
    }
}

void TreeWalker::takeCard(int card_value) {
    if (!needCard()) {
        throw runtime_error("TreeWalker: Not expecting a card at current stage");
    }

    // Update common shoe
    common_shoe_.burnRankValue(card_value);

    // Update reference round
    reference_round_.takeCard(card_value);

    // If we have a split hand in stack and this is a player card, set the second card
    if (is_split_pending_) { 
        // old tree is no longer relevant, build new tree and return
        // instead of getting a child of the old tree
        handleSplitCard(card_value); 
        return;
    }

    // Find child corresponding to this card
    TransitionEvent card_event = card_value;
    moveToChild(card_event);
}


void TreeWalker::handleSplitCard(int card_value) {
    bool got_all_cards = false;
    size_t last_index = split_hand_rounds_stack_.size() - 1;
    // cannot use needCard() here because if player has 21, round needs dealer card - not what we want
    if (split_hand_rounds_stack_[last_index].getStage() == BJStage::PLAYER_CARD) {
        // round to be player first is at the top of the stack
        // it receives the card before the second round object in the stack
        split_hand_rounds_stack_[last_index].finalizeFakeSplit(card_value);

    } else if (split_hand_rounds_stack_[last_index - 1].getStage() == BJStage::PLAYER_CARD) {
        split_hand_rounds_stack_[last_index - 1].finalizeFakeSplit(card_value);
        got_all_cards = true;

    } else {
        throw runtime_error("TreeWalker: Split hand already has two cards");
    }

    if (got_all_cards) {
        buildSplitRound();
        is_split_pending_ = false;
    }
}

void TreeWalker::buildSplitRound() {
    // Create new round with the card for current round
    BJRound new_round = split_hand_rounds_stack_.back();
    split_hand_rounds_stack_.pop_back();
    
    if (new_round.getStage() != BJStage::PLAYER_ACTION) {
        // the split hand is 21 and is standing automatically
        if (!split_hand_rounds_stack_.empty()) {
            // we need to build the next split round
            buildSplitRound();
        } else {
            current_node_ = nullptr;
        }
        return;
    }
    
    shared_ptr<AbstractFloorCeilNode> new_root = buildNonFinalRootNode(
        new_round,
        common_shoe_,
        max_hand_size_full_enum_,
        dealer_sim_depth_,
        sim_algo_
    );

    current_node_ = new_root;
    current_node_->buildTree();
    convertToFullUpToDecision();
}



void TreeWalker::takePlayerAction(PlayerAction action) {
    if (!needPlayerAction()) {
        throw runtime_error("TreeWalker: Not expecting a player action at current stage");
    }

    // Update reference round
    reference_round_.takeAction(action);

    if (action == PlayerAction::SPLIT) {
        handleSplitAction();
        // current tree is no longer relevant, return
        return;
    }

    moveToChild(action);
}


void TreeWalker::takeDealerAction(DealerAction action) {
    if (!needDealerAction()) {
        throw runtime_error("TreeWalker: Not expecting a dealer action at current stage");
    }
    
    // Update reference round
    reference_round_.takeAction(action);
    
    moveToChild(action);
}


void TreeWalker::moveToChild(TransitionEvent event) {
    shared_ptr<AbstractBJTreeNode> child = findChildByEvent(event);
    if (!child) {
        throw runtime_error("TreeWalker: Event " + to_string(event) + " not found in children");
    }
    current_node_ = dynamic_pointer_cast<AbstractFloorCeilNode>(child);
    if (current_node_ == nullptr && !split_hand_rounds_stack_.empty()) {
        buildSplitRound();
    } else if (current_node_ != nullptr) {
        convertToFullUpToDecision();
    }
}


optional<PlayerAction> TreeWalker::getBestAction() const {
    shared_ptr<DecisionNode> current_node_decision_cast = \
        dynamic_pointer_cast<DecisionNode>(current_node_);
    if (current_node_decision_cast == nullptr) {
        return nullopt;
    }
    
    return current_node_decision_cast->getDecisionChoice();
}


vector<pair<PlayerAction, ValueEstimate>> TreeWalker::getBestActions() const {
    // Cast to DecisionNode to get possible actions
    auto decision_node = dynamic_pointer_cast<DecisionNode>(current_node_);
    if (!decision_node) { return {}; }

    // Get possible actions and their values
    vector<pair<PlayerAction, ValueEstimate>> action_values;

    for (const auto& action_event : decision_node->children_events_) {
        ValueEstimate estimate = getEventValueEstimate(action_event);
        PlayerAction action = get<PlayerAction>(action_event);
        action_values.emplace_back(action, estimate);
    }

    // Sort by value (best to worst) - descending order
    sort(
        action_values.begin(), action_values.end(),
        [](const pair<PlayerAction, ValueEstimate>& a, const pair<PlayerAction, ValueEstimate>& b) {
             return a.second.ev > b.second.ev; // Descending order
        }
    );
    
    return action_values;
}

ValueEstimate TreeWalker::getValueEstimate() const {
    ValueEstimate estimate;
    if (current_node_ == nullptr) { return estimate; }
    estimate.ev = current_node_->getValue();
    estimate.ev_min = current_node_->getFloorValue();
    estimate.ev_max = current_node_->getCeilValue();
    return estimate;    
}

ValueEstimate TreeWalker::getEventValueEstimate(const TransitionEvent& event) const {
    ValueEstimate estimate;
    if (current_node_ == nullptr) { return estimate; }
    shared_ptr<AbstractBJTreeNode> child = findChildByEvent(event);
    if (!child) {
        throw runtime_error("TreeWalker: Event " + to_string(event) + " not found in children");
    }
    estimate.ev = child->getValue();
    estimate.ev_min = child->getFloorValue();
    estimate.ev_max = child->getCeilValue();
    return estimate;
}

string TreeWalker::getStateInfo() const {
    ostringstream oss;
    oss << "TreeWalker State:\n";
    oss << "  Reference round:\n";
    oss << reference_round_.toString() << "\n";
    
    // Determine expected action using needX methods (not node stage which may be fake split)
    string expected = "none";
    if (needCard()) expected = "card";
    else if (needPlayerAction()) expected = "player_action";
    else if (needDealerAction()) expected = "dealer_action";
    oss << "  Expected: " << expected << "\n";
    
    // Shoe counts
    bool compact = true;
    oss << "  Shoe: " << common_shoe_.toStringCount(compact) << "\n";
    
    if (current_node_ == nullptr) {
        oss << "  Round finished (no current node)\n";
        return oss.str();
    }
    
    double ceil_value = current_node_->getCeilValue();
    double floor_value = current_node_->getFloorValue();
    double node_value = current_node_->getValue();
    oss << "  Node value: " << fixed << setprecision(6) << node_value 
        << " [" << floor_value << ", " << ceil_value << "]\n";
    oss << "  Children built: " << (current_node_->has_built_children_ ? "yes" : "no") << "\n";
    
    // Iterate over children/transition events and print their values
    if (current_node_->has_built_children_ && !current_node_->children_events_.empty()) {
        oss << "  Children events and values:\n";
        for (size_t i = 0; i < current_node_->children_events_.size() && i < current_node_->children_.size(); ++i) {
            const TransitionEvent& event = current_node_->children_events_[i];

            shared_ptr<AbstractBJTreeNode> child = current_node_->children_[i];
            if (child) {
                double child_value;
                child_value = child->getValue();                
                double child_ceil = child->getCeilValue();
                double child_floor = child->getFloorValue();
                oss << "    " << to_string(event) << ": " << fixed << setprecision(6) << child_value 
                    << " [" << child_floor << ", " << child_ceil << "]\n";
            }
        }
    }
    
    return oss.str();
}


shared_ptr<AbstractBJTreeNode> TreeWalker::findChildByEvent(const TransitionEvent& event) const {
    shared_ptr<AbstractBJTreeNode> child = nullptr;
    for (size_t i = 0; i < current_node_->children_events_.size(); ++i) {
        const TransitionEvent& child_event = current_node_->children_events_[i];
        
        // Compare events - need to check type and value
        if (holds_alternative<int>(event) && holds_alternative<int>(child_event)) {
            if (get<int>(event) == get<int>(child_event)) {
                child = current_node_->children_[i];
            }
        } else if (holds_alternative<PlayerAction>(event) && holds_alternative<PlayerAction>(child_event)) {
            if (get<PlayerAction>(event) == get<PlayerAction>(child_event)) {
                child = current_node_->children_[i];
            }
        } else if (holds_alternative<DealerAction>(event) && holds_alternative<DealerAction>(child_event)) {
            if (get<DealerAction>(event) == get<DealerAction>(child_event)) {
                child = current_node_->children_[i];
            }
        }
    }
    return child;
}


void TreeWalker::handleSplitAction() {        
    BJRound pre_split_round = current_node_->bj_round_;
    vector<PlayerAction> available_actions = pre_split_round.getAvailableActions();
    if (!ranges::contains(available_actions, PlayerAction::SPLIT)) {
        throw runtime_error("TreeWalker: Cannot split at current stage");
    }
    
    pre_split_round.startFakeSplit();
    split_hand_rounds_stack_.push_back(pre_split_round);
    split_hand_rounds_stack_.push_back(pre_split_round);
    is_split_pending_ = true;
}

} // namespace blackjack
