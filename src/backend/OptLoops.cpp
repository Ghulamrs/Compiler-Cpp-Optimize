#include "OptLoops.h"

namespace opt {

Loops::Loops(Flow &flow) : depth_(flow.blocks.size(), 0) {
    flow.dominators();
    for (const Edge &e : flow.edges) {
        if (e.to == Edge::kExit || e.kind == Edge::Eh || !flow.dominates(e.to, e.from)) continue;
        loops_.push_back(bodyOf(flow, e.to, e.from));
        for (int b : loops_.back().blocks) ++depth_[b];
    }
}

// The head, and everything that reaches the back edge's source without
// passing the head: a walk backward over the predecessors from there.
Loops::Loop Loops::bodyOf(const Flow &flow, int head, int back) const {
    Loop loop;
    loop.head = head;
    loop.back = back;
    std::vector<bool> in(flow.blocks.size(), false);
    in[head] = true;
    loop.blocks.push_back(head);
    std::vector<int> work;
    if (!in[back]) { in[back] = true; loop.blocks.push_back(back); work.push_back(back); }
    while (!work.empty()) {
        const int b = work.back();
        work.pop_back();
        for (int p : flow.predBlocks(b))
            if (!in[p]) { in[p] = true; loop.blocks.push_back(p); work.push_back(p); }
    }
    return loop;
}

}
