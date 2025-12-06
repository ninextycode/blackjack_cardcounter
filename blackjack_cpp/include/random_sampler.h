#pragma once

#include <random>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <memory>

using namespace std;


class RandomSampler {
public:
    // Seed generator for creating new samplers with unique seeds
    static void resetGlobalSeedGenerator(uint64_t seed);
    static void resetGlobalSeedGenerator();
    static RandomSampler createNextSampler();

    RandomSampler(uint64_t seed);
    RandomSampler(const RandomSampler&);
    RandomSampler(const string& state);
    RandomSampler& operator=(const RandomSampler& other);

    string getRngState() const;

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
    static uint64_t generateSeed();
    inline static mt19937_64 global_seed_generator{random_device{}()};
    inline static mutex global_seed_generator_mutex;

    mt19937_64 gen;
    uniform_real_distribution<double> uniform_dist;
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