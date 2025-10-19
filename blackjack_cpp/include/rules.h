#pragma once
#include <string>

using namespace std;

namespace blackjack {

struct BJRules {
    bool dealer_checks_blackjack = true;
    bool dealer_hits_soft_17 = false;
    bool allow_late_surrender = false;
    bool allow_early_surrender_on_ten = false;
    bool allow_early_surrender_on_ace = false;
    bool allow_early_surrender_on_all = false;
    bool dealer_shows_card_on_surrender = false;
    bool allow_insurance_vs_ace = true;
    double natural_blackjack_payout = 1.5;
    double surrender_payout = 0.5;
    double insurance_payout = 2.0;
    int max_splits_allowed = 1;
    bool allow_action_on_split_aces = true;
    bool allow_double_after_split = true;
    bool allow_double_on_soft = true;
    bool allow_split_different_tens = true;
    bool no_natural_bj_on_split = true;

    string to_string() const;
};

} // namespace blackjack
