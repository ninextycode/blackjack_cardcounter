#include "edge.h"
#include "shoe.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <limits>
#include <iomanip>
#include <optional>
#include <boost/program_options.hpp>
#include "dealer_sim.h"


using namespace std;
using namespace blackjack;
namespace po = boost::program_options;

// Shoe order: 2, 3, 4, 5, 6, 7, 8, 9, 10, A (10 values)

struct RunOptions {
    int depth;
    double gap;
    vector<int> shoe_counts;  // 10 values: ranks 2-10 and A
};

// Returns nullopt if help was requested or parsing failed
optional<RunOptions> parseArgs(int argc, char* argv[]) {
    RunOptions opts;
    vector<int> counts;

    // Define command line options
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
        ("fast", "use fast settings (depth=3, gap=0.03)")
        ("medium", "use medium settings (depth=9, gap=0.003)")
        ("best", "use best settings (depth=9, gap=0.00001) [default]")
        ("depth,d", po::value<int>(&opts.depth), "simulation depth (manual override)")
        ("gap,g", po::value<double>(&opts.gap), "gap target (manual override)")
        ("shoe,s", po::value<vector<int>>(&counts)->multitoken()->required(), 
         "shoe counts as: n2 n3 n4 n5 n6 n7 n8 n9 n10 nA")
    ;

    // Allow positional arguments for shoe
    po::positional_options_description pos;
    pos.add("shoe", 10);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
        .options(desc)
        .positional(pos)
        .run(), vm);

    if (vm.count("help")) {
        cout << "Usage: [options] n2 n3 n4 n5 n6 n7 n8 n9 n10 nA\n\n";
        cout << desc << "\n";
        cout << "Presets:\n";
        cout << "  --fast   : depth=3,  gap=0.03\n";
        cout << "  --medium : depth=9,  gap=0.003\n";
        cout << "  --best   : depth=9,  gap=0.00001 (default)\n";
        cout << "\nExample: --fast 24 24 24 24 24 24 24 24 96 24\n";
        cout << "Type 'quit' or 'exit' to exit interactive mode.\n";
        return nullopt;
    }

    po::notify(vm);

    if (counts.size() != 10) {
        throw runtime_error("Expected 10 values for ranks 2-10 and A, got " + to_string(counts.size()));
    }
    opts.shoe_counts = counts;

    // Determine depth and gap based on presets or manual values
    // Default to "best" preset
    int preset_depth = 9;
    double preset_gap = 0.00001;

    if (vm.count("fast")) {
        preset_depth = 3;
        preset_gap = 0.03;
    } else if (vm.count("medium")) {
        preset_depth = 9;
        preset_gap = 0.003;
    } else if (vm.count("best")) {
        preset_depth = 9;
        preset_gap = 0.00001;
    }

    // Manual overrides take precedence
    if (!vm.count("depth")) {
        opts.depth = preset_depth;
    }
    if (!vm.count("gap")) {
        opts.gap = preset_gap;
    }

    return opts;
}

// Parse a line of input into argc/argv style
vector<string> tokenizeLine(const string& line) {
    vector<string> tokens;
    istringstream iss(line);
    string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

optional<RunOptions> parseLineArgs(const string& line) {
    vector<string> tokens = tokenizeLine(line);
    if (tokens.empty()) {
        return nullopt;
    }
    
    // Build argv-style array (first element is program name placeholder)
    vector<const char*> argv_ptrs;
    argv_ptrs.push_back("edge_exec");
    for (const auto& t : tokens) {
        argv_ptrs.push_back(t.c_str());
    }
    
    return parseArgs(static_cast<int>(argv_ptrs.size()), const_cast<char**>(argv_ptrs.data()));
}

void run(const RunOptions& opts) {
    // Create shoe from counts
    // Order in input: 2, 3, 4, 5, 6, 7, 8, 9, 10, A
    // RankMap order: index 2-10 for values 2-10, index 11 for Ace
    RankMap<int> rank_counts;
    rank_counts.fill(0);
    for (int i = 0; i <= 9; ++i) {
        rank_counts.at(2 + i) = opts.shoe_counts[i];  // 2-10, then A at index 11
    }
    
    ProbabilisticRankShoe shoe(rank_counts, RandomSampler::createNextSampler());
    BJRules rules = getDefaultRules();
    
    EdgeResult result = calculateEdge(
        shoe,
        rules,
        100,    // bet_unit
        opts.depth,
        opts.gap,
        SimAlgo::COMBO
    );
    
    // Output at full precision
    cout << fixed << setprecision(numeric_limits<double>::max_digits10);
    cout << result.ev << ", ";
    cout << result.ev_min << ", ";
    cout << result.ev_max << endl;
}

void runInteractive() {
    cout << "Ready. Enter commands (type 'help' for usage, 'quit' to exit):" << endl;
    cout << flush;
    
    string line;
    while (getline(cin, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == string::npos) {
            continue;  // Empty line
        }
        line = line.substr(start);
        
        // Check for exit commands
        if (line == "quit" || line == "exit" || line == "q") {
            break;
        }
        
        try {
            auto opts = parseLineArgs(line);
            if (opts) {
                run(*opts);
            }
        } catch (const po::error& e) {
            cerr << "Error: " << e.what() << endl;
        } catch (const exception& e) {
            cerr << "Error: " << e.what() << endl;
        }
        cout << flush;
    }
}

int main(int argc, char* argv[]) {
    PreloadedComboData::loadAll(
        "/home/maxim//Programming/blackjack_cardcounter/combinations/s16_new/", 11
    );

    // If no arguments provided, run in interactive mode
    if (argc == 1) {
        runInteractive();
        return 0;
    }

    // Otherwise, run single command from command line
    try {
        auto opts = parseArgs(argc, argv);
        if (!opts) {
            return 0;  // Help was shown
        }
        run(*opts);
    } catch (const po::error& e) {
        cerr << "Error: " << e.what() << endl;
        cerr << "Use --help for usage information." << endl;
        return 1;
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}
