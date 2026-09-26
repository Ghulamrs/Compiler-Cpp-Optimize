#pragma once

// **Dataflow problems over the flow graph**, after GCC's df: each problem is solved on request, over the blocks and edges the flow holds, and its solution stands until something changes the stream.
// Liveness lives in the flow itself (Flow::live), since every pass asks it and it is what the blocks' in/out are. This file holds the problems only some passes ask for.
// Session 2 adds def-use chains here, built from reaching definitions as GCC builds them.

#include "OptFlow.h"

#include <vector>

namespace opt {

// **Reaching definitions**, one register at a time: the definitions of r that may hold its value on entry to each block.
// A definition is a number the caller chose; the caller says which definition each entry leaves in each register (`lastDef[k][r]`, or -1),
// and which definition stands for what a block entered from nowhere this function shows starts with (`unknownIn[b][r]`, or -1).
struct ReachingDefs {
    typedef std::vector<int> DefList;
    std::vector<std::vector<DefList>> in;      // [block][register], sorted, unique

    void solve(const Flow &f, const Stream &s, int registers,
               const std::vector<std::vector<int>> &lastDef,
               const std::vector<std::vector<int>> &unknownIn);
};

}
