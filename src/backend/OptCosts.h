#pragma once

// **What each level does, in one place.** -O1 is small code and -O2 is fast
// code, and both run the same pipeline: where a pass has two spellings of
// the same result, -O1 takes the smaller and -O2 the faster. That is how
// cl's /O1 and /O2 part (/Os against /Ot), and how GCC's -Os relates to -O2
// (the same passes, size costs in place of speed costs); GCC's own -O1 is a
// subset of passes instead, which cxx1 does not follow.
//
// Every number a pass takes from the level is a field here, so a pass never
// tests the level itself. A pass that grows code (an inlined body, a cmov
// where a branch is shorter) will be gated by a cost read from here, not
// switched off by level.

namespace opt {

struct Costs {
    int level = 0;          // 1 or 2; 0 means no optimizer stands in front
    bool forSize = true;    // -O1: size is the cost; -O2: speed is

    // The rounds a pass group repeats until one finds nothing.
    int rounds = 0;
    // Callee-saved registers locals may be kept in, and the accesses,
    // loop-weighted, that earn one. Speed spends registers freely; size
    // asks that the saves pay for themselves.
    int registers = 0;
    long minWeight = 0;
    // A block of three words or more copied by `rep movsq` (smaller) rather
    // than unrolled moves (faster).
    bool stringCopies = false;

    // **No loop-head alignment at -O2**, although cl pads with npad: measured
    // on the box with and without `.balign 16` before every loop head -
    // Compiler++'s bench 843 against 844 ms over 15 interleaved rounds,
    // loops.cpp's five kernels equal to the millisecond - for 3,904 bytes.
    // Nothing to buy, so no field for it.

    static Costs forLevel(int level) {
        Costs c;
        c.level = level;
        c.forSize = level <= 1;
        if (level <= 1) { c.rounds = 8; c.registers = 2; c.minWeight = 6; c.stringCopies = true; }
        else { c.rounds = 16; c.registers = 5; c.minWeight = 2; c.stringCopies = false; }
        return c;
    }
};

}
