#pragma once
#include <map>
#include <array>
#include <vector>
#include <optional>
#include <string>
#include "cards.h"

using namespace std;

namespace blackjack {

struct RankProbability {
    std::array<double, 10> probs{}; // index 0->P(2), ..., index 9->P(11)
    inline double p(int rank_value) const {
        int idx = rank_value - 2;
        if (idx < 0 || idx >= 10) return 0.0;
        return probs[idx];
    }
};

class ProbabilisticRankShoe {
public:
    ProbabilisticRankShoe(int n_decks = 8);
    ProbabilisticRankShoe copy() const;
    // Returns probabilities for values 2..11 as RankProbability
    RankProbability get_rank_value_probabilities(const optional<vector<int>>& given_rank_values_set = nullopt) const;
    void burn_card(const Card& c);
    void burn_rank_value(int rank_value);
    void lock_dealer_card_not_ace();
    void lock_dealer_card_not_ten();
    void unlock_dealer_card();
    string to_string() const;

    int n_decks;
    int n_total; // remaining cards
    // Counts for values 2..11
    array<int, 10> value_counts{};     // index 0->value 2, ..., index 9->value 11
    // Raw probabilities (without dealer info), maintained alongside counts
    array<double, 10> value_probs{};   // index 0->P(2), ..., index 9->P(11)
    optional<int> given_dealer_card_is_not_value;
private:
    void recompute_raw_probabilities();
    void take_given_dealer_info_into_account(
        RankProbability& probs
    ) const;
    void probabilities_given_rank_values_set(
        RankProbability& probs,
        const optional<vector<int>>& given
    ) const;
};

} // namespace blackjack
