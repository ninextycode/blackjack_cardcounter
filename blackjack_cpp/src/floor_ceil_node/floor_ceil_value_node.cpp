#include "floor_ceil_node.h"

namespace blackjack {

FloorCeilValueNode::FloorCeilValueNode(double floor_value, double ceil_value, AbstractBJTreeNode* parent)
    : AbstractBJTreeNode(
        (parent != nullptr ? parent->bj_round_ : BJRound(shared_ptr<const BJRules>())),
        (parent != nullptr ? parent->shoe_ : ProbabilisticRankShoe()),
        parent
    ),
    floor_value_(floor_value),
    ceil_value_(ceil_value)
{
    children_.clear();
    has_completed_tree_ = true;
    has_built_children_ = true;
}

void FloorCeilValueNode::createChild(
    const BJRound&,
    const ProbabilisticRankShoe&,
    const TransitionEvent&,
    double
) {
    // Do nothing - FloorCeilValueNode has no children
}

void FloorCeilValueNode::buildTreeLayer(optional<int>) {
    // Do nothing - already complete
}

bool FloorCeilValueNode::treeCompleted() const {
    return true;
}

bool FloorCeilValueNode::childrenTreesCompleted() const {
    return true;
}

double FloorCeilValueNode::getValue() const {
    throw runtime_error("FloorCeilValueNode does not have a single value");
}

double FloorCeilValueNode::getCeilValue() const {
    return ceil_value_;
}

double FloorCeilValueNode::getFloorValue() const {
    return floor_value_;
}

void FloorCeilValueNode::buildChildrenPlayerCard() {
    // Do nothing
}

void FloorCeilValueNode::buildChildrenDealerCard() {
    // Do nothing
}

} // namespace blackjack
