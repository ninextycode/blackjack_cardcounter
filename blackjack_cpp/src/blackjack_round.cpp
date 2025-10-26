#include "blackjack_round.h"
#include <stdexcept>
#include <algorithm>
#include <sstream>

namespace blackjack {

BJRound::BJRound(const BJRules* rules):
    rules_(rules),
    dealer_hand(),
    player_hands{ValueOnlyHand()},
    active_hand_idx(0),
    dealer_checked_blackjack(false), 
    dealer_has_bj_after_check(false),
    bet_unit(0),
    total_player_bet(0),
    total_player_got(0),
    player_value(0),
    n_splits(0),
    split_origin_idx(nullopt),
    pending_double_bet(false),
    is_hand_in_progress{true},
    hand_bets{0},
    insurance_bet(0),
    surrendered(false),
    early_surrendered(false),
    last_action(nullopt),
    last_card(nullopt),
    stage_(BJStage::NOT_STARTED) 
{

}

BJRound BJRound::copy() const {
    BJRound n(rules_);
    n.stage_ = stage_;
    n.dealer_hand = dealer_hand;
    n.player_hands = player_hands;
    n.active_hand_idx = active_hand_idx;
    n.dealer_checked_blackjack = dealer_checked_blackjack;
    n.dealer_has_bj_after_check = dealer_has_bj_after_check;
    n.bet_unit = bet_unit;
    n.total_player_bet = total_player_bet;
    n.total_player_got = total_player_got;
    n.player_value = player_value;
    n.n_splits = n_splits;
    n.split_origin_idx = split_origin_idx;
    n.pending_double_bet = pending_double_bet;
    n.is_hand_in_progress = is_hand_in_progress;
    n.hand_bets = hand_bets;
    n.insurance_bet = insurance_bet;
    n.surrendered = surrendered;
    n.early_surrendered = early_surrendered;
    n.last_action = last_action;
    n.last_card = last_card;
    return n;
}

void BJRound::startRound(int bet_unit_) {
    if (stage_ != BJStage::NOT_STARTED) throw runtime_error("Cannot restart the round");
    bet_unit = bet_unit_;
    hand_bets = {bet_unit};
    is_hand_in_progress = {true};
    stage_ = BJStage::PLAYER_CARD;
}

BJStage BJRound::getStage() const { return stage_; }
bool BJRound::needCard() const { return stage_==BJStage::PLAYER_CARD || stage_==BJStage::DEALER_CARD; }
bool BJRound::needAction() const { return needPlayerAction() || needDealerAction(); }
bool BJRound::needPlayerAction() const { return stage_==BJStage::PLAYER_ACTION || stage_==BJStage::PLAYER_OFFERED_INSURANCE || stage_==BJStage::PLAYER_OFFERED_EARLY_SURRENDER; }
bool BJRound::needDealerAction() const { return stage_==BJStage::DEALER_CHECK_BJ; }
std::optional<std::vector<int>> BJRound::getPossibleNextCardRanks() const {
    if (!(stage_ == BJStage::PLAYER_CARD || stage_ == BJStage::DEALER_CARD)) {
        return std::vector<int>{};
    }
    if (stage_ == BJStage::PLAYER_CARD) {
        return std::nullopt; // all values allowed
    }
    // DEALER_CARD
    if (dealer_hand.size() == 1) {
        if (dealer_checked_blackjack) {
            if (dealer_has_bj_after_check) {
                int up = dealer_hand.values()[0];
                if (up == 11) return std::vector<int>{10};
                if (up == 10) return std::vector<int>{11};
                throw runtime_error("Dealer must have blackjack but cards are not consistent");
            } else {
                int up = dealer_hand.values()[0];
                if (up == 11) {
                    std::vector<int> v; v.reserve(9);
                    for (int x = 2; x <= 11; ++x) if (x != 10) v.push_back(x);
                    return v;
                }
                if (up == 10) {
                    std::vector<int> v; v.reserve(8);
                    for (int x = 2; x <= 9; ++x) v.push_back(x);
                    return v;
                }
                throw runtime_error("Dealer cannot have blackjack but cards are not consistent");
            }
        } else {
            return std::nullopt; // no check yet, any value
        }
    }
    return std::nullopt;
}

void BJRound::takeCard(int value) {
    auto allowed = getPossibleNextCardRanks();
    if (allowed.has_value() && !allowed->empty()) {
        if (find(allowed->begin(), allowed->end(), value) == allowed->end()) {
            throw runtime_error("Invalid card value for current state");
        }
    }
    // Set last card and clear last action
    last_card = value;
    last_action = nullopt;
    
    if (stage_ == BJStage::PLAYER_CARD) takePlayerCard(value);
    else if (stage_ == BJStage::DEALER_CARD) takeDealerCard(value);
    else throw runtime_error("Cannot take card in current stage");
}

void BJRound::takePlayerCard(int value) {
    if (active_hand_idx >= (int)player_hands.size()) throw runtime_error("Invalid hand index");
    if (!is_hand_in_progress[active_hand_idx]) throw runtime_error("Hand is not in progress");
    ValueOnlyHand &hand = player_hands[active_hand_idx];
    if (hand.is_bust()) throw runtime_error("Hand is already bust");

    hand.add_value(value);

    if (hand.size() < 2) { stage_ = BJStage::PLAYER_CARD; return; }

    if (pending_double_bet || (hand.get_best_value().has_value() && hand.get_best_value().value()==21) || hand.is_bust()) {
        is_hand_in_progress[active_hand_idx] = false;
        if (pending_double_bet) pending_double_bet = false;
    }

    if (split_origin_idx.has_value()) {
        // split ace just got the second card, stand, depending on the rules
        if (!rules_->allow_action_on_split_aces && !hand.values().empty() && hand.values()[0] == 11) {
            is_hand_in_progress[active_hand_idx] = false;
        }

        if (active_hand_idx == split_origin_idx.value()) {
            active_hand_idx += 1; stage_ = BJStage::PLAYER_CARD; return;
        } else if (active_hand_idx == split_origin_idx.value() + 1) {
            active_hand_idx = split_origin_idx.value(); split_origin_idx = nullopt;
        } else {
            throw runtime_error("Illegal state during split");
        }
    }

    if (dealer_hand.size() == 0) stage_ = BJStage::DEALER_CARD; else sameHandOrNextOrDealer();
}

void BJRound::takeDealerCard(int value) {
    dealer_hand.add_value(value);

    if (surrendered) {
        if (!rules_->dealer_shows_card_on_surrender)
            throw runtime_error("Dealer does not show card on surrender yet card is given");
        calculateValue(); stage_ = BJStage::ROUND_OVER; return;
    }

    if (dealer_hand.size() == 1) {
        if (rules_->allow_insurance_vs_ace && value == 11) {
            stage_ = BJStage::PLAYER_OFFERED_INSURANCE;
        } else if (canEarlySurrender()) {
            stage_ = BJStage::PLAYER_OFFERED_EARLY_SURRENDER;
        } else if (rules_->dealer_checks_blackjack && dealerCanHaveBj()) {
            stage_ = BJStage::DEALER_CHECK_BJ;
        } else {
            sameHandOrNextOrDealer();
        }
        return;
    }

    bool all_player_nbj = true;
    for (auto &h: player_hands) { if (!h.is_natural_blackjack()) { all_player_nbj = false; break; } }

    if (all_player_nbj) { calculateValue(); stage_ = BJStage::ROUND_OVER; return; }

    auto dv = dealer_hand.get_best_value();
    if (!dv.has_value()) { calculateValue(); stage_ = BJStage::ROUND_OVER; return; }

    if (dv.value() >= 17) {
        if (dealer_hand.is_soft_17() && rules_->dealer_hits_soft_17) stage_ = BJStage::DEALER_CARD;
        else { calculateValue(); stage_ = BJStage::ROUND_OVER; }
    } else {
        stage_ = BJStage::DEALER_CARD;
    }
}

bool BJRound::dealerCanHaveBj() const {
    if (dealer_hand.size() != 1) return false;
    int up = dealer_hand.values()[0];
    return up == 11 || up == 10;
}

bool BJRound::canEarlySurrender() const {
    if (dealer_checked_blackjack || dealer_hand.size() != 1) return false;
    if (player_hands.size() > 1) return false;
    if (player_hands[0].is_natural_blackjack()) return false;
    int up = dealer_hand.values()[0];
    if (rules_->allow_early_surrender_on_all) return true;
    if (rules_->allow_early_surrender_on_ace && up == 11) return true;
    if (rules_->allow_early_surrender_on_ten && up == 10) return true;
    return false;
}

void BJRound::takeAction(PlayerAction action) {
    if (stage_ == BJStage::DEALER_CHECK_BJ) {
        throw runtime_error("Use dealer action for dealer check");
    }

    // Set last action and clear last card
    last_action = action;
    last_card = nullopt;

    if (stage_ == BJStage::PLAYER_OFFERED_INSURANCE) {
        if (action == PlayerAction::TAKE_INSURANCE) {
            insurance_bet = bet_unit / 2;
        } else if (action == PlayerAction::REFUSE_INSURANCE) {
            insurance_bet = 0;
        } else {
            throw runtime_error("Unexpected insurance action");
        }
        if (rules_->dealer_checks_blackjack) {
            stage_ = BJStage::DEALER_CHECK_BJ;
        } else {
            sameHandOrNextOrDealer();
        }
        return;
    }

    if (stage_ == BJStage::PLAYER_OFFERED_EARLY_SURRENDER) {
        if (action == PlayerAction::SURRENDER) {
            surrendered = true;
            early_surrendered = true;
            if (!rules_->dealer_shows_card_on_surrender) {
                calculateValue(); stage_ = BJStage::ROUND_OVER;
            } else {
                stage_ = BJStage::DEALER_CARD;
            }
        } else if (action == PlayerAction::DECLINE_EARLY_SURRENDER) {
            if (rules_->dealer_checks_blackjack && dealerCanHaveBj()) {
                stage_ = BJStage::DEALER_CHECK_BJ;
            } else {
                sameHandOrNextOrDealer();
            }
        } else {
            throw runtime_error("Unexpected early surrender action");
        }
        return;
    }

    if (stage_ != BJStage::PLAYER_ACTION) throw runtime_error("Cannot take player action in stage");
    if (!is_hand_in_progress[active_hand_idx]) throw runtime_error("Current hand has already been completed");

    if (action == PlayerAction::STAND) actionStand();
    else if (action == PlayerAction::HIT) actionHit();
    else if (action == PlayerAction::DOUBLE) actionDouble();
    else if (action == PlayerAction::SPLIT) actionSplit();
    else if (action == PlayerAction::SURRENDER) {
        surrendered = true; early_surrendered = false;
        if (!rules_->dealer_shows_card_on_surrender) { calculateValue(); stage_ = BJStage::ROUND_OVER; }
        else { stage_ = BJStage::DEALER_CARD; }
    }
    else throw runtime_error("Unexpected player action");
}

void BJRound::takeAction(DealerAction action) {
    if (stage_ != BJStage::DEALER_CHECK_BJ) throw runtime_error("Not in dealer check stage");
    dealer_checked_blackjack = true;
    if (action == DealerAction::CONFIRM_BLACKJACK) {
        dealer_has_bj_after_check = true;
        stage_ = BJStage::DEALER_CARD; // reveal hole card and finish
    } else if (action == DealerAction::CONFIRM_NO_BLACKJACK) {
        dealer_has_bj_after_check = false;
        sameHandOrNextOrDealer();
    } else {
        throw runtime_error("Unexpected dealer action");
    }
}

void BJRound::actionStand() {
    is_hand_in_progress[active_hand_idx] = false;
    advanceToNextOrDealer();
}

void BJRound::actionHit() { stage_ = BJStage::PLAYER_CARD; }

void BJRound::actionDouble() { 
    hand_bets[active_hand_idx] = 2 * bet_unit;
    pending_double_bet = true; 
    stage_ = BJStage::PLAYER_CARD; 
}

void BJRound::actionSplit() {
    // naive split implementation: split current hand into two hands
    if (active_hand_idx >= (int)player_hands.size()) throw runtime_error("Invalid hand index");
    auto h = player_hands[active_hand_idx];
    auto p = h.split();
    player_hands[active_hand_idx] = p.first;
    player_hands.insert(player_hands.begin() + active_hand_idx + 1, p.second);
    is_hand_in_progress.insert(is_hand_in_progress.begin() + active_hand_idx + 1, true);
    hand_bets.insert(hand_bets.begin() + active_hand_idx + 1, hand_bets[active_hand_idx]);
    split_origin_idx = active_hand_idx;
    n_splits += 1;
    stage_ = BJStage::PLAYER_CARD;
}

void BJRound::actionLateSurrender() {
    surrendered = true; early_surrendered = false;
    if (!rules_->dealer_shows_card_on_surrender) { calculateValue(); stage_ = BJStage::ROUND_OVER; }
    else { stage_ = BJStage::DEALER_CARD; }
}

void BJRound::advanceToNextOrDealer() {
    active_hand_idx += 1; sameHandOrNextOrDealer();
}

void BJRound::sameHandOrNextOrDealer() {
    // move to next in-progress hand or dealer card stage
    while (active_hand_idx < (int)is_hand_in_progress.size() && !is_hand_in_progress[active_hand_idx]) active_hand_idx++;
    if (active_hand_idx >= (int)is_hand_in_progress.size()) stage_ = BJStage::DEALER_CARD; else stage_ = BJStage::PLAYER_ACTION;
}

void BJRound::calculateValue() {
    int base_bets_total = 0;
    for (int b : hand_bets) base_bets_total += b;
    total_player_bet = base_bets_total + insurance_bet;

    bool dealer_has_blackjack = dealer_hand.is_natural_blackjack();

    int got_for_insurance = 0;
    if (insurance_bet > 0) {
        if (dealer_has_blackjack) {
            got_for_insurance = (int)(insurance_bet + insurance_bet * rules_->insurance_payout);
        }
    }

    if (surrendered && (!dealer_has_blackjack || early_surrendered)) {
        if ((int)player_hands.size() != 1) {
            throw runtime_error("Cannot surrender after split");
        }
        total_player_got = (int)(bet_unit * rules_->surrender_payout) + got_for_insurance;
        player_value = total_player_got - total_player_bet;
        return;
    }

    auto dealer_value = dealer_hand.get_best_value();
    int got_per_hand_sum = 0;
    for (size_t i = 0; i < player_hands.size(); ++i) {
        const auto &hand = player_hands[i];
        int bet = hand_bets[i];

        if (dealer_has_blackjack) {
            if (hand.is_natural_blackjack()) got_per_hand_sum += bet; else got_per_hand_sum += 0;
            continue;
        }

        if (((player_hands.size() == 1) || !rules_->no_natural_bj_on_split) && hand.is_natural_blackjack()) {
            got_per_hand_sum += (int)(bet * (1 + rules_->natural_blackjack_payout));
            continue;
        }

        auto pv = hand.get_best_value();
        if (!pv.has_value()) { got_per_hand_sum += 0; continue; }

        if (!dealer_value.has_value()) { got_per_hand_sum += 2 * bet; continue; }

        if (pv.value() > dealer_value.value()) got_per_hand_sum += 2 * bet;
        else if (pv.value() == dealer_value.value()) got_per_hand_sum += bet;
        else got_per_hand_sum += 0;
    }

    total_player_got = got_per_hand_sum + got_for_insurance;
    player_value = total_player_got - total_player_bet;
}

int BJRound::getPlayerValue() const { return player_value; }

bool BJRound::dealerExpectsToShowBlackjack() const {
    return dealer_hand.size() == 1 
        && dealer_checked_blackjack 
        && dealer_has_bj_after_check;
}

vector<PlayerAction> BJRound::getAvailableActions() const {
    vector<PlayerAction> actions;
    if (stage_ == BJStage::DEALER_CHECK_BJ) {
        return actions; // Dealer action handled separately
    }
    if (stage_ == BJStage::PLAYER_OFFERED_INSURANCE) {
        return { PlayerAction::TAKE_INSURANCE, PlayerAction::REFUSE_INSURANCE };
    }
    if (stage_ == BJStage::PLAYER_OFFERED_EARLY_SURRENDER) {
        return { PlayerAction::SURRENDER, PlayerAction::DECLINE_EARLY_SURRENDER };
    }
    if (stage_ != BJStage::PLAYER_ACTION) return actions;

    const ValueOnlyHand &hand = player_hands[active_hand_idx];
    if (!is_hand_in_progress[active_hand_idx]) throw runtime_error("Hand is not in progress");

    actions = { PlayerAction::STAND, PlayerAction::HIT };

    bool first_hand_action = hand.size() == 2;
    bool first_hand_first_action = first_hand_action && (player_hands.size() == 1);

    if (first_hand_action && !(hand.is_soft() && !rules_->allow_double_on_soft) && !(n_splits > 0 && !rules_->allow_double_after_split)) {
        actions.push_back(PlayerAction::DOUBLE);
    }

    if (first_hand_action && (rules_->max_splits_allowed < 0 || n_splits < rules_->max_splits_allowed)) {
        bool same_value = hand.is_same_value_pair();
        if (same_value && rules_->allow_split_different_tens) {
            actions.push_back(PlayerAction::SPLIT);
        } else if (same_value && !rules_->allow_split_different_tens) {
            bool same_rank = hand.is_same_rank_pair(); // always false for ValueOnlyHand
            if (same_rank) actions.push_back(PlayerAction::SPLIT);
        }
    }

    if (first_hand_first_action && rules_->allow_late_surrender) {
        actions.push_back(PlayerAction::SURRENDER);
    }

    return actions;
}

std::string BJRound::toString() const {
    std::string result;
    
    // Helper to convert hand values to string (like Python hand_to_str)
    auto hand_to_str = [](const ValueOnlyHand& hand) -> std::string {
        std::string s;
        for (int val : hand.values()) {
            s += value_to_rank_char(val);
        }
        return s;
    };
    
    // Add last action/card information
    if (last_action.has_value()) {
        result += "Last action: " + to_string(last_action.value()) + "\n";
    }
    if (last_card.has_value()) {
        result += "Last card: " + std::to_string(last_card.value()) + "\n";
    }
    
    // Dealer line
    std::string dealer_line = "Dealer";
    
    if (dealer_hand.size() == 0) {
        dealer_line += " (no cards)";
    } else if (dealer_hand.size() == 1) {
        dealer_line += " " + std::to_string(dealer_hand.values()[0]) + "X";
        
        if (insurance_bet > 0) {
            dealer_line += " (insurance " + std::to_string(insurance_bet) + ")";
        }
        
        // Add blackjack check result if dealer has checked
        if (dealer_checked_blackjack) {
            if (dealer_has_bj_after_check) {
                dealer_line += " (checked - bj)";
            } else {
                dealer_line += " (checked - no bj)";
            }
        }
    } else {
        // Dealer has 2+ cards
        std::string card_str;
        for (int val : dealer_hand.values()) {
            card_str += std::to_string(val);
        }
        
        auto dealer_value = dealer_hand.get_best_value();
        if (dealer_value.has_value()) {
            dealer_line += " " + card_str + " (" + std::to_string(dealer_value.value()) + ")";
        } else {
            dealer_line += " " + card_str + " (bust)";
        }
    }
    
    result += dealer_line + "\n";
    
    // Player line(s)
    if (surrendered) {
        const auto& hand = player_hands[0];
        std::string card_str = hand_to_str(hand);
        std::string surrender_type = early_surrendered ? "early surrender" : "late surrender";
        result += "Player " + card_str + " (" + surrender_type + ")\n";
    } else {
        // Normal play - show all hands
        std::vector<std::string> player_lines;
        
        for (size_t i = 0; i < player_hands.size(); ++i) {
            const auto& hand = player_hands[i];
            if (hand.size() == 0) continue;
            
            std::string parts;
            
            std::string card_str = hand_to_str(hand);
            parts += card_str;
            
            auto value = hand.get_best_value();
            if (value.has_value()) {
                std::string stand_str = "";
                if (!is_hand_in_progress[i]) {
                    stand_str = " - stand";
                }
                
                int hard_value = hand.get_hard_value();
                if (!(rules_->no_natural_bj_on_split && n_splits > 0) && hand.is_natural_blackjack()) {
                    parts += "(bj)";
                } else if (hard_value != value.value()) {
                    parts += "(" + std::to_string(value.value()) + "/" + std::to_string(hard_value) + stand_str + ")";
                } else {
                    parts += "(" + std::to_string(value.value()) + stand_str + ")";
                }
            } else {
                parts += "(bust)";
            }
            
            int bet = hand_bets[i];
            parts += "[$" + std::to_string(bet) + "]";
            
            player_lines.push_back(parts);
        }
        
        if (!player_lines.empty()) {
            result += "Player ";
            for (size_t i = 0; i < player_lines.size(); ++i) {
                if (i > 0) result += " | ";
                result += player_lines[i];
            }
            result += "\n";
        }
    }
    
    // Final totals if round is over
    if (stage_ == BJStage::ROUND_OVER) {
        result += "Player total bet: " + std::to_string(total_player_bet) + "\n";
        result += "Player total payout: " + std::to_string(total_player_got) + "\n";
        int net = player_value;
        std::string net_sign = (net > 0) ? "+" : "";
        result += "Player net: " + net_sign + std::to_string(net) + "\n";
    }
    
    // Remove trailing newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    
    return result;
}

} // namespace blackjack
