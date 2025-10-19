#include "../include/game_tree.h"
#include <algorithm>
#include <stdexcept>

namespace blackjack {

BJTreeNodeType get_type(const BJRound& bj_round) {
    if (bj_round.get_stage() == BJStage::ROUND_OVER) return BJTreeNodeType::TERMINAL;
    if (bj_round.need_card()) return BJTreeNodeType::CARD;
    if (bj_round.need_player_action()) return BJTreeNodeType::PLAYER_ACTION;
    if (bj_round.need_dealer_action()) return BJTreeNodeType::DEALER_CHECK_BJ;
    throw runtime_error("Cannot determine node type");
}

BJTreeNode::BJTreeNode(const BJRound& bj_round_, const ProbabilisticRankShoe& shoe_, BJTreeNode* parent_, bool copy_data)
    : bj_round(copy_data ? bj_round_.copy() : bj_round_)
    , shoe(copy_data ? shoe_.copy() : shoe_)
    , parent(parent_)
    , value(0.0)
    , has_built_children(false) {
    node_type = get_type(this->bj_round);
}

bool BJTreeNode::tree_completed() const { 
    if (!has_built_children) {
        return false;
    } else {
        return children_trees_completed(); 
    }
}

bool BJTreeNode::children_trees_completed() const {
    for (auto &c: children) {
        if (!c->tree_completed()) {
            return false;
        }
    }
    return true; 
}

void BJTreeNode::create_child(
    const BJRound& child_bj_round,
    const ProbabilisticRankShoe& child_shoe,
    double prob) 
{
    children.emplace_back(
        make_unique<BJTreeNode>(child_bj_round, child_shoe, this, false)
    );
    children_prob.push_back(prob);
}

void BJTreeNode::build_tree_layer(int depth) {
    if (depth==0) {
        return;
    }
    if (tree_completed()) {
        return;
    }

    if (!has_built_children) {
        build_children();
    }

    for (auto &c: children) {
        c->build_tree_layer(depth-1);
    }

    if (children_trees_completed()) {
        if (node_type == BJTreeNodeType::PLAYER_ACTION) {
            _player_action_node_completion();
        }
        if (node_type == BJTreeNodeType::CARD || node_type == BJTreeNodeType::DEALER_CHECK_BJ) {
            _chance_node_completion();
        }
    }
}

void BJTreeNode::_player_action_node_completion() {
    vector<double> vals;
    for (auto &c: children) {
        vals.push_back(c->get_value());
    }
    auto it = max_element(vals.begin(), vals.end());
    int idx = (int)distance(vals.begin(), it);
    children_prob[idx] = 1.0;
    value = vals[idx];
}

void BJTreeNode::_chance_node_completion() {
    vector<double> children_values;
    for (auto &c: children) {
        children_values.push_back(c->get_value());
    }
    this->value = 0;
    for (size_t i = 0; i < children.size(); i++) {
        this->value += (children_prob[i] * children_values[i]);
    }
}

void BJTreeNode::build_children() {
    if (node_type == BJTreeNodeType::TERMINAL || bj_round.get_stage() == BJStage::DEALER_CARD) {
        value = bj_round.get_player_value();
    } else if (node_type == BJTreeNodeType::CARD) {
        _build_children_card();
    } else if (node_type == BJTreeNodeType::PLAYER_ACTION) {
        _build_children_player_action();
    } else if (node_type == BJTreeNodeType::DEALER_CHECK_BJ) {
        _build_children_dealer_action();
    }
    has_built_children = true;
}

void BJTreeNode::_build_children_card() {
    auto possible_ranks = bj_round.get_possible_next_card_ranks();
    vector<int> possible_values;
    for (auto &r: possible_ranks) possible_values.push_back(rank_value(r));
    auto probs = shoe.get_rank_value_probabilities(nullopt);
    for (int rv=2; rv<=11; ++rv) {
        double prob = probs.p(rv);
        if (prob == 0.0) continue;
        Card card(Rank::ACE);
        // map value back to a Rank: prefer ACE for 11, TEN for 10, else from value
        Rank rank;
        if (rv==11) rank = Rank::ACE;
        else if (rv==10) rank = Rank::TEN;
        else if (rv==9) rank = Rank::NINE;
        else if (rv==8) rank = Rank::EIGHT;
        else if (rv==7) rank = Rank::SEVEN;
        else if (rv==6) rank = Rank::SIX;
        else if (rv==5) rank = Rank::FIVE;
        else if (rv==4) rank = Rank::FOUR;
        else if (rv==3) rank = Rank::THREE;
        else if (rv==2) rank = Rank::TWO;
        else rank = Rank::TEN;
        card.rank = rank;

        BJRound bj_copy = bj_round.copy();
    ProbabilisticRankShoe shoe_copy = shoe.copy();
        bj_copy.take_card(card);
        shoe_copy.burn_card(card);
    create_child(bj_copy, shoe_copy, prob);
    }
}

void BJTreeNode::_build_children_dealer_action() {
    // simplified: dealer check for blackjack either true or false based on shoe probabilities
    if (bj_round.get_stage() != BJStage::DEALER_CHECK_BJ) throw runtime_error("not dealer check");
    auto dv = bj_round.dealer_hand.get_best_value();
    if (!dv.has_value()) throw runtime_error("dealer value missing");
    int dealer_value = *dv;
    auto rv_prob = shoe.get_rank_value_probabilities();
    ProbabilisticRankShoe shoe_no_bj = shoe.copy();
    double p_dealer_blackjack = 0.0;
    if (dealer_value == 11) { p_dealer_blackjack = rv_prob.p(10); shoe_no_bj.lock_dealer_card_not_ten(); }
    else if (dealer_value == 10) { p_dealer_blackjack = rv_prob.p(11); shoe_no_bj.lock_dealer_card_not_ace(); }
    else throw runtime_error("Dealer cannot check blackjack with value other than 10 or 11");

    BJRound bj_bj = bj_round.copy(); bj_bj.take_action(DealerAction::CONFIRM_BLACKJACK);
    BJRound bj_no = bj_round.copy(); bj_no.take_action(DealerAction::CONFIRM_NO_BLACKJACK);
    create_child(bj_bj, shoe.copy(), p_dealer_blackjack);
    create_child(bj_no, shoe_no_bj, 1.0 - p_dealer_blackjack);
}

void BJTreeNode::_build_children_player_action() {
    auto actions = bj_round.get_available_actions();
    for (auto a: actions) {
        BJRound copy = bj_round.copy();
        copy.take_action(a);
        create_child(copy, shoe.copy(), 0.0);
    }
}

double BJTreeNode::get_value() const { return value; }

} // namespace blackjack
