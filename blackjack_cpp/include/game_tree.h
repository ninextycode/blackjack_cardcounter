#pragma once
#include <vector>
#include <memory>
#include "blackjack_round.h"
#include "shoe.h"

using namespace std;

namespace blackjack {

enum class BJTreeNodeType { CARD, PLAYER_ACTION, DEALER_CHECK_BJ, TERMINAL };

BJTreeNodeType get_type(const BJRound& bj_round);

class BJTreeNode {
public:
    BJTreeNode(const BJRound& bj_round, const ProbabilisticRankShoe& shoe, BJTreeNode* parent=nullptr, bool copy_data=true);
    bool tree_completed() const;
    bool children_trees_completed() const;
    void create_child(
        const BJRound& child_bj_round,
        const ProbabilisticRankShoe& child_shoe,
        double prob=0.0
    );
    void build_tree_layer(int depth);
    void build_children();
    double get_value() const;

    BJRound bj_round;
    ProbabilisticRankShoe shoe;
    BJTreeNode* parent;
    vector<unique_ptr<BJTreeNode>> children;
    vector<double> children_prob;
    double value;
    bool has_built_children;
    BJTreeNodeType node_type;
private:
    void _player_action_node_completion();
    void _chance_node_completion();
    void _build_children_card();
    void _build_children_dealer_action();
    void _build_children_player_action();
};

} // namespace blackjack
