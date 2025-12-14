import numpy as np
import pandas as pd
import os
from blackjack.actions import PlayerAction, DealerAction
from blackjack.blackjack_round import BJRound, BJStage
from blackjack_py import ProbabilisticRankShoe, RandomSampler
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
        hard_alt_path = os.path.join(folder, "hard_deviations.csv")
        soft_alt_path = os.path.join(folder, "soft_deviations.csv")
        pair_alt_path = os.path.join(folder, "pair_deviations.csv")
        insurance_path = os.path.join(folder, "insurance.csv")
        self.hard_alt_table = pd.read_csv(hard_alt_path)
        self.soft_alt_table = pd.read_csv(soft_alt_path)
        self.pair_alt_table = pd.read_csv(pair_alt_path)
        self.insurance_table = pd.read_csv(insurance_path)
        fill_tc_limits(self.hard_alt_table)
        fill_tc_limits(self.soft_alt_table)
        fill_tc_limits(self.pair_alt_table)
        fill_tc_limits(self.insurance_table)


    def split_action(self, hand_value, dealer_value, true_count):
        """
        returns an action that should override split/no-split decision
        returns none if should stick to basic strategy
        """
        if hand_value == 12:
            card_value = 11
        else:
            card_value = hand_value // 2

        true_count_int = int(true_count)
        idx = (
            (self.pair_alt_table["card_value"] == card_value)
            & (self.pair_alt_table["dealer"] == dealer_value)
        )
        row = self.pair_alt_table.loc[idx, ["tc_min", "tc_max", "action"]].values
        if len(row) > 0:
            tc_min, tc_max, actions_str = row[0]
            if tc_min <= true_count_int <= tc_max:
                assert len(actions_str) == 1
                return actions_str_to_list(actions_str[0])
        return None
        

    def get_deviated_actions(self, player_value, is_soft, is_pair, dealer_value, true_count):
        actions = []

        true_count_int = int(true_count)
        insurance_tc_min, insurance_tc_max = self.insurance_table.loc[0, ["tc_min", "tc_max"]].values
        if insurance_tc_min <= true_count_int <= insurance_tc_max:
            actions.append(PlayerAction.TAKE_INSURANCE)
        
        if is_pair:
            split_actions = self.split_action(player_value, dealer_value, true_count)
            if split_actions is not None:
                actions.extend(split_actions)
                
        if is_soft:
            dev_table = self.soft_alt_table
        else:
            dev_table = self.hard_alt_table
        
        idx = (
            dev_table["player"] == player_value
            & (dev_table["dealer"] == dealer_value)
        )
        row = dev_table.loc[idx, ["tc_min", "tc_max", "action"]].values

        if len(row) > 0:
            tc_min, tc_max, actions_str = row[0]
            if tc_min <= true_count_int <= tc_max:
                actions.extend(actions_str_to_list(actions_str))

        return actions


class BasicStrategy:
    def __init__(self, folder):
        hard_path = os.path.join(folder, "s17_hard.csv")
        soft_path = os.path.join(folder, "s17_soft.csv")
        split_path = os.path.join(folder, "s17_split.csv")
        self.hard_table = pd.read_csv(hard_path)
        self.soft_table = pd.read_csv(soft_path)
        self.split_table = pd.read_csv(split_path)


    def should_split(self, hand_value, dealer_value):
        if hand_value == 12:
            card_value = 11
        else:
            card_value = hand_value // 2

        val_series = self.split_table.loc[
            self.split_table["card_value"] == card_value, str(dealer_value)
        ]
        assert len(val_series) == 1
        return val_series.iat[0] == "p" 


    def get_actions(self, player_value, is_soft, is_pair, dealer_value, true_count):
        actions = [PlayerAction.REFUSE_INSURANCE]
        if is_pair:
            if self.should_split(player_value, dealer_value):
                actions.append(PlayerAction.SPLIT)
        
        if is_soft:
            table = self.soft_table
        else:
            table = self.hard_table

        actions_str_entry = table.loc[
            table["player"] == player_value, 
            str(dealer_value)
        ]
        assert len(actions_str_entry) == 1
        actions.extend(actions_str_to_list(actions_str_entry.iat[0]))
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