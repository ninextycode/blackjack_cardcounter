#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "random_sampler.h"
#include "shoe.h"
#include "cards.h"


namespace py = pybind11;

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


PYBIND11_MODULE(blackjack_py, m) {
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
            "get_rank_value_counts",
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
        );
}