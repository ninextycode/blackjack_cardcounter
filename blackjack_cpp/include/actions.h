#pragma once
#include <string>
#include <stdexcept>

using namespace std;

namespace blackjack {

enum class PlayerAction {
    HIT,
    STAND,
    DOUBLE,
    SPLIT,
    SURRENDER,
    DECLINE_EARLY_SURRENDER,
    TAKE_INSURANCE,
    REFUSE_INSURANCE
};

enum class DealerAction {
    CONFIRM_BLACKJACK,
    CONFIRM_NO_BLACKJACK
};

inline string to_string(PlayerAction a) {
    switch(a) {
        case PlayerAction::HIT: return "HIT";
        case PlayerAction::STAND: return "STAND";
        case PlayerAction::DOUBLE: return "DOUBLE";
        case PlayerAction::SPLIT: return "SPLIT";
        case PlayerAction::SURRENDER: return "SURRENDER";
        case PlayerAction::DECLINE_EARLY_SURRENDER: return "DECLINE_EARLY_SURRENDER";
        case PlayerAction::TAKE_INSURANCE: return "TAKE_INSURANCE";
        case PlayerAction::REFUSE_INSURANCE: return "REFUSE_INSURANCE";
    }
    return "UNKNOWN";
}

inline string to_string(DealerAction a) {
    switch(a) {
        case DealerAction::CONFIRM_BLACKJACK: return "CONFIRM_BLACKJACK";
        case DealerAction::CONFIRM_NO_BLACKJACK: return "CONFIRM_NO_BLACKJACK";
    }
    return "UNKNOWN";
}

inline PlayerAction player_action_from_string(const string& s) {
    if (s == to_string(PlayerAction::HIT)) return PlayerAction::HIT;
    if (s == to_string(PlayerAction::STAND)) return PlayerAction::STAND;
    if (s == to_string(PlayerAction::DOUBLE)) return PlayerAction::DOUBLE;
    if (s == to_string(PlayerAction::SPLIT)) return PlayerAction::SPLIT;
    if (s == to_string(PlayerAction::SURRENDER)) return PlayerAction::SURRENDER;
    if (s == to_string(PlayerAction::DECLINE_EARLY_SURRENDER)) return PlayerAction::DECLINE_EARLY_SURRENDER;
    if (s == to_string(PlayerAction::TAKE_INSURANCE)) return PlayerAction::TAKE_INSURANCE;
    if (s == to_string(PlayerAction::REFUSE_INSURANCE)) return PlayerAction::REFUSE_INSURANCE;
    throw invalid_argument("Unknown PlayerAction: " + s);
}

inline DealerAction dealer_action_from_string(const string& s) {
    if (s == to_string(DealerAction::CONFIRM_BLACKJACK)) return DealerAction::CONFIRM_BLACKJACK;
    if (s == to_string(DealerAction::CONFIRM_NO_BLACKJACK)) return DealerAction::CONFIRM_NO_BLACKJACK;
    throw invalid_argument("Unknown DealerAction: " + s);
}

} // namespace blackjack
