from blackjack.cards import Card, Rank


class Hand:
    def __init__(self, cards: list[Card]=None):
        if cards is None:
            cards = list()
        self.cards: list[Card] = list(cards)
        self._best_value = 0
        self._hard_value = 0
        self._reset_value()
        
    def size(self):
        return len(self.cards)

    def copy(self):
        return Hand(self.cards)

    def add_card(self, card):
        self.cards.append(card)
        self._reset_value()

    def ranks(self):
        return [c.rank for c in self.cards]

    def is_same_rank_pair(self):
        if len(self.cards) != 2:
            return False
        rank1 = self.cards[0].rank
        rank2 = self.cards[1].rank
        return rank1 == rank2
    
    def is_same_value_pair(self):
        if len(self.cards) != 2:
            return False
        value1 = self.cards[0].rank_value(soft=False)
        value2 = self.cards[1].rank_value(soft=False)
        return value1 == value2

    def split(self):
        return Hand([self.cards[0]]), Hand([self.cards[1]])


    def is_natural_blackjack(self):
        return len(self.cards) == 2 and self.get_best_value() == 21

    def is_soft(self):
        hard_value = self.get_hard_value()
        return self.get_best_value() != hard_value

    def is_soft_17(self):
        ranks = self.ranks()
        has_ace = Rank.ACE in ranks
        hard_value_no_ace = \
            sum([r.rank_value(soft=False) for r in ranks]) - 1
        return has_ace and (hard_value_no_ace + 11 == 17)

    def _reset_best_value(self):
        soft_value = 0
        n_soft_aces = 0
        for c in self.cards:
            soft_value += c.rank_value(soft=True)
            if c.rank == Rank.ACE:
                n_soft_aces += 1

        value = soft_value
        while value > 21 and n_soft_aces > 0:
            value -= 10
            n_soft_aces -= 1

        if value > 21:
            value = None
        self._best_value = value

    def _reset_hard_value(self):
        self._hard_value = sum(
            [r.rank_value(soft=False) for r in self.ranks()]
        )
    
    def _reset_value(self):
        self._reset_best_value()
        self._reset_hard_value()
    
    def get_best_value(self):
        return self._best_value

    def get_hard_value(self):
        return self._hard_value

    def is_natural_blackjack(self):
        return len(self.cards) == 2 and self.get_best_value() == 21
    
    def is_bust(self):
        return self.get_best_value() is None
    
    def __getitem__(self, idx):
        return self.cards[idx]
 
    def __str__(self):
        value = self.get_best_value()
        if value is None:
            value = f"bust {self.get_hard_value()}"
        return "[" + ",".join([str(c) for c in self.cards]) + f"] ({value})"
 
    def __repr__(self):
        return f"<{self.__class__.__name__}: {str(self)}>"

    def __len__(self):
        return len(self.cards)


class ValueOnlyHand:
    """
    Hand-like class that stores integer soft card values instead of Card objects.
    Each value is expected to be the soft value (Ace as 11, other cards as 2-10).
    Provides a similar API to Hand for use in logic that only needs numeric totals.
    Note: is_same_rank_pair cannot be determined from integer values alone and returns False.
    """
    def __init__(self, values: list[int] = None):
        if values is None:
            values = []
        self.cards: list[int] = list(values)
        self._best_value = 0
        self._hard_value = 0
        self._reset_value()

    def size(self) -> int:
        return len(self.cards)

    def copy(self):
        return ValueOnlyHand(self.cards)

    def add_card(self, value: int):
        self.cards.append(value)
        self._reset_value()

    def is_same_rank_pair(self) -> bool:
        raise NotImplementedError

    def is_same_value_pair(self) -> bool:
        if len(self.cards) != 2:
            return False
        v1 = self.cards[0]
        v2 = self.cards[1]
        return v1 == v2

    def split(self):
        return ValueOnlyHand([self.cards[0]]), ValueOnlyHand([self.cards[1]])

    def is_natural_blackjack(self) -> bool:
        return len(self.cards) == 2 and self.get_best_value() == 21

    def is_soft(self) -> bool:
        return self.get_best_value() != self.get_hard_value()

    def is_soft_17(self) -> bool:
        has_ace = any(v == 11 for v in self.cards)
        hard_sum = self.get_hard_value()
        # hard_sum counts each soft ace as 1. Subtract that 1 to get non-ace sum.
        hard_value_no_ace = hard_sum - 1 if has_ace else hard_sum
        return has_ace and (hard_value_no_ace + 11 == 17)

    def _reset_best_value(self):
        soft_value = sum(self.cards)
        n_soft_aces = sum(1 for v in self.cards if v == 11)

        value = soft_value
        while value > 21 and n_soft_aces > 0:
            value -= 10
            n_soft_aces -= 1

        if value > 21:
            value = None
        self._best_value = value

    def _reset_hard_value(self):
        # Treat any soft Ace (11) as 1 for hard total.
        self._hard_value = sum(1 if v == 11 else v for v in self.cards)

    def _reset_value(self):
        self._reset_best_value()
        self._reset_hard_value()

    def get_best_value(self):
        return self._best_value

    def get_hard_value(self):
        return self._hard_value

    def is_bust(self) -> bool:
        return self.get_best_value() is None

    def __str__(self):
        value = self.get_best_value()
        hard_value = self.get_hard_value()
        if value is None:
            value = "bust"
        vals = ",".join(["A" if v == 11 else str(v) for v in self.cards])

        if value == hard_value:
            value_lbl = f"({value})"
        else:
            value_lbl = f"({hard_value}/{value})"
        return f"{vals}{value_lbl}"

    def __repr__(self):
        return f"<{self.__class__.__name__}: {str(self)}>"

    def __len__(self):
        return len(self.cards)
    
    def __getitem__(self, idx):
        return self.cards[idx]
