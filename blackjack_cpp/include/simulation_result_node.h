#pragma once

#include "abstract_node.h"

namespace blackjack {

class SimulationResultNode : public AbstractBJTreeNode {
public:
    SimulationResultNode(double value, AbstractBJTreeNode *parent = nullptr);

    // Override abstract methods with "do nothing" implementations
    void createChild(
        const BJRound &child_bj_round,
        const ProbabilisticRankShoe &child_shoe,
        const TransitionEvent &event,
        double prob = 0.0
    ) override;

    void buildTreeLayer(optional<int> depth) override;
    bool treeCompleted() const override;
    bool childrenTreesCompleted() const override;
    double getValue() const override;

protected:
    void buildChildrenPlayerCard() override;
    void buildChildrenDealerCard() override;
};

} // namespace blackjack
