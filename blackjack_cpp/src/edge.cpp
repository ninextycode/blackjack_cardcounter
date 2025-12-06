#include "edge.h"
#include "blackjack_round.h"
#include <vector>
#include <omp.h>

using namespace std;

namespace blackjack {

namespace {

// Utility function to build tree and converge to target gap
void buildAndConverge(FloorCeilNode& node, double gap_target_absolute) {
    node.buildTree();
    for (int depth = 0; depth < 100; ++depth) {
        node.convertToFullUpToDepth(depth);
        if (node.getCeilValue() - node.getFloorValue() < gap_target_absolute) {
            break;
        }
    }
}

// Result struct for createRootNode
struct RootNodeResult {
    shared_ptr<FloorCeilNode> node;
    bool is_terminal;  // true if player has natural blackjack (no tree needed)
    double terminal_ev;  // EV if terminal
};

// Create root node based on game state, returns shared_ptr (does NOT build or converge)
RootNodeResult createRootNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    const BJRules& rules,
    int bet_unit,
    int sim_depth,
    SimAlgo algo
) {
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

} // anonymous namespace



EdgeResult calculateEdge(
    const ProbabilisticRankShoe& shoe,
    const BJRules& rules,
    int bet_unit,
    int sim_depth,
    double gap_target,
    SimAlgo algo
) {
    auto rules_ptr = make_shared<const BJRules>(rules);
    
    // Generate all starting hand combinations
    struct Task {
        int p0, p1, d;
        double prob;
    };
    vector<Task> tasks;
    
    for (int p0 = 2; p0 <= 11; ++p0) {
        for (int p1 = p0; p1 <= 11; ++p1) {
            for (int d = 2; d <= 11; ++d) {
                ProbabilisticRankShoe shoe_copy = shoe;
                double prob = 1.0;
                
                // Calculate probability
                auto prob_map = shoe_copy.get_rank_value_probabilities();
                prob *= prob_map.at(p0);
                if (prob == 0) continue;
                shoe_copy.burnRankValue(p0);
                
                prob_map = shoe_copy.get_rank_value_probabilities();
                prob *= prob_map.at(p1);
                if (prob == 0) continue;
                shoe_copy.burnRankValue(p1);
                
                prob_map = shoe_copy.get_rank_value_probabilities();
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

    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < tasks.size(); ++i) {
        const Task& task = tasks[i];
        
        ProbabilisticRankShoe task_shoe = shoe;
        BJRound bj_round(rules_ptr);
        bj_round.startRound(bet_unit);
        
        for (int c : {task.p0, task.p1, task.d}) {
            bj_round.takeCard(c);
            task_shoe.burnRankValue(c);
        }

        auto result = createRootNode(bj_round, task_shoe, rules, bet_unit, sim_depth, algo);
        
        if (!result.is_terminal && result.node) {
            buildAndConverge(*result.node, gap_target * bet_unit);
        }
        
        double ev, ev_min, ev_max;
        
        if (result.is_terminal) {
            ev = result.terminal_ev;
            ev_min = ev;
            ev_max = ev;
        } else {
            ev = result.node->getValue();
            ev_min = result.node->getFloorValue();
            ev_max = result.node->getCeilValue();
        }
        
        evs[i] = ev;
        ev_mins[i] = ev_min;
        ev_maxs[i] = ev_max;
    }

    // Compute weighted average
    double total_ev = 0, total_ev_min = 0, total_ev_max = 0;
    for (size_t i = 0; i < tasks.size(); ++i) {
        total_ev += evs[i] * tasks[i].prob;
        total_ev_min += ev_mins[i] * tasks[i].prob;
        total_ev_max += ev_maxs[i] * tasks[i].prob;
    }

    EdgeResult edge_result;
    edge_result.ev = total_ev;
    edge_result.ev_min = total_ev_min;
    edge_result.ev_max = total_ev_max;
    
    return edge_result;
}

} // namespace blackjack
