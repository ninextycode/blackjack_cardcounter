#include "../include/blackjack_round.h"
#include <stdexcept>

namespace blackjack {

BJRound::BJRound(const BJRules* rules):
    rules_(rules),
    dealer_hand(),
    player_hands{Hand()},
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
    return n;
}

void BJRound::start_round(int bet_unit_) {
    if (stage_ != BJStage::NOT_STARTED) throw runtime_error("Cannot restart the round");
    bet_unit = bet_unit_;
    hand_bets = {bet_unit};
    is_hand_in_progress = {true};
    stage_ = BJStage::PLAYER_CARD;
}

BJStage BJRound::get_stage() const { return stage_; }
bool BJRound::need_card() const { return stage_==BJStage::PLAYER_CARD || stage_==BJStage::DEALER_CARD; }
bool BJRound::need_action() const { return need_player_action() || need_dealer_action(); }
bool BJRound::need_player_action() const { return stage_==BJStage::PLAYER_ACTION || stage_==BJStage::PLAYER_OFFERED_INSURANCE || stage_==BJStage::PLAYER_OFFERED_EARLY_SURRENDER; }
bool BJRound::need_dealer_action() const { return stage_==BJStage::DEALER_CHECK_BJ; }

vector<Rank> BJRound::get_possible_next_card_ranks() const {
    if (!(stage_==BJStage::PLAYER_CARD || stage_==BJStage::DEALER_CARD)) return {};
    if (stage_==BJStage::PLAYER_CARD) {
        return { Rank::ACE, Rank::TWO, Rank::THREE, Rank::FOUR, Rank::FIVE, Rank::SIX, Rank::SEVEN, Rank::EIGHT, Rank::NINE, Rank::TEN, Rank::JACK, Rank::QUEEN, Rank::KING };
    }
    // Dealer card logic simplified: always allow all ranks for now (game_tree will filter by probabilities)
    return { Rank::ACE, Rank::TWO, Rank::THREE, Rank::FOUR, Rank::FIVE, Rank::SIX, Rank::SEVEN, Rank::EIGHT, Rank::NINE, Rank::TEN, Rank::JACK, Rank::QUEEN, Rank::KING };
}

void BJRound::take_card(const Card& card) {
    // basic validation omitted for brevity
    if (stage_==BJStage::PLAYER_CARD) _take_player_card(card);
    else if (stage_==BJStage::DEALER_CARD) _take_dealer_card(card);
    else throw runtime_error("Cannot take card in current stage");
}

void BJRound::_take_player_card(const Card& card) {
    if (active_hand_idx >= (int)player_hands.size()) throw runtime_error("Invalid hand index");
    if (!is_hand_in_progress[active_hand_idx]) throw runtime_error("Hand is not in progress");
    Hand &hand = player_hands[active_hand_idx];
    if (hand.is_bust()) throw runtime_error("Hand is already bust");
    hand.add_card(card);
    if (hand.size() < 2) { stage_ = BJStage::PLAYER_CARD; return; }
    if (pending_double_bet || (hand.get_best_value().has_value() && hand.get_best_value().value()==21) || hand.is_bust()) {
        is_hand_in_progress[active_hand_idx] = false;
        if (pending_double_bet) pending_double_bet = false;
    }
    if (split_origin_idx.has_value()) {
        if (!rules_->allow_action_on_split_aces && hand.size()>=1 && hand.get_best_value().has_value() && hand.get_best_value().value()==21 && hand.cards().size()>0 && hand[0].rank==Rank::ACE) {
            // approximate: if split aces, make it inactive
            is_hand_in_progress[active_hand_idx] = false;
        }
        if (active_hand_idx == split_origin_idx.value()) {
            active_hand_idx += 1; stage_ = BJStage::PLAYER_CARD; return;
        } else if (active_hand_idx == split_origin_idx.value()+1) {
            active_hand_idx = split_origin_idx.value(); split_origin_idx = nullopt;
        } else {
            throw runtime_error("Illegal state during split");
        }
    }
    if (dealer_hand.size()==0) stage_ = BJStage::DEALER_CARD; else _same_hand_or_next_or_dealer();
}

void BJRound::_take_dealer_card(const Card& card) {
    dealer_hand.add_card(card);
    if (surrendered) {
        if (!rules_->dealer_shows_card_on_surrender) throw runtime_error("Dealer does not show card on surrender yet card is given");
        calculate_value(); stage_ = BJStage::ROUND_OVER; return;
    }
    if (dealer_hand.size()==1) {
        if (rules_->allow_insurance_vs_ace && card.rank==Rank::ACE) stage_ = BJStage::PLAYER_OFFERED_INSURANCE;
        else if (_can_early_surrender()) stage_ = BJStage::PLAYER_OFFERED_EARLY_SURRENDER;
        else if (rules_->dealer_checks_blackjack && _dealer_can_have_bj()) stage_ = BJStage::DEALER_CHECK_BJ;
        else _same_hand_or_next_or_dealer();
        return;
    }
    bool all_player_nbj = true;
    for (auto &h: player_hands) if (!h.is_natural_blackjack()) { all_player_nbj = false; break; }
    if (all_player_nbj) { calculate_value(); stage_ = BJStage::ROUND_OVER; return; }
    auto dv = dealer_hand.get_best_value();
    if (!dv.has_value()) { calculate_value(); stage_ = BJStage::ROUND_OVER; return; }
    if (dv.value() >= 17) {
        if (dealer_hand.is_soft_17() && rules_->dealer_hits_soft_17) stage_ = BJStage::DEALER_CARD;
        else { calculate_value(); stage_ = BJStage::ROUND_OVER; }
    } else {
        stage_ = BJStage::DEALER_CARD;
    }
}

bool BJRound::_dealer_can_have_bj() const {
    if (dealer_hand.size() != 1) return false;
    auto up_card = dealer_hand.front();
    return up_card.rank==Rank::ACE || up_card.rank_value()==10;
}

bool BJRound::_can_early_surrender() const {
    if (dealer_checked_blackjack || dealer_hand.size() != 1) return false;
    if (player_hands.size() > 1) return false;
    if (player_hands[0].is_natural_blackjack()) return false;
    auto up_card = dealer_hand.front();
    if (rules_->allow_early_surrender_on_all) return true;
    if (rules_->allow_early_surrender_on_ace && up_card.rank == Rank::ACE) return true;
    if (rules_->allow_early_surrender_on_ten && up_card.rank_value() == 10) return true;
    return false;
}

void BJRound::take_action(PlayerAction action) {
    if (stage_==BJStage::DEALER_CHECK_BJ) throw runtime_error("Use dealer action for dealer check");
    if (stage_==BJStage::PLAYER_OFFERED_INSURANCE) {
        if (action==PlayerAction::TAKE_INSURANCE) { insurance_bet = bet_unit/2; }
        stage_ = BJStage::PLAYER_ACTION; return;
    }
    if (stage_==BJStage::PLAYER_OFFERED_EARLY_SURRENDER) {
        if (action==PlayerAction::SURRENDER) { _action_late_surrender(); }
        else if (action==PlayerAction::DECLINE_EARLY_SURRENDER) { stage_ = BJStage::PLAYER_ACTION; }
        return;
    }
    if (stage_ != BJStage::PLAYER_ACTION) throw runtime_error("Cannot take player action in stage");
    if (!is_hand_in_progress[active_hand_idx]) throw runtime_error("Hand already completed");
    if (action==PlayerAction::STAND) _action_stand();
    else if (action==PlayerAction::HIT) _action_hit();
    else if (action==PlayerAction::DOUBLE) _action_double();
    else if (action==PlayerAction::SPLIT) _action_split();
    else if (action==PlayerAction::SURRENDER) _action_late_surrender();
    else throw runtime_error("Unexpected player action");
}

void BJRound::take_action(DealerAction action) {
    if (stage_ != BJStage::DEALER_CHECK_BJ) throw runtime_error("Not in dealer check stage");
    if (action==DealerAction::CONFIRM_BLACKJACK) { dealer_has_bj_after_check = true; dealer_checked_blackjack = true; stage_ = BJStage::ROUND_OVER; }
    else { dealer_has_bj_after_check = false; dealer_checked_blackjack = true; stage_ = BJStage::PLAYER_ACTION; }
}

void BJRound::_action_stand() {
    is_hand_in_progress[active_hand_idx] = false;
    _same_hand_or_next_or_dealer();
}

void BJRound::_action_hit() { stage_ = BJStage::PLAYER_CARD; }

void BJRound::_action_double() { pending_double_bet = true; stage_ = BJStage::PLAYER_CARD; }

void BJRound::_action_split() {
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

void BJRound::_action_late_surrender() {
    surrendered = true; stage_ = BJStage::ROUND_OVER; total_player_got -= (int)(bet_unit * rules_->surrender_payout);
}

void BJRound::_advance_to_next_or_dealer() {
    active_hand_idx += 1; _same_hand_or_next_or_dealer();
}

void BJRound::_same_hand_or_next_or_dealer() {
    // move to next in-progress hand or dealer card stage
    while (active_hand_idx < (int)is_hand_in_progress.size() && !is_hand_in_progress[active_hand_idx]) active_hand_idx++;
    if (active_hand_idx >= (int)is_hand_in_progress.size()) stage_ = BJStage::DEALER_CARD; else stage_ = BJStage::PLAYER_ACTION;
}

void BJRound::calculate_value() {
    // simplified: compute player's total outcome as sum of hand best values for now
    int total = 0;
    for (auto &h: player_hands) {
        auto v = h.get_best_value();
        if (!v.has_value()) total -= 1; else total += *v;
    }
    player_value = total;
}

int BJRound::get_player_value() const { return player_value; }

vector<PlayerAction> BJRound::get_available_actions() const {
    vector<PlayerAction> actions;
    if (stage_==BJStage::PLAYER_OFFERED_INSURANCE) {
        actions = { PlayerAction::TAKE_INSURANCE, PlayerAction::REFUSE_INSURANCE };
        return actions;
    }
    if (stage_==BJStage::PLAYER_OFFERED_EARLY_SURRENDER) {
        actions = { PlayerAction::SURRENDER, PlayerAction::DECLINE_EARLY_SURRENDER };
        return actions;
    }
    if (stage_!=BJStage::PLAYER_ACTION) return actions; // none

    // Base actions
    actions = { PlayerAction::STAND, PlayerAction::HIT };

    // Double if allowed by rules (simple heuristic: allow unless bust or already doubled)
    const Hand &h = player_hands[active_hand_idx];
    if (!h.is_bust()) {
        actions.push_back(PlayerAction::DOUBLE);
    }

    // Split if current hand is pair and splits remain
    if (n_splits < rules_->max_splits_allowed) {
        if (rules_->allow_split_different_tens ? h.is_same_value_pair() : h.is_same_rank_pair()) {
            actions.push_back(PlayerAction::SPLIT);
        }
    }

    // Late surrender if rules allow and hand still active
    if (rules_->allow_late_surrender && is_hand_in_progress[active_hand_idx]) {
        actions.push_back(PlayerAction::SURRENDER);
    }

    return actions;
}

} // namespace blackjack
