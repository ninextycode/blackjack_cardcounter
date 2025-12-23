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


struct ValueEstimate {
    double ev;
    double ev_min;
    double ev_max;
};

/**
 * TreeWalker - High-level interface to interact with nodes of the floor_ceil_node family.
 * 
 * This class provides a convenient way to:
 * - Navigate through the game tree by providing cards or actions
 * - Build the tree until the best action is found
 * - Get best actions when expecting player decisions
 * - Handle split situations carefully
 */
class TreeWalker {
public:
    static TreeWalker buildInitialTreeWalker(
        pair<int, int> player_cards,
        int dealer_card,
        const BJRules& rules,
        RankCount shoe_rank_count,
        int max_hand_size_full_enum = 0,
        int dealer_sim_depth = 9,
        SimAlgo sim_algo = SimAlgo::COMBO,
        int bet_unit = 100,
        bool initial_cards_burned = true
    );

    /**
     * Constructor
     * @param root_node The root node to start from (must be a FloorCeilNode or subclass)
     */
    TreeWalker(
        shared_ptr<AbstractFloorCeilNode> root_node
    );

    // shared_ptr<AbstractBJTreeNode> getCurrentNode() const { return current_node_; }
    const ProbabilisticRankShoe& getCurrentShoe() const { return current_node_->shoe_; }

    bool needCard() const;
    bool needPlayerAction() const;
    bool needDealerAction() const;
    bool finished() const;

    vector<PlayerAction> getAvailablePlayerActions() const;
    vector<DealerAction> getAvailableDealerActions() const;
    vector<int> getPossibleNextCardRanks() const;

    void takeCard(int card_value);
    void takePlayerAction(PlayerAction action);
    void takeDealerAction(DealerAction action);


    vector<pair<PlayerAction, ValueEstimate>> getBestActions() const;
    optional<PlayerAction> getBestAction() const;
    ValueEstimate getEventValueEstimate(const TransitionEvent& event) const;
    ValueEstimate getValueEstimate() const;

    void tightenValueEstimateGap(double relative_gap);
    
    string getStateInfo() const;

private:
    shared_ptr<AbstractFloorCeilNode> current_node_;

    // Common shoe maintained across all split hands
    ProbabilisticRankShoe common_shoe_;

    // Stack of split hands (each is a pair<int, int> representing the two cards)
    // First card is set when split occurs, second card starts as -1
    // When we provide a card to the current round, we set the second card in the top stack entry
    // When a round finishes, we pop from this stack and create a new round
    vector<BJRound> split_hand_rounds_stack_;

    // To be taken from the root node in constructor
    // so that we can use the same parameters for all split rounds
    int max_hand_size_full_enum_;
    int dealer_sim_depth_;
    SimAlgo sim_algo_;

    // indicates if we are waiting for the second cards for the split hands
    bool is_split_pending_;

    // actual round object, not used in tree walking
    BJRound reference_round_;  

    shared_ptr<AbstractBJTreeNode> findChildByEvent(const TransitionEvent& event) const;
    void moveToChild(TransitionEvent event);

    void convertToFullUpToDecision();

    void handleSplitAction();
    void handleSplitCard(int card_value);
    void buildSplitRound();
};

} // namespace blackjack
