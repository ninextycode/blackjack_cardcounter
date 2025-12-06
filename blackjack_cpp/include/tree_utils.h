#pragma once

#include "abstract_node.h"
#include "simulation_result_node.h"
#include "mixed_node.h"
#include "floor_ceil_node.h"
#include <map>
#include <vector>
#include <string>
#include <deque>

namespace blackjack {

// Use NodeLevel and NodeIterator from mixed_node.h

// Get all nodes organized by level
std::map<int, std::vector<AbstractBJTreeNode*>> getNodesByLevels(AbstractBJTreeNode* root);

// Count stages in a list of nodes
std::map<std::string, int> countStages(const std::vector<AbstractBJTreeNode*>& nodes);

// Log tree structure with stage counts per level
void logTreeStructure(AbstractBJTreeNode* root);

// Format a card rank value as string (T for 10, A for 11)
std::string formatCardValue(int value);

// Format a TransitionEvent as string
std::string formatEvent(const TransitionEvent& event);

// Format a tree node line for display
std::string formatNodeLine(
    AbstractBJTreeNode* node,
    const TransitionEvent* event = nullptr,
    bool is_best = false,
    double prob = 0.0,
    int level = 0,
    int indent_size = 2
);

// Format tree output as hierarchical string
std::string formatTreeOutput(
    AbstractBJTreeNode* root,
    int n_levels = 2,
    int indent_size = 2
);

// Print formatted tree output
void printTree(
    AbstractBJTreeNode* root,
    int n_levels = 2,
    int indent_size = 2
);

} // namespace blackjack
