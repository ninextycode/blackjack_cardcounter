#include "cpp_exec/main.h"
#include "blackjack_round.h"
#include "mixed_node.h"
#include "shoe.h"
#include "tree_utils.h"
#include "random_sampler.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <fstream>     
#include <filesystem>  

using namespace std;
using namespace blackjack;



void blackjack::testSampling() {
	size_t seed = 0;
	RandomSampler::resetGlobalSeedGenerator(seed);
	ProbabilisticRankShoe shoe(8);
	string log_file = "100ranks_" + std::to_string(seed) + ".txt";

	// Open output file and write comma-separated ranks
	std::ofstream ofs(log_file);

	ofs << "ranks_cpp = [";
	for (size_t i = 0; i < 8*52; i++) {
		cout << i << " " << shoe.getNumberOfCards() << endl;
		cout << shoe.toString() << endl;
		int rank = shoe.sampleAndBurnRank();
		if (i != 0) ofs << ", ";
		ofs << rank;
	}

	ofs << "]\n";
	ofs.close();
}