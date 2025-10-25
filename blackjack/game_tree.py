from abc import ABC, abstractmethod
from blackjack.blackjack_round import BJStage, BJRound
from blackjack.actions import DealerAction
import numpy as np


class AbstractBJTreeNode(ABC):
    """Abstract base class for blackjack game tree nodes."""
    
    def __init__(self, bj_round, shoe, parent=None, copy_data=True):
        self.bj_round : BJRound = bj_round.copy() if copy_data else bj_round
        self.shoe = shoe.copy() if copy_data else shoe
        self.parent = parent
        self.children = []
        self.children_prob = []
        self.children_events = []
        self.value = 0
        self.has_built_children = False
        self.has_completed_tree = False
        self._depth_from_root = 0
        if self.parent is not None:
            self._depth_from_root = self.parent._depth_from_root + 1 
    

    def set_as_root(self):
        """Set this node as the root of the tree."""
        self.parent = None
        self._update_depth_from_root()


    def _update_depth_from_root(self):
        """Update depth from root for this node and its children."""
        if self.parent is None:
            self._depth_from_root = 0
        else:
            self._depth_from_root = self.parent._depth_from_root + 1 
        for child in self.children:
            child._update_depth_from_root()


    def tree_completed(self):
        """Check if tree construction is complete."""
        return self.has_completed_tree


    def children_trees_completed(self):
        """Check if all children have completed their trees."""
        return self.has_built_children and all([child.tree_completed() for child in self.children])


    @abstractmethod
    def create_child(self, child_bj_round, child_shoe, event, prob=0):
        """Create a child node. Must be implemented by subclasses."""
        pass


    def build_tree(self):
        """Build complete tree without depth limit."""
        self.build_tree_layer(depth=None)

    def recompute_tree_value(self):
        """
            Recompute the values of the tree.

            If children were not built yet, throws an exception
            
            If new children were added, their subtrees will also be built.
            
            If children have not changed, no new children will be created and their values will be reused.
        """
        if not self.has_built_children:
            raise RuntimeError("Cannot recompute tree value before building children")

        for child in self.children:
            child.build_tree()
        if self.children_trees_completed():
            self._compute_node_value()
            self.has_completed_tree = True  
        else:
            raise RuntimeError("Cannot recompute tree value, some children have incomplete trees")


    def build_tree_layer(self, depth):
        """Build tree up to a specified depth."""
        if depth == 0 or self.tree_completed():
            return
        
        if not self.has_built_children:
            self.build_children()
        
        for child in self.children:
            child_depth = depth - 1 if depth is not None else None
            child.build_tree_layer(child_depth)
    
        if self.children_trees_completed():
            self._compute_node_value()
            self.has_completed_tree = True  


    def _compute_node_value(self):
        """Complete the node based on its stage type."""
        stage = self.bj_round.get_stage()
        
        if stage in (
            BJStage.PLAYER_ACTION,
            BJStage.PLAYER_OFFERED_EARLY_SURRENDER,
            BJStage.PLAYER_OFFERED_INSURANCE
        ):
            self._compute_action_node_value()
        elif stage in (
            BJStage.DEALER_CARD,
            BJStage.PLAYER_CARD,
            BJStage.DEALER_CHECK_BJ
        ):
            self._compute_chance_node_value()
        elif stage == BJStage.ROUND_OVER:
            self._compute_terminal_node_value()
        else:
            raise RuntimeError(f"Unexpected game stage {stage} after building children")


    def _compute_action_node_value(self):
        """Complete an action node by selecting best action."""
        values = [child.get_value() for child in self.children]
        action_id = np.argmax(values)
        for i in range(len(self.children)):
            self.children_prob[i] = 0
        self.children_prob[action_id] = 1
        self.value = values[action_id]


    def _compute_chance_node_value(self):
        """Complete a chance node by computing expected value."""
        values = [child.get_value() for child in self.children]
        self.value = sum([p * v for p, v in zip(self.children_prob, values)])


    def _compute_terminal_node_value(self):
        """Complete a terminal node by setting its value."""
        self.value = self.bj_round.get_player_value()


    def build_children(self):
        """Build child nodes based on current game stage."""
        stage = self.bj_round.get_stage()

        if stage == BJStage.ROUND_OVER:
            # value assignment should not be handled here 
            # this is a function only for building children
            pass  
        elif stage == BJStage.PLAYER_CARD:
            self._build_children_player_card()
        elif stage == BJStage.DEALER_CARD:
            self._build_children_dealer_card()
        elif stage in (
            BJStage.PLAYER_ACTION,
            BJStage.PLAYER_OFFERED_EARLY_SURRENDER,
            BJStage.PLAYER_OFFERED_INSURANCE
        ):
            self._build_children_player_action()
        elif stage == BJStage.DEALER_CHECK_BJ:
            self._build_children_dealer_check_bj()
        else:
            raise RuntimeError(f"Unexpected game stage {stage}")

        self.has_built_children = True


    @abstractmethod
    def _build_children_player_card(self):
        """Build children for card dealing stages. Must be implemented by subclasses."""
        pass


    @abstractmethod
    def _build_children_dealer_card(self):
        """Build children for card dealing stages. Must be implemented by subclasses."""
        pass


    def _build_children_dealer_check_bj(self):
        """Build children for dealer blackjack check."""
        dealer_value = self.bj_round.dealer_hand.get_best_value()
        rv_prob = self.shoe.get_rank_value_probabilities()

        shoe_no_bj = self.shoe.copy()
        if dealer_value == 11:
            p_dealer_blackjack = rv_prob[10]
            shoe_no_bj.lock_dealer_card_not_ten()
        elif dealer_value == 10:
            p_dealer_blackjack = rv_prob[11]
            shoe_no_bj.lock_dealer_card_not_ace()
        else:
            raise RuntimeError("Dealer cannot check blackjack with value other than 10 or 11")
        
        bj_round_dealer_bj = self.bj_round.copy()
        bj_round_dealer_bj.take_action(DealerAction.CONFIRM_BLACKJACK)

        bj_round_no_dealer_bj = self.bj_round.copy()
        bj_round_no_dealer_bj.take_action(DealerAction.CONFIRM_NO_BLACKJACK)

        self.create_child(
            bj_round_dealer_bj, self.shoe.copy(), 
            DealerAction.CONFIRM_BLACKJACK, p_dealer_blackjack
        )
        self.create_child(
            bj_round_no_dealer_bj, shoe_no_bj,
            DealerAction.CONFIRM_NO_BLACKJACK, 1 - p_dealer_blackjack
        )


    def _build_children_player_action(self):
        """Build children for player action stages."""
        actions = self.bj_round.get_available_actions()
        for a in actions:
            bj_round_copy = self.bj_round.copy()
            bj_round_copy.take_action(a)
            self.create_child(bj_round_copy, self.shoe.copy(), a)


    def get_value(self):
        """Get the computed value of this node."""
        if not self.tree_completed():
            raise RuntimeError("Cannot get value before completing the tree")
        return self.value



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
        

class SimulationResultNode:
    """Node to hold simulation results without building children."""
    def __init__(self, value, parent=None):
        self.value = value
        self.parent = parent
        self.children = []
    
    def build_tree_layer(self, depth):
        pass

    def tree_completed(self):
        return True
    
    def children_trees_completed(self):
        return True

    def get_value(self):
        return self.value