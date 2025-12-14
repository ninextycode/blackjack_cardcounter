#include "tree_walker.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>

using namespace std;

namespace blackjack {

TreeWalker::TreeWalker(
    shared_ptr<AbstractBJTreeNode> root_node,
    optional<double> target_gap
) : root_node_(root_node), current_node_(root_node), target_gap_(target_gap) {
    if (!root_node_) {
        throw runtime_error("TreeWalker: root_node cannot be null");
    }
}

bool TreeWalker::expectsCard() const {
    BJStage stage = current_node_->bj_round_.getStage();
    return stage == BJStage::PLAYER_CARD || stage == BJStage::DEALER_CARD;
}

bool TreeWalker::expectsPlayerAction() const {
    BJStage stage = current_node_->bj_round_.getStage();
    return stage == BJStage::PLAYER_ACTION ||
           stage == BJStage::PLAYER_OFFERED_INSURANCE ||
           stage == BJStage::PLAYER_OFFERED_EARLY_SURRENDER;
}

bool TreeWalker::expectsDealerAction() const {
    BJStage stage = current_node_->bj_round_.getStage();
    return stage == BJStage::DEALER_CHECK_BJ;
}

void TreeWalker::provideCard(int card_value) {
    if (!expectsCard()) {
        throw runtime_error("TreeWalker: Not expecting a card at current stage");
    }

    // If we're in a split and playing the first hand, track the card
    if (split_state_.has_value() && !split_state_->first_hand_complete) {
        const BJRound& round = current_node_->bj_round_;
        if (round.active_hand_idx == split_state_->first_hand_idx) {
            // This card is being added to the first hand
            split_state_->first_hand_cards.push_back(card_value);
        }
    }

    // Build children if not already built
    if (!current_node_->has_built_children_) {
        current_node_->buildChildren();
    }

    // Find child corresponding to this card
    TransitionEvent card_event = card_value;
    shared_ptr<AbstractBJTreeNode> child = findChildByEvent(card_event);

    if (!child) {
        // Card might not be in children yet - try building more
        current_node_->buildTreeLayer(nullopt);
        child = findChildByEvent(card_event);
        
        if (!child) {
            throw runtime_error("TreeWalker: Card " + std::to_string(card_value) + " not found in children");
        }
    }

    navigateToChild(child);
}

void TreeWalker::providePlayerAction(PlayerAction action) {
    if (!expectsPlayerAction()) {
        throw runtime_error("TreeWalker: Not expecting a player action at current stage");
    }

    // Special handling: if we're in a split and the first hand just finished
    if (split_state_.has_value() && !split_state_->first_hand_complete) {
        // Check if this action completes the first hand
        const BJRound& round = current_node_->bj_round_;
        if (round.active_hand_idx == split_state_->first_hand_idx) {
            // Update first hand cards
            split_state_->first_hand_cards = extractHandCards(round, split_state_->first_hand_idx);
        }
    }

    // Build children if not already built
    if (!current_node_->has_built_children_) {
        current_node_->buildChildren();
    }

    // Find child corresponding to this action
    TransitionEvent action_event = action;
    shared_ptr<AbstractBJTreeNode> child = findChildByEvent(action_event);

    if (!child) {
        throw runtime_error("TreeWalker: Player action not found in children");
    }

    // Special handling for split - only if we're not already in a split
    if (!split_state_.has_value()) {
        auto split_node = dynamic_pointer_cast<SplitNode>(child);
        if (split_node) {
            handleSplit(split_node);
            return; // handleSplit already navigated
        }
    }

    navigateToChild(child);
    
    // Check if first hand is complete and we need to switch to second hand
    if (split_state_.has_value() && !split_state_->first_hand_complete) {
        const BJRound& round = current_node_->bj_round_;
        // Check if we've moved to the second hand or dealer stage
        if (round.active_hand_idx == split_state_->second_hand_idx || 
            round.getStage() == BJStage::DEALER_CARD) {
            // First hand is complete, build second hand tree
            buildSecondSplitHandTree();
            split_state_->first_hand_complete = true;
            current_node_ = split_state_->second_hand_tree_root;
        }
    }
}

void TreeWalker::provideDealerAction(DealerAction action) {
    if (!expectsDealerAction()) {
        throw runtime_error("TreeWalker: Not expecting a dealer action at current stage");
    }

    // Build children if not already built
    if (!current_node_->has_built_children_) {
        current_node_->buildChildren();
    }

    // Find child corresponding to this action
    TransitionEvent action_event = action;
    shared_ptr<AbstractBJTreeNode> child = findChildByEvent(action_event);

    if (!child) {
        throw runtime_error("TreeWalker: Dealer action not found in children");
    }

    navigateToChild(child);
}

vector<pair<PlayerAction, double>> TreeWalker::getBestActions() const {
    if (!expectsPlayerAction()) {
        throw runtime_error("TreeWalker: Not expecting a player action at current stage");
    }

    // Cast to DecisionNode to get possible actions
    auto decision_node = dynamic_pointer_cast<DecisionNode>(current_node_);
    if (!decision_node) {
        throw runtime_error("TreeWalker: Current node is not a DecisionNode");
    }

    // Build children if not already built
    if (!current_node_->has_built_children_) {
        current_node_->buildChildren();
    }

    // Get possible actions and their values
    vector<pair<PlayerAction, double>> action_values;

    for (const auto& action : decision_node->possible_actions_) {
        TransitionEvent action_event = action;
        shared_ptr<AbstractBJTreeNode> child = findChildByEvent(action_event);
        
        if (child) {
            double value = getNodeValue(child.get());
            action_values.push_back({action, value});
        }
    }

    // Sort by value (best to worst) - descending order
    sort(action_values.begin(), action_values.end(),
         [](const pair<PlayerAction, double>& a, const pair<PlayerAction, double>& b) {
             return a.second > b.second; // Descending order
         });
    
    return action_values;
}

bool TreeWalker::buildUntilGapTight(int max_depth) {
    if (!target_gap_.has_value()) {
        return true; // No target gap specified
    }

    int depth = 0;
    while (depth < max_depth) {
        double current_gap = getCurrentGap();
        if (current_gap <= target_gap_.value()) {
            return true; // Gap is tight enough
        }

        // Build next layer
        buildLayerAndCheckGap();
        depth++;
    }

    return false; // Max depth reached
}

double TreeWalker::getCurrentGap() const {
    FloorCeilNode* fc_node = dynamic_cast<FloorCeilNode*>(current_node_.get());
    if (fc_node) {
        double floor = getNodeFloorValue(current_node_.get());
        double ceil = getNodeCeilValue(current_node_.get());
        return ceil - floor;
    }

    ValueNode* v_node = dynamic_cast<ValueNode*>(current_node_.get());
    if (v_node) {
        double floor = getNodeFloorValue(current_node_.get());
        double ceil = getNodeCeilValue(current_node_.get());
        return ceil - floor;
    }

    FloorCeilValueNode* fcv_node = dynamic_cast<FloorCeilValueNode*>(current_node_.get());
    if (fcv_node) {
        double floor = getNodeFloorValue(current_node_.get());
        double ceil = getNodeCeilValue(current_node_.get());
        return ceil - floor;
    }

    return 0.0; // Node doesn't support floor/ceil
}

bool TreeWalker::isGapTightEnough() const {
    if (!target_gap_.has_value()) {
        return true; // No target gap specified
    }
    return getCurrentGap() <= target_gap_.value();
}

string TreeWalker::getStateInfo() const {
    ostringstream oss;
    oss << "TreeWalker State:\n";
    oss << "  Current stage: " << static_cast<int>(current_node_->bj_round_.getStage()) << "\n";
    oss << "  Expects card: " << (expectsCard() ? "yes" : "no") << "\n";
    oss << "  Expects player action: " << (expectsPlayerAction() ? "yes" : "no") << "\n";
    oss << "  Expects dealer action: " << (expectsDealerAction() ? "yes" : "no") << "\n";
    
    double gap = getCurrentGap();
    oss << "  Current gap: " << fixed << setprecision(6) << gap << "\n";
    if (target_gap_.has_value()) {
        oss << "  Target gap: " << fixed << setprecision(6) << target_gap_.value() << "\n";
        oss << "  Gap tight enough: " << (isGapTightEnough() ? "yes" : "no") << "\n";
    }
    
    oss << "  Node value: " << fixed << setprecision(6) << current_node_->getValue() << "\n";
    oss << "  Children built: " << (current_node_->has_built_children_ ? "yes" : "no") << "\n";
    oss << "  Number of children: " << current_node_->children_.size() << "\n";
    
    return oss.str();
}

shared_ptr<AbstractBJTreeNode> TreeWalker::findChildByEvent(const TransitionEvent& event) const {
    for (size_t i = 0; i < current_node_->children_events_.size(); ++i) {
        const TransitionEvent& child_event = current_node_->children_events_[i];
        
        // Compare events - need to check type and value
        if (holds_alternative<int>(event) && holds_alternative<int>(child_event)) {
            if (get<int>(event) == get<int>(child_event)) {
                return current_node_->children_[i];
            }
        } else if (holds_alternative<PlayerAction>(event) && holds_alternative<PlayerAction>(child_event)) {
            if (get<PlayerAction>(event) == get<PlayerAction>(child_event)) {
                return current_node_->children_[i];
            }
        } else if (holds_alternative<DealerAction>(event) && holds_alternative<DealerAction>(child_event)) {
            if (get<DealerAction>(event) == get<DealerAction>(child_event)) {
                return current_node_->children_[i];
            }
        }
    }
    return nullptr;
}

void TreeWalker::navigateToChild(shared_ptr<AbstractBJTreeNode> child) {
    current_node_ = child;
    
    // If target gap is specified, try to build until gap is tight
    if (target_gap_.has_value() && !isGapTightEnough()) {
        buildUntilGapTight();
    }
}

void TreeWalker::buildLayerAndCheckGap() {
    if (!current_node_->has_built_children_) {
        current_node_->buildChildren();
    } else {
        current_node_->buildTreeLayer(nullopt);
    }
    
    // Recompute values after building
    current_node_->recomputeTreeValue();
}

void TreeWalker::handleSplit(shared_ptr<SplitNode> split_node) {
    // Extract split information from the round state
    // Note: SplitNode has already modified the round by adding a placeholder card (value 2) to the first hand
    const BJRound& round = split_node->bj_round_;
    
    if (!round.split_origin_idx.has_value()) {
        throw runtime_error("TreeWalker: SplitNode does not have split_origin_idx set");
    }
    
    int first_hand_idx = round.split_origin_idx.value();
    int second_hand_idx = first_hand_idx + 1;
    
    if (first_hand_idx >= (int)round.player_hands.size() || 
        second_hand_idx >= (int)round.player_hands.size()) {
        throw runtime_error("TreeWalker: Invalid split hand indices");
    }
    
    // Get the hands - first hand has original card + placeholder (value 2), second hand has one card
    const ValueOnlyHand& first_hand_with_placeholder = round.player_hands[first_hand_idx];
    const ValueOnlyHand& second_hand = round.player_hands[second_hand_idx];
    
    if (second_hand.size() != 1) {
        throw runtime_error("TreeWalker: Second split hand should have exactly one card");
    }
    
    // The first hand's original card is the first value (before placeholder)
    // The placeholder is the last value (2)
    if (first_hand_with_placeholder.size() < 2) {
        throw runtime_error("TreeWalker: First split hand should have at least 2 cards (original + placeholder)");
    }
    
    int first_hand_original_card = first_hand_with_placeholder.values()[0];
    int second_hand_initial_card = second_hand.values()[0];
    
    // Initialize split state
    SplitState split_state;
    split_state.first_hand_idx = first_hand_idx;
    split_state.second_hand_idx = second_hand_idx;
    split_state.second_hand_initial_card = second_hand_initial_card;
    split_state.original_shoe = split_node->shoe_;
    split_state.first_hand_complete = false;
    
    // Build tree for first hand with second hand's initial card removed from shoe
    ProbabilisticRankShoe first_hand_shoe = split_node->shoe_;
    first_hand_shoe.burnRankValue(second_hand_initial_card);
    
    // Create a round state for the first hand (without the placeholder)
    BJRound first_hand_round = round.copy();
    first_hand_round.active_hand_idx = first_hand_idx;
    // Remove the placeholder card (value 2) from the first hand
    ValueOnlyHand first_hand_clean = first_hand_round.player_hands[first_hand_idx];
    // The placeholder is always the last card added, so we need to reconstruct the hand
    // Actually, we can't easily remove it, so we'll work with what we have
    // The first hand in the round already has the placeholder, but we'll ignore it in our tree
    
    // Actually, let's create a clean round state for the first hand
    // We need to go back to before the placeholder was added
    // The first hand should only have its original card
    first_hand_round.player_hands[first_hand_idx] = ValueOnlyHand(true);
    first_hand_round.player_hands[first_hand_idx].add_value(first_hand_original_card);
    first_hand_round.hand_bets[first_hand_idx] = round.hand_bets[first_hand_idx]; // Restore original bet
    first_hand_round.is_hand_in_progress[first_hand_idx] = true;
    
    // Build root node for first hand tree
    split_state.first_hand_tree_root = buildRootNode(
        first_hand_round,
        first_hand_shoe,
        split_node->max_hand_size_full_enum_,
        split_node->dealer_sim_depth_,
        split_node->sim_algo_
    );
    
    // Initialize first hand cards with just the original card
    split_state.first_hand_cards = {first_hand_original_card};
    
    split_state_ = split_state;
    
    // Navigate to the first hand tree
    current_node_ = split_state.first_hand_tree_root;
}

double TreeWalker::getNodeValue(AbstractBJTreeNode* node) const {
    return node->getValue();
}

double TreeWalker::getNodeFloorValue(AbstractBJTreeNode* node) const {
    FloorCeilNode* fc_node = dynamic_cast<FloorCeilNode*>(node);
    if (fc_node) {
        return fc_node->getFloorValue();
    }

    ValueNode* v_node = dynamic_cast<ValueNode*>(node);
    if (v_node) {
        return v_node->getFloorValue();
    }

    FloorCeilValueNode* fcv_node = dynamic_cast<FloorCeilValueNode*>(node);
    if (fcv_node) {
        return fcv_node->getFloorValue();
    }

    // Fallback to regular value
    return node->getValue();
}

double TreeWalker::getNodeCeilValue(AbstractBJTreeNode* node) const {
    FloorCeilNode* fc_node = dynamic_cast<FloorCeilNode*>(node);
    if (fc_node) {
        return fc_node->getCeilValue();
    }

    ValueNode* v_node = dynamic_cast<ValueNode*>(node);
    if (v_node) {
        return v_node->getCeilValue();
    }

    FloorCeilValueNode* fcv_node = dynamic_cast<FloorCeilValueNode*>(node);
    if (fcv_node) {
        return fcv_node->getCeilValue();
    }

    // Fallback to regular value
    return node->getValue();
}

vector<int> TreeWalker::extractHandCards(const BJRound& round, int hand_idx) const {
    if (hand_idx >= (int)round.player_hands.size()) {
        return {};
    }
    const ValueOnlyHand& hand = round.player_hands[hand_idx];
    return hand.values();
}

void TreeWalker::buildFirstSplitHandTree() {
    if (!split_state_.has_value()) {
        throw runtime_error("TreeWalker: buildFirstSplitHandTree called without split state");
    }
    
    // First hand tree should already be built in handleSplit
    // This is a placeholder for future expansion if needed
}

void TreeWalker::buildSecondSplitHandTree() {
    if (!split_state_.has_value()) {
        throw runtime_error("TreeWalker: buildSecondSplitHandTree called without split state");
    }
    
    // Build shoe with all cards from first hand removed
    ProbabilisticRankShoe second_hand_shoe = split_state_->original_shoe;
    
    // Remove second hand's initial card (already accounted in original split)
    second_hand_shoe.burnRankValue(split_state_->second_hand_initial_card);
    
    // Remove all cards used in the first hand
    for (int card : split_state_->first_hand_cards) {
        second_hand_shoe.burnRankValue(card);
    }
    
    // Create round state for the second hand
    // We need to get the current round state and modify it for the second hand
    const BJRound& current_round = current_node_->bj_round_;
    BJRound second_hand_round = current_round.copy();
    second_hand_round.active_hand_idx = split_state_->second_hand_idx;
    
    // Find SplitNode to get its parameters - look in parent chain
    AbstractBJTreeNode* node = current_node_->parent_;
    SplitNode* split_node_ptr = nullptr;
    while (node) {
        split_node_ptr = dynamic_cast<SplitNode*>(node);
        if (split_node_ptr) break;
        node = node->parent_;
    }
    
    if (!split_node_ptr) {
        throw runtime_error("TreeWalker: Cannot find SplitNode parent for second hand tree");
    }
    
    split_state_->second_hand_tree_root = buildRootNode(
        second_hand_round,
        second_hand_shoe,
        split_node_ptr->max_hand_size_full_enum_,
        split_node_ptr->dealer_sim_depth_,
        split_node_ptr->sim_algo_
    );
}

bool TreeWalker::isInSplitHandTree() const {
    return split_state_.has_value();
}

} // namespace blackjack
