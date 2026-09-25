#pragma once

// **A scalar local leaves its frame slot for a pseudo** - GCC's into-SSA of
// a non-addressable local, reduced to a rename: every access names the
// pseudo, and the allocator decides its register, or gives the slot back.

#include "Mir.h"

namespace mir {

class Locals {
public:
    explicit Locals(opt::Function &fn) : fn_(fn) {}

    // Every promotable local renamed to a new pseudo, its slot kept in Function::slots; how many were.
    int promote();

private:
    opt::Function &fn_;
};

}
