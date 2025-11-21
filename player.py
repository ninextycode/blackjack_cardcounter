import logging
from blackjack.cards import Rank
from blackjack.rules import BJRules
from blackjack.blackjack_round import BJRound, BJStage
from blackjack.actions import PlayerAction, DealerAction
from blackjack.hand import ValueOnlyHand
import get_cards_tablet
import time

logger = logging.getLogger(__name__)


def card_list_str_to_rank_values(card_list_str):
    return tuple(Rank(s[:-1]).rank_value() for s in card_list_str)


class Player:
    rules = BJRules(
        dealer_checks_blackjack = True,
        dealer_hits_soft_17 = False,
        allow_late_surrender = False,
        allow_early_surrender_on_ten = False,
        allow_early_surrender_on_ace = False,
        allow_early_surrender_on_all = False,
        dealer_shows_card_on_surrender = True,
        allow_insurance_vs_ace = True,
        natural_blackjack_payout = 3 / 2,
        surrender_payout = 1 / 2,
        insurance_payout = 2 / 1,
        max_splits_allowed = 1,
        allow_action_on_split_aces = True,
        allow_double_after_split = True,
        allow_double_on_soft = True,
        allow_split_different_tens = True,
        no_natural_bj_on_split = True,
        split_order_reversed = True
    )

    def __init__(self, actor, scr_taker, card_counter, strategy):
        self.actor = actor
        self.scr_taker = scr_taker  # "/media/maxim/T7/frames/")
        self.round = BJRound(Player.rules)
        self.card_counter = card_counter
        self.strategy = strategy

    def is_pre_shuffle(self):
        game_img = self.scr_taker.get_screen()
        return get_cards_tablet.is_pre_shuffle(game_img)

    def reset_round(self):
        self.round = BJRound(Player.rules)

    def play_round(self, bet=1000):
        self.reset_round()
        self.initial_stage_round(bet)
        # check if round is over after initial stage - dealer or player has blackjack
        if self.round.get_stage() != BJStage.ROUND_OVER:
            self.decisions_stage_round()
        self.end_round()


    def initial_stage_round(self, bet):
        game_img = self.scr_taker.get_screen()
        while not get_cards_tablet.is_table_empty(game_img):
            time.sleep(0.5)
            game_img = self.scr_taker.get_screen()
        self.round.start_round(bet)
        self.actor.place_bet(bet)
        time.sleep(0.5)
        self.actor.deal()
        self.wait_for_initial_cards()

        logger.info("initial hand")
        logger.info(self.round)
        logger.info(f"true count: {self.card_counter.get_true_count()}")

        player_had_bj = self.round.player_hands[0].is_natural_blackjack()

        # dont get active hand - hand can be inactive in case of blackjacks
        player_hand = self.round.player_hands[0]
        dealer_hand = self.round.get_dealer_hand()
        strategy_actions = []
        if len(dealer_hand) == 1:
            # dealer haven't shown blackjack yet - may need to take insurance
            strategy_actions = self.strategy.get_actions(
                player_hand.get_best_value(),
                player_hand.is_soft(),
                player_hand.is_same_value_pair(),
                dealer_hand.get_best_value(),
                self.card_counter.get_true_count()
            )
        should_take_insurance = False
        for a in strategy_actions:
            if a == PlayerAction.TAKE_INSURANCE:
                should_take_insurance = True
                break
            elif a == PlayerAction.REFUSE_INSURANCE:
                break
            
        if not player_had_bj:
            if self.round.get_dealer_hand()[0] == 10:
                self.wait_for_dealer_bj_or_action_request()
            elif self.round.get_dealer_hand()[0] == 11:
                # if dealer has A, insurance is always offered
                if should_take_insurance:
                    self.take_insurance()
                else:
                    self.refuse_insurance()
                self.wait_for_dealer_bj_or_action_request()
            else:
                self.wait_for_action_request()
        else:  # player has blackjack
            if self.round.get_dealer_hand()[0] == 11:
                if should_take_insurance:
                    self.take_insurance()
                else:
                    self.refuse_insurance()
            # sometimes dealer both 2 cards are captured by wait_for_initial_cards
            # instead of one
            self.wait_for_dealer_full_hand(only_two_cards=True)


    def decisions_stage_round(self):
        while self.round.need_player_action():
            player_hand = self.round.get_active_player_hand()
            dealer_hand = self.round.get_dealer_hand()
                
            strategy_actions = self.strategy.get_actions(
                player_hand.get_best_value(),
                player_hand.is_soft(),
                player_hand.is_same_value_pair(),
                dealer_hand.get_best_value(),
                self.card_counter.get_true_count()
            )
            possible_actions = self.round.get_available_actions()
            for action in strategy_actions:
                if action in possible_actions:
                    self.take_action(action)
                    break
        
        self.wait_for_dealer_full_hand(
            only_two_cards=all(hand.is_bust() for hand in self.round.player_hands)
        )


    def take_action(self, action):
        if action == PlayerAction.DOUBLE:
            self.double()
        elif action == PlayerAction.SPLIT:
            self.split()
        elif action == PlayerAction.HIT:
            self.hit()
        elif action == PlayerAction.STAND:
            self.stand()
        elif action == PlayerAction.REFUSE_INSURANCE:
            self.refuse_insurance()
        elif action == PlayerAction.TAKE_INSURANCE:
            self.take_insurance()
        else:
            raise ValueError(f"Unsupported action {action}")

    def press_android_button(self, action):
        self.wait_for_action_btn()
        self.btn_action_delay()
        self.actor.take_action(action)
        game_img = self.scr_taker.get_screen()
        # failed to register action - try again
        # should choose a different approach with stand on split pair
        # when standing on the first pair of hand
        # I need to check that  
        # round should have been updated before button is pressed
        stand_on_first_hand = (action == PlayerAction.STAND) and (self.round.active_hand_idx == 1)
        if not stand_on_first_hand:
            if get_cards_tablet.can_hit_stand(game_img):
                logger.warning(f"second attempt {action}")
                self.actor.take_action(action)
        elif stand_on_first_hand:
            active_hand_position = get_cards_tablet.get_active_hand(game_img)
            # should switch to left
            if active_hand_position == get_cards_tablet.HandPosition.RIGHT: 
                logger.warning(f"second attempt {action} on first hand")
                self.actor.take_action(action)

    def double(self):
        self.round.take_action(PlayerAction.DOUBLE)
        self.press_android_button(PlayerAction.DOUBLE)
        self.wait_for_player_card()

    def split(self):
        self.round.take_action(PlayerAction.SPLIT)
        self.press_android_button(PlayerAction.SPLIT)
        self.wait_for_player_card()
        self.wait_for_player_card()

    def hit(self):
        self.round.take_action(PlayerAction.HIT)
        self.press_android_button(PlayerAction.HIT)
        self.wait_for_player_card()

    def stand(self):
        self.round.take_action(PlayerAction.STAND)
        self.press_android_button(PlayerAction.STAND)

    def refuse_insurance(self):
        self.wait_for_insurance_option()
        self.round.take_action(PlayerAction.REFUSE_INSURANCE)
        self.actor.refuse_insurance()

    def take_insurance(self):
        self.wait_for_insurance_option()
        self.round.take_action(PlayerAction.TAKE_INSURANCE)
        self.actor.take_insurance()
  

    def btn_action_delay(self):
        # active_hand = self.round.get_active_player_hand()
        # hand_idx = self.round.active_hand_idx
        # # if the hand is the second hand in a pair, 
        # # and this is a first action, add a delay
        # if hand_idx > 0 and len(active_hand) == 2:     
        pass
        # time.sleep(1)

    def take_card(self, card):
        self.round.take_card(card)
        self.card_counter.update_count(card)

    def wait_for_action_btn(self):
        while True:
            game_img = self.scr_taker.get_screen()
            can_stand = get_cards_tablet.can_hit_stand(game_img)
            if can_stand:
                break

    def end_round(self):
        logger.info("final stage")
        assert self.round.get_stage() == BJStage.ROUND_OVER
        logger.info(self.round)

        self.actor.rebuy()
        

    def wait_for_initial_cards(self):
        logger.info("wait_for_initial_cards")
        game_img = self.scr_taker.get_screen()
        dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
        middle_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_player_cards_middle(game_img))

        dealer_cards_last = dealer_cards_new
        middle_cards_last = middle_cards_new

        cards_confirmed = False
        while True:
            if (
                cards_confirmed 
                and len(dealer_cards_new) >= 1
                and len(middle_cards_new) >= 2
            ):
                break

            game_img = self.scr_taker.get_screen()
            dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
            middle_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_player_cards_middle(game_img))

            if dealer_cards_new == dealer_cards_last and middle_cards_new == middle_cards_last:
                cards_confirmed = True
            else:
                cards_confirmed = False
                dealer_cards_last = dealer_cards_new
                middle_cards_last = middle_cards_new

        self.take_card(middle_cards_new[0])
        self.take_card(middle_cards_new[1])
        self.take_card(dealer_cards_new[0])
        # on this stage the dealer can show 2 cards - if she has blackjack 
        # or if player has bj and she shows that she doesn't have one
        # I need to do the check here because here I will send card to take_card and this should happen after bj check
        if len(dealer_cards_new) >= 2:
            if sum(dealer_cards_new) == 21:
                self.round.take_action(DealerAction.CONFIRM_BLACKJACK)
            elif dealer_cards_new[0] == 10: 
                # dealer checks for blackjack at 10 and 11 but for A(11) she first has to offer insurance
                self.round.take_action(DealerAction.CONFIRM_NO_BLACKJACK)
            self.take_card(dealer_cards_new[1])


    def wait_for_insurance_option(self):
        logger.info("wait_for_insurance_option")
        game_img = self.scr_taker.get_screen()
        while not get_cards_tablet.is_insurance_offered(game_img):
            game_img = self.scr_taker.get_screen()


    def wait_for_action_request(self):
        logger.info("wait_for_action_request")
        game_img = self.scr_taker.get_screen()
        active_hand_position = get_cards_tablet.get_active_hand(game_img)
        while active_hand_position == get_cards_tablet.HandPosition.NONE:
            game_img = self.scr_taker.get_screen()
            active_hand_position = get_cards_tablet.get_active_hand(game_img)


    def wait_for_dealer_bj_or_action_request(self):
        # after insurance refused dealer either shows bj or points to my hand
        logger.info("wait_for_dealer_bj_or_action_request")
        current_dealer_hand = self.round.get_dealer_hand()
        if len(current_dealer_hand) >= 2:
            # dealer already has 2 cards - this is only possible if dealer has blackjack
            # and this was confirmed when checking initial cards
            return
        
        game_img = self.scr_taker.get_screen()
        dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
        dealer_cards_last = dealer_cards_new
        
        dealer_cards_confirmed = False
        action_request = False
        dealer_blackjack = False
        active_hand_position = get_cards_tablet.get_active_hand(game_img)
        while True:
            # dealer shows blackjack
            if dealer_cards_confirmed and len(dealer_cards_new) >= 2:
                dealer_blackjack = True
                break
            # dealer asks for action
            if active_hand_position != get_cards_tablet.HandPosition.NONE:
                action_request = True
                break
                
            game_img = self.scr_taker.get_screen()
            active_hand_position = get_cards_tablet.get_active_hand(game_img)
            dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
            
            if dealer_cards_new == dealer_cards_last:
                dealer_cards_confirmed = True
            else:
                dealer_cards_last = dealer_cards_new
                dealer_cards_confirmed = False

        if dealer_blackjack:
            self.round.take_action(DealerAction.CONFIRM_BLACKJACK)
            self.take_card(dealer_cards_new[1])
        else:
            self.round.take_action(DealerAction.CONFIRM_NO_BLACKJACK)
        

    def wait_for_dealer_full_hand(self, only_two_cards=False):
        logger.info(f"wait_for_dealer_full_hand only_two_cards {only_two_cards}")
        if only_two_cards and len(self.round.get_dealer_hand()) >= 2:
            return
        
        game_img = self.scr_taker.get_screen()

        dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
        dealer_cards_last = dealer_cards_new
        current_hand = ValueOnlyHand(dealer_cards_new)
        
        dealer_confirmed = False
        
        n_start_cards = len(self.round.get_dealer_hand())
        while True:
            if dealer_confirmed: 
                if only_two_cards:
                    if len(current_hand) >= 2:
                        break
                else:
                    if current_hand.is_bust() or current_hand.get_best_value() >= 17:
                        break
        
            game_img = self.scr_taker.get_screen()
            dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
            # print(dealer_cards_new)
            if dealer_cards_new == dealer_cards_last:
                dealer_confirmed = True
                current_hand = ValueOnlyHand(dealer_cards_new)
            else:
                dealer_confirmed = False
                dealer_cards_last = dealer_cards_new
                current_hand = None

        if self.round.get_stage() == BJStage.DEALER_CHECK_BJ:
            logger.info("second check bj tested")
            if sum(dealer_cards_new) == 21:
                self.round.take_action(DealerAction.CONFIRM_BLACKJACK)
            else:
                self.round.take_action(DealerAction.CONFIRM_NO_BLACKJACK)

        for c in current_hand.cards[n_start_cards:]:
            self.take_card(c)


    def wait_for_player_card(self):
        logger.info("wait_for_player_card")
        round_hand_idx = self.round.active_hand_idx
        n_hands = len(self.round.player_hands)
        if n_hands == 1 and round_hand_idx == 0:
            active_hand_position = get_cards_tablet.HandPosition.MIDDLE
        elif n_hands == 2 and round_hand_idx == 0:
            # hands go in the order of right -> left
            active_hand_position = get_cards_tablet.HandPosition.RIGHT
        elif n_hands == 2 and round_hand_idx == 1:
            active_hand_position = get_cards_tablet.HandPosition.LEFT
        else:
            raise ValueError(f"Unsupported hands state n={n_hands}, idx={round_hand_idx}")
        
        hand = self.round.get_active_player_hand()
        
        # 4 cards in each line 
        n_cards_last_line = len(hand) % 4
        if len(hand) > 0 and n_cards_last_line == 0:
            n_cards_last_line = 4

        # print(n_cards_last_line)
        # 6th card covers cards 1,2,3,4 (entire previous line)
        visible_tail_idx = slice(-n_cards_last_line, None)
        if n_cards_last_line == 4:
            # 5th card covers cards 1,2,3
            visible_tail_idx = slice(-1, None)
        # 6th (2nd in the new line) will cover the entire previous line
        # no need to adjust slice, just take the last (current) line
        
        visible_old_tail = tuple(hand[visible_tail_idx])
        game_img = self.scr_taker.get_screen()
        hand_cards_new = card_list_str_to_rank_values(
            get_cards_tablet.get_player_cards(game_img, active_hand_position)
        )
        hand_cards_last = hand_cards_new
        
        hand_confirmed = False
        while True:
            # print(hand_confirmed, hand_cards_new, visible_old_tail)
            if (
                hand_confirmed 
                and hand_cards_new[:-1] == visible_old_tail
                and len(hand_cards_new) == len(visible_old_tail) + 1
            ):
                break

            game_img = self.scr_taker.get_screen()
            hand_cards_new = card_list_str_to_rank_values(
                get_cards_tablet.get_player_cards(game_img, active_hand_position)
            )
            
            if hand_cards_new == hand_cards_last:
                hand_confirmed = True
            else:
                hand_confirmed = False
                hand_cards_last = hand_cards_new
        
        new_card = hand_cards_new[-1]
        self.take_card(new_card)

