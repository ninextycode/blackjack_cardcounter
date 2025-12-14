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
    Hand(const vector<Card>& cards);
    int size() const;
    void add_card(const Card& c);
    pair<Hand, Hand> split() const;
    bool is_same_rank_pair() const;
    bool is_same_value_pair() const;
    bool is_natural_blackjack() const;
    bool is_bust() const;
    bool is_soft() const;
    bool is_soft_17() const;
    optional<int> get_best_value() const;
    int get_hard_value() const;
    string to_string() const;
    // Accessors for read-only access to underlying cards
    const vector<Card>& cards() const;
    const Card& front() const;
    const Card& operator[](size_t i) const;

private:
    void reset_cache() const;
    void compute_best_value() const;
    vector<Card> cards_;
    mutable optional<int> best_value_cache_;
    mutable int hard_value_cache_;
};

// Hand-like class storing integer soft values (Ace as 11, others 2-10).
// Mirrors Python ValueOnlyHand API for tree logic that only needs totals.
class ValueOnlyHand {
public:
    ValueOnlyHand(bool natural_blackjack_possible = true);
    explicit ValueOnlyHand(const vector<int>& values, bool natural_blackjack_possible = true);
    explicit ValueOnlyHand(vector<int>&& values, bool natural_blackjack_possible = true);

    int size() const;
    void add_value(int v);
    void add_values(const vector<int>& v);
    int pop_value(size_t n = 1);
    pair<ValueOnlyHand, ValueOnlyHand> split() const;
    bool is_same_rank_pair() const;       // Not determinable from values
    bool is_same_value_pair() const;
    bool is_natural_blackjack() const;
    bool is_bust() const;
    bool is_soft() const;
    bool is_soft_17() const;
    optional<int> get_best_value() const;
    int get_hard_value() const;
    string to_string() const;

    // Access underlying values
    const vector<int>& values() const;

private:
    void reset_cache() const;
    void compute_values() const;
    vector<int> values_;
    mutable optional<int> best_value_cache_;
    mutable int hard_value_cache_;
    bool natural_blackjack_possible_;
};

} // namespace blackjack
