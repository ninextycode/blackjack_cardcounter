raise NotImplementedError

from blackjack.cards import Deck, Hand, Rank
from abc import ABC, abstractmethod
import logging
from blackjack.actions import PlayerAction



class Dealer:
    def __init__(self, hit_on_soft_17):
        self.hand: Hand = Hand()
        self.hit_on_soft_17 = hit_on_soft_17


    def add_card(self, c):
        self.hand.add_card(c)


    def take_action(self):
        value = self.hand.get_best_value()
        if value is None: # dealer is bust
            return PlayerAction.STAND
        elif value <= 16:
            return PlayerAction.HIT
        elif self.hit_on_soft_17 and self.hand.is_soft_17():
            return PlayerAction.HIT
        else:
            return PlayerAction.STAND


class Rules:
    def __init__(self):
        self.n_decks = 8
        self.dealer_hits_soft_17 = True
        self.allow_surrender = True
        self.only_late_surrender = True
        self.natural_blackjack_payout = 3 / 2
        self.max_splits_allowed = 1
        self.allow_double_after_split = False
        self.allow_double_on_soft = False
        self.act_on_split_aces = True


class ActionConsumerInterface(ABC):
    @abstractmethod
    def take_action(self, action):
        pass

    @abstractmethod
    def need_action(self):
        pass


class PlayerInterface(ABC):
    def __init__(self):
        self.hands: list[Hand] = None
        self.bets: list[float] = None

    @abstractmethod
    def new_game_started(self):
        pass

    @abstractmethod
    def take_action(
        self, hand_idx: int, dealer_hand: Hand,
        action_consumer: ActionConsumerInterface
    ):
        pass


class HandActionConsumer(ActionConsumerInterface):
    def __init__(self, game, hand_idx):
        super().__init__()
        self.game = game
        self.hand_idx = hand_idx
        self._need_action = True


    def take_action(self, action):
        if not self._need_action:
            e = RuntimeError("Action already applied")
            self.game.logger.error(f"{e.__class__.__name__} {e}")
            raise e

        try:
            self.game.process_player_action_raise_on_error(
                self.hand_idx, action
            )
        except Exception as e:
            self.game.logger.error(f"{e.__class__.__name__} {e}")
            raise
        else:
            self._need_action = False


    def need_action(self):
        return self._need_action


class IllegalActionError(RuntimeError):
    pass


class RoundObjects:
    def __init__(self, rules: Rules, deck: Deck):
        self.playing_deck: Deck = deck
        self.surrendered: bool = False
        self.surrender_blocked: bool = False
        self.can_surrender: bool = rules.allow_surrender
        self.bust_on_all_hands: bool = False
        self.finished_hand_idx: list[int] = []
        self.hit_hands_idxs: list[int] = []
        self.n_splits: int = 0


class DeckDispenser(ABC):
    def __init__(self):
        pass

    def get_deck(self) -> Deck:
        pass


class RandomDeckDispenser(DeckDispenser):
    def __init__(self, n_decks=8):
        super().__init__()
        self.n_decks = n_decks

    def get_deck(self):
        deck = Deck(self.n_decks)
        deck.shuffle()
        return deck


class Game:
    def __init__(
        self,
        rules: Rules, player: PlayerInterface,
        deck_dispenser: DeckDispenser = None,
        logger: logging.Logger = None
    ):
        self.player: PlayerInterface = player
        self.rules: Rules = rules
        self.dealer = Dealer(self.rules.dealer_hits_soft_17)
        self.round_objects: RoundObjects = None
        if logger is None:
            null_logger = logging.getLogger()
            null_logger.addHandler(logging.NullHandler())
            logger = null_logger
        self.logger = logger

        if deck_dispenser is None:
            deck_dispenser = RandomDeckDispenser(self.rules.n_decks)
        self.deck_dispenser: DeckDispenser = deck_dispenser

        self.n_rounds = 0
        self.logger.info(" - Initializing blackjack")
        self.logger.info(f"rules: {self.rules.__dict__}")
        self.logger.info(f" ")


    def play_round(self, player_bet=1):
        self.logger.info(f" - Starting blackjack")
        self.logger.info(f"Round #{self.n_rounds + 1}")
        self.prepare_round_objects()
        self.initial_actions(player_bet)
        self.player_plays_hands()
        self.dealer_plays()
        bet, won = self.account_money()
        self.logger.info(f"Round #{self.n_rounds + 1} finished")
        self.logger.info(f" ")
        self.n_rounds += 1
        return bet, won


    def prepare_round_objects(self):
        self.round_objects = RoundObjects(
            self.rules,
            self.deck_dispenser.get_deck()
        )
        self.player.hands = [Hand()]
        self.dealer.hand = Hand()
        self.player.new_game_started()

    def initial_actions(self, player_bet):
        self.player.bets = [player_bet]

        self.player.hands[0].add_card(self.round_objects.playing_deck.pop())
        self.player.hands[0].add_card(self.round_objects.playing_deck.pop())

        # it is mathematically the same that we give dealer one card
        # and the 2nd card only after player's action
        # because dealer's second card is not seen to player
        self.dealer.hand.add_card(self.round_objects.playing_deck.pop())

        self.logger.info(f" - Initial setup")
        self.logger.info(f"Player bets {self.player.bets[0]}:")
        self.logger.info(f"Player hand: {self.player.hands[0]}")
        self.logger.info(f"Dealer hand: {self.dealer.hand}")


    def player_plays_hands(self):
        hand_idx = 0
        # number of hands changes dynamically after splits
        while hand_idx < len(self.player.hands):
            self.logger.info(f" - Playing hand {hand_idx + 1}")
            self.play_ith_hand(hand_idx)
            self.logger.info(f"Finishing playing hand {hand_idx + 1}")
            # cannot surrender after first hand
            self.round_objects.can_surrender = False
            hand_idx += 1

        self.round_objects.bust_on_all_hands = all(
            [h.get_best_value() is None for h in self.player.hands]
        )

    def play_ith_hand(self, hand_idx):
        while hand_idx not in self.round_objects.finished_hand_idx:
            if self.player.hands[hand_idx].is_natural_blackjack():
                self.logger.info(f"Hand {hand_idx + 1} is a natural blackjack, no more actions")
                self.round_objects.finished_hand_idx.append(hand_idx)
                return

            next_action_consumer = HandActionConsumer(self, hand_idx)
            self.player.take_action(
                hand_idx=hand_idx, dealer_hand=self.dealer.hand.copy(),
                action_consumer=next_action_consumer
            )
            if next_action_consumer.need_action():
                raise RuntimeError("ActionConsumer still needs an action")

            value = self.player.hands[hand_idx].get_best_value()
            if value is None or value == 21:
                # player is bust or has best value
                self.round_objects.finished_hand_idx.append(hand_idx)


    def process_player_action_raise_on_error(self, hand_idx, player_action):
        self.logger.info(f" - Action for hand {hand_idx + 1}: {player_action}")

        if player_action == PlayerAction.SURRENDER:
            self.process_surrender(hand_idx)
        elif player_action == PlayerAction.STAND:
            self.process_stand(hand_idx)
        elif player_action == PlayerAction.DOUBLE:
            self.process_double(hand_idx)
        elif player_action == PlayerAction.HIT:
            self.process_hit(hand_idx)
        elif player_action == PlayerAction.SPLIT:
            self.process_split(hand_idx)
        else:
            raise ValueError(f"Unexpected action {player_action}")


    def process_surrender(self, hand_idx):
        if self.round_objects.can_surrender:
            self.round_objects.surrendered = True
            self.round_objects.finished_hand_idx.append(hand_idx)
        else:
            raise IllegalActionError("Cannot surrender")


    def process_stand(self, hand_idx):
        self.round_objects.finished_hand_idx.append(hand_idx)


    def process_double(self, hand_idx):
        if (
            hand_idx in self.round_objects.hit_hands_idxs or
            (self.player.hands[hand_idx].is_soft() and not self.rules.allow_double_on_soft) or
            (self.round_objects.n_splits > 0 and not self.rules.allow_double_after_split)
        ):
            raise IllegalActionError("Cannot double")

        self.player.bets[hand_idx] = self.player.bets[hand_idx] * 2
        self.player.hands[hand_idx].add_card(self.round_objects.playing_deck.pop())
        self.round_objects.finished_hand_idx.append(hand_idx)

        self.log_current_player_hands()
        self.log_current_player_bets()



    def process_hit(self, hand_idx):
        self.player.hands[hand_idx].add_card(self.round_objects.playing_deck.pop())
        self.round_objects.hit_hands_idxs.append(hand_idx)

        self.log_current_player_hands()
        self.log_current_player_bets()


    def process_split(self, hand_idx):
        if (
            self.rules.max_splits_allowed is not None and
            self.round_objects.n_splits >= self.rules.max_splits_allowed
        ):
            raise IllegalActionError(f"Cannot split more than {self.rules.max_splits_allowed} times")

        # Determine if we're splitting aces to enforce special rule if needed
        original_hand = self.player.hands[hand_idx]
        splitting_aces = (
            original_hand.can_split() and original_hand.cards[0].rank == Rank.ACE
        )

        split_1, split_2 = original_hand.split()
        split_1.add_card(self.round_objects.playing_deck.pop())
        split_2.add_card(self.round_objects.playing_deck.pop())
        self.player.hands.pop(hand_idx)
        self.player.hands.insert(hand_idx, split_1)
        self.player.hands.insert(hand_idx + 1, split_2)
        bet = self.player.bets.pop(hand_idx)
        self.player.bets.insert(hand_idx, bet)
        self.player.bets.insert(hand_idx + 1, bet)
        self.log_current_player_hands()
        self.log_current_player_bets()
        self.round_objects.n_splits += 1

        # If the rule disallows acting on split aces, both hands auto-stand
        if splitting_aces and not self.rules.act_on_split_aces:
            self.logger.info("Split aces rule active: one card only, both hands stand")
            # mark both new hands as finished (no further actions allowed)
            self.round_objects.finished_hand_idx.append(hand_idx)
            self.round_objects.finished_hand_idx.append(hand_idx + 1)


    def log_current_player_hands(self):
        self.logger.info(f"Player: current hands " + " ".join([str(h) for h in self.player.hands]))


    def log_current_player_bets(self):
        self.logger.info(f"Player: current bets {self.player.bets}")


    def dealer_plays(self):
        self.logger.info(f" - Dealer is taking cards")

        if self.round_objects.bust_on_all_hands:
            self.logger.info("Player is bust - no actions taken")
            return

        dealer_hand = self.dealer.hand
        dealer_hand.add_card(self.round_objects.playing_deck.pop())
        self.logger.info(f"Dealer second card {dealer_hand.cards[1]}")
        self.logger.info(f"Dealer hand {dealer_hand}")

        if self.round_objects.surrendered:
            if dealer_hand.is_natural_blackjack():
                if self.rules.only_late_surrender:
                    self.logger.info("Dealer has blackjack, surrender not allowed")
                    self.round_objects.surrender_blocked = True
                else:
                    self.logger.info("Dealer has blackjack, but surrender allowed")
                    return

            else:
                self.logger.info("Player surrendered - no actions taken")
                return


        value = dealer_hand.get_best_value()
        while value is not None:  # None = bust
            # dealer must hit on 16, stand on 17 or higher
            if value <= 16 or (dealer_hand.is_soft_17() and self.dealer.hit_on_soft_17):
                self.dealer_hits()
                value = dealer_hand.get_best_value()
            else:
                self.logger.info("Dealer stands")
                break

        if value is None:
            self.logger.info("Dealer is bust")


    def dealer_hits(self):
        self.dealer.hand.add_card(self.round_objects.playing_deck.pop())
        self.logger.info("Dealer hits")
        self.logger.info(f"Dealer hand {self.dealer.hand}")


    def account_money(self):
        self.logger.info(" - Accounting for bets and wins")
        wins = []

        if self.round_objects.surrendered and not self.round_objects.surrender_blocked:
            total_win = self.player.bets[0] / 2
        else:
            for hand_idx in range(len(self.player.hands)):
                wins.append(self.get_money_won_for_hand(hand_idx))
            total_win = sum(wins)

        total_bet = sum(self.player.bets)


        self.logger.info(f"Total value of bets {total_bet}")
        self.logger.info(f"Total player gets {total_win}")
        return total_bet, total_win


    def get_money_won_for_hand(self, hand_idx):
        self.logger.info(f"Accounting for bets and wins for hand {hand_idx + 1}")

        player_bet = self.player.bets[hand_idx]
        player_hand = self.player.hands[hand_idx]
        dealer_hand = self.dealer.hand

        self.logger.info(f"Player hand {player_hand}")
        self.logger.info(f"Dealer hand {dealer_hand}")

        player_win = self.account_for_blackjacks(player_bet, player_hand, dealer_hand)
        if player_win is not None:
            self.logger.info(f"Player gets {player_win}")
            return player_win

        player_win = self.account_for_bust(player_bet, player_hand, dealer_hand)
        if player_win is not None:
            self.logger.info(f"Player gets {player_win}")
            return player_win

        player_win = self.account_for_value(player_bet, player_hand, dealer_hand)
        self.logger.info(f"Player gets {player_win}")
        return player_win

    def account_for_blackjacks(self, player_bet, player_hand, dealer_hand):
        player_bj = player_hand.is_natural_blackjack()
        dealer_bj = dealer_hand.is_natural_blackjack()

        if player_bj:
            self.logger.info("Player has natural blackjack")

        if dealer_bj:
            self.logger.info("Dealer has natural blackjack")

        player_win = None

        if player_bj and dealer_bj:
            player_win = player_bet * 2
        elif player_bj and not dealer_bj:
            player_win = player_bet * (1 + self.rules.natural_blackjack_payout)
        elif not player_bj and dealer_bj:
            player_win = 0

        return player_win

    def account_for_bust(self, player_bet, player_hand, dealer_hand):
        player_value = player_hand.get_best_value()
        dealer_value = dealer_hand.get_best_value()
        player_win = None

        if player_value is None:
            self.logger.info("Player is bust")
            player_win = 0
        elif dealer_value is None:
            self.logger.info("Dealer is bust")
            player_win = player_bet * 2

        return player_win

    def account_for_value(self, player_bet, player_hand, dealer_hand):
        player_value = player_hand.get_best_value()
        dealer_value = dealer_hand.get_best_value()

        self.logger.info("Winner is decided by hand values")

        if player_value == dealer_value:
            player_win = player_bet
        elif player_value > dealer_value:
            player_win = 2 * player_bet
        else:
            player_win = 0

        return player_win
