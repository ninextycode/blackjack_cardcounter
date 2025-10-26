#pragma once
#include <string>
#include <vector>

using namespace std;

namespace blackjack {

enum class Suit { SPADE, HEART, CLUB, DIAMOND, STUB };

enum class Rank {
    TWO, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE,
    TEN, JACK, QUEEN, KING, ACE
};

inline int rank_value(Rank r, bool soft=true) {
    switch(r) {
        case Rank::ACE: return soft ? 11 : 1;
        case Rank::KING: case Rank::QUEEN: case Rank::JACK: case Rank::TEN: return 10;
        case Rank::NINE: return 9;
        case Rank::EIGHT: return 8;
        case Rank::SEVEN: return 7;
        case Rank::SIX: return 6;
        case Rank::FIVE: return 5;
        case Rank::FOUR: return 4;
        case Rank::THREE: return 3;
        case Rank::TWO: return 2;
    }
    return 0;
}

inline char value_to_rank_char(int value) {
    // Convert a card value (2-11) to rank character matching Python Rank.from_value().value
    switch(value) {
        case 2: return '2';
        case 3: return '3';
        case 4: return '4';
        case 5: return '5';
        case 6: return '6';
        case 7: return '7';
        case 8: return '8';
        case 9: return '9';
        case 10: return 'T';
        case 11: return 'A';
        default: return '?';
    }
}

struct Card {
    Rank rank;
    Suit suit;
    Card(Rank r, Suit s = Suit::STUB) : rank(r), suit(s) {}
    inline int rank_value(bool soft=true) const { return blackjack::rank_value(rank, soft); }
    inline string str() const {
    static const char *rchars = "23456789TJQKA";
        int idx = 0;
        switch(rank) {
            case Rank::TWO: idx = 0; break; case Rank::THREE: idx = 1; break; case Rank::FOUR: idx = 2; break;
            case Rank::FIVE: idx = 3; break; case Rank::SIX: idx = 4; break; case Rank::SEVEN: idx = 5; break;
            case Rank::EIGHT: idx = 6; break; case Rank::NINE: idx = 7; break; case Rank::TEN: idx = 8; break;
            case Rank::JACK: idx = 9; break; case Rank::QUEEN: idx = 10; break; case Rank::KING: idx = 11; break;
            case Rank::ACE: idx = 12; break;
        }
        char rc = rchars[idx];
        char sc = ' ';
        switch(suit) { case Suit::SPADE: sc='s'; break; case Suit::HEART: sc='h'; break; case Suit::CLUB: sc='c'; break; case Suit::DIAMOND: sc='d'; break; default: sc='.'; }
    return string()+rc+sc;
    }
};

} // namespace blackjack
