import unittest
from blackjack.cards import Rank, Card
from blackjack.edge import build_root_node
from blackjack.floor_ceil_node import DecisionNode
from blackjack.rules import BJRules
from blackjack.blackjack_round import BJRound
from blackjack_py import ProbabilisticRankShoe


class TestRoundEv(unittest.TestCase):
    def setUp(self):
        self.rules = BJRules(
            dealer_checks_blackjack=True,
            dealer_hits_soft_17=False,
            allow_late_surrender=False,
            allow_early_surrender_on_ten=False,
            allow_early_surrender_on_ace=False,
            allow_early_surrender_on_all=False,
            dealer_shows_card_on_surrender=False,
            allow_insurance_vs_ace=True,
            natural_blackjack_payout=3/2,
            surrender_payout=1/2,
            insurance_payout=2/1,
            max_splits_allowed=1,
            allow_action_on_split_aces=True,
            allow_double_after_split=True,
            allow_double_on_soft=True,
            allow_split_different_tens=True
        )

        self.shoe = ProbabilisticRankShoe(n_decks=6, seed=42)
        self.bj_round = BJRound(self.rules)
        self.bj_round.start_round(100)


    def test_aa_v_a(self):
        cards = [
            Card(Rank.ACE),
            Card(Rank.ACE),
            Card(Rank.ACE)  
        ]
        cards = [c.rank_value() for c in cards]
        
        self.bj_round.take_card(cards[0])
        self.bj_round.take_card(cards[1])
        self.bj_round.take_card(cards[2])

        self.shoe.burn_rank_value(cards[0])
        self.shoe.burn_rank_value(cards[1])
        self.shoe.burn_rank_value(cards[2])

        root_node = DecisionNode(self.bj_round, self.shoe)
        root_node.build_tree()
        root_node.convert_to_full_up_to_depth(depth=float("inf"))

        value = root_node.get_value()
        correct_value = 10.487
        self.assertAlmostEqual(value, correct_value, places=3)

    def test_aa_v_six(self):
        cards = [
            Card(Rank.ACE),
            Card(Rank.ACE),
            Card(Rank.SIX)  
        ]
        cards = [c.rank_value() for c in cards]
        
        self.bj_round.take_card(cards[0])
        self.bj_round.take_card(cards[1])
        self.bj_round.take_card(cards[2])

        self.shoe.burn_rank_value(cards[0])
        self.shoe.burn_rank_value(cards[1])
        self.shoe.burn_rank_value(cards[2])

        root_node = DecisionNode(self.bj_round, self.shoe)
        root_node.build_tree()
        root_node.convert_to_full_up_to_depth(depth=float("inf"))

        value = root_node.get_value()
        correct_value = 99.122
        self.assertAlmostEqual(value, correct_value, places=3)

