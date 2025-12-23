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
#include "cpp_exec/main.h"
#include "dealer_sim.h"
using namespace blackjack;
using namespace std;



int main() {
    PreloadedComboData::loadAll(
        "/home/maxim//Programming/blackjack_cardcounter/combinations/", 11
    );
    // testTreeWalker();
    // testTreeWalkerWithCards();
    // testTreeWalkerSplit();
    // testTreeWalkerAAvA();

    // testEdgeTiming();
    // testEdgeCalculatorBasic();
    // testEdgeCalculatorCache();
    // testEdgeCalculatorTightenGap();
    testEdgeCalculatorCountSkew();
    
    return 0;
}
