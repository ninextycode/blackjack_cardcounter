#include "edge.h"
#include "blackjack_round.h"
#include <vector>
#include <variant>
#include <omp.h>

using namespace std;

namespace blackjack {

// Result type: either terminal EV (double) or a node to evaluate
using RootNodeResult = variant<double, shared_ptr<AbstractFloorCeilNode>>;

// Utility function to build tree and converge to target gap
void buildAndConverge(AbstractFloorCeilNode& node, double gap_target_absolute) {
    node.buildTree();
    for (int depth = 0; depth < 100; ++depth) {
        auto [value_changed, is_final] = node.convertToFullUpToDepth(make_optional(depth));
        if (is_final || node.getCeilValue() - node.getFloorValue() < gap_target_absolute) {
            break;
        }
    }
}

// Create root node based on game state, returns shared_ptr (does NOT build or converge)
// Returns double for terminal states, node otherwise
RootNodeResult createRootNode(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int sim_depth,
    SimAlgo algo
) {
    BJStage stage = bj_round.getStage();
    
    // Check for player natural blackjack with no further action
    if (stage == BJStage::DEALER_CARD 
        && bj_round.player_hands.size() == 1 
        && bj_round.player_hands[0].is_natural_blackjack()) {
        return bj_round.bet_unit * bj_round.rules_->natural_blackjack_payout;
    }

    int n_dealer_sim_runs = 1;  // Default number of dealer sim runs
    
    if (stage == BJStage::DEALER_CHECK_BJ) {
        // Dealer checks blackjack (ten up), insurance not offered
        return make_shared<DealerCheckBJNode>(
            bj_round, shoe, n_dealer_sim_runs, false, false, sim_depth, algo
        );
    } else {
        // Normal decision node (including insurance offers)
        return make_shared<DecisionNode>(
            bj_round, shoe, n_dealer_sim_runs, sim_depth, algo
        );
    }
}


ValueResult calculateEdge(
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
                auto prob_map = shoe_copy.getRankValueProbabilities();
                prob *= prob_map.at(p0);
                if (prob == 0) continue;
                shoe_copy.burnRankValue(p0);
                
                prob_map = shoe_copy.getRankValueProbabilities();
                prob *= prob_map.at(p1);
                if (prob == 0) continue;
                shoe_copy.burnRankValue(p1);
                
                prob_map = shoe_copy.getRankValueProbabilities();
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

        auto result = createRootNode(bj_round, task_shoe, sim_depth, algo);
        
        if (holds_alternative<shared_ptr<AbstractFloorCeilNode>>(result)) {
            auto& node = get<shared_ptr<AbstractFloorCeilNode>>(result);
            node->buildTree();
            node->convertToFullUpToGap(gap_target * bet_unit);
            evs[i] = node->getValue();
            ev_mins[i] = node->getFloorValue();
            ev_maxs[i] = node->getCeilValue();
        } else {
            evs[i] = get<double>(result);
            ev_mins[i] = evs[i];
            ev_maxs[i] = evs[i];
        }
    }

    // Compute weighted average
    double total_ev = 0, total_ev_min = 0, total_ev_max = 0;
    for (size_t i = 0; i < tasks.size(); ++i) {
        total_ev += evs[i] * tasks[i].prob;
        total_ev_min += ev_mins[i] * tasks[i].prob;
        total_ev_max += ev_maxs[i] * tasks[i].prob;
    }

    ValueResult edge_result;
    edge_result.ev = total_ev;
    edge_result.ev_min = total_ev_min;
    edge_result.ev_max = total_ev_max;
    
    return edge_result;
}



EdgeCalculator::EdgeCalculator(
    const BJRules& rules,
    int bet_unit
) : rules_(make_shared<const BJRules>(rules)),
    bet_unit_(bet_unit)
{}


void EdgeCalculator::calculateEdge(
    const ProbabilisticRankShoe& shoe,
    int sim_depth,
    double gap_target,
    SimAlgo algo,
    bool parallel
) {    
    tasks_.clear();
    hand_results_.clear();
    hand_index_.clear();

    for (int p0 = 2; p0 <= 11; ++p0) {
        for (int p1 = p0; p1 <= 11; ++p1) {
            for (int d = 2; d <= 11; ++d) {
                ProbabilisticRankShoe shoe_copy = shoe;
                double prob = 1.0;
                
                // Calculate probability
                auto prob_map = shoe_copy.getRankValueProbabilities();
                prob *= prob_map.at(p0);
                if (prob == 0) continue;
                shoe_copy.burnRankValue(p0);
                
                prob_map = shoe_copy.getRankValueProbabilities();
                prob *= prob_map.at(p1);
                if (prob == 0) continue;
                shoe_copy.burnRankValue(p1);
                
                prob_map = shoe_copy.getRankValueProbabilities();
                prob *= prob_map.at(d);
                if (prob == 0) continue;
                
                // Count multiplier (2 for non-pairs since order matters)
                int n_count = (p0 == p1) ? 1 : 2;
                prob *= n_count;
                
                tasks_.push_back({p0, p1, d, prob});
            }
        }
    }

    // Results storage
    vector<double> evs(tasks_.size());
    vector<double> ev_mins(tasks_.size());
    vector<double> ev_maxs(tasks_.size());
    hand_results_.resize(tasks_.size());

    for (size_t i = 0; i < tasks_.size(); ++i) {
        const Task& task = tasks_[i];
        hand_index_.emplace(make_tuple(task.p0, task.p1, task.d), i);
    }

    #pragma omp parallel for schedule(dynamic) if(parallel && !omp_in_parallel())
    for (size_t i = 0; i < tasks_.size(); ++i) {
        const Task& task = tasks_[i];
        
        ProbabilisticRankShoe task_shoe = shoe;
        BJRound bj_round(rules_);
        bj_round.startRound(bet_unit_);
        
        int p0 = task.p0;
        int p1 = task.p1;
        int d = task.d;

        for (int c : {p0, p1, d}) {
            bj_round.takeCard(c);
            task_shoe.burnRankValue(c);
        }

        auto result = createRootNode(bj_round, task_shoe, sim_depth, algo);
        
        double ev, ev_min, ev_max;
        
        if (holds_alternative<double>(result)) {
            ev = get<double>(result);
            ev_min = ev;
            ev_max = ev;
            hand_results_[i] = ev;
        } else {
            auto& node = get<shared_ptr<AbstractFloorCeilNode>>(result);
            node->buildTree();
            node->convertToFullUpToGap(gap_target * bet_unit_);
            ev = node->getValue();
            ev_min = node->getFloorValue();
            ev_max = node->getCeilValue();
            hand_results_[i] = node;
        }
        
        evs[i] = ev;
        ev_mins[i] = ev_min;
        ev_maxs[i] = ev_max;
    }

    // Compute weighted average
    double total_ev = 0, total_ev_min = 0, total_ev_max = 0;
    for (size_t i = 0; i < tasks_.size(); ++i) {
        total_ev += evs[i] * tasks_[i].prob;
        total_ev_min += ev_mins[i] * tasks_[i].prob;
        total_ev_max += ev_maxs[i] * tasks_[i].prob;
    }

    // Compute weighted average
    edge_result_.ev = total_ev;
    edge_result_.ev_min = total_ev_min;
    edge_result_.ev_max = total_ev_max;

    has_edge_result_ = true;
}



void EdgeCalculator::tightenValueEstimateGap(
    double relative_gap,
    bool parallel
) {
    if (!has_edge_result_) {
        throw runtime_error("Edge result not calculated - call calculateEdge() first");
    }

    #pragma omp parallel for schedule(dynamic) if(parallel && !omp_in_parallel())
    for (size_t i = 0; i < hand_results_.size(); ++i) {
        auto& result = hand_results_[i];
        if (holds_alternative<shared_ptr<AbstractFloorCeilNode>>(result)) {
            auto& node = get<shared_ptr<AbstractFloorCeilNode>>(result);
            node->convertToFullUpToGap(relative_gap * bet_unit_);
        }
    }

    double total_ev = 0, total_ev_min = 0, total_ev_max = 0;
    for (size_t i = 0; i < hand_results_.size(); ++i) {
        double ev = 0;
        double ev_min = 0;
        double ev_max = 0;
        auto& result = hand_results_[i];
        if (holds_alternative<double>(result)) {
            ev = get<double>(result);
            ev_min = ev;
            ev_max = ev;
        } else {
            auto& node = get<shared_ptr<AbstractFloorCeilNode>>(result);
            ev = node->getValue();
            ev_min = node->getFloorValue();
            ev_max = node->getCeilValue();
        }

        total_ev += ev * tasks_[i].prob;
        total_ev_min += ev_min * tasks_[i].prob;
        total_ev_max += ev_max * tasks_[i].prob;
    }

    edge_result_.ev = total_ev;
    edge_result_.ev_min = total_ev_min;
    edge_result_.ev_max = total_ev_max;
}

TreeWalker EdgeCalculator::createTreeWalker(
    pair<int, int> player_cards,
    int dealer_card
) {
    TreeKey key = makeKey(player_cards, dealer_card);
    
    auto it = hand_index_.find(key);
    if (it == hand_index_.end()) {
        throw runtime_error("No cached tree for this hand - call calculateEdge() first");
    }

    size_t idx = it->second;
    auto& result = hand_results_[idx];
    if (!holds_alternative<shared_ptr<AbstractFloorCeilNode>>(result)) {
        throw runtime_error("No cached tree for this hand - terminal result");
    }

    // copy the node to a new shared_ptr
    auto node_ptr = get<shared_ptr<AbstractFloorCeilNode>>(result);
    return TreeWalker(node_ptr);
}


bool EdgeCalculator::canCreateTreeWalker(
    pair<int, int> player_cards,
    int dealer_card
) const {
    TreeKey key = makeKey(player_cards, dealer_card);
    auto it = hand_index_.find(key);
    if (it == hand_index_.end()) {
        return false;
    }
    
    auto result = hand_results_[it->second];
    if (holds_alternative<double>(result)) {
        return false;
    }
    return true;
}

bool EdgeCalculator::hasCachedTree(
    pair<int, int> player_cards,
    int dealer_card
) const {
    TreeKey key = makeKey(player_cards, dealer_card);
    auto it = hand_index_.find(key);
    if (it == hand_index_.end()) {
        return false;
    }

    const auto& result = hand_results_[it->second];
    if (!holds_alternative<shared_ptr<AbstractFloorCeilNode>>(result)) {
        return false;
    }
    const auto& node = get<shared_ptr<AbstractFloorCeilNode>>(result);
    return node != nullptr;
}

ValueResult EdgeCalculator::getEdgeResult() const {
    if (!has_edge_result_) {
        throw runtime_error("Edge result not calculated - call calculate() first");
    }
    return edge_result_;
}

bool EdgeCalculator::hasEdgeResult() const {
    return has_edge_result_;
}

} // namespace blackjack
