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
    BJRound(const BJRound& round);
    BJRound(const BJRound&& round);
    BJRound copy() const;
    void startRound(int bet_unit);
    BJStage getStage() const;
    bool needCard() const;
    bool needAction() const;
    bool needPlayerAction() const;
    bool needDealerAction() const;
    // When nullopt, all rank values 2..11 are possible (matching Python: get_possible_next_card_ranks)
    std::optional<std::vector<int>> getPossibleNextCardRanks() const;
    void takeCard(int value);
    void takeAction(PlayerAction action);
    void takeAction(DealerAction action);
    int getPlayerValue() const;
    // List available player actions in the current stage
    vector<PlayerAction> getAvailableActions() const;
    // Check if dealer expects to show blackjack (matching Python API)
    bool dealerExpectsToShowBlackjack() const;
    // String representation matching Python __str__
    std::string toString() const;

    // Public state used by game tree builder
    const BJRules* rules_;
    ValueOnlyHand dealer_hand;
    vector<ValueOnlyHand> player_hands;
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
    optional<PlayerAction> last_action;
    optional<int> last_card;

private:
    BJStage stage_;
    void takePlayerCard(int value);
    void takeDealerCard(int value);
    bool dealerCanHaveBj() const;
    void actionStand();
    void actionHit();
    void actionDouble();
    void actionSplit();
    void actionLateSurrender();
    void advanceToNextOrDealer();
    void calculateValue();
    void sameHandOrNextOrDealer();
    bool canEarlySurrender() const;
};

} // namespace blackjack
