from blackjack.actions import PlayerAction
from blackjack.game_node import AbstractBJTreeNode, SimulationResultNode
from blackjack.blackjack_round import BJStage
import numpy as np
from collections import deque
from blackjack.tree_utils import iterate_nodes_by_levels



class MixedNodeOld(AbstractBJTreeNode):
    def __init__(
            self,
            bj_round,
            shoe,
            max_hand_size_full_enum,
            player_card_initial_samples=1,
            n_dealer_sim_runs=100,
            parent=None,
            copy_data=True
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data
        )
        self.max_hand_size_full_enum = max_hand_size_full_enum
        self.n_dealer_sim_runs = n_dealer_sim_runs
        self.player_card_initial_samples = player_card_initial_samples
        self._active_hand_size = None
        hands = self.bj_round.player_hands
        hand_idx = self.bj_round.active_hand_idx
        if hand_idx < len(hands):
            active_hand = self.bj_round.player_hands[hand_idx]
            self._active_hand_size = active_hand.size()

    def is_past_three_initial_cards(self):
        has_dealer_card = self.bj_round.dealer_hand.size() > 0
        has_player_two_cards = len(self.bj_round.player_hands) > 1 \
            or self.bj_round.player_hands[0].size() >= 2
        return has_dealer_card and has_player_two_cards
    
    def create_child(self, child_bj_round, child_shoe, transition_event, prob=0):
        child = MixedNode(
            child_bj_round, child_shoe, parent=self, copy_data=False,
            max_hand_size_full_enum=self.max_hand_size_full_enum,
            player_card_initial_samples=self.player_card_initial_samples,
            n_dealer_sim_runs=self.n_dealer_sim_runs,
        )
        self.children.append(child)
        self.children_prob.append(prob)
        self.children_events.append(transition_event)
        return child
    

    def _build_children_player_card(self):
        """Build children for card dealing stages. Must be implemented by subclasses."""
        if self._active_hand_size > self.max_hand_size_full_enum:
            # perform sampling
            for _ in range(self.player_card_initial_samples):
                self._add_player_card_sample()
        else:
            # do full enumeration
            self._build_full_children_player_card()

        
    def _build_full_children_player_card(self):
        """Build children by enumerating all possible card values."""
        possible_card_ranks = self.bj_round.get_possible_next_card_ranks()
        if possible_card_ranks is None:
            possible_values = range(2, 12)
        else:
            possible_values = set(possible_card_ranks)
        card_value_probabilities = self.shoe.get_rank_value_probabilities(possible_values)
        
        for rv, p in card_value_probabilities.items():
            if p == 0:
                continue
            card = rv

            bj_round_copy = self.bj_round.copy()
            shoe_copy = self.shoe.copy()
            bj_round_copy.take_card(card)
            shoe_copy.burn_rank_value(card)
            self.create_child(bj_round_copy, shoe_copy, card, p)


    def _build_children_dealer_card(self):
        if self.bj_round.dealer_expects_to_show_blackjack():
            self._build_child_dealer_blackjack()    
        else:
            self._run_dealer_cards_simulations()
        self.has_built_children = True


    def _build_child_dealer_blackjack(self):
        """Build single child for dealer showing blackjack."""
        possible_ranks = self.bj_round.get_possible_next_card_ranks()
        # possible_ranks is either [10] or [11]
        rank_value = possible_ranks[0]
        shoe_copy = self.shoe.copy()
        shoe_copy.burn_rank_value(rank_value)
        card = rank_value
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_card(card)
        self.create_child(bj_round_copy, shoe_copy, card, 1)


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


    def add_player_card_sample(self):
        if not self.tree_completed():
            raise RuntimeError("Cannot add player card sample to incomplete tree.")
        
        updated_node = None
        for lvl, node in iterate_nodes_by_levels(self):
            if not isinstance(node, MixedNode):
                continue
            if node.bj_round.get_stage() != BJStage.PLAYER_CARD:
                continue

            added_to_node = node._add_player_card_sample()
            if added_to_node:
                updated_node = node
                break
        
        if updated_node is None:
            return False

        while updated_node is not self:
            updated_node.recompute_tree_value()
            updated_node = updated_node.parent
        self.recompute_tree_value()
        return True


    def _add_player_card_sample(self):
        """ 
            Add a sampled child for player card stage.
            This function merely adds a new child and updates probabilities.
            It does not recurse into children and does not recompute tree value.
        """
        assert self.bj_round.get_stage() == BJStage.PLAYER_CARD
        old_values = set(self.children_events)
        possible_ranks = self.bj_round.get_possible_next_card_ranks()
        if possible_ranks is None:
            possible_values = range(2, 12)
        else:
            possible_values = set(possible_ranks)
        new_values = [
            rv for rv in possible_values if rv not in old_values
        ]
        if len(new_values) == 0:
            return False

        shoe_sample = self.shoe.copy()
        card = shoe_sample.sample_rank(new_values)
        # calculate new probabilities while conditioning on existing samples 
        # but before burning the sampled card 
        new_probabilities = shoe_sample.get_rank_value_probabilities(
            old_values | {card}
        )
        shoe_sample.burn_rank_value(card)
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_card(card)
        new_child = self.create_child(bj_round_copy, shoe_sample, card, 1)
        self.children_prob = [new_probabilities[rv] for rv in self.children_events]
        return True
    


    def convert_to_full_next_layer(self):
        """
            On each path from root to leaves, convert the first sample to full enumeration
        """
        if not self.tree_completed():
            raise RuntimeError(
                "Cannot convert player card sample to full enum in an incomplete tree."
            )

        stage = self.bj_round.get_stage()
        children_changed = False

        if stage in (BJStage.ROUND_OVER, BJStage.DEALER_CARD):
            return False
        
        elif stage != BJStage.PLAYER_CARD:
            for ch in self.children:
                child_changed = ch.convert_to_full_next_layer()
                if child_changed:
                    children_changed = True
        
        else:  # PLAYER_CARD
            # if there can be an extension from sample to full enumeration here - do it here
            # if not, go it children
            self_children_added = self._convert_from_sample_to_full()
            if not self_children_added:
                for ch in self.children:
                    child_changed = ch.convert_to_full_next_layer()
                    if child_changed:
                        children_changed = True
            else:
                children_changed = True     
            
        if children_changed:
            self.recompute_tree_value()
            return True
        else:
            return False
        

    def _convert_from_sample_to_full(self):
        """
            Convert a sampled player card node to full enumeration.
            This function merely adds the missing children and updates probabilities.
            It does not recurse into children and does not recompute tree value.
        """
        assert self.bj_round.get_stage() == BJStage.PLAYER_CARD
        # Rebuild children with full enumeration
        old_values = list(self.children_events)
        possible_ranks = self.bj_round.get_possible_next_card_ranks()
        if possible_ranks is None:
            possible_values = range(2, 12)
        else:
            possible_values = set(possible_ranks)
        new_values = [
            rv for rv in possible_values if rv not in old_values
        ]
        if len(new_values) == 0:
            return False
        
        new_probabilities = \
            self.shoe.get_rank_value_probabilities(
                possible_ranks
            )

        for new_rank_value in new_values:
            shoe_sample = self.shoe.copy()
            shoe_sample.burn_rank_value(new_rank_value)

            card = new_rank_value
            bj_round_copy = self.bj_round.copy()
            bj_round_copy.take_card(card)
            new_child = self.create_child(bj_round_copy, shoe_sample, card)
        
        self.children_prob = []
        for rv in old_values:
            self.children_prob.append(new_probabilities[rv])
        for rv in new_values:
            self.children_prob.append(new_probabilities[rv])
        
        return True


    def single_node_from_sample_to_full(self):
        if not self.tree_completed():
            raise RuntimeError(
                "Cannot convert player card sample to full enum in an incomplete tree."
            )
        
        # BFS: find the most shallow PLAYER_CARD MixedNode that can be converted
        updated_node = None
        for lvl, node in iterate_nodes_by_levels(self):
            if not isinstance(node, MixedNode):
                continue
            if node.bj_round.get_stage() != BJStage.PLAYER_CARD:
                continue
            added = node._convert_from_sample_to_full()
            if added:
                updated_node = node
                break

        if updated_node is None:
            return False

        # Recompute values up from updated_node to self
        n = updated_node
        while n is not self:
            n.recompute_tree_value()
            n = n.parent
        self.recompute_tree_value()
        return True
        

    def convert_to_full_up_to_depth(self, depth):
        if not self.tree_completed():
            raise RuntimeError(
                "Cannot convert player card sample to full enum in an incomplete tree."
            )

        if depth < 0:
            return False
        
        children_changed = False

        stage = self.bj_round.get_stage()
        
        if stage not in (BJStage.DEALER_CARD, BJStage.ROUND_OVER):
            for ch in self.children:
                child_changed = ch.convert_to_full_up_to_depth(depth - 1)
                if child_changed:
                    children_changed = True

        if stage == BJStage.PLAYER_CARD:
            added_children_self = self._convert_from_sample_to_full()
            if added_children_self:
                children_changed = True

        if children_changed:
            self.recompute_tree_value()
            return True
        return False    
    