#include "random_sampler.h"
#include <sstream>


void RandomSampler::resetGlobalSeedGenerator(uint64_t seed) {
    // lock_guard<mutex> lock(global_seed_generator_mutex);
    global_seed_generator.seed(seed);
}


void RandomSampler::resetGlobalSeedGenerator() {
    // lock_guard<mutex> lock(global_seed_generator_mutex);
    global_seed_generator.seed(random_device{}());
}


uint64_t RandomSampler::generateSeed() {
    // lock_guard<mutex> lock(global_seed_generator_mutex);
    auto seed = global_seed_generator();
    // avoid seed 0
    return seed ? seed : 1;
}

RandomSampler RandomSampler::createNextSampler() {
    // return RandomSampler(generateSeed());
    return RandomSampler(global_seed_generator());
}

RandomSampler::RandomSampler(uint64_t seed): 
    gen(seed) {
}

// RandomSampler::RandomSampler(const RandomSampler& other):
//     gen(other.gen),
//     uniform_dist(other.uniform_dist) {
// }

RandomSampler::RandomSampler(const string& state) {
    istringstream iss(state);
    iss >> gen;
}

string RandomSampler::getRngState() const {
    ostringstream oss;
    oss << gen;
    return oss.str();
}

void RandomSampler::resetSeed(uint64_t seed) {
    gen.seed(seed);
}

double RandomSampler::uniform() {
    return u01f_minstd();  // uniform_dist(gen);
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

int RandomSampler::discrete_counts(const vector<int>& counts) {
    if (counts.empty()) {
        throw invalid_argument("Counts vector is empty");
    }
    
    int total = 0;
    for (int count : counts) {
        if (count < 0) {
            throw invalid_argument("Counts must be non-negative");
        }
        total += count;
    }
    if (total == 0) {
        throw invalid_argument("Total count is zero");
    }
    
    int r = fast_randint(total);
    int cumulative = 0;
    for (size_t i = 0; i < counts.size(); ++i) {
        cumulative += counts[i];
        if (r < cumulative) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(counts.size() - 1);
}


int RandomSampler::randint(int low, int high) {
    if (low >= high) {
        throw invalid_argument("low must be less than high");
    }
    uniform_int_distribution<int> dist(low, high - 1);
    return dist(gen);
}


double RandomSampler::normal(double mean, double stddev) {
    normal_distribution<double> dist(mean, stddev);
    return dist(gen);
}

// RandomSampler& RandomSampler::operator=(const RandomSampler& other) {
//     gen = other.gen;
//     uniform_dist = other.uniform_dist;
//     return *this;
// }
