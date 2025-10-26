#include "abstract_node.h"
#include <algorithm>
#include <stdexcept>
#include <numeric>

using namespace std;

namespace blackjack {

AbstractBJTreeNode::AbstractBJTreeNode(
    const BJRound &bj_round,
    const ProbabilisticRankShoe &shoe,
    AbstractBJTreeNode *parent,
    bool copy_data
) :
    bj_round_(copy_data ? bj_round.copy() : bj_round),
    shoe_(copy_data ? shoe.copy() : shoe),
    parent_(parent),
    value_(0.0),
    has_built_children_(false),
    has_completed_tree_(false),
    depth_from_root_(0)
{
    if (parent_ != nullptr) {
        depth_from_root_ = parent_->depth_from_root_ + 1;
    }
}

void AbstractBJTreeNode::setAsRoot() {
    parent_ = nullptr;
    updateDepthFromRoot();
}

void AbstractBJTreeNode::updateDepthFromRoot() {
    if (parent_ == nullptr) {
        depth_from_root_ = 0;
    } else {
        depth_from_root_ = parent_->depth_from_root_ + 1;
    }
    for (auto &child : children_) {
        child->updateDepthFromRoot();
    }
}

bool AbstractBJTreeNode::treeCompleted() const {
    return has_completed_tree_;
}

bool AbstractBJTreeNode::childrenTreesCompleted() const {
    if (!has_built_children_) {
        return false;
    }
    for (const auto &child : children_) {
        if (!child->treeCompleted()) {
            return false;
        }
    }
    return true;
}

void AbstractBJTreeNode::buildTree() {
    buildTreeLayer(nullopt);
}

void AbstractBJTreeNode::recomputeTreeValue() {
    if (!has_built_children_) {
        throw runtime_error("Cannot recompute tree value before building children");
    }

    for (auto &child : children_) {
        child->buildTree();
    }

    if (childrenTreesCompleted()) {
        computeNodeValue();
        has_completed_tree_ = true;
    } else {
        throw runtime_error("Cannot recompute tree value, some children have incomplete trees");
    }
}

void AbstractBJTreeNode::buildTreeLayer(optional<int> depth) {
    if (depth.has_value() && depth.value() == 0) {
        return;
    }
    if (treeCompleted()) {
        return;
    }

    if (!has_built_children_) {
        buildChildren();
    }

    for (auto &child : children_) {
        optional<int> child_depth = nullopt;
        if (depth.has_value()) {
            child_depth = depth.value() - 1;
        }
        child->buildTreeLayer(child_depth);
    }

    if (childrenTreesCompleted()) {
        computeNodeValue();
        has_completed_tree_ = true;
    }
}

void AbstractBJTreeNode::computeNodeValue() {
    BJStage stage = bj_round_.getStage();

    if (stage == BJStage::PLAYER_ACTION ||
        stage == BJStage::PLAYER_OFFERED_EARLY_SURRENDER ||
        stage == BJStage::PLAYER_OFFERED_INSURANCE)
    {
        computeActionNodeValue();
    }
    else if (stage == BJStage::DEALER_CARD ||
             stage == BJStage::PLAYER_CARD ||
             stage == BJStage::DEALER_CHECK_BJ)
    {
        computeChanceNodeValue();
    }
    else if (stage == BJStage::ROUND_OVER) {
        computeTerminalNodeValue();
    }
    else {
        throw runtime_error("Unexpected game stage after building children");
    }
}

void AbstractBJTreeNode::computeActionNodeValue() {
    vector<double> values;
    values.reserve(children_.size());
    for (auto &child : children_) {
        values.push_back(child->getValue());
    }

    auto it = max_element(values.begin(), values.end());
    size_t action_id = distance(values.begin(), it);

    for (size_t i = 0; i < children_prob_.size(); ++i) {
        children_prob_[i] = (i == action_id) ? 1.0 : 0.0;
    }
    value_ = values[action_id];
}

void AbstractBJTreeNode::computeChanceNodeValue() {
    value_ = 0.0;
    for (size_t i = 0; i < children_.size(); ++i) {
        value_ += children_prob_[i] * children_[i]->getValue();
    }
}

void AbstractBJTreeNode::computeTerminalNodeValue() {
    value_ = static_cast<double>(bj_round_.getPlayerValue());
}

void AbstractBJTreeNode::buildChildren() {
    BJStage stage = bj_round_.getStage();

    if (stage == BJStage::ROUND_OVER) {
        // Terminal node - no children to build
    }
    else if (stage == BJStage::PLAYER_CARD) {
        buildChildrenPlayerCard();
    }
    else if (stage == BJStage::DEALER_CARD) {
        buildChildrenDealerCard();
    }
    else if (stage == BJStage::PLAYER_ACTION ||
             stage == BJStage::PLAYER_OFFERED_EARLY_SURRENDER ||
             stage == BJStage::PLAYER_OFFERED_INSURANCE)
    {
        buildChildrenPlayerAction();
    }
    else if (stage == BJStage::DEALER_CHECK_BJ) {
        buildChildrenDealerCheckBj();
    }
    else {
        throw runtime_error("Unexpected game stage");
    }

    has_built_children_ = true;
}

void AbstractBJTreeNode::buildChildrenDealerCheckBj() {
    auto dealer_value_opt = bj_round_.dealer_hand.get_best_value();
    if (!dealer_value_opt.has_value()) {
        throw runtime_error("Dealer has no valid value during blackjack check");
    }
    int dealer_value = dealer_value_opt.value();

    auto rv_prob = shoe_.get_rank_value_probabilities(nullopt);

    auto shoe_no_bj = shoe_.copy();
    double p_dealer_blackjack;

    if (dealer_value == 11) {
        p_dealer_blackjack = rv_prob.at(10);
        shoe_no_bj.lockDealerCardNotTen();
    }
    else if (dealer_value == 10) {
        p_dealer_blackjack = rv_prob.at(11);
        shoe_no_bj.lockDealerCardNotAce();
    }
    else {
        throw runtime_error("Dealer cannot check blackjack with value other than 10 or 11");
    }

    auto bj_round_dealer_bj = bj_round_.copy();
    bj_round_dealer_bj.takeAction(DealerAction::CONFIRM_BLACKJACK);

    auto bj_round_no_dealer_bj = bj_round_.copy();
    bj_round_no_dealer_bj.takeAction(DealerAction::CONFIRM_NO_BLACKJACK);

    createChild(
        bj_round_dealer_bj,
        shoe_.copy(),
        DealerAction::CONFIRM_BLACKJACK,
        p_dealer_blackjack
    );

    createChild(
        bj_round_no_dealer_bj,
        shoe_no_bj,
        DealerAction::CONFIRM_NO_BLACKJACK,
        1.0 - p_dealer_blackjack
    );
}

void AbstractBJTreeNode::buildChildrenPlayerAction() {
    auto actions = bj_round_.getAvailableActions();

    for (const auto& action : actions) {
        BJRound bj_round_copy = bj_round_.copy();
        bj_round_copy.takeAction(action);

        createChild(bj_round_copy, shoe_.copy(), action, 0.0);
    }
}

double AbstractBJTreeNode::getValue() const {
    if (!treeCompleted()) {
        throw runtime_error("Cannot get value before completing the tree");
    }
    return value_;
}

} // namespace blackjack
