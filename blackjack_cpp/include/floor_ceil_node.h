#pragma once

#include <memory>
#include <vector>
#include <optional>
#include <string>
#include <set>

#include "abstract_node.h"
#include "blackjack_round.h"
#include "shoe.h"
#include "actions.h"
#include "dealer_sim.h"

using namespace std;

namespace blackjack {

// Forward declarations
class FloorCeilNode;
class DecisionNode;
class HitNode;
class SplitNode;
class DoubleNode;
class DealerCheckBJNode;
class ValueNode;
class FloorCeilValueNode;

// Simulation algorithm type
enum class SimAlgo {
    COMBO,
    RECURSIVE
};

/**
 * ValueNode - holds simulation results without building children.
 * Matches Python abstract_node.py ValueNode class.
 */
class ValueNode : public AbstractBJTreeNode {
public:
    ValueNode(double value, AbstractBJTreeNode* parent = nullptr);

    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    void buildTreeLayer(optional<int> depth) override;
    bool treeCompleted() const override;
    bool childrenTreesCompleted() const override;
    double getValue() const override;
    double getCeilValue() const;
    double getFloorValue() const;

protected:
    void buildChildrenPlayerCard() override;
    void buildChildrenDealerCard() override;
};

/**
 * FloorCeilValueNode - holds ceil and floor values without building children.
 * Matches Python abstract_node.py FloorCeilValueNode class.
 */
class FloorCeilValueNode : public AbstractBJTreeNode {
public:
    FloorCeilValueNode(double floor_value, double ceil_value, AbstractBJTreeNode* parent = nullptr);

    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    void buildTreeLayer(optional<int> depth) override;
    bool treeCompleted() const override;
    bool childrenTreesCompleted() const override;
    double getValue() const override;
    double getCeilValue() const;
    double getFloorValue() const;

protected:
    void buildChildrenPlayerCard() override;
    void buildChildrenDealerCard() override;

private:
    double floor_value_;
    double ceil_value_;
};

/**
 * FloorCeilNode - base class for floor/ceiling value computation.
 * Matches Python floor_ceil_node.py FloorCeilNode class.
 */
class FloorCeilNode : public AbstractBJTreeNode {
public:
    FloorCeilNode(
        const BJRound& bj_round,
        const ProbabilisticRankShoe& shoe,
        int max_hand_size_full_enum,
        int dealer_sim_depth = 5,
        SimAlgo sim_algo = SimAlgo::RECURSIVE,
        AbstractBJTreeNode* parent = nullptr
    );

    virtual ~FloorCeilNode() = default;

    // Override abstract methods
    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    // Floor/ceil value accessors
    virtual double getCeilValue() const;
    virtual double getFloorValue() const;

    // Override rebuild
    virtual void rebuildChildren();

    // Tree conversion methods
    // Returns pair<bool, bool>: (value_changed, is_final)
    // value_changed: whether the node value changed
    // is_final: whether no further exploration can improve the value
    virtual pair<bool, bool> convertToFullUpToDepth(int depth);

    // Configuration
    int max_hand_size_full_enum_;
    int dealer_sim_depth_;
    SimAlgo sim_algo_;

protected:
    // Run dealer simulation using configured algorithm
    double runDealerSim(const BJRound& bj_round, const ProbabilisticRankShoe& shoe);

    // Value computation methods
    virtual void computeFloorValue();
    virtual void computeCeilValue();
    void computeChanceNodeCeilValue();
    void computeChanceNodeFloorValue();
    void computeNodeValue() override;

    // Child building methods
    void buildChildrenDealerCard() override;
    void buildChildDealerBlackjack();
    void buildChildrenPlayerCard() override;

    // Conversion helper
    virtual bool convertFromSampleToFull();

    // Internal state
    optional<int> active_hand_size_;
    double ceil_value_;
    double floor_value_;
};

/**
 * DecisionNode - represents game state before player makes decision.
 * Matches Python floor_ceil_node.py DecisionNode class.
 */
class DecisionNode : public FloorCeilNode {
public:
    DecisionNode(
        const BJRound& bj_round,
        const ProbabilisticRankShoe& shoe,
        int max_hand_size_full_enum,
        int dealer_sim_depth = 5,
        SimAlgo sim_algo = SimAlgo::RECURSIVE,
        AbstractBJTreeNode* parent = nullptr
    );

    // Override createChild to create appropriate node types
    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    void buildChildren() override;

    // Override rebuild
    void rebuildChildren() override;

    // Decision-related accessors
    optional<PlayerAction> getDecisionChoice() const;
    AbstractBJTreeNode* getDecisionChoiceChild();
    bool hasDecided() const;
    vector<pair<PlayerAction, AbstractBJTreeNode*>> getPossibleActionChildren();

    // Tree conversion
    // Returns pair<bool, bool>: (value_changed, is_final)
    pair<bool, bool> convertToFullUpToDepth(int depth) override;
    pair<bool, bool> convertBjCheckChildrenToFullUpToDepth(int depth);
    pair<bool, bool> convertPossibleChildrenToFullUpToDepth(int depth);
    pair<bool, bool> convertDecisionChildToFullUpToDepth(int depth);

    // Possible actions tracking
    vector<PlayerAction> possible_actions_;

protected:
    void computeFloorValue() override;
    void computeCeilValue() override;
    void computeActionNodeValue();
    void computeNodeValue() override;
    void updatePossibleActions();

    void buildChildrenPlayerAction();
    void buildChildrenInsurance();
};

/**
 * DealerCheckBJNode - handles dealer blackjack check with insurance.
 * Matches Python floor_ceil_node.py DealerCheckBJNode class.
 */
class DealerCheckBJNode : public FloorCeilNode {
public:
    DealerCheckBJNode(
        const BJRound& bj_round,
        const ProbabilisticRankShoe& shoe,
        int max_hand_size_full_enum,
        bool took_insurance,
        bool insurance_offered,
        int dealer_sim_depth = 5,
        SimAlgo sim_algo = SimAlgo::RECURSIVE,
        AbstractBJTreeNode* parent = nullptr
    );

    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    void buildChildren() override;

    double getDealerBlackjackChance() const;

    // Returns pair<bool, bool>: (value_changed, is_final)
    pair<bool, bool> convertToFullUpToDepth(int depth) override;

    // Child indices
    int dealer_bj_child_idx_;
    int dealer_no_bj_child_idx_;
    bool insurance_offered_;
    bool took_insurance_;
    double insurance_bet_;

protected:
    void computeNodeValue() override;
    void computeCeilValue() override;
    void computeFloorValue() override;
};

/**
 * SplitNode - handles split action simulation.
 * Matches Python floor_ceil_node.py SplitNode class.
 */
class SplitNode : public FloorCeilNode {
public:
    SplitNode(
        const BJRound& bj_round,
        const ProbabilisticRankShoe& shoe,
        int max_hand_size_full_enum,
        int dealer_sim_depth = 5,
        SimAlgo sim_algo = SimAlgo::RECURSIVE,
        AbstractBJTreeNode* parent = nullptr
    );

    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    void buildChildren() override;

    // Index of the first hand (placeholder hand with bet=0)
    int first_hand_idx_;

protected:
    void computeNodeValue() override;
    void computeCeilValue() override;
    void computeFloorValue() override;
    bool convertFromSampleToFull() override;
};

/**
 * HitNode - represents game state after player hits.
 * Matches Python floor_ceil_node.py HitNode class.
 */
class HitNode : public FloorCeilNode {
public:
    HitNode(
        const BJRound& bj_round,
        const ProbabilisticRankShoe& shoe,
        int max_hand_size_full_enum,
        int dealer_sim_depth = 5,
        SimAlgo sim_algo = SimAlgo::RECURSIVE,
        AbstractBJTreeNode* parent = nullptr
    );

    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    void buildChildren() override;

    bool canAddSample() const;
    bool addSample();

    // Card categorization
    RankProbability rank_probabilities_;
    vector<int> cards_bust_;
    vector<int> cards_21_;
    vector<int> cards_not_sampled_;
    vector<int> cards_sampled_;
    double p_bust_;
    double p_21_;
    double max_child_value_;
    double min_child_value_;

protected:
    void initValues();
    void computeNodeValue() override;
    bool convertFromSampleToFull() override;
};

/**
 * DoubleNode - handles double down action.
 * Matches Python floor_ceil_node.py DoubleNode class.
 */
class DoubleNode : public FloorCeilNode {
public:
    DoubleNode(
        const BJRound& bj_round,
        const ProbabilisticRankShoe& shoe,
        AbstractBJTreeNode* parent,
        int dealer_sim_depth = 5,
        SimAlgo sim_algo = SimAlgo::RECURSIVE
    );

    void createChild(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        const TransitionEvent& event,
        double prob = 0.0
    ) override;

    void buildChildren() override;
    
    double getCeilValue() const override;
    double getFloorValue() const override;

protected:
    void computeNodeValue() override;
};

/**
 * Factory function to build root node based on round state.
 * Matches Python floor_ceil_node.py build_root_node function.
 */
shared_ptr<AbstractBJTreeNode> buildRootNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int max_hand_size_full_enum = 1,
    int dealer_sim_depth = 5,
    SimAlgo sim_algo = SimAlgo::RECURSIVE
);

} // namespace blackjack
