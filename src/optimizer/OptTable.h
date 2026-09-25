#pragma once

// **One description of every x86 mnemonic the optimizer knows** - what GCC
// keeps in its machine description and cxx1 kept as a dozen lists spread over
// the passes. Each mnemonic has a kind, which says how `effectsOf` reads it,
// a set of flags the passes ask about, and the width its suffix names.
//
// The table records what the passes recognised before it existed, quirks
// included: `addq` names a width but is not arithmetic here, `sal` is a
// shift but not arithmetic, `testl` reads only through its operands but is
// not marked so. Each is a candidate for a later change; none is one now.

#include <string>

namespace opt {

struct Opcode {
    // How the instruction is read for its effects: the branch classify takes.
    enum Kind {
        Unknown,        // not in the table: opaque, reads and writes everything
        Call, Ret, Jmp, Jcc, Setcc,
        Push, Pop, Leave,
        SignExtendAx,   // cqo, cdq: rax into rdx:rax
        ExtendAx,       // cltq, cdqe: eax into rax
        StringMove,     // rep movsq
        Div,            // idiv, div: rdx:rax
        Compare,        // cmp, test, ucomis: reads both, writes the flags
        Unary,          // neg, not, inc, dec: reads and writes its operand
        Move,           // a copy, an extension, lea, an SSE move
        Rmw,            // two-operand arithmetic: reads and writes b
        X87,            // f*: memory and its own stack
    };
    enum : unsigned {
        kMergeXmm = 1u << 0,    // an SSE move into a register keeps its upper lanes
        kNoFlags = 1u << 1,     // leaves the flags alone (SSE arithmetic, not)
        kExplicit = 1u << 2,    // reads registers only through its operands
        kWritesOnly = 1u << 3,  // its destination is written, not read (mov*, set*, lea)
        kImmSource = 1u << 4,   // the source may be an immediate in place of a register
        kRenamable = 1u << 5,   // a frame operand may become a register as it stands
        kShift = 1u << 6,       // a shift or rotate: the count can only be %cl
        kZeroIdiom = 1u << 7,   // of a register with itself, writes zero without reading
        kAddress = 1u << 8,     // lea: forms an address from its source
        kCvt = 1u << 9,         // a conversion between integer and floating forms
    };
    const char *name;
    Kind kind;
    unsigned flags;
    int width;                  // the width the suffix names, in bytes; 0 where only a register can

    bool has(unsigned f) const { return (flags & f) != 0; }
};

// The entry for a mnemonic; an unknown one gets `Unknown`, with the flags a
// prefix family carries (mov*, set*, cvt*, f*) so the passes see it as before.
const Opcode &opcodeOf(const std::string &m);

// The move families the passes recognise by spelling: a plain copy at eight
// bytes (mov, movq), at four too (movl), and every plain width (movw, movb).
bool isMovQ(const std::string &m);    // mov, movq
bool isMovQL(const std::string &m);   // mov, movq, movl
bool isMovAny(const std::string &m);  // mov, movq, movl, movw, movb
bool isPush(const std::string &m);    // push, pushq
bool isPop(const std::string &m);     // pop, popq

// The condition a jcc or setcc tests, and its opposite; "" for anything else.
std::string conditionOf(const std::string &m);
std::string inverse(const std::string &cc);

}
