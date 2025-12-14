#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <string>
#include <utility>

#include "floor_ceil_node.h"
#include "blackjack_round.h"
#include "shoe.h"
#include "actions.h"

using namespace std;

namespace blackjack {

/**
 * TreeWalker - High-level interface to interact with nodes of the floor_ceil_node family.
 * 
 * This class provides a convenient way to:
 * - Navigate through the game tree by providing cards or actions
 * - Build the tree until a target gap is reached
 * - Get best actions when expecting player decisions
 * - Handle split situations carefully
 */
class TreeWalker {
public:
    /**
     * Constructor
     * @param root_node The root node to start from (must be a FloorCeilNode or subclass)
     * @param target_gap Optional target gap between floor and ceil values. If provided,
     *                   the tree will be built until the gap is tight enough.
     */
    TreeWalker(
        shared_ptr<AbstractBJTreeNode> root_node,
        optional<double> target_gap = nullopt
    );

    /**
     * Get the current node
     */
    shared_ptr<AbstractBJTreeNode> getCurrentNode() const { return current_node_; }

    /**
     * Get the current round state
     */
    const BJRound& getCurrentRound() const { return current_node_->bj_round_; }

    /**
     * Get the current shoe state
     */
    const ProbabilisticRankShoe& getCurrentShoe() const { return current_node_->shoe_; }

    /**
     * Check if we're expecting a card next (player or dealer)
     */
    bool expectsCard() const;

    /**
     * Check if we're expecting a player action next
     */
    bool expectsPlayerAction() const;

    /**
     * Check if we're expecting a dealer action next
     */
    bool expectsDealerAction() const;

    /**
     * Provide the next card and navigate to the corresponding child node.
     * @param card_value The card value (2-11)
     * @throws runtime_error if not expecting a card or if the card is invalid
     */
    void provideCard(int card_value);

    /**
     * Provide the next player action and navigate to the corresponding child node.
     * @param action The player action
     * @throws runtime_error if not expecting a player action or if the action is invalid
     */
    void providePlayerAction(PlayerAction action);

    /**
     * Provide the next dealer action and navigate to the corresponding child node.
     * @param action The dealer action
     * @throws runtime_error if not expecting a dealer action or if the action is invalid
     */
    void provideDealerAction(DealerAction action);

    /**
     * Get the best actions when expecting a player action.
     * Returns a vector of (action, value) pairs, sorted from best to worst.
     * @throws runtime_error if not expecting a player action
     */
    vector<pair<PlayerAction, double>> getBestActions() const;

    /**
     * Build the tree until the gap between floor and ceil is tight enough.
     * This will recursively build layers until the gap at the current node
     * is less than or equal to the target gap.
     * @param max_depth Maximum depth to build (safety limit)
     * @returns true if gap target was reached, false if max_depth was hit
     */
    bool buildUntilGapTight(int max_depth = 100);

    /**
     * Get the current gap (ceil - floor) at the current node.
     * Returns 0.0 if the node doesn't support floor/ceil values.
     */
    double getCurrentGap() const;

    /**
     * Check if the current gap is tight enough (<= target_gap_)
     */
    bool isGapTightEnough() const;

    /**
     * Get information about the current state
     */
    string getStateInfo() const;

private:
    shared_ptr<AbstractBJTreeNode> root_node_;
    shared_ptr<AbstractBJTreeNode> current_node_;
    optional<double> target_gap_;

    // Split tracking state
    struct SplitState {
        int first_hand_idx;
        int second_hand_idx;
        int second_hand_initial_card;  // The card that goes to the second hand
        vector<int> first_hand_cards;  // Cards used in the first hand
        shared_ptr<AbstractBJTreeNode> first_hand_tree_root;
        shared_ptr<AbstractBJTreeNode> second_hand_tree_root;
        bool first_hand_complete;
        ProbabilisticRankShoe original_shoe;  // Shoe before split
    };
    optional<SplitState> split_state_;

    /**
     * Find child node corresponding to a given event
     */
    shared_ptr<AbstractBJTreeNode> findChildByEvent(const TransitionEvent& event) const;

    /**
     * Navigate to a child node
     */
    void navigateToChild(shared_ptr<AbstractBJTreeNode> child);

    /**
     * Build tree layer and check gap
     */
    void buildLayerAndCheckGap();

    /**
     * Handle split situation - initialize split state and build first hand tree
     */
    void handleSplit(shared_ptr<SplitNode> split_node);

    /**
     * Extract cards used in a hand from the round state
     */
    vector<int> extractHandCards(const BJRound& round, int hand_idx) const;

    /**
     * Build a complete tree for the first split hand
     */
    void buildFirstSplitHandTree();

    /**
     * Build a complete tree for the second split hand
     */
    void buildSecondSplitHandTree();

    /**
     * Check if we're currently navigating through a split hand tree
     */
    bool isInSplitHandTree() const;

    /**
     * Get value from a node (handles different node types)
     */
    double getNodeValue(AbstractBJTreeNode* node) const;

    /**
     * Get floor value from a node (handles different node types)
     */
    double getNodeFloorValue(AbstractBJTreeNode* node) const;

    /**
     * Get ceil value from a node (handles different node types)
     */
    double getNodeCeilValue(AbstractBJTreeNode* node) const;
};

} // namespace blackjack
