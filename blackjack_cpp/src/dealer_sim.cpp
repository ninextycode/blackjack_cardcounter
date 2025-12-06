#include "dealer_sim.h"

#include <stdexcept>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>
#include <iostream>

using namespace std;
using json = nlohmann::json;

namespace blackjack {

// Helper function to compute probability of a card sequence
static double getCardsProbability(
    const vector<int>& cards,
    ProbabilisticRankShoe& shoe,
    const optional<vector<int>>& possible_first_card
) {
    double prob = 1.0;
    vector<int> burned_cards;
    
    for (size_t i = 0; i < cards.size(); ++i) {
        int card = cards[i];
        optional<vector<int>> possible_cards = (i == 0) ? possible_first_card : nullopt;
        
        auto probs = shoe.get_rank_value_probabilities(possible_cards);
        double card_prob = probs.at(card);
        
        if (card_prob == 0.0) {
            prob = 0.0;
            break;
        }
        
        prob *= card_prob;
        shoe.burnRankValue(card);
        burned_cards.push_back(card);
    }
    
    // Restore burned cards
    for (int card : burned_cards) {
        shoe.addRankValue(card);
    }
    
    return prob;
}

double runDealerCardsSimulationCombo(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    int n_full_sample,
    bool verbose
) {
    // Validate preconditions (matching Python asserts)
    ValueOnlyHand dealer_hand = bj_round.dealer_hand;
    bool dealer_hit_soft_17 = bj_round.rules_->dealer_hits_soft_17;
    
    // if (dealer_hand.size() != 1) {
    //     throw runtime_error("Dealer hand must have exactly 1 card");
    // }
    // if (bj_round.player_hands.size() != 1) {
    //     throw runtime_error("Must have exactly 1 player hand");
    // }
    // if (bj_round.getStage() != BJStage::DEALER_CARD) {
    //     throw runtime_error("Stage must be DEALER_CARD");
    // }
    // if (bj_round.player_hands[0].is_natural_blackjack()) {
    //     throw runtime_error("Player has natural blackjack");
    // }
    
    if (n_dealer_sim_runs <= 0) {
        n_dealer_sim_runs = 1;
    }
    
    // Copy and unlock shoe if needed
    ProbabilisticRankShoe shoe_copy(shoe);
    if (shoe_copy.isDealerCardLocked()) {
        shoe_copy.unlockDealerCard();
    }
    
    auto player_best_value_opt = bj_round.player_hands[0].get_best_value();
    // if (!player_best_value_opt.has_value()) {
    //     throw runtime_error("Player hand is bust");
    // }
    int player_hand_value = player_best_value_opt.value();
    
    // Get preloaded data
    // if (bj_round.rules_->dealer_hits_soft_17) {
    //     throw runtime_error("dealer_hits_soft_17 not supported in combo algorithm");
    // }
    
    bool dealer_confirmed_no_bj = bj_round.rules_->dealer_checks_blackjack;
    // if (dealer_confirmed_no_bj && bj_round.dealer_has_bj_after_check) {
    //     throw runtime_error("dealer_has_bj_after_check must be false");
    // }
    
    int upcard = dealer_hand.values()[0];
    const CombinationsData& data = PreloadedComboData::getData(
        dealer_confirmed_no_bj, n_full_sample, upcard
    );
    
    optional<vector<int>> possible_second_card = bj_round.getPossibleNextCardRanks();
    
    const auto& bust_combos_data = data.bust_combos_data;
    const auto& stand_combos_data = data.stand_combos_data;
    const auto& other_combos_data = data.other_combos_data;
    const auto& stand_values = data.stand_values;

    // if (verbose) {
    //     cout << "Number of bust combos: " << bust_combos_data.size() << endl;
    //     cout << "Number of stand combos: " << stand_combos_data.size() << endl;
    //     cout << "Number of other combos: " << other_combos_data.size() << endl;
    // }

    double total_p = 0.0;
    
    // Process bust combos
    double dealer_bust_p = 0.0;
    for (const auto& [combo, num_perms] : bust_combos_data) {
        double prob = getCardsProbability(combo, shoe_copy, possible_second_card);
        if (prob != 0.0) {
            prob *= num_perms;
            total_p += prob;
            dealer_bust_p += prob;
        }
    }
    
    // Process stand combos
    vector<double> ev_stand(stand_combos_data.size(), 0.0);
    vector<double> prob_stand(stand_combos_data.size(), 0.0);
    
    for (size_t i = 0; i < stand_combos_data.size(); ++i) {
        int stand_value = stand_values[i];
        if (player_hand_value > stand_value) {
            ev_stand[i] = 1.0;
        } else if (player_hand_value == stand_value) {
            ev_stand[i] = 0.0;
        } else {
            ev_stand[i] = -1.0;
        }
    }
    
    for (size_t i = 0; i < stand_combos_data.size(); ++i) {
        const auto& [combo, num_perms] = stand_combos_data[i];
        double prob = getCardsProbability(combo, shoe_copy, possible_second_card);
        if (prob != 0.0) {
            prob *= num_perms;
            total_p += prob;
            prob_stand[i] = prob;
        }
    }
    
    // Process other combos (need Monte Carlo simulation)
    vector<double> ev_other(other_combos_data.size(), 0.0);
    vector<double> prob_other(other_combos_data.size(), 0.0);
    
    for (size_t i_comb = 0; i_comb < other_combos_data.size(); ++i_comb) {
        const auto& [combo, num_perms] = other_combos_data[i_comb];
        
        double prob = 1.0;
        bool impossible = false;
        vector<int> burned_cards;
        
        for (size_t i_card = 0; i_card < combo.size(); ++i_card) {
            int card = combo[i_card];
            optional<vector<int>> possible_cards = (i_card == 0) ? possible_second_card : nullopt;
            
            auto probs = shoe_copy.get_rank_value_probabilities(possible_cards);
            double card_prob = probs.at(card);
            
            if (card_prob == 0.0) {
                impossible = true;
                break;
            }
            
            prob *= card_prob;
            shoe_copy.burnRankValue(card);
            burned_cards.push_back(card);
            dealer_hand.add_value(card);
        }
        
        if (impossible) {
            // Restore state
            for (int card : burned_cards) {
                shoe_copy.addRankValue(card);
                dealer_hand.pop_value();
            }
            continue;
        }
        
        prob *= num_perms;
        total_p += prob;
        prob_other[i_comb] = prob;
        
        // Run Monte Carlo simulation for this combo
        // assert not dealerStandOrBust in Python
        auto sim_values = runDealerCardsSimulation(
            player_hand_value,
            dealer_hand,
            shoe_copy,
            num_perms * n_dealer_sim_runs,
            dealer_hit_soft_17
        );
        
        double mean_value = accumulate(sim_values.begin(), sim_values.end(), 0.0) 
                           / static_cast<double>(sim_values.size());
        ev_other[i_comb] = mean_value;
        
        // Restore state
        for (int card : burned_cards) {
            shoe_copy.addRankValue(card);
            dealer_hand.pop_value();
        }
    }
    
    // Compute final values
    double p_bust = dealer_bust_p;
    double ev_bust_value = 1.0 * dealer_bust_p;
    
    double p_stand = accumulate(prob_stand.begin(), prob_stand.end(), 0.0);
    double p_other = total_p - p_bust - p_stand;
    
    double ev_stand_value = 0.0;
    for (size_t i = 0; i < stand_combos_data.size(); ++i) {
        ev_stand_value += ev_stand[i] * prob_stand[i];
    }
    
    double ev_other_value = 0.0;
    for (size_t i = 0; i < other_combos_data.size(); ++i) {
        ev_other_value += ev_other[i] * prob_other[i];
    }
    
    // if (verbose) {
    //     printf("p_bust = %f\n", p_bust);
    //     printf("p_stand = %f, ev_stand_value = %f\n", p_stand, ev_stand_value);
    //     printf("p_other = %f, ev_other_value = %f\n", p_other, ev_other_value);
    //     printf("p_total = %f\n", total_p);
    // }
    
    double final_value = bj_round.bet_unit * (
        ev_other_value + ev_stand_value + ev_bust_value
    );
    
    // if (abs(total_p - 1.0) > 1e-8) {
    //     throw runtime_error("Probability does not sum to 1, total_p = " + std::to_string(total_p));
    // }
    
    return final_value;
}

bool dealerStandOrBust(
    const ValueOnlyHand& dealer_hand,
    bool dealer_hit_soft_17
) {
    bool dealer_stand = false;
    auto best_value_opt = dealer_hand.get_best_value();
    
    if (!best_value_opt.has_value()) {
        return true; // bust
    }
    
    int best_value = best_value_opt.value();
    
    if (dealer_hit_soft_17) {
        if (best_value > 17) {
            dealer_stand = true;
        } else if (best_value == 17 && !dealer_hand.is_soft_17()) {
            dealer_stand = true;
        }
    } else {
        if (best_value >= 17) {
            dealer_stand = true;
        }
    }
    
    return dealer_stand;
}

double runDealerCardsSimulationRecursive(
    const BJRound& bj_round,
    const ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    int n_full_sample
) {
    BJStage stage = bj_round.getStage();
    
    if (stage == BJStage::ROUND_OVER) {
        return static_cast<double>(bj_round.getPlayerValue());
    }
    
    if (bj_round.player_hands.size() != 1) {
        throw runtime_error("Dealer simulation requires exactly one player hand");
    }
    if (stage != BJStage::DEALER_CARD) {
        throw runtime_error("Dealer simulation requires DEALER_CARD stage");
    }
    if (bj_round.player_hands[0].is_natural_blackjack()) {
        throw runtime_error("Player has natural blackjack - should not run dealer sim");
    }
    
    ProbabilisticRankShoe shoe_copy(shoe);
    if (shoe_copy.isDealerCardLocked()) {
        shoe_copy.unlockDealerCard();
    }
    
    auto possible_ranks = bj_round.getPossibleNextCardRanks();
    
    auto player_best_value = bj_round.player_hands[0].get_best_value();
    if (!player_best_value.has_value()) {
        throw runtime_error("Player hand is bust - should not run dealer sim");
    }
    int player_value = player_best_value.value();
    
    ValueOnlyHand dealer_hand = bj_round.dealer_hand;
    
    double value = bj_round.bet_unit * runDealerCardsSimulationRecursiveInternal(
        player_value,
        dealer_hand,
        shoe_copy,
        n_dealer_sim_runs,
        n_full_sample,
        possible_ranks,
        bj_round.rules_->dealer_hits_soft_17
    );
    
    return value;
}

double runDealerCardsSimulationRecursiveInternal(
    int player_value,
    ValueOnlyHand& dealer_hand,
    ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    int n_full_sample,
    const optional<vector<int>>& possible_ranks,
    bool dealer_hit_soft_17
) {
    vector<int> ranks_to_iterate;
    if (possible_ranks.has_value()) {
        ranks_to_iterate = possible_ranks.value();
    } else {
        for (int v = 2; v <= 11; ++v) {
            ranks_to_iterate.push_back(v);
        }
    }
    
    auto probabilities = shoe.get_rank_value_probabilities(possible_ranks);
    
    RankMap<double> value_by_first_card;
    value_by_first_card.fill(0.0);
    
    for (int first_card : ranks_to_iterate) {
        double prob = probabilities.at(first_card);
        if (prob <= 0.0) {
            continue;
        }
        
        shoe.burnRankValue(first_card);
        dealer_hand.add_value(first_card);
        
        if (dealer_hand.is_bust()) {
            value_by_first_card.at(first_card) = 1.0;
        }
        else if (dealerStandOrBust(dealer_hand, dealer_hit_soft_17)) {
            auto dealer_value_opt = dealer_hand.get_best_value();
            int dealer_value = dealer_value_opt.value();
            
            if (dealer_value < player_value) {
                value_by_first_card.at(first_card) = 1.0;
            } else if (dealer_value > player_value) {
                value_by_first_card.at(first_card) = -1.0;
            } else {
                value_by_first_card.at(first_card) = 0.0;
            }
        }
        else if (n_full_sample > 1) {
            value_by_first_card.at(first_card) = runDealerCardsSimulationRecursiveInternal(
                player_value,
                dealer_hand,
                shoe,
                n_dealer_sim_runs,
                n_full_sample - 1,
                nullopt, // After first card, any rank is possible
                dealer_hit_soft_17
            );
        }
        else {
            auto values = runDealerCardsSimulation(
                player_value,
                dealer_hand,
                shoe,
                n_dealer_sim_runs,
                dealer_hit_soft_17
            );
            double mean_value = accumulate(values.begin(), values.end(), 0.0) / (double) values.size();
            value_by_first_card.at(first_card) = mean_value;
        }
        
        dealer_hand.pop_value();
        shoe.addRankValue(first_card);
    }
    
    double mean_value = 0.0;
    for (int first_card : ranks_to_iterate) {
        double prob = probabilities.at(first_card);
        if (prob <= 0.0) {
            continue;
        }
        mean_value += prob * value_by_first_card.at(first_card);
    }
    
    return mean_value;
}

vector<double> runDealerCardsSimulation(
    int player_value,
    const ValueOnlyHand& dealer_hand,
    ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    bool dealer_hit_soft_17
) {
    vector<double> values;
    values.reserve(static_cast<size_t>(n_dealer_sim_runs));
    
    for (int i = 0; i < n_dealer_sim_runs; ++i) {
        ValueOnlyHand dealer_hand_sim = dealer_hand.copy();
        vector<int> burned_cards;
        
        while (!dealer_hand_sim.is_bust() && 
               !dealerStandOrBust(dealer_hand_sim, dealer_hit_soft_17)) 
        {
            int rank = shoe.sampleAndBurnRank(nullopt);
            dealer_hand_sim.add_value(rank);
            burned_cards.push_back(rank);
        }
        
        // Restore shoe state
        for (int card : burned_cards) {
            shoe.addRankValue(card);
        }
        
        // Collect results
        if (dealer_hand_sim.is_bust()) {
            values.push_back(1.0);
        } else {
            auto dealer_value_opt = dealer_hand_sim.get_best_value();
            int dealer_value = dealer_value_opt.value();
            
            if (dealer_value < player_value) {
                values.push_back(1.0);
            } else if (dealer_value > player_value) {
                values.push_back(-1.0);
            } else {
                values.push_back(0.0);
            }
        }
    }
    
    return values;
}



CombinationsData loadCombinationsData(const string& json_path) {
    ifstream file(json_path);
    if (!file.is_open()) {
        throw runtime_error("Failed to open file: " + json_path);
    }

    json j;
    file >> j;
    file.close();

    CombinationsData data;

    // Parse bust_combos_data: [[combo_array, count], ...]
    for (const auto& entry : j["bust_combos_data"]) {
        vector<int> combo = entry[0].get<vector<int>>();
        int count = entry[1].get<int>();
        data.bust_combos_data.emplace_back(combo, count);
    }

    // Parse stand_combos_data: [[combo_array, count], ...]
    for (const auto& entry : j["stand_combos_data"]) {
        vector<int> combo = entry[0].get<vector<int>>();
        int count = entry[1].get<int>();
        data.stand_combos_data.emplace_back(combo, count);
    }

    // Parse stand_values: [value1, value2, ...]
    data.stand_values = j["stand_values"].get<vector<int>>();

    // Parse other_combos_data: [[combo_array, count], ...]
    for (const auto& entry : j["other_combos_data"]) {
        vector<int> combo = entry[0].get<vector<int>>();
        int count = entry[1].get<int>();
        data.other_combos_data.emplace_back(combo, count);
    }

    return data;
}


void PreloadedComboData::loadAll(const string& base_path, int max_depth) {
    data_no_bj_.clear();
    data_bj_not_checked_.clear();
    
    constexpr int num_upcards = 10;  // upcards 2-11
    
    // Load dealer_checked_no_bj data
    for (int depth = 1; depth <= max_depth; ++depth) {
        data_no_bj_[depth].resize(num_upcards);
        for (int upcard = 2; upcard <= 11; ++upcard) {
            string filename = base_path + "/dealer_checked_no_bj/depth_" 
                + std::to_string(depth) + "_upcard_" + std::to_string(upcard) + ".json";
            
            ifstream test_file(filename);
            if (test_file.good()) {
                test_file.close();
                data_no_bj_[depth][upcard - 2] = loadCombinationsData(filename);
            } else {
                throw runtime_error("Failed to load: " + filename);
            }
        }
    }
    
    // Load bj_not_checked data
    for (int depth = 1; depth <= max_depth; ++depth) {
        data_bj_not_checked_[depth].resize(num_upcards);
        for (int upcard = 2; upcard <= 11; ++upcard) {
            string filename = base_path + "/bj_not_checked/depth_" 
                + std::to_string(depth) + "_upcard_" + std::to_string(upcard) + ".json";
            
            ifstream test_file(filename);
            if (test_file.good()) {
                test_file.close();
                data_bj_not_checked_[depth][upcard - 2] = loadCombinationsData(filename);
            } else {
                throw runtime_error("Failed to load: " + filename);
            }
        }
    }
    
    loaded_ = true;
}

const CombinationsData& PreloadedComboData::getData(
    bool dealer_confirmed_no_bj, int depth, int upcard
) {
    if (!loaded_) {
        throw runtime_error("PreloadedComboData not loaded. Call loadAll() first.");
    }
    
    if (upcard < 2 || upcard > 11) {
        throw runtime_error("Invalid upcard: " + std::to_string(upcard) + ". Must be 2-11.");
    }
    
    auto& data_map = dealer_confirmed_no_bj ? data_no_bj_ : data_bj_not_checked_;
    
    auto depth_it = data_map.find(depth);
    if (depth_it == data_map.end()) {
        throw runtime_error("No data for depth " + std::to_string(depth));
    }
    
    return depth_it->second[upcard - 2];
}

bool PreloadedComboData::isLoaded() {
    return loaded_;
}


} // namespace blackjack
