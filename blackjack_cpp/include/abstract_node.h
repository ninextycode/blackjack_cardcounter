#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <variant>
#include <stdexcept>

#include "blackjack_round.h"
#include "shoe.h"
#include "actions.h"

using namespace std;

namespace blackjack {

// Forward declaration
class AbstractBJTreeNode;

// Transition event type
using TransitionEvent = variant<int, PlayerAction, DealerAction>;

string to_string(const TransitionEvent& event);
string to_string_card(const int& card);

/**
 * ValueNodeInterface - interface for nodes that can return values.
 * All nodes can return a value, and optionally provide floor/ceil values.
 * By default, getFloorValue() and getCeilValue() throw exceptions.
 * Only ValueNode overrides them to return getValue().
 */
class ValueNodeInterface {
public:
    virtual ~ValueNodeInterface() = default;
    virtual double getValue() const = 0;
    
    // Default implementations throw exceptions
    // Subclasses must override to provide actual floor/ceil values
    virtual double getFloorValue() const {
        throw runtime_error("getFloorValue() not implemented for this node type");
    }
    
    virtual double getCeilValue() const {
        throw runtime_error("getCeilValue() not implemented for this node type");
    }
};

class AbstractBJTreeNode : public ValueNodeInterface {
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
    virtual double getValue() const override;
    
    // Floor/ceil value access (default implementations throw exceptions)
    // Subclasses must override to provide implementations
    virtual double getFloorValue() const override;
    virtual double getCeilValue() const override;

    // Child creation (pure virtual)
    virtual void createChild(
        const BJRound &child_bj_round,
        const ProbabilisticRankShoe &child_shoe,
        const TransitionEvent &event,
        double prob = 0.0
    ) = 0;

    // Tree manipulation
    void setAsRoot();

    // Rebuild children
    virtual void rebuildChildren();

    // Add child directly (for manual child management)
    void addChild(shared_ptr<AbstractBJTreeNode> child, const TransitionEvent& event, double prob = 0.0);

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
    // Helper methods for computing node values (virtual for override)
    virtual void computeNodeValue();
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
