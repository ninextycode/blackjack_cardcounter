#include "../include/blackjack_round.h"
#include "../include/game_tree.h"
#include "../include/shoe.h"
#include <chrono>
#include <iostream>

using namespace blackjack;
using namespace std;

int main() {
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

	BJRound bj_round(&rules);
	ProbabilisticRankShoe shoe(8);
	bj_round.start_round(10);

	bj_round.take_card(Card(Rank::ACE));
	bj_round.take_card(Card(Rank::ACE));
	bj_round.take_card(Card(Rank::SIX));
	bj_round.take_action(PlayerAction::SPLIT);

	shoe.burn_rank_value(11);
	shoe.burn_rank_value(11);
	shoe.burn_rank_value(6);

	BJTreeNode root_node(bj_round, shoe);

	std::cout << "Blackjack round initialized." << std::endl;
	for (int i=0; i < 50; ++i) {
		auto t0 = std::chrono::high_resolution_clock::now();
		root_node.build_tree_layer(i);
		auto t1 = std::chrono::high_resolution_clock::now();
		double dt = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		std::cout << "Depth " << i << " built in " << dt / 1000 << " s" << std::endl;
        if (root_node.tree_completed()) break;
	}

    std::cout << "Round EV " << root_node.get_value() << std::endl;
	return 0;
}

