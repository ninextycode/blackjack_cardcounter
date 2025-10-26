#pragma once

#include "abstract_node.h"
#include "simulation_result_node.h"
#include "mixed_node.h"
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

} // namespace blackjack
