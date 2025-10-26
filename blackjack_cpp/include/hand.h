#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "cards.h"

using namespace std;

namespace blackjack {

class Hand {
public:
    Hand();
    Hand(const std::vector<Card>& cards);
    int size() const;
    void add_card(const Card& c);
    std::pair<Hand, Hand> split() const;
    bool is_same_rank_pair() const;
    bool is_same_value_pair() const;
    bool is_natural_blackjack() const;
    bool is_bust() const;
    bool is_soft() const;
    bool is_soft_17() const;
    std::optional<int> get_best_value() const;
    int get_hard_value() const;
    std::string to_string() const;
    // Accessors for read-only access to underlying cards
    const std::vector<Card>& cards() const;
    const Card& front() const;
    const Card& operator[](size_t i) const;

private:
    void reset_cache() const;
    void compute_best_value() const;
    std::vector<Card> cards_;
    mutable std::optional<int> best_value_cache_;
    mutable int hard_value_cache_;
};

// Hand-like class storing integer soft values (Ace as 11, others 2-10).
// Mirrors Python ValueOnlyHand API for tree logic that only needs totals.
class ValueOnlyHand {
public:
    ValueOnlyHand();
    explicit ValueOnlyHand(const std::vector<int>& values);

    int size() const;
    void add_value(int v);
    std::pair<ValueOnlyHand, ValueOnlyHand> split() const;
    bool is_same_rank_pair() const;       // Not determinable from values
    bool is_same_value_pair() const;
    bool is_natural_blackjack() const;
    bool is_bust() const;
    bool is_soft() const;
    bool is_soft_17() const;
    std::optional<int> get_best_value() const;
    int get_hard_value() const;
    std::string to_string() const;

    // Access underlying values
    const std::vector<int>& values() const;

private:
    void reset_cache() const;
    void compute_values() const;
    std::vector<int> values_;
    mutable std::optional<int> best_value_cache_;
    mutable int hard_value_cache_;
};

} // namespace blackjack
