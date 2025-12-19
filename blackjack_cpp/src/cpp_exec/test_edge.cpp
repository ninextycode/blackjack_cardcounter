#include "cpp_exec/main.h"
#include "floor_ceil_node.h"
#include "tree_utils.h"
#include "shoe.h"
#include "rules.h"
#include "dealer_sim.h"
#include "edge.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <omp.h>

using namespace std;

namespace {

// Utility function to build tree and converge to target gap
void buildAndConverge(blackjack::AbstractFloorCeilNode& node, double gap_target) {
    node.buildTree();
    for (int depth = 0; depth < 100; ++depth) {
        auto [value_changed, is_final] = node.convertToFullUpToDepth(depth);
        if (is_final || node.getCeilValue() - node.getFloorValue() < gap_target) {
            break;
        }
    }
}

// Extract best action from a DealerCheckBJNode (ten upcard case)
optional<blackjack::PlayerAction> getBestActionFromDealerCheckNode(
    blackjack::DealerCheckBJNode& node
) {
    auto* no_bj_child = dynamic_cast<blackjack::DecisionNode*>(
        node.children_[node.dealer_no_bj_child_idx_].get()
    );
    if (no_bj_child) {
        return no_bj_child->getDecisionChoice();
    }
    return nullopt;
}

// Extract best action from a DecisionNode, following through insurance refusal if needed
optional<blackjack::PlayerAction> getBestActionFromDecisionNode(
    blackjack::DecisionNode& node
) {
    auto best_action = node.getDecisionChoice();
    
    // If insurance stage with REFUSE, get the next action
    if (node.bj_round_.getStage() == blackjack::BJStage::PLAYER_OFFERED_INSURANCE 
        && best_action == blackjack::PlayerAction::REFUSE_INSURANCE
        && !node.bj_round_.player_hands[0].is_natural_blackjack()) {
        // Follow the chain: DecisionNode -> DealerCheckBJNode -> DecisionNode
        auto* insurance_child = dynamic_cast<blackjack::DealerCheckBJNode*>(
            node.getDecisionChoiceChild()
        );
        if (insurance_child) {
            auto* grandchild = dynamic_cast<blackjack::DecisionNode*>(
                insurance_child->children_[insurance_child->dealer_no_bj_child_idx_].get()
            );
            if (grandchild) {
                best_action = grandchild->getDecisionChoice();
            }
        }
    }
    return best_action;
}

// Result struct for createRootNode
struct RootNodeResult {
    shared_ptr<blackjack::AbstractFloorCeilNode> node;
    bool is_terminal;  // true if player has natural blackjack (no tree needed)
    double terminal_ev;  // EV if terminal
};

// Create root node based on game state, returns shared_ptr (does NOT build or converge)
RootNodeResult createRootNode(
    const blackjack::BJRound& bj_round,
    const blackjack::ProbabilisticRankShoe& shoe,
    const blackjack::BJRules& rules,
    int bet_unit,
    int sim_depth,
    blackjack::SimAlgo algo
) {
    using namespace blackjack;
    
    RootNodeResult result;
    result.is_terminal = false;
    result.terminal_ev = 0;
    
    BJStage stage = bj_round.getStage();
    
    // Check for player natural blackjack with no further action
    if (stage == BJStage::DEALER_CARD 
        && bj_round.player_hands.size() == 1 
        && bj_round.player_hands[0].is_natural_blackjack()) {
        result.is_terminal = true;
        result.terminal_ev = bet_unit * rules.natural_blackjack_payout;
        result.node = nullptr;
        return result;
    }
    
    if (stage == BJStage::DEALER_CHECK_BJ) {
        // Dealer checks blackjack (ten up), insurance not offered
        auto node = make_shared<DealerCheckBJNode>(
            bj_round, shoe, 1, false, false, sim_depth, algo
        );
        result.node = node;
    } else {
        // Normal decision node (including insurance offers)
        auto node = make_shared<DecisionNode>(
            bj_round, shoe, 1, sim_depth, algo
        );
        result.node = node;
    }
    
    return result;
}

// Extract best action from root node after building/converging
optional<blackjack::PlayerAction> getBestActionFromRootNode(RootNodeResult& result) {
    if (result.is_terminal || !result.node) {
        return nullopt;
    }
    
    // Check if it's a DealerCheckBJNode
    auto* dealer_check_node = dynamic_cast<blackjack::DealerCheckBJNode*>(result.node.get());
    if (dealer_check_node) {
        return getBestActionFromDealerCheckNode(*dealer_check_node);
    }
    
    // Otherwise it's a DecisionNode
    auto* decision_node = dynamic_cast<blackjack::DecisionNode*>(result.node.get());
    if (decision_node) {
        return getBestActionFromDecisionNode(*decision_node);
    }
    
    return nullopt;
}

} // anonymous namespace


void blackjack::testEv() {
    // Setup rules matching Python notebook
    BJRules rules = getDefaultRules();

    auto rules_ptr = make_shared<const BJRules>(rules);

    // Setup AA v A (Ace, Ace vs Dealer Ace)
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(8, 42);  // 8 decks, seed 42
    bj_round.startRound(100);  // bet unit = 10

    // Player cards: A, A; Dealer upcard: A
    int player_card_0 = 11;  // Ace
    int player_card_1 = 11;  // Ace
    int dealer_upcard = 11;  // Ace

    // int player_card_0 = 5;  // 5
    // int player_card_1 = 5;  // 5
    // int dealer_upcard = 10;  // Ten

    bj_round.takeCard(player_card_0);
    bj_round.takeCard(player_card_1);
    bj_round.takeCard(dealer_upcard);

    shoe.burnRankValue(player_card_0);
    shoe.burnRankValue(player_card_1);
    shoe.burnRankValue(dealer_upcard);

    cout << "======================================" << endl;
    cout << "Testing EV " << endl;
    cout << "======================================" << endl;
    cout << bj_round.toString() << endl;

    // Test with different depths and algorithms
    cout << endl;
    cout << "=" << string(69, '=') << endl;
    cout << left << setw(8) << "Depth" 
         << setw(12) << "Algorithm" 
         << setw(14) << "Runtime (s)" 
         << setw(10) << "Value" 
         << setw(10) << "Floor" 
         << setw(10) << "Ceil" 
         << setw(10) << "Gap" << endl;
    cout << "=" << string(69, '=') << endl;

    vector<int> depth_values{2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    for (int depth : depth_values) {
        for (SimAlgo algo : {SimAlgo::COMBO, SimAlgo::RECURSIVE}) {
        // SimAlgo algo = SimAlgo::RECURSIVE;
            string algo_name = (algo == SimAlgo::COMBO) ? "combo" : "recursive";

            auto t_start = chrono::high_resolution_clock::now();

            // Create root node using common function
            RootNodeResult result = createRootNode(bj_round, shoe, rules, 100, depth, algo);
            
            if (!result.is_terminal && result.node) {
                buildAndConverge(*result.node, 0.01 * 100);
            }

            auto t_end = chrono::high_resolution_clock::now();
            double runtime = chrono::duration<double>(t_end - t_start).count();

            if (result.is_terminal) {
                cout << left << setw(8) << depth
                        << setw(12) << algo_name
                        << fixed << setprecision(3) << setw(14) << runtime
                        << setprecision(4) << setw(10) << result.terminal_ev
                        << setw(10) << result.terminal_ev
                        << setw(10) << 0.0 << endl;
                cout << "Terminal node (player natural blackjack)" << endl;
            } else {
                double value = result.node->getValue();
                double floor_val = result.node->getFloorValue();
                double ceil_val = result.node->getCeilValue();
                double gap = ceil_val - floor_val;

                cout << left << setw(8) << depth
                        << setw(12) << algo_name
                        << fixed << setprecision(3) << setw(14) << runtime
                        << setprecision(4) 
                        << setw(10) << value 
                        << setw(10) << floor_val
                        << setw(10) << ceil_val
                        << setw(10) << gap << endl;
                
                // Print tree with 2 levels of children
                // cout << "\nTree structure:" << endl;
                // printTree(result.node.get(), 3);
                // cout << endl;
            }
        }
        cout << "-" << string(69, '-') << endl;
    }
    cout << "=" << string(69, '=') << endl;
}


void blackjack::testEdge() {
    // Setup rules matching Python notebook
    BJRules rules;
    rules.dealer_checks_blackjack = true;
    rules.dealer_hits_soft_17 = false;
    rules.allow_late_surrender = false;
    rules.allow_early_surrender_on_ten = false;
    rules.allow_early_surrender_on_ace = false;
    rules.allow_early_surrender_on_all = false;
    rules.dealer_shows_card_on_surrender = false;
    rules.allow_insurance_vs_ace = true;
    rules.natural_blackjack_payout = 3.0 / 2.0;
    rules.surrender_payout = 1.0 / 2.0;
    rules.insurance_payout = 2.0 / 1.0;
    rules.max_splits_allowed = 1;
    rules.allow_action_on_split_aces = true;
    rules.allow_double_after_split = true;
    rules.allow_double_on_soft = true;
    rules.allow_split_different_tens = true;

    auto rules_ptr = make_shared<const BJRules>(rules);

    const int n_decks = 6;
    const int bet_unit = 100;
    const int sim_depth = 9;
    const double gap_target = 0.1;
    const SimAlgo algo = SimAlgo::COMBO;

    // Generate all starting hand combinations
    struct Task {
        int p0, p1, d;
        double prob;
    };
    vector<Task> tasks;

    RandomSampler::resetGlobalSeedGenerator(42);
    ProbabilisticRankShoe base_shoe(n_decks);
    
    for (int p0 = 2; p0 <= 11; ++p0) {
        for (int p1 = p0; p1 <= 11; ++p1) {
            for (int d = 2; d <= 11; ++d) {
                ProbabilisticRankShoe shoe = base_shoe;
                double prob = 1.0;
                
                // Calculate probability
                auto prob_map = shoe.getRankValueProbabilities();
                prob *= prob_map.at(p0);
                if (prob == 0) continue;
                shoe.burnRankValue(p0);
                
                prob_map = shoe.getRankValueProbabilities();
                prob *= prob_map.at(p1);
                if (prob == 0) continue;
                shoe.burnRankValue(p1);
                
                prob_map = shoe.getRankValueProbabilities();
                prob *= prob_map.at(d);
                if (prob == 0) continue;
                
                // Count multiplier (2 for non-pairs since order matters)
                int n_count = (p0 == p1) ? 1 : 2;
                prob *= n_count;
                
                tasks.push_back({p0, p1, d, prob});
            }
        }
    }

    // Results storage
    vector<double> evs(tasks.size());
    vector<double> ev_mins(tasks.size());
    vector<double> ev_maxs(tasks.size());
    vector<optional<PlayerAction>> best_actions(tasks.size());

    auto t_start = chrono::high_resolution_clock::now();

    #pragma omp parallel for schedule(dynamic) if(!omp_in_parallel())
    for (size_t i = 0; i < tasks.size(); ++i) {
        const Task& task = tasks[i];
        
        ProbabilisticRankShoe shoe(n_decks);
        BJRound bj_round(rules_ptr);
        bj_round.startRound(bet_unit);
        
        for (int c : {task.p0, task.p1, task.d}) {
            bj_round.takeCard(c);
            shoe.burnRankValue(c);
        }

        auto result = createRootNode(bj_round, shoe, rules, bet_unit, sim_depth, algo);
        
        if (!result.is_terminal && result.node) {
            buildAndConverge(*result.node, gap_target * bet_unit);
        }
        
        double ev, ev_min, ev_max;
        optional<PlayerAction> best_action;
        
        if (result.is_terminal) {
            ev = result.terminal_ev;
            ev_min = ev;
            ev_max = ev;
            best_action = nullopt;
        } else {
            ev = result.node->getValue();
            ev_min = result.node->getFloorValue();
            ev_max = result.node->getCeilValue();
            best_action = getBestActionFromRootNode(result);
        }
        
        evs[i] = ev;
        ev_mins[i] = ev_min;
        ev_maxs[i] = ev_max;
        best_actions[i] = best_action;
    }

    auto t_end = chrono::high_resolution_clock::now();
    double runtime = chrono::duration<double>(t_end - t_start).count();

    // Compute weighted average
    double total_ev = 0, total_ev_min = 0, total_ev_max = 0;
    for (size_t i = 0; i < tasks.size(); ++i) {
        total_ev += evs[i] * tasks[i].prob;
        total_ev_min += ev_mins[i] * tasks[i].prob;
        total_ev_max += ev_maxs[i] * tasks[i].prob;
    }

    // Helper to format card value
    auto cardStr = [](int v) -> string {
        if (v == 10) return "T";
        if (v == 11) return "A";
        return std::to_string(v);
    };

    // Helper to format action
    auto actionStr = [](optional<PlayerAction> a) -> string {
        if (!a.has_value()) return "-";
        return to_string(a.value());
    };

    // Print table header
    cout << fixed << setprecision(2);
    cout << left << setw(10) << "Hand" 
         << setw(6) << "Up" 
         << setw(12) << "EV" 
         << setw(12) << "EV_min" 
         << setw(12) << "EV_max" 
         << setw(12) << "Action" << endl;
    cout << string(64, '-') << endl;

    // Print each row
    for (size_t i = 0; i < tasks.size(); ++i) {
        const Task& t = tasks[i];
        string hand = cardStr(t.p0) + "," + cardStr(t.p1);
        cout << left << setw(10) << hand 
             << setw(6) << cardStr(t.d) 
             << setw(12) << evs[i] 
             << setw(12) << ev_mins[i] 
             << setw(12) << ev_maxs[i] 
             << setw(12) << actionStr(best_actions[i]) << endl;
    }

    cout << string(64, '-') << endl;
    cout << fixed << setprecision(3);
    cout << "ev = " << total_ev 
         << ", ev_min = " << total_ev_min 
         << ", ev_max = " << total_ev_max << endl;
    cout << "runtime = " << runtime << " s" << endl;
}


void blackjack::testDealerSim() {
    // Setup rules matching Python notebook
    BJRules rules;
    rules.dealer_checks_blackjack = true;
    rules.dealer_hits_soft_17 = false;
    rules.allow_late_surrender = false;
    rules.allow_early_surrender_on_ten = false;
    rules.allow_early_surrender_on_ace = false;
    rules.allow_early_surrender_on_all = false;
    rules.dealer_shows_card_on_surrender = false;
    rules.allow_insurance_vs_ace = true;
    rules.natural_blackjack_payout = 3.0 / 2.0;
    rules.surrender_payout = 1.0 / 2.0;
    rules.insurance_payout = 2.0 / 1.0;
    rules.max_splits_allowed = 1;
    rules.allow_action_on_split_aces = true;
    rules.allow_double_after_split = true;
    rules.allow_double_on_soft = true;
    rules.allow_split_different_tens = true;

    auto rules_ptr = make_shared<const BJRules>(rules);

    const int n_decks = 8;
    const int bet_unit = 100;
    
    // Setup 55 vs T (5, 5 vs Dealer Ten)
    BJRound bj_round(rules_ptr);
    ProbabilisticRankShoe shoe(n_decks, 42);  // 8 decks, seed 42
    bj_round.startRound(bet_unit);

    int player_card_0 = 5;  // 5
    int player_card_1 = 5;  // 5
    int dealer_upcard = 10;  // Ten

    bj_round.takeCard(player_card_0);
    bj_round.takeCard(player_card_1);
    bj_round.takeCard(dealer_upcard);

    shoe.burnRankValue(player_card_0);
    shoe.burnRankValue(player_card_1);
    shoe.burnRankValue(dealer_upcard);

    // Player stands with 10 - simulate dealer cards
    // We need to simulate dealer drawing cards when player has 10 vs dealer Ten up
    bj_round.takeAction(DealerAction::CONFIRM_NO_BLACKJACK);
    bj_round.takeAction(PlayerAction::STAND);
    
    cout << "======================================" << endl;
    cout << "Testing Dealer Simulation - 55 vs T (player stands with 10)" << endl;
    cout << "======================================" << endl;
    cout << bj_round.toString() << endl;
    cout << endl;

    // Test with different simulation depths
    cout << "=" << string(69, '=') << endl;
    cout << left << setw(12) << "Depth" 
         << setw(12) << "N_Sims" 
         << setw(14) << "Runtime (s)" 
         << setw(14) << "EV" << endl;
    cout << "=" << string(69, '=') << endl;

    vector<pair<int, int>> test_configs = {
        {3, 1},
        {4, 1},
        {5, 1},
        {6, 1},
        {7, 1},
        {8, 1},  // Full enumeration
    };

    for (const auto& [depth, n_sims] : test_configs) {
        auto t_start = chrono::high_resolution_clock::now();
        
        double ev = runDealerCardsSimulationRecursive(
            bj_round, 
            shoe, 
            n_sims, 
            depth
        );

        auto t_end = chrono::high_resolution_clock::now();
        double runtime = chrono::duration<double>(t_end - t_start).count();

        cout << left << setw(12) << depth
             << setw(12) << n_sims
             << fixed << setprecision(3) << setw(14) << runtime
             << setprecision(16) << setw(14) << ev << endl;
    }

    cout << "=" << string(69, '=') << endl;
}


void blackjack::testEdgeTiming() {
    BJRules rules = getDefaultRules();
    
    const int n_decks = 6;
    const int bet_unit = 100;
    const double gap_target = 0.001;
    
    ProbabilisticRankShoe shoe(n_decks);
    
    cout << "======================================" << endl;
    cout << "Testing Edge Timing with Default Rules" << endl;
    cout << "======================================" << endl;
    cout << "Decks: " << n_decks << ", Bet unit: " << bet_unit << ", Gap target: " << gap_target << endl;
    cout << endl;
    
    cout << "=" << string(80, '=') << endl;
    cout << left << setw(10) << "Depth" 
         << setw(12) << "Algorithm"
         << setw(14) << "Runtime (s)" 
         << setw(20) << "EV" 
         << setw(20) << "EV_min" 
         << setw(20) << "EV_max" << endl;
    cout << "=" << string(80, '=') << endl;

    vector<int> depths = {/*1, 2, 3, 4, 5, 6, 7,*/ 8, 9, 10/*, 11*/};
    
    for (int depth : depths) {
        for (SimAlgo algo : {/*SimAlgo::RECURSIVE,*/ SimAlgo::COMBO}) {
            string algo_name = (algo == SimAlgo::COMBO) ? "combo" : "recursive";
            auto t_start = chrono::high_resolution_clock::now();
            
            EdgeResult result = calculateEdge(
                shoe,
                rules,
                bet_unit,
                depth,
                gap_target,
                algo
            );
            
            auto t_end = chrono::high_resolution_clock::now();
            double runtime = chrono::duration<double>(t_end - t_start).count();
            
            cout << left << setw(10) << depth
                << setw(12) << algo_name
                << fixed << setprecision(3) << setw(14) << runtime
                << setprecision(6) << setw(20) << result.ev
                << setw(20) << result.ev_min
                << setw(20) << result.ev_max << endl;
        }
    }
    
    cout << "=" << string(80, '=') << endl;
}



void blackjack::testEdgeTimingWithGap() {
    BJRules rules = getDefaultRules();
    
    const int n_decks = 6;
    const int bet_unit = 100;
    
    mt19937_64 gen(random_device{}());
    RankCount random_shoe_data;
    for (int rank = 2; rank <= 11; ++rank) {
        int max_count = 4 * n_decks;
        if (rank == 10) {
            max_count = 16 * n_decks;  // 4 face cards * 4 suits * 6 decks
        }
        uniform_int_distribution<int> dist(1, max_count);
        random_shoe_data.at(rank) = dist(gen);
    }
    
    // RandomSampler::resetGlobalSeedGenerator(42);

    ProbabilisticRankShoe shoe(random_shoe_data, RandomSampler::createNextSampler());
    
    cout << "======================================" << endl;
    cout << "Testing Edge Timing with Different Gap Targets" << endl;
    cout << "======================================" << endl;
    cout << "Random shoe composition:" << endl;
    for (int rank = 2; rank <= 11; ++rank) {
        cout << "Rank " << (rank == 11 ? "A" : std::to_string(rank)) << ": " << random_shoe_data.at(rank) << endl;
    }
    cout << endl;
    
    vector<SimAlgo> algos = { /* SimAlgo::RECURSIVE, */ SimAlgo::COMBO};
    vector<double> gap_targets{0.1, 0.03, 0.01, 0.003, 0.001, 0.0003, 0.0001, 0.00003, 0.00001};
    vector<int> depths = {/*2, 3, 4, 5, 6, 7,*/ 8, 9, 10/*, 11*/};
    
    // First, compute the reference value using both algorithms with finest gap and highest depth
    int ref_depth = depths.back();
    double ref_gap = gap_targets.back();
    
    auto t_ref_recursive_start = chrono::high_resolution_clock::now();
    EdgeResult ref_recursive = calculateEdge(shoe, rules, bet_unit, ref_depth, ref_gap, SimAlgo::RECURSIVE);
    auto t_ref_recursive_end = chrono::high_resolution_clock::now();
    double ref_recursive_runtime = chrono::duration<double>(t_ref_recursive_end - t_ref_recursive_start).count();
    
    auto t_ref_combo_start = chrono::high_resolution_clock::now();
    EdgeResult ref_combo = calculateEdge(shoe, rules, bet_unit, ref_depth, ref_gap, SimAlgo::COMBO);
    auto t_ref_combo_end = chrono::high_resolution_clock::now();
    double ref_combo_runtime = chrono::duration<double>(t_ref_combo_end - t_ref_combo_start).count();
    
    double ref_value = (ref_recursive.ev + ref_combo.ev) / 2.0;
    
    cout << "Reference value (avg of RECURSIVE and COMBO at depth=" << ref_depth 
         << ", gap=" << ref_gap << "): " << fixed << setprecision(8) << ref_value << endl;
    cout << "  RECURSIVE: " << setprecision(8) << ref_recursive.ev << " (runtime: " << fixed << setprecision(3) << ref_recursive_runtime << " s)" << endl;
    cout << "  COMBO:     " << setprecision(8) << ref_combo.ev << " (runtime: " << fixed << setprecision(3) << ref_combo_runtime << " s)" << endl;
    cout << endl;
    
    // Iterate: algo -> depth -> gap_target
    for (SimAlgo algo : algos) {
        string algo_name = (algo == SimAlgo::COMBO) ? "COMBO" : "RECURSIVE";
        
        cout << "=" << string(120, '=') << endl;
        cout << "Algorithm: " << algo_name << endl;
        cout << "=" << string(120, '=') << endl;
        
        cout << left << setw(10) << "Depth" 
             << setw(14) << "Gap Target"
             << setw(14) << "Runtime (s)" 
             << setw(20) << "EV" 
             << setw(20) << "EV_min" 
             << setw(20) << "EV_max"
             << setw(20) << "Error" << endl;
        cout << "-" << string(120, '-') << endl;
        
        for (int depth : depths) {
            for (double gap_target : gap_targets) {
                auto t_start = chrono::high_resolution_clock::now();
                
                EdgeResult result = calculateEdge(
                    shoe,
                    rules,
                    bet_unit,
                    depth,
                    gap_target,
                    algo
                );
                
                auto t_end = chrono::high_resolution_clock::now();
                double runtime = chrono::duration<double>(t_end - t_start).count();
                
                double error = result.ev - ref_value;
                
                cout << left << setw(10) << depth
                     << setw(14) << scientific << setprecision(1) << gap_target
                     << fixed << setprecision(3) << setw(14) << runtime
                     << setprecision(6) << setw(20) << result.ev
                     << setw(20) << result.ev_min
                     << setw(20) << result.ev_max
                     << setprecision(8) << setw(20) << error << endl;
            }
            cout << "-" << string(120, '-') << endl;
        }
        cout << endl;
    }
}