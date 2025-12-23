#include "cpp_exec/main.h"
#include "edge.h"
#include "rules.h"
#include "shoe.h"
#include "tree_walker.h"
#include <iomanip>
#include <iostream>

using namespace std;
using namespace blackjack;

namespace {

void printEdgeResult(const ValueResult& result) {
    cout << fixed << setprecision(4)
         << "ev=" << result.ev
         << " [" << result.ev_min << ", " << result.ev_max << "]" << endl;
}

void printCheck(const string& label, bool ok) {
    cout << (ok ? "[OK] " : "[FAIL] ") << label << endl;
}

} // namespace

void blackjack::testEdgeCalculatorBasic() {
    cout << "\n=== Test: EdgeCalculator basic ===" << endl;

    BJRules rules = getDefaultRules();
    EdgeCalculator calc(rules, 10);

    cout << "hasEdgeResult before: " << boolalpha << calc.hasEdgeResult() << endl;

    try {
        calc.getEdgeResult();
        cout << "[FAIL] getEdgeResult should throw before calculateEdge" << endl;
    } catch (const exception& e) {
        cout << "[OK] getEdgeResult threw: " << e.what() << endl;
    }

    ProbabilisticRankShoe shoe(6);
    calc.calculateEdge(
        shoe,
        9,
        0.001,
        SimAlgo::RECURSIVE,
        true
    );

    printCheck("hasEdgeResult after", calc.hasEdgeResult());

    ValueResult result = calc.getEdgeResult();
    printEdgeResult(result);
    bool bounds_ok = result.ev_min <= result.ev && result.ev <= result.ev_max;
    printCheck("ev bounds are consistent", bounds_ok);
}

void blackjack::testEdgeCalculatorCache() {
    cout << "\n=== Test: EdgeCalculator cache ===" << endl;

    BJRules rules = getDefaultRules();
    EdgeCalculator calc(rules, 10);
    ProbabilisticRankShoe shoe(6);

    calc.calculateEdge(
        shoe,
        9,
        0.001,
        SimAlgo::COMBO,
        true
    );

    ValueResult result = calc.getEdgeResult();
    printEdgeResult(result);

    pair<int, int> player_cards{10, 6};
    int dealer_card = 6;

    bool can_create = calc.canCreateTreeWalker(player_cards, dealer_card);
    printCheck("canCreateTreeWalker returns true", can_create);

    bool has_cache = calc.hasCachedTree(player_cards, dealer_card);
    printCheck("hasCachedTree returns true", has_cache);

    if (has_cache) {
        TreeWalker walker = calc.createTreeWalker(player_cards, dealer_card);
        printCheck("hasCachedTree after createTreeWalker is false",
                   !calc.hasCachedTree(player_cards, dealer_card));

        if (!walker.finished()) {
            cout << "TreeWalker state: " << walker.getStateInfo() << endl;
        }
    }

    try {
        calc.createTreeWalker(player_cards, dealer_card);
        cout << "[FAIL] createTreeWalker should throw when cache is empty" << endl;
    } catch (const exception& e) {
        cout << "[OK] createTreeWalker threw: " << e.what() << endl;
    }

    EdgeCalculator calc_missing(rules, 10);
    ProbabilisticRankShoe missing_shoe(1, 13);
    missing_shoe.setNumberOfRankCards(5, 0);
    calc_missing.calculateEdge(
        missing_shoe,
        1,
        1.0,
        SimAlgo::RECURSIVE,
        false
    );

    bool can_missing = calc_missing.canCreateTreeWalker({5, 6}, 6);
    printCheck("canCreateTreeWalker returns false when shoe lacks cards",
               !can_missing);
}

void blackjack::testEdgeCalculatorTightenGap() {
    cout << "\n=== Test: EdgeCalculator tighten gap ===" << endl;

    BJRules rules = getDefaultRules();
    EdgeCalculator calc(rules, 100);
    ProbabilisticRankShoe shoe(6, 123);

    vector<double> gaps = {0.3, 0.1, 0.03, 0.01, 0.003, 0.001, 0.0003, 0.0001, 0.00003, 0.00001, 0.000003, 0.000001};
 
    calc.calculateEdge(
        shoe,
        9,
        gaps[0],
        SimAlgo::COMBO,
        true
    );

    ValueResult current = calc.getEdgeResult();
    double gap_current = current.ev_max - current.ev_min;
    cout << fixed << setprecision(6)
         << "gap_start=" << gap_current
         << " ev=" << current.ev
         << " [" << current.ev_min << ", " << current.ev_max << "]" << endl;

    int step = 0;
    
    for (double gap : gaps) {
        calc.tightenValueEstimateGap(gap, true);
        ValueResult after = calc.getEdgeResult();
        double gap_after = after.ev_max - after.ev_min;

        cout << "step=" << step
             << " target=" << gap
             << " gap=" << gap_after
             << " ev=" << after.ev
             << " [" << after.ev_min << ", " << after.ev_max << "]" << endl;

        bool gap_shrunk = gap_after <= gap_current + 1e-9;
        printCheck("gap shrunk", gap_shrunk);

        gap_current = gap_after;
        ++step;
    }


    // using function call
    double last_gap_target = gaps.back();
    auto edge_result = calculateEdge(shoe, rules, 10, 9, last_gap_target, SimAlgo::COMBO);
    cout << "Edge result using function call: " << edge_result.ev << " [" << edge_result.ev_min << ", " << edge_result.ev_max << "]" << endl;

}

void blackjack::testEdgeCalculatorCountSkew() {
    cout << "\n=== Test: EdgeCalculator count skew ===" << endl;

    BJRules rules = getDefaultRules();
    const int bet_unit = 100;
    const int sim_depth = 9;
    const double gap_target = 0.001;

    ProbabilisticRankShoe low_shoe(6);
    // burn 16 tens
    for (int i = 0; i < 16; ++i) {
        low_shoe.burnRankValue(10);
    }
    // burn 4 aces
    for (int i = 0; i < 4; ++i) {
        low_shoe.burnRankValue(11);
    }

    EdgeCalculator low_calc(rules, bet_unit);
    low_calc.calculateEdge(
        low_shoe,
        sim_depth,
        gap_target,
        SimAlgo::COMBO,
        true
    );
    ValueResult low_result = low_calc.getEdgeResult();
    cout << fixed << setprecision(6)
         << "low_count ev=" << low_result.ev
         << " [" << low_result.ev_min << ", " << low_result.ev_max << "]" << endl;

    ProbabilisticRankShoe high_shoe(6);
    for (int v : {2, 3, 4, 5, 6}) {
        // burn 4 times 
        for (int i = 0; i < 4; ++i) {
            high_shoe.burnRankValue(v);
        }
    }

    EdgeCalculator high_calc(rules, bet_unit);
    high_calc.calculateEdge(
        high_shoe,
        sim_depth,
        gap_target,
        SimAlgo::COMBO,
        true
    );
    ValueResult high_result = high_calc.getEdgeResult();
    cout << fixed << setprecision(6)
         << "high_count ev=" << high_result.ev
         << " [" << high_result.ev_min << ", " << high_result.ev_max << "]" << endl;
}
