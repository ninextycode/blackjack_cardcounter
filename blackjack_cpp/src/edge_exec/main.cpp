#include "edge.h"
#include "shoe.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <iomanip>

using namespace std;
using namespace blackjack;

// Parse shoe string like "[4,4,4,4,4,4,4,4,4,16]" into rank counts
// Order is: 2, 3, 4, 5, 6, 7, 8, 9, 10, A
vector<int> parseShoeString(const string& input) {
    vector<int> counts;
    string s = input;
    
    // Remove brackets if present
    if (!s.empty() && s.front() == '[') s.erase(0, 1);
    if (!s.empty() && s.back() == ']') s.pop_back();
    
    stringstream ss(s);
    string token;
    while (getline(ss, token, ',')) {
        counts.push_back(stoi(token));
    }
    
    if (counts.size() != 10) {
        throw runtime_error("Expected 10 values for ranks 2-10 and A, got " + to_string(counts.size()));
    }
    
    return counts;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " [n2,n3,n4,n5,n6,n7,n8,n9,n10,nA]" << endl;
        cerr << "Example: " << argv[0] << " [24,24,24,24,24,24,24,24,96,24]" << endl;
        return 1;
    }
    
    try {
        vector<int> counts = parseShoeString(argv[1]);
        
        // Create shoe from counts
        // Order in input: 2, 3, 4, 5, 6, 7, 8, 9, 10, A
        // RankMap order: index 2-10 for values 2-10, index 11 for Ace
        RankMap<int> rank_counts;
        rank_counts.fill(0);
        for (int i = 0; i < 9; ++i) {
            rank_counts.at(2 + i) = counts[i];  // 2-10
        }
        rank_counts.at(11) = counts[9];  // Ace
        
        ProbabilisticRankShoe shoe(rank_counts, RandomSampler::createNextSampler());
        BJRules rules = getDefaultRules();
        
        EdgeResult result = calculateEdge(
            shoe,
            rules,
            100,    // bet_unit
            4,      // sim_depth
            0.01,   // gap_target
            SimAlgo::RECURSIVE
        );
        
        // Output at full precision
        cout << fixed << setprecision(numeric_limits<double>::max_digits10);
        cout << result.ev << ", ";
        cout << result.ev_min << ", ";
        cout << result.ev_max << endl;
        
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}
