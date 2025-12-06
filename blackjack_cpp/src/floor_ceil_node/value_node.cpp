#include "floor_ceil_node.h"

namespace blackjack {

ValueNode::ValueNode(double value, AbstractBJTreeNode* parent)
    : AbstractBJTreeNode(
        (parent != nullptr ? parent->bj_round_ : BJRound(shared_ptr<const BJRules>())),
        (parent != nullptr ? parent->shoe_ : ProbabilisticRankShoe()),
        parent
    )
{
    value_ = value;
    children_.clear();
    has_completed_tree_ = true;
    has_built_children_ = true;
}

void ValueNode::createChild(
    const BJRound&,
    const ProbabilisticRankShoe&,
    const TransitionEvent&,
    double
) {
    // Do nothing - ValueNode has no children
}

void ValueNode::buildTreeLayer(optional<int>) {
    // Do nothing - already complete
}

bool ValueNode::treeCompleted() const {
    return true;
}

bool ValueNode::childrenTreesCompleted() const {
    return true;
}

double ValueNode::getValue() const {
    return value_;
}

double ValueNode::getCeilValue() const {
    return value_;
}

double ValueNode::getFloorValue() const {
    return value_;
}

void ValueNode::buildChildrenPlayerCard() {
    // Do nothing
}

void ValueNode::buildChildrenDealerCard() {
    // Do nothing
}

} // namespace blackjack
