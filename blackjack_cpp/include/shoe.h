#pragma once
#include <map>
#include <array>
#include <vector>
#include <optional>
#include <string>
#include <memory>
#include "cards.h"
#include "random_sampler.h"

using namespace std;

namespace blackjack {

template <typename T>
struct RankMap {
    array<T, 10> data{}; // index 0->(2), ..., index 9->(11)
    inline void fill(const T& value) {
        data.fill(value);
    }

    inline T& at(int rank_value) {
        return data[getIdx(rank_value)];
    }
    inline const T& at(int rank_value) const {
        return data[getIdx(rank_value)];
    }

private:
    inline int getIdx(int rank_value) const {
        int idx = rank_value - 2;
        if (idx < 0 || idx >= 10) {
            throw runtime_error("Invalid rank value");
        }
        return idx;
    }
};


using RankProbability = RankMap<double>; 
using RankCount = RankMap<int>;

class ProbabilisticRankShoe {
public:
    ProbabilisticRankShoe(int n_decks = 8);
    ProbabilisticRankShoe(int n_decks, uint64_t seed);
    ProbabilisticRankShoe(int n_decks, const RandomSampler& sampler);
    ProbabilisticRankShoe(RankCount&& rank_counts, const RandomSampler& sampler);
    ProbabilisticRankShoe(const RankCount& rank_counts, const RandomSampler& sampler);
    // ProbabilisticRankShoe(const ProbabilisticRankShoe&);

    ProbabilisticRankShoe(const ProbabilisticRankShoe&) = default;
    ProbabilisticRankShoe& operator=(const ProbabilisticRankShoe&) = default;

    ProbabilisticRankShoe(ProbabilisticRankShoe&&) noexcept = default;
    ProbabilisticRankShoe& operator=(ProbabilisticRankShoe&&) noexcept = default;

    ~ProbabilisticRankShoe() = default;

    ProbabilisticRankShoe copyResetSampler() const;
    void resetSampler();
    void resetSampler(const RandomSampler& sampler);

    // Returns probabilities for values 2..11 as RankProbability
    RankProbability getRankValueProbabilities(
        const optional<vector<int>>& given_rank_values_set = nullopt
    ) const;
    
    double getRankValueProbability(
        int rank, const optional<vector<int>>& given_rank_values_set = nullopt
    ) const;

    void burnCard(const Card& c);
    void burnRankValue(int rank_value);
    ProbabilisticRankShoe copyAndBurnRank(int rank_value) const;
    void addRankValue(int rank_value);
    void addRankValues(const vector<int>& rank_values);
    
    void setNumberOfRankCards(int rank_value, int number);
    int getNumberOfRankCards(int rank_value) const;
    int getNumberOfCards() const;

    void lockDealerCardNot(int rank_value);
    void lockDealerCardNotAce();
    void lockDealerCardNotTen();
    void unlockDealerCard();
    int dealerCardLockedValue();
    bool isDealerCardLocked();
    
    int sampleAndBurnRank(
        const optional<vector<int>>& given_rank_values_set = nullopt
    );
    int sampleRank(
        const optional<vector<int>>& given_rank_values_set = nullopt
    );

    RankCount getRankCount() const;

    string toString() const;
    string toStringProb() const;
    string toStringCount(bool compact = false) const;
    string toStringCountFull() const;
    string toStringCountCompact() const;


    string getSamplerRngState() const;
private:
    void recomputeRawProbabilities();
    void takeGivenDealerInfoIntoAccount(
        RankProbability& probs
    ) const;
    void takeGivenSetIntoAccount(
        RankProbability& probs,
        const optional<vector<int>>& given
    ) const;

    int n_total_; // remaining cards
    // Counts for values 2..11
    RankCount value_counts_;     // index 0->value 2, ..., index 9->value 11
    // Raw probabilities (without dealer info), maintained alongside counts
    RankProbability value_probs_;   // index 0->P(2), ..., index 9->P(11)
    optional<int> given_dealer_card_is_not_value_;

    RandomSampler sampler_;
};

} // namespace blackjack
