# from blackjack.shoe import ProbabilisticRankShoe
import numpy as np
from blackjack.hand import ValueOnlyHand
from blackjack_py import ProbabilisticRankShoe, RandomSampler
import time
from blackjack.blackjack_round import BJRound, BJStage
from itertools import combinations_with_replacement, product
from collections import Counter, defaultdict
from math import factorial
from collections.abc import Iterable
from typing import Optional, Dict, Tuple, List


# =============================================================================
# C++ Style Optimizations: Helper functions for inline hand value calculations
# =============================================================================

def _add_card_to_hand_value(current_value: int, is_soft: bool, card: int) -> Tuple[int, bool]:
    """
    Update hand value when adding a card.
    Returns the new (value, is_soft) pair.
    This is an inline optimization to avoid creating ValueOnlyHand objects.
    """
    if card == 11:  # Ace
        if current_value + 11 <= 21:
            current_value += 11
            is_soft = True
        else:
            current_value += 1
    else:
        current_value += card
    
    # Convert soft to hard if bust
    if current_value > 21 and is_soft:
        current_value -= 10
        is_soft = False
    
    return current_value, is_soft


def _get_dealer_cards_id(cards: List[int]) -> int:
    """
    Encode a sequence of cards as a unique 64-bit integer ID.
    Each card encoded as 4 bits (0-15), allowing up to 16 cards.
    Format: 0000000<c0><c1><c2><c3>...
    """
    id_val = cards[0]
    for i in range(1, len(cards)):
        id_val = (id_val << 4) | cards[i]
    return id_val


# Type alias for shoe state cache
ShoeState = Tuple[float, Dict[int, int]]  # (probability, rank_counts)


def _get_dealer_cards_probability(
    comb_id: int,
    shoe_cache: Dict[int, ShoeState],
    base_shoe: ProbabilisticRankShoe,
    possible_first_card: Optional[List[int]]
) -> ShoeState:
    """
    Get probability and remaining shoe state for a dealer card combination.
    Uses dynamic programming with memoization.
    
    Args:
        comb_id: Encoded combination ID
        shoe_cache: Memoization cache
        base_shoe: The original shoe state
        possible_first_card: Restriction on first card (for BJ check scenarios)
    
    Returns:
        Tuple of (probability, remaining_rank_counts)
    """
    # Check cache first
    if comb_id in shoe_cache:
        return shoe_cache[comb_id]
    
    # Base case: single card (rank values 2-11)
    if 2 <= comb_id <= 11:
        probs = base_shoe.get_rank_value_probabilities(possible_first_card)
        prob = probs.get(comb_id, 0.0)
        
        rank_count = base_shoe.get_rank_value_counts()
        rank_count[comb_id] = max(0, rank_count[comb_id] - 1)
        
        shoe_cache[comb_id] = (prob, rank_count)
        return shoe_cache[comb_id]
    
    # Recursive case: multiple cards
    elif comb_id > 11:
        last_card = comb_id & 0xF
        id_wo_last_card = comb_id >> 4
        
        # Get state without last card (recursive)
        comb_wo_last_prob, rank_count_wo_last = _get_dealer_cards_probability(
            id_wo_last_card, shoe_cache, base_shoe, possible_first_card
        )
        
        if comb_wo_last_prob == 0.0:
            shoe_cache[comb_id] = (0.0, {})
            return shoe_cache[comb_id]
        
        # Calculate probability of last card given previous cards burned
        total_count = sum(rank_count_wo_last.values())
        last_card_prob = rank_count_wo_last.get(last_card, 0) / max(1, total_count)
        comb_prob = comb_wo_last_prob * last_card_prob
        
        # Update rank counts
        rank_count = rank_count_wo_last.copy()
        rank_count[last_card] = max(0, rank_count.get(last_card, 0) - 1)
        
        shoe_cache[comb_id] = (comb_prob, rank_count)
        return shoe_cache[comb_id]
    
    else:
        raise RuntimeError(f"Invalid combination id {comb_id}")


def _run_dealer_cards_simulation_simple(
    player_value: int,
    dealer_value: int,
    is_soft: bool,
    shoe: ProbabilisticRankShoe,
    n_sim_runs: int,
    dealer_hit_soft_17: bool = False
) -> float:
    """
    Run dealer simulation using inline value updates (no hand objects).
    This is significantly faster than using ValueOnlyHand objects.
    
    Args:
        player_value: Player's final hand value
        dealer_value: Dealer's current hand value
        is_soft: Whether dealer's hand is soft
        shoe: The shoe to sample from
        n_sim_runs: Number of simulation runs
        dealer_hit_soft_17: Whether dealer hits on soft 17
    
    Returns:
        Mean EV from simulation runs
    """
    sum_values = 0.0
    burned_cards = []
    
    for _ in range(max(1, n_sim_runs)):
        burned_cards.clear()
        current_value = dealer_value
        current_is_soft = is_soft
        
        # Dealer draw loop
        while True:
            # Check if bust
            if current_value > 21:
                break
            
            # Check if dealer should stand
            if dealer_hit_soft_17:
                should_stand = (current_value > 17) or \
                               (current_value == 17 and not current_is_soft)
            else:
                should_stand = (current_value >= 17)
            
            if should_stand:
                break
            
            # Draw a card
            rank = shoe.sample_and_burn_rank()
            burned_cards.append(rank)
            
            # Update hand value inline
            current_value, current_is_soft = _add_card_to_hand_value(
                current_value, current_is_soft, rank
            )
        
        # Collect results
        if current_value > 21:
            sum_values += 1.0  # dealer bust
        else:
            if current_value < player_value:
                sum_values += 1.0
            elif current_value > player_value:
                sum_values -= 1.0
            # else: push, 0.0
        
        # Restore shoe state
        for card in burned_cards:
            shoe.add_rank_value(card)
    
    return sum_values / max(1, n_sim_runs)



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
            shoe_copy.reset_sampler()

        # Simulate dealer cards until round over
        while not bj_round_copy.get_stage() == BJStage.ROUND_OVER:
            possible_values = bj_round_copy.get_possible_next_card_ranks()
            card = shoe_copy.sample_and_burn_rank(possible_values)
            bj_round_copy.take_card(card)
        
        # Collect results
        values.append(bj_round_copy.get_player_value())
    return np.mean(values)
          

total_sim_time = 0

def run_dealer_cards_simulation_recursive(
    bj_round: BJRound,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int,
    n_full_sample: int = 5,
    simulation_for_last_hand=False
):
    """Run Monte Carlo simulations for dealer play."""
    start_time = time.time()
    stage = bj_round.get_stage()

    if stage == BJStage.ROUND_OVER:
        return bj_round.get_player_value()

    if simulation_for_last_hand:
        player_hand = bj_round.player_hands[-1]
    else:
        assert len(bj_round.player_hands) == 1
        player_hand = bj_round.player_hands[0]
    
    assert not player_hand.is_natural_blackjack()

    assert stage == BJStage.DEALER_CARD
    
    shoe = shoe.copy()
    if shoe.is_dealer_card_locked():
        shoe.unlock_dealer_card()

    possible_ranks = bj_round.get_possible_next_card_ranks()

    value = bj_round.bet_unit * _run_dealer_cards_simulation_recursive(
        player_hand.get_best_value(),
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


def _run_dealer_cards_simulation_recursive(
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
            value_by_first_card[first_card] = _run_dealer_cards_simulation_recursive(
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
                dealer_hit_soft_17,
                new_random_sampler=True
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
    new_random_sampler = False
):
    values = []
    
    if new_random_sampler:
        shoe = shoe.copy()
        shoe.reset_sampler()

    for i in range(n_dealer_sim_runs):
        # Simulate dealer cards until round over
        
        dealer_hand_sim = dealer_hand.copy()
        shoe_sim = shoe

        burned_cards = []
        while (
            not dealer_hand_sim.is_bust() 
            and not _dealer_stand_or_bust(dealer_hand_sim, dealer_hit_soft_17)
        ):
            rank = shoe_sim.sample_and_burn_rank()
            dealer_hand_sim.add_card(rank)
            burned_cards.append(rank)
        
        for card in burned_cards:
            shoe_sim.add_rank_value(card)

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


def get_cards_probability(cards, shoe, possible_first_card=None):
    prob = 1
    burned_cards = []
    for i, card in enumerate(cards):
        if i == 0:
            possible_cards = possible_first_card
        else:
            possible_cards = None
        probs = shoe.get_rank_value_probabilities(possible_cards)
        if card not in probs or probs[card] == 0:
            prob = 0
            break

        prob = prob * probs[card]
        shoe.burn_rank_value(card)
        burned_cards.append(card)
    
    for card in burned_cards:
        shoe.add_rank_value(card)
    
    return prob


import pickle


def _load_and_precompute_combo_data(filepath: str) -> dict:
    """
    Load pickle file and precompute combo_id, dealer_value, and is_soft for all combinations.
    This avoids recalculating these values on every function call.
    
    Args:
        filepath: Path to the pickle file containing raw combo data
    
    Returns:
        Precomputed dict with enhanced combo tuples
    """
    with open(filepath, "rb") as f:
        raw_data = pickle.load(f)
    
    precomputed = {}
    for depth, upcard_data in raw_data.items():
        precomputed[depth] = {}
        for upcard, data in upcard_data.items():
            precomputed[depth][upcard] = {
                'stand_combos_data': [],
                'other_combos_data': [],
                'bust_combos_data': data.get('bust_combos_data', []),  # Keep as-is (not used in optimized version)
                'stand_values': data.get('stand_values', [])  # Keep for backwards compatibility
            }
            
            # Precompute stand combos: (combo, num_perms, combo_id, dealer_value)
            for combo, num_perms in data.get('stand_combos_data', []):
                combo_id = _get_dealer_cards_id(combo)
                dealer_value = upcard
                is_soft = (upcard == 11)
                for card in combo:
                    dealer_value, is_soft = _add_card_to_hand_value(dealer_value, is_soft, card)
                precomputed[depth][upcard]['stand_combos_data'].append(
                    (combo, num_perms, combo_id, dealer_value)
                )
            
            # Precompute other combos: (combo, num_perms, combo_id, dealer_value, is_soft)
            for combo, num_perms in data.get('other_combos_data', []):
                combo_id = _get_dealer_cards_id(combo)
                dealer_value = upcard
                is_soft = (upcard == 11)
                for card in combo:
                    dealer_value, is_soft = _add_card_to_hand_value(dealer_value, is_soft, card)
                precomputed[depth][upcard]['other_combos_data'].append(
                    (combo, num_perms, combo_id, dealer_value, is_soft)
                )
    
    return precomputed


# Precompute all combo data at import time
precomputed_combinations_with_counts_no_bj = _load_and_precompute_combo_data(
    "combinations/combinations_with_counts_no_bj_d13.pkl"
)
precomputed_combinations_with_counts = _load_and_precompute_combo_data(
    "combinations/combinations_with_counts_d13.pkl"
)




def run_dealer_cards_simulation_combo_old(
    bj_round: BJRound,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int = None,
    n_full_sample: int = 5,
    verbose = False,
    simulation_for_last_hand = False
):
    dealer_hand: ValueOnlyHand = bj_round.dealer_hand.copy()
    dealer_hit_soft_17 = bj_round.rules.dealer_hits_soft_17
    assert dealer_hand.size() == 1
    assert bj_round.get_stage() == BJStage.DEALER_CARD

    if simulation_for_last_hand:
        player_hand = bj_round.player_hands[-1]
    else:
        assert len(bj_round.player_hands) == 1
        player_hand = bj_round.player_hands[0]
    assert not player_hand.is_natural_blackjack()

    if n_dealer_sim_runs is None:
        n_dealer_sim_runs = 1
        
    if shoe.is_dealer_card_locked():
        shoe = shoe.copy()
        shoe.unlock_dealer_card()
        
    player_hand_value = player_hand.get_best_value()

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
    for i_comb, (combo, num_perms) in enumerate(bust_combos_data):
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
    for i_comb, (combo, num_perms) in enumerate(stand_combos_data):
        prob = get_cards_probability(combo, shoe, possible_second_card)
        if prob != 0:
            prob = prob * num_perms
            total_p += prob
            prob_stand[i_comb] = prob

    ev_other = np.zeros(len(other_combos_data))
    prob_other= np.zeros(len(other_combos_data))
    
    for i_comb, (combo, num_perms) in enumerate(other_combos_data):
        prob = 1
        impossible = False
        burned_cards = []
        for i_card, card in enumerate(combo):
            if i_card == 0:
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
        prob_other[i_comb] = prob

        assert not _dealer_stand_or_bust(dealer_hand, dealer_hit_soft_17)
        sim_values = _run_dealer_cards_simulation(
            player_hand_value,
            dealer_hand,
            shoe,
            num_perms * n_dealer_sim_runs,
            dealer_hit_soft_17,
            new_random_sampler=False
        )
        ev_other[i_comb] = np.mean(sim_values)
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
    if np.abs(total_p - 1) > 1e-8:
        raise RuntimeError(f"Probability does not sum to 1 , total_p = {total_p}")
    return final_value


def run_dealer_cards_simulation_combo(
    bj_round: BJRound,
    shoe: ProbabilisticRankShoe,
    n_dealer_sim_runs: int = None,
    n_full_sample: int = 5,
    verbose: bool = False,
    simulation_for_last_hand: bool = False
):
    """
    Optimized version of run_dealer_cards_simulation_combo with C++ style optimizations:
    1. Dynamic programming for shoe probability calculations with caching
    2. Precomputed dealer values to avoid recalculation
    3. Simplified simulation using inline hand value updates
    4. Skip bust combo calculation (infer from 1 - total_p)
    
    Args:
        bj_round: Current blackjack round state
        shoe: The shoe to sample from
        n_dealer_sim_runs: Number of Monte Carlo runs per combo (default: 1)
        n_full_sample: Depth of precomputed combinations to use
        verbose: Print debug information
        simulation_for_last_hand: Use last player hand (for splits)
    
    Returns:
        Expected value of the round
    """
    dealer_hand: ValueOnlyHand = bj_round.dealer_hand.copy()
    dealer_hit_soft_17 = bj_round.rules.dealer_hits_soft_17
    upcard = dealer_hand.cards[0]
    
    assert dealer_hand.size() == 1
    assert bj_round.get_stage() == BJStage.DEALER_CARD

    if simulation_for_last_hand:
        player_hand = bj_round.player_hands[-1]
    else:
        assert len(bj_round.player_hands) == 1
        player_hand = bj_round.player_hands[0]
    assert not player_hand.is_natural_blackjack()

    if n_dealer_sim_runs is None:
        n_dealer_sim_runs = 1
    
    # Copy and unlock shoe if needed
    if shoe.is_dealer_card_locked():
        shoe = shoe.copy()
        shoe.unlock_dealer_card()
        
    player_hand_value = player_hand.get_best_value()

    # Load precomputed combinations
    assert not bj_round.rules.dealer_hits_soft_17, "dealer_hits_soft_17 not supported in combo algorithm"
    if bj_round.rules.dealer_checks_blackjack:
        assert not bj_round.dealer_has_bj_after_check
        data = precomputed_combinations_with_counts_no_bj[n_full_sample][upcard]
    else:
        data = precomputed_combinations_with_counts[n_full_sample][upcard]
        
    possible_second_card = bj_round.get_possible_next_card_ranks()

    bust_combos_data = data['bust_combos_data']
    stand_combos_data = data['stand_combos_data']  # Now: (combo, num_perms, combo_id, dealer_value)
    other_combos_data = data['other_combos_data']  # Now: (combo, num_perms, combo_id, dealer_value, is_soft)
    
    # Initialize DP cache for shoe probability calculations
    shoe_cache: Dict[int, ShoeState] = {}
    
    total_p = 0.0
    ev_stand_value = 0.0
    
    # Process stand combos with DP cache - using precomputed combo_id and dealer_value
    for combo, num_perms, combo_id, dealer_value in stand_combos_data:
        prob, _ = _get_dealer_cards_probability(
            combo_id, shoe_cache, shoe, possible_second_card
        )
        
        if prob == 0.0:
            continue
        
        # Compute EV for this stand outcome (dealer_value is precomputed)
        if player_hand_value > dealer_value:
            ev_stand = 1.0
        elif player_hand_value < dealer_value:
            ev_stand = -1.0
        else:
            ev_stand = 0.0
        
        prob *= num_perms
        total_p += prob
        ev_stand_value += ev_stand * prob
    
    # Process other combos (need simulation) - using precomputed values
    ev_other_value = 0.0
    p_other = 0.0
    
    for combo, num_perms, combo_id, dealer_value, is_soft in other_combos_data:
        prob_cached, rank_count_cached = _get_dealer_cards_probability(
            combo_id, shoe_cache, shoe, possible_second_card
        )
        
        if prob_cached == 0.0:
            continue
        
        prob = prob_cached * num_perms
        total_p += prob
        p_other += prob
        
        # Create a shoe with the cached rank counts for simulation
        # Use the optimized simple simulation
        sim_shoe = ProbabilisticRankShoe(1)  # Create minimal shoe
        for rv in range(2, 12):
            sim_shoe.set_number_of_rank_cards(rv, rank_count_cached.get(rv, 0))
        sim_shoe.reset_sampler(RandomSampler.create_next_sampler())
        
        # dealer_value and is_soft are precomputed
        mean_value = _run_dealer_cards_simulation_simple(
            player_hand_value,
            dealer_value,
            is_soft,
            sim_shoe,
            num_perms * n_dealer_sim_runs,
            dealer_hit_soft_17
        )
        
        ev_other_value += mean_value * prob
    
    # Optimization: infer bust probability from remaining probability mass
    # This avoids iterating through all bust combos
    dealer_bust_p = 1.0 - total_p
    total_p = 1.0
    
    p_bust = dealer_bust_p
    ev_bust_value = 1.0 * dealer_bust_p
    p_stand = total_p - p_bust - p_other
    
    if verbose:
        print(f"shoe_cache.size = {len(shoe_cache)}")
        print(f"p_bust = {p_bust}")
        print(f"p_stand = {p_stand}, ev_stand_value = {ev_stand_value}")
        print(f"p_other = {p_other}, ev_other_value = {ev_other_value}")
        print(f"p_total = {total_p}")
    
    final_value = bj_round.bet_unit * (
        ev_other_value + ev_stand_value + ev_bust_value
    )

    return final_value