#pragma once

// **x86's instructions in the shared flow graph**: how each moves control, and
// the flow built from the effects table under a calling convention.

#include "OptEffects.h"

namespace opt {

Control controlOf(const Instr &i);

struct Flow : FlowOf<Entry> {
    void build(const Stream &s, const Convention &c, const std::vector<Region> &regions) {
        FlowOf<Entry>::build(s, [&c](const Instr &i) { return effectsOf(i, c); }, regions);
    }
};

}
