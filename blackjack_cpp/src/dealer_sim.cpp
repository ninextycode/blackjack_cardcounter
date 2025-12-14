#include "dealer_sim.h"

#include <stdexcept>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <chrono>
#include <nlohmann/json.hpp>
#include <iostream>

using namespace std;
using json = nlohmann::json;

namespace blackjack {

// Helper function to update hand value when adding a card
// Returns the new (value, is_soft) pair
inline pair<int, bool> addCardToHandValue(int current_value, bool is_soft, int card) {
    if (card == 11) {  // Ace
        if (current_value + 11 <= 21) {
            current_value += 11;
            is_soft = true;
        } else {
            current_value += 1;
        }
    } else {
        current_value += card;
    }
    
    // Convert soft to hard if bust
    if (current_value > 21 && is_soft) {
        current_value -= 10;
        is_soft = false;
    }
    
    return {current_value, is_soft};
}

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
        
        auto probs = shoe.getRankValueProbabilities(possible_cards);
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


// 0000000<c0><c1><c2><c3>
uint64_t getDealerCardsId(
    const vector<int>& cards
) {
    // each chard is encoded as 4 byte int, starting from the left
    uint64_t id = cards[0];
    for (size_t i = 1; i < cards.size(); ++i) {
        id = id << 4;
        id |= (static_cast<uint64_t>(cards[i]));
    }
    return id;
}


using ShoeState = pair<double, RankCount>;

const ShoeState& getDealerCardsProbability(
    uint64_t comb_id,
    unordered_map<uint64_t, ShoeState>& shoe_cache,
    const ProbabilisticRankShoe& base_shoe,
    const optional<vector<int>>& possible_first_card
) {
    const auto& it = shoe_cache.find(comb_id);
    if (it != shoe_cache.end()) {
        return it->second;
    }

    if (2 <= comb_id && comb_id <= 11) {
        double prob = base_shoe.getRankValueProbability(comb_id, possible_first_card);
        RankCount rank_count = base_shoe.getRankCount();
        rank_count.at(comb_id) = max(0, rank_count.at(comb_id) - 1);
        const auto& [it, success] = shoe_cache.try_emplace(
            comb_id, prob, std::move(rank_count)
        );
        return it->second;

    } else if (comb_id > 11) {
        int last_card = comb_id & 0xF;
        uint64_t id_wo_last_card = comb_id >> 4;

        const ShoeState shoe_state_wo_last = getDealerCardsProbability(
            id_wo_last_card, shoe_cache, base_shoe, possible_first_card
        );
        
        double comb_wo_last_prob = shoe_state_wo_last.first;
        
        if (comb_wo_last_prob == 0.0) {
            const auto& [it, success] = shoe_cache.try_emplace(
                comb_id, 0.0, RankCount()
            );
            return it->second;
        }

        const RankCount& rank_count_last_unaccounted = shoe_state_wo_last.second;

        int total_count = accumulate(
            rank_count_last_unaccounted.data.begin(),
            rank_count_last_unaccounted.data.end(),
            0
        );

        double last_card_prob = \
            double(rank_count_last_unaccounted.at(last_card)) / double(max(1, total_count));
        double comb_prob = comb_wo_last_prob * last_card_prob;
        
        RankCount rank_count_last_accounted = rank_count_last_unaccounted;
        rank_count_last_accounted.at(last_card) = \
            max(0, rank_count_last_accounted.at(last_card) - 1);

        const auto& [it, success] = shoe_cache.try_emplace(
            comb_id,
            comb_prob,
            std::move(rank_count_last_accounted)
        );

        return it->second;
    } else {
        throw runtime_error("Invalid combination id " + std::to_string(comb_id));
    }
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
    
    if (dealer_hand.size() != 1) {
        throw runtime_error("Dealer hand must have exactly 1 card");
    }
    if (bj_round.player_hands.size() != 1) {
        throw runtime_error("Must have exactly 1 player hand");
    }
    if (bj_round.getStage() != BJStage::DEALER_CARD) {
        throw runtime_error("Stage must be DEALER_CARD");
    }
    if (bj_round.player_hands[0].is_natural_blackjack()) {
        throw runtime_error("Player has natural blackjack");
    }
    
    if (n_dealer_sim_runs <= 0) {
        n_dealer_sim_runs = 1;
    }
    
    // Copy and unlock shoe if needed
    ProbabilisticRankShoe shoe_copy(shoe);
    if (shoe_copy.isDealerCardLocked()) {
        shoe_copy.unlockDealerCard();
    }
    
    auto player_best_value_opt = bj_round.player_hands[0].get_best_value();
    if (!player_best_value_opt.has_value()) {
        throw runtime_error("Player hand is bust");
    }
    int player_hand_value = player_best_value_opt.value();
    
    // Get preloaded data
    if (bj_round.rules_->dealer_hits_soft_17) {
        throw runtime_error("dealer_hits_soft_17 not supported in combo algorithm");
    }
    
    bool dealer_confirmed_no_bj = bj_round.rules_->dealer_checks_blackjack;
    if (dealer_confirmed_no_bj && bj_round.dealer_has_bj_after_check) {
        throw runtime_error("dealer_has_bj_after_check must be false");
    }
    
    int upcard = dealer_hand.values()[0];
    const CombinationsData& data = PreloadedComboData::getData(
        dealer_confirmed_no_bj, n_full_sample, upcard
    );
    
    optional<vector<int>> possible_second_card = bj_round.getPossibleNextCardRanks();
    
    const auto& bust_combos_data = data.bust_combos_data;
    const auto& stand_combos_data = data.stand_combos_data;
    const auto& other_combos_data = data.other_combos_data;
    // const auto& stand_values = data.stand_values;

    if (verbose) {
        cout << "Number of bust combos: " << bust_combos_data.size() << endl;
        cout << "Number of stand combos: " << stand_combos_data.size() << endl;
        cout << "Number of other combos: " << other_combos_data.size() << endl;
    }

    unordered_map<uint64_t, ShoeState> shoe_cache;
    // 2 - 49
    // 3 - 176
    // 4 - 413
    // 5 - 673
    // 6 - 869
    // 7 - 986
    // switch (n_full_sample) {
    //     case 2: shoe_cache.reserve(50); break;
    //     case 3: shoe_cache.reserve(200); break;
    //     case 4: shoe_cache.reserve(450); break;
    //     case 5: shoe_cache.reserve(700); break;
    //     case 6: shoe_cache.reserve(900); break;
    //     case 7: shoe_cache.reserve(1000); break;    
    // }
    shoe_cache.reserve(1500);

    double total_p = 0.0;

    double ev_stand_value = 0.0;

    // Process stand combos
    // vector<double> ev_stand(stand_combos_data.size(), 0.0);
    // vector<double> prob_stand(stand_combos_data.size(), 0.0);
    
    // for (size_t i = 0; i < stand_combos_data.size(); ++i) {
    //     int stand_value = stand_values[i];
    //     if (player_hand_value > stand_value) {
    //         ev_stand[i] = 1.0;
    //     } else if (player_hand_value == stand_value) {
    //         ev_stand[i] = 0.0;
    //     } else {
    //         ev_stand[i] = -1.0;
    //     }
    // }
    
    double p_stand = 0.0;
    for (size_t i = 0; i < stand_combos_data.size(); ++i) {
        const auto& [combo, num_perms, combo_id, dealer_value] = stand_combos_data[i];
        
        double prob = getDealerCardsProbability(
            combo_id,
            shoe_cache,
            shoe_copy,
            possible_second_card
        ).first;
        
        if (prob == 0.0) { continue; }
    
        // Instead of "if" branches:
        int cmp = (player_hand_value > dealer_value) - (player_hand_value < dealer_value);
        double ev_stand = static_cast<double>(cmp);

        prob *= num_perms;
        total_p += prob;
        p_stand += prob;
        ev_stand_value += ev_stand * prob;
    }
    
    
    double ev_other_value = 0.0;
    double p_other = 0.0;

    double hand_copy_time_us = 0;
    double sim_time_us = 0;

    for (size_t i_comb = 0; i_comb < other_combos_data.size(); ++i_comb) {
        const auto& [combo, num_perms, combo_id, dealer_value, is_soft] = other_combos_data[i_comb];
        
        const auto& [prob_cached, rank_count_cached] = getDealerCardsProbability(
            combo_id,
            shoe_cache,
            shoe_copy,
            possible_second_card
        );
        
        if (prob_cached == 0) {
            continue;
        }

        double prob = prob_cached * num_perms;
        total_p += prob;
        p_other += prob;

        // Run Monte Carlo simulation for this combo
        auto sim_start = std::chrono::high_resolution_clock::now();
        ProbabilisticRankShoe sim_shoe(rank_count_cached, RandomSampler::createNextSampler());
        double mean_value = runDealerCardsSimulationSimple(
            player_hand_value,
            dealer_value,
            is_soft,
            sim_shoe,
            num_perms * n_dealer_sim_runs,
            dealer_hit_soft_17
        );
        auto sim_end = std::chrono::high_resolution_clock::now();
        sim_time_us += std::chrono::duration<double, std::micro>(sim_end - sim_start).count();
        
        ev_other_value += mean_value * prob;
    }
    
    // Process bust combos
    double dealer_bust_p = 0.0;
    // TODO unsafe - can comment out - unsafe but faster to skip and then get as 1 - total_p
    // for (const auto& [combo, num_perms] : bust_combos_data) {
    //     // double prob = getCardsProbability(combo, shoe_copy, possible_second_card);
    //     double prob = getDealerCardsProbability(
    //         getDealerCardsId(combo),
    //         shoe_cache,
    //         shoe_copy,
    //         possible_second_card
    //     ).first;
        
    //     if (prob != 0.0) {
    //         prob *= num_perms;
    //         total_p += prob;
    //         dealer_bust_p += prob;
    //     }
    // }

    // Compute final values
    // TODO potentially unsafe assumption
    dealer_bust_p = 1 - total_p;  // total_p now excludes busts - TODO unsafe
    total_p = 1;
    
    double p_bust = dealer_bust_p;
    double ev_bust_value = 1.0 * dealer_bust_p;
    
    if (verbose) {
        printf("hand_copy_time_us = %f\n", hand_copy_time_us);
        printf("sim_time_us = %f\n", sim_time_us);
        printf("shoe_cache.size = %zu\n", shoe_cache.size());
        printf("p_bust = %f\n", p_bust);
        printf("p_stand = %f, ev_stand_value = %f\n", p_stand, ev_stand_value);
        printf("p_other = %f, ev_other_value = %f\n", p_other, ev_other_value);
        printf("p_total = %f\n", total_p);
    }
    
    double final_value = bj_round.bet_unit * (
        ev_other_value + ev_stand_value + ev_bust_value
    );
    
    if (abs(total_p - 1.0) > 1e-8) {
        throw runtime_error("Probability does not sum to 1, total_p = " + std::to_string(total_p));
    }
    
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
    
    auto probabilities = shoe.getRankValueProbabilities(possible_ranks);
    
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
            value_by_first_card.at(first_card) = runDealerCardsSimulation(
                player_value,
                dealer_hand,
                shoe,
                n_dealer_sim_runs,
                dealer_hit_soft_17
            );
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

double runDealerCardsSimulation(
    int player_value,
    ValueOnlyHand& dealer_hand,
    ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    bool dealer_hit_soft_17
) {
    double sum_values = 0.0;
    vector<int> burned_cards;
    burned_cards.reserve(10);  // Max realistic dealer cards
    
    for (int i = 0; i < max(1, n_dealer_sim_runs); ++i) {
        burned_cards.clear();
        
        while (!dealer_hand.is_bust() && 
               !dealerStandOrBust(dealer_hand, dealer_hit_soft_17)) 
        {
            int rank = shoe.sampleAndBurnRank(nullopt);
            dealer_hand.add_value(rank);
            burned_cards.push_back(rank);
        }
        
        // Collect results
        if (dealer_hand.is_bust()) {
            sum_values += 1.0;
        } else {
            int dealer_value = *dealer_hand.get_best_value();

            if (dealer_value < player_value) {
                sum_values += 1.0;
            } else if (dealer_value > player_value) {
                sum_values -= 1.0;
            }
            // else: push 0.0, no change to sum
        }

        // Restore shoe state
        shoe.addRankValues(burned_cards);
        // Restore dealer hand state
        dealer_hand.pop_value(burned_cards.size());
        
    }
    return sum_values / static_cast<double>(n_dealer_sim_runs);
}

double runDealerCardsSimulationSimple(
    int player_value,
    int dealer_value,
    bool is_soft,
    ProbabilisticRankShoe& shoe,
    int n_dealer_sim_runs,
    bool dealer_hit_soft_17
) {
    double sum_values = 0.0;
    vector<int> burned_cards;
    burned_cards.reserve(10);
    
    for (int i = 0; i < max(1, n_dealer_sim_runs); ++i) {
        burned_cards.clear();
        int current_value = dealer_value;
        bool current_is_soft = is_soft;
        
        // Dealer draw loop
        while (true) {
            // Check if bust
            if (current_value > 21) {
                break;
            }
            
            // Check if dealer should stand
            bool should_stand;
            if (dealer_hit_soft_17) {
                should_stand = (current_value > 17) || 
                               (current_value == 17 && !current_is_soft);
            } else {
                should_stand = (current_value >= 17);
            }
            
            if (should_stand) {
                break;
            }
            
            // Draw a card
            int rank = shoe.sampleAndBurnRank(nullopt);
            burned_cards.push_back(rank);
            
            // Update hand value
            tie(current_value, current_is_soft) = addCardToHandValue(current_value, current_is_soft, rank);
        }
        
        // Collect results
        if (current_value > 21) {
            sum_values += 1.0;  // dealer bust
        } else {
            if (current_value < player_value) {
                sum_values += 1.0;
            } else if (current_value > player_value) {
                sum_values -= 1.0;
            }
        }
        
        // Restore shoe state
        shoe.addRankValues(burned_cards);
    }
    
    return sum_values / static_cast<double>(max(1, n_dealer_sim_runs));
}



CombinationsData loadCombinationsData(const string& json_path, int upcard) {
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
        uint64_t combo_id = getDealerCardsId(combo);
        data.bust_combos_data.emplace_back(std::move(combo), count, combo_id);
    }

    // Parse stand_combos_data: [[combo_array, count], ...]
    for (const auto& entry : j["stand_combos_data"]) {
        vector<int> combo = entry[0].get<vector<int>>();
        int count = entry[1].get<int>();
        uint64_t combo_id = getDealerCardsId(combo);
        
        // Compute dealer value from upcard + combo (is_soft not needed for stand)
        int dealer_value = upcard;
        bool is_soft = (upcard == 11);
        for (int card : combo) {
            tie(dealer_value, is_soft) = addCardToHandValue(dealer_value, is_soft, card);
        }
        
        data.stand_combos_data.emplace_back(std::move(combo), count, combo_id, dealer_value);
    }

    // Parse stand_values: [value1, value2, ...]
    data.stand_values = j["stand_values"].get<vector<int>>();

    // Parse other_combos_data: [[combo_array, count], ...]
    for (const auto& entry : j["other_combos_data"]) {
        vector<int> combo = entry[0].get<vector<int>>();
        int count = entry[1].get<int>();
        uint64_t combo_id = getDealerCardsId(combo);
        
        // Compute dealer value and soft flag from upcard + combo
        int dealer_value = upcard;
        bool is_soft = (upcard == 11);
        for (int card : combo) {
            tie(dealer_value, is_soft) = addCardToHandValue(dealer_value, is_soft, card);
        }
        
        data.other_combos_data.emplace_back(std::move(combo), count, combo_id, dealer_value, is_soft);
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
                data_no_bj_[depth][upcard - 2] = loadCombinationsData(filename, upcard);
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
                data_bj_not_checked_[depth][upcard - 2] = loadCombinationsData(filename, upcard);
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
