#pragma once

// **What an instruction reads, writes and touches**, the one fact every pass
// asks. An instruction this table does not know is opaque: it reads and
// writes everything, so no pass moves anything across it.

#include "OptIr.h"
#include "OptTable.h"

struct Abi;

namespace opt {

// What a call reads and what it may leave changed, from the ABI's own table.
struct Convention {
    RegSet arguments = 0;
    RegSet clobbered = 0;
    RegSet returned = 0;
    RegSet preserved = 0;
};
Convention conventionOf(const Abi &abi);

Effects effectsOf(const Instr &i, const Convention &c);

// **What an instruction does with each operand it names**: read it, write it,
// write part of it and keep the rest, or only form an address from it.
constexpr unsigned kRead = 1, kWrite = 2, kKeep = 4, kAddress = 8;
struct Roles {
    unsigned a = 0, b = 0;
};
Roles rolesOf(const Instr &i);

// Whether an instruction reads registers only through its operands.
bool explicitOnly(const Instr &i);

}
