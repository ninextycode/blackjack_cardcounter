#pragma once
#include <string>

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

} // namespace blackjack
