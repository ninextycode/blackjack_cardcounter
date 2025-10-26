#include "simulation_result_node.h"

namespace blackjack {

SimulationResultNode::SimulationResultNode(double value, AbstractBJTreeNode *parent)
    : AbstractBJTreeNode(
        (parent != nullptr ? parent->bj_round_ : BJRound(nullptr)),
        (parent != nullptr ? parent->shoe_ : ProbabilisticRankShoe()),
        parent,
        false
    )
{
    value_ = value;
    children_.clear();
    // Mark as completed immediately
    has_completed_tree_ = true;
    has_built_children_ = true;
}

void SimulationResultNode::createChild(
    const BJRound &,
    const ProbabilisticRankShoe &,
    const TransitionEvent &,
    double
) {
    // Do nothing
}

void SimulationResultNode::buildTreeLayer(optional<int>) {
    // Do nothing
}

bool SimulationResultNode::treeCompleted() const {
    return true;
}

bool SimulationResultNode::childrenTreesCompleted() const {
    return true;
}

double SimulationResultNode::getValue() const {
    return value_;
}

void SimulationResultNode::buildChildrenPlayerCard() {
    // Do nothing
}

void SimulationResultNode::buildChildrenDealerCard() {
    // Do nothing
}

} // namespace blackjack
