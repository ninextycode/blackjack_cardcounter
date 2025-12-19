#include "shoe.h"
#include <sstream>
#include <algorithm>
#include <stdexcept>

using namespace std;

namespace blackjack {

ProbabilisticRankShoe::ProbabilisticRankShoe(int n_decks):
    ProbabilisticRankShoe(n_decks, RandomSampler::createNextSampler())
{
}

ProbabilisticRankShoe::ProbabilisticRankShoe(int n_decks, uint64_t seed):
    ProbabilisticRankShoe(n_decks, RandomSampler(seed))
{
}

ProbabilisticRankShoe::ProbabilisticRankShoe(int n_decks, const RandomSampler& sampler):
    sampler_(sampler)
{
    n_total_ = 52 * n_decks;
    value_counts_.fill(0);
    for (int i = 2; i < 10; i++) {
        value_counts_.at(i) = 4 * n_decks;   // 2..9
    }
    value_counts_.at(10) = 16 * n_decks;                         // 10s (T,J,Q,K)
    value_counts_.at(11) = 4 * n_decks;                          // Aces (11)
    recomputeRawProbabilities();
    given_dealer_card_is_not_value_ = nullopt;
}


// ProbabilisticRankShoe::ProbabilisticRankShoe(const ProbabilisticRankShoe& other)
//     : sampler_(other.sampler_)
// {
//     n_total_ = other.n_total_;
//     value_counts_ = other.value_counts_;
//     value_probs_ = other.value_probs_;
//     given_dealer_card_is_not_value_ = other.given_dealer_card_is_not_value_;
// }


ProbabilisticRankShoe::ProbabilisticRankShoe(const RankCount& rank_counts, const RandomSampler& sampler):
    sampler_(sampler)
{
    n_total_ = 0;
    value_counts_ = rank_counts;
    for (int i = 2; i <= 11; ++i) {
        n_total_ += value_counts_.at(i);
    }
    recomputeRawProbabilities();
    given_dealer_card_is_not_value_ = nullopt;
}

ProbabilisticRankShoe::ProbabilisticRankShoe(RankCount&& rank_counts, const RandomSampler& sampler):
    sampler_(sampler)
{
    n_total_ = 0;
    value_counts_ = std::move(rank_counts);
    for (int i = 2; i <= 11; ++i) {
        n_total_ += value_counts_.at(i);
    }
    recomputeRawProbabilities();
    given_dealer_card_is_not_value_ = nullopt;
}


void ProbabilisticRankShoe::setNumberOfRankCards(
    int rank_value,
    int number
) {
    if (number < 0) {
        throw runtime_error("Number of rank values cannot be negative");
    }
    int current_number = value_counts_.at(rank_value);
    n_total_ += (number - current_number);
    value_counts_.at(rank_value) = number;
    recomputeRawProbabilities();
}

int ProbabilisticRankShoe::getNumberOfRankCards(
    int rank_value
) const {
    return value_counts_.at(rank_value);
}

int ProbabilisticRankShoe::getNumberOfCards() const {
    return n_total_;
}

ProbabilisticRankShoe ProbabilisticRankShoe::copyResetSampler() const {
    ProbabilisticRankShoe copy = *this;
    copy.resetSampler();
    return copy;
}

void ProbabilisticRankShoe::resetSampler() {
    resetSampler(RandomSampler::createNextSampler());
}


void ProbabilisticRankShoe::resetSampler(const RandomSampler& sampler) {
    sampler_ = sampler;
}


int ProbabilisticRankShoe::sampleAndBurnRank(
    const optional<vector<int>>& given_rank_values_set
) {
    int rv = sampleRank(given_rank_values_set);
    burnRankValue(rv);
    return rv;
}

ProbabilisticRankShoe ProbabilisticRankShoe::copyAndBurnRank(
    int rank_value
) const {
    ProbabilisticRankShoe new_shoe(*this);
    new_shoe.burnRankValue(rank_value);
    return new_shoe;
}


void ProbabilisticRankShoe::addRankValues(
    const vector<int>& rank_values
) {
    for (int rank_value : rank_values) {
        value_counts_.at(rank_value) += 1;
    }
    n_total_ += rank_values.size();
    recomputeRawProbabilities();
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
) {
    if (!given_rank_values_set.has_value() && !isDealerCardLocked()) {
        vector<int> counts;
        for (int rv = 2; rv <= 11; rv++) {
            counts.push_back(value_counts_.at(rv));
        }
        return sampler_.discrete_counts(counts) + 2;
    }


    RankProbability probs = getRankValueProbabilities(given_rank_values_set);
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

    // return sampler_.choice_counts(rank_values, prob_counts);
    return sampler_.choice(rank_values, prob_vec);
}


void ProbabilisticRankShoe::recomputeRawProbabilities() {
    for (int i = 2; i <= 11; i++) {
        value_probs_.at(i) = n_total_ > 0 \
            ? double(value_counts_.at(i)) / double(n_total_) \
            : 0.0;
    }
}


RankProbability ProbabilisticRankShoe::getRankValueProbabilities(
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

double ProbabilisticRankShoe::getRankValueProbability(
    int rank, const optional<vector<int>>& given_rank_values_set
) const {
    RankProbability probabilities = getRankValueProbabilities(given_rank_values_set);
    return probabilities.at(rank);
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
        if (rv == vnot) {
            probabilities.at(rv) = p;
        } else {
            probabilities.at(rv) = p * coef;
        }
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

RankCount ProbabilisticRankShoe::getRankCount() const {
    return value_counts_;
}


void ProbabilisticRankShoe::lockDealerCardNot(int rank_value) { given_dealer_card_is_not_value_ = rank_value; }
void ProbabilisticRankShoe::lockDealerCardNotAce() { lockDealerCardNot(11); }
void ProbabilisticRankShoe::lockDealerCardNotTen() { lockDealerCardNot(10); }
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
    return toStringCount();
}

string ProbabilisticRankShoe::toStringProb() const {
    ostringstream ss;
    ss << "ProbabilisticRankShoe\n";

    auto probs = getRankValueProbabilities();
    for (int rv = 2; rv <= 11; rv++) {
        double p = probs.at(rv);
        if (p==0.0) {
            continue;
        }
        ss << "  p(" << rv;
        if (given_dealer_card_is_not_value_.has_value()) {
            ss << "|D!=" << *given_dealer_card_is_not_value_ << "\n";
        }
        ss << ") = " << (p*100.0) << "%\n";
    }
    return ss.str();
}

string ProbabilisticRankShoe::toStringCount() const {
    ostringstream ss;
    ss << "ProbabilisticRankShoe\n";

    for (int rv = 2; rv <= 11; rv++) {
        int count = value_counts_.at(rv);
        if (count == 0) {
            continue;
        }
        ss << "  count(" << rv << ") = " << count << "\n";
    }
    ss << "  total = " << n_total_ << "\n";
    return ss.str();
}

string ProbabilisticRankShoe::getSamplerRngState() const {
    return sampler_.getRngState();
}

} // namespace blackjack
