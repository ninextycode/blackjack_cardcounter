#pragma once
#include <vector>
#include <optional>
#include <memory>
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

inline string to_string(BJStage stage) {
    switch(stage) {
        case BJStage::NOT_STARTED: return "NOT_STARTED";
        case BJStage::PLAYER_OFFERED_INSURANCE: return "PLAYER_OFFERED_INSURANCE";
        case BJStage::PLAYER_OFFERED_EARLY_SURRENDER: return "PLAYER_OFFERED_EARLY_SURRENDER";
        case BJStage::PLAYER_ACTION: return "PLAYER_ACTION";
        case BJStage::PLAYER_CARD: return "PLAYER_CARD";
        case BJStage::DEALER_CHECK_BJ: return "DEALER_CHECK_BJ";
        case BJStage::DEALER_CARD: return "DEALER_CARD";
        case BJStage::ROUND_OVER: return "ROUND_OVER";
    }
    return "UNKNOWN_BJ_STAGE";
}


class BJRound {
public:
    BJRound(shared_ptr<const BJRules> rules);
    BJRound(const BJRound& round) = default;
    BJRound(BJRound&& round) = default;
    BJRound& operator=(const BJRound& round) = default;
    BJRound& operator=(BJRound&& round) = default;

    void startRound(int bet_unit = 100);
    BJStage getStage() const;
    const ValueOnlyHand& getActivePlayerHand() const;
    bool needCard() const;
    bool needAction() const;
    bool needPlayerAction() const;
    bool needDealerAction() const;
    // When nullopt, all rank values 2..11 are possible (matching Python: get_possible_next_card_ranks)
    optional<vector<int>> getPossibleNextCardRanks() const;
    void takeCard(int value);
    void takeAction(PlayerAction action);
    void takeAction(DealerAction action);
    int getPlayerValue() const;
    // List available player actions in the current stage
    vector<PlayerAction> getAvailableActions() const;
    // Check if dealer expects to show blackjack (matching Python API)
    bool dealerExpectsToShowBlackjack() const;
    // String representation matching Python __str__
    string toString(bool with_last_action = false) const;

    /**
     * startFakeSplit - For game tree modelling purposes.
     * Simulates a split where the first hand gets card value 2 and stands.
     * This is used to model split scenarios in game trees where we need to
     * track the first hand separately but don't want to build its full tree.
     */
    void startFakeSplit();
    void finalizeFakeSplit(int card_value);

    /**
     * setStageForTreeBuilding - For tree building: allow setting stage directly.
     * This is needed when creating rounds from split hand stack.
     */
    // void setStageForTreeBuilding(BJStage stage);

    // Public state used by game tree builder
    shared_ptr<const BJRules> rules_;
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
