#pragma once

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>

#include "blackjack_round.h"
#include "shoe.h"
#include "hand.h"

using namespace std;

namespace blackjack {

/**
 * Run recursive dealer simulation for expected value calculation.
 * 
 * @param bj_round The current blackjack round state
 * @param shoe The shoe to draw cards from
 * @param n_dealer_sim_runs Number of Monte Carlo simulation runs (used when n_full_sample exhausted)
 * @param n_full_sample Depth of full enumeration before falling back to Monte Carlo
 * @return Expected value of the round
 */
double runDealerCardsSimulationRecursive(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    int n_full_sample = 5
);

/**
 * Run dealer simulation using combo algorithm.
 * Uses precomputed combination data for exact probability calculation
 * of bust/stand outcomes, with Monte Carlo fallback for "other" combos.
 * 
 * Requires PreloadedComboData::loadAll() to be called first.
 * 
 * @param bj_round The current blackjack round state
 * @param shoe The shoe to draw cards from
 * @param n_dealer_sim_runs Number of Monte Carlo runs for "other" combos (default 1)
 * @param n_full_sample Depth of precomputed combinations to use
 * @param verbose If true, print debug info
 * @return Expected value of the round
 * @throws runtime_error if preconditions not met or data not loaded
 */
double runDealerCardsSimulationCombo(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs = 0,
    int n_full_sample = 5,
    bool verbose = false
);

// Internal helper functions (exposed for testing)

/**
 * Check if dealer should stand or has busted.
 * 
 * @param dealer_hand The dealer's hand
 * @param dealer_hit_soft_17 Whether dealer hits on soft 17
 * @return true if dealer should stand or is bust, false if dealer should hit
 */
bool dealerStandOrBust(
    const ValueOnlyHand& dealer_hand,
    bool dealer_hit_soft_17
);

/**
 * Internal recursive simulation function.
 * Operates on player value and dealer hand directly for efficiency.
 * 
 * @param player_value The player's hand value
 * @param dealer_hand The dealer's hand (modified in place)
 * @param shoe The shoe (modified in place)
 * @param n_dealer_sim_runs Monte Carlo runs
 * @param n_full_sample Remaining depth for full enumeration
 * @param possible_ranks Optional set of possible ranks for next card
 * @param dealer_hit_soft_17 Whether dealer hits soft 17
 * @return Normalized expected value (-1 to 1)
 */
double runDealerCardsSimulationRecursiveInternal(
    int player_value,
    ValueOnlyHand& dealer_hand,
    ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    int n_full_sample = 3,
    const optional<vector<int>>& possible_ranks = nullopt,
    bool dealer_hit_soft_17 = false
);

/**
 * Monte Carlo simulation for remaining dealer cards.
 * 
 * @param player_value The player's hand value
 * @param dealer_hand The dealer's hand (not modified)
 * @param shoe The shoe (temporarily modified during simulation)
 * @param n_dealer_sim_runs Number of simulation runs
 * @param dealer_hit_soft_17 Whether dealer hits soft 17
 * @return Mean outcome value (-1 to 1)
 */
double runDealerCardsSimulation(
    int player_value,
    ValueOnlyHand& dealer_hand,
    ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    bool dealer_hit_soft_17 = false
);

/**
 * Simplified Monte Carlo simulation for remaining dealer cards.
 * Uses raw value and soft flag instead of Hand object for better performance.
 * 
 * @param player_value The player's hand value
 * @param dealer_value The dealer's current hand value
 * @param is_soft Whether the dealer's hand is soft (contains an ace counted as 11)
 * @param shoe The shoe (temporarily modified during simulation)
 * @param n_dealer_sim_runs Number of simulation runs
 * @param dealer_hit_soft_17 Whether dealer hits soft 17
 * @return Mean outcome value (-1 to 1)
 */
double runDealerCardsSimulationSimple(
    int player_value,
    int dealer_value,
    bool is_soft,
    ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    bool dealer_hit_soft_17 = false
);

/**
 * Data structure holding precomputed dealer card combinations.
 * Used for the combo algorithm optimization.
 * 
 * Each combo tuple contains:
 * - vector<int>: sequence of card values
 * - int: count/permutations of this combination
 * - uint64_t: precomputed combo ID for cache lookup
 * - int: dealer hand value after this combo (for stand/other combos)
 * - bool: whether dealer hand is soft after this combo (other combos only)
 */
struct CombinationsData {
    // (combo, count, combo_id)
    vector<tuple<vector<int>, int, uint64_t>> bust_combos_data;
    // (combo, count, combo_id, dealer_value)
    vector<tuple<vector<int>, int, uint64_t, int>> stand_combos_data;
    vector<int> stand_values;
    // (combo, count, combo_id, dealer_value, is_soft)
    vector<tuple<vector<int>, int, uint64_t, int, bool>> other_combos_data;
};

/**
 * Load precomputed combinations data from a JSON file.
 * 
 * JSON format expected:
 * {
 *   "bust_combos_data": [[[card1, card2, ...], count], ...],
 *   "stand_combos_data": [[[card1, card2, ...], count], ...],
 *   "stand_values": [value1, value2, ...],
 *   "other_combos_data": [[[card1, card2, ...], count], ...]
 * }
 * 
 * @param json_path Path to the JSON file
 * @param upcard The dealer's upcard value (2-11) for precomputing hand values
 * @return CombinationsData structure with loaded data
 * @throws runtime_error if file cannot be opened or parsed
 */
CombinationsData loadCombinationsData(const string& json_path, int upcard);


/**
 * Cache for preloaded combination data.
 * Stores data indexed by [dealer_confirm_no_bj][depth][upcard].
 * 
 * Usage:
 *   PreloadedComboData::loadAll("/path/to/combinations");
 *   const auto& data = PreloadedComboData::getData(true, 5, 6);
 */
class PreloadedComboData {
public:
    /**
     * Load all combination data from a directory.
     * Expects files named: depth_{depth}_upcard_{upcard}.json
     * in subdirectories: dealer_checked_no_bj/ and bj_not_checked/
     * 
     * @param base_path Base directory containing the subdirectories
     * @param max_depth Maximum depth to load (default 8)
     */
    static void loadAll(const string& base_path, int max_depth = 8);

    /**
     * Get preloaded combination data for given parameters.
     * 
     * @param dealer_confirmed_no_bj Whether dealer has confirmed no blackjack
     * @param depth The depth of combinations
     * @param upcard The dealer's upcard value (2-11, where 11 = Ace)
     * @return Reference to the CombinationsData
     * @throws runtime_error if data not loaded or invalid parameters
     */
    static const CombinationsData& getData(
        bool dealer_confirmed_no_bj, int depth, int upcard
    );

    static bool isLoaded();

private:
    // Storage: [depth][upcard - 2] -> CombinationsData
    // Outer map for depth (sparse), inner vector for upcard (dense, index = upcard - 2)
    static inline unordered_map<int, vector<CombinationsData>> data_no_bj_;
    static inline unordered_map<int, vector<CombinationsData>> data_bj_not_checked_;
    static inline bool loaded_ = false;
};


} // namespace blackjack
