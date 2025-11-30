#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <variant>

#include "blackjack_round.h"
#include "shoe.h"
#include "actions.h"

using namespace std;

namespace blackjack {

// Forward declaration
class AbstractBJTreeNode;

// Transition event type
using TransitionEvent = variant<int, PlayerAction, DealerAction>;

class AbstractBJTreeNode {
public:
    AbstractBJTreeNode(
        const BJRound &bj_round,
        const ProbabilisticRankShoe &shoe,
        AbstractBJTreeNode *parent = nullptr
    );

    virtual ~AbstractBJTreeNode() = default;

    // Core tree operations
    virtual void buildTree();
    virtual void recomputeTreeValue();
    virtual void buildTreeLayer(optional<int> depth);
    virtual void buildChildren();

    // Status checks
    virtual bool treeCompleted() const;
    virtual bool childrenTreesCompleted() const;

    // Value access
    virtual double getValue() const;

    // Child creation (pure virtual)
    virtual void createChild(
        const BJRound &child_bj_round,
        const ProbabilisticRankShoe &child_shoe,
        const TransitionEvent &event,
        double prob = 0.0
    ) = 0;

    // Tree manipulation
    void setAsRoot();

    // Public state
    BJRound bj_round_;
    ProbabilisticRankShoe shoe_;
    AbstractBJTreeNode *parent_;
    vector<shared_ptr<AbstractBJTreeNode>> children_;
    vector<double> children_prob_;
    vector<TransitionEvent> children_events_;
    double value_;
    bool has_built_children_;
    bool has_completed_tree_;
    int depth_from_root_;

protected:
    // Helper methods for computing node values
    void computeNodeValue();
    void computeActionNodeValue();
    void computeChanceNodeValue();
    void computeTerminalNodeValue();

    // Abstract methods for building children (must be implemented by subclasses)
    virtual void buildChildrenPlayerCard() = 0;
    virtual void buildChildrenDealerCard() = 0;

    // Non-abstract child builders
    void buildChildrenDealerCheckBj();
    void buildChildrenPlayerAction();

    // Depth management
    void updateDepthFromRoot();
};

} // namespace blackjack
