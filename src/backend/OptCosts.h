#pragma once

// **What each level does, in one place.** -O1 is small code and -O2 is fast
// code, and both run the same pipeline: where a pass has two spellings of
// the same result, -O1 takes the smaller and -O2 the faster. That is how
// cl's /O1 and /O2 part (/Os against /Ot), and how GCC's -Os relates to -O2
// (the same passes, size costs in place of speed costs); GCC's own -O1 is a
// subset of passes instead, which cxx1 does not follow.
//
// Every number a pass takes from the level is a question asked of a Costs,
// so a pass never tests the level itself. The two levels are the two
// classes below behind one interface: a pass that grows code (an inlined
// body, a cmov where a branch is shorter) is gated by what the costs
// answer, not switched off by level.

#include <memory>

namespace opt {

class Costs {
public:
    virtual ~Costs() {}

    int level() const { return level_; }
    // Whether size is the cost (-O1) or speed is (-O2).
    virtual bool forSize() const = 0;

    // The rounds a pass group repeats until one finds nothing.
    virtual int rounds() const = 0;
    // Callee-saved registers locals may be kept in, and the accesses,
    // loop-weighted, that earn one. Speed spends registers freely; size
    // asks that the saves pay for themselves.
    virtual int registers() const = 0;
    virtual long minWeight() const = 0;
    // A block of three words or more copied by `rep movsq` (smaller) rather
    // than unrolled moves (faster).
    virtual bool stringCopies() const = 0;

    // The costs of a level, 1 or 2.
    static std::unique_ptr<Costs> forLevel(int level);

protected:
    explicit Costs(int level) : level_(level) {}

private:
    int level_;
};

// **-O1: size.** Nothing is done that does not make the code smaller.
class SizeCosts final : public Costs {
public:
    SizeCosts() : Costs(1) {}
    bool forSize() const override { return true; }
    int rounds() const override { return 8; }
    int registers() const override { return 2; }
    long minWeight() const override { return 6; }
    bool stringCopies() const override { return true; }
};

// **-O2: speed.** Registers spent freely, and code grown where time is saved.
//
// **No loop-head alignment**, although cl pads with npad: measured on the
// box with and without `.balign 16` before every loop head - Compiler++'s
// bench 843 against 844 ms over 15 interleaved rounds, loops.cpp's five
// kernels equal to the millisecond - for 3,904 bytes. Nothing to buy, so
// no question for it.
class SpeedCosts final : public Costs {
public:
    SpeedCosts() : Costs(2) {}
    bool forSize() const override { return false; }
    int rounds() const override { return 16; }
    int registers() const override { return 5; }
    long minWeight() const override { return 2; }
    bool stringCopies() const override { return false; }
};

inline std::unique_ptr<Costs> Costs::forLevel(int level) {
    if (level <= 1) return std::unique_ptr<Costs>(new SizeCosts());
    return std::unique_ptr<Costs>(new SpeedCosts());
}

}
