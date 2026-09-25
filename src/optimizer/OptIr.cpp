#include "OptIr.h"

#include <cstdlib>
#include <cstring>

namespace opt {

namespace {

// The four names of each general register, by width: 8, 4, 2 and 1 bytes.
const char *const kNames[kGprs][4] = {
    {"%rax", "%eax", "%ax", "%al"},     {"%rcx", "%ecx", "%cx", "%cl"},
    {"%rdx", "%edx", "%dx", "%dl"},     {"%rbx", "%ebx", "%bx", "%bl"},
    {"%rsp", "%esp", "%sp", "%spl"},    {"%rbp", "%ebp", "%bp", "%bpl"},
    {"%rsi", "%esi", "%si", "%sil"},    {"%rdi", "%edi", "%di", "%dil"},
    {"%r8", "%r8d", "%r8w", "%r8b"},    {"%r9", "%r9d", "%r9w", "%r9b"},
    {"%r10", "%r10d", "%r10w", "%r10b"}, {"%r11", "%r11d", "%r11w", "%r11b"},
    {"%r12", "%r12d", "%r12w", "%r12b"}, {"%r13", "%r13d", "%r13w", "%r13b"},
    {"%r14", "%r14d", "%r14w", "%r14b"}, {"%r15", "%r15d", "%r15w", "%r15b"},
};
const char *const kXmmNames[16] = {
    "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7",
    "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15",
};
const int kWidths[4] = {8, 4, 2, 1};

bool same(Str s, const char *name) {
    return s.n == std::strlen(name) && std::memcmp(s.p, name, s.n) == 0;
}

}

Reg parseReg(Str name) {
    for (int r = 0; r < kGprs; ++r)
        for (int w = 0; w < 4; ++w)
            if (same(name, kNames[r][w])) return Reg{r, kWidths[w]};
    for (int x = 0; x < 16; ++x)
        if (same(name, kXmmNames[x])) return Reg{kXmm0 + x, 16};
    return Reg{};
}

const char *regName(int id, int width) {
    if (id >= kXmm0) return kXmmNames[id - kXmm0];
    for (int w = 0; w < 4; ++w)
        if (kWidths[w] == width) return kNames[id][w];
    return kNames[id][0];
}

Operand Operand::from(const Op &o) {
    Operand x;
    switch (o.kind) {
    case Op::Reg:
        x.reg = parseReg(o.text);
        x.kind = Register;
        if (x.reg.id < 0) x.text = std::string(o.text);  // %st(0) and the like
        break;
    case Op::Imm:
        x.kind = Immediate;
        x.numeric = o.immNumeric;
        if (o.immNumeric)
            x.value = o.immNeg ? -static_cast<long long>(o.uimm) : static_cast<long long>(o.uimm);
        else x.text = std::string(o.text);
        // `immText("8")` is a number too, when it spells back the same.
        if (!o.immNumeric && !x.text.empty() && x.text.find_first_not_of("-0123456789") == std::string::npos) {
            const long long v = std::strtoll(x.text.c_str(), nullptr, 10);
            if (std::to_string(v) == x.text) { x.numeric = true; x.value = v; x.text.clear(); }
        }
        break;
    case Op::Mem:
        x.kind = Memory;
        x.reg = parseReg(o.text);
        x.disp = o.disp;
        x.hasDisp = o.hasDisp;
        if (x.reg.id < 0) x.text = std::string(o.text);
        if (o.scale != 0) { x.index = parseReg(o.index); x.scale = o.scale; }
        break;
    case Op::Rip: x.kind = RipSymbol; x.text = std::string(o.text); break;
    case Op::Ind:
        x.kind = Indirect;
        x.reg = parseReg(o.text);
        if (x.reg.id < 0) x.text = std::string(o.text);
        break;
    case Op::Lbl: x.kind = Label; x.text = std::string(o.text); break;
    }
    return x;
}

Operand Operand::ofReg(int id, int width) {
    Operand x;
    x.kind = Register;
    x.reg = Reg{id, width};
    return x;
}

Operand Operand::ofImm(long long v) {
    Operand x;
    x.kind = Immediate;
    x.value = v;
    x.numeric = true;
    return x;
}

Operand Operand::ofMem(int base, long long disp) {
    Operand x;
    x.kind = Memory;
    x.reg = Reg{base, 8};
    x.disp = disp;
    x.hasDisp = disp != 0;
    return x;
}

Op Operand::op() const {
    const Str name = reg.id >= 0 ? Str(regName(reg.id, reg.width)) : Str(text);
    switch (kind) {
    case Register: return ::reg(name);
    case Immediate: return numeric ? ::imm(value) : immText(text);
    case Memory: {
        const Str base = reg.id >= 0 ? Str(regName(reg.id, 8)) : Str(text);
        if (scale != 0) return memIndexed(disp, hasDisp, base, Str(regName(index.id, 8)), scale);
        return hasDisp ? mem(disp, base) : mem(base);
    }
    case RipSymbol: return rip(text);
    case Indirect: return ind(name);
    case Label: return lbl(text);
    case None: break;
    }
    return immText("");
}

}
