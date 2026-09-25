#pragma once

// The passes over one function. Each says whether it changed anything, so the
// driver can run them until nothing does.

#include "OptFlow.h"

namespace opt {

// What nobody reads, a sign extension included when only its low half is read.
bool removeDead(Stream &s, Flow &f);

// Code no label leads to after a jump or return, and a jump to the next label.
bool removeUnreachable(Stream &s);

// **A copy out of a dying register** folds into the instruction that wrote it.
bool coalesceCopies(Stream &s, Flow &f, const Convention &c);

// The shortest encoding of what is read after it: no REX where no upper half is read.
bool shrink(Stream &s, Flow &f, const Convention &c);

// **A load read once** becomes that instruction's memory operand.
bool foldLoads(Stream &s, Flow &f, const Convention &c);

// **A constant added to a register only used as an address** moves into the displacements.
bool foldOffsets(Stream &s, Flow &f, const Convention &c);

// **A register added to one only used as an address** becomes its index, a shift of it the scale.
bool foldIndex(Stream &s, Flow &f, const Convention &c);

// **A division by a constant is a multiply by its magic number** - see OptDivide.cpp.
bool divideByConstant(Stream &s, Flow &f);

struct Function;
// **A loop-invariant computation moves in front of the loop**, into a register dead there - see OptHoist.cpp.
bool hoistInvariants(Function &fn);

// **A loop that fits one line is padded in front so that it does not cross one** - see OptAlign.cpp.
bool alignLoops(Function &fn);

// **A jump that only jumps on, and a constant that decides the compare-and-branch it reaches** - see OptJumps.cpp.
bool threadJumps(Function &fn);

// A local the walker placed in the frame, rbp-relative.
struct Local {
    long long disp;
    int size;
};

// **Frame slots something outside the stream reads or writes** - a funclet
// of the function, or the runtime - as if their address had escaped: no
// register takes one, and a store to one is never dead. The two ask
// differently, as they do of the stream itself: promotion asks whether the
// slot is touched, the object at an address taken being that object alone;
// dead-store removal asks whether the store may be reached, an address
// taken reaching anything above it in the frame.
class SharedSlots {
public:
    void add(long long disp, int width) { slots_.push_back(Local{disp, width}); }
    void addressTaken(long long disp) {
        add(disp, 1);
        escapesFrom_ = escapesFrom_ < disp ? escapesFrom_ : disp;
    }
    // Every frame slot the stream reads, writes or takes the address of.
    void addAccessesOf(const Stream &s);
    // Whether an access of `width` bytes at `disp` touches a shared slot.
    bool overlaps(long long disp, int width) const {
        for (const Local &l : slots_)
            if (disp < l.disp + l.size && l.disp < disp + width) return true;
        return false;
    }
    // Whether a store of `width` bytes at `disp` may be read outside the stream.
    bool mayReach(long long disp, int width) const {
        return (escapesFrom_ < 0 && disp + width > escapesFrom_) || overlaps(disp, width);
    }

private:
    std::vector<Local> slots_;
    long long escapesFrom_ = 0;
};

// **The scalar locals a register could hold**: not shared, never addressed,
// every access whole and by an instruction that takes a register there;
// none where the frame escapes or setjmp is called. `mentioned`: every register named.
std::vector<Local> promotableLocals(const Stream &s, const std::vector<Local> &locals, const SharedSlots &shared,
                                    RegSet &mentioned);

// Each register goes back before rsp is taken from the frame for the return, ahead of the epilogue.
void insertRestores(Stream &s, const std::vector<SavedReg> &saves);

// **A frame store never read back**, where no address reaches it; whole functions only.
bool removeDeadStores(Stream &s, const SharedSlots &shared);

// A register saved for a local that no longer names it, and its restores, go.
void dropUnusedSaves(Stream &s, std::vector<SavedReg> &saves, long long top);

// **What each register holds, followed forward through a block** - see OptValues.cpp.
bool forwardValues(Stream &s, Flow &f, const Convention &c, long long tempFrom = 0);

}
