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


    def _run_dealer_cards_simulations(self):
        """Run Monte Carlo simulations for dealer play."""
        values = []
        for i in range(self.n_dealer_sim_runs):
            bj_round_copy = self.bj_round.copy()
            shoe_copy = self.shoe.copy()

            # Simulate dealer cards until round over
            while not bj_round_copy.get_stage() == BJStage.ROUND_OVER:
                possible_values = bj_round_copy.get_possible_next_card_ranks()
                card = shoe_copy.sample_and_burn_rank(possible_values)
                bj_round_copy.take_card(card)
            
            # Collect results
            values.append(bj_round_copy.get_player_value())
        self.children = [SimulationResultNode(np.mean(values), self)]
        self.children_prob = [1]


  

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