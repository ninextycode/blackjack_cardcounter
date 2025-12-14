from blackjack.hand import Hand, ValueOnlyHand
from blackjack.cards import Card, Rank
from blackjack.actions import PlayerAction, DealerAction
from enum import Enum, auto 
from blackjack.rules import BJRules


class BJStage(Enum): 
    NOT_STARTED = auto()
    PLAYER_OFFERED_INSURANCE = auto()
    PLAYER_OFFERED_EARLY_SURRENDER = auto()
    PLAYER_ACTION = auto()
    PLAYER_CARD = auto()
    DEALER_CHECK_BJ = auto()
    DEALER_CARD = auto()
    ROUND_OVER = auto()


def all_rank_values():
    return range(2, 12)


class BJRound:
    def __init__(self, rules = BJRules()):
        self.stage = BJStage.NOT_STARTED

        self.rules : BJRules = rules
        # self.dealer_hand = Hand()
        # self.player_hands = [Hand()]
        self.dealer_hand = ValueOnlyHand()
        self.player_hands = [
            ValueOnlyHand()
        ]
        if not rules.allow_split_different_tens:
            raise NotImplementedError("Cannot differentiate different tens in ValueOnlyHand")
        
        self.active_hand_idx = 0
        self.dealer_checked_blackjack = False
        self.dealer_has_bj_after_check = False
        
        self.bet_unit = 0
        self.total_player_bet = 0
        self.total_player_got = 0
        self.player_value = 0

        self.n_splits = 0
        self.split_origin_idx = None
        self.pending_double_bet = False

        self.is_hand_in_progress = [True]
        self.hand_bets = [0]
        self.insurance_bet = 0

        self.surrendered = False
        self.early_surrendered = False

        self.last_action = None
        self.last_card = None


    def copy(self):
        new_node = BJRound(self.rules)
        new_node.stage = self.stage
        new_node.dealer_hand = self.dealer_hand.copy()
        new_node.player_hands = [hand.copy() for hand in self.player_hands]
        new_node.active_hand_idx = self.active_hand_idx
        new_node.dealer_checked_blackjack = self.dealer_checked_blackjack
        new_node.dealer_has_bj_after_check = self.dealer_has_bj_after_check

        new_node.bet_unit = self.bet_unit
        new_node.total_player_bet = self.total_player_bet
        new_node.total_player_got = self.total_player_got
        new_node.player_value = self.player_value

        new_node.n_splits = self.n_splits
        new_node.split_origin_idx = self.split_origin_idx
        new_node.pending_double_bet = self.pending_double_bet

        new_node.is_hand_in_progress = self.is_hand_in_progress.copy()
        new_node.hand_bets = self.hand_bets.copy()
        new_node.insurance_bet = self.insurance_bet

        new_node.surrendered = self.surrendered
        new_node.early_surrendered = self.early_surrendered

        new_node.last_action = self.last_action
        new_node.last_card = self.last_card

        return new_node

    def start_round(self, bet_unit):
        if self.stage != BJStage.NOT_STARTED:
            raise RuntimeError("Cannot restart the round")
        self.bet_unit = bet_unit
        self.hand_bets = [bet_unit]
        self.is_hand_in_progress[0] = True
        self.stage = BJStage.PLAYER_CARD

    def get_active_player_hand(self):
        if self.active_hand_idx >= len(self.player_hands):
            raise RuntimeError("Invalid state")
        return self.player_hands[self.active_hand_idx]

    def get_dealer_hand(self):
        return self.dealer_hand

    def get_stage(self):
        return self.stage

    def need_card(self):
        return self.stage in (BJStage.PLAYER_CARD, BJStage.DEALER_CARD)
    
    def need_action(self):
        return self.need_player_action() or self.need_dealer_action()
    
    def need_player_action(self):
        return self.stage in (
            BJStage.PLAYER_ACTION,
            BJStage.PLAYER_OFFERED_INSURANCE,
            BJStage.PLAYER_OFFERED_EARLY_SURRENDER,
        )

    def need_dealer_action(self):
        return self.stage == BJStage.DEALER_CHECK_BJ

    def dealer_expects_to_show_blackjack(self):
        return (
            self.dealer_hand.size() == 1
            and self.dealer_checked_blackjack
            and self.dealer_has_bj_after_check
        )

    def get_possible_next_card_ranks(self):
        """
        Get possible next card ranks given the current stage and state.
        Returns None if all ranks are possible.
        """
        if self.stage not in (BJStage.PLAYER_CARD, BJStage.DEALER_CARD):
            return []
        
        if self.stage == BJStage.PLAYER_CARD:
            return None
        
        elif self.stage == BJStage.DEALER_CARD:
            if self.dealer_hand.size() == 1:
                if self.dealer_checked_blackjack:
                    if self.dealer_has_bj_after_check:
                        if self.dealer_hand.cards[0] == 11:
                            # return [r for r in Rank if r == 10]
                            return [10]
                        if self.dealer_hand.cards[0] == 10:
                            # return [Rank.ACE]
                            return [11]
                        raise RuntimeError("Dealer must have blackjack but cards are not consistent")
                    else:
                        if self.dealer_hand.cards[0] == 11:
                            # return [r for r in Rank if r != 10]
                            return [r for r in range(2, 12) if r != 10]
                        if self.dealer_hand.cards[0] == 10:
                            # return [r for r in Rank if r != Rank.ACE]
                            return list(range(2, 11))

                        raise RuntimeError("Dealer cannot have blackjack but cards are not consistent")
                else:
                    return None
            else:
                return None


    def take_card(self, card):
        possible_values = self.get_possible_next_card_ranks()
        if possible_values is not None and card not in possible_values:
            raise RuntimeError(f"Invalid card {card}")

        # Set last card and clear last action
        self.last_card = card
        self.last_action = None

        if self.stage == BJStage.PLAYER_CARD:
            self._take_player_card(card)
        elif self.stage == BJStage.DEALER_CARD:
            self._take_dealer_card(card)
        else:
            raise RuntimeError(f"Cannot take card in stage {self.stage}")
        

    def _take_player_card(self, card):        
        if self.active_hand_idx >= len(self.player_hands):
            raise RuntimeError("Invalid hand index")
        if not self.is_hand_in_progress[self.active_hand_idx]:
            raise RuntimeError("Hand is not in progress")

        hand : Hand = self.player_hands[self.active_hand_idx]
        if hand.is_bust():
            raise RuntimeError("Hand is already bust")

        hand.add_card(card)

        # If this is the first card, wait for the second for the same hand
        if hand.size() < 2:
            self.stage = BJStage.PLAYER_CARD
            return

        # handle hand becoming inactive
        if self.pending_double_bet or hand.get_best_value() == 21 or hand.is_bust():
            self.is_hand_in_progress[self.active_hand_idx] = False
            if self.pending_double_bet:
                self.pending_double_bet = False

        # handle split
        if self.split_origin_idx is not None:
            # split ace just got the second card, stand, depending on the rules
            if not self.rules.allow_action_on_split_aces and hand.cards[0] == 11:
                self.is_hand_in_progress[self.active_hand_idx] = False
            
            # this was the first hand of the split
            # get second card for the second hand of the split 
            if self.active_hand_idx == self.split_origin_idx:
                self.active_hand_idx += 1 
                self.stage = BJStage.PLAYER_CARD
                return

            # step back to the first hand of the split, split-related actions ended
            elif self.active_hand_idx == (self.split_origin_idx + 1):
                self.active_hand_idx = self.split_origin_idx
                self.split_origin_idx = None
            else:
                raise RuntimeError("Illegal state during split")
    
        if (
            self.dealer_hand.size() == 0
        ):
            # Player has at least 2 cards here
            # If dealer has no cards yet, go to dealer card stage
            self.stage = BJStage.DEALER_CARD
        else:
            self._same_hand_or_next_or_dealer()


    def _take_dealer_card(self, card: int):
        self.dealer_hand.add_card(card)

        if self.surrendered:
            if not self.rules.dealer_shows_card_on_surrender:
                raise RuntimeError("Dealer does not show card on surrender yet card is given")
            self.calculate_value()
            self.stage = BJStage.ROUND_OVER
            return
        
        # decision after the card was added
        if self.dealer_hand.size() == 1:
            # Order of operations after dealer's first card:
            # 1. Insurance (if ace)
            # 2. Early surrender (if applicable)
            # 3. Dealer check for blackjack
            # 4. Player action if player doesn't have 21, else dealer takes the 2nd card
            if self.rules.allow_insurance_vs_ace and card == 11:
                self.stage = BJStage.PLAYER_OFFERED_INSURANCE
            elif self._can_early_surrender():
                self.stage = BJStage.PLAYER_OFFERED_EARLY_SURRENDER
            elif self.rules.dealer_checks_blackjack and self._dealer_can_have_bj():
                self.stage = BJStage.DEALER_CHECK_BJ
            else:
                self._same_hand_or_next_or_dealer()
        
        elif (
            len(self.player_hands) == 1 and self.player_hands[0].is_natural_blackjack()
        ):
            # Player's blackjack check
            # Dealer already has 2 cards
            # when player has natural blackjack
            # reveal the hole card, then finish, do not draw
            self.calculate_value()
            self.stage = BJStage.ROUND_OVER

        elif all([hand.is_bust() for hand in self.player_hands]):
            # Player's bust check
            # Dealer already has 2 cards
            # when player is bust on all hands
            # reveal the hole card, then finish, do not draw
            self.calculate_value()
            self.stage = BJStage.ROUND_OVER
            
        elif self.dealer_hand.get_best_value() is None:
            # dealer is bust
            self.calculate_value()
            self.stage = BJStage.ROUND_OVER

        elif self.dealer_hand.get_best_value() >= 17:
            if self.dealer_hand.is_soft_17() and self.rules.dealer_hits_soft_17:
                self.stage = BJStage.DEALER_CARD
            else:
                self.calculate_value()
                self.stage = BJStage.ROUND_OVER
        else: # value below 17
            self.stage = BJStage.DEALER_CARD


    def _dealer_can_have_bj(self):
        if self.dealer_hand.size() != 1:
            return False
        up_card = self.dealer_hand.cards[0]
        return up_card == 11 or up_card == 10
    

    def get_player_value(self):
        return self.player_value


    def get_player_bet(self):
        return self.total_player_bet
    
    
    def get_player_pay(self):
        return self.total_player_got


    def get_available_actions(self):
        if self.stage == BJStage.DEALER_CHECK_BJ:
            return [DealerAction.CONFIRM_BLACKJACK, DealerAction.CONFIRM_NO_BLACKJACK]
        
        if self.stage == BJStage.PLAYER_OFFERED_INSURANCE:
            return [PlayerAction.TAKE_INSURANCE, PlayerAction.REFUSE_INSURANCE]

        if self.stage == BJStage.PLAYER_OFFERED_EARLY_SURRENDER:
            return [PlayerAction.SURRENDER, PlayerAction.DECLINE_EARLY_SURRENDER]
        
        if self.stage != BJStage.PLAYER_ACTION:
            return []

        # BJStage.PLAYER_ACTION

        # Guard against bad index
        if self.active_hand_idx >= len(self.player_hands):
            raise RuntimeError("Invalid hand index")

        if not self.is_hand_in_progress[self.active_hand_idx]:
            raise RuntimeError("Hand is not in progress")
        
        hand = self.player_hands[self.active_hand_idx]
        # if we reached BJStage.PLAYER_ACTION stage, we can be sure hand value is not 21 - so can hit, stand, double, surrender 
        actions = [
            PlayerAction.STAND,
            PlayerAction.HIT
        ]

        first_hand_action = hand.size() == 2 
        first_hand_first_action = first_hand_action and len(self.player_hands) == 1
        # Double conditions: exactly two cards, not soft unless allowed,
        # allowed after split per rules, and not on natural 21
        if (
            first_hand_action
            and not (hand.is_soft() and not self.rules.allow_double_on_soft)
            and not (self.n_splits > 0 and not self.rules.allow_double_after_split)
        ):
            actions.append(PlayerAction.DOUBLE)

        # Split conditions: two cards under split limit.
        # Always allow same rank; allow different tens if rule enabled.
        if (
            first_hand_action
            and (
                self.rules.max_splits_allowed is None
                or self.n_splits < self.rules.max_splits_allowed
            )
        ):
            same_value = hand.is_same_value_pair()
            if same_value and self.rules.allow_split_different_tens:
                actions.append(PlayerAction.SPLIT)
            elif same_value and not self.rules.allow_split_different_tens:
                same_rank = hand.is_same_rank_pair()
                if same_rank:
                    actions.append(PlayerAction.SPLIT)
            
        # Late surrender: only available after dealer check or if dealer can't have BJ
        # Early surrender is handled as a separate stage, not as general player action
        if first_hand_first_action and self.rules.allow_late_surrender:
            actions.append(PlayerAction.SURRENDER)

        return actions
    

    def _can_early_surrender(self):
        if self.dealer_checked_blackjack or self.dealer_hand.size() != 1:
            return False

        if len(self.player_hands) > 1:
            return False
        
        if self.player_hands[0].is_natural_blackjack():
            return False

        up_card = self.dealer_hand.cards[0]
        
        # Check all three early surrender rules
        if self.rules.allow_early_surrender_on_all:
            return True
        if self.rules.allow_early_surrender_on_ace and up_card == 11:
            return True
        if self.rules.allow_early_surrender_on_ten and up_card == 10:
            return True
        
        return False


    def take_action(self, action):
        legal_actions = self.get_available_actions()
        if action not in legal_actions:
            raise RuntimeError(f"Illegal action {action} for current stage {self.stage}")

        # Set last action and clear last card
        self.last_action = action
        self.last_card = None

        # Handle dealer blackjack reveal step
        if self.stage == BJStage.DEALER_CHECK_BJ:
            self._dealer_check_action(action)
            return 

        # Handle insurance decision step
        if self.stage == BJStage.PLAYER_OFFERED_INSURANCE:
            self._insurance_action(action)
            return

        # Handle early surrender offer step
        if self.stage == BJStage.PLAYER_OFFERED_EARLY_SURRENDER:
            if action == PlayerAction.SURRENDER:
                self._action_early_surrender()
            elif action == PlayerAction.DECLINE_EARLY_SURRENDER:
                self._action_decline_early_surrender()
            else:
                raise RuntimeError(f"Unexpected early surrender action {action}")
            return
        
        # PLAYER_ACTION
        if self.stage != BJStage.PLAYER_ACTION:
            raise RuntimeError(f"Cannot take player action in stage {self.stage}")
        
        if not self.is_hand_in_progress[self.active_hand_idx]:
            raise RuntimeError(f"Current hand {self.active_hand_idx} has already been completed")

        if action == PlayerAction.STAND:
            self._action_stand()
        elif action == PlayerAction.HIT:
            self._action_hit()
        elif action == PlayerAction.DOUBLE:
            self._action_double()
        elif action == PlayerAction.SPLIT:
            self._action_split()
        elif action == PlayerAction.SURRENDER:
            self._action_late_surrender()
        else:
            raise RuntimeError(f"Unexpected action {action}")
 

    def _advance_to_next_or_dealer(self):
        # Move to the next in-progress hand index, or to dealer
        self.active_hand_idx += 1
        self._same_hand_or_next_or_dealer()


    def _same_hand_or_next_or_dealer(self):
        # Stay on the current in-progress hand index,
        # or go to next hand, or to dealer if no active hands
        while (
            self.active_hand_idx < len(self.player_hands)
            and not self.is_hand_in_progress[self.active_hand_idx]
        ):
            self.active_hand_idx += 1

        if self.active_hand_idx < len(self.player_hands):
            self.stage = BJStage.PLAYER_ACTION
        else:
            self.stage = BJStage.DEALER_CARD


    def _action_stand(self):
        self.is_hand_in_progress[self.active_hand_idx] = False
        self._advance_to_next_or_dealer()


    def _action_hit(self):
        self.stage = BJStage.PLAYER_CARD


    def _action_double(self):
        self.hand_bets[self.active_hand_idx] = 2 * self.bet_unit
        self.pending_double_bet = True
        self.stage = BJStage.PLAYER_CARD


    def _action_split(self):
        idx = self.active_hand_idx
        hand = self.player_hands[idx]
        new_hand_1, new_hand_2 = hand.split()
        # replace and insert
        # when split_order_left_right is False, the first/left card correspond 
        # to the first hand to be player, otherwise order is reversed
        if self.rules.split_order_reversed:
            new_hand_1, new_hand_2 = new_hand_2, new_hand_1
        self.player_hands[idx] = new_hand_1
        self.player_hands.insert(idx + 1, new_hand_2)
        self.n_splits += 1

        self.hand_bets.insert(idx + 1, self.bet_unit)
        self.is_hand_in_progress.insert(idx + 1, True)

        self.split_origin_idx = idx
        self.active_hand_idx = idx
        self.stage = BJStage.PLAYER_CARD


    def _action_early_surrender(self):
        self.surrendered = True
        self.early_surrendered = True
        self._finalize_surrender()

    def _action_late_surrender(self):
        self.surrendered = True
        self.early_surrendered = False
        self._finalize_surrender()

    def _finalize_surrender(self):
        if not self.rules.dealer_shows_card_on_surrender:
            self.calculate_value()
            self.stage = BJStage.ROUND_OVER
        else:
            self.stage = BJStage.DEALER_CARD

    def _action_decline_early_surrender(self):
        if self.rules.dealer_checks_blackjack and self._dealer_can_have_bj():
            self.stage = BJStage.DEALER_CHECK_BJ
        else:
            self._same_hand_or_next_or_dealer()


    def _insurance_action(self, action: PlayerAction):
        if action == PlayerAction.TAKE_INSURANCE:
            self.insurance_bet = self.bet_unit / 2
        elif action == PlayerAction.REFUSE_INSURANCE:
            self.insurance_bet = 0
        else:
            raise RuntimeError(f"Unexpected insurance action {action}")
        # After insurance decision, dealer may check blackjack, else proceed to player action
        if self.rules.dealer_checks_blackjack:
            self.stage = BJStage.DEALER_CHECK_BJ
        else:
            self._same_hand_or_next_or_dealer()


    def _dealer_check_action(self, action: DealerAction):
        self.dealer_checked_blackjack = True
        if action == DealerAction.CONFIRM_BLACKJACK:
            self.dealer_has_bj_after_check = True
            # Continue dealer flow to draw hole card if needed
            self.stage = BJStage.DEALER_CARD
        elif action == DealerAction.CONFIRM_NO_BLACKJACK:
            self.dealer_has_bj_after_check = False
            self._same_hand_or_next_or_dealer()
        else:
            raise RuntimeError(f"Unexpected dealer action {action}")


    def calculate_value(self):
        base_bets_total = sum(self.hand_bets)
        self.total_player_bet = base_bets_total + self.insurance_bet

        dealer_has_bj = self.dealer_hand.is_natural_blackjack()

        got_per_hand = []
        got_for_insurance = 0

        # Insurance settlement
        if self.insurance_bet > 0:
            if dealer_has_bj:
                got_for_insurance = self.insurance_bet + self.insurance_bet * self.rules.insurance_payout

        # Surrender handling: pay back half the base bet
        # Late surrender: only valid if dealer doesn't have blackjack
        if self.surrendered and (not dealer_has_bj or self.early_surrendered):
            if len(self.player_hands) != 1:
                raise RuntimeError("Cannot surrender after split")
            
            self.total_player_got = self.bet_unit * self.rules.surrender_payout + got_for_insurance
            self.player_value = self.total_player_got - self.total_player_bet
            return
        
        # Settle each hand
        dealer_value = self.dealer_hand.get_best_value()
        for i, hand in enumerate(self.player_hands):
            bet = self.hand_bets[i]

            if dealer_has_bj:
                # Base bet pushes if player also has a natural blackjack; otherwise loses
                if hand.is_natural_blackjack():
                    got_per_hand.append(bet)
                else:
                    got_per_hand.append(0)
                continue
            
            # Dealer does not have blackjack - check player blackjack
            if len(self.player_hands) == 1 and hand.is_natural_blackjack():
                got_per_hand.append(bet * (1 + self.rules.natural_blackjack_payout))
                continue
            
            # Dealer and Player do not have blackjack
            player_value = hand.get_best_value()
            if player_value is None:
                got_per_hand.append(0)
                continue

            if dealer_value is None:
                # Dealer busts; player wins
                got_per_hand.append(2 * bet)
                continue

            if player_value > dealer_value:
                got_per_hand.append(2 * bet)
            elif player_value == dealer_value:
                got_per_hand.append(bet)
            else:
                got_per_hand.append(0)

        self.total_player_got = sum(got_per_hand) + got_for_insurance
        self.player_value = self.total_player_got - self.total_player_bet
    

    def __str__(self, with_last_action=False):
        def hand_to_str(hand):
            return "".join(Rank.from_value(card).value for card in hand.cards)

        lines = []
        
        # Add last action/card information
        if with_last_action and self.last_action is not None:
            lines.append(f"Last action: {self.last_action.value}")
        if with_last_action and self.last_card is not None:
            lines.append(f"Last card: {self.last_card}")
        
        # Dealer lines
        dealer_parts = ["Dealer"]
        if self.dealer_hand.size() == 0:
            dealer_parts.append("(no cards)")
        elif self.dealer_hand.size() == 1:
            dealer_parts.append(str(self.dealer_hand))
            
            if self.insurance_bet > 0:
                dealer_parts.append(f"(insurance {self.insurance_bet})")
            
            # Add blackjack check result if dealer has checked
            if self.dealer_checked_blackjack:
                if self.dealer_has_bj_after_check:
                    dealer_parts.append("(checked - bj)")
                else:
                    dealer_parts.append("(checked - no bj)")
        else:
            card_str = str(self.dealer_hand)
            dealer_parts.append(f"{card_str}")
        
        lines.append(" ".join(dealer_parts))
        
        # Player lines
        if self.surrendered:
            hand = self.player_hands[0]
            card_str = str(hand)
            surrender_type = "early surrender" if self.early_surrendered else "late surrender"
            lines.append(f"Player {card_str} ({surrender_type})")
        else:
            # Normal play - show all hands
            player_lines = []
            for i, hand in enumerate(self.player_hands):
                if hand.size() == 0:
                    continue
                
                parts = []
                
                card_str = str(hand)
                parts.append(card_str)

                value = hand.get_best_value()
                if value is not None:
                    if hand.is_natural_blackjack():
                        parts.append(" bj")
                    elif not self.is_hand_in_progress[i]:
                        parts.append(f"-stand")


                bet = self.hand_bets[i]
                parts.append(f"[${bet}]")


                player_lines.append("".join(parts))
            
            # Join multiple hands with " | "
            if player_lines:
                lines.append("Player " + " | ".join(player_lines))
        
        # Final totals if round is over
        if self.stage == BJStage.ROUND_OVER:
            lines.append(f"Player total bet: {self.total_player_bet}")
            lines.append(f"Player total payout: {self.total_player_got}")
            net = self.player_value
            net_sign = "+" if net > 0 else ""  # minus is added automatically
            lines.append(f"Player net: {net_sign}{net}")

        return "\n".join(lines)