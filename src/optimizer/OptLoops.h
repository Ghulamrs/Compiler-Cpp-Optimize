#pragma once

// **The natural loops of a flow graph**, from its dominators, as GCC's loop-init finds them: each
// back edge b -> h whose target dominates its source heads a loop made of h and every block that reaches b
// without passing through h. What a pass asks is how many loops hold a block - the weight of what happens there.

#include "OptFlow.h"

#include <vector>

namespace opt {

class Loops {
public:
    struct Loop {
        int head;
        int back;                   // the block the back edge leaves
        std::vector<int> blocks;    // the head first, then in the order found
    };

    // Computed from the flow as it stands; asks it for its dominators.
    explicit Loops(Flow &flow);

    int count() const { return static_cast<int>(loops_.size()); }
    const std::vector<Loop> &all() const { return loops_; }
    // How many loops hold the block.
    int depthOf(int block) const { return depth_[block]; }

private:
    std::vector<Loop> loops_;
    std::vector<int> depth_;

    Loop bodyOf(const Flow &flow, int head, int back) const;
};

}
