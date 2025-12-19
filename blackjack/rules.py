from dataclasses import dataclass

import blackjack_cpp


@dataclass(frozen=True)
class BJRules:
    dealer_checks_blackjack: bool = True
    dealer_hits_soft_17: bool = False
    allow_late_surrender: bool = False
    allow_early_surrender_on_ten: bool = False
    allow_early_surrender_on_ace: bool = False
    allow_early_surrender_on_all: bool = False
    dealer_shows_card_on_surrender: bool = False
    allow_insurance_vs_ace: bool = True
    natural_blackjack_payout: float = 3 / 2
    surrender_payout: float = 1 / 2
    insurance_payout: float = 2 / 1
    max_splits_allowed: int = 1
    allow_action_on_split_aces: bool = True
    allow_double_after_split: bool = True
    allow_double_on_soft: bool = True
    allow_split_different_tens: bool = True

    def __str__(self):
        str_lines = ["BJRules: "]
        for field in self.__dataclass_fields__:
            value = getattr(self, field)
            str_lines.append(f" - {field}: {value}")
        return "\n".join(str_lines)

    def to_cpp(self) -> blackjack_cpp.CppBJRules:
        cpp_rules = blackjack_cpp.CppBJRules()
        for field in self.__dataclass_fields__:
            setattr(cpp_rules, field, getattr(self, field))
        return cpp_rules