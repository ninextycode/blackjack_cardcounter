#include "cpp_exec/main.h"
#include "tree_walker.h"
#include "floor_ceil_node.h"
#include "blackjack_round.h"
#include "shoe.h"
#include "actions.h"
#include "rules.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <chrono>

using namespace std;
using namespace blackjack;

void blackjack::testTreeWalker() {
    cout << "\n=== Test: Basic TreeWalker ===" << endl;

    // Setup rules using default and modify as needed
    BJRules rules = getDefaultRules();

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(100);

    // Deal initial cards: player 10, 6; dealer 6
    bj_round.takeCard(10);
    bj_round.takeCard(6);
    bj_round.takeCard(6);

    shoe.burnRankValue(10);
    shoe.burnRankValue(6);
    shoe.burnRankValue(6);

    cout << "Initial round state:" << endl;
    cout << bj_round.toString() << endl;

    // Create root node using buildNonFinalRootNode (returns AbstractFloorCeilNode)
    int max_hand_size_full_enum = 3;
    int dealer_sim_depth = 5;
    auto root_node = buildNonFinalRootNode(
        bj_round,
        shoe,
        max_hand_size_full_enum,
        dealer_sim_depth,
        SimAlgo::COMBO
    );

    // Create TreeWalker
    TreeWalker walker(root_node);

    cout << "\nInitial state:" << endl;
    if (!walker.finished()) {
        cout << walker.getStateInfo() << endl;
    }

    // Check if expecting player action
    if (!walker.finished() && walker.needPlayerAction()) {
        cout << "\nGetting best actions..." << endl;
        auto best_actions = walker.getBestActions();
        cout << "Actions (sorted from best to worst):" << endl;
        for (const auto& [action, estimate] : best_actions) {
            cout << "  " << to_string(action) << ": " 
                 << fixed << setprecision(6) << estimate.ev 
                 << " [" << estimate.ev_min << ", " << estimate.ev_max << "]" << endl;
        }

        // Get best action directly
        PlayerAction best_action = walker.getBestAction();
        cout << "\nBest action: " << to_string(best_action) << endl;
        auto best_estimate = walker.getEventValueEstimate(TransitionEvent(best_action));
        cout << "Best action EV: " << fixed << setprecision(6) 
             << best_estimate.ev << " [" << best_estimate.ev_min << ", " << best_estimate.ev_max << "]" << endl;

        // Try taking a player action
        if (!walker.finished()) {
            auto best_actions = walker.getBestActions();
            if (!best_actions.empty()) {
                PlayerAction best_action = best_actions[0].first;
                cout << "\nTaking best action: " << to_string(best_action) << endl;
                walker.takePlayerAction(best_action);
                cout << "After action:" << endl;
                if (!walker.finished()) {
                    cout << walker.getStateInfo() << endl;
                } else {
                    cout << "  Round finished" << endl;
                }
            }
        }
    }
}

void blackjack::testTreeWalkerWithCards() {
    cout << "\n=== Test: TreeWalker with Cards ===" << endl;

    // Setup rules using default and modify as needed
    BJRules rules = getDefaultRules();
    rules.dealer_checks_blackjack = false;
    rules.allow_insurance_vs_ace = false;
    rules.max_splits_allowed = 0;
    rules.allow_action_on_split_aces = false;
    rules.allow_double_after_split = false;
    rules.allow_split_different_tens = false;

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(100);

    // Deal initial cards: player 10, 5; dealer 6
    bj_round.takeCard(10);
    bj_round.takeCard(5);
    bj_round.takeCard(6);

    shoe.burnRankValue(10);
    shoe.burnRankValue(5);
    shoe.burnRankValue(6);

    cout << "Initial round state:" << endl;
    cout << bj_round.toString() << endl;

    auto root_node = buildNonFinalRootNode(
        bj_round,
        shoe,
        3,
        5,
        SimAlgo::COMBO
    );

    TreeWalker walker(root_node);

    cout << "\nInitial state:" << endl;
    if (!walker.finished()) {
        cout << walker.getStateInfo() << endl;
    }

    // Player should hit
    if (!walker.finished() && walker.needPlayerAction()) {
        cout << "\nPlayer action decision:" << endl;
        auto best_actions = walker.getBestActions();
        for (const auto& [action, estimate] : best_actions) {
            cout << "  " << to_string(action) << ": " 
                 << fixed << setprecision(6) << estimate.ev 
                 << " [" << estimate.ev_min << ", " << estimate.ev_max << "]" << endl;
        }

        // Take HIT action
        walker.takePlayerAction(PlayerAction::HIT);
        cout << "\nAfter HIT action:" << endl;
        if (!walker.finished()) {
            cout << walker.getStateInfo() << endl;
        }

        // Now expecting a card
        if (!walker.finished() && walker.needCard()) {
            cout << "\nTaking card: 5" << endl;
            walker.takeCard(5);
            cout << walker.getStateInfo() << endl;
        }
    }
}

void blackjack::testTreeWalkerSplit() {
    cout << "\n=== Test: TreeWalker with Split ===" << endl;

    // Setup rules using default and modify as needed
    BJRules rules = getDefaultRules();
    rules.dealer_checks_blackjack = false;
    rules.allow_insurance_vs_ace = false;

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(100);

    // Deal initial cards: player pair of 8s; dealer 6
    bj_round.takeCard(8);
    bj_round.takeCard(8);
    bj_round.takeCard(6);

    shoe.burnRankValue(8);
    shoe.burnRankValue(8);
    shoe.burnRankValue(6);

    cout << "Initial round state:" << endl;
    cout << bj_round.toString() << endl;

    auto root_node = buildNonFinalRootNode(
        bj_round,
        shoe,
        3,
        5,
        SimAlgo::COMBO
    );

    TreeWalker walker(root_node);

    cout << "\nInitial state:" << endl;
    if (!walker.finished()) {
        cout << walker.getStateInfo() << endl;
    }

    // Check if split is available
    if (!walker.finished() && walker.needPlayerAction()) {
        cout << "\nAvailable actions:" << endl;
        auto best_actions = walker.getBestActions();
        for (const auto& [action, estimate] : best_actions) {
            cout << "  " << to_string(action) << ": " 
                 << fixed << setprecision(6) << estimate.ev 
                 << " [" << estimate.ev_min << ", " << estimate.ev_max << "]" << endl;
        }

        // Check if split is available
        bool has_split = false;
        for (const auto& [action, value] : best_actions) {
            if (action == PlayerAction::SPLIT) {
                has_split = true;
                cout << "\nTaking SPLIT action..." << endl;
                walker.takePlayerAction(PlayerAction::SPLIT);
                
                // After split, we need to provide cards for both split hands
                if (!walker.finished() && walker.needCard()) {
                    cout << "Providing first card for split hands: 7" << endl;
                    walker.takeCard(7);
                    
                    if (!walker.finished() && walker.needCard()) {
                        cout << "Providing second card for split hands: 9" << endl;
                        walker.takeCard(9);
                    }
                }
                
                cout << "After SPLIT:" << endl;
                if (!walker.finished()) {
                    cout << walker.getStateInfo() << endl;
                } else {
                    cout << "  Round finished" << endl;
                }
                
                // On the first split hand, take HIT and then provide card 6
                bool first_split_hand_played = false;
                
                // Continue playing until the round is finished
                int iteration = 0;
                const int max_iterations = 50; // Safety limit
                while (!walker.finished() && iteration < max_iterations) {
                    iteration++;
                    cout << "\n--- Iteration " << iteration << " ---" << endl;
                    
                    if (walker.needCard()) {
                        // On first split hand, provide card 6 after HIT
                        if (!first_split_hand_played) {
                            cout << "Taking card for first split hand: 6" << endl;
                            walker.takeCard(6);
                            first_split_hand_played = true;
                        } else {
                            // Get best actions to see what card might be good
                            // For simplicity, just take a reasonable card
                            auto possible_cards = walker.getPossibleNextCardRanks();
                            if (!possible_cards.empty()) {
                                // Take a middle-value card (e.g., 7)
                                int card_to_take = 7;
                                if (find(possible_cards.begin(), possible_cards.end(), card_to_take) == possible_cards.end()) {
                                    card_to_take = possible_cards[possible_cards.size() / 2];
                                }
                                cout << "Taking card: " << card_to_take << endl;
                                walker.takeCard(card_to_take);
                            }
                        }
                        if (!walker.finished()) {
                            cout << walker.getStateInfo() << endl;
                        }
                    } else if (walker.needPlayerAction()) {
                        // On first split hand, take HIT
                        if (!first_split_hand_played) {
                            cout << "Taking HIT action on first split hand" << endl;
                            walker.takePlayerAction(PlayerAction::HIT);
                        } else {
                            auto best_actions = walker.getBestActions();
                            if (!best_actions.empty()) {
                                PlayerAction best_action = best_actions[0].first;
                                auto estimate = best_actions[0].second;
                                cout << "Taking best action: " << to_string(best_action) 
                                     << " (EV: " << fixed << setprecision(6) << estimate.ev 
                                     << " [" << estimate.ev_min << ", " << estimate.ev_max << "])" << endl;
                                walker.takePlayerAction(best_action);
                            }
                        }
                        if (!walker.finished()) {
                            cout << walker.getStateInfo() << endl;
                        }
                    } else if (walker.needDealerAction()) {
                        auto dealer_actions = walker.getAvailableDealerActions();
                        if (!dealer_actions.empty()) {
                            // For dealer check BJ, confirm no blackjack (since dealer has 6 showing)
                            DealerAction dealer_action = DealerAction::CONFIRM_NO_BLACKJACK;
                            cout << "Taking dealer action: " << to_string(dealer_action) << endl;
                            walker.takeDealerAction(dealer_action);
                            if (!walker.finished()) {
                                cout << walker.getStateInfo() << endl;
                            }
                        }
                    } else {
                        // Shouldn't happen, but break to avoid infinite loop
                        cout << "Unexpected state - neither card nor action needed" << endl;
                        break;
                    }
                }
                
                if (walker.finished()) {
                    cout << "\n=== Round Finished ===" << endl;
                } else if (iteration >= max_iterations) {
                    cout << "\n=== Reached max iterations ===" << endl;
                }
                
                break;
            }
        }

        if (!has_split) {
            cout << "\nSplit not available, taking best action instead" << endl;
            if (!best_actions.empty() && !walker.finished()) {
                walker.takePlayerAction(best_actions[0].first);
                if (!walker.finished()) {
                    cout << walker.getStateInfo() << endl;
                }
            }
        }
    }
}

// Helper function to setup AAvA test
TreeWalker setupAAvATest() {
    BJRules rules = getDefaultRules();
    rules.dealer_checks_blackjack = true;
    rules.allow_insurance_vs_ace = false;

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(100);

    // Deal initial cards: player pair of Aces; dealer Ace
    bj_round.takeCard(11);  // Ace
    bj_round.takeCard(11);  // Ace
    bj_round.takeCard(11);  // Dealer Ace

    shoe.burnRankValue(11);
    shoe.burnRankValue(11);
    shoe.burnRankValue(11);

    cout << "Initial round state:" << endl;
    cout << bj_round.toString() << endl;

    auto root_node = buildNonFinalRootNode(
        bj_round,
        shoe,
        0,
        9,
        SimAlgo::COMBO
    );

    auto start = chrono::high_resolution_clock::now();
    TreeWalker walker(root_node);
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
    cout << "\nTreeWalker constructor took: " << duration.count() << " ms" << endl;

    cout << "\nInitial state:" << endl;
    cout << walker.getStateInfo() << endl;

    // Dealer checks for blackjack
    cout << "\nDealer checking for blackjack..." << endl;
    walker.takeDealerAction(DealerAction::CONFIRM_NO_BLACKJACK);
    cout << walker.getStateInfo() << endl;

    // Split
    cout << "\nTaking SPLIT action..." << endl;
    walker.takePlayerAction(PlayerAction::SPLIT);

    return walker;
}

void blackjack::testTreeWalkerAAvA() {
    // Case 1: First gets 21, second doesn't
    {
        cout << "\n=== Test: AAvA - First hand 21, second not ===" << endl;
        TreeWalker walker = setupAAvATest();

        // Provide cards T (10) and 4 for split hands
        cout << "\nProviding first card for split hands: 10" << endl;
        walker.takeCard(10);
        cout << walker.getStateInfo() << endl;

        cout << "\nProviding second card for split hands: 4" << endl;
        walker.takeCard(4);
        cout << walker.getStateInfo() << endl;

        // First hand should autostand (Ace + 10 = 21)
        // Now on second hand (A+4=15), hit and stand
        cout << "\nHit on second hand, take card 4:" << endl;
        walker.takePlayerAction(PlayerAction::HIT);
        walker.takeCard(4);
        cout << walker.getStateInfo() << endl;

        cout << "\nStand on second hand:" << endl;
        walker.takePlayerAction(PlayerAction::STAND);
        cout << walker.getStateInfo() << endl;
    }

    // Case 2: Second gets 21, first doesn't
    {
        cout << "\n=== Test: AAvA - Second hand 21, first not ===" << endl;
        TreeWalker walker = setupAAvATest();

        // Provide cards 4 and T (10) for split hands
        cout << "\nProviding first card for split hands: 4" << endl;
        walker.takeCard(4);
        cout << walker.getStateInfo() << endl;

        cout << "\nProviding second card for split hands: 10" << endl;
        walker.takeCard(10);
        cout << walker.getStateInfo() << endl;

        // First hand (A+4=15), hit and stand
        cout << "\nHit on first hand, take card 4:" << endl;
        walker.takePlayerAction(PlayerAction::HIT);
        walker.takeCard(4);
        cout << walker.getStateInfo() << endl;

        cout << "\nStand on first hand:" << endl;
        walker.takePlayerAction(PlayerAction::STAND);
        cout << walker.getStateInfo() << endl;

        // Second hand should autostand (Ace + 10 = 21)
        // Walker should finish or be at end of round
        cout << "\nAfter first hand done, second hand autostood:" << endl;
        cout << walker.getStateInfo() << endl;
    }

    // Case 3: Both get 21
    {
        cout << "\n=== Test: AAvA - Both hands 21 ===" << endl;
        TreeWalker walker = setupAAvATest();

        // Provide cards T (10) and T (10) for split hands
        cout << "\nProviding first card for split hands: 10" << endl;
        walker.takeCard(10);
        cout << walker.getStateInfo() << endl;

        cout << "\nProviding second card for split hands: 10" << endl;
        walker.takeCard(10);
        cout << walker.getStateInfo() << endl;

        // Both hands should autostand (Ace + 10 = 21)
        // Walker should finish or be at end of round
        cout << "\nBoth hands autostood:" << endl;
        cout << walker.getStateInfo() << endl;
    }

    // Case 4: Neither gets 21
    {
        cout << "\n=== Test: AAvA - Neither hand 21 ===" << endl;
        TreeWalker walker = setupAAvATest();

        // Provide cards 4 and 5 for split hands
        cout << "\nProviding first card for split hands: 4" << endl;
        walker.takeCard(4);
        cout << walker.getStateInfo() << endl;

        cout << "\nProviding second card for split hands: 5" << endl;
        walker.takeCard(5);
        cout << walker.getStateInfo() << endl;

        // First hand (A+4=15), hit and stand
        cout << "\nHit on first hand, take card 4:" << endl;
        walker.takePlayerAction(PlayerAction::HIT);
        walker.takeCard(4);
        cout << walker.getStateInfo() << endl;

        cout << "\nStand on first hand:" << endl;
        walker.takePlayerAction(PlayerAction::STAND);
        cout << walker.getStateInfo() << endl;

        // Second hand (A+5=16), hit and stand
        cout << "\nHit on second hand, take card 3:" << endl;
        walker.takePlayerAction(PlayerAction::HIT);
        walker.takeCard(3);
        cout << walker.getStateInfo() << endl;

        cout << "\nStand on second hand:" << endl;
        walker.takePlayerAction(PlayerAction::STAND);
        cout << walker.getStateInfo() << endl;
    }
}

