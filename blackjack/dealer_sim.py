# from blackjack.shoe import ProbabilisticRankShoe
import numpy as np
from blackjack.hand import ValueOnlyHand
from blackjack_py import ProbabilisticRankShoe
import time
from blackjack.blackjack_round import BJRound, BJStage
from itertools import combinations_with_replacement, product
from collections import Counter, defaultdict
from math import factorial
from collections.abc import Iterable



def multiset_with_perm_counts(n: int, k: int):
    """
    Generate all size-k selections from n elements (with replacement),
    unique up to ordering, and for each yield:
        (values_tuple, number_of_distinct_permutations)

    Elements are:
      - 0..n-1  if one_based=False
      - 1..n    if one_based=True
    """
    if isinstance(n, Iterable):
        elems = n
    else:
        elems = list(range(n))

    for combo in combinations_with_replacement(elems, k):
        counts = Counter(combo).values()
        denom = 1
        for c in counts:
            denom *= factorial(c)
        num_perms = factorial(k) // denom
        yield combo, num_perms



def generate_and_save_combs_with_counts(i: int):
    combinations_with_counts = multiset_with_perm_counts(range(2, 12), i)
    np_data = []
    for comb, count in combinations_with_counts:
        np_data.append(list(comb) + [count])
    np.savetxt(f"combinations/combinations_with_counts_{i}.csv", np.array(np_data), fmt="%.0f")
         
    for dealer_hit_soft_17 in [False, True]:
        realistic_combinations = defaultdict(int)
        for comb, count in combinations_with_counts:
            hand = ValueOnlyHand()
            for v in comb:
                if _dealer_stand_or_bust(hand, dealer_hit_soft_17=dealer_hit_soft_17):
                    break
                hand.add_card(v)
            realistic_combinations[tuple(hand.cards)] += count

        for comb, count in realistic_combinations:
            np_data.append(list(comb) + [count])

        decision_17_str = "h17" if dealer_hit_soft_17 else "s17"
        np.savetxt(
            f"combinations/realistic_comb_with_counts_{decision_17_str}_{i}.csv",
            np.array(np_data), 
            fmt="%.0f"
        )


def load_realistic_combs_with_counts(i: int, dealer_hits_soft_17: bool = False):
    decision_17_str = "h17" if dealer_hits_soft_17 else "s17"
    fname = f"combinations/realistic_comb_with_counts_{decision_17_str}_{i}.csv"
    combinations = []
    counts = []

    with open(fname, "r") as f:
        for line in f.readlines():
            row = [int(x) for x in line.strip().split()]        
            combinations.append(tuple(row[:-1]))
            counts.append(row[-1])
    print(len(combinations))
    return list(zip(combinations, counts))



def run_dealer_cards_simulation(
    bj_round: BJRound,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int,
    reset_shoe_sampler: bool = True
):
    """Run Monte Carlo simulations for dealer play."""
    values = []

    if shoe.is_dealer_card_locked():
        shoe.unlock_dealer_card()
        shoe = shoe.copy()

    for i in range(n_dealer_sim_runs):
        bj_round_copy = bj_round.copy()
        shoe_copy = shoe.copy()
        if reset_shoe_sampler:
            shoe_copy.change_random_sampler()

        # Simulate dealer cards until round over
        while not bj_round_copy.get_stage() == BJStage.ROUND_OVER:
            possible_values = bj_round_copy.get_possible_next_card_ranks()
            card = shoe_copy.sample_and_burn_rank(possible_values)
            bj_round_copy.take_card(card)
        
        # Collect results
        values.append(bj_round_copy.get_player_value())
    return np.mean(values)
          

def run_dealer_cards_simulation_recursive_old(
    bj_round: BJRound,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int,
    n_full_sample: int = 4
):
    """Run Monte Carlo simulations for dealer play."""

    possible_ranks = bj_round.get_possible_next_card_ranks()
    value_by_first_card = {}

    if shoe.is_dealer_card_locked():
        shoe = shoe.copy()
        shoe.unlock_dealer_card()

    probabilities = shoe.get_rank_value_probabilities(possible_ranks)
    if possible_ranks is None:
        possible_ranks = list(range(2, 12))

    for first_card in possible_ranks:
        if first_card not in probabilities:
            continue
        values = []
        shoe_first_card = shoe.copy()
        bj_round_first_card = bj_round.copy()
        shoe_first_card.burn_rank_value(first_card)
        bj_round_first_card.take_card(first_card)

        if bj_round_first_card.get_stage() == BJStage.ROUND_OVER:
            value_by_first_card[first_card] = bj_round_first_card.get_player_value()
        elif n_full_sample > 1:
            value_by_first_card[first_card] = run_dealer_cards_simulation_recursive_old(
                bj_round_first_card,
                shoe_first_card,
                n_dealer_sim_runs,
                n_full_sample - 1
            )
        else:
            for i in range(n_dealer_sim_runs):
                shoe_sim = shoe_first_card.copy()
                bj_round_sim = bj_round_first_card.copy()

                # Simulate dealer cards until round over
                while not bj_round_sim.get_stage() == BJStage.ROUND_OVER:
                    card = shoe_sim.sample_and_burn_rank()
                    bj_round_sim.take_card(card)
                
                # Collect results
                values.append(bj_round_sim.get_player_value())
            value_by_first_card[first_card] = np.mean(values)

    mean_value = 0.0
    for first_card in possible_ranks:
        if first_card not in probabilities:
            continue
        mean_value += probabilities[first_card] * value_by_first_card[first_card]
    return mean_value


total_sim_time = 0

def run_dealer_cards_simulation_recursive(
    bj_round: BJRound,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int,
    n_full_sample: int = 5
):
    """Run Monte Carlo simulations for dealer play."""
    start_time = time.time()
    stage = bj_round.get_stage()

    if stage == BJStage.ROUND_OVER:
        return bj_round.get_player_value()

    assert len(bj_round.player_hands) == 1
    assert stage == BJStage.DEALER_CARD
    assert not bj_round.player_hands[0].is_natural_blackjack()

    if shoe.is_dealer_card_locked():
        shoe = shoe.copy()
        shoe.unlock_dealer_card()

    possible_ranks = bj_round.get_possible_next_card_ranks()

    value = bj_round.bet_unit * _run_dealer_cards_simulation_recursive_2(
        bj_round.player_hands[0].get_best_value(),
        bj_round.dealer_hand,
        shoe,
        n_dealer_sim_runs,
        n_full_sample,
        possible_ranks,
        bj_round.rules.dealer_hits_soft_17 
    )

    global total_sim_time
    end_time = time.time()
    total_sim_time += end_time - start_time
    return value


def _dealer_stand_or_bust(
    dealer_hand: ValueOnlyHand, dealer_hit_soft_17: bool
):
    dealer_stand = False
    best_value = dealer_hand.get_best_value()
    if best_value is None:
        return True # bust
    
    if dealer_hit_soft_17:
        if best_value > 17:
            dealer_stand = True
        elif best_value == 17 and not dealer_hand.is_soft_17():
            dealer_stand = True
    else:
        if best_value >= 17:
            dealer_stand = True
    return dealer_stand


def _run_dealer_cards_simulation_recursive_2(
    player_value: int,
    dealer_hand,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int,
    n_full_sample: int = 3,
    possible_ranks = None,
    dealer_hit_soft_17 = False
):
    probabilities = shoe.get_rank_value_probabilities(possible_ranks)
    if possible_ranks is None:
        possible_ranks = list(range(2, 12))

    value_by_first_card = {}

    for first_card in possible_ranks:
        if first_card not in probabilities:
            continue
        values = []
        shoe.burn_rank_value(first_card)
        dealer_hand.add_card(first_card)

        if dealer_hand.is_bust():
            value_by_first_card[first_card] = 1
            
        elif _dealer_stand_or_bust(dealer_hand, dealer_hit_soft_17):
            if dealer_hand.get_best_value() < player_value:
                value_by_first_card[first_card] = 1
            elif dealer_hand.get_best_value() > player_value:
                value_by_first_card[first_card] = -1
            else:
                value_by_first_card[first_card] = 0

        elif n_full_sample > 1:
            value_by_first_card[first_card] = _run_dealer_cards_simulation_recursive_2(
                player_value,
                dealer_hand,
                shoe,
                n_dealer_sim_runs,
                n_full_sample - 1,
                possible_ranks=None,  # After first card, any rank is possible
                dealer_hit_soft_17=dealer_hit_soft_17
            )

        else:
            values = _run_dealer_cards_simulation(
                player_value,
                dealer_hand,
                shoe,
                n_dealer_sim_runs,
                dealer_hit_soft_17
            )
            value_by_first_card[first_card] = np.mean(values)

        dealer_hand.pop_card()
        shoe.add_rank_value(first_card)

    mean_value = 0.0
    for first_card in possible_ranks:
        if first_card not in probabilities:
            continue
        mean_value += probabilities[first_card] * value_by_first_card[first_card]
    return mean_value
    


def _run_dealer_cards_simulation(
    player_value: int,
    dealer_hand: ValueOnlyHand,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int,
    dealer_hit_soft_17 = False,
    reset_shoe_sampler: bool = True
):
    values = []

    for i in range(n_dealer_sim_runs):
        # Simulate dealer cards until round over
        
        dealer_hand_sim = dealer_hand.copy()
        shoe_sim = shoe.copy()
        if reset_shoe_sampler:
            shoe_sim.change_random_sampler()

        while (
            not dealer_hand_sim.is_bust() 
            and not _dealer_stand_or_bust(dealer_hand_sim, dealer_hit_soft_17)
        ):
            rank = shoe_sim.sample_and_burn_rank()
            dealer_hand_sim.add_card(rank)

        # Collect results
        if dealer_hand_sim.is_bust():
            values.append(1)
        else:
            if dealer_hand_sim.get_best_value() < player_value:
                values.append(1)
            elif dealer_hand_sim.get_best_value() > player_value:
                values.append(-1)
            else:
                values.append(0)
    return values




combinations_with_counts_precomputed = dict()

for i in range(1, 8):
    comb_count = load_realistic_combs_with_counts(i)
    comb_count_by_first_card = defaultdict(list)
    for comb, count in comb_count:
        first_card = comb[0]
        rest_cards = comb[1:]
        comb_count_by_first_card[first_card].append( (rest_cards, count) )
    combinations_with_counts_precomputed[i] = comb_count_by_first_card



def get_cards_probability(cards, shoe, possible_first_card=None):
    prob = 1
    for i, card in enumerate(cards):
        if i == 0:
            possible_cards = possible_first_card
        else:
            possible_cards = None
        probs = shoe.get_rank_value_probabilities(possible_cards)
        if card not in probs or probs[card] == 0:
            return 0
        prob = prob * probs[card]
        shoe.burn_rank_value(card)
    for card in cards:
        shoe.add_rank_value(card)
    return prob


import pickle

with open("combinations/combinations_with_counts_no_bj.pkl", "rb") as f:
    precomputed_combinations_with_counts_no_bj = pickle.load(f)

with open("combinations/combinations_with_counts.pkl", "rb") as f:
    precomputed_combinations_with_counts = pickle.load(f)


def run_dealer_cards_simulation_comb(
    bj_round: BJRound,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int = 1,
    n_full_sample: int = 5,
    verbose = False
):
    start_time = time.time()

    dealer_hand: ValueOnlyHand = bj_round.dealer_hand.copy()
    dealer_hit_soft_17 = bj_round.rules.dealer_hits_soft_17
    assert dealer_hand.size() == 1
    assert len(bj_round.player_hands) == 1
    assert bj_round.get_stage() == BJStage.DEALER_CARD
    assert not bj_round.player_hands[0].is_natural_blackjack()


    if shoe.is_dealer_card_locked():
        shoe = shoe.copy()
        shoe.unlock_dealer_card()
        
    player_hand_value = bj_round.player_hands[0].get_best_value()

    assert not bj_round.rules.dealer_hits_soft_17
    if bj_round.rules.dealer_checks_blackjack:
        assert not bj_round.dealer_has_bj_after_check
        data = precomputed_combinations_with_counts_no_bj[n_full_sample][dealer_hand.cards[0]]
    else:
        data = precomputed_combinations_with_counts[n_full_sample][dealer_hand.cards[0]]
        
    possible_second_card = bj_round.get_possible_next_card_ranks()

    bust_combos_data = data['bust_combos_data']
    stand_combos_data = data['stand_combos_data']
    other_combos_data = data['other_combos_data']
    stand_values = data['stand_values']

    total_p = 0 

    dealer_bust_p = 0
    for i, (combo, num_perms) in enumerate(bust_combos_data):
        prob = get_cards_probability(combo, shoe, possible_second_card)
        if prob != 0:
            prob = prob * num_perms
            total_p += prob
            dealer_bust_p += prob
        
    ev_stand = np.zeros(len(stand_combos_data))
    prob_stand = np.zeros(len(stand_combos_data))
    ev_stand[player_hand_value > stand_values] = 1
    ev_stand[player_hand_value == stand_values] = 0
    ev_stand[player_hand_value < stand_values] = -1
    for i, (combo, num_perms) in enumerate(stand_combos_data):
        prob = get_cards_probability(combo, shoe, possible_second_card)
        if prob != 0:
            prob = prob * num_perms
            total_p += prob
            prob_stand[i] = prob

    ev_other = np.zeros(len(stand_combos_data))
    prob_other= np.zeros(len(stand_combos_data))
    
    for i, (combo, num_perms) in enumerate(other_combos_data):
        prob = 1
        impossible = False
        burned_cards = []
        for i, card in enumerate(combo):
            if i == 0:
                probs = shoe.get_rank_value_probabilities(possible_second_card)
            else:
                probs = shoe.get_rank_value_probabilities()
            if card not in probs or probs[card] == 0:
                impossible = True
                break
            prob = prob * probs[card]
            shoe.burn_rank_value(card)
            burned_cards.append(card)
            dealer_hand.add_card(card)
  
        if impossible:
            for card in burned_cards:
                shoe.add_rank_value(card) 
                dealer_hand.pop_card()
            continue
        
        prob = prob * num_perms
        total_p += prob
        prob_other[i] = prob

        assert not _dealer_stand_or_bust(dealer_hand, dealer_hit_soft_17)
        sim_values = _run_dealer_cards_simulation(
            player_hand_value,
            dealer_hand,
            shoe,
            n_dealer_sim_runs,
            dealer_hit_soft_17
        )
        ev_other[i] = np.mean(sim_values)
        for card in burned_cards:
            shoe.add_rank_value(card)
            dealer_hand.pop_card()
    

    p_bust = dealer_bust_p
    ev_bust_value = 1 * dealer_bust_p
    p_stand = prob_stand.sum()
    p_other = total_p - p_bust - p_stand
    ev_stand_value = ev_stand.dot(prob_stand).item()
    ev_other_value = ev_other.dot(prob_other).item()
    if verbose:
        print("p_bust =", p_bust)
        print("p_stand =", p_stand, "ev_stand_value =", ev_stand_value)
        print("p_other =", p_other, "ev_other_value =", ev_other_value)
        print("p_total =", total_p)
    final_value = bj_round.bet_unit * (
        ev_other_value
        + ev_stand_value
        + ev_bust_value
    )

    global total_sim_time
    end_time = time.time()
    total_sim_time += end_time - start_time
    return final_value