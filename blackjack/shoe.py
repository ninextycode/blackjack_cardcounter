from blackjack.cards import Card, Rank
import numpy as np

"""
Global RNG for the shoe module. New ProbabilisticRankShoe instances
will use this generator by default unless an explicit rng is provided.
This centralizes randomness and improves reproducibility.
"""
_global_rng: np.random.Generator = np.random.default_rng()

def set_shoe_rng(rng: np.random.Generator) -> None:
    global _global_rng
    _global_rng = rng

def seed_shoe_rng(seed: int | None) -> None:
    global _global_rng
    _global_rng = np.random.default_rng(seed)




class ProbabilisticRankShoe:
    def __init__(self, n_decks = 8, rng: np.random.Generator | None = None):
        self.n_decks = n_decks
        self.n_total = 52 * self.n_decks
        self.rank_value_counts = {
            rv: 0 for rv in range(2, 12)
        }
        for r in Rank:
            rv = r.rank_value()
            self.rank_value_counts[rv] += self.n_total / len(Rank)
        self.given_dealer_card_is_not_value = None
        # Use a dedicated RNG for reproducibility; default to module-level SHOE_RNG
        self.rng: np.random.Generator = rng if rng is not None else _global_rng

 
    def sample_rank(self, given_rank_values_set=None):
        probabilities = self.get_rank_value_probabilities(given_rank_values_set)
        rank_values = list(probabilities.keys())
        probs = [probabilities[rv] for rv in rank_values]
        sampled_rank_value = self.rng.choice(rank_values, p=probs)
        return sampled_rank_value


    def sample_and_burn_rank(self, given_rank_values_set=None):
        sampled_rank_value = self.sample_rank(given_rank_values_set)
        self.burn_rank_value(sampled_rank_value)
        return sampled_rank_value


    def copy(self):
        # Keep the same RNG reference to maintain a single source of randomness unless overridden
        new_shoe = ProbabilisticRankShoe(self.n_decks, rng=self.rng)
        new_shoe.n_total = self.n_total
        new_shoe.rank_value_counts = self.rank_value_counts.copy()
        new_shoe.given_dealer_card_is_not_value = self.given_dealer_card_is_not_value
        return new_shoe
    

    def get_rank_value_probabilities(self, given_rank_values_set=None):
        probabilities = self._get_raw_rank_value_probabilities()
        probabilities = self._take_given_dealer_info_into_account(probabilities)
        probabilities = self._probabilities_given_rank_values_set(
            probabilities, given_rank_values_set
        )
        return probabilities
    

    def _take_given_dealer_info_into_account(self, probabilities):
        if self.given_dealer_card_is_not_value is None:
            return probabilities
    
        n_cards_dealer_card_is_not = sum([
            count for rv, count in self.rank_value_counts.items()
            if rv != self.given_dealer_card_is_not_value
        ])
        card_dealer_card_is_not_coef = (n_cards_dealer_card_is_not - 1) / n_cards_dealer_card_is_not

        for rv, p in probabilities.items():
            p = p * self.n_total / (self.n_total - 1)
            if rv == self.given_dealer_card_is_not_value:
                probabilities[rv] = p
            else:
                probabilities[rv] = p * card_dealer_card_is_not_coef
        
        return probabilities


    def _probabilities_given_rank_values_set(self, probabilities, given_rank_values_set):
        if given_rank_values_set is None:
            return probabilities
        
        given_rank_p = sum(probabilities[rv] for rv in given_rank_values_set)
        for rv, p in probabilities.items():
            if rv in given_rank_values_set:
                probabilities[rv] = p / given_rank_p
            else:
                probabilities[rv] = 0.0
        return probabilities
    

    def _get_raw_rank_value_probabilities(self):
        return {
            rv: count / self.n_total
            for rv, count in self.rank_value_counts.items()
        }


    def burn_card(self, card: Card):
        self.burn_rank_value(card.rank_value())


    def burn_rank_value(self, rank_value):
        if self.rank_value_counts[rank_value] < 1:
            raise RuntimeError(f"Card count for rank value {rank_value} is too low")
        self.rank_value_counts[rank_value] -= 1
        self.n_total -= 1


    def lock_dealer_card_not_ace(self):
        self.given_dealer_card_is_not_value = 11


    def lock_dealer_card_not_ten(self):
        self.given_dealer_card_is_not_value = 10


    def unlock_dealer_card(self):
        self.given_dealer_card_is_not_value = None

    def __str__(self):
        probabilities = self.get_rank_value_probabilities()
        info_lines = [self.__class__.__name__]
        d_not_str = ""
        if self.given_dealer_card_is_not_value is not None:
            d_not_str = f"|D≠{self.given_dealer_card_is_not_value}"
        for rv in range(2, 12):
            p = probabilities[rv]
            if p == 0:
                continue
            info_lines.append(f"  p({rv:2d}{d_not_str}) = {p*100:.2f}%")
        return "\n".join(info_lines)