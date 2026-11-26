#pragma once

// **A scalar local leaves its frame slot for a pseudo** - GCC's into-SSA of
// a non-addressable local, reduced to a rename: every access names the
// pseudo, and the allocator decides its register, or gives the slot back.

#include "Mir.h"

namespace mir {

class Locals {
public:
    explicit Locals(opt::Function &fn) : fn_(fn) {}

    int promote();  // promotable locals renamed to pseudos; how many

private:
    opt::Function &fn_;
};

}
