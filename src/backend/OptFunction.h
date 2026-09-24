#pragma once

// **One function as the passes see it**: the stream the walker wrote, the
// facts the walker told the optimizer about it, the flow graph over it, the
// costs of the level, and what the passes have established so far. GCC's
// `struct function` with its `cfg` and `curr_properties`, reduced to what
// cxx1 needs.

#include "OptCosts.h"
#include "OptFlow.h"
#include "OptPasses.h"
#include "Spelling.h"

#include <set>
#include <string>
#include <vector>

namespace opt {

// **Properties a pass requires, provides or destroys**, as GCC's PROP_ bits.
// The pass manager checks a pass's requirements before it runs and updates
// the set after, so a pass that reads a stale flow graph is caught at once.
enum Prop : unsigned {
    kPropFlow = 1u << 0,        // the flow graph describes the stream as it stands
    kPropPhysical = 1u << 1,    // every register named is a physical one (no pseudos)
};

struct Function {
    explicit Function(const Costs &costs) : costs_(&costs) {}

    std::string name;
    Stream stream;
    Convention convention;
    Flow flow;
    unsigned props = kPropPhysical;

    // The level's answers: what a pass asks instead of the level.
    const Costs &costs() const { return *costs_; }

    // What the walker said of the function.
    std::vector<Local> locals;      // its scalar locals, rbp-relative
    bool promotable = false;        // whether any may be kept in a register
    bool whole = true;              // the stream is all of it: no funclet cut before this
    std::set<std::string> jumpOnly; // labels named only in jumps, droppable when nothing does

    // The frame: as walked, what inlined callees added below it, and what
    // the passes add below both. The outgoing area stays under all of it.
    int prologueAt = -1;            // the prologue event's entry; -1 once written out
    int frameSize = 0;
    int inlineTop = 0;
    std::string lsda;
    int outgoing = 0;
    std::vector<SavedReg> saves;    // the callee-saved registers the passes took
    int size = 0;                   // the frame as the passes leave it

    // The frame below which the passes may place what they add.
    int frameBase() const { return frameSize > inlineTop ? frameSize : inlineTop; }

    bool has(unsigned p) const { return (props & p) == p; }

    void buildFlow() {
        flow.build(stream, convention);
        props |= kPropFlow;
    }

private:
    const Costs *costs_;
};

}
