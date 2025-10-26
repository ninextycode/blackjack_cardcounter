#include "tree_utils.h"
#include <iostream>
#include <iomanip>

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

} // namespace blackjack
