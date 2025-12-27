import logging
import queue
import threading
from abc import ABC, abstractmethod
from android_bot.image_assets import ImageAssets
from blackjack.cards import Rank
from blackjack.rules import BJRules
from blackjack.blackjack_round import BJRound, BJStage
from blackjack.actions import PlayerAction, DealerAction
from blackjack.hand import ValueOnlyHand
from android_bot import get_cards_tablet
from android_bot import image_utils
from blackjack_cpp import TreeWalker
import time

logger = logging.getLogger(__name__)


DEFAULT_RULES = BJRules(
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
    allow_split_different_tens = True
)


def card_list_str_to_rank_values(card_list_str):
    return tuple(Rank(s[:-1]).rank_value() for s in card_list_str)


def is_timeout_reached(timeout, start_time):
    if timeout is None:
        return False
    return time.time() - start_time > timeout


def drain_and_mark_done(q: queue.Queue) -> None:
    try:
        while True:
            q.get_nowait()
    except queue.Empty:
        pass

    try:
        while True:
            q.task_done()
    except ValueError:
        pass


def wait_click_ui_element(
    actor, ui_element_img, screen_taker,
    timeout_s=None,
    wait_to_disappear=False,
    crop=None
):
    """ wait for ui element to appear and click on it until it disappears """

    start_time = time.time()
    ui_appeared = False
    while True:
        if is_timeout_reached(timeout_s, start_time):
            return False

        game_img = screen_taker.get_screen()
        if crop is not None:
            game_img = game_img[crop[0]:crop[1], crop[2]:crop[3]]
        positions = image_utils.find_template_middle_positions(game_img, [ui_element_img])
        
        if len(positions) > 0:
            ui_appeared = True
            position = positions[0]
            if crop is not None:
                position = (position[0] + crop[2], position[1] + crop[0])
            actor.click(*position)
            if not wait_to_disappear:
                return True
        elif len(positions) == 0 and ui_appeared:
            return True

            

class DecisionMaker(ABC):
    def __init__(self, rules=DEFAULT_RULES):
        self.rules = rules

    @abstractmethod
    def start_round(self, bet):
        raise NotImplementedError

    @abstractmethod
    def take_card(self, card):
        raise NotImplementedError

    @abstractmethod
    def take_player_action(self, action):
        raise NotImplementedError

    @abstractmethod
    def take_dealer_action(self, action):
        raise NotImplementedError

    @abstractmethod
    def wait_for_best_player_action(self):
        raise NotImplementedError

    def waitForBestPlayerAction(self):
        return self.wait_for_best_player_action()


class TWDecisionMaker(DecisionMaker):
    def __init__(self, shoe=None, ev_calculator=None, rules=DEFAULT_RULES):
        super().__init__(rules=rules)
        if shoe is None and ev_calculator is None:
            raise ValueError("Either shoe or ev_calculator must be provided")
        if shoe is not None and ev_calculator is not None:
            raise ValueError("Only one of shoe or ev_calculator must be provided")

        self._worker_exception = None
        self.round = BJRound(self.rules)
        self.tree_walker = None
        self.shoe = shoe
        self.ev_calculator = ev_calculator
        self._current_bet = None
        self._shoe_lock = threading.Lock()
        self._current_best_action = None
        self._event_queue = queue.Queue()
        self._worker_thread = threading.Thread(target=self._worker_loop, daemon=True)
        self._worker_thread.start()

    def start_round(self, bet):
        self._enqueue_event("start_round", bet)

    def take_card(self, card):
        self._enqueue_event("card", card)

    def take_player_action(self, action):
        self._enqueue_event("player_action", action)

    def take_dealer_action(self, action):
        self._enqueue_event("dealer_action", action)

    def _enqueue_event(self, event_type, payload):
        if self._worker_exception is not None:
            raise self._worker_exception
        if not self._worker_thread.is_alive():
            raise RuntimeError("Worker thread is not alive, cannot enqueue event")
        self._event_queue.put((event_type, payload))

    def wait_for_best_player_action(self):
        if not self._worker_thread.is_alive() and self._event_queue.qsize() > 0:
            raise RuntimeError("Worker thread is not alive and events are still in queue")
        self._event_queue.join()
        self._raise_if_worker_failed()
        if self._current_best_action is None:
            raise RuntimeError("Best action not available after processing all events")
        return self._current_best_action

    def get_shoe(self):
        with self._shoe_lock:
            return self.shoe.copy()

    def _worker_loop(self):
        while True:
            try:
                event_type, payload = self._event_queue.get_nowait()
            except queue.Empty:
                try:
                    self._update_best_action()
                except Exception as exc:
                    self._set_exception_drain_queue(exc)
                    return
                event_type, payload = self._event_queue.get()

            try:
                if event_type == "start_round":
                    self._handle_start_round(payload)
                elif event_type == "card":
                    self._handle_card(payload)
                elif event_type == "player_action":
                    self._handle_player_action(payload)
                elif event_type == "dealer_action":
                    self._handle_dealer_action(payload)
                else:
                    raise ValueError(f"Unsupported event type {event_type}")
                self._event_queue.task_done()

            except Exception as exc:
                self._set_exception_drain_queue(exc)
                return


    def _set_exception_drain_queue(self, exc):
        self._worker_exception = exc
        drain_and_mark_done(self._event_queue)


    def _raise_if_worker_failed(self):
        if self._worker_exception is not None:
            raise self._worker_exception

    def _handle_start_round(self, bet):
        self.round = BJRound(self.rules)
        self.tree_walker = None
        self._current_bet = bet
        self._current_best_action = None
        self.round.start_round(bet)

    def _handle_card(self, card):
        stage = self.round.get_stage()
        self.round.take_card(card)
        self._burn_shoe_rank(card)
        if stage == BJStage.PLAYER_CARD and self.tree_walker is not None:
            self.tree_walker.take_card(card)
        self._maybe_init_tree_walker()

    def _handle_player_action(self, action):
        self.round.take_action(action)
        if self.tree_walker is not None:
            self.tree_walker.take_player_action(action.value)

    def _handle_dealer_action(self, action):
        if self.tree_walker is None:
            # we do not need treewalker if dealer checks for blackjack with 10 when player has blackjack
            if not (
                self.round.get_stage() == BJStage.DEALER_CHECK_BJ
                and self.round.get_dealer_hand()[0] == 10
            ):
                raise ValueError("Dealer action not allowed in stage other than dealer check blackjack")
        else:
            self.tree_walker.take_dealer_action(action.value)
        self.round.take_action(action)

    def _burn_shoe_rank(self, card):
        with self._shoe_lock:
            if self.shoe is None:
                return
            self.shoe.burn_rank_value(card)

    def _maybe_init_tree_walker(self):
        if self.tree_walker is not None:
            return
        if len(self.round.get_dealer_hand()) < 1:
            return
        if len(self.round.player_hands) < 1 or len(self.round.player_hands[0]) < 2:
            return
        if self.round.get_stage() in [BJStage.DEALER_CARD, BJStage.ROUND_OVER]:
            return
        if self._current_bet is None:
            return
        self._init_tree_walker(self._current_bet)

    def _init_tree_walker(self, bet):
        if self.tree_walker is not None:
            raise ValueError("TreeWalker already initialized")
        if self.shoe is not None:
            self._create_new_tree_walker_with_shoe(bet)
        elif self.ev_calculator is not None:
            self._get_tree_walker_from_ev_calculator(bet)
        else:
            raise ValueError("Either shoe or ev_calculator must be provided")

    def _create_new_tree_walker_with_shoe(self, bet):
        player_cards = self.round.player_hands[0].cards
        dealer_card = self.round.get_dealer_hand()[0]
        self.tree_walker = TreeWalker.build_initial(
            (player_cards[0], player_cards[1]), dealer_card,
            self.round.rules.to_cpp(),
            self.shoe.get_rank_count(),
            bet_unit=bet,
            initial_cards_burned=True
        )

    def _get_tree_walker_from_ev_calculator(self, bet):
        player_cards = self.round.player_hands[0].cards
        dealer_card = self.round.get_dealer_hand()[0]
        self.tree_walker = self.ev_calculator.create_tree_walker(
            (player_cards[0], player_cards[1]), dealer_card)

    def _choose_treewalker_action(self) -> str:
        if self.tree_walker is None:
            raise RuntimeError("TreeWalker not initialized")
        simple_best_action = self.tree_walker.get_best_action()
        if simple_best_action is not None:
            return simple_best_action
        best_actions = self.tree_walker.get_best_actions()
        best_action, _value_estimate = best_actions[0]
        return best_action

    def _update_best_action(self):
        if not self.round.need_player_action():
            self._current_best_action = None
            return
        action_str = self._choose_treewalker_action()
        if action_str is None:
            raise RuntimeError("TreeWalker did not return an available action")
        self._current_best_action = PlayerAction[action_str]


class TabletPlayer:
    rules = DEFAULT_RULES

    def __init__(self, actor, scr_taker, decision_maker):
        self.actor = actor
        self.scr_taker = scr_taker
        self.decision_maker = decision_maker
        self.round = BJRound(self.rules)

        # Thread-safe screen capture
        self._screen_lock = threading.Lock()
        self._current_screen = None
        self._capture_thread = None
        self._stop_capture = threading.Event()

        self.start_screen_capture()


    def start_screen_capture(self):
        return
        
        # """Start the background screen capture thread."""
        # self._stop_capture.clear()
        # self._capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
        # self._capture_thread.start()
            
        # # Wait for first frame to be captured
        # while self.get_screen() is None:
        #     time.sleep(0.01)


    def stop_screen_capture(self):
        return

        # """Stop the background screen capture thread."""
        # self._stop_capture.set()
        
        # if self._capture_thread is not None:
        #     self._capture_thread.join()
        #     self._capture_thread = None


    def _capture_loop(self):
        """Background loop that continuously captures the screen."""
        while not self._stop_capture.is_set():
            screen = self.scr_taker.get_screen()
            with self._screen_lock:
                self._current_screen = screen
            time.sleep(0.01)


    def get_screen(self):
        return self.scr_taker.get_screen()
        # """Get the current screen image (thread-safe)."""
        # with self._screen_lock:
        #     return self._current_screen


    def __del__(self):
        if self._capture_thread is not None:
            self.stop_screen_capture()


    def is_pre_shuffle(self):
        game_img = self.get_screen()
        return get_cards_tablet.is_pre_shuffle(game_img)


    def play_round(self, bet):
        self.initial_stage_round(bet)
        # check if round is over after initial stage - dealer or player has blackjack
        if self.round.get_stage() != BJStage.ROUND_OVER:
            self.decisions_stage_round()
        self.end_round()


    def initial_stage_round(self, bet):
        self.round = BJRound(self.rules)
        game_img = self.get_screen()
        while not get_cards_tablet.is_table_empty(game_img):
            time.sleep(0.5)
            game_img = self.get_screen()
        self.round.start_round(bet)
        self.decision_maker.start_round(bet)
        self.actor.place_bet(bet)
        time.sleep(0.5)
        self.actor.deal()
        self.wait_for_initial_cards()

        logger.info("initial hand")
        logger.info(self.round)
        player_had_bj = self.round.player_hands[0].is_natural_blackjack()

        # dont get active hand - hand can be inactive in case of blackjacks
        dealer_hand = self.round.get_dealer_hand()
        insurance_action = None
        if self.round.get_stage() == BJStage.PLAYER_OFFERED_INSURANCE:
            insurance_action = self.decision_maker.wait_for_best_player_action()
            
        if not player_had_bj:
            if self.round.get_dealer_hand()[0] == 10:
                self.wait_for_dealer_bj_or_action_request()
            elif self.round.get_dealer_hand()[0] == 11:
                # if dealer has A, insurance is always offered
                if insurance_action == PlayerAction.TAKE_INSURANCE:
                    self.take_insurance()
                else:
                    self.refuse_insurance()
                self.wait_for_dealer_bj_or_action_request()
            else:
                self.wait_for_action_request()
        else:  # player has blackjack
            if self.round.get_dealer_hand()[0] == 11:
                if insurance_action == PlayerAction.TAKE_INSURANCE:
                    self.take_insurance()
                else:
                    self.refuse_insurance()
            # sometimes dealer both 2 cards are captured by wait_for_initial_cards
            # instead of one
            self.wait_for_dealer_full_hand(only_two_cards=True)


    def decisions_stage_round(self):
        logger.info("decisions_stage_round")
        while self.round.need_player_action():
            logger.info("wait_for_best_player_action")
            action = self.decision_maker.wait_for_best_player_action()
            if action is None:
                raise RuntimeError("TreeWalker did not return an available action")
            self.take_action(action)
        
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
        game_img = self.get_screen()
        # failed to register action - try again
        # should choose a different approach with stand on split pair
        # when standing on the first pair of hand
        # I need to check that  
        # round should have been updated before button is pressed
        stand_on_first_hand = (action == PlayerAction.STAND) and (self.round.active_hand_idx == 1)

        if stand_on_first_hand:
            time.sleep(1)  # give an opportunity to switch to left hand
            game_img = self.get_screen()
            active_hand_position = get_cards_tablet.get_active_hand(game_img)
            # should switch to left
            if active_hand_position == get_cards_tablet.HandPosition.RIGHT: 
                logger.warning(f"second attempt {action} on first hand")
                self.actor.take_action(action)

        elif not stand_on_first_hand:
            if get_cards_tablet.can_hit_stand(game_img):
                logger.warning(f"second attempt {action}")
                self.actor.take_action(action)


    def double(self):
        self.round.take_action(PlayerAction.DOUBLE)
        self.decision_maker.take_player_action(PlayerAction.DOUBLE)
        self.press_android_button(PlayerAction.DOUBLE)
        self.wait_for_player_card()

    def split(self):
        self.round.take_action(PlayerAction.SPLIT)
        self.decision_maker.take_player_action(PlayerAction.SPLIT)
        self.press_android_button(PlayerAction.SPLIT)
        self.wait_for_player_card()
        self.wait_for_player_card()

    def hit(self):
        self.round.take_action(PlayerAction.HIT)
        self.decision_maker.take_player_action(PlayerAction.HIT)
        self.press_android_button(PlayerAction.HIT)
        self.wait_for_player_card()

    def stand(self):
        self.round.take_action(PlayerAction.STAND)
        self.decision_maker.take_player_action(PlayerAction.STAND)
        self.press_android_button(PlayerAction.STAND)

    def refuse_insurance(self):
        self.wait_for_insurance_option()
        self.round.take_action(PlayerAction.REFUSE_INSURANCE)
        self.decision_maker.take_player_action(PlayerAction.REFUSE_INSURANCE)
        self.actor.refuse_insurance()

    def take_insurance(self):
        self.wait_for_insurance_option()
        self.round.take_action(PlayerAction.TAKE_INSURANCE)
        self.decision_maker.take_player_action(PlayerAction.TAKE_INSURANCE)
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
        self.decision_maker.take_card(card)


    def wait_for_action_btn(self, timeout_s=None):
        start_time = time.time()
        while True:
            if is_timeout_reached(timeout_s, start_time):
                return False
            game_img = self.get_screen()
            can_stand = get_cards_tablet.can_hit_stand(game_img)
            if can_stand:
                return True

    def end_round(self):
        logger.info("final stage")
        assert self.round.get_stage() == BJStage.ROUND_OVER
        logger.info(self.round)
        

    def wait_for_initial_cards(self, timeout_s=None):
        logger.info("wait_for_initial_cards")
        start_time = time.time()
        game_img = self.get_screen()
        dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
        middle_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_player_cards_middle(game_img))

        dealer_cards_last = dealer_cards_new
        middle_cards_last = middle_cards_new

        cards_confirmed = False
        while True:
            if is_timeout_reached(timeout_s, start_time):
                return False
            if (
                cards_confirmed 
                and len(dealer_cards_new) >= 1
                and len(middle_cards_new) >= 2
            ):
                break

            game_img = self.get_screen()
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
                self.take_dealer_action(DealerAction.CONFIRM_BLACKJACK)
            elif dealer_cards_new[0] == 10: 
                # dealer checks for blackjack at 10 and 11 but for A(11) she first has to offer insurance
                self.take_dealer_action(DealerAction.CONFIRM_NO_BLACKJACK)
            self.take_card(dealer_cards_new[1])
        return True


    def wait_for_insurance_option(self, timeout_s=None):
        logger.info("wait_for_insurance_option")
        start_time = time.time()
        game_img = self.get_screen()
        while not get_cards_tablet.is_insurance_offered(game_img):
            if is_timeout_reached(timeout_s, start_time):
                return False
            game_img = self.get_screen()
        return True


    def wait_for_action_request(self, timeout_s=None):
        logger.info("wait_for_action_request")
        start_time = time.time()
        game_img = self.get_screen()
        active_hand_position = get_cards_tablet.get_active_hand(game_img)
        while active_hand_position == get_cards_tablet.HandPosition.NONE:
            if is_timeout_reached(timeout_s, start_time):
                return False
            game_img = self.get_screen()
            active_hand_position = get_cards_tablet.get_active_hand(game_img)
        return True


    def wait_for_dealer_bj_or_action_request(self, timeout_s=None):
        # after insurance refused dealer either shows bj or points to my hand
        logger.info("wait_for_dealer_bj_or_action_request")
        current_dealer_hand = self.round.get_dealer_hand()
        if len(current_dealer_hand) >= 2:
            # dealer already has 2 cards - this is only possible if dealer has blackjack
            # and this was confirmed when checking initial cards
            return True
        
        start_time = time.time()
        game_img = self.get_screen()
        dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
        dealer_cards_last = dealer_cards_new
        
        dealer_cards_confirmed = False
        action_request = False
        dealer_blackjack = False
        active_hand_position = get_cards_tablet.get_active_hand(game_img)
        while True:
            if is_timeout_reached(timeout_s, start_time):
                return False
            # dealer shows blackjack
            if dealer_cards_confirmed and len(dealer_cards_new) >= 2:
                dealer_blackjack = True
                break
            # dealer asks for action
            if active_hand_position != get_cards_tablet.HandPosition.NONE:
                action_request = True
                break
                
            game_img = self.get_screen()
            active_hand_position = get_cards_tablet.get_active_hand(game_img)
            dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
            
            if dealer_cards_new == dealer_cards_last:
                dealer_cards_confirmed = True
            else:
                dealer_cards_last = dealer_cards_new
                dealer_cards_confirmed = False

        if dealer_blackjack:
            self.take_dealer_action(DealerAction.CONFIRM_BLACKJACK)
            self.take_card(dealer_cards_new[1])
        else:
            self.take_dealer_action(DealerAction.CONFIRM_NO_BLACKJACK)
        return True
        

    def wait_for_dealer_full_hand(self, only_two_cards=False, timeout_s=None):
        logger.info(f"wait_for_dealer_full_hand only_two_cards {only_two_cards}")
        if only_two_cards and len(self.round.get_dealer_hand()) >= 2:
            return True
        
        start_time = time.time()
        game_img = self.get_screen()

        dealer_cards_new = card_list_str_to_rank_values(get_cards_tablet.get_dealer_cards(game_img))
        dealer_cards_last = dealer_cards_new
        current_hand = ValueOnlyHand(dealer_cards_new)
        
        dealer_confirmed = False
        
        n_start_cards = len(self.round.get_dealer_hand())
        while True:
            if is_timeout_reached(timeout_s, start_time):
                return False
            if dealer_confirmed: 
                if only_two_cards:
                    if len(current_hand) >= 2:
                        break
                else:
                    if current_hand.is_bust() or current_hand.get_best_value() >= 17:
                        break
        
            game_img = self.get_screen()
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
                self.take_dealer_action(DealerAction.CONFIRM_BLACKJACK)
            else:
                self.take_dealer_action(DealerAction.CONFIRM_NO_BLACKJACK)

        for c in current_hand.cards[n_start_cards:]:
            self.take_card(c)
        return True


    def wait_for_player_card(self, timeout_s=None):
        logger.info("wait_for_player_card")
        start_time = time.time()
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
        game_img = self.get_screen()
        hand_cards_new = card_list_str_to_rank_values(
            get_cards_tablet.get_player_cards(game_img, active_hand_position)
        )
        hand_cards_last = hand_cards_new
        
        hand_confirmed = False
        while True:
            if is_timeout_reached(timeout_s, start_time):
                return False
            # print(hand_confirmed, hand_cards_new, visible_old_tail)
            if (
                hand_confirmed 
                and hand_cards_new[:-1] == visible_old_tail
                and len(hand_cards_new) == len(visible_old_tail) + 1
            ):
                break

            game_img = self.get_screen()
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
        return True


    def take_dealer_action(self, action):
        self.round.take_action(action)
        self.decision_maker.take_dealer_action(action)


    def rebuy(self):
        crop = (860, 1200, 1100, 1200)

        success = False
        while not success:
            time.sleep(1)

            self.actor.start_rebuy()
            success = wait_click_ui_element(
                self.actor, ImageAssets.rebuy_confirm_1,
                self, timeout_s=10, crop=crop
            )
            if not success:
                continue
            success = wait_click_ui_element(
                self.actor, ImageAssets.rebuy_confirm_2,
                self, timeout_s=10, crop=crop
            )
