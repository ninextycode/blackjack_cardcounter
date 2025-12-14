#include "rules.h"
#include <sstream>

namespace blackjack {

BJRules getDefaultRules() {
    BJRules rules;
    rules.dealer_checks_blackjack = true;
    rules.dealer_hits_soft_17 = false;
    rules.allow_late_surrender = false;
    rules.allow_early_surrender_on_ten = false;
    rules.allow_early_surrender_on_ace = false;
    rules.allow_early_surrender_on_all = false;
    rules.dealer_shows_card_on_surrender = false;
    rules.allow_insurance_vs_ace = true;
    rules.natural_blackjack_payout = 3.0 / 2.0;
    rules.surrender_payout = 1.0 / 2.0;
    rules.insurance_payout = 2.0 / 1.0;
    rules.max_splits_allowed = 1;
    rules.allow_action_on_split_aces = true;
    rules.allow_double_after_split = true;
    rules.allow_double_on_soft = true;
    rules.allow_split_different_tens = true;
    rules.split_order_reversed = false;
    return rules;
}

std::string BJRules::to_string() const {
    std::ostringstream ss;
    ss << "BJRules:\n";
    ss << " dealer_checks_blackjack: " << dealer_checks_blackjack << "\n";
    ss << " dealer_hits_soft_17: " << dealer_hits_soft_17 << "\n";
    ss << " allow_late_surrender: " << allow_late_surrender << "\n";
    ss << " allow_early_surrender_on_ten: " << allow_early_surrender_on_ten << "\n";
    ss << " allow_early_surrender_on_ace: " << allow_early_surrender_on_ace << "\n";
    ss << " allow_early_surrender_on_all: " << allow_early_surrender_on_all << "\n";
    ss << " dealer_shows_card_on_surrender: " << dealer_shows_card_on_surrender << "\n";
    ss << " allow_insurance_vs_ace: " << allow_insurance_vs_ace << "\n";
    ss << " natural_blackjack_payout: " << natural_blackjack_payout << "\n";
    ss << " surrender_payout: " << surrender_payout << "\n";
    ss << " insurance_payout: " << insurance_payout << "\n";
    ss << " max_splits_allowed: " << max_splits_allowed << "\n";
    ss << " allow_action_on_split_aces: " << allow_action_on_split_aces << "\n";
    ss << " allow_double_after_split: " << allow_double_after_split << "\n";
    ss << " allow_double_on_soft: " << allow_double_on_soft << "\n";
    ss << " allow_split_different_tens: " << allow_split_different_tens << "\n";
    ss << " split_order_reversed: " << split_order_reversed << "\n";
    return ss.str();
}

} // namespace blackjack
