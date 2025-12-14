from typing import override
from blackjack.actions import DealerAction, PlayerAction
from blackjack.abstract_node import (
    AbstractBJTreeNode,
    FloorCeilValueNode,
    ValueNode
)
from blackjack.dealer_sim import (
    run_dealer_cards_simulation_combo,
    run_dealer_cards_simulation_recursive
)
from blackjack.blackjack_round import BJRound, BJStage
import numpy as np
from collections import defaultdict, deque
from blackjack.rules import BJRules
from blackjack.tree_utils import iterate_nodes_by_levels
from dataclasses import replace


class FloorCeilNode(AbstractBJTreeNode):
    def __init__(
            self,
            bj_round,
            shoe,
            max_hand_size_full_enum=0,
            dealer_sim_depth=5,
            sim_algo=None,
            parent=None,
            copy_data=True
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data
        )
        self.shoe.reset_sampler()
        self.max_hand_size_full_enum = max_hand_size_full_enum
        self.sim_algo = sim_algo if sim_algo is not None else 'combo'
        self.dealer_sim_depth = dealer_sim_depth
        self._active_hand_size = None
        hands = self.bj_round.player_hands
        hand_idx = self.bj_round.active_hand_idx
        if hand_idx < len(hands):
            active_hand = self.bj_round.player_hands[hand_idx]
            self._active_hand_size = active_hand.size()
        self.ceil_value = None
        self.floor_value = None


    def _run_dealer_sim(self, bj_round, shoe):
        """Run dealer simulation using the configured algorithm."""
        # split is simplified by treating the first hand of the split as having value
        # zero and only simulating the last one        
        if self.sim_algo == 'combo':
            return run_dealer_cards_simulation_combo(
                bj_round, shoe, None,
                n_full_sample=self.dealer_sim_depth,
                simulation_for_last_hand=True
            )
        elif self.sim_algo == 'recursive':
            return run_dealer_cards_simulation_recursive(
                bj_round, shoe, 1,
                n_full_sample=self.dealer_sim_depth,
                simulation_for_last_hand=True
            )
        else:
            raise ValueError(f"Unknown sim_algo: {self.sim_algo}. Use 'combo' or 'recursive'.")


    def rebuild_children(self):
        self.ceil_value = None
        self.floor_value = None
        super().rebuild_children()


    def get_ceil_value(self):
        if self.ceil_value is None:
            raise RuntimeError("Ceil value not computed.")
        return self.ceil_value


    def get_floor_value(self):
        if self.floor_value is None:
            raise RuntimeError("Floor value not computed.")
        return self.floor_value
    

    def _compute_floor_value(self):
        raise NotImplementedError()


    def _compute_ceil_value(self):
        raise NotImplementedError()


    def _compute_chance_node_ceil_value(self):
        if not self.has_built_children:
            raise RuntimeError("Cannot get ceil value before building children.")
        ceil_value = 0
        for child, p in zip(self.children, self.children_prob):
            ceil_value += p * child.get_ceil_value()
        self.ceil_value = ceil_value


    def _compute_chance_node_floor_value(self):
        if not self.has_built_children:
            raise RuntimeError("Cannot get floor value before building children.")
        floor_value = 0
        for child, p in zip(self.children, self.children_prob):
            floor_value += p * child.get_floor_value()
        self.floor_value = floor_value


    def _compute_node_value(self):
        super()._compute_node_value()
        self._compute_ceil_value()
        self._compute_floor_value()


    
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



    def convert_to_full_up_to_depth(self, depth):
        """
        Note: I cannot use the return value of this function to determine
        Whether the tree cannot be updated anymore
        It can return false just because the depth is too shallow
        to trigger change in children

        Return 2 boolean values
        First - whether node value has changed
        Second - is_final: True if no further exploration can improve the value
        """
        if not self.tree_completed():
            raise RuntimeError(
                "Cannot convert player card sample to full enum in an incomplete tree."
            )

        if depth < 0:
            return False, False
        
        children_changed = False
        is_final = True

        stage = self.bj_round.get_stage()
        
        if stage in (BJStage.DEALER_CARD, BJStage.ROUND_OVER):
            return False, True
        
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

            child_changed, child_is_final = ch.convert_to_full_up_to_depth(depth - 1)
            if child_changed:
                children_changed = True
            if not child_is_final:
                is_final = False

        
        if children_changed:
            self.recompute_tree_value()
            value_changed = True
        else:
            value_changed = False

        return value_changed, is_final    


    def _convert_from_sample_to_full(self):
        """
            Convert a sampled player card node to full enumeration.
            This function merely adds the missing children and updates probabilities.
            It does not recurse into children and does not recompute tree value.
        """
        raise NotImplementedError()


class DecisionNode(FloorCeilNode):
    """
    The node represents the game state before player makes the decision 
    Children of this node are the cards that can come after hit
    """
    def __init__(
        self,
        bj_round,
        shoe,
        max_hand_size_full_enum=0,
        dealer_sim_depth=5,
        sim_algo=None,
        parent=None,
        copy_data=True
    ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            max_hand_size_full_enum=max_hand_size_full_enum,
            dealer_sim_depth=dealer_sim_depth,
            sim_algo=sim_algo
        )
        # list of actions that are still possible (not yet excluded)
        # initialized from available actions, split exclusion handled in build_children
        self.possible_actions = self.bj_round.get_available_actions()
            

    def rebuild_children(self):
        self.possible_actions = self.bj_round.get_available_actions()
        super().rebuild_children()


    def _compute_floor_value(self):
        if len(self.possible_actions) > 1:
            # get the highest floor value among all possible actions
            floor_values = [
                self.children[self.children_events.index(action)].get_floor_value()
                for action in self.possible_actions
            ]
            self.floor_value = max(floor_values)
        else:
            self.floor_value = self.get_decision_choice_child().get_floor_value()


    def _compute_ceil_value(self):
        if len(self.possible_actions) > 1:
            # get the highest ceil value among all possible actions
            ceil_values = [
                self.children[self.children_events.index(action)].get_ceil_value()
                for action in self.possible_actions
            ]
            self.ceil_value = max(ceil_values)
        else:
            self.ceil_value = self.get_decision_choice_child().get_ceil_value()
            

    @property
    def decision_choice(self):
        """Backwards-compatible property. Returns the single decided action, or None if undecided."""
        if len(self.possible_actions) != 1:
            return None
        return self.possible_actions[0]

    def get_decision_choice_child(self):
        if len(self.possible_actions) != 1:
            raise RuntimeError("Decision choice is not yet made (multiple actions still possible).")
        action = self.possible_actions[0]
        child_idx = self.children_events.index(action)
        return self.children[child_idx]


    def has_decided(self):
        """Returns True if only one action remains possible."""
        return len(self.possible_actions) == 1


    def get_possible_action_children(self):
        """Returns list of (action, child) tuples for all possible actions."""
        return [
            (action, self.children[self.children_events.index(action)])
            for action in self.possible_actions
        ]
    
    
    def _compute_action_node_value(self):
        """Compute value using only non-excluded actions."""
        if not self.possible_actions:
            raise RuntimeError("No possible actions remaining to evaluate.")

        # Identify the best-valued action among those still possible.
        values = []
        child_indices = []
        for action in self.possible_actions:
            child_idx = self.children_events.index(action)
            child_indices.append(child_idx)
            values.append(self.children[child_idx].get_value())

        best_idx_in_possible = int(np.argmax(values))
        best_child_idx = child_indices[best_idx_in_possible]

        # Clear probabilities for all actions, then select the best possible action.
        self.children_prob = [0 for _ in self.children_prob]
        self.children_prob[best_child_idx] = 1
        self.value = values[best_idx_in_possible]


    def _compute_node_value(self):
        self._compute_action_node_value()
        self._update_possible_actions()
        self._compute_floor_value()
        self._compute_ceil_value()


    def _update_possible_actions(self):
        """
        Exclude actions whose ceiling is below any other action's floor.
        An action can be excluded if there exists another action whose floor
        is at least as high as this action's ceiling.
        """
        # Get floor and ceil values for each possible action
        floor_values = []
        ceil_values = []
        for action in self.possible_actions:
            child_idx = self.children_events.index(action)
            child = self.children[child_idx]
            floor_values.append(child.get_floor_value())
            ceil_values.append(child.get_ceil_value())
        
        # Find the maximum floor value among all possible actions
        max_floor = max(floor_values)
        
        # Exclude actions whose ceiling is below the max floor
        new_possible_actions = [
            action for action, ceil_val in zip(self.possible_actions, ceil_values)
            if ceil_val >= max_floor
        ]
        
        self.possible_actions = new_possible_actions


    def build_children(self):
        if not self.is_player_decision_node():
            raise RuntimeError(
                f"DecisionNode can only build children for player decision stage, "
                f"but {self.bj_round.get_stage()} was provided."
            )

        stage = self.bj_round.get_stage()
        if stage == BJStage.PLAYER_OFFERED_INSURANCE:
            self._build_children_insurance()
        elif stage in (
            BJStage.PLAYER_ACTION, BJStage.PLAYER_OFFERED_EARLY_SURRENDER
        ):
            self._build_children_player_action()
        else:
            raise RuntimeError(f"Unexpected game stage: {stage}")
        self.has_built_children = True


    def _build_children_player_action(self):
        for a in self.possible_actions:
            bj_round_child = self.bj_round.copy()
            shoe_copy = self.shoe.copy()

            if a == PlayerAction.SPLIT:
                # the first hand of the split is set to <card>2 hand with zero value
                # to simplify the tree only the second one is considered, it's value is doubled
                # this is handled inside SplitNode
                bj_round_child.take_action(PlayerAction.SPLIT)
                child = SplitNode(
                    bj_round_child, shoe_copy,
                    max_hand_size_full_enum=self.max_hand_size_full_enum,
                    dealer_sim_depth=self.dealer_sim_depth,
                    sim_algo=self.sim_algo,
                    parent=self,
                    copy_data=False
                )
                self.add_child(child, PlayerAction.SPLIT)
            
            elif a == PlayerAction.HIT:
                bj_round_child.take_action(PlayerAction.HIT)
                child = HitNode(
                    bj_round_child, shoe_copy, 
                    max_hand_size_full_enum=self.max_hand_size_full_enum,
                    dealer_sim_depth=self.dealer_sim_depth,
                    sim_algo=self.sim_algo,
                    parent=self,
                    copy_data=False
                )
                self.add_child(child, PlayerAction.HIT)
            
            elif a == PlayerAction.STAND:
                bj_round_child.take_action(PlayerAction.STAND)
                value = self._run_dealer_sim(bj_round_child, shoe_copy)
                child = ValueNode(value, self)
                self.add_child(child, PlayerAction.STAND)

            elif a == PlayerAction.DOUBLE:
                bj_round_child.take_action(PlayerAction.DOUBLE)
                child = DoubleNode(
                    bj_round_child, shoe_copy,
                    parent=self,
                    dealer_sim_depth=self.dealer_sim_depth,
                    sim_algo=self.sim_algo,
                    copy_data=False
                )
                self.add_child(child, PlayerAction.DOUBLE)
            
            elif a == PlayerAction.DECLINE_EARLY_SURRENDER:
                bj_round_child.take_action(PlayerAction.DECLINE_EARLY_SURRENDER)
                child = DecisionNode(
                    bj_round_child, shoe_copy,
                    max_hand_size_full_enum=self.max_hand_size_full_enum,
                    dealer_sim_depth=self.dealer_sim_depth,
                    sim_algo=self.sim_algo,
                    parent=self,
                    copy_data=False
                )
                self.add_child(child, PlayerAction.DECLINE_EARLY_SURRENDER)
            
            elif a == PlayerAction.SURRENDER:
                value = - bj_round_child.bet_unit + bj_round_child.bet_unit * bj_round_child.rules.surrender_payout
                child = ValueNode(value, self)
                self.add_child(child, PlayerAction.SURRENDER)

            else:
                raise RuntimeError(f"Unexpected player action {a}")

        self.has_built_children = True


    def _build_children_insurance(self):
        bj_round_child = self.bj_round.copy()
        bj_round_child.take_action(PlayerAction.REFUSE_INSURANCE)
        
        accept_child = DealerCheckBJNode(
            bj_round_child, self.shoe.copy(),
            max_hand_size_full_enum=self.max_hand_size_full_enum,
            dealer_sim_depth=self.dealer_sim_depth,
            sim_algo=self.sim_algo,
            parent=self,
            copy_data=False,
            took_insurance=True,
            insurance_offered=True
        )
        
        decline_child = DealerCheckBJNode(
            bj_round_child, self.shoe.copy(),
            max_hand_size_full_enum=self.max_hand_size_full_enum,
            dealer_sim_depth=self.dealer_sim_depth,
            sim_algo=self.sim_algo,
            parent=self,
            copy_data=False,
            took_insurance=False,
            insurance_offered=True
        )

        accept_child.build_children()
        decline_child.build_children()

        accept_child.children[accept_child.dealer_no_bj_child_idx] = \
            decline_child.children[decline_child.dealer_no_bj_child_idx]
        common_child = accept_child.children[accept_child.dealer_no_bj_child_idx]
        common_child.parent = [accept_child, decline_child]

        self.add_child(accept_child, PlayerAction.TAKE_INSURANCE)
        self.add_child(decline_child, PlayerAction.REFUSE_INSURANCE)


    def convert_to_full_up_to_depth(self, depth):        
        if not self.tree_completed():
            raise RuntimeError(
                "Cannot convert DecisionNode to full enum in an incomplete tree."
            )

        if depth < 0:
            return False, False
        
        # convert children
        # should only go to the branches of possible actions
        # should recompute ceil and floor values and re-filter possible actions

        if self.bj_round.get_stage() == BJStage.PLAYER_OFFERED_INSURANCE:
            children_changed, is_final = \
                self.convert_bj_check_children_to_full_up_to_depth(depth)
        else:
            children_changed, is_final = \
                self.convert_possible_children_to_full_up_to_depth(depth)

        # this will build trees from the newly created children
        if children_changed:
            self.recompute_tree_value()
            return True, is_final
        else:
            return False, is_final
            

    def convert_bj_check_children_to_full_up_to_depth(self, depth):  
        # special case - update the downstream round tree where dealer does not have bj 
        # and update nodes where dealer checks for blackjack
        # both insurance children share the same no-BJ subtree
        child: DealerCheckBJNode = self.children[0]
        dealer_no_bj_round_tree = child.children[child.dealer_no_bj_child_idx]
        if isinstance(dealer_no_bj_round_tree, ValueNode):
            # case where player has bj but dealer does not - no subtree to expand
            return False, True
        
        round_child_changed, child_is_final = \
            dealer_no_bj_round_tree.convert_to_full_up_to_depth(depth - 2)
        
        if round_child_changed:
            if not self.has_decided():
                for ch in self.children:
                    ch.recompute_tree_value()
            else:
                self.get_decision_choice_child().recompute_tree_value()
            return True, child_is_final
        else:
            return False, child_is_final
        

    def convert_possible_children_to_full_up_to_depth(self, depth):  
        """Convert only children corresponding to possible actions."""
        children_changed = False
        is_final = True
        for action in self.possible_actions:
            child_idx = self.children_events.index(action)
            ch = self.children[child_idx]
            if isinstance(ch, (ValueNode, DoubleNode)):
                continue
            child_changed, child_is_final = ch.convert_to_full_up_to_depth(depth - 1)
            if child_changed:
                children_changed = True
            if not child_is_final:
                is_final = False
        return children_changed, is_final
        

class DealerCheckBJNode(FloorCeilNode):
    def __init__(
            self,
            bj_round,
            shoe,
            took_insurance,
            insurance_offered,
            max_hand_size_full_enum=0,
            dealer_sim_depth=5,
            sim_algo=None,
            parent=None,
            copy_data=True
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            max_hand_size_full_enum=max_hand_size_full_enum,
            dealer_sim_depth=dealer_sim_depth,
            sim_algo=sim_algo
        )
        self.insurance_offered = insurance_offered
        self.took_insurance = took_insurance
        # set insurance bet in bj_round for correct payout calculation
        self.insurance_bet = self.bj_round.bet_unit / 2
        # bj_round state should always reject insurance to avoid 2 identical trees
        # for taking and rejecting insurance, handle payout outside bj_round 
        # game rules should be such that dealer checks blackjack
        # otherwise we may need a approach to estimate value of taking insurance from the
        # round without insurance 
        assert bj_round.rules.dealer_checks_blackjack 
        assert self.bj_round.insurance_bet == 0
        assert len(self.bj_round.player_hands) == 1
        self.dealer_bj_child_idx = 0
        self.dealer_no_bj_child_idx = 1
        

    def get_dealer_blackjack_chance(self):
        rank_prob = self.shoe.get_rank_value_probabilities()
        dealer_upcard = self.bj_round.dealer_hand[0]
        assert len(self.bj_round.dealer_hand) == 1
        assert dealer_upcard in (10, 11)
        if dealer_upcard == 10:
            return rank_prob.get(11, 0)
        else:
            return rank_prob.get(10, 0)
        

    def build_children(self):
        bj_round_child = self.bj_round.copy()
        player_has_bj = self.bj_round.get_active_player_hand().is_natural_blackjack()

        insurance_bet = self.bj_round.bet_unit / 2
        if player_has_bj:
            # insurance_bet * (1 + self.bj_round.rules.insurance_payout) + self.bj_round.bet_unit - (self.bj_round.bet_unit + insurance_bet) 
            player_value_dealer_bj_insurance = self.bj_round.rules.insurance_payout * insurance_bet
            # self.bj_round.bet_unit - self.bj_round.bet_unit = 0 
            player_value_dealer_bj_no_insurance = 0
        else:
            # insurance_bet * (1 + self.bj_round.rules.insurance_payout) - (self.bj_round.bet_unit + insurance_bet) 
            player_value_dealer_bj_insurance = insurance_bet * self.bj_round.rules.insurance_payout - self.bj_round.bet_unit
            # -(self.bj_round.bet_unit) 
            player_value_dealer_bj_no_insurance = -self.bj_round.bet_unit
 
        if self.took_insurance:
            player_value_dealer_bj = player_value_dealer_bj_insurance
        else:
            player_value_dealer_bj = player_value_dealer_bj_no_insurance

        dealer_bj_value_node = ValueNode(
            player_value_dealer_bj,
            self # set self as the parent of all nodes here
        )

        bj_round_no_bj_child = bj_round_child.copy()
        shoe_copy_no_bj = self.shoe.copy()
        bj_round_no_bj_child.take_action(DealerAction.CONFIRM_NO_BLACKJACK)
        shoe_copy_no_bj.lock_dealer_card_not_ten()

        if player_has_bj:
            dealer_no_bj_node = ValueNode(
                self.bj_round.bet_unit * self.bj_round.rules.natural_blackjack_payout,
                self
            )
        else:
            dealer_no_bj_node = DecisionNode(
                bj_round_no_bj_child, shoe_copy_no_bj,
                max_hand_size_full_enum=self.max_hand_size_full_enum,
                dealer_sim_depth=self.dealer_sim_depth,
                sim_algo=self.sim_algo,
                parent=self,
                copy_data=False
            )

        p_blackjack = self.get_dealer_blackjack_chance()
        p_no_blackjack = 1 - p_blackjack

        if self.dealer_bj_child_idx == 0 and self.dealer_no_bj_child_idx == 1:    
            self.add_child(dealer_bj_value_node, DealerAction.CONFIRM_BLACKJACK, p_blackjack)
            self.add_child(dealer_no_bj_node, DealerAction.CONFIRM_NO_BLACKJACK, p_no_blackjack)
        elif self.dealer_bj_child_idx == 1 and self.dealer_no_bj_child_idx == 0:
            self.add_child(dealer_no_bj_node, DealerAction.CONFIRM_NO_BLACKJACK, p_no_blackjack)
            self.add_child(dealer_bj_value_node, DealerAction.CONFIRM_BLACKJACK, p_blackjack)
        else:
            raise RuntimeError("Invalid child index configuration in DealerCheckBJNode.")
        
        self.has_built_children = True


    def _compute_node_value(self):
        if not self.took_insurance:
            # simple chance node
            self._compute_chance_node_value()
        else:
            # dealer_blackjack node value is correct
            # no_blackjack decision tree node value should be reduced by insurance amount
            # assume insurance is bet_unit / 2
            node_values = [ch.get_value() for ch in self.children]
            node_values[self.dealer_no_bj_child_idx] -= self.insurance_bet
            self.value = sum(
                p * v for p, v in zip(self.children_prob, node_values)
            )
        self._compute_ceil_value()
        self._compute_floor_value()


    def _compute_ceil_value(self):
        if not self.took_insurance:
            self._compute_chance_node_ceil_value()
        else:
            ceil_values = [ch.get_ceil_value() for ch in self.children]
            ceil_values[self.dealer_no_bj_child_idx] -= self.insurance_bet
            self.ceil_value = sum(
                p * v for p, v in zip(self.children_prob, ceil_values)
            )


    def _compute_floor_value(self):
        if not self.took_insurance:
            self._compute_chance_node_floor_value()
        else:
            floor_values =  [ch.get_floor_value() for ch in self.children]
            floor_values[self.dealer_no_bj_child_idx] -= self.insurance_bet
            self.floor_value = sum(
                p * v for p, v in zip(self.children_prob, floor_values)
            )


    def convert_to_full_up_to_depth(self, depth):    
        if self.insurance_offered:
            # in the case of insurance, tree expansion and value update should be handled by
            # decision node
            raise RuntimeError(
                "Cannot convert DealerCheckBJNode with insurance to full enumeration directly."
            )
        else:
            # case where dealer checks for bj but insurance is not offered
            return super().convert_to_full_up_to_depth(depth) 


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
            max_hand_size_full_enum=0,
            dealer_sim_depth=5,
            sim_algo=None,
            parent=None,
            copy_data=True
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            max_hand_size_full_enum=max_hand_size_full_enum,
            dealer_sim_depth=dealer_sim_depth,
            sim_algo=sim_algo
        )
        # the first hand of the split is set to <card>2 hand with zero value
        # to simplify the tree only the second one is considered, it's value is doubled
        # use 2 because it will never give 21 and "stand" will always be a legal action
        self.bj_round.take_card(2)  # placeholder card, not accounted in the shoe
        self.first_hand_idx = self.bj_round.active_hand_idx
        self.bj_round.hand_bets[self.first_hand_idx] = 0


    def create_child(self, child_bj_round, child_shoe, event, prob=0):
        stage = child_bj_round.get_stage()
        if stage == BJStage.PLAYER_ACTION:  # split has the value less than 21
            child = DecisionNode(
                child_bj_round, child_shoe, parent=self, copy_data=False,
                max_hand_size_full_enum=self.max_hand_size_full_enum,
                dealer_sim_depth=self.dealer_sim_depth,
                sim_algo=self.sim_algo
            )
        elif stage == BJStage.DEALER_CARD:  # split has the value of 21
            value = self._run_dealer_sim(child_bj_round, child_shoe)
            child = ValueNode(value, self)
        else:
            raise RuntimeError(
                f"Unexpected stage {stage} in SplitNode child creation."
            )
        return self.add_child(child, event, prob)


    def _convert_from_sample_to_full(self):
        return False  # full sample on first build_children already


    def build_children(self):
        card_probabilities = self.shoe.get_rank_value_probabilities()
        for card, prob in card_probabilities.items():
            if prob == 0:
                continue
            
            child_shoe = self.shoe.copy()
            child_shoe.burn_rank_value(card)

            child_bj_round = self.bj_round.copy()
            child_bj_round.take_card(card)
            
            # stand on the first hand - it already got a placeholder card and its value is set to zero
            child_bj_round.take_action(PlayerAction.STAND)

            self.create_child(
                child_bj_round, child_shoe, card, prob
            )
        self.has_built_children = True


    def _compute_node_value(self):
        """Complete expected value, double it as this node corresponds to a pair."""
        self._compute_chance_node_value()
        self.value = 2 * self.value
        self._compute_ceil_value()
        self._compute_floor_value()

    def _compute_ceil_value(self):
        self._compute_chance_node_ceil_value()
        self.ceil_value = 2 * self.ceil_value
     
    def _compute_floor_value(self):
        self._compute_chance_node_floor_value()
        self.floor_value = 2 * self.floor_value


class HitNode(FloorCeilNode):
    """
    The node represents the game state *after* player hits
    Children of this node are the cards that can come after hit
    """
    def __init__(
            self,
            bj_round,
            shoe,
            max_hand_size_full_enum=0,
            dealer_sim_depth=5,
            sim_algo=None,
            parent=None,
            copy_data=True
        ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            max_hand_size_full_enum=max_hand_size_full_enum,
            dealer_sim_depth=dealer_sim_depth,
            sim_algo=sim_algo
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
        
        if len(hand) <= self.max_hand_size_full_enum:
            # Hand is small enough - use full enumeration for all remaining cards
            self.cards_sampled = self.cards_not_sampled
            self.cards_not_sampled = []
        else:
            # Hand is large - sample only one card, rest remain unsampled
            sample_card = self.shoe.sample_rank()
            # we could have sampled a card that leads to bust or stand on 21
            # self.cards_not_sampled do not include such cards 
            if sample_card in self.cards_not_sampled:
                self.cards_not_sampled.remove(sample_card)
                self.cards_sampled.append(sample_card)
    

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
        self._compute_chance_node_ceil_value()
        self._compute_chance_node_floor_value()
        

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
            dealer_sim_depth=self.dealer_sim_depth,
            sim_algo=self.sim_algo,
            parent=self,
            copy_data=False
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
                dealer_sim_depth=self.dealer_sim_depth,
                sim_algo=self.sim_algo,
                parent=self,
                copy_data=False
            )
            child_idx = self.children_events.index(c)
            old_child = self.children[child_idx]
            old_child.parent = None
            self.children[child_idx] = new_child

        self.cards_not_sampled = []
        return True


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
            value_21 = self._run_dealer_sim(child_bj_round, child_shoe)
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
                dealer_sim_depth=self.dealer_sim_depth,
                sim_algo=self.sim_algo,
                parent=self,
                copy_data=False
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
        
        assert np.abs(1 - sum(self.children_prob)) < 1e-7
        self.has_built_children = True


class DoubleNode(FloorCeilNode):
    def __init__(
        self,
        bj_round,
        shoe,
        parent,
        copy_data=True,
        dealer_sim_depth=5,
        sim_algo=None
    ):
        super().__init__(
            bj_round,
            shoe,
            parent=parent,
            copy_data=copy_data,
            dealer_sim_depth=dealer_sim_depth,
            sim_algo=sim_algo,
            max_hand_size_full_enum=None
        )
        

    def build_children(self):
        rank_prob = self.shoe.get_rank_value_probabilities()
        for card, p in rank_prob.items():
            player_hand = self.bj_round.get_active_player_hand().copy()
            player_hand.add_card(card)
            if player_hand.is_bust():
                value = - 2 * self.bj_round.bet_unit
                child = ValueNode(value, self)
            else:
                bj_round_copy = self.bj_round.copy()
                shoe_copy = self.shoe.copy()
                bj_round_copy.take_card(card)
                shoe_copy.burn_rank_value(card)
                value = self._run_dealer_sim(bj_round_copy, shoe_copy)
                value = 2 * value # double bet
                child = ValueNode(value, self)
            self.children.append(child)
            self.children_prob.append(p)
            self.children_events.append(card)
        self.has_built_children = True

        self._compute_chance_node_value()
        self.has_completed_tree = True


    def _compute_node_value(self):
        pass

    def get_ceil_value(self):
        return self.get_value()


    def get_floor_value(self):
        return self.get_value()
