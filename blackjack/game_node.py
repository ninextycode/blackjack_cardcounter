from abc import ABC, abstractmethod
from blackjack.blackjack_round import BJStage, BJRound
from blackjack.actions import DealerAction
import numpy as np


class BJTreeNode(AbstractBJTreeNode):
    """Exact probability tree node for blackjack game tree."""

    def __init__(
        self,
        bj_round, 
        shoe,
        parent=None,
        monte_carlo_depth=None,
        copy_data=True
    ):
        super().__init__(bj_round, shoe, parent, copy_data)
        self.monte_carlo_depth = monte_carlo_depth


    def create_child(self, child_bj_round, child_shoe, prob=0):
        if self.monte_carlo_depth is not None and (self._depth_from_root + 1) >= self.monte_carlo_depth:
            child = MonteCarloNode(child_bj_round, child_shoe, parent=self, copy_data=False)
        else:
            child = BJTreeNode(
                child_bj_round, child_shoe, parent=self, copy_data=False,
                monte_carlo_depth=self.monte_carlo_depth
            )
        self.children.append(child)
        self.children_prob.append(prob)


    def resample_player_cards(self, rerun_dealer_simulations=False):
        changed = False
        for child in self.children:
            child_changed = \
                child.resample_player_cards(rerun_dealer_simulations)
            if child_changed:
                changed = True
        if changed:
            self.has_completed_tree = False
            self.build_tree()
        return changed
    

    def _build_children_dealer_card(self):
        self._build_children_card()


    def _build_children_player_card(self):
        self._build_children_card()


    def _build_children_card(self):
        """Build children by enumerating all possible card values."""
        possible_card_ranks = self.bj_round.get_possible_next_card_ranks()
        possible_values = set([r.rank_value() for r in possible_card_ranks])
        card_value_probabilities = self.shoe.get_rank_value_probabilities(possible_values)
        
        for rv, p in card_value_probabilities.items():
            if p == 0:
                continue
            card = rv

            bj_round_copy = self.bj_round.copy()
            shoe_copy = self.shoe.copy()
            bj_round_copy.take_card(card)
            shoe_copy.burn_card(card)
            self.create_child(bj_round_copy, shoe_copy, p)




class MonteCarloNode(AbstractBJTreeNode):
    """Monte Carlo sampling node for blackjack game tree."""
    
    def __init__(self, bj_round, shoe, parent=None, copy_data=True, n_dealer_simulation_runs=100):
        super().__init__(bj_round, shoe, parent, copy_data)
        self.n_dealer_simulation_runs = n_dealer_simulation_runs


    def create_child(self, child_bj_round, child_shoe, prob=0):
        """Create a MonteCarloNode child."""
        child = MonteCarloNode(
            child_bj_round, child_shoe, parent=self, copy_data=False,
            n_dealer_simulation_runs=self.n_dealer_simulation_runs
        )
        self.children.append(child)
        self.children_prob.append(prob)


    def _build_children_player_card(self):
        """Build single child by sampling a card."""
        shoe_sample = self.shoe.copy()
        card_rank = shoe_sample.sample_and_burn_rank()
        card = Card(Rank.from_value(card_rank))
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_card(card)
        self.create_child(bj_round_copy, shoe_sample, 1)


    def _build_children_dealer_card(self):
        if self.bj_round.dealer_expects_to_show_blackjack():
            self._build_child_dealer_blackjack()    
        else:
            self._run_dealer_cards_simulations()
        self.has_built_children = True


    def _build_child_dealer_blackjack(self):
        """Build single child for dealer showing blackjack."""
        possible_ranks = self.bj_round.get_possible_next_card_ranks()
        rank_value = possible_ranks[0].rank_value()
        shoe_copy = self.shoe.copy()
        shoe_copy.burn_rank_value(rank_value)
        card = Card(Rank.from_value(rank_value))
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_card(card)
        self.create_child(bj_round_copy, shoe_copy, 1)


    def _run_dealer_cards_simulations(self):
        """Run Monte Carlo simulations for dealer play."""
        values = []
        for i in range(self.n_dealer_simulation_runs):
            bj_round_copy = self.bj_round.copy()
            shoe_copy = self.shoe.copy()

            # Simulate dealer cards until round over
            while not bj_round_copy.get_stage() == BJStage.ROUND_OVER:
                possible_ranks = bj_round_copy.get_possible_next_card_ranks()
                possible_values = {r.rank_value() for r in possible_ranks}
                card_rank = shoe_copy.sample_and_burn_rank(possible_values)

                card = Card(Rank.from_value(card_rank))
                bj_round_copy.take_card(card)
            
            # Collect results
            values.append(bj_round_copy.get_player_value())
        self.children = [SimulationResultNode(np.mean(values))]
        self.children_prob = [1]


    def resample_player_cards(self, rerun_dealer_simulations=False):
        """Resample player cards to get new Monte Carlo estimate."""
        if not self.tree_completed():
            raise RuntimeError("Cannot resample before completing the game tree")
        
        stage = self.bj_round.get_stage()

        changed = False
        if stage == BJStage.ROUND_OVER:
            changed = False
        elif stage == BJStage.DEALER_CARD:
            if not rerun_dealer_simulations:
                changed = False
            else:
                self._run_dealer_cards_simulations()
                changed = True
        elif stage == BJStage.PLAYER_CARD:
            # Rebuild the whole tree from this node
            self.children = []
            self.has_built_children = False
            changed = True
        elif stage in (
            BJStage.PLAYER_ACTION,
            BJStage.PLAYER_OFFERED_EARLY_SURRENDER,
            BJStage.PLAYER_OFFERED_INSURANCE,
            BJStage.DEALER_CHECK_BJ
        ):
            # For action nodes, recursively try to reach player card sample nodes and rebuild them
            # For the parent node itself, children stay the same but their values change
            for child in self.children:
                child_changed = child.resample_player_cards()
                if child_changed:
                    changed = True
        else:
            raise RuntimeError(f"Unexpected game stage {stage} during resampling")
        
        if changed:
            self.has_completed_tree = False
            self.build_tree()
            return True
        else:
            return False
