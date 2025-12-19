import numpy as np
import pandas as pd
import os
from blackjack.actions import PlayerAction, DealerAction
from blackjack.blackjack_round import BJRound, BJStage
from blackjack_cpp import ProbabilisticRankShoe, RandomSampler
from blackjack.rules import BJRules
import time
import random
import tqdm


def fill_tc_limits(table):
    table["tc_min"] = table["tc_min"].fillna(-np.inf)
    table["tc_max"] = table["tc_max"].fillna(np.inf)

str_to_action = {
    "s": PlayerAction.STAND,
    "h": PlayerAction.HIT,
    "d": PlayerAction.DOUBLE,
    "p": PlayerAction.SPLIT,
}

def actions_str_to_list(actions_str):
    return [str_to_action[action_char] for action_char in actions_str]


class DeviationStrategy:
    def __init__(self, folder):
        hard_alt_table = pd.read_csv(os.path.join(folder, "hard_deviations.csv"))
        soft_alt_table = pd.read_csv(os.path.join(folder, "soft_deviations.csv"))
        pair_alt_table = pd.read_csv(os.path.join(folder, "pair_deviations.csv"))
        insurance_table = pd.read_csv(os.path.join(folder, "insurance.csv"))
        fill_tc_limits(hard_alt_table)
        fill_tc_limits(soft_alt_table)
        fill_tc_limits(pair_alt_table)
        fill_tc_limits(insurance_table)
        
        # Build lookup dicts: (player, dealer) -> (tc_min, tc_max, actions_str)
        self._hard_dev = {
            (row["player"], row["dealer"]): (row["tc_min"], row["tc_max"], row["action"])
            for _, row in hard_alt_table.iterrows()
        }
        self._soft_dev = {
            (row["player"], row["dealer"]): (row["tc_min"], row["tc_max"], row["action"])
            for _, row in soft_alt_table.iterrows()
        }
        self._pair_dev = {
            (row["card_value"], row["dealer"]): (row["tc_min"], row["tc_max"], row["action"])
            for _, row in pair_alt_table.iterrows()
        }
        # Insurance bounds
        self._insurance_tc_min = insurance_table.loc[0, "tc_min"]
        self._insurance_tc_max = insurance_table.loc[0, "tc_max"]

    def split_action(self, hand_value, dealer_value, true_count):
        """
        returns an action that should override split/no-split decision
        returns none if should stick to basic strategy
        """
        card_value = 11 if hand_value == 12 else hand_value // 2
        true_count_int = int(true_count)
        
        dev = self._pair_dev.get((card_value, dealer_value))
        if dev is not None:
            tc_min, tc_max, actions_str = dev
            if tc_min <= true_count_int <= tc_max:
                return actions_str_to_list(actions_str)
        return None

    def get_deviated_actions(self, player_value, is_soft, is_pair, dealer_value, true_count):
        actions = []
        true_count_int = int(true_count)
        
        if self._insurance_tc_min <= true_count_int <= self._insurance_tc_max:
            actions.append(PlayerAction.TAKE_INSURANCE)
        
        if is_pair:
            split_actions = self.split_action(player_value, dealer_value, true_count)
            if split_actions is not None:
                actions.extend(split_actions)
        
        lookup = self._soft_dev if is_soft else self._hard_dev
        dev = lookup.get((player_value, dealer_value))
        if dev is not None:
            tc_min, tc_max, actions_str = dev
            if tc_min <= true_count_int <= tc_max:
                actions.extend(actions_str_to_list(actions_str))

        return actions


class BasicStrategy:
    def __init__(self, folder):
        hard_table = pd.read_csv(os.path.join(folder, "s17_hard.csv"))
        soft_table = pd.read_csv(os.path.join(folder, "s17_soft.csv"))
        split_table = pd.read_csv(os.path.join(folder, "s17_split.csv"))
        
        # Build lookup dicts: (player/card_value, dealer) -> actions_str or bool
        self._hard = {
            (row["player"], d): row[str(d)]
            for _, row in hard_table.iterrows()
            for d in range(2, 12)
        }
        self._soft = {
            (row["player"], d): row[str(d)]
            for _, row in soft_table.iterrows()
            for d in range(2, 12)
        }
        self._split = {
            (row["card_value"], d): row[str(d)] == "p"
            for _, row in split_table.iterrows()
            for d in range(2, 12)
        }

    def get_actions(self, player_value, is_soft, is_pair, dealer_value, true_count):
        actions = [PlayerAction.REFUSE_INSURANCE]
        if is_pair:
            card_value = 11 if player_value == 12 else player_value // 2
            if self._split[(card_value, dealer_value)]:
                actions.append(PlayerAction.SPLIT)
        
        lookup = self._soft if is_soft else self._hard
        actions.extend(actions_str_to_list(lookup[(player_value, dealer_value)]))
        return actions
    

class DeviatedBasicStrategy:
    def __init__(self, basic_strategy_folder, deviation_strategy_folder):
        self.basic_strategy = BasicStrategy(basic_strategy_folder)
        self.deviation_strategy = DeviationStrategy(deviation_strategy_folder)


    def get_actions(self, player_value, is_soft, is_pair, dealer_value, true_count):
        dev_actions = self.deviation_strategy.get_deviated_actions(
            player_value, is_soft, is_pair, dealer_value, true_count
        )
        basic_actions = self.basic_strategy.get_actions(
            player_value, is_soft, is_pair, dealer_value, true_count
        )
        # dev actions get priority
        return dev_actions + basic_actions
    

class CardCounter:
    @staticmethod
    def from_rank_count(rank_count, n_decks=0):
        counter = CardCounter(n_decks)
        # we know the cards remaining in the shoe, not the ones removed
        # at the start the shoe was balanced - 
        # 5 ranks for -1 / 3 ranks for 0 / 5 ranks for +1 
        # shoe is positively unbalanced to the degree there are more T-A than 2-6 
        n_26 = 0
        n_TA = 0
        for r, c in rank_count.items():
            counter.remaining_cards += c 
            if 2 <= r <= 6:
                n_26 += c
            elif r >= 10:
                n_TA += c
        counter.running_count = n_TA - n_26
        return counter

    def __init__(self, n_decks, current_penetration=0):
        self.n_decks = n_decks
        self.remaining_cards = n_decks * 52
        self.remaining_cards -= int(round(self.remaining_cards * current_penetration))
        self.running_count = 0
    
    def reset(self):
        self.remaining_cards = self.n_decks * 52
        self.running_count = 0

    def update_count(self, rank_value):
        # Tag values: 2–6 = +1, 7–9 = 0, 10–A = −1
        if rank_value < 7:
            self.running_count += 1
        elif rank_value > 9:
            self.running_count -= 1
        self.remaining_cards -= 1
    
    def get_true_count(self):
        remaining_decks = self.remaining_cards / 52
        if remaining_decks <= 0:
            return None
        return self.running_count / remaining_decks
    
    def get_integer_tc(self):
        # use integer TCs rounded toward zero
        # 1.9 -> 1 | -1.8 -> -1
        true_count = self.get_true_count()
        if true_count is None:
            return None
        return int(true_count)