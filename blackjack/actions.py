from enum import Enum, auto 


class TextEnum(Enum):
    @staticmethod
    def _generate_next_value_(name, start, count, last_values):
        return name


class PlayerAction(TextEnum):
    HIT = auto()
    STAND = auto()
    DOUBLE = auto()
    SPLIT = auto()
    SURRENDER = auto()
    DECLINE_EARLY_SURRENDER = auto()
    TAKE_INSURANCE = auto()
    REFUSE_INSURANCE = auto()
    

class DealerAction(TextEnum):
    CONFIRM_BLACKJACK = auto()
    CONFIRM_NO_BLACKJACK = auto()
