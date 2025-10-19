#pragma once
#include <vector>
#include <optional>
#include "hand.h"
#include "rules.h"
#include "actions.h"
#include "cards.h"

using namespace std;

namespace blackjack {

enum class BJStage {
    NOT_STARTED,
    PLAYER_OFFERED_INSURANCE,
    PLAYER_OFFERED_EARLY_SURRENDER,
    PLAYER_ACTION,
    PLAYER_CARD,
    DEALER_CHECK_BJ,
    DEALER_CARD,
    ROUND_OVER
};

class BJRound {
public:
    BJRound(const BJRules* rules);
    BJRound copy() const;
    void start_round(int bet_unit);
    BJStage get_stage() const;
    bool need_card() const;
    bool need_action() const;
    bool need_player_action() const;
    bool need_dealer_action() const;
    vector<Rank> get_possible_next_card_ranks() const;
    void take_card(const Card& card);
    void take_action(PlayerAction action);
    void take_action(DealerAction action);
    int get_player_value() const;
    // List available player actions in the current stage
    vector<PlayerAction> get_available_actions() const;

    // Public state used by game tree builder
    const BJRules* rules_;
    Hand dealer_hand;
    vector<Hand> player_hands;
    int active_hand_idx;
    bool dealer_checked_blackjack;
    bool dealer_has_bj_after_check;
    int bet_unit;
    int total_player_bet;
    int total_player_got;
    int player_value;
    int n_splits;
    optional<int> split_origin_idx;
    bool pending_double_bet;
    vector<bool> is_hand_in_progress;
    vector<int> hand_bets;
    int insurance_bet;
    bool surrendered;
    bool early_surrendered;

private:
    BJStage stage_;
    void _take_player_card(const Card& card);
    void _take_dealer_card(const Card& card);
    bool _dealer_can_have_bj() const;
    void _action_stand();
    void _action_hit();
    void _action_double();
    void _action_split();
    void _action_late_surrender();
    void _advance_to_next_or_dealer();
    void calculate_value();
    void _same_hand_or_next_or_dealer();
    bool _can_early_surrender() const;
};

} // namespace blackjack
