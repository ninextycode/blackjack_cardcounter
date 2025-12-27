#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "random_sampler.h"
#include "shoe.h"
#include "rules.h"
#include "actions.h"
#include "tree_walker.h"
#include "dealer_sim.h"
#include "edge.h"


namespace py = pybind11;

// Helper function to convert Python dict to RankCount
blackjack::RankCount dictToRankCount(const py::dict& d) {
    blackjack::RankCount rank_count;
    for (int rv = 2; rv <= 11; rv++) {
        if (d.contains(py::int_(rv))) {
            rank_count.at(rv) = d[py::int_(rv)].cast<int>();
        } else {
            rank_count.at(rv) = 0;
        }
    }
    return rank_count;
}

py::object choiceArray(
    RandomSampler* sampler,
    py::object values,
    py::array_t<double> probs_array
) {
    // if (py::isinstance<py::array>(values)) {
    //     arr = values.cast<py::array>();
    // } 
    // // Check if it's a list/sequence
    // else if (py::isinstance<py::list>(values) || py::isinstance<py::tuple>(values)) {
    //     py::module_ np = py::module_::import("numpy");
    //     arr = np.attr("array")(values);
    // }
    // else {
    //     throw std::runtime_error("values must be a list, tuple, or NumPy array");
    // }

    auto probs_buf = probs_array.request();
    if (probs_buf.ndim != 1) {
        throw runtime_error("Probabilities must be 1-dimensional");
    }
    
    vector<double> probs(
        static_cast<double*>(probs_buf.ptr), 
        static_cast<double*>(probs_buf.ptr) + probs_buf.shape[0]
    );
    
    if ((long int) len(values) != probs_buf.shape[0]) {
        throw runtime_error("values and probs must have same length");
    }
    
    int idx = sampler->discrete(probs);
    return values[py::int_(idx)];
}


PYBIND11_MODULE(blackjack_cpp, m) {
    py::class_<RandomSampler, shared_ptr<RandomSampler>>(m, "RandomSampler")
        .def(py::init<uint64_t>(), py::arg("seed") = 0)
        .def(
            "uniform",
            &RandomSampler::uniform,
            "Generate uniform random number in [0, 1)"
        )
        .def(
            "discrete",
            &RandomSampler::discrete,
            "Sample from discrete distribution",
            py::arg("probs")
        )
        .def(
            "randint",
            &RandomSampler::randint,
            "Generate random integer in [low, high)",
            py::arg("low"), py::arg("high")
        )
        .def(
            "normal",
            &RandomSampler::normal,
            "Generate normal random number",
            py::arg("mean") = 0.0, py::arg("stddev") = 1.0
        )
        .def(
            "choice", 
            &choiceArray,
            "Select element from array with given probabilities",
            py::arg("values"), py::arg("p")
        )
        // Expose static methods
        .def_static(
            "reset_global_seed_generator",
            py::overload_cast<>(&RandomSampler::resetGlobalSeedGenerator),
            "Reset the global seed generator with an auto-generated seed"
        )
        .def_static(
            "reset_global_seed_generator",
            py::overload_cast<uint64_t>(&RandomSampler::resetGlobalSeedGenerator),
            py::arg("seed"),
            "Reset the global seed generator used for creating new samplers"
        )
        .def_static(
            "create_next_sampler",
            &RandomSampler::createNextSampler,
            "Create a new sampler with a seed from the global seed generator"
        );

    // Expose ProbabilisticRankShoe
    py::class_<blackjack::ProbabilisticRankShoe>(m, "ProbabilisticRankShoe")
        .def(
            py::init<int>(),
            py::arg("n_decks") = 8,
            "Create a probabilistic rank shoe with auto-generated seed"
        )
        .def(
            py::init<int, uint64_t>(),
            py::arg("n_decks"),
            py::arg("seed"),
            "Create a probabilistic rank shoe with a specific seed"
        )
        .def(
            py::init<int, RandomSampler>(),
            py::arg("n_decks"),
            py::arg("sampler"),
            "Create a probabilistic rank shoe with a specific sampler"
        )
        .def(
            "copy",
            [](const blackjack::ProbabilisticRankShoe& self) {
                return blackjack::ProbabilisticRankShoe(self);
            },
            "Create a copy of the shoe"
        )
        .def(
            "copy_reset_sampler",
            [](const blackjack::ProbabilisticRankShoe& self) {
                return blackjack::ProbabilisticRankShoe(self.copyResetSampler());
            },
            "Create a copy of the shoe with a reset sampler"
        )
        .def(py::pickle(
            // __getstate__
            [](blackjack::ProbabilisticRankShoe& shoe) {
                py::dict state;
                state["sampler_state"] = shoe.getSamplerRngState();
                state["dealer_locked"] = shoe.dealerCardLockedValue();
                py::dict counts;
                for (int rv = 2; rv <= 11; rv++) {
                    counts[py::int_(rv)] = shoe.getNumberOfRankCards(rv);
                }
                state["counts"] = counts;
                return state;
            },
            // __setstate__
            [](py::dict state) {
                string sampler_state = state["sampler_state"].cast<string>();
                int dealer_locked = state["dealer_locked"].cast<int>();
                py::dict counts = state["counts"].cast<py::dict>();
                
                blackjack::ProbabilisticRankShoe shoe(1, RandomSampler(sampler_state));
                for (int rv = 2; rv <= 11; rv++) {
                    shoe.setNumberOfRankCards(rv, counts[py::int_(rv)].cast<int>());
                }
                if (dealer_locked == 11) {
                    shoe.lockDealerCardNotAce();
                } else if (dealer_locked == 10) {
                    shoe.lockDealerCardNotTen();
                }
                return shoe;
            }
        ))
        .def(
            "get_rank_value_probabilities",
            [](const blackjack::ProbabilisticRankShoe& shoe, 
               const optional<vector<int>>& given_rank_values_set) {
                auto rank_probs = shoe.getRankValueProbabilities(given_rank_values_set);
                py::dict result;
                for (int rv = 2; rv <= 11; rv++) {
                    double p = rank_probs.at(rv);
                    if (p > 0) {
                        result[py::int_(rv)] = p;
                    }
                }
                return result;
            },
            py::arg("given_rank_values_set") = py::none(),
            "Get probabilities for rank values 2-11 as a dictionary"
        )
        .def(
            "get_rank_count",
            [](const blackjack::ProbabilisticRankShoe& shoe) {
                auto rank_counts = shoe.getRankCount();
                py::dict result;
                for (int rv = 2; rv <= 11; rv++) {
                    int c = rank_counts.at(rv);
                    result[py::int_(rv)] = c;
                }
                return result;
            },
            "Get counts for rank values 2-11 as a dictionary"
        )
        .def(
            "burn_rank_value",
            &blackjack::ProbabilisticRankShoe::burnRankValue,
            py::arg("rank_value"),
            "Burn a card with the given rank value"
        )
        .def(
            "add_rank_value",
            &blackjack::ProbabilisticRankShoe::addRankValue,
            py::arg("rank_value"),
            "Add a card with the given rank value back to the shoe"
        )
        .def(
            "sample_rank",
            &blackjack::ProbabilisticRankShoe::sampleRank,
            py::arg("given_rank_values_set") = py::none(),
            "Sample a rank value from the shoe"
        )
        .def(
            "sample_and_burn_rank",
            &blackjack::ProbabilisticRankShoe::sampleAndBurnRank,
            py::arg("given_rank_values_set") = py::none(),
            "Sample and burn a rank value from the shoe"
        )
        .def(
            "lock_dealer_card_not_ace",
            &blackjack::ProbabilisticRankShoe::lockDealerCardNotAce,
            "Lock dealer card to not be an ace"
        )
        .def(
            "lock_dealer_card_not_ten",
            &blackjack::ProbabilisticRankShoe::lockDealerCardNotTen,
            "Lock dealer card to not be a ten"
        )
        .def(
            "lock_dealer_card_not",
            &blackjack::ProbabilisticRankShoe::lockDealerCardNot,
            py::arg("rank_value"),
            "Lock dealer card to not be the given rank value"
        )
        .def(
            "unlock_dealer_card",
            &blackjack::ProbabilisticRankShoe::unlockDealerCard,
            "Unlock dealer card constraint"
        )
        .def(
            "__str__",
            &blackjack::ProbabilisticRankShoe::toString,
            "String representation of the shoe"
        )
        .def(
            "dealer_card_locked_value",
            &blackjack::ProbabilisticRankShoe::dealerCardLockedValue,
            "Get the locked dealer card value, or -1 if not locked"
        )
        .def(
            "is_dealer_card_locked",
            &blackjack::ProbabilisticRankShoe::isDealerCardLocked,
            "Check if the dealer card is locked"
        )
        .def(
            "reset_sampler",
            py::overload_cast<>(&blackjack::ProbabilisticRankShoe::resetSampler),
            "Reset the random sampler with a new auto-generated seed"
        )
        .def(
            "reset_sampler",
            py::overload_cast<const RandomSampler&>(&blackjack::ProbabilisticRankShoe::resetSampler),
            py::arg("sampler"),
            "Reset the random sampler with a specific sampler"
        )
        .def(
            "get_number_of_rank_cards",
            &blackjack::ProbabilisticRankShoe::getNumberOfRankCards,
            py::arg("rank_value"),
            "Get the number of cards with the given rank value"
        )
        .def(
            "set_number_of_rank_cards",
            &blackjack::ProbabilisticRankShoe::setNumberOfRankCards,
            py::arg("rank_value"),
            py::arg("number"),
            "Set the number of cards with the given rank value"
        )
        .def(
            "get_number_of_cards",
            &blackjack::ProbabilisticRankShoe::getNumberOfCards,
            "Get the number of cards in the shoe"
        )
        .def(
            "__len__",
            &blackjack::ProbabilisticRankShoe::getNumberOfCards,
            "Get the number of cards in the shoe"
        );

    // Expose BJRules as CppBJRules
    py::class_<blackjack::BJRules>(m, "CppBJRules")
        .def(py::init<>())
        .def_readwrite("dealer_checks_blackjack", &blackjack::BJRules::dealer_checks_blackjack)
        .def_readwrite("dealer_hits_soft_17", &blackjack::BJRules::dealer_hits_soft_17)
        .def_readwrite("allow_late_surrender", &blackjack::BJRules::allow_late_surrender)
        .def_readwrite("allow_early_surrender_on_ten", &blackjack::BJRules::allow_early_surrender_on_ten)
        .def_readwrite("allow_early_surrender_on_ace", &blackjack::BJRules::allow_early_surrender_on_ace)
        .def_readwrite("allow_early_surrender_on_all", &blackjack::BJRules::allow_early_surrender_on_all)
        .def_readwrite("dealer_shows_card_on_surrender", &blackjack::BJRules::dealer_shows_card_on_surrender)
        .def_readwrite("allow_insurance_vs_ace", &blackjack::BJRules::allow_insurance_vs_ace)
        .def_readwrite("natural_blackjack_payout", &blackjack::BJRules::natural_blackjack_payout)
        .def_readwrite("surrender_payout", &blackjack::BJRules::surrender_payout)
        .def_readwrite("insurance_payout", &blackjack::BJRules::insurance_payout)
        .def_readwrite("max_splits_allowed", &blackjack::BJRules::max_splits_allowed)
        .def_readwrite("allow_action_on_split_aces", &blackjack::BJRules::allow_action_on_split_aces)
        .def_readwrite("allow_double_after_split", &blackjack::BJRules::allow_double_after_split)
        .def_readwrite("allow_double_on_soft", &blackjack::BJRules::allow_double_on_soft)
        .def_readwrite("allow_split_different_tens", &blackjack::BJRules::allow_split_different_tens)
        .def("__str__", &blackjack::BJRules::to_string);

    m.def("getDefaultRules", &blackjack::getDefaultRules, "Get the default blackjack rules");

    // Expose SimAlgo enum
    py::enum_<blackjack::SimAlgo>(m, "SimAlgo")
        .value("COMBO", blackjack::SimAlgo::COMBO)
        .value("RECURSIVE", blackjack::SimAlgo::RECURSIVE);

    // Module-level function for convenient combo data loading
    m.def(
        "load_combo_data",
        [](const string& base_path, int max_depth) {
            if (!blackjack::PreloadedComboData::isLoaded()) {
                blackjack::PreloadedComboData::loadAll(base_path, max_depth);
            }
        },
        py::arg("base_path"),
        py::arg("max_depth") = 11,
        "Load precomputed combo data if not already loaded (required for SimAlgo.COMBO)"
    );

    // Expose ValueEstimate struct
    py::class_<blackjack::ValueEstimate>(m, "ValueEstimate")
        .def(py::init<>())
        .def_readwrite("ev", &blackjack::ValueEstimate::ev)
        .def_readwrite("ev_min", &blackjack::ValueEstimate::ev_min)
        .def_readwrite("ev_max", &blackjack::ValueEstimate::ev_max)
        .def("__repr__", [](const blackjack::ValueEstimate& v) {
            return "ValueEstimate(" + to_string(v.ev) + ", "
                    " [" + to_string(v.ev_min) + ", " + to_string(v.ev_max) + "])";
        });

    // Expose EdgeResult struct
    py::class_<blackjack::ValueResult>(m, "EdgeResult")
        .def(py::init<>())
        .def_readwrite("ev", &blackjack::ValueResult::ev)
        .def_readwrite("ev_min", &blackjack::ValueResult::ev_min)
        .def_readwrite("ev_max", &blackjack::ValueResult::ev_max)
        .def("__repr__", [](const blackjack::ValueResult& v) {
            return "EdgeResult(" + to_string(v.ev) + ", "
                    " [" + to_string(v.ev_min) + ", " + to_string(v.ev_max) + "])";
        });

    // Expose TreeWalker class
    py::class_<blackjack::TreeWalker>(m, "TreeWalker")
        .def_static(
            "build_initial",
            [](
                pair<int, int> player_cards,
                int dealer_card,
                const blackjack::BJRules& rules,
                py::dict shoe_rank_count_dict,
                int max_hand_size_full_enum,
                int dealer_sim_depth,
                blackjack::SimAlgo sim_algo,
                int bet_unit,
                bool initial_cards_burned
            ) {
                return blackjack::TreeWalker::buildInitialTreeWalker(
                    player_cards,
                    dealer_card,
                    rules,
                    dictToRankCount(shoe_rank_count_dict),
                    max_hand_size_full_enum,
                    dealer_sim_depth,
                    sim_algo,
                    bet_unit,
                    initial_cards_burned
                );
            },
            py::arg("player_cards"),
            py::arg("dealer_card"),
            py::arg("rules"),
            py::arg("shoe_rank_count"),
            py::arg("max_hand_size_full_enum") = 0,
            py::arg("dealer_sim_depth") = 9,
            py::arg("sim_algo") = blackjack::SimAlgo::COMBO,
            py::arg("bet_unit") = 100,
            py::arg("initial_cards_burned") = true,
            "Build an initial TreeWalker from player cards, dealer card, rules, and shoe counts"
        )
        .def("need_card", &blackjack::TreeWalker::needCard, "Check if the walker needs a card")
        .def("need_player_action", &blackjack::TreeWalker::needPlayerAction, "Check if the walker needs a player action")
        .def("need_dealer_action", &blackjack::TreeWalker::needDealerAction, "Check if the walker needs a dealer action")
        .def("finished", &blackjack::TreeWalker::finished, "Check if the round is finished")
        .def(
            "get_available_player_actions",
            [](const blackjack::TreeWalker& walker) {
                auto actions = walker.getAvailablePlayerActions();
                py::list result;
                for (const auto& action : actions) {
                    result.append(blackjack::to_string(action));
                }
                return result;
            },
            "Get available player actions as strings"
        )
        .def(
            "get_available_dealer_actions",
            [](const blackjack::TreeWalker& walker) {
                auto actions = walker.getAvailableDealerActions();
                py::list result;
                for (const auto& action : actions) {
                    result.append(blackjack::to_string(action));
                }
                return result;
            },
            "Get available dealer actions as strings"
        )
        .def(
            "get_possible_next_card_ranks",
            &blackjack::TreeWalker::getPossibleNextCardRanks,
            "Get possible next card ranks"
        )
        .def("take_card", &blackjack::TreeWalker::takeCard, py::arg("card_value"), "Take a card")
        .def(
            "take_player_action",
            [](blackjack::TreeWalker& walker, const string& action_str) {
                walker.takePlayerAction(blackjack::player_action_from_string(action_str));
            },
            py::arg("action"),
            "Take a player action (as string: HIT, STAND, DOUBLE, SPLIT, SURRENDER, etc.)"
        )
        .def(
            "take_dealer_action",
            [](blackjack::TreeWalker& walker, const string& action_str) {
                walker.takeDealerAction(blackjack::dealer_action_from_string(action_str));
            },
            py::arg("action"),
            "Take a dealer action (as string: CONFIRM_BLACKJACK, CONFIRM_NO_BLACKJACK)"
        )
        .def(
            "get_best_actions",
            [](const blackjack::TreeWalker& walker) {
                auto actions = walker.getBestActions();
                py::list result;
                for (const auto& [action, estimate] : actions) {
                    result.append(py::make_tuple(blackjack::to_string(action), estimate));
                }
                return result;
            },
            "Get best actions sorted by expected value, returns list of (action_str, ValueEstimate)"
        )
        .def(
            "get_best_action",
            [](const blackjack::TreeWalker& walker) -> py::object {
                auto best_action = walker.getBestAction();
                if (!best_action.has_value()) {
                    return py::none();
                }
                return py::str(blackjack::to_string(best_action.value()));
            },
            "Get the best action as a string, or None if no conclusive best action"
        )
        .def("get_value_estimate", &blackjack::TreeWalker::getValueEstimate, "Get the current value estimate")
        .def(
            "tighten_value_estimate_gap",
            &blackjack::TreeWalker::tightenValueEstimateGap,
            py::arg("relative_gap"),
            "Tighten the value estimate gap"
        )
        .def("get_state_info", &blackjack::TreeWalker::getStateInfo, "Get current state info as string")
        .def("__str__", &blackjack::TreeWalker::getStateInfo);

        
    // Expose EdgeCalculator class
    py::class_<blackjack::EdgeCalculator>(m, "EdgeCalculator")
        .def(
            py::init<const blackjack::BJRules&, int>(),
            py::arg("rules"),
            py::arg("bet_unit") = 100,
            "Create an EdgeCalculator with rules and bet unit"
        )
        .def(
            "calculate_edge",
            &blackjack::EdgeCalculator::calculateEdge,
            py::arg("shoe"),
            py::arg("sim_depth") = 9,
            py::arg("gap_target") = 0.03,
            py::arg("algo") = blackjack::SimAlgo::COMBO,
            py::arg("parallel") = true,
            "Calculate edge and cache trees for TreeWalker creation"
        )
        .def(
            "get_edge_result",
            [](const blackjack::EdgeCalculator& calc) -> py::object {
                if (!calc.hasEdgeResult()) {
                    return py::none();
                }
                return py::cast(calc.getEdgeResult());
            },
            "Get the last edge result, or None if not calculated"
        )
        .def(
            "tighten_value_estimate_gap",
            &blackjack::EdgeCalculator::tightenValueEstimateGap,
            py::arg("relative_gap"),
            py::arg("parallel") = true,
            "Tighten value estimate gap for cached nodes and recompute edge"
        )
        .def(
            "create_tree_walker",
            &blackjack::EdgeCalculator::createTreeWalker,
            py::arg("player_cards"),
            py::arg("dealer_card"),
            "Create a TreeWalker for a starting hand (consumes cached tree)"
        )
        .def(
            "can_create_tree_walker",
            &blackjack::EdgeCalculator::canCreateTreeWalker,
            py::arg("player_cards"),
            py::arg("dealer_card"),
            "Check if a TreeWalker can be created with the current shoe"
        )
        .def(
            "has_cached_tree",
            &blackjack::EdgeCalculator::hasCachedTree,
            py::arg("player_cards"),
            py::arg("dealer_card"),
            "Check if a cached tree exists for the given starting hand"
        );
}
