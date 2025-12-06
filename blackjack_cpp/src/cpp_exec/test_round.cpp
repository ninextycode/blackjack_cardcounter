#include "cpp_exec/main.h"
#include "blackjack_round.h"
#include "mixed_node.h"
#include "shoe.h"
#include "tree_utils.h"
#include "random_sampler.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>     
#include <filesystem>  

using namespace std;
using namespace blackjack;


// Helper function to convert TransitionEvent to string
string blackjack::eventToString(const TransitionEvent& event) {
    if (holds_alternative<int>(event)) {
        int card = get<int>(event);
        return "Card:" + std::to_string(card);
    } else if (holds_alternative<PlayerAction>(event)) {
        PlayerAction action = get<PlayerAction>(event);
        switch (action) {
            case PlayerAction::STAND: return "STAND";
            case PlayerAction::HIT: return "HIT";
            case PlayerAction::DOUBLE: return "DOUBLE";
            case PlayerAction::SPLIT: return "SPLIT";
            case PlayerAction::SURRENDER: return "SURRENDER";
            case PlayerAction::TAKE_INSURANCE: return "TAKE_INSURANCE";
            case PlayerAction::REFUSE_INSURANCE: return "REFUSE_INSURANCE";
            case PlayerAction::DECLINE_EARLY_SURRENDER: return "DECLINE_EARLY_SURRENDER";
            default: return "UNKNOWN_ACTION";
        }
    } else if (holds_alternative<DealerAction>(event)) {
        DealerAction action = get<DealerAction>(event);
        switch (action) {
            case DealerAction::CONFIRM_BLACKJACK: return "DEALER_BJ";
            case DealerAction::CONFIRM_NO_BLACKJACK: return "DEALER_NO_BJ";
            default: return "UNKNOWN_DEALER_ACTION";
        }
    }
    return "UNKNOWN";
}

// Helper function to print tree structure recursively
void blackjack::printTreeStructure(
    AbstractBJTreeNode* node,
    int max_depth,
    int current_depth,
    const string& prefix,
    bool is_last
) {
    if (!node) return;
    if (max_depth >= 0 && current_depth > max_depth) return;
    
    // Print current node
    cout << prefix;
    cout << (is_last ? "└── " : "├── ");
    
    // Print node info
    cout << "Depth:" << node->depth_from_root_ 
         << " Value:" << fixed << setprecision(4) << node->value_
         << " Children:" << node->children_.size();
    
    if (node->has_completed_tree_) {
        cout << " [COMPLETE]";
    }
    
    cout << endl;
    
    // Don't recurse if we've reached max depth
    if (max_depth >= 0 && current_depth >= max_depth) {
        if (!node->children_.empty()) {
            cout << prefix << (is_last ? "    " : "│   ") << "... (" << node->children_.size() << " children not shown)" << endl;
        }
        return;
    }
    
    // Print children
    for (size_t i = 0; i < node->children_.size(); ++i) {
        bool child_is_last = (i == node->children_.size() - 1);
        string child_prefix = prefix + (is_last ? "    " : "│   ");
        
        // Print edge info (event and probability)
        cout << child_prefix;
        cout << (child_is_last ? "└── " : "├── ");
        cout << "[" << eventToString(node->children_events_[i]) 
             << " p=" << fixed << setprecision(4) << node->children_prob_[i] << "]" << endl;
        
        // Recursively print child
        printTreeStructure(node->children_[i].get(), max_depth, current_depth + 1, child_prefix, child_is_last);
    }
}

// Print tree statistics
void blackjack::printTreeStatistics(AbstractBJTreeNode* root) {
    int total_nodes = 0;
    int terminal_nodes = 0;
    int max_depth = 0;
    
    // BFS traversal to count nodes
    NodeIterator iter(root);
    while (iter.hasNext()) {
        auto node_level = iter.next();
        total_nodes++;
        max_depth = max(max_depth, node_level.level);
        if (node_level.node->children_.empty()) {
            terminal_nodes++;
        }
    }
    
    cout << "\n=== Tree Statistics ===" << endl;
    cout << "Total nodes: " << total_nodes << endl;
    cout << "Terminal nodes: " << terminal_nodes << endl;
    cout << "Max depth: " << max_depth << endl;
    cout << "Root value (EV): " << fixed << setprecision(6) << root->getValue() << endl;
    cout << "Tree completed: " << (root->treeCompleted() ? "Yes" : "No") << endl;
}



void blackjack::testAcePair() {

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
    rules.no_natural_bj_on_split = true;

    RandomSampler::resetGlobalSeedGenerator(42);

    auto rules_ptr = make_shared<const BJRules>(rules);
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8);
    bj_round.startRound(10);

    bj_round.takeCard(11);
    bj_round.takeCard(11);
    bj_round.takeCard(6);

    shoe.burnRankValue(11);
    shoe.burnRankValue(11);
    shoe.burnRankValue(6);

    int max_hand_size_full_enum = 3;
    int player_card_initial_samples = 1;
    MixedNode root_node(bj_round, shoe, max_hand_size_full_enum, player_card_initial_samples);

    cout << "Blackjack round initialized." << endl;
    cout << bj_round.toString() << endl;
    cout << "Building game tree..." << endl << endl;

    auto t0_total = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 50; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        root_node.buildTreeLayer(i);
        auto t1 = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration_cast<std::chrono::milliseconds>(
            t1 - t0
        ).count();
        cout << "Depth " << i << " built in " << dt / 1000 << " s" << endl;
        
        if (root_node.treeCompleted()) {
            cout << "\nTree building completed at depth " << i << endl;
            break;
        }
    }
    auto t1_total = std::chrono::high_resolution_clock::now();
    double dt_total = std::chrono::duration_cast<std::chrono::milliseconds>(
        t1_total - t0_total
    ).count();
    cout << "\nTotal tree building time: " << dt_total / 1000 << " s" << endl;
    
    // Print final statistics
    printTreeStatistics(&root_node);

    // Log tree structure by levels (from tree_utils)
    logTreeStructure(&root_node);

    // for (int i = 0; i < 50; ++i) {
    //     cout << endl;
        
    //     auto t0 = std::chrono::high_resolution_clock::now();
    //     root_node.convertToFullUpToDepth(i);
    //     auto t1 = std::chrono::high_resolution_clock::now();
    //     double dt = std::chrono::duration_cast<std::chrono::milliseconds>(
    //         t1 - t0
    //     ).count();
    //     cout << "Depth " << i << " full enumeration in " << dt / 1000 << " s" << endl;
    //     cout << "Current root value (EV): " << fixed << setprecision(6) << root_node.getValue() << endl;

    //     logTreeStructure(&root_node);
    // }
}
