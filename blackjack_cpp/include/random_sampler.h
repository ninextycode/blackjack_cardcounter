#pragma once

#include <random>
#include <vector>
#include <stdexcept>
#include <thread>
#include <functional>

using namespace std;


class RandomSampler {
public:
    // Seed generator for creating new samplers with unique seeds
    static void resetGlobalSeedGenerator(uint64_t seed);
    static void resetGlobalSeedGenerator();
    static RandomSampler createNextSampler();

    RandomSampler(uint64_t seed);
    RandomSampler(const string& state);

    RandomSampler(const RandomSampler&) = default;
    RandomSampler& operator=(const RandomSampler& other) = default;
    
    RandomSampler(RandomSampler&&) noexcept = default;
    RandomSampler& operator=(RandomSampler&& other) noexcept = default;

    ~RandomSampler() = default;

    string getRngState() const;

    void resetSeed(uint64_t seed);

    double uniform();
    
    // Sample from discrete distribution given probabilities
    int discrete(const vector<double>& probs);
    int discrete_counts(const vector<int>& counts);
    
    // Sample integers in range [low, high)
    int randint(int low, int high);
    
    // Normal distribution
    double normal(double mean = 0.0, double stddev = 1.0);
    
    // Choice - select element from array with given probabilities
    template<typename T>
    T choice(const vector<T>& values, const vector<double>& probs);


    template<typename T>
    T choice_counts(const vector<T>& values, const vector<int>& counts);
private:
    static uint64_t generateSeed();

    static seed_seq createSeederSeedSeq() {
        random_device rd;
        uint64_t seed = rd();
        return createSeederSeedSeq(seed);
    }
    
    static seed_seq createSeederSeedSeq(uint64_t seed) {
        uint64_t thread_id_hash = hash<thread::id>{}(this_thread::get_id());
        return seed_seq{seed, thread_id_hash};
    }
        
    inline static thread_local minstd_rand global_seed_generator = [] {
        auto seq = createSeederSeedSeq();   // seq is a local lvalue
        return minstd_rand(seq);            // uses Sseq& constructor
    }();

    inline float u01f_minstd() {
        return std::generate_canonical<float, 24>(gen); // [0,1)
    }

    inline int fast_randint(int max) {
        return gen() % max;
    }

    minstd_rand gen;
    // mt19937_64 gen;
    // uniform_real_distribution<double> uniform_dist(0, 1);
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


template<typename T>
T RandomSampler::choice_counts(const vector<T>& values, const vector<int>& counts) {
    if (values.size() != counts.size()) {
        throw invalid_argument("values and counts must have same length");
    }
    if (values.empty()) {
        throw invalid_argument("Cannot choose from empty array");
    }
    int idx = discrete_counts(counts);
    return values[idx];
}