#pragma once

#include "shoe.h"
#include "rules.h"
#include "floor_ceil_node.h"

namespace blackjack {

/**
 * Result of edge calculation
 */
struct EdgeResult {
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
EdgeResult calculateEdge(
    const ProbabilisticRankShoe& shoe,
    const BJRules& rules,
    int bet_unit = 100,
    int sim_depth = 5,
    double gap_target = 0.01,
    SimAlgo algo = SimAlgo::RECURSIVE
);

} // namespace blackjack
