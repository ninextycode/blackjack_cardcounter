import copy
from enum import Enum, auto
import numpy as np
from itertools import chain
from collections import deque
from functools import total_ordering


class SuitStub(Enum):
    STUB = "."


@total_ordering
class Suit(Enum):
    SPADE = "s"
    HEART = "h"
    CLUB = "c"
    DIAMOND = "d"

    def ord_value(self):
        return "dchs".index(self.value)

    def __eq__(self, other):
        return self.ord_value() == other.ord_value()

    def __lt__(self, other):
        return self.ord_value() < other.ord_value()


@total_ordering
class Rank(Enum):
    ACE = "A"
    KING = "K"
    QUEEN = "Q"
    JACK = "J"
    TEN = "T"
    NINE = "9"
    EIGHT = "8"
    SEVEN = "7"
    SIX = "6"
    FIVE = "5"
    FOUR = "4"
    THREE = "3"
    TWO = "2"

    @staticmethod
    def from_value(rank_value):
        # for the value of ten just use card T
        if rank_value == 10:
            return Rank.TEN
        
        if rank_value == 1:
            return Rank.ACE
        
        for rank in Rank:
            if rank.rank_value() == rank_value:
                return rank
        raise ValueError(f"Invalid rank value {rank_value}")

    def rank_value(self, soft=True):
        if self == Rank.ACE:
            return 11 if soft else 1
        elif self in [Rank.KING, Rank.QUEEN, Rank.JACK, Rank.TEN]:
            return 10
        elif self == Rank.NINE:
            return 9
        elif self == Rank.EIGHT:
            return 8
        elif self == Rank.SEVEN:
            return 7
        elif self == Rank.SIX:
            return 6
        elif self == Rank.FIVE:
            return 5
        elif self == Rank.FOUR:
            return 4
        elif self == Rank.THREE:
            return 3
        elif self == Rank.TWO:
            return 2
        else:
            raise ValueError(f"Unexpected rank {self}")

    def suitless_card(self):
        return Card(self, SuitStub.STUB)

    def ord_value(self):
        return "23456789TJQKA".index(self.value)

    def __eq__(self, other):
        return self.ord_value() == other.ord_value()

    def __lt__(self, other):
        return self.ord_value() < other.ord_value()



@total_ordering
class Card:
    def __init__(self, rank: Rank, suit: Suit | SuitStub = SuitStub.STUB):
        self.rank: Rank = rank
        self.suit: Suit = suit

    def __eq__(self, value):
        return self.rank == value.rank and self.suit == value.suit

    def __lt__(self, other):
        if self.rank < other.rank:
            return True
        if self.rank > other.rank:
            return False
        return self.suit < other.suit

    def from_str(card_str: str):
        rank_char = card_str[0]
        suit_char = card_str[1] if len(card_str) > 1 else "."
        rank = Rank(rank_char)
        suit = SuitStub.STUB if suit_char == "." else Suit(suit_char)
        return Card(rank, suit)

    def rank_value(self, soft=True):
        return self.rank.rank_value(soft)

    def __str__(self):
        return f"{self.rank.value}{self.suit.value}"

    def __repr__(self):
        return f"<{self.__class__.__name__}: {self.rank.value}{self.suit.value}>"
