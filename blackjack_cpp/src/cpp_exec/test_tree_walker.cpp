#include "tree_walker.h"
#include "floor_ceil_node.h"
#include "blackjack_round.h"
#include "shoe.h"
#include "actions.h"
#include <iostream>
#include <iomanip>
#include <chrono>

using namespace std;
using namespace blackjack;

void testBasicTreeWalker() {
    cout << "\n=== Test: Basic TreeWalker ===" << endl;

    // Setup rules
    BJRules rules;
    rules.dealer_checks_blackjack = true;
    rules.dealer_hits_soft_17 = false;
    rules.allow_late_surrender = false;
    rules.allow_early_surrender_on_ten = false;
    rules.allow_early_surrender_on_ace = false;
    rules.allow_early_surrender_on_all = false;
    rules.dealer_shows_card_on_surrender = false;
    rules.allow_insurance_vs_ace = true;
    rules.natural_blackjack_payout = 1.5;
    rules.surrender_payout = 0.5;
    rules.insurance_payout = 2.0;
    rules.max_splits_allowed = 1;
    rules.allow_action_on_split_aces = true;
    rules.allow_double_after_split = true;
    rules.allow_double_on_soft = true;
    rules.allow_split_different_tens = true;

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(10);

    // Deal initial cards: player 10, 6; dealer 6
    bj_round.takeCard(10);
    bj_round.takeCard(6);
    bj_round.takeCard(6);

    shoe.burnRankValue(10);
    shoe.burnRankValue(6);
    shoe.burnRankValue(6);

    cout << "Initial round state:" << endl;
    cout << bj_round.toString() << endl;

    // Create root node
    int max_hand_size_full_enum = 3;
    int dealer_sim_depth = 5;
    auto root_node = buildRootNode(
        bj_round,
        shoe,
        max_hand_size_full_enum,
        dealer_sim_depth,
        SimAlgo::RECURSIVE
    );

    // Create TreeWalker with target gap
    double target_gap = 0.1;
    TreeWalker walker(root_node, target_gap);

    cout << "\nInitial state:" << endl;
    cout << walker.getStateInfo() << endl;

    // Check if expecting player action
    if (walker.expectsPlayerAction()) {
        cout << "\nGetting best actions..." << endl;
        auto best_actions = walker.getBestActions();
        cout << "Actions (sorted from best to worst):" << endl;
        for (const auto& [action, value] : best_actions) {
            cout << "  " << to_string(action) << ": " 
                 << fixed << setprecision(6) << value << endl;
        }
    }

    // Build tree until gap is tight
    cout << "\nBuilding tree until gap is tight..." << endl;
    bool reached = walker.buildUntilGapTight(20);
    cout << "Gap target reached: " << (reached ? "yes" : "no") << endl;
    cout << walker.getStateInfo() << endl;

    // Try providing a player action
    if (walker.expectsPlayerAction()) {
        auto best_actions = walker.getBestActions();
        if (!best_actions.empty()) {
            PlayerAction best_action = best_actions[0].first;
            cout << "\nTaking best action: " << to_string(best_action) << endl;
            walker.providePlayerAction(best_action);
            cout << "After action:" << endl;
            cout << walker.getStateInfo() << endl;
        }
    }
}

void testTreeWalkerWithCards() {
    cout << "\n=== Test: TreeWalker with Cards ===" << endl;

    BJRules rules;
    rules.dealer_checks_blackjack = false;
    rules.dealer_hits_soft_17 = false;
    rules.allow_late_surrender = false;
    rules.allow_early_surrender_on_ten = false;
    rules.allow_early_surrender_on_ace = false;
    rules.allow_early_surrender_on_all = false;
    rules.dealer_shows_card_on_surrender = false;
    rules.allow_insurance_vs_ace = false;
    rules.natural_blackjack_payout = 1.5;
    rules.max_splits_allowed = 0;
    rules.allow_action_on_split_aces = false;
    rules.allow_double_after_split = false;
    rules.allow_double_on_soft = true;
    rules.allow_split_different_tens = false;

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(10);

    // Deal initial cards: player 10, 5; dealer 6
    bj_round.takeCard(10);
    bj_round.takeCard(5);
    bj_round.takeCard(6);

    shoe.burnRankValue(10);
    shoe.burnRankValue(5);
    shoe.burnRankValue(6);

    cout << "Initial round state:" << endl;
    cout << bj_round.toString() << endl;

    auto root_node = buildRootNode(
        bj_round,
        shoe,
        3,
        5,
        SimAlgo::RECURSIVE
    );

    TreeWalker walker(root_node);

    cout << "\nInitial state:" << endl;
    cout << walker.getStateInfo() << endl;

    // Player should hit
    if (walker.expectsPlayerAction()) {
        cout << "\nPlayer action decision:" << endl;
        auto best_actions = walker.getBestActions();
        for (const auto& [action, value] : best_actions) {
            cout << "  " << to_string(action) << ": " 
                 << fixed << setprecision(6) << value << endl;
        }

        // Take HIT action
        walker.providePlayerAction(PlayerAction::HIT);
        cout << "\nAfter HIT action:" << endl;
        cout << walker.getStateInfo() << endl;

        // Now expecting a card
        if (walker.expectsCard()) {
            cout << "\nProviding card: 6" << endl;
            walker.provideCard(6);
            cout << walker.getStateInfo() << endl;
        }
    }
}

void testTreeWalkerSplit() {
    cout << "\n=== Test: TreeWalker with Split ===" << endl;

    BJRules rules;
    rules.dealer_checks_blackjack = false;
    rules.dealer_hits_soft_17 = false;
    rules.allow_late_surrender = false;
    rules.allow_early_surrender_on_ten = false;
    rules.allow_early_surrender_on_ace = false;
    rules.allow_early_surrender_on_all = false;
    rules.dealer_shows_card_on_surrender = false;
    rules.allow_insurance_vs_ace = false;
    rules.natural_blackjack_payout = 1.5;
    rules.max_splits_allowed = 1;
    rules.allow_action_on_split_aces = true;
    rules.allow_double_after_split = true;
    rules.allow_double_on_soft = true;
    rules.allow_split_different_tens = true;

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(10);

    // Deal initial cards: player pair of 8s; dealer 6
    bj_round.takeCard(8);
    bj_round.takeCard(8);
    bj_round.takeCard(6);

    shoe.burnRankValue(8);
    shoe.burnRankValue(8);
    shoe.burnRankValue(6);

    cout << "Initial round state:" << endl;
    cout << bj_round.toString() << endl;

    auto root_node = buildRootNode(
        bj_round,
        shoe,
        3,
        5,
        SimAlgo::RECURSIVE
    );

    TreeWalker walker(root_node, 0.5); // Target gap of 0.5

    cout << "\nInitial state:" << endl;
    cout << walker.getStateInfo() << endl;

    // Check if split is available
    if (walker.expectsPlayerAction()) {
        cout << "\nAvailable actions:" << endl;
        auto best_actions = walker.getBestActions();
        for (const auto& [action, value] : best_actions) {
            cout << "  " << to_string(action) << ": " 
                 << fixed << setprecision(6) << value << endl;
        }

        // Try splitting
        bool has_split = false;
        for (const auto& [action, value] : best_actions) {
            if (action == PlayerAction::SPLIT) {
                has_split = true;
                cout << "\nTaking SPLIT action..." << endl;
                walker.providePlayerAction(PlayerAction::SPLIT);
                cout << "After SPLIT:" << endl;
                cout << walker.getStateInfo() << endl;
                break;
            }
        }

        if (!has_split) {
            cout << "\nSplit not available, taking best action instead" << endl;
            if (!best_actions.empty()) {
                walker.providePlayerAction(best_actions[0].first);
                cout << walker.getStateInfo() << endl;
            }
        }
    }
}

int main() {
    cout << "TreeWalker Test Suite" << endl;
    cout << "=====================" << endl;

    try {
        testBasicTreeWalker();
        testTreeWalkerWithCards();
        testTreeWalkerSplit();
        
        cout << "\n=== All tests completed ===" << endl;
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
