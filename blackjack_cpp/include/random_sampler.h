#pragma once

#include <random>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <memory>

using namespace std;


class RandomSampler {
public:
    static void setSeedGlobalSampler(uint64_t seed);
    static shared_ptr<RandomSampler> getGlobalSampler();

    RandomSampler(uint64_t seed = 0);
    
    void resetSeed(uint64_t seed);

    double uniform();
    
    // Sample from discrete distribution given probabilities
    int discrete(const vector<double>& probs);
    
    // Sample integers in range [low, high)
    int randint(int low, int high);
    
    // Normal distribution
    double normal(double mean = 0.0, double stddev = 1.0);
    
    // Choice - select element from array with given probabilities
    template<typename T>
    T choice(const vector<T>& values, const vector<double>& probs);

private:
    static shared_ptr<RandomSampler> global_sampler;

    mt19937_64 gen;
    uniform_real_distribution<double> uniform_dist;
    mutex random_sampler_mutex;
};


template<typename T>
T RandomSampler::choice(const vector<T>& values, const vector<double>& probs) {
    if (values.size() != probs.size()) {
        throw invalid_argument("values and probs must have same length");
    }
    if (values.empty()) {
        throw invalid_argument("Cannot choose from empty array");
    }
    int idx = discrete(probs);
    return values[idx];
}