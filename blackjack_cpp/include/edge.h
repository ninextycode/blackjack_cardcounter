#pragma once

#include "shoe.h"
#include "rules.h"
#include "floor_ceil_node.h"
#include "tree_walker.h"
#include <unordered_map>
#include <tuple>
#include <variant>

namespace blackjack {

/**
 * Result of edge calculation
 */
struct ValueResult {
    double ev;        // Expected value (mid-point of floor and ceil)
    double ev_min;    // Floor value (lower bound)
    double ev_max;    // Ceil value (upper bound)
};

/**
 * Calculate the house edge given a shoe state.
 * 
 * This computes the weighted average EV across all possible starting hands
 * (player card 1, player card 2, dealer upcard) using optimal play.
 * 
 * @param shoe The current shoe state
 * @param rules The blackjack rules to use
 * @param bet_unit The bet unit (default 100)
 * @param sim_depth Simulation depth for dealer card enumeration (default 5)
 * @param gap_target Relative gap target for convergence (default 0.01)
 * @param algo Simulation algorithm to use (default RECURSIVE)
 * @return EdgeResult containing EV bounds and runtime
 */
ValueResult calculateEdge(
    const ProbabilisticRankShoe& shoe,
    const BJRules& rules,
    int bet_unit = 100,
    int sim_depth = 5,
    double gap_target = 0.01,
    SimAlgo algo = SimAlgo::COMBO
);

class EdgeCalculator {
public:
    EdgeCalculator(
        const BJRules& rules,
        int bet_unit = 100
    );
    
    /**
     * Calculate edge and cache all built trees for later TreeWalker creation.
     */
    void calculateEdge(
        const ProbabilisticRankShoe& shoe,
        int sim_depth = 9,
        double gap_target = 0.03,
        SimAlgo algo = SimAlgo::COMBO,    
        bool parallel = true
    );

    /**
     * Tighten value estimate gaps for cached nodes and recompute edge result.
     */
    void tightenValueEstimateGap(
        double relative_gap,
        bool parallel = true
    );
    
    ValueResult getEdgeResult() const;
    
    bool hasEdgeResult() const;

    /**
     * Create a TreeWalker for a specific starting hand.
     * If calculate() was called first, pops the cached tree (more efficient).
     * Otherwise builds a new tree.
     */
    TreeWalker createTreeWalker(
        pair<int, int> player_cards,
        int dealer_card
    );
    
    /**
     * Check if a TreeWalker can be created for the given cards.
     * Returns true if the shoe has enough cards.
     */
    bool canCreateTreeWalker(
        pair<int, int> player_cards,
        int dealer_card
    ) const;
    
    /**
     * Check if a cached tree exists for the given starting hand.
     */
    bool hasCachedTree(
        pair<int, int> player_cards,
        int dealer_card
    ) const;

private:
    struct Task {
        int p0, p1, d;
        double prob;
    };
    using TreeKey = tuple<int, int, int>;  // (p0, p1, dealer) with p0 <= p1
    struct TreeKeyHash {
        size_t operator()(const TreeKey& k) const noexcept {
            size_t h1 = std::hash<int>{}(get<0>(k));
            size_t h2 = std::hash<int>{}(get<1>(k));
            size_t h3 = std::hash<int>{}(get<2>(k));
            return h1 ^ h2 ^ h3;
        }
    };
    using HandResult = variant<double, shared_ptr<AbstractFloorCeilNode>>;

    static TreeKey makeKey(pair<int, int> player_cards, int dealer_card) {
        int p0 = min(player_cards.first, player_cards.second);
        int p1 = max(player_cards.first, player_cards.second);
        return make_tuple(p0, p1, dealer_card);
    }
    
    shared_ptr<const BJRules> rules_;
    int bet_unit_;
    
    vector<Task> tasks_;
    vector<HandResult> hand_results_;
    unordered_map<TreeKey, size_t, TreeKeyHash> hand_index_;
    ValueResult edge_result_;
    bool has_edge_result_ = false;
};

} // namespace blackjack
