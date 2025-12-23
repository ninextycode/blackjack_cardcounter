#include <gtest/gtest.h>
#include "edge.h"
#include "rules.h"
#include "shoe.h"
#include "tree_walker.h"
#include "dealer_sim.h"

using namespace blackjack;


class EdgeCalculatorTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        PreloadedComboData::loadAll(
            "/home/maxim//Programming/blackjack_cardcounter/combinations/", 11
        );
    }

    EdgeCalculatorTest() : 
        rules(getDefaultRules()), 
        calc(rules, 100) 
    {}

    BJRules rules;
    EdgeCalculator calc;
};


TEST_F(EdgeCalculatorTest, Basic) {
    ProbabilisticRankShoe shoe(6);
    calc.calculateEdge(shoe, 9, 0.001, SimAlgo::COMBO, true);
    ValueResult result = calc.getEdgeResult();

    double expected_ev = -0.28;
    EXPECT_NEAR(result.ev, expected_ev, 0.01);


    // AA hand vs dealer A
    TreeWalker walker = calc.createTreeWalker({11, 11}, 11);
    walker.tightenValueEstimateGap(0.001);
    
    // value 10.4861
    EXPECT_NEAR(walker.getValueEstimate().ev, 10.49, 0.01);

    auto best_action = walker.getBestAction();
    ASSERT_TRUE(best_action.has_value());
    EXPECT_EQ(*best_action, PlayerAction::REFUSE_INSURANCE);

    walker.takePlayerAction(PlayerAction::REFUSE_INSURANCE);
    walker.takeDealerAction(DealerAction::CONFIRM_NO_BLACKJACK);

    // value 60.2803
    EXPECT_NEAR(walker.getValueEstimate().ev, 60.28, 0.01);

    best_action = walker.getBestAction();
    ASSERT_TRUE(best_action.has_value());
    EXPECT_EQ(*best_action, PlayerAction::SPLIT);
    walker.takePlayerAction(PlayerAction::SPLIT);
    walker.takeCard(8);
    walker.takeCard(7);
    // A8 and A7 after split

    best_action = walker.getBestAction();
    ASSERT_TRUE(best_action.has_value());
    EXPECT_EQ(*best_action, PlayerAction::STAND);  // A8
    walker.takePlayerAction(PlayerAction::STAND);
    
    best_action = walker.getBestAction();
    ASSERT_TRUE(best_action.has_value());
    EXPECT_EQ(*best_action, PlayerAction::HIT);  // A7

}


TEST_F(EdgeCalculatorTest, LowCount) {
    ProbabilisticRankShoe shoe(6);

    // burn 16 tens
    for (int i = 0; i < 16; ++i) {
        shoe.burnRankValue(10);
    }
    // burn 4 aces
    for (int i = 0; i < 4; ++i) {
        shoe.burnRankValue(11);
    }

    calc.calculateEdge(shoe, 9, 0.001, SimAlgo::COMBO, true);
    ValueResult result = calc.getEdgeResult();
    double expected_ev = -2.0175;
    EXPECT_NEAR(result.ev, expected_ev, 0.01);
}


TEST_F(EdgeCalculatorTest, HighCount) {
    ProbabilisticRankShoe shoe(6);

    // burn 2, 3, 4, 5, 6
    for (int v : {2, 3, 4, 5, 6}) {
        // burn 4 times
        for (int i = 0; i < 4; ++i) {
            shoe.burnRankValue(v);
        }
    }

    calc.calculateEdge(shoe, 9, 0.001, SimAlgo::COMBO, true);
    double expected_ev = 1.47;
    EXPECT_NEAR(calc.getEdgeResult().ev, expected_ev, 0.01);
}