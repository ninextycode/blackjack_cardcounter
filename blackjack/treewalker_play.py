from blackjack.blackjack_round import BJRound, BJStage
from blackjack.actions import DealerAction, PlayerAction
from blackjack.rules import BJRules
from blackjack_cpp import TreeWalker
import logging


def play_round(rules, shoe, bet_unit, logger=None):
    if logger is None:
        logger = logging.getLogger(__name__)
        logger.addHandler(logging.NullHandler())

    shoe.unlock_dealer_card()
    bj_round = BJRound(rules)
    bj_round.start_round(bet_unit=bet_unit)
    dealer_second_card = None

    stage = bj_round.get_stage()
    
    logger.info("=" * 32)
    logger.info(str(bj_round))

    tree_walker = None

    while stage != BJStage.ROUND_OVER:
        need_action = bj_round.need_player_action() or bj_round.need_dealer_action()
        if tree_walker is None and need_action:
            # initialize the tree walker after the first cards are dealt
            tree_walker = TreeWalker.build_initial(
                bj_round.player_hands[0].cards,
                bj_round.dealer_hand[0],
                bj_round.rules.to_cpp(),
                shoe.get_rank_count(),
                bet_unit=bet_unit,
                initial_cards_burned=True
            )
        
        possible_cards = bj_round.get_possible_next_card_ranks()
        possible_actions = bj_round.get_available_actions()

        if len(possible_actions) > 0:
            dealer_hand = bj_round.get_dealer_hand()
            dealer_upcard = dealer_hand[0]

            if bj_round.need_player_action():
                best_actions = tree_walker.get_best_actions()
                best_action = PlayerAction[best_actions[0][0]]

                if best_action not in possible_actions:
                    raise RuntimeError(f"Best action {best_action} not in possible actions {possible_actions}")
                bj_round.take_action(best_action)
                tree_walker.take_player_action(best_action.value)

            elif stage == BJStage.DEALER_CHECK_BJ:            
                if dealer_upcard == 11:  # dealer has Ace
                    second_card_for_bj = 10
                else:  # dealer has 10
                    second_card_for_bj = 11

                # all cards are possible for the second dealer's card
                # no conditioning on possible_cards - conditioning is for the player's cards
                dealer_second_card = shoe.sample_and_burn_rank()

                if dealer_second_card == second_card_for_bj:
                    action = DealerAction.CONFIRM_BLACKJACK
                else:
                    action = DealerAction.CONFIRM_NO_BLACKJACK
                    shoe.lock_dealer_card_not(second_card_for_bj)

                bj_round.take_action(action)
                tree_walker.take_dealer_action(action.value)
    
            else:
                raise RuntimeError
        else:  # no possible actions, so we need to take a card
            if stage == BJStage.DEALER_CARD and dealer_second_card is not None:
                next_card = dealer_second_card
                dealer_second_card = None
            else:
                next_card = shoe.sample_and_burn_rank(possible_cards)
            
            if stage == BJStage.PLAYER_CARD and tree_walker is not None:
                # skip initial cards, in this case treewalker is None
                logger.info(f"Taking card {next_card} with TreeWalker")
                tree_walker.take_card(next_card)
            
            bj_round.take_card(next_card)
        
        stage = bj_round.get_stage()

        logger.info("-" * 32)
        logger.info(bj_round.__str__(with_last_action=True))
        logger.info(f"TreeWalker: {str(tree_walker)}")
        # if tree_walker is not None and bj_round.get_stage() != BJStage.DEALER_CHECK_BJ:
        #     tree_walker.tighten_value_estimate_gap(0.1)

    logger.info("=" * 32)
    return bj_round