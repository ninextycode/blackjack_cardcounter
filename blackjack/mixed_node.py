from typing import override
from blackjack.actions import DealerAction, PlayerAction
from blackjack.abstract_node import AbstractBJTreeNode, FloorCeilValueNode, ValueNode, run_dealer_cards_simulation
from blackjack.blackjack_round import BJRound, BJStage
import numpy as np
from collections import deque
from blackjack.rules import BJRules
from blackjack.tree_utils import iterate_nodes_by_levels
from dataclasses import replace


class FloorCeilNode(AbstractBJTreeNode):
    def __init__(
            self,
            bj_round,
            shoe,
            max_hand_size_full_enum,
            player_card_initial_samples=1,
            n_dealer_sim_runs=100,
            parent=None,
            copy_data=True,
            n_splits_happened=0
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
        self.n_splits_happened = n_splits_happened


    def is_past_three_initial_cards(self):
        has_dealer_card = self.bj_round.dealer_hand.size() > 0
        has_player_two_cards = len(self.bj_round.player_hands) > 1 \
            or self.bj_round.player_hands[0].size() >= 2
        return has_dealer_card and has_player_two_cards


    def create_child(self, child_bj_round, child_shoe, transition_event, prob=0):
        child = FloorCeilNode(
            child_bj_round, child_shoe, parent=self, copy_data=False,
            max_hand_size_full_enum=self.max_hand_size_full_enum,
            player_card_initial_samples=self.player_card_initial_samples,
            n_dealer_sim_runs=self.n_dealer_sim_runs,
            n_splits_happened=self.n_splits_happened
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


    def _build_children_player_action(self):
        """Build children for player action stages."""
        actions = self.bj_round.get_available_actions()
        
        for a in actions:
            if a == PlayerAction.SPLIT:
                self.create_split_action_child()
            elif a == PlayerAction.HIT:
                self.create_hit_action_child()
            elif a == PlayerAction.STAND:
                self.create_stand_action_child()
            elif a == PlayerAction.DOUBLE:
                self.create_double_action_child()
            else:
                raise RuntimeError(f"Unexpected player action {a}")


    def create_action_child(self, action):
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_action(action)
        self.create_child(bj_round_copy, self.shoe.copy(), action)


    def create_split_action_child(self):
        if self.n_splits_happened >= self.bj_round.rules.max_splits_allowed:
            return

        bj_round_copy = self.bj_round.copy()
        shoe_copy = self.shoe.copy()
        child = SplitNode(
            bj_round_copy, shoe_copy, parent=self, copy_data=False,
            max_hand_size_full_enum=self.max_hand_size_full_enum,
            player_card_initial_samples=self.player_card_initial_samples,
            n_dealer_sim_runs=self.n_dealer_sim_runs,
            n_splits_happened=self.n_splits_happened + 1
        )
        self.add_child(child, PlayerAction.SPLIT)
        return child

    def create_hit_action_child(self):
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_action(PlayerAction.HIT)
        shoe_copy = self.shoe.copy()
        child = HitNode(
            bj_round_copy, shoe_copy,
            max_hand_size_full_enum=self.max_hand_size_full_enum,
            n_dealer_sim_runs=self.n_dealer_sim_runs,
            parent=self,
            copy_data=False,
            player_card_initial_samples=self.player_card_initial_samples,
            n_splits_happened=self.n_splits_happened
        )
        self.add_child(child, PlayerAction.HIT)
        return child

    def create_stand_action_child(self):
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_action(PlayerAction.STAND)
        value = run_dealer_cards_simulation(
            bj_round_copy, self.shoe, self.n_dealer_sim_runs
        )
        child = ValueNode(value, self)
        self.add_child(child, PlayerAction.STAND)
        return child

    def create_double_action_child(self):
        bj_round_copy = self.bj_round.copy()
        shoe_copy = self.shoe.copy()
        bj_round_copy.take_action(PlayerAction.DOUBLE)
        child = DoubleNode(
            bj_round_copy, shoe_copy,
            parent=self,
            n_dealer_sim_runs=self.n_dealer_sim_runs
        ) 
        self.add_child(child, PlayerAction.DOUBLE)
        return child


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


    def _convert_from_sample_to_full(self):
        """
            Convert a sampled player card node to full enumeration.
            This function merely adds the missing children and updates probabilities.
            It does not recurse into children and does not recompute tree value.
        """
        raise NotImplementedError()


    def convert_to_full_up_to_depth(self, depth):
        if not self.tree_completed():
            raise RuntimeError(
                "Cannot convert player card sample to full enum in an incomplete tree."
            )

        if depth < 0:
            return False
        
        children_changed = False

        stage = self.bj_round.get_stage()
        
        if stage in (BJStage.DEALER_CARD, BJStage.ROUND_OVER):
            return False

        # convert itself
        if stage == BJStage.PLAYER_CARD:
            changed_children_self = self._convert_from_sample_to_full()
            if changed_children_self:
                children_changed = True

        # convert children
        for ch in self.children:
            if isinstance(ch, (ValueNode, DoubleNode)):
                continue
            
            # if there is a new child that doesnt have a tree - build the tree
            if not ch.tree_completed():
                ch.build_tree()
                children_changed = True

            child_changed = ch.convert_to_full_up_to_depth(depth - 1)
            if child_changed:
                children_changed = True

        # this will build trees from the newly created children
        if children_changed:
            self.recompute_tree_value()
            return True
        return False    


class SplitNode(FloorCeilNode):
    """
    On "split" action selection, this class should receive the round object 
    unchanged, before the decision was supplied
    it will then simulate only one hand of the pair and double the value
    it will use the old round object to construct a completely new one
    instead of using the old 
    """
    def __init__(
            self,
            bj_round,
            shoe,
            max_hand_size_full_enum,
            player_card_initial_samples=1,
            n_dealer_sim_runs=100,
            parent=None,
            copy_data=True,
            n_splits_happened=0
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            max_hand_size_full_enum=max_hand_size_full_enum,
            n_dealer_sim_runs=n_dealer_sim_runs,
            player_card_initial_samples=player_card_initial_samples,
            n_splits_happened=n_splits_happened
        )

        self.original_bj_round = self.bj_round

        stage = self.original_bj_round.get_stage()
        assert stage == BJStage.PLAYER_ACTION
        assert PlayerAction.SPLIT in self.bj_round.get_available_actions()

        # need to build a new bj_round object - only first player card, waiting for the second
        subtree_rules = replace(self.bj_round.rules, ignore_player_natural_blackjack=True)
        # on player getting "2-card 21" after split
        # the dealer has to keep hitting until it has to stop by the rules
        # I have to set ignore_player_natural_blackjack to true
        # otherwise 2-card 21 will be treated like BJ so dealer will only get the second
        subtree_bj_round = BJRound(subtree_rules)
        subtree_bj_round.start_round(self.bj_round.bet_unit)
        subtree_bj_round.take_card(self.bj_round.player_hands[0][0])
        self.bj_round = subtree_bj_round
    

    def create_child(self, child_bj_round, child_shoe, event, prob=0):
        stage = child_bj_round.get_stage()
        if stage == BJStage.PLAYER_ACTION:
            child = DecisionNode(
                child_bj_round, child_shoe, parent=self, copy_data=False,
                max_hand_size_full_enum=self.max_hand_size_full_enum,
                player_card_initial_samples=self.player_card_initial_samples,
                n_dealer_sim_runs=self.n_dealer_sim_runs,
                n_splits_happened=self.n_splits_happened
            )
        elif stage == BJStage.DEALER_CARD:
            value = run_dealer_cards_simulation(
                child_bj_round, child_shoe, self.n_dealer_sim_runs
            )
            child = ValueNode(value, self)
        else:
            raise RuntimeError(
                f"Unexpected stage {stage} in SplitNode child creation."
            )
        return self.add_child(child, event, prob)

    def _convert_from_sample_to_full(self):
        return False  # full sample on first build_children already

    def _build_children_player_card(self):
        raise NotImplementedError()
     
    def _build_children_dealer_card(self):
        raise NotImplementedError()


    def build_children(self):
        card_probabilities = self.shoe.get_rank_value_probabilities()
        for card, prob in card_probabilities.items():
            if prob == 0:
                continue
            
            # card_bj_round is supposed to represent one of the split hands 
            # 2 split hands are approximated by it
            child_shoe = self.shoe.copy()
            child_shoe.burn_rank_value(card)

            # this is wrong because I need to block blackjack evaluation
            child_bj_round = self.bj_round.copy()

            child_bj_round.take_card(card)
            child_bj_round.take_card(self.original_bj_round.dealer_hand[0])

            if self.original_bj_round.insurance_bet > 0:
                child_bj_round.take_action(PlayerAction.TAKE_INSURANCE)

            if child_bj_round.get_stage() == BJStage.DEALER_CHECK_BJ:
                # split action would only be possible if dealer does not have blackjack
                child_bj_round.take_action(DealerAction.CONFIRM_NO_BLACKJACK)

            self.create_child(
                child_bj_round, child_shoe, card, prob
            )
        self.has_built_children = True


    def _compute_node_value(self):
        """Complete expected value, double it as this node corresponds to a pair."""
        values = [child.get_value() for child in self.children]
        self.value = 2 * sum([p * v for p, v in zip(self.children_prob, values)])


class DecisionNode(FloorCeilNode):
    """
    The node represents the game state before player makes the decision 
    Children of this node are the cards that can come after hit
    """
    def __init__(
            self,
            bj_round,
            shoe,
            max_hand_size_full_enum,
            player_card_initial_samples=1,
            n_dealer_sim_runs=100,
            parent=None,
            copy_data=True,
            n_splits_happened=0
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            max_hand_size_full_enum=max_hand_size_full_enum,
            n_dealer_sim_runs=n_dealer_sim_runs,
            player_card_initial_samples=player_card_initial_samples,
            n_splits_happened=n_splits_happened
        )
        # when decision on the best value can be made, set this variable to non-None value
        self.decision_choice = None
        self.ceil_value = None
        self.floor_value = None

    def rebuild_children(self):
        super().rebuild_children()
        self.decision_choice = None
        self.ceil_value = None
        self.floor_value = None

    def get_ceil_value(self):
        return self.ceil_value

    def get_floor_value(self):
        return self.floor_value


    def _compute_floor_value(self):
        if self.decision_choice is None:
            # get the highest floor value among all decision/children
            floor_values = [child.get_floor_value() for child in self.children]
            self.floor_value = max(floor_values)
        else:
            child_idx = self.children_events.index(self.decision_choice)
            if child_idx == -1:
                raise RuntimeError(f"Decision choice {self.decision_choice} not found among children.")
            self.floor_value = self.children[child_idx].get_floor_value()

    def _compute_ceil_value(self):
        if self.decision_choice is None:
            # get the highest ceil value among all decision/children
            ceil_values = [child.get_ceil_value() for child in self.children]
            self.ceil_value = max(ceil_values)
        else:
            child_idx = self.children_events.index(self.decision_choice)
            if child_idx == -1:
                raise RuntimeError(f"Decision choice {self.decision_choice} not found among children.")
            self.ceil_value = self.children[child_idx].get_ceil_value()
            

    def is_decided(self):
        return self.decision_choice is not None


    def _update_decision(self):
        # decision is made when one action's min value is 
        # at least as high as other action's max values
        max_values = [ch.get_ceil_value() for ch in self.children]
        for ch_i, (ch, ch_action) in enumerate(zip(self.children, self.children_events)):
            other_max_values = max_values[:ch_i] + max_values[ch_i+1:]
            child_min_value = ch.get_floor_value()
            if child_min_value >= max(other_max_values):
                self.decision_choice = ch_action
                return True
        return False


    def _compute_node_value(self):
        super()._compute_node_value() # will set the probability to the action of highest value
        self._compute_ceil_value()
        self._compute_floor_value()
        self._update_decision()


    def build_children(self):
        if self.bj_round.get_stage() != BJStage.PLAYER_ACTION:
            raise RuntimeError(
                f"DecisionNode can only build children for PLAYER_ACTION stage, "
                f"but {self.bj_round.get_stage()} was provided."
            )

        possible_actions = self.bj_round.get_available_actions()
        for a in possible_actions:
            bj_round_child = self.bj_round.copy()

            if a == PlayerAction.SPLIT:
                if self.n_splits_happened >= self.bj_round.rules.max_splits_allowed:
                    continue
                else:
                    # decision nodes should be spawned lower down the tree, 
                    # after the split decision is already not possible 
                    raise RuntimeError("Split action should not be handled by DecisionNode.")
            
            elif a == PlayerAction.HIT:
                bj_round_child.take_action(PlayerAction.HIT)
                child = HitNode(
                    bj_round_child, self.shoe, 
                    max_hand_size_full_enum=self.max_hand_size_full_enum,
                    n_dealer_sim_runs=self.n_dealer_sim_runs,
                    parent=self,
                    copy_data=False,
                    player_card_initial_samples=self.player_card_initial_samples,
                    n_splits_happened=self.n_splits_happened
                )
                self.add_child(child, PlayerAction.HIT)
            
            elif a == PlayerAction.STAND:
                bj_round_child.take_action(PlayerAction.STAND)
                value = run_dealer_cards_simulation(
                    bj_round_child, self.shoe, self.n_dealer_sim_runs
                )
                child = ValueNode(value, self)
                self.add_child(child, PlayerAction.STAND)

            elif a == PlayerAction.DOUBLE:
                bj_round_child.take_action(PlayerAction.DOUBLE)
                child = DoubleNode(
                    bj_round_child, self.shoe,
                    parent=self,
                    n_dealer_sim_runs=self.n_dealer_sim_runs
                )
                self.add_child(child, PlayerAction.DOUBLE)
            
            else:
                raise RuntimeError(f"Unexpected player action {a}")

        self.has_built_children = True


    def convert_to_full_up_to_depth(self, depth):        
        if not self.tree_completed():
            raise RuntimeError(
                "Cannot convert DecisionNode to full enum in an incomplete tree."
            )

        if depth < 0:
            return False
        
        children_changed = False

        # convert children
        # should only go to the branch of decided action if decided action is not none
        # should recompute ceil and floor values and re-decide decision
        if self.decision_choice is None:
            for ch in self.children:
                if isinstance(ch, (ValueNode, DoubleNode)):
                    continue
                child_changed = ch.convert_to_full_up_to_depth(depth - 1)
                if child_changed:
                    children_changed = True
        else:
            child_idx = self.children_events.index(self.decision_choice)
            child = self.children[child_idx]
            if not isinstance(child, (ValueNode, DoubleNode)):
                child_changed = child.convert_to_full_up_to_depth(depth - 1)
                if child_changed:
                    children_changed = True

        # this will build trees from the newly created children
        if children_changed:
            self.recompute_tree_value()
            return True
        return False    




class HitNode(FloorCeilNode):
    """
    The node represents the game state *after* player hits
    Children of this node are the cards that can come after hit
    """
    def __init__(
            self,
            bj_round,
            shoe,
            max_hand_size_full_enum,
            player_card_initial_samples=1,
            n_dealer_sim_runs=100,
            parent=None,
            copy_data=True,
            n_splits_happened=0
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            max_hand_size_full_enum=max_hand_size_full_enum,
            n_dealer_sim_runs=n_dealer_sim_runs,
            player_card_initial_samples=player_card_initial_samples,
            n_splits_happened=n_splits_happened
        )
        self.rank_probabilities = self.shoe.get_rank_value_probabilities()
        self.cards_bust = []
        self.cards_21 = []
        self.cards_not_sampled = []
        self.cards_sampled = []
        self.p_bust = 0
        self.p_21 = 0
        self.max_child_value = self.bj_round.bet_unit
        # this node should be constructed after the given hand
        # can no longer double, so min value is just -bet
        # if double was possible, min value could be lower
        self.min_child_value = -self.bj_round.bet_unit 
        self.init_values()


    def init_values(self):
        hand = self.bj_round.get_active_player_hand()

        for c, p in self.rank_probabilities.items():
            hand_copy = hand.copy()
            hand_copy.add_card(c)
            if hand_copy.is_bust():
                self.p_bust += p
                self.cards_bust.append(c)
            elif hand_copy.get_best_value() == 21:
                self.p_21 += p
                self.cards_21.append(c)
            else:
                self.cards_not_sampled.append(c)
        
        sample_card = self.shoe.sample_rank()
        if sample_card in self.cards_not_sampled:
            self.cards_not_sampled.remove(sample_card)
            self.cards_sampled.append(sample_card)
        
    def get_ceil_value(self):
        if not self.has_built_children:
            raise RuntimeError("Cannot get ceil value before building children.")
        ceil_value = 0
        for child, p in zip(self.children, self.children_prob):
            ceil_value += p * child.get_ceil_value()
        return ceil_value
    
    def get_floor_value(self):
        if not self.has_built_children:
            raise RuntimeError("Cannot get floor value before building children.")
        floor_value = 0
        for child, p in zip(self.children, self.children_prob):
            floor_value += p * child.get_floor_value()
        return floor_value
    
    def _compute_node_value(self):
        nodes_with_value_cum_prob = 0
        values = []
        probs = []
        for ch_card, ch_prob, child in zip(self.children_events, self.children_prob, self.children):
            if ch_card in self.cards_not_sampled:
                continue
            nodes_with_value_cum_prob += ch_prob
            values.append(child.get_value())
            probs.append(ch_prob)
        self.value = sum([p * v for p, v in zip(probs, values)]) / nodes_with_value_cum_prob
        

    def can_add_sample(self):
        return len(self.cards_not_sampled) > 0

    def add_sample(self):
        if not self.can_add_sample():
            return False
        sample_card = self.shoe.sample_rank(self.cards_not_sampled)
        self.cards_not_sampled.remove(sample_card)
        self.cards_sampled.append(sample_card)
        
        shoe_copy = self.shoe.copy()
        shoe_copy.burn_rank_value(sample_card)
        bj_round_copy = self.bj_round.copy()
        bj_round_copy.take_card(sample_card)
        new_child = DecisionNode(
            bj_round_copy, shoe_copy,
            max_hand_size_full_enum=self.max_hand_size_full_enum,
            n_dealer_sim_runs=self.n_dealer_sim_runs,
            parent=self,
            copy_data=False,
            player_card_initial_samples=self.player_card_initial_samples,
            n_splits_happened=self.n_splits_happened
        )

        child_idx = self.children_events.index(sample_card)
        old_child = self.children[child_idx]
        old_child.parent = None
        self.children[child_idx] = new_child
        
        return True

    def _convert_from_sample_to_full(self):
        if len(self.cards_not_sampled) == 0:
            return False
        
        for c in self.cards_not_sampled:
            shoe_copy = self.shoe.copy()
            shoe_copy.burn_rank_value(c)
            bj_round_copy = self.bj_round.copy()
            bj_round_copy.take_card(c)
            new_child = DecisionNode(
                bj_round_copy, shoe_copy,
                max_hand_size_full_enum=self.max_hand_size_full_enum,
                n_dealer_sim_runs=self.n_dealer_sim_runs,
                parent=self,
                copy_data=False,
                player_card_initial_samples=self.player_card_initial_samples,
                n_splits_happened=self.n_splits_happened
            )
            child_idx = self.children_events.index(c)
            old_child = self.children[child_idx]
            old_child.parent = None
            self.children[child_idx] = new_child

        self.cards_not_sampled = []
        return True


    def create_child(self, child_bj_round, child_shoe, event, prob=0):
        raise NotImplementedError()
        
    def _build_children_player_card(self):
        raise NotImplementedError()

    def _build_children_dealer_card(self):
        raise NotImplementedError()

     
    def build_children(self): 
        stage = self.bj_round.get_stage()
        if stage != BJStage.PLAYER_CARD:
            raise RuntimeError(
                f"HitNode can only build children for PLAYER_CARD stage, "
                f"but {stage} was provided."
            )

        for c in self.cards_21:
            child_shoe = self.shoe.copy()
            child_shoe.burn_rank_value(c)
            child_bj_round = self.bj_round.copy()
            child_bj_round.take_card(c)
            value_21 = run_dealer_cards_simulation(
                child_bj_round, child_shoe, self.n_dealer_sim_runs
            )
            child = ValueNode(value_21, self)
            p = self.rank_probabilities[c]
            self.add_child(child, c, p)
            # replace max_child_value, estimate it as the value 
            # of the node where player gets 21
            self.max_child_value = value_21

        for c in self.cards_bust:
            child = ValueNode(-self.bj_round.bet_unit, self)
            p = self.rank_probabilities[c]
            self.add_child(child, c, p)

        for c in self.cards_sampled:
            child_shoe = self.shoe.copy()
            child_shoe.burn_rank_value(c)
            child_bj_round = self.bj_round.copy()
            child_bj_round.take_card(c)
            child = DecisionNode(
                child_bj_round, child_shoe,
                max_hand_size_full_enum=self.max_hand_size_full_enum,
                n_dealer_sim_runs=self.n_dealer_sim_runs,
                parent=self,
                copy_data=False,
                player_card_initial_samples=self.player_card_initial_samples,
                n_splits_happened=self.n_splits_happened
            )
            p = self.rank_probabilities[c]
            self.add_child(child, c, p)
        
        for c in self.cards_not_sampled:
            # for cards not sampled right now, use the min and max value boundaries data
            child = FloorCeilValueNode(
                self.min_child_value, self.max_child_value, self
            )
            p = self.rank_probabilities[c]
            self.add_child(child, c, p)
        
        self.has_built_children = True


class DoubleNode(AbstractBJTreeNode):
    def __init__(
        self,
        bj_round,
        shoe,
        parent,
        copy_data=True,
        n_dealer_sim_runs=100
    ):
        super().__init__(bj_round, shoe, parent=parent, copy_data=copy_data)
        self.n_dealer_sim_runs = n_dealer_sim_runs
        
    def build_children(self):
        rank_prob = self.shoe.get_rank_value_probabilities()
        for card, p in rank_prob.items():
            bj_round_copy = self.bj_round.copy()
            shoe_copy = self.shoe.copy()
            bj_round_copy.take_card(card)
            shoe_copy.burn_rank_value(card)
            value = run_dealer_cards_simulation(
                bj_round_copy, shoe_copy, self.n_dealer_sim_runs
            )
            child = ValueNode(value, self)
            self.children.append(child)
            self.children_prob.append(p)
            self.children_events.append(card)
        self.build_children = True

        self._compute_chance_node_value()
        self.has_completed_tree = True

    def get_ceil_value(self):
        return self.get_value()

    def get_floor_value(self):
        return self.get_value()
