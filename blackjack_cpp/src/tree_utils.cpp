#include "tree_utils.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <variant>
#include <functional>

using namespace std;

namespace blackjack {

// Get all nodes organized by level
map<int, vector<AbstractBJTreeNode*>> getNodesByLevels(AbstractBJTreeNode* root) {
    map<int, vector<AbstractBJTreeNode*>> nodes_by_level;
    
    if (!root) return nodes_by_level;
    
    NodeIterator iter(root);
    while (iter.hasNext()) {
        NodeLevel node_level = iter.next();
        nodes_by_level[node_level.level].push_back(node_level.node);
    }
    
    return nodes_by_level;
}

// Count stages in a list of nodes
map<string, int> countStages(const vector<AbstractBJTreeNode*>& nodes) {
    map<string, int> stage_counter;
    
    for (AbstractBJTreeNode* node : nodes) {
        BJStage stage;
        
        // Check if it's a SimulationResultNode
        SimulationResultNode* sim_node = dynamic_cast<SimulationResultNode*>(node);
        if (sim_node) {
            stage = BJStage::ROUND_OVER;
        } else {
            stage = node->bj_round_.getStage();
        }
        
        string stage_name;
        
        if (stage == BJStage::PLAYER_CARD) {
            stage_name = "PLAYER_CARD";
            size_t num_children = node->children_.size();
            if (num_children == 1) {
                stage_name += "_SINGLE";
            } else if (num_children < 10) {
                stage_name += "_PARTIAL";
            } else {
                stage_name += "_FULL";
            }
        } else {
            // Convert BJStage enum to string
            switch (stage) {
                case BJStage::NOT_STARTED: stage_name = "NOT_STARTED"; break;
                case BJStage::PLAYER_CARD: stage_name = "PLAYER_CARD"; break;
                case BJStage::DEALER_CARD: stage_name = "DEALER_CARD"; break;
                case BJStage::PLAYER_ACTION: stage_name = "PLAYER_ACTION"; break;
                case BJStage::PLAYER_OFFERED_INSURANCE: stage_name = "PLAYER_OFFERED_INSURANCE"; break;
                case BJStage::PLAYER_OFFERED_EARLY_SURRENDER: stage_name = "PLAYER_OFFERED_EARLY_SURRENDER"; break;
                case BJStage::DEALER_CHECK_BJ: stage_name = "DEALER_CHECK_BJ"; break;
                case BJStage::ROUND_OVER: stage_name = "ROUND_OVER"; break;
                default: stage_name = "UNKNOWN"; break;
            }
        }
        
        stage_counter[stage_name]++;
    }
    
    return stage_counter;
}

// Log tree structure with stage counts per level
void logTreeStructure(AbstractBJTreeNode* root) {
    if (!root) {
        cout << "Tree is empty" << endl;
        return;
    }
    
    auto nodes_by_level = getNodesByLevels(root);
    
    cout << "\n=== Tree Structure by Level ===" << endl;
    
    for (const auto& [level, nodes] : nodes_by_level) {
        auto stage_counts = countStages(nodes);
        
        cout << "Level " << setw(2) << level << ": " 
             << setw(5) << nodes.size() << " nodes  ";
        
        // Print stage counts
        bool first = true;
        for (const auto& [stage_name, count] : stage_counts) {
            if (!first) cout << ", ";
            cout << stage_name << ":" << count;
            first = false;
        }
        cout << endl;
    }
    
    cout << endl;
}

// Format card rank value as string (T for 10, A for 11)
string formatCardValue(int value) {
    if (value == 10) return "T";
    if (value == 11) return "A";
    return std::to_string(value);
}

// Format a TransitionEvent as string
string formatEvent(const TransitionEvent& event) {
    if (holds_alternative<int>(event)) {
        return formatCardValue(get<int>(event));
    } else if (holds_alternative<PlayerAction>(event)) {
        return to_string(get<PlayerAction>(event));
    } else if (holds_alternative<DealerAction>(event)) {
        return to_string(get<DealerAction>(event));
    }
    return "UNKNOWN";
}

// Helper to get node type name
static string getNodeTypeName(AbstractBJTreeNode* node) {
    // Check specific types
    if (dynamic_cast<SimulationResultNode*>(node)) return "SimResult";
    if (dynamic_cast<FloorCeilValueNode*>(node)) return "FloorCeilValue";
    if (dynamic_cast<DecisionNode*>(node)) return "Decision";
    if (dynamic_cast<DealerCheckBJNode*>(node)) return "DealerCheckBJ";
    if (dynamic_cast<SplitNode*>(node)) return "Split";
    if (dynamic_cast<HitNode*>(node)) return "Hit";
    if (dynamic_cast<DoubleNode*>(node)) return "Double";
    if (dynamic_cast<AbstractFloorCeilNode*>(node)) return "FloorCeil";
    if (dynamic_cast<ValueNode*>(node)) return "FloorCeil";
    return "Unknown";
}

// Helper to get stage string
static string getStageString(AbstractBJTreeNode* node) {
    SimulationResultNode* sim_node = dynamic_cast<SimulationResultNode*>(node);
    if (sim_node) return "Terminal";
    
    FloorCeilValueNode* fc_val_node = dynamic_cast<FloorCeilValueNode*>(node);
    if (fc_val_node) return "FloorCeilPlaceholder";
    
    BJStage stage = node->bj_round_.getStage();
    switch (stage) {
        case BJStage::NOT_STARTED: return "NOT_STARTED";
        case BJStage::PLAYER_CARD: return "PLAYER_CARD";
        case BJStage::DEALER_CARD: return "DEALER_CARD";
        case BJStage::PLAYER_ACTION: return "PLAYER_ACTION";
        case BJStage::PLAYER_OFFERED_INSURANCE: return "PLAYER_OFFERED_INSURANCE";
        case BJStage::PLAYER_OFFERED_EARLY_SURRENDER: return "PLAYER_OFFERED_EARLY_SURRENDER";
        case BJStage::DEALER_CHECK_BJ: return "DEALER_CHECK_BJ";
        case BJStage::ROUND_OVER: return "ROUND_OVER";
        default: return "UNKNOWN";
    }
}

// Helper to format numeric value
static string formatValue(double val, bool has_value) {
    if (!has_value) return "N/A";
    ostringstream oss;
    oss << showpos << fixed << setprecision(3) << val;
    return oss.str();
}

// Get best action index for DecisionNode
static int getBestActionIdx(AbstractBJTreeNode* node) {
    DecisionNode* decision = dynamic_cast<DecisionNode*>(node);
    if (!decision) return -1;
    if (!decision->has_built_children_) return -1;
    
    for (size_t i = 0; i < decision->children_prob_.size(); ++i) {
        if (decision->children_prob_[i] == 1.0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Format a tree node line for display
string formatNodeLine(
    AbstractBJTreeNode* node,
    const TransitionEvent* event,
    bool is_best,
    double prob,
    int level,
    int indent_size
) {
    ostringstream oss;
    
    // Indent
    oss << string(indent_size * level, ' ');
    
    // Best marker
    oss << (is_best ? "* " : "  ");
    
    // Event
    if (event) {
        oss << "[" << formatEvent(*event) << "] ";
    }
    
    // Node type and stage
    string node_type = getNodeTypeName(node);
    string stage_str = getStageString(node);
    oss << node_type;  // << "(" << stage_str << ")";
    
    // Probability
    if (prob > 0.0) {
        oss << " p=" << fixed << setprecision(4) << prob;
    }
    
    // Values
    AbstractFloorCeilNode* fc_node = dynamic_cast<AbstractFloorCeilNode*>(node);
    ValueNode* sim_node = dynamic_cast<ValueNode*>(node);
    
    double value = 0.0;
    bool has_value = false;
    double floor_val = 0.0;
    double ceil_val = 0.0;
    bool has_floor_ceil = false;
    
    if (sim_node) {
        value = sim_node->getValue();
        has_value = true;
    } else if (fc_node) {
        try {
            value = fc_node->getValue();
            has_value = true;
            floor_val = fc_node->getFloorValue();
            ceil_val = fc_node->getCeilValue();
            has_floor_ceil = true;
        } catch (...) {
            has_value = false;
        }
    }
    
    oss << " EV=" << formatValue(value, has_value);
    if (has_floor_ceil) {
        oss << " [" << formatValue(floor_val, true) << ", " << formatValue(ceil_val, true) << "]";
    }
    
    // Decision info
    DecisionNode* decision = dynamic_cast<DecisionNode*>(node);
    if (decision) {
        if (decision->hasDecided()) {
            auto choice = decision->getDecisionChoice();
            if (choice.has_value()) {
                oss << " -> " << to_string(choice.value());
            }
        } else {
            oss << " (possible: ";
            bool first = true;
            for (PlayerAction action : decision->meaningful_actions_) {
                if (!first) oss << ", ";
                oss << to_string(action);
                first = false;
            }
            oss << ")";
        }
    }
    
    return oss.str();
}

// Format tree output as hierarchical string
string formatTreeOutput(AbstractBJTreeNode* root, int n_levels, int indent_size) {
    vector<string> lines;
    
    // Inner traversal function
    function<void(AbstractBJTreeNode*, const TransitionEvent*, bool, double, int)> traverse;
    traverse = [&](AbstractBJTreeNode* node, const TransitionEvent* event, bool is_best, double prob, int level) {
        lines.push_back(formatNodeLine(node, event, is_best, prob, level, indent_size));
        
        if (level >= n_levels) return;
        
        // Check for terminal nodes
        if (dynamic_cast<SimulationResultNode*>(node)) return;
        if (dynamic_cast<FloorCeilValueNode*>(node)) return;
        
        if (!node->has_built_children_ || node->children_.empty()) return;
        
        int best_idx = getBestActionIdx(node);
        
        for (size_t i = 0; i < node->children_.size(); ++i) {
            AbstractBJTreeNode* child = node->children_[i].get();
            const TransitionEvent* child_event = &node->children_events_[i];
            double child_prob = (i < node->children_prob_.size()) ? node->children_prob_[i] : 0.0;
            bool child_is_best = (static_cast<int>(i) == best_idx);
            traverse(child, child_event, child_is_best, child_prob, level + 1);
        }
    };
    
    traverse(root, nullptr, false, 0.0, 0);
    
    ostringstream result;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) result << "\n";
        result << lines[i];
    }
    return result.str();
}

// Print formatted tree output
void printTree(AbstractBJTreeNode* root, int n_levels, int indent_size) {
    cout << formatTreeOutput(root, n_levels, indent_size) << endl;
}

} // namespace blackjack
