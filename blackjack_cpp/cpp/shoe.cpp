#include "../include/shoe.h"
#include <sstream>
#include <algorithm>
#include <stdexcept>

using namespace std;

namespace blackjack {

ProbabilisticRankShoe::ProbabilisticRankShoe(int n_decks_): n_decks(n_decks_) {
    n_total = 52 * n_decks;
    value_counts.fill(0);
    for (int i=0;i<8;++i) value_counts[i] = 4 * n_decks;   // 2..9
    value_counts[8] = 16 * n_decks;                         // 10s (T,J,Q,K)
    value_counts[9] = 4 * n_decks;                          // Aces (11)
    recompute_raw_probabilities();
    given_dealer_card_is_not_value = nullopt;
}

ProbabilisticRankShoe ProbabilisticRankShoe::copy() const {
    ProbabilisticRankShoe s(n_decks);
    s.n_total = n_total;
    s.value_counts = value_counts;
    s.value_probs = value_probs;
    s.given_dealer_card_is_not_value = given_dealer_card_is_not_value;
    return s;
}

void ProbabilisticRankShoe::recompute_raw_probabilities() {
    for (int i=0;i<10;++i) {
        value_probs[i] = n_total>0 ? double(value_counts[i]) / double(n_total) : 0.0;
    }
}

void ProbabilisticRankShoe::take_given_dealer_info_into_account(
    RankProbability& probabilities
) const {
    if (!given_dealer_card_is_not_value.has_value()) return;

    int vnot = *given_dealer_card_is_not_value;
    int n_cards_dealer_card_is_not = 0;
    
    for (int v=2; v<=11; ++v) {
        if (v != vnot) {
            n_cards_dealer_card_is_not += value_counts[v-2];
        }
    }
    
    double coef = 0;
    if (n_cards_dealer_card_is_not > 0) {
        coef = double(n_cards_dealer_card_is_not - 1) / double(n_cards_dealer_card_is_not);
    }

    for (int rv=2; rv<=11; ++rv) {
        double p = probabilities.probs[rv-2] * double(n_total) / double(max(1, n_total - 1));
        if (rv == vnot) probabilities.probs[rv-2] = p;
        else probabilities.probs[rv-2] = p * coef;
    }
    return;
}

void ProbabilisticRankShoe::probabilities_given_rank_values_set(
    RankProbability& probabilities, const optional<vector<int>>& given
) const {
    if (!given.has_value()) return;

    double given_rank_p = 0.0;
    for (int rv: *given) given_rank_p += probabilities.p(rv);
    if (given_rank_p == 0.0) {
        probabilities.probs.fill(0.0);
        return;
    }
    for (int rv=2; rv<=11; ++rv) {
        bool keep = given && find(given->begin(), given->end(), rv) != given->end();
        probabilities.probs[rv-2] = keep ? probabilities.probs[rv-2] / given_rank_p : 0.0;
    }
    return;
}

RankProbability ProbabilisticRankShoe::get_rank_value_probabilities(
    const optional<vector<int>>& given_rank_values_set
) const {
    RankProbability probabilities;
    for (int v=2; v <= 11; v++) {
        probabilities.probs[v-2] = value_probs[v-2];
    }
    take_given_dealer_info_into_account(probabilities);
    probabilities_given_rank_values_set(probabilities, given_rank_values_set);
    return probabilities;
}

void ProbabilisticRankShoe::burn_card(const Card& c) { 
    burn_rank_value(c.rank_value()); 
}

void ProbabilisticRankShoe::burn_rank_value(int rank_value) {
    int idx = rank_value - 2;
    if (idx < 0 || idx >= 10) {
        throw runtime_error("Invalid rank value");
    }
    if (value_counts[idx] < 1) {
        throw runtime_error("Card count for rank value is below 1");
    }
    value_counts[idx] -= 1;
    n_total -= 1;
    recompute_raw_probabilities();
}

void ProbabilisticRankShoe::lock_dealer_card_not_ace() { given_dealer_card_is_not_value = 11; }
void ProbabilisticRankShoe::lock_dealer_card_not_ten() { given_dealer_card_is_not_value = 10; }
void ProbabilisticRankShoe::unlock_dealer_card() { given_dealer_card_is_not_value = nullopt; }

string ProbabilisticRankShoe::to_string() const {
    ostringstream ss;
    ss << "ProbabilisticRankShoe\n";
    if (given_dealer_card_is_not_value.has_value()) {
        ss << "|D!=" << *given_dealer_card_is_not_value << "\n";
    }
    auto probs = get_rank_value_probabilities();
    for (int rv=2; rv<=11; ++rv) {
        double p = probs.p(rv);
        if (p==0.0) continue;
        ss << "  p(" << rv << ") = " << (p*100.0) << "%\n";
    }
    return ss.str();
}

} // namespace blackjack
