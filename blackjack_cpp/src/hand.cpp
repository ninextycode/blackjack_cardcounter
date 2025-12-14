#include "hand.h"
#include <numeric>
#include <sstream>

namespace blackjack {

Hand::Hand(): cards_(), best_value_cache_(std::nullopt), hard_value_cache_(0) {}

Hand::Hand(const std::vector<Card>& cards): cards_(cards), best_value_cache_(std::nullopt), hard_value_cache_(0) {}

int Hand::size() const { return (int)cards_.size(); }

void Hand::add_card(const Card& c) {
    cards_.push_back(c);
    reset_cache();
}

std::pair<Hand,Hand> Hand::split() const {
    if (cards_.size() != 2) throw std::runtime_error("Can only split 2-card hand");
    return { Hand(std::vector<Card>{cards_[0]}), Hand(std::vector<Card>{cards_[1]}) };
}

bool Hand::is_same_rank_pair() const {
    if (cards_.size() != 2) return false;
    return cards_[0].rank == cards_[1].rank;
}

bool Hand::is_same_value_pair() const {
    if (cards_.size() != 2) return false;
    return cards_[0].rank_value(false) == cards_[1].rank_value(false);
}

bool Hand::is_natural_blackjack() const {
    auto v = get_best_value();
    return cards_.size() == 2 && v && *v == 21;
}

bool Hand::is_bust() const {
    auto v = get_best_value();
    return !v.has_value();
}

bool Hand::is_soft() const {
    int hard = get_hard_value();
    auto best = get_best_value();
    if (!best) return false;
    return *best != hard;
}

bool Hand::is_soft_17() const {
    bool has_ace = false;
    int hard_no_ace = 0;
    for (auto &c: cards_) {
        if (c.rank == Rank::ACE) has_ace = true;
        hard_no_ace += c.rank_value(false);
    }
    // The python code subtracts 1 then adds 11 for a single ace; replicate logic
    return has_ace && (hard_no_ace - 1 + 11 == 17);
}

void Hand::reset_cache() const {
    best_value_cache_ = std::nullopt;
    hard_value_cache_ = 0;
}

void Hand::compute_best_value() const {
    int soft_value = 0;
    int n_soft_aces = 0;
    for (auto &c: cards_) {
        soft_value += c.rank_value(true);
        if (c.rank == Rank::ACE) n_soft_aces += 1;
    }
    int value = soft_value;
    while (value > 21 && n_soft_aces > 0) {
        value -= 10;
        n_soft_aces -= 1;
    }
    if (value > 21) {
        best_value_cache_ = std::nullopt;
    } else {
        best_value_cache_ = value;
    }
    int hv = 0;
    for (auto &c: cards_) hv += c.rank_value(false);
    hard_value_cache_ = hv;
}

std::optional<int> Hand::get_best_value() const {
    if (!best_value_cache_.has_value() && hard_value_cache_==0 && cards_.empty()) {
        // empty hand -> best value 0
        best_value_cache_ = 0;
        hard_value_cache_ = 0;
        return best_value_cache_;
    }
    if (!best_value_cache_.has_value() || hard_value_cache_==0) compute_best_value();
    return best_value_cache_;
}

int Hand::get_hard_value() const {
    if (best_value_cache_.has_value() && hard_value_cache_==0) compute_best_value();
    if (hard_value_cache_==0) {
        int hv=0; for (auto &c: cards_) hv+=c.rank_value(false); return hv;
    }
    return hard_value_cache_;
}

std::string Hand::to_string() const {
    std::ostringstream ss;
    ss << "[";
    for (auto &c: cards_) ss << c.str();
    ss << "] (";
    auto bv = get_best_value();
    if (!bv.has_value()) ss << "bust"; else ss << *bv;
    ss << ")";
    return ss.str();
}

const std::vector<Card>& Hand::cards() const { return cards_; }
const Card& Hand::front() const { return cards_.front(); }
const Card& Hand::operator[](size_t i) const { return cards_.at(i); }

// ---------------- ValueOnlyHand ----------------

ValueOnlyHand::ValueOnlyHand(bool natural_blackjack_possible)
    : values_(), best_value_cache_(std::nullopt), hard_value_cache_(0), 
      natural_blackjack_possible_(natural_blackjack_possible) {}

ValueOnlyHand::ValueOnlyHand(const std::vector<int>& values, bool natural_blackjack_possible)
    : values_(values), best_value_cache_(std::nullopt), hard_value_cache_(0),
      natural_blackjack_possible_(natural_blackjack_possible) {}

ValueOnlyHand::ValueOnlyHand(std::vector<int>&& values, bool natural_blackjack_possible)
    : values_(std::move(values)), best_value_cache_(std::nullopt), hard_value_cache_(0),
      natural_blackjack_possible_(natural_blackjack_possible) {}

int ValueOnlyHand::size() const { return (int)values_.size(); }

void ValueOnlyHand::add_value(int v) {
    values_.push_back(v);
    reset_cache();
}

void ValueOnlyHand::add_values(const vector<int>& v) {
    values_.insert(values_.end(), v.begin(), v.end());
    reset_cache();
}

int ValueOnlyHand::pop_value(size_t n) {
    int v = values_.back();;
    for (size_t i = 0; i < n; ++i) {
        values_.pop_back();
    }
    reset_cache();
    return v;
}


std::pair<ValueOnlyHand, ValueOnlyHand> ValueOnlyHand::split() const {
    if (values_.size() != 2) throw std::runtime_error("Can only split 2-value hand");
    // natural blackjack impossible after split
    return {
        ValueOnlyHand(std::vector<int>{values_[0]}, false),
        ValueOnlyHand(std::vector<int>{values_[1]}, false)
    };
}

bool ValueOnlyHand::is_same_rank_pair() const {
    // Not determinable from integer values only.
    return false;
}

bool ValueOnlyHand::is_same_value_pair() const {
    if (values_.size() != 2) return false;
    return values_[0] == values_[1];
}

bool ValueOnlyHand::is_natural_blackjack() const {
    auto v = get_best_value();
    return natural_blackjack_possible_ && values_.size() == 2 && v && *v == 21;
}

bool ValueOnlyHand::is_bust() const {
    auto v = get_best_value();
    return !v.has_value();
}

bool ValueOnlyHand::is_soft() const {
    auto best = get_best_value();
    if (!best) return false;
    return *best != get_hard_value();
}

bool ValueOnlyHand::is_soft_17() const {
    bool has_ace = false;
    int hard_no_ace = 0;
    for (int v : values_) {
        if (v == 11) has_ace = true;
        // hard contribution counts ace as 1
        hard_no_ace += (v == 11 ? 1 : v);
    }
    // hard_no_ace already counted ace as 1; emulate python: (hard-1)+11 == 17
    if (!has_ace) return false;
    return (hard_no_ace - 1 + 11) == 17;
}

void ValueOnlyHand::reset_cache() const {
    best_value_cache_ = std::nullopt;
    hard_value_cache_ = 0;
}

void ValueOnlyHand::compute_values() const {
    int soft = 0;
    int n_soft_aces = 0;
    for (int v : values_) {
        soft += v;
        if (v == 11) n_soft_aces += 1;
    }
    int value = soft;
    while (value > 21 && n_soft_aces > 0) {
        value -= 10;
        n_soft_aces -= 1;
    }
    if (value > 21) best_value_cache_ = std::nullopt; else best_value_cache_ = value;

    int hard = 0;
    for (int v : values_) hard += (v == 11 ? 1 : v);
    hard_value_cache_ = hard;
}

std::optional<int> ValueOnlyHand::get_best_value() const {
    if (!best_value_cache_.has_value()) compute_values();
    return best_value_cache_;
}

int ValueOnlyHand::get_hard_value() const {
    if (hard_value_cache_ == 0 && !values_.empty()) compute_values();
    return hard_value_cache_;
}

std::string ValueOnlyHand::to_string() const {
    std::ostringstream ss;
    // Format matching Python: vals(value) or vals(hard/value) if soft
    // Show "A" for ace (11), numbers for others
    for (size_t i = 0; i < values_.size(); ++i) {
        if (i) ss << ",";
        if (values_[i] == 11) {
            ss << "A";
        } else {
            ss << values_[i];
        }
    }
    auto bv = get_best_value();
    int hv = get_hard_value();
    if (!bv.has_value()) {
        ss << "(bust)";
    } else {
        if (*bv == hv) {
            ss << "(" << *bv << ")";
        } else {
            ss << "(" << hv << "/" << *bv << ")";
        }
    }
    return ss.str();
}

const std::vector<int>& ValueOnlyHand::values() const { return values_; }

} // namespace blackjack
