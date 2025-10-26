#!/usr/bin/env python3
"""
Test script to verify C++ and Python implementations produce identical results
"""

import sys
sys.path.append('/home/maxim/Programming/blackjack_cardcounter')

from blackjack.blackjack_round import BJRound as PyBJRound
from blackjack.rules import BJRules as PyBJRules
from blackjack.actions import PlayerAction, DealerAction

def test_double_down():
    """Test that double down correctly doubles the bet"""
    print("=" * 50)
    print("TEST: Double Down Bet Calculation")
    print("=" * 50)
    
    rules = PyBJRules()
    round = PyBJRound(rules)
    round.start_round(bet_unit=10)
    
    # Player gets 11
    round.take_card(5)
    round.take_card(6)
    
    # Dealer gets 7
    round.take_card(7)
    
    # Player doubles down
    print(f"Before double: hand_bets = {round.hand_bets}")
    round.take_action(PlayerAction.DOUBLE)
    print(f"After double: hand_bets = {round.hand_bets}")
    assert round.hand_bets[0] == 20, f"Expected bet of 20, got {round.hand_bets[0]}"
    
    # Player gets 10 (total 21)
    round.take_card(10)
    
    # Dealer draws to completion
    round.take_card(10)  # 17
    
    print(f"Final state: {round}")
    print(f"Player bet: {round.total_player_bet}")
    print(f"Player payout: {round.total_player_got}")
    print(f"Player value: {round.player_value}")
    
    assert round.total_player_bet == 20, f"Expected total bet of 20, got {round.total_player_bet}"
    assert round.total_player_got == 40, f"Expected payout of 40, got {round.total_player_got}"
    assert round.player_value == 20, f"Expected net value of 20, got {round.player_value}"
    
    print("✅ PASSED: Double down bet calculation correct")
    print()

def test_split_availability():
    """Test split availability with different max_splits_allowed values"""
    print("=" * 50)
    print("TEST: Split Availability")
    print("=" * 50)
    
    # Test with max_splits_allowed = 1
    rules = PyBJRules(max_splits_allowed=1)
    round = PyBJRound(rules)
    round.start_round(bet_unit=10)
    
    # Player gets pair of 8s
    round.take_card(8)
    round.take_card(8)
    
    # Dealer gets 6
    round.take_card(6)
    
    actions = round.get_available_actions()
    print(f"With max_splits=1, n_splits=0: Split available = {PlayerAction.SPLIT in actions}")
    assert PlayerAction.SPLIT in actions, "Split should be available"
    
    # After one split, split should not be available
    round.take_action(PlayerAction.SPLIT)
    round.take_card(3)  # First split hand
    round.take_card(5)  # Second split hand
    
    actions = round.get_available_actions()
    print(f"With max_splits=1, n_splits=1: Split available = {PlayerAction.SPLIT in actions}")
    assert PlayerAction.SPLIT not in actions, "Split should not be available after reaching limit"
    
    print("✅ PASSED: Split availability correct")
    print()

def test_stand_multiple_hands():
    """Test standing on multiple hands advances correctly"""
    print("=" * 50)
    print("TEST: Stand on Multiple Hands")
    print("=" * 50)
    
    rules = PyBJRules(max_splits_allowed=1)
    round = PyBJRound(rules)
    round.start_round(bet_unit=10)
    
    # Player gets pair of 8s
    round.take_card(8)
    round.take_card(8)
    
    # Dealer gets 10
    round.take_card(10)
    
    # Split
    round.take_action(PlayerAction.SPLIT)
    round.take_card(3)  # First hand: 8,3
    round.take_card(5)  # Second hand: 8,5
    
    # Should be on first hand (index 0)
    print(f"Active hand index: {round.active_hand_idx}")
    assert round.active_hand_idx == 0, f"Expected active_hand_idx=0, got {round.active_hand_idx}"
    
    # Stand on first hand
    round.take_action(PlayerAction.STAND)
    
    # Should advance to second hand (index 1)
    print(f"After stand, active hand index: {round.active_hand_idx}")
    assert round.active_hand_idx == 1, f"Expected active_hand_idx=1 after stand, got {round.active_hand_idx}"
    
    # Stand on second hand
    round.take_action(PlayerAction.STAND)
    
    # Should move to dealer
    print(f"Stage after standing on all hands: {round.stage}")
    from blackjack.blackjack_round import BJStage
    assert round.stage == BJStage.DEALER_CARD, f"Expected DEALER_CARD stage, got {round.stage}"
    
    print("✅ PASSED: Stand advances correctly through multiple hands")
    print()

if __name__ == "__main__":
    print("\n" + "="*60)
    print("PYTHON IMPLEMENTATION TESTS")
    print("="*60 + "\n")
    
    try:
        test_double_down()
        test_split_availability()
        test_stand_multiple_hands()
        
        print("\n" + "="*60)
        print("✅ ALL TESTS PASSED!")
        print("="*60 + "\n")
        
        print("Now run the equivalent C++ tests to verify identical behavior.")
        
    except AssertionError as e:
        print(f"\n❌ TEST FAILED: {e}\n")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ ERROR: {e}\n")
        import traceback
        traceback.print_exc()
        sys.exit(1)
