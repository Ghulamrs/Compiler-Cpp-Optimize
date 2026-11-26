#pragma once

// **The optimizer's view of one function**: the instructions the walker wrote,
// each operand parsed once into registers, displacements and immediates, and
// kept in one ordered stream with the labels and the events between them.

#include "OptCore.h"
#include "../backend/Spelling.h"

#include <functional>
#include <string>
#include <vector>

namespace opt {

// The general registers in encoding order, then xmm0-15 as 16-31.
enum : int { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
constexpr int kGprs = 16;
constexpr int kXmm0 = 16;
constexpr int kRegs = 32;


// **rsp and rbp are the frame**: no pass narrows, renames or retargets them.
constexpr bool frameReg(int r) { return r == RSP || r == RBP; }

// A register's number and the width named, in bytes; -1 for one this does not
// model (the x87 stack). The name for a number and width, for writing back.
struct Reg {
    int id = -1;
    int width = 0;
};
Reg parseReg(Str name);
const char *regName(int id, int width);

struct Operand {
    // SegMemory and TlsSymbol are Op::Seg and Op::Tls: memory a thread reaches through its own
    // pointer. Neither is a Memory to the passes that fold or forward through one - `isMem()` says
    // no - but a TlsSymbol names its base register wherever a Memory would, so renaming reaches it.
    enum Kind { None, Register, Immediate, Memory, RipSymbol, Indirect, Label, SegMemory, TlsSymbol };
    Kind kind = None;
    Reg reg;                  // Register; Indirect's target; Memory's and TlsSymbol's base
    long long disp = 0;       // Memory
    bool hasDisp = false;
    Reg index;                // Memory: `disp(base,index,scale)`; scale 0 is none
    int scale = 0;
    long long value = 0;      // Immediate, when numeric
    bool numeric = false;
    std::string text;         // an Immediate that is not numeric, a symbol, a label

    static Operand from(const Op &o);
    static Operand ofReg(int id, int width);
    static Operand ofImm(long long v);
    static Operand ofMem(int base, long long disp);
    Op op() const;            // valid while this operand is
    bool isReg(int id) const { return kind == Register && reg.id == id; }
    bool isMem() const { return kind == Memory; }
    // Whether `reg` is a base register this operand reads: a Memory's or a TlsSymbol's.
    bool basedOnReg() const { return kind == Memory || kind == TlsSymbol; }
    bool indexed() const { return kind == Memory && scale != 0; }
};

struct Instr {
    std::string m;
    Operand a, b;             // AT&T order: a is the source, b the destination
    int operands = 0;
    // A call's argument registers as the walker placed them; without the
    // word, the convention's whole set is read.
    RegSet args = 0;
    bool exactArgs = false;
};

// Everything between functionBegin and functionEnd; an event is a spelling
// call that is not an instruction, replayed where it stood.
using Entry = EntryOf<Instr, std::function<void(Spelling &)>>;
using Stream = std::vector<Entry>;

}
