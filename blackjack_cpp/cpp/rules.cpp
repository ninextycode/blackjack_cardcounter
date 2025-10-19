#include "../include/rules.h"
#include <sstream>

namespace blackjack {

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
    ss << " no_natural_bj_on_split: " << no_natural_bj_on_split << "\n";
    return ss.str();
}

} // namespace blackjack
