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
    throw runtime_error("ValueNode is a terminal node and cannot have children");
}

void ValueNode::buildTreeLayer(optional<int>) {
    // do nothing - terminal node
}

void ValueNode::buildChildren() {
    // do nothing - terminal node
}

void ValueNode::rebuildChildren() {
    // do nothing - terminal node
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
    throw runtime_error("ValueNode is a terminal node and cannot build children");
}

void ValueNode::buildChildrenDealerCard() {
    throw runtime_error("ValueNode is a terminal node and cannot build children");
}

} // namespace blackjack
