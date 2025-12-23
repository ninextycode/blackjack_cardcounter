#include <string>
#include "abstract_node.h"


using namespace std;


namespace blackjack {
    void testSampling();
    string eventToString(const TransitionEvent& event);
    void printTreeStructure(
        AbstractBJTreeNode* node,
        int max_depth = -1,
        int current_depth = 0,
        const string& prefix = "",
        bool is_last = true
    );
    void printTreeStatistics(AbstractBJTreeNode* root);
    void testAcePair();
    void testEdge();
    void testEdgeTiming();
    void testEv();
    void testDealerSim();
    void testEdgeTimingWithGap();
    void testTreeWalker();
    void testTreeWalkerWithCards();
    void testTreeWalkerSplit();
    void testTreeWalkerAAvA();


    void testEdgeCalculatorBasic();
    void testEdgeCalculatorCache();
    void testEdgeCalculatorTightenGap();
    void testEdgeCalculatorCountSkew();
}
