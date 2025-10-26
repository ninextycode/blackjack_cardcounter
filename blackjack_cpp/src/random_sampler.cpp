#include "random_sampler.h"

shared_ptr<RandomSampler> RandomSampler::global_sampler = make_shared<RandomSampler>();

void RandomSampler::setSeedGlobalSampler(uint64_t seed) {
    global_sampler->resetSeed(seed);
}

shared_ptr<RandomSampler> RandomSampler::getGlobalSampler() {
    return global_sampler;
}


RandomSampler::RandomSampler(uint64_t seed): 
    gen(seed),
    uniform_dist(0.0, 1.0) {
}

void RandomSampler::resetSeed(uint64_t seed) {
    lock_guard<mutex> lock(random_sampler_mutex);
    gen.seed(seed);
}

double RandomSampler::uniform() {
    lock_guard<mutex> lock(random_sampler_mutex);
    return uniform_dist(gen);
}


int RandomSampler::discrete(const vector<double>& probs) {
    if (probs.empty()) {
        throw invalid_argument("Probability vector is empty");
    }
    
    double u = uniform();
    double cumsum = 0.0;
    
    for (size_t i = 0; i < probs.size(); ++i) {
        cumsum += probs[i];
        if (u < cumsum) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(probs.size() - 1);
}


int RandomSampler::randint(int low, int high) {
    if (low >= high) {
        throw invalid_argument("low must be less than high");
    }
    lock_guard<mutex> lock(random_sampler_mutex);
    uniform_int_distribution<int> dist(low, high - 1);
    return dist(gen);
}


double RandomSampler::normal(double mean, double stddev) {
    lock_guard<mutex> lock(random_sampler_mutex);
    normal_distribution<double> dist(mean, stddev);
    return dist(gen);
}



/*
    // Choice with NumPy arrays
    py::object choice_array(py::array values, py::array_t<double> probs_array) {
        auto probs_buf = probs_array.request();
        if (probs_buf.ndim != 1) {
            throw runtime_error("Probabilities must be 1-dimensional");
        }
        
        vector<double> probs(
            static_cast<double*>(probs_buf.ptr), 
            static_cast<double*>(probs_buf.ptr) + probs_buf.shape[0]
        );
        
        auto values_buf = values.request();
        if (values_buf.ndim != 1) {
            throw runtime_error("Values must be 1-dimensional");
        }
        if (values_buf.shape[0] != probs_buf.shape[0]) {
            throw runtime_error("values and probs must have same length");
        }
        
        int idx = discrete(probs);
        return values[py::int_(idx)];
    }
};

PYBIND11_MODULE(random_sampler, m) {
    py::class_<RandomSampler>(m, "RandomSampler")
        .def(py::init<uint64_t>(), py::arg("seed") = 0)
        .def("seed", &RandomSampler::seed, "Set the random seed")
        .def("uniform", &RandomSampler::uniform, "Generate uniform random number in [0, 1)")
        .def("discrete", &RandomSampler::discrete, "Sample from discrete distribution",
             py::arg("probs"))
        .def("discrete_multiple", &RandomSampler::discrete_multiple,
             "Sample multiple times from discrete distribution",
             py::arg("probs"), py::arg("n"))
        .def("discrete_array", &RandomSampler::discrete_array,
             "Sample multiple times from discrete distribution (NumPy interface)",
             py::arg("probs"), py::arg("n"))
        .def("randint", &RandomSampler::randint, "Generate random integer in [low, high)",
             py::arg("low"), py::arg("high"))
        .def("normal", &RandomSampler::normal, "Generate normal random number",
             py::arg("mean") = 0.0, py::arg("stddev") = 1.0)
        .def("choice_int", &RandomSampler::choice_int, 
             "Choose one element from int array with given probabilities",
             py::arg("values"), py::arg("probs"))
        .def("choice_double", &RandomSampler::choice_double,
             "Choose one element from double array with given probabilities",
             py::arg("values"), py::arg("probs"))
        .def("choice_string", &RandomSampler::choice_string,
             "Choose one element from string array with given probabilities",
             py::arg("values"), py::arg("probs"))
        .def("choice", &RandomSampler::choice_array,
             "Choose one element from array with given probabilities (NumPy interface)",
             py::arg("values"), py::arg("probs"))
        .def("choice_multiple_int", &RandomSampler::choice_multiple_int,
             "Choose multiple elements from int array with given probabilities",
             py::arg("values"), py::arg("probs"), py::arg("n"), py::arg("replace") = true)
        .def("choice_multiple_double", &RandomSampler::choice_multiple_double,
             "Choose multiple elements from double array with given probabilities",
             py::arg("values"), py::arg("probs"), py::arg("n"), py::arg("replace") = true)
        .def("choice_multiple_string", &RandomSampler::choice_multiple_string,
             "Choose multiple elements from string array with given probabilities",
             py::arg("values"), py::arg("probs"), py::arg("n"), py::arg("replace") = true)
        .def("choice_multiple", &RandomSampler::choice_multiple_array,
             "Choose multiple elements from array with given probabilities (NumPy interface)",
             py::arg("values"), py::arg("probs"), py::arg("n"), py::arg("replace") = true);
}

*/