#include "floor_ceil_node.h"
#include <gtest/gtest.h>
#include "rules.h"
#include "shoe.h"
#include "dealer_sim.h"

using namespace blackjack;

class FloorCeilNodeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        PreloadedComboData::loadAll(
            "/home/maxim//Programming/blackjack_cardcounter/combinations/", 11
        );
    }

    FloorCeilNodeTest() : 
        rules(make_shared<BJRules>(getDefaultRules()))
    {}

    shared_ptr<BJRules> rules;
};

TEST_F(FloorCeilNodeTest, AAvA) {
    ProbabilisticRankShoe shoe(6);
    BJRound bj_round(rules);
    bj_round.startRound(100);
    
    bj_round.takeCard(11);
    bj_round.takeCard(11);
    bj_round.takeCard(11);
    shoe.burnRankValue(11);
    shoe.burnRankValue(11);
    shoe.burnRankValue(11);
    
    auto root_node = buildNonFinalRootNode(
        bj_round,
        shoe,
        0, // max_hand_size_full_enum
        9, // dealer_sim_depth
        SimAlgo::COMBO
    );
    
    root_node->buildTree();
    root_node->convertToFullUpToGap(0.001);

    double expected_value = 10.4875;
    EXPECT_NEAR(root_node->getValue(), expected_value, 0.01);
}