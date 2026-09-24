#include "OptFlow.h"

namespace opt {

// A jcc falls through and jmp does not; ret leaves, and jmp through a
// register goes where nothing here can follow.
Control controlOf(const Instr &i) {
    Control c;
    if (i.m == "ret") { c.ends = true; c.falls = false; c.returns = true; return c; }
    if (i.m == "call") { c.calls = true; return c; }
    if (i.m[0] != 'j') return c;
    c.ends = true;
    c.falls = i.m != "jmp";
    if (i.a.kind == Operand::Label) c.target = i.a.text;
    return c;
}

}
