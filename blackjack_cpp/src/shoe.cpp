#include "shoe.h"
#include <sstream>
#include <algorithm>
#include <stdexcept>

using namespace std;

namespace blackjack {

ProbabilisticRankShoe::ProbabilisticRankShoe(
    int n_decks, 
    uint64_t seed
):
    ProbabilisticRankShoe::ProbabilisticRankShoe(
        n_decks, make_shared<RandomSampler>(seed)
    )
{ }


ProbabilisticRankShoe::ProbabilisticRankShoe(
    int n_decks, 
    shared_ptr<RandomSampler> sampler
):
    n_decks_(n_decks),
    sampler_(sampler)
{
    if (!sampler_) {
        sampler_ = RandomSampler::getGlobalSampler();
    }

    n_total_ = 52 * n_decks_;
    value_counts_.fill(0);
    for (int i = 2; i < 10; i++) {
        value_counts_.at(i) = 4 * n_decks_;   // 2..9
    }
    value_counts_.at(10) = 16 * n_decks_;                         // 10s (T,J,Q,K)
    value_counts_.at(11) = 4 * n_decks_;                          // Aces (11)
    recomputeRawProbabilities();
    given_dealer_card_is_not_value_ = nullopt;
}


ProbabilisticRankShoe::ProbabilisticRankShoe(const ProbabilisticRankShoe& other)
    : n_decks_(other.n_decks_)
{
    if (other.sampler_ == RandomSampler::getGlobalSampler()) {
        sampler_ = other.sampler_;
    } else {
        sampler_ = make_shared<RandomSampler>(*other.sampler_);
    }
    n_total_ = other.n_total_;
    value_counts_ = other.value_counts_;
    value_probs_ = other.value_probs_;
    given_dealer_card_is_not_value_ = other.given_dealer_card_is_not_value_;
}


void ProbabilisticRankShoe::changeRandomSampler(int seed) {
    if (seed == -1) {
        sampler_ = RandomSampler::getGlobalSampler();
    } else {
        sampler_ = make_shared<RandomSampler>(uint64_t(seed));
    }
}


int ProbabilisticRankShoe::sampleAndBurnRank(
    const optional<vector<int>>& given_rank_values_set
) {
    int rv = sampleRank(given_rank_values_set);
    burnRankValue(rv);
    return rv;
}


void ProbabilisticRankShoe::addRankValue(
    int rank_value
) {
    value_counts_.at(rank_value) += 1;
    n_total_ += 1;
    recomputeRawProbabilities();
}


int ProbabilisticRankShoe::sampleRank(
    const optional<vector<int>>& given_rank_values_set
) const {
    RankProbability probs = get_rank_value_probabilities(given_rank_values_set);
    vector<double> prob_vec;
    vector<int> rank_values;
    for (int rv = 2; rv <= 11; rv++) {
        double p = probs.at(rv);
        if (p > 0.0) {
            prob_vec.push_back(p);
            rank_values.push_back(rv);
        }
    }
    if (prob_vec.empty()) {
        throw runtime_error("No available ranks to sample from");
    }
    return sampler_->choice(rank_values, prob_vec);
}


void ProbabilisticRankShoe::recomputeRawProbabilities() {
    for (int i = 2; i <= 11; i++) {
        value_probs_.at(i) = n_total_ > 0 \
            ? double(value_counts_.at(i)) / double(n_total_) \
            : 0.0;
    }
}


void ProbabilisticRankShoe::takeGivenDealerInfoIntoAccount(
    RankProbability& probabilities
) const {
    if (!given_dealer_card_is_not_value_.has_value()) return;

    int vnot = *given_dealer_card_is_not_value_;
    int n_cards_dealer_card_is_not = 0;
    
    for (int v = 2; v <= 11; v++) {
        if (v != vnot) {
            n_cards_dealer_card_is_not += value_counts_.at(v);
        }
    }
    
    double coef = 0;
    if (n_cards_dealer_card_is_not > 0) {
        coef = double(n_cards_dealer_card_is_not - 1) / double(n_cards_dealer_card_is_not);
    }

    for (int rv = 2; rv <= 11; rv++) {
        double p = probabilities.at(rv) * double(n_total_) / double(max(1, n_total_ - 1));
        if (rv == vnot) probabilities.at(rv) = p;
        else probabilities.at(rv) = p * coef;
    }
    return;
}


void ProbabilisticRankShoe::takeGivenSetIntoAccount(
    RankProbability& probabilities,
    const optional<vector<int>>& given
) const {
    if (!given.has_value()) return;

    double given_rank_p = 0.0;
    for (int rv: *given) {
        given_rank_p += probabilities.at(rv);
    }
    if (given_rank_p == 0.0) {
        probabilities.fill(0.0);
        return;
    }
    for (int rv = 2; rv <= 11; rv++) {
        bool keep = given && find(given->begin(), given->end(), rv) != given->end();
        probabilities.at(rv) = keep ? probabilities.at(rv) / given_rank_p : 0.0;
    }
    return;
}


RankProbability ProbabilisticRankShoe::get_rank_value_probabilities(
    const optional<vector<int>>& given_rank_values_set
) const {
    RankProbability probabilities;
    for (int v = 2; v <= 11; v++) {
        probabilities.at(v) = value_probs_.at(v);
    }
    takeGivenDealerInfoIntoAccount(probabilities);
    takeGivenSetIntoAccount(probabilities, given_rank_values_set);
    return probabilities;
}


void ProbabilisticRankShoe::burnCard(const Card& c) { 
    burnRankValue(c.rank_value()); 
}


void ProbabilisticRankShoe::burnRankValue(int rank_value) {
    if (value_counts_.at(rank_value) < 1) {
        throw runtime_error("Card count for rank value is below 1");
    }
    value_counts_.at(rank_value) -= 1;
    n_total_ -= 1;
    recomputeRawProbabilities();
}


void ProbabilisticRankShoe::lockDealerCardNotAce() { given_dealer_card_is_not_value_ = 11; }
void ProbabilisticRankShoe::lockDealerCardNotTen() { given_dealer_card_is_not_value_ = 10; }
void ProbabilisticRankShoe::unlockDealerCard() { given_dealer_card_is_not_value_ = nullopt; }

int ProbabilisticRankShoe::dealerCardLockedValue() {
    if (!given_dealer_card_is_not_value_.has_value()) {
        return -1;
    }
    return *given_dealer_card_is_not_value_;
}

bool ProbabilisticRankShoe::isDealerCardLocked() {
    return given_dealer_card_is_not_value_.has_value();
}

string ProbabilisticRankShoe::toString() const {
    ostringstream ss;
    ss << "ProbabilisticRankShoe\n";
    if (given_dealer_card_is_not_value_.has_value()) {
        ss << "|D!=" << *given_dealer_card_is_not_value_ << "\n";
    }
    auto probs = get_rank_value_probabilities();
    for (int rv = 2; rv <= 11; rv++) {
        double p = probs.at(rv);
        if (p==0.0) {
            continue;
        }
        ss << "  p(" << rv << ") = " << (p*100.0) << "%\n";
    }
    return ss.str();
}

} // namespace blackjack
