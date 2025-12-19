from blackjack.blackjack_round import BJRound, BJStage
from blackjack.actions import DealerAction
import logging


def play_round(rules, shoe, bet_unit, counter, strategy, logger=None):
    if logger is None:
        logger = logging.getLogger(__name__)
        logger.addHandler(logging.NullHandler())

    shoe.unlock_dealer_card()
    bj_round = BJRound(rules)
    bj_round.start_round(bet_unit=bet_unit)
    dealer_second_card = None

    stage = bj_round.get_stage()

    logger.info(str(bj_round))

    while stage != BJStage.ROUND_OVER:
        possible_cards = bj_round.get_possible_next_card_ranks()
        possible_actions = bj_round.get_available_actions()

        if len(possible_actions) > 0:
            dealer_hand = bj_round.get_dealer_hand()
            dealer_upcard = dealer_hand[0]

            if bj_round.need_player_action():
                true_count = counter.get_true_count()
                player_hand = bj_round.get_active_player_hand()
                
                is_soft = player_hand.is_soft()
                is_pair = player_hand.is_same_value_pair()
                player_value = player_hand.get_best_value()

                actions = strategy.get_actions(
                    player_value,
                    is_soft,
                    is_pair,
                    dealer_upcard,
                    true_count
                )

                chosen_action = None
                for a in actions:
                    if a in possible_actions:
                        chosen_action = a
                        break
                assert chosen_action is not None
                bj_round.take_action(chosen_action)

            elif stage == BJStage.DEALER_CHECK_BJ:            
                if dealer_upcard == 11:  # Ace
                    second_card_needed = 10
                else:  # dealer has 10
                    second_card_needed = 11

                # all cards are possible for the second dealer's card
                # no conditioning on possible_cards
                dealer_second_card = shoe.sample_and_burn_rank()

                if dealer_upcard + dealer_second_card == 21:
                    bj_round.take_action(DealerAction.CONFIRM_BLACKJACK)
                else:
                    bj_round.take_action(DealerAction.CONFIRM_NO_BLACKJACK)
                    shoe.lock_dealer_card_not(second_card_needed)
            else:
                raise RuntimeError
        else:
            if stage == BJStage.DEALER_CARD and dealer_second_card is not None:
                next_card = dealer_second_card
                dealer_second_card = None
            else:
                next_card = shoe.sample_and_burn_rank(possible_cards)
            bj_round.take_card(next_card)
            counter.update_count(next_card)
        
        stage = bj_round.get_stage()

        logger.info("-" * 32)
        logger.info(str(bj_round))
    return bj_round