#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include "random_sampler.h"


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


PYBIND11_MODULE(random_sampler, m) {
    py::class_<RandomSampler>(m, "RandomSampler")
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
            "set_seed_global_sampler",
            &RandomSampler::setSeedGlobalSampler,
            py::arg("seed")
        )
        .def_static(
            "get_global_sampler",
            &RandomSampler::getGlobalSampler,
            py::return_value_policy::reference
        )
    ;
}