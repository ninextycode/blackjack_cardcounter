from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import asdict
import multiprocessing
import numpy as np
import tqdm
from blackjack.abstract_node import ValueNode
from blackjack.actions import DealerAction
from blackjack.blackjack_round import BJRound, BJStage
from blackjack.floor_ceil_node import DealerCheckBJNode, DecisionNode
from blackjack.rules import BJRules
from blackjack_py import ProbabilisticRankShoe


def build_root_node(bj_round, shoe, sim_depth, sim_algo):
    stage = bj_round.get_stage()
    if stage == BJStage.DEALER_CHECK_BJ:
        # dealer checks blackjack with ten
        # insurance not offered
        root_node = DealerCheckBJNode(
            bj_round,
            shoe,
            max_hand_size_full_enum=1,
            took_insurance=False,
            insurance_offered=False,
            dealer_sim_depth=sim_depth,
            sim_algo=sim_algo
        )
    elif (
        stage == BJStage.DEALER_CARD \
        and len(bj_round.player_hands) == 1 \
        and bj_round.player_hands[0].is_natural_blackjack()
    ):
        # player has blackjack, insurance not offered - go to dealer card immediately
        root_node = ValueNode(
            bj_round.bet_unit * bj_round.rules.natural_blackjack_payout,
            bj_round=bj_round,
            shoe=shoe
        )
    elif stage in (
        BJStage.PLAYER_ACTION,
        BJStage.PLAYER_OFFERED_INSURANCE,
        BJStage.PLAYER_OFFERED_EARLY_SURRENDER
    ):
        root_node = DecisionNode(
            bj_round,
            shoe,
            max_hand_size_full_enum=1,
            dealer_sim_depth=sim_depth,
            sim_algo=sim_algo
        )
    else:
        raise ValueError(f"Cannot build root node for stage {stage}")
    
    return root_node


def generate_initial_rounds():
    for player_card_0 in range(2, 12):
        for player_card_1 in range(player_card_0, 12):
            for dealer_upcard in range(2, 12):
                n_count = 1 if player_card_0 == player_card_1 else 2
                yield player_card_0, player_card_1, dealer_upcard, n_count


def build_complete_round_tree_args(args):
    """Worker function that reconstructs node and computes value."""
    shoe, p0, p1, d, gap_target, rules_dict, sim_depth, algo = args
    
    # Reconstruct rules and objects
    rules = BJRules(**rules_dict)
    bj_round = BJRound(rules)
    bj_round.start_round(100)
    
    for c in [p0, p1, d]:
        bj_round.take_card(c)
        shoe.burn_rank_value(d)
    
    root_node = build_root_node(bj_round, shoe, sim_depth, algo)
    build_complete_tree(root_node, gap_target, first_decision_required=True)
    return root_node


def get_ev_first_actions_args(args_dict):
    """Worker function that reconstructs node and computes value."""
    args_dict
    shoe = args_dict["initial_shoe"]
    p0 = args_dict["player_card_0"]
    p1 = args_dict["player_card_1"]
    d = args_dict["dealer_card"]
    gap_target = args_dict["gap_target"]
    rules_dict = args_dict["rules_dict"]
    sim_depth = args_dict["sim_depth"]
    algo = args_dict["algorithm"]
    
    # Reconstruct rules and objects
    rules = BJRules(**rules_dict)
    bj_round = BJRound(rules)
    bj_round.start_round(100)
    
    for c in [p0, p1, d]:
        bj_round.take_card(c)
        shoe.burn_rank_value(d)
    
    root_node = build_root_node(bj_round, shoe, sim_depth, algo)
    build_complete_tree(root_node, gap_target, first_decision_required=True)
    return (
        (
            root_node.get_value(),
            root_node.get_floor_value(),
            root_node.get_ceil_value(),
        ),
        get_first_actions(root_node)
    )
    

def build_complete_tree(root_node, gap_target, first_decision_required):
    if isinstance(root_node, ValueNode):
        return
    
    gap_target_unit = gap_target * root_node.bj_round.bet_unit
    root_node.build_tree()
    
    for i in range(100):
        value_changed, is_final = root_node.convert_to_full_up_to_depth(depth=i)
        
        if is_final:
            break

        value_gap_ok = \
            (root_node.get_ceil_value() - root_node.get_floor_value()) < gap_target_unit
        if (
            value_gap_ok and \
            not (first_decision_required and is_first_action_undecided(root_node))
        ):
            break


def is_first_action_undecided(root_node):
    if isinstance(root_node, DecisionNode):
        if root_node.bj_round.get_stage() == BJStage.PLAYER_ACTION:
            return root_node.decision_choice is None
        else:  # insurance decision
            if root_node.decision_choice is not None:
                insurance_decision_child = root_node.get_decision_choice_child()
                return is_first_action_undecided(insurance_decision_child)
            else:
                return True
            
    elif isinstance(root_node, DealerCheckBJNode):
        return is_first_action_undecided(
            root_node.children[root_node.dealer_no_bj_child_idx]
        )
    else:
        return False
    

def get_first_actions(root_node):
    if isinstance(root_node, DecisionNode):
        if root_node.bj_round.get_stage() == BJStage.PLAYER_ACTION:
            return [list(root_node.possible_actions)]
        else:  # insurance decision
            insurance_decisions = list(root_node.possible_actions)
            # both TAKE_INSURANCE and REFUSE_INSURANCE 
            # link the same decision node after them
            decision_after_insurance = get_first_actions(root_node.children[0])
            return insurance_decisions, decision_after_insurance
        
    elif isinstance(root_node, DealerCheckBJNode):
        return get_first_actions(
            root_node.children[root_node.dealer_no_bj_child_idx]
        )
    
    else:
        return []
    


class BeforeCardsSolutionExplorer:
    def __init__(self, shoe: ProbabilisticRankShoe, rules: BJRules):
        self.shoe = shoe
        self.rules = rules
        self.algorithm = "recursive"
        self.ev = np.nan
        self.ev_min = np.nan
        self.ev_max = np.nan
        self.ev_sim_depth = 3
        self.ev_gap_target = 0.01
        self.initial_decisions = {}

    def compute_edge(self):
        task_args = []
        probs = []
        for p0, p1, d, n in generate_initial_rounds():
            shoe = self.shoe
            burned_cards = []
            prob = 1
            for c in [p0, p1, d]:
                prob_c_dict = shoe.get_rank_value_probabilities()
                prob *= prob_c_dict.get(c, 0)
                if prob == 0:
                    break
                shoe.burn_rank_value(c)
                burned_cards.append(c)
            for c in burned_cards:
                shoe.return_rank_value(c)
            if prob == 0:
                continue 
            
            probs.append(prob)

            task_args.append(
                dict(
                    initial_shoe=shoe,
                    player_card_0=p0,
                    player_card_1=p1,
                    dealer_card=d,
                    gap_target=self.ev_gap_target,
                    rules_dict=asdict(self.rules),
                    sim_depth=self.ev_sim_depth,
                    algo=self.algorithm
                )
            )

        max_workers = multiprocessing.cpu_count()
        results = [None] * len(task_args)

        with ProcessPoolExecutor(max_workers=max_workers) as executor:
            future_to_idx = {
                executor.submit(get_ev_first_actions_args, task): idx 
                for idx, task in enumerate(task_args)
            }
            
            for future in tqdm.tqdm(as_completed(future_to_idx), total=len(task_args)):
                idx = future_to_idx[future]
                results[idx] = future.result()

        values = []
        for args, result in zip(task_args, results):
            (v, v_min, v_max), actions = result
            values.append((v, v_min, v_max))
            p0 = args["player_card_0"] 
            p1 = args["player_card_1"]
            d = args["dealer_card"]
            self.initial_decisions[(p0, p1, d)] = actions

        values = np.array(values)
        probs = np.array(probs, shape=(-1, 1))
        ev_results = np.sum(values * probs, axis=0)

        self.ev = ev_results[0] / 100
        self.ev_min = ev_results[1] / 100
        self.ev_max = ev_results[2] / 100


    def get_first_actions(self, p0, p1, d):
        return self.initial_decisions[(p0, p1, d)]


    def process_initial_cards(self, p0, p1, d):
        pass


class SolutionExplorer:
    def __init__(
        self,
        shoe,
        rules,
        player_card_0,
        player_card_1,
        dealer_card,
        initial_actions
    ):
        self.ev_sim_depth = 5
        self.ev_gap_target = 0.01
        self.ev = np.nan
        self.ev_min = np.nan
        self.ev_max = np.nan
        self.shoe = shoe
        self.algorithm = "recursive"
        bj_round = BJRound(rules)

        bj_round.start_round(100)
        for c in [player_card_0, player_card_1, dealer_card]:
            bj_round.take_card(c)
            # shoe has to be in a correct state already
            # initial cards already must be removed

        # TODO special case should be applied to split action
        for a in initial_actions:
            # if decisions after the initial insurance are possible - dealer does not have blackjack
            if bj_round.get_stage() == BJStage.DEALER_CHECK_BJ:
                bj_round.take_action(DealerAction.CONFIRM_NO_BLACKJACK)
            bj_round.take_action(a)

        self.root_node = build_root_node(
            bj_round,
            shoe,
            sim_depth=self.ev_sim_depth,
            sim_algo=self.algorithm
        )

        build_complete_tree(
            self.root_node,
            gap_target=self.ev_gap_target,
            first_decision_required=True
        )


    def card_given(self, card):
        child_idx = self.root_node.children_events.index(card)
        self.root_node = self.root_node.children[child_idx]
        build_complete_tree(self.root_node, self.ev_gap_target, first_decision_required=True)


    def action_taken(self, action):
        child_idx = self.root_node.possible_actions.index(action)
        self.root_node = self.root_node.children[child_idx]
        build_complete_tree(self.root_node, self.ev_gap_target, first_decision_required=True)


    def get_actions(self):
        return list(self.root_node.possible_actions)


    def get_ev(self, with_min_max=False):
        ev_data = (
            self.root_node.get_value() / 100,
            self.root_node.get_floor_value() / 100,
            self.root_node.get_ceil_value() / 100
        )

        if with_min_max:
            return ev_data
        else:
            return ev_data[0]