#include "OptPasses.h"

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }
bool reg64(const Operand &o) { return gpr(o) && o.reg.width == 8; }
bool is(const std::string &m, std::initializer_list<const char *> names) {
    for (const char *n : names) if (m == n) return true;
    return false;
}

// An array index scaled by an element of one byte.
bool scaleByOne(const Instr &i) {
    return i.m == "imul" && i.operands == 2 && i.a.kind == Operand::Immediate && i.a.numeric && i.a.value == 1 && gpr(i.b);
}

// **The same result in fewer bytes**, given what is read after it: a write of
// four bytes zeroes the upper half, so where nobody reads that half, or the
// value is a small non-negative constant, the REX prefix goes.
bool shorter(Instr &i, RegSet wide, bool flagsLive) {
    if (gpr(i.b) && frameReg(i.b.reg.id)) return false;
    // A power of two is a shift, which is quicker and no longer.
    const long long v = i.a.value;
    if (i.m == "imul" && i.operands == 2 && i.a.kind == Operand::Immediate && i.a.numeric && gpr(i.b) &&
        !flagsLive && v > 1 && v <= (1LL << 30) && (v & (v - 1)) == 0) {
        int n = 0;
        while ((1LL << n) != v) ++n;
        i = Instr{"shl", Operand::ofImm(n), i.b, 2};
        return true;
    }
    if (is(i.m, {"movzbq", "movzwq"}) && reg64(i.b)) {
        i.m[i.m.size() - 1] = 'l';
        i.b.reg.width = 4;
        return true;
    }
    if (gpr(i.b) && i.b.reg.width == 4 && is(i.m, {"mov", "movl"}) && i.a.kind == Operand::Immediate &&
        i.a.numeric && i.a.value == 0 && !flagsLive) {
        i = Instr{"xor", i.b, i.b, 2};
        return true;
    }
    if (!reg64(i.b)) return false;
    const bool upperRead = (wide & bit(i.b.reg.id)) != 0;
    if (isMovQ(i.m) && i.a.kind == Operand::Immediate && i.a.numeric) {
        const long long v = i.a.value;
        if (v == 0 && !flagsLive) { i = Instr{"xor", Operand::ofReg(i.b.reg.id, 4), Operand::ofReg(i.b.reg.id, 4), 2}; return true; }
        if ((v >= 0 && v <= 0x7fffffffLL) || (!upperRead && v == static_cast<int>(v))) { i.m = "mov"; i.b.reg.width = 4; return true; }
        return false;
    }
    if (upperRead) return false;
    // Arithmetic whose low half depends on the low halves alone.
    const bool arith = is(i.m, {"add", "sub", "and", "or", "xor", "imul"}) && i.operands == 2 && !flagsLive;
    if (arith && ((i.a.kind == Operand::Immediate && i.a.numeric && i.a.value == static_cast<int>(i.a.value)) || reg64(i.a))) {
        if (reg64(i.a)) i.a.reg.width = 4;
        i.b.reg.width = 4;
        return true;
    }
    if (i.m == "movslq" && i.a.isMem()) { i.m = "movl"; i.b.reg.width = 4; return true; }
    if (i.m == "movslq" && gpr(i.a) && i.a.reg.id != i.b.reg.id) { i.m = "mov"; i.b.reg.width = 4; return true; }
    if (isMovQ(i.m) && i.a.isMem()) { i.m = "movl"; i.b.reg.width = 4; return true; }
    return false;
}

}

bool shrink(Stream &s, Flow &f, const Convention &c) {
    f.solve(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet wide = blk.wideOut;
        bool flags = blk.flagsOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &en = s[k];
            if (en.kind != Entry::Ins || en.dead) continue;
            if (scaleByOne(en.ins) && !flags) { en.dead = changed = true; continue; }
            if (shorter(en.ins, wide, flags)) { f.effects[k] = effectsOf(en.ins, c); changed = true; }
            const Effects &e = f.effects[k];
            wide = (wide & ~e.writes) | e.wide;
            flags = e.flagsRead || (flags && !e.flagsWritten);
        }
    }
    return changed;
}

}
