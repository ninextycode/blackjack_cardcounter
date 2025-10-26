#pragma once

#include "abstract_node.h"
#include "simulation_result_node.h"
#include <deque>

namespace blackjack {

class MixedNode : public AbstractBJTreeNode {
public:
    MixedNode(
        const BJRound &bj_round,
        const ProbabilisticRankShoe &shoe,
        int hand_size_full_enum_limit,
        int player_card_initial_samples = 1,
        size_t n_dealer_sim_runs = 100,
        AbstractBJTreeNode *parent = nullptr,
        bool copy_data = true
    );

    // Override createChild
    void createChild(
        const BJRound &child_bj_round,
        const ProbabilisticRankShoe &child_shoe,
        const TransitionEvent &event,
        double prob = 0.0
    ) override;

    // MixedNode-specific methods
    bool isPastThreeInitialCards() const;
    bool addPlayerCardSample();
    bool convertToFullNextLayer();
    bool singleNodeFromSampleToFull();
    bool convertToFullUpToDepth(int depth);

    // Configuration
    int hand_size_full_enum_limit_;
    size_t n_dealer_sim_runs_;
    int player_card_initial_samples_;

protected:
    void buildChildrenPlayerCard() override;
    void buildChildrenDealerCard() override;

private:
    // Helper methods
    void buildFullChildrenPlayerCard();
    void buildChildDealerBlackjack();
    void runDealerCardsSimulations();
    bool addPlayerCardSampleImpl();
    bool convertFromSampleToFull();

    // Internal state
    optional<int> active_hand_size_;
};

// Helper function for BFS traversal
struct NodeLevel {
    int level;
    AbstractBJTreeNode* node;
};

class NodeIterator {
public:
    NodeIterator(AbstractBJTreeNode* root);
    bool hasNext();
    NodeLevel next();

private:
    deque<NodeLevel> queue_;
};

} // namespace blackjack
